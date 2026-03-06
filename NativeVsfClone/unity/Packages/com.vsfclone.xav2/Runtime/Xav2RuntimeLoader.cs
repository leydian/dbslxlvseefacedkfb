using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using UnityEngine;

namespace VsfClone.Xav2.Runtime
{
    public static class Xav2RuntimeLoader
    {
        private const ushort SectionTextureBlob = 0x0002;
        private const ushort SectionMaterialOverride = 0x0003;
        private const ushort SectionMeshRenderPayload = 0x0011;
        private const ushort SectionMaterialShaderParams = 0x0012;
        private const ushort SectionSkinPayload = 0x0013;
        private const ushort SectionBlendShapePayload = 0x0014;
        private const ushort SectionMaterialTypedParams = 0x0015;
        private const ushort SectionSkeletonPosePayload = 0x0016;
        private const ushort SectionSkeletonRigPayload = 0x0017;
        private const ushort SectionSpringBonePayload = 0x0018;
        private const ushort SectionPhysBonePayload = 0x0019;
        private const ushort SectionPhysicsColliderPayload = 0x001A;
        private const ushort SectionFlagPayloadCompressedLz4 = 0x0001;
        private const ushort SectionFlagKnownMask = SectionFlagPayloadCompressedLz4;

        /// <summary>
        /// Loads an XAV2 avatar payload from disk and throws on failure.
        /// </summary>
        public static Xav2AvatarPayload Load(string path)
        {
            if (TryLoad(path, out var payload, out var diagnostics))
            {
                return payload;
            }
            throw BuildLoadException(path, diagnostics);
        }

        /// <summary>
        /// Loads an XAV2 avatar payload from disk without throwing.
        /// </summary>
        public static bool TryLoad(string path, out Xav2AvatarPayload payload, out Xav2LoadDiagnostics diagnostics)
        {
            return TryLoad(path, out payload, out diagnostics, new Xav2LoadOptions());
        }

        /// <summary>
        /// Loads an XAV2 avatar payload from disk using explicit loader options.
        /// </summary>
        public static bool TryLoad(
            string path,
            out Xav2AvatarPayload payload,
            out Xav2LoadDiagnostics diagnostics,
            Xav2LoadOptions options)
        {
            payload = new Xav2AvatarPayload();
            diagnostics = new Xav2LoadDiagnostics();
            options ??= new Xav2LoadOptions();

            byte[] bytes;
            try
            {
                bytes = File.ReadAllBytes(path);
            }
            catch (Exception ex)
            {
                return Fail(
                    diagnostics,
                    Xav2LoadErrorCode.IoError,
                    $"I/O error while reading '{path}': {ex.Message}");
            }

            var cursor = 0;
            if (!TryReadAscii(bytes, ref cursor, 4, out var magic))
            {
                return Fail(diagnostics, Xav2LoadErrorCode.MagicMismatch, "XAV2 header is truncated.");
            }
            if (!string.Equals(magic, "XAV2", StringComparison.Ordinal))
            {
                return Fail(diagnostics, Xav2LoadErrorCode.MagicMismatch, "XAV2 magic mismatch.");
            }
            diagnostics.ParserStage = "parse";

            if (!TryReadUInt16(bytes, ref cursor, out var version))
            {
                return Fail(diagnostics, Xav2LoadErrorCode.UnsupportedVersion, "XAV2 version field is truncated.");
            }
            if (version != 1 && version != 2 && version != 3 && version != 4 && version != 5)
            {
                return Fail(diagnostics, Xav2LoadErrorCode.UnsupportedVersion, $"Unsupported XAV2 version: {version}");
            }
            diagnostics.SourceFormatVersion = version;

            if (!TryReadUInt32(bytes, ref cursor, out var manifestSize))
            {
                return Fail(diagnostics, Xav2LoadErrorCode.ManifestTruncated, "Manifest size field is truncated.");
            }
            if (manifestSize > (uint)(bytes.Length - cursor))
            {
                return Fail(diagnostics, Xav2LoadErrorCode.ManifestTruncated, "Manifest bytes are out of file range.");
            }
            if (!TryReadUtf8(bytes, ref cursor, (int)manifestSize, out var manifestJson))
            {
                return Fail(diagnostics, Xav2LoadErrorCode.ManifestTruncated, "Manifest payload is truncated.");
            }

            Xav2Manifest manifest;
            try
            {
                manifest = JsonUtility.FromJson<Xav2Manifest>(manifestJson) ?? new Xav2Manifest();
            }
            catch (Exception ex)
            {
                return Fail(diagnostics, Xav2LoadErrorCode.ManifestInvalid, $"Manifest JSON parse failed: {ex.Message}");
            }
            payload.Manifest = manifest;
            NormalizeManifest(manifest);
            diagnostics.SourceMaterialParamEncoding = manifest.materialParamEncoding ?? "legacy-json";
            if (string.IsNullOrWhiteSpace(manifest.avatarId) ||
                manifest.meshRefs.Count == 0 ||
                manifest.materialRefs.Count == 0 ||
                manifest.textureRefs.Count == 0)
            {
                return Fail(
                    diagnostics,
                    Xav2LoadErrorCode.MissingRequiredManifestKeys,
                    "Manifest requires avatarId/meshRefs/materialRefs/textureRefs.");
            }
            diagnostics.ParserStage = "resolve";

            var materialsByName = new Dictionary<string, Xav2MaterialPayload>(StringComparer.OrdinalIgnoreCase);
            while (cursor < bytes.Length)
            {
                diagnostics.ParserStage = "payload";
                if ((bytes.Length - cursor) < 8)
                {
                    return Fail(
                        diagnostics,
                        Xav2LoadErrorCode.SectionHeaderTruncated,
                        $"Section header is truncated at offset {cursor}.");
                }

                if (!TryReadUInt16(bytes, ref cursor, out var sectionType) ||
                    !TryReadUInt16(bytes, ref cursor, out var sectionFlags) ||
                    !TryReadUInt32(bytes, ref cursor, out var sectionSize))
                {
                    return Fail(
                        diagnostics,
                        Xav2LoadErrorCode.SectionHeaderTruncated,
                        $"Section header is truncated at offset {cursor}.");
                }
                if (sectionSize > (uint)(bytes.Length - cursor))
                {
                    return Fail(
                        diagnostics,
                        Xav2LoadErrorCode.SectionTruncated,
                        $"Section payload is truncated for type 0x{sectionType:X4}.");
                }

                var sectionOffset = cursor;
                var sectionLength = (int)sectionSize;
                cursor += sectionLength;

                byte[] sectionBytes = null;
                if ((sectionFlags & SectionFlagPayloadCompressedLz4) != 0)
                {
                    if (version < 5)
                    {
                        return Fail(
                            diagnostics,
                            Xav2LoadErrorCode.SectionSchemaInvalid,
                            $"Compressed section is not supported for XAV2 version {version}: type=0x{sectionType:X4}.");
                    }

                    if (!TryDecodeCompressedSection(
                            bytes,
                            sectionOffset,
                            sectionLength,
                            out sectionBytes,
                            out var compressionError))
                    {
                        return Fail(
                            diagnostics,
                            Xav2LoadErrorCode.CompressionDecodeFailed,
                            $"Compressed section decode failed for type 0x{sectionType:X4}: {compressionError}");
                    }
                }

                var unknownFlags = (ushort)(sectionFlags & ~SectionFlagKnownMask);
                if (unknownFlags != 0)
                {
                    if (!AddWarningOrFail(
                            diagnostics,
                            options,
                            $"XAV2_SECTION_FLAGS_NONZERO: type=0x{sectionType:X4}, flags={unknownFlags}"))
                    {
                        return false;
                    }
                }

                var parseBytes = sectionBytes ?? bytes;
                var parseOffset = sectionBytes != null ? 0 : sectionOffset;
                var parseLength = sectionBytes != null ? sectionBytes.Length : sectionLength;

                if (!TryParseSection(
                        sectionType,
                        parseBytes,
                        parseOffset,
                        parseLength,
                        version,
                        payload,
                        materialsByName,
                        diagnostics,
                        options))
                {
                    return false;
                }
            }

            foreach (var mat in materialsByName.Values)
            {
                payload.Materials.Add(mat);
            }

            if (!CanonicalizeMaterialPayloads(payload, diagnostics, options))
            {
                return false;
            }

            if (!EvaluatePartialCompatibility(payload, diagnostics, options, version))
            {
                return false;
            }
            diagnostics.ParserStage = "runtime-ready";
            return true;
        }

        private static bool TryDecodeCompressedSection(
            byte[] bytes,
            int sectionOffset,
            int sectionLength,
            out byte[] decoded,
            out string error)
        {
            decoded = Array.Empty<byte>();
            error = string.Empty;
            if (sectionLength < 4)
            {
                error = "compressed section envelope is truncated";
                return false;
            }

            var expectedLength = BitConverter.ToInt32(bytes, sectionOffset);
            if (expectedLength < 0)
            {
                error = $"invalid uncompressed size: {expectedLength}";
                return false;
            }

            var payloadLength = sectionLength - 4;
            var compressed = new byte[payloadLength];
            if (payloadLength > 0)
            {
                Buffer.BlockCopy(bytes, sectionOffset + 4, compressed, 0, payloadLength);
            }

            if (!Xav2Lz4Codec.TryDecompress(compressed, expectedLength, out decoded))
            {
                error = $"LZ4 payload decode failed (compressed={payloadLength}, expected={expectedLength})";
                return false;
            }
            return true;
        }

        private static bool TryParseSection(
            ushort sectionType,
            byte[] bytes,
            int sectionOffset,
            int sectionLength,
            int formatVersion,
            Xav2AvatarPayload payload,
            Dictionary<string, Xav2MaterialPayload> materialsByName,
            Xav2LoadDiagnostics diagnostics,
            Xav2LoadOptions options)
        {
            switch (sectionType)
            {
                case SectionMeshRenderPayload:
                    return TryParseMesh(bytes, sectionOffset, sectionLength, payload, diagnostics, options);
                case SectionTextureBlob:
                    return TryParseTexture(bytes, sectionOffset, sectionLength, payload, diagnostics, options);
                case SectionMaterialOverride:
                    return TryParseMaterialOverride(bytes, sectionOffset, sectionLength, materialsByName, diagnostics, options);
                case SectionMaterialShaderParams:
                    return TryParseMaterialParams(bytes, sectionOffset, sectionLength, materialsByName, diagnostics, options);
                case SectionSkinPayload:
                    return TryParseSkin(bytes, sectionOffset, sectionLength, payload, diagnostics, options);
                case SectionBlendShapePayload:
                    return TryParseBlendShape(bytes, sectionOffset, sectionLength, payload, diagnostics, options);
                case SectionMaterialTypedParams:
                    return TryParseMaterialTypedParams(
                        bytes,
                        sectionOffset,
                        sectionLength,
                        materialsByName,
                        diagnostics,
                        options,
                        string.Equals(payload.Manifest.materialParamEncoding, "typed-v3", StringComparison.OrdinalIgnoreCase) ||
                        string.Equals(payload.Manifest.materialParamEncoding, "typed-v4", StringComparison.OrdinalIgnoreCase));
                case SectionSkeletonPosePayload:
                    return TryParseSkeletonPose(bytes, sectionOffset, sectionLength, payload, diagnostics, options);
                case SectionSkeletonRigPayload:
                    return TryParseSkeletonRig(bytes, sectionOffset, sectionLength, payload, diagnostics, options);
                case SectionSpringBonePayload:
                    return TryParseSpringBone(bytes, sectionOffset, sectionLength, payload, diagnostics, options);
                case SectionPhysBonePayload:
                    return TryParsePhysBone(bytes, sectionOffset, sectionLength, payload, diagnostics, options);
                case SectionPhysicsColliderPayload:
                    return TryParsePhysicsCollider(bytes, sectionOffset, sectionLength, payload, diagnostics, options);
                default:
                    return HandleUnknownSection(diagnostics, options, sectionType);
            }
        }

        private static bool HandleUnknownSection(
            Xav2LoadDiagnostics diagnostics,
            Xav2LoadOptions options,
            ushort sectionType)
        {
            var warning = $"XAV2_UNKNOWN_SECTION: 0x{sectionType:X4}";
            var policy = options?.UnknownSectionPolicy ?? Xav2UnknownSectionPolicy.Warn;
            switch (policy)
            {
                case Xav2UnknownSectionPolicy.Ignore:
                    return true;
                case Xav2UnknownSectionPolicy.Fail:
                    return Fail(
                        diagnostics,
                        Xav2LoadErrorCode.UnknownSectionNotAllowed,
                        $"Unknown XAV2 section is not allowed by policy: 0x{sectionType:X4}");
                case Xav2UnknownSectionPolicy.Warn:
                default:
                    return AddWarningOrFail(diagnostics, options, warning);
            }
        }

        private static bool TryParseMesh(
            byte[] bytes,
            int sectionOffset,
            int sectionLength,
            Xav2AvatarPayload payload,
            Xav2LoadDiagnostics diagnostics,
            Xav2LoadOptions options)
        {
            using var ms = new MemoryStream(bytes, sectionOffset, sectionLength, false);
            using var br = new BinaryReader(ms, Encoding.UTF8);

            if (!TryReadSizedString(br, out var name) ||
                !TryReadUInt32(br, out var vertexStride) ||
                !TryReadInt32(br, out var materialIndex) ||
                !TryReadBytesWithUInt32Length(br, out var vertexBlob) ||
                !TryReadUInt32(br, out var indexCount))
            {
                return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 mesh section.");
            }

            var indices = new uint[indexCount];
            for (var i = 0; i < indexCount; i++)
            {
                if (!TryReadUInt32(br, out indices[i]))
                {
                    return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 mesh index array.");
                }
            }

            if (ms.Position != ms.Length)
            {
                return AddWarningOrFail(diagnostics, options, $"XAV2_MESH_TRAILING_BYTES: mesh={name}");
            }

            payload.Meshes.Add(new Xav2MeshPayload
            {
                Name = name,
                VertexStride = vertexStride,
                MaterialIndex = materialIndex,
                VertexBlob = vertexBlob,
                Indices = indices
            });
            return true;
        }

        private static bool TryParseTexture(
            byte[] bytes,
            int sectionOffset,
            int sectionLength,
            Xav2AvatarPayload payload,
            Xav2LoadDiagnostics diagnostics,
            Xav2LoadOptions options)
        {
            using var ms = new MemoryStream(bytes, sectionOffset, sectionLength, false);
            using var br = new BinaryReader(ms, Encoding.UTF8);

            if (!TryReadSizedString(br, out var name) ||
                !TryReadBytesWithUInt32Length(br, out var blob))
            {
                return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 texture section.");
            }

            if (ms.Position != ms.Length)
            {
                return AddWarningOrFail(diagnostics, options, $"XAV2_TEXTURE_TRAILING_BYTES: texture={name}");
            }

            payload.Textures.Add(new Xav2TexturePayload
            {
                Name = name,
                Bytes = blob
            });
            return true;
        }

        private static bool TryParseMaterialOverride(
            byte[] bytes,
            int sectionOffset,
            int sectionLength,
            Dictionary<string, Xav2MaterialPayload> materialsByName,
            Xav2LoadDiagnostics diagnostics,
            Xav2LoadOptions options)
        {
            using var ms = new MemoryStream(bytes, sectionOffset, sectionLength, false);
            using var br = new BinaryReader(ms, Encoding.UTF8);

            if (!TryReadSizedString(br, out var name) || !TryReadSizedString(br, out var shaderName))
            {
                return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 material section.");
            }

            var material = new Xav2MaterialPayload
            {
                Name = name,
                ShaderName = shaderName,
                ShaderVariant = "default"
            };

            var afterShaderPos = ms.Position;
            var parsedWithVariant = false;
            if (TryReadSizedString(br, out var variant) &&
                TryReadSizedString(br, out var baseColorTexture) &&
                TryReadSizedString(br, out var alphaMode) &&
                TryReadSingle(br, out var alphaCutoff) &&
                TryReadByte(br, out var doubleSided))
            {
                parsedWithVariant = ms.Position == ms.Length;
                if (parsedWithVariant)
                {
                    material.ShaderVariant = string.IsNullOrWhiteSpace(variant) ? "default" : variant;
                    material.BaseColorTextureName = baseColorTexture;
                    material.AlphaMode = alphaMode;
                    material.AlphaCutoff = alphaCutoff;
                    material.DoubleSided = doubleSided != 0;
                }
            }

            if (!parsedWithVariant)
            {
                ms.Position = afterShaderPos;
                if (!TryReadSizedString(br, out var legacyBaseTexture) ||
                    !TryReadSizedString(br, out var legacyAlphaMode) ||
                    !TryReadSingle(br, out var legacyAlphaCutoff) ||
                    !TryReadByte(br, out var legacyDoubleSided))
                {
                    return Fail(
                        diagnostics,
                        Xav2LoadErrorCode.SectionSchemaInvalid,
                        $"Invalid XAV2 material section payload for '{name}'.");
                }

                material.ShaderVariant = "default";
                material.BaseColorTextureName = legacyBaseTexture;
                material.AlphaMode = legacyAlphaMode;
                material.AlphaCutoff = legacyAlphaCutoff;
                material.DoubleSided = legacyDoubleSided != 0;
                if (ms.Position != ms.Length)
                {
                    return Fail(
                        diagnostics,
                        Xav2LoadErrorCode.SectionSchemaInvalid,
                        $"Invalid trailing bytes in XAV2 material section '{name}'.");
                }

                if (!AddWarningOrFail(diagnostics, options, $"XAV2_MATERIAL_LEGACY_VARIANT: material={name}"))
                {
                    return false;
                }
            }

            materialsByName[name] = material;
            return true;
        }

        private static bool TryParseMaterialParams(
            byte[] bytes,
            int sectionOffset,
            int sectionLength,
            Dictionary<string, Xav2MaterialPayload> materialsByName,
            Xav2LoadDiagnostics diagnostics,
            Xav2LoadOptions options)
        {
            using var ms = new MemoryStream(bytes, sectionOffset, sectionLength, false);
            using var br = new BinaryReader(ms, Encoding.UTF8);

            if (!TryReadSizedString(br, out var name) || !TryReadSizedString(br, out var paramsJson))
            {
                return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 material params section.");
            }

            if (ms.Position != ms.Length)
            {
                return AddWarningOrFail(diagnostics, options, $"XAV2_MATERIAL_PARAMS_TRAILING_BYTES: material={name}");
            }

            if (!materialsByName.TryGetValue(name, out var material))
            {
                material = new Xav2MaterialPayload { Name = name };
                materialsByName[name] = material;
            }

            material.ShaderParamsJson = string.IsNullOrWhiteSpace(paramsJson) ? "{}" : paramsJson;
            return true;
        }

        private static bool TryParseMaterialTypedParams(
            byte[] bytes,
            int sectionOffset,
            int sectionLength,
            Dictionary<string, Xav2MaterialPayload> materialsByName,
            Xav2LoadDiagnostics diagnostics,
            Xav2LoadOptions options,
            bool preferTypedV3)
        {
            using var ms = new MemoryStream(bytes, sectionOffset, sectionLength, false);
            using var br = new BinaryReader(ms, Encoding.UTF8);

            if (!TryReadSizedString(br, out var name) ||
                !TryReadSizedString(br, out var shaderFamily) ||
                !TryReadUInt32(br, out var featureFlags) ||
                !TryReadUInt16(br, out var floatCount))
            {
                return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 material typed params section.");
            }

            if (!materialsByName.TryGetValue(name, out var material))
            {
                material = new Xav2MaterialPayload { Name = name };
                materialsByName[name] = material;
            }

            material.ShaderFamily = NormalizeShaderFamily(shaderFamily);
            material.FeatureFlags = featureFlags;
            material.TypedFloatParams.Clear();
            material.TypedColorParams.Clear();
            material.TypedTextureParams.Clear();

            var schemaVersion = (ushort)2;
            var parsed = false;

            var parseStart = ms.Position;
            if (preferTypedV3 && floatCount >= 3U)
            {
                var schemaProbePos = ms.Position;
                if (TryReadUInt16(br, out var schemaFloatCount))
                {
                    if (TryParseTypedMaterialBody(br, schemaFloatCount, material) && ms.Position == ms.Length)
                    {
                        schemaVersion = floatCount;
                        parsed = true;
                    }
                }
                if (!parsed)
                {
                    ms.Position = schemaProbePos;
                    material.TypedFloatParams.Clear();
                    material.TypedColorParams.Clear();
                    material.TypedTextureParams.Clear();
                }
            }

            if (!parsed)
            {
                ms.Position = parseStart;
                if (!TryParseTypedMaterialBody(br, floatCount, material) || ms.Position != ms.Length)
                {
                    if (!preferTypedV3 && floatCount >= 3U)
                    {
                        ms.Position = parseStart;
                        material.TypedFloatParams.Clear();
                        material.TypedColorParams.Clear();
                        material.TypedTextureParams.Clear();
                        if (TryReadUInt16(br, out var schemaFloatCount) &&
                            TryParseTypedMaterialBody(br, schemaFloatCount, material) &&
                            ms.Position == ms.Length)
                        {
                            schemaVersion = floatCount;
                            parsed = true;
                        }
                    }
                    if (!parsed)
                    {
                        return AddWarningOrFail(diagnostics, options, $"XAV2_MATERIAL_TYPED_SCHEMA_INVALID: material={name}");
                    }
                }
            }

            material.TypedSchemaVersion = schemaVersion;
            material.MaterialParamEncoding = schemaVersion >= 3 ? $"typed-v{schemaVersion}" : "typed-v2";

            if (!string.Equals(material.ShaderFamily, "legacy", StringComparison.OrdinalIgnoreCase))
            {
                if (!material.TypedColorParams.Exists(p => string.Equals(p.Id, "_BaseColor", StringComparison.Ordinal)))
                {
                    if (!AddWarningOrFail(diagnostics, options, $"XAV2_MATERIAL_TYPED_MISSING_REQUIRED_PARAM: material={name}, id=_BaseColor"))
                    {
                        return false;
                    }
                }
            }
            if (!IsSupportedShaderFamily(material.ShaderFamily))
            {
                if (!AddWarningOrFail(
                        diagnostics,
                        options,
                        $"XAV2_MATERIAL_TYPED_UNSUPPORTED_SHADER_FAMILY: material={name}, family={material.ShaderFamily}"))
                {
                    return false;
                }
            }

            return true;
        }

        private static bool TryParseTypedMaterialBody(BinaryReader br, ushort floatCount, Xav2MaterialPayload material)
        {
            for (var i = 0; i < floatCount; i++)
            {
                if (!TryReadSizedString(br, out var id) || !TryReadSingle(br, out var value))
                {
                    return false;
                }
                material.TypedFloatParams.Add(new Xav2TypedFloatParam { Id = id, Value = value });
            }

            if (!TryReadUInt16(br, out var colorCount))
            {
                return false;
            }
            for (var i = 0; i < colorCount; i++)
            {
                if (!TryReadSizedString(br, out var id) ||
                    !TryReadSingle(br, out var r) ||
                    !TryReadSingle(br, out var g) ||
                    !TryReadSingle(br, out var b) ||
                    !TryReadSingle(br, out var a))
                {
                    return false;
                }
                material.TypedColorParams.Add(new Xav2TypedColorParam { Id = id, R = r, G = g, B = b, A = a });
            }

            if (!TryReadUInt16(br, out var textureCount))
            {
                return false;
            }
            for (var i = 0; i < textureCount; i++)
            {
                if (!TryReadSizedString(br, out var slot) || !TryReadSizedString(br, out var textureRef))
                {
                    return false;
                }
                material.TypedTextureParams.Add(new Xav2TypedTextureParam
                {
                    Slot = slot,
                    TextureRef = textureRef
                });
            }

            return true;
        }

        private static bool TryParseSkin(
            byte[] bytes,
            int sectionOffset,
            int sectionLength,
            Xav2AvatarPayload payload,
            Xav2LoadDiagnostics diagnostics,
            Xav2LoadOptions options)
        {
            using var ms = new MemoryStream(bytes, sectionOffset, sectionLength, false);
            using var br = new BinaryReader(ms, Encoding.UTF8);

            if (!TryReadSizedString(br, out var meshName) || !TryReadUInt32(br, out var boneCount))
            {
                return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 skin section.");
            }

            var boneIndices = new int[boneCount];
            for (var i = 0; i < boneCount; i++)
            {
                if (!TryReadInt32(br, out boneIndices[i]))
                {
                    return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 skin bone indices.");
                }
            }

            if (!TryReadUInt32(br, out var bindPoseCount))
            {
                return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 skin bindpose count.");
            }
            var bindPoses = new float[bindPoseCount];
            for (var i = 0; i < bindPoseCount; i++)
            {
                if (!TryReadSingle(br, out bindPoses[i]))
                {
                    return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 skin bindpose payload.");
                }
            }

            if (!TryReadBytesWithUInt32Length(br, out var skinWeightBlob))
            {
                return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 skin weight payload.");
            }

            if (ms.Position != ms.Length)
            {
                return AddWarningOrFail(diagnostics, options, $"XAV2_SKIN_TRAILING_BYTES: mesh={meshName}");
            }

            payload.Skins.Add(new Xav2SkinPayload
            {
                MeshName = meshName,
                BoneIndices = boneIndices,
                BindPoses16xN = bindPoses,
                SkinWeightBlob = skinWeightBlob
            });
            return true;
        }

        private static bool TryParseBlendShape(
            byte[] bytes,
            int sectionOffset,
            int sectionLength,
            Xav2AvatarPayload payload,
            Xav2LoadDiagnostics diagnostics,
            Xav2LoadOptions options)
        {
            using var ms = new MemoryStream(bytes, sectionOffset, sectionLength, false);
            using var br = new BinaryReader(ms, Encoding.UTF8);

            if (!TryReadSizedString(br, out var meshName) || !TryReadUInt32(br, out var frameCount))
            {
                return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 blendshape section.");
            }

            var blendPayload = new Xav2BlendShapePayload { MeshName = meshName };
            for (var i = 0; i < frameCount; i++)
            {
                if (!TryReadSizedString(br, out var frameName) ||
                    !TryReadSingle(br, out var weight) ||
                    !TryReadBytesWithUInt32Length(br, out var deltaVertices) ||
                    !TryReadBytesWithUInt32Length(br, out var deltaNormals) ||
                    !TryReadBytesWithUInt32Length(br, out var deltaTangents))
                {
                    return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 blendshape frame payload.");
                }

                blendPayload.Frames.Add(new Xav2BlendShapeFramePayload
                {
                    Name = frameName,
                    Weight = weight,
                    DeltaVertices = deltaVertices,
                    DeltaNormals = deltaNormals,
                    DeltaTangents = deltaTangents
                });
            }

            if (ms.Position != ms.Length)
            {
                return AddWarningOrFail(diagnostics, options, $"XAV2_BLENDSHAPE_TRAILING_BYTES: mesh={meshName}");
            }

            payload.BlendShapes.Add(blendPayload);
            return true;
        }

        private static bool TryParseSkeletonPose(
            byte[] bytes,
            int sectionOffset,
            int sectionLength,
            Xav2AvatarPayload payload,
            Xav2LoadDiagnostics diagnostics,
            Xav2LoadOptions options)
        {
            using var ms = new MemoryStream(bytes, sectionOffset, sectionLength, false);
            using var br = new BinaryReader(ms, Encoding.UTF8);

            if (!TryReadSizedString(br, out var meshName) || !TryReadUInt32(br, out var matrixValueCount))
            {
                return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 skeleton pose section.");
            }

            if ((matrixValueCount % 16U) != 0U)
            {
                return AddWarningOrFail(
                    diagnostics,
                    options,
                    $"XAV3_SKINNING_MATRIX_INVALID: mesh={meshName}, count={matrixValueCount}");
            }

            var matrices = new float[matrixValueCount];
            for (var i = 0; i < matrixValueCount; i++)
            {
                if (!TryReadSingle(br, out matrices[i]))
                {
                    return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 skeleton matrix payload.");
                }
            }

            if (ms.Position != ms.Length)
            {
                return AddWarningOrFail(diagnostics, options, $"XAV2_SKELETON_TRAILING_BYTES: mesh={meshName}");
            }

            payload.Skeletons.Add(new Xav2SkeletonPayload
            {
                MeshName = meshName,
                BoneMatrices16xN = matrices
            });
            return true;
        }

        private static bool TryParseSkeletonRig(
            byte[] bytes,
            int sectionOffset,
            int sectionLength,
            Xav2AvatarPayload payload,
            Xav2LoadDiagnostics diagnostics,
            Xav2LoadOptions options)
        {
            using var ms = new MemoryStream(bytes, sectionOffset, sectionLength, false);
            using var br = new BinaryReader(ms, Encoding.UTF8);

            if (!TryReadSizedString(br, out var meshName) || !TryReadUInt32(br, out var boneCount))
            {
                return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 skeleton rig section.");
            }

            var rig = new Xav2SkeletonRigPayload
            {
                MeshName = meshName
            };

            for (var i = 0; i < boneCount; i++)
            {
                if (!TryReadSizedString(br, out var boneName) ||
                    !TryReadInt32(br, out var parentIndex) ||
                    !TryReadUInt32(br, out var matrixValueCount))
                {
                    return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 skeleton rig bone entry.");
                }

                if (matrixValueCount != 16U)
                {
                    return Fail(
                        diagnostics,
                        Xav2LoadErrorCode.SectionSchemaInvalid,
                        $"Invalid XAV4 rig matrix payload size for mesh '{meshName}' bone '{boneName}': {matrixValueCount}.");
                }

                var matrix = new float[matrixValueCount];
                for (var j = 0; j < matrixValueCount; j++)
                {
                    if (!TryReadSingle(br, out matrix[j]))
                    {
                        return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 skeleton rig matrix payload.");
                    }
                }

                rig.Bones.Add(new Xav2RigBonePayload
                {
                    Name = boneName,
                    ParentIndex = parentIndex,
                    LocalMatrix16 = matrix
                });
            }

            if (!ValidateRigGraph(rig, diagnostics, options))
            {
                return false;
            }

            if (ms.Position != ms.Length)
            {
                return AddWarningOrFail(diagnostics, options, $"XAV4_RIG_TRAILING_BYTES: mesh={meshName}");
            }

            payload.SkeletonRigs.Add(rig);
            return true;
        }

        private static bool ValidateRigGraph(
            Xav2SkeletonRigPayload rig,
            Xav2LoadDiagnostics diagnostics,
            Xav2LoadOptions options)
        {
            var nameSet = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            for (var i = 0; i < rig.Bones.Count; i++)
            {
                var bone = rig.Bones[i];
                if (string.IsNullOrWhiteSpace(bone.Name))
                {
                    if (!AddWarningOrFail(
                            diagnostics,
                            options,
                            $"XAV4_RIG_SCHEMA_INVALID: mesh='{rig.MeshName}', issue=empty_bone_name, index={i}"))
                    {
                        return false;
                    }
                }
                else if (!nameSet.Add(bone.Name))
                {
                    if (!AddWarningOrFail(
                            diagnostics,
                            options,
                            $"XAV4_RIG_SCHEMA_INVALID: mesh='{rig.MeshName}', issue=duplicate_bone_name, bone='{bone.Name}'"))
                    {
                        return false;
                    }
                }

                if (bone.ParentIndex < -1 || bone.ParentIndex >= rig.Bones.Count || bone.ParentIndex == i)
                {
                    if (!AddWarningOrFail(
                            diagnostics,
                            options,
                            $"XAV4_RIG_SCHEMA_INVALID: mesh='{rig.MeshName}', issue=invalid_parent_index, bone='{bone.Name}', parent={bone.ParentIndex}"))
                    {
                        return false;
                    }
                }
            }

            for (var i = 0; i < rig.Bones.Count; i++)
            {
                var visited = new HashSet<int>();
                var cursor = i;
                while (cursor >= 0 && cursor < rig.Bones.Count)
                {
                    if (!visited.Add(cursor))
                    {
                        if (!AddWarningOrFail(
                                diagnostics,
                                options,
                                $"XAV4_RIG_SCHEMA_INVALID: mesh='{rig.MeshName}', issue=parent_cycle, bone='{rig.Bones[i].Name}'"))
                        {
                            return false;
                        }
                        break;
                    }
                    cursor = rig.Bones[cursor].ParentIndex;
                }
            }

            return true;
        }

        private static bool TryParsePhysicsCollider(
            byte[] bytes,
            int sectionOffset,
            int sectionLength,
            Xav2AvatarPayload payload,
            Xav2LoadDiagnostics diagnostics,
            Xav2LoadOptions options)
        {
            using var ms = new MemoryStream(bytes, sectionOffset, sectionLength, false);
            using var br = new BinaryReader(ms, Encoding.UTF8);
            if (!TryReadSizedString(br, out var name) ||
                !TryReadSizedString(br, out var bonePath) ||
                !TryReadByte(br, out var shapeRaw) ||
                !TryReadSingle(br, out var radius) ||
                !TryReadSingle(br, out var height) ||
                !TryReadVector3(br, out var localPosition) ||
                !TryReadVector3(br, out var localDirection))
            {
                return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 physics collider section.");
            }

            if (ms.Position != ms.Length)
            {
                return AddWarningOrFail(
                    diagnostics,
                    options,
                    $"XAV2_PHYSICS_SCHEMA_INVALID: collider={name}, issue=trailing-bytes");
            }

            payload.PhysicsColliders.Add(new Xav2PhysicsColliderPayload
            {
                Name = name,
                BonePath = bonePath,
                Shape = Enum.IsDefined(typeof(Xav2PhysicsColliderShape), (int)shapeRaw)
                    ? (Xav2PhysicsColliderShape)shapeRaw
                    : Xav2PhysicsColliderShape.Unknown,
                Radius = radius,
                Height = height,
                LocalPosition = localPosition,
                LocalDirection = localDirection
            });
            return true;
        }

        private static bool TryParseSpringBone(
            byte[] bytes,
            int sectionOffset,
            int sectionLength,
            Xav2AvatarPayload payload,
            Xav2LoadDiagnostics diagnostics,
            Xav2LoadOptions options)
        {
            using var ms = new MemoryStream(bytes, sectionOffset, sectionLength, false);
            using var br = new BinaryReader(ms, Encoding.UTF8);
            if (!TryReadSizedString(br, out var name) ||
                !TryReadSizedString(br, out var rootBonePath) ||
                !TryReadStringList(br, out var bonePaths) ||
                !TryReadSingle(br, out var stiffness) ||
                !TryReadSingle(br, out var drag) ||
                !TryReadSingle(br, out var radius) ||
                !TryReadVector3(br, out var gravity) ||
                !TryReadStringList(br, out var colliderRefs) ||
                !TryReadByte(br, out var enabledByte))
            {
                return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 springbone section.");
            }

            if (ms.Position != ms.Length)
            {
                return AddWarningOrFail(
                    diagnostics,
                    options,
                    $"XAV2_PHYSICS_SCHEMA_INVALID: springBone={name}, issue=trailing-bytes");
            }

            payload.SpringBones.Add(new Xav2SpringBonePayload
            {
                Name = name,
                RootBonePath = rootBonePath,
                BonePaths = bonePaths,
                Stiffness = stiffness,
                Drag = drag,
                Radius = radius,
                Gravity = gravity,
                ColliderRefs = colliderRefs,
                Enabled = enabledByte != 0
            });
            return true;
        }

        private static bool TryParsePhysBone(
            byte[] bytes,
            int sectionOffset,
            int sectionLength,
            Xav2AvatarPayload payload,
            Xav2LoadDiagnostics diagnostics,
            Xav2LoadOptions options)
        {
            using var ms = new MemoryStream(bytes, sectionOffset, sectionLength, false);
            using var br = new BinaryReader(ms, Encoding.UTF8);
            if (!TryReadSizedString(br, out var name) ||
                !TryReadSizedString(br, out var rootBonePath) ||
                !TryReadStringList(br, out var bonePaths) ||
                !TryReadSingle(br, out var pull) ||
                !TryReadSingle(br, out var spring) ||
                !TryReadSingle(br, out var immobile) ||
                !TryReadSingle(br, out var radius) ||
                !TryReadVector3(br, out var gravity) ||
                !TryReadStringList(br, out var colliderRefs) ||
                !TryReadByte(br, out var enabledByte))
            {
                return Fail(diagnostics, Xav2LoadErrorCode.SectionSchemaInvalid, "Invalid XAV2 physbone section.");
            }

            if (ms.Position != ms.Length)
            {
                return AddWarningOrFail(
                    diagnostics,
                    options,
                    $"XAV2_PHYSICS_SCHEMA_INVALID: physBone={name}, issue=trailing-bytes");
            }

            payload.PhysBones.Add(new Xav2PhysBonePayload
            {
                Name = name,
                RootBonePath = rootBonePath,
                BonePaths = bonePaths,
                Pull = pull,
                Spring = spring,
                Immobile = immobile,
                Radius = radius,
                Gravity = gravity,
                ColliderRefs = colliderRefs,
                Enabled = enabledByte != 0
            });
            return true;
        }

        private static void NormalizeManifest(Xav2Manifest manifest)
        {
            manifest.avatarId ??= string.Empty;
            manifest.displayName ??= string.Empty;
            manifest.sourceExt ??= ".vrm";
            manifest.exporterVersion ??= "0.3.0";
            manifest.physicsSource ??= "none";
            manifest.meshRefs ??= new List<string>();
            manifest.materialRefs ??= new List<string>();
            manifest.textureRefs ??= new List<string>();
            manifest.strictShaderSet ??= new List<string>();
            manifest.materialParamEncoding ??= "legacy-json";
            if (manifest.schemaVersion == 0U)
            {
                manifest.schemaVersion = 1U;
            }
            if (manifest.physicsSchemaVersion == 0U)
            {
                manifest.physicsSchemaVersion = 1U;
            }
        }

        private static bool CanonicalizeMaterialPayloads(
            Xav2AvatarPayload payload,
            Xav2LoadDiagnostics diagnostics,
            Xav2LoadOptions options)
        {
            if (payload == null)
            {
                return true;
            }

            foreach (var material in payload.Materials)
            {
                material.ShaderFamily = NormalizeShaderFamily(material.ShaderFamily);
                if (string.Equals(material.ShaderFamily, "legacy", StringComparison.OrdinalIgnoreCase))
                {
                    material.ShaderFamily = InferShaderFamilyFromShaderName(material.ShaderName);
                }

                if (!IsParityShaderFamily(material.ShaderFamily))
                {
                    var policy = options?.ShaderPolicy ?? Xav2ShaderPolicy.WarnFallback;
                    if (policy == Xav2ShaderPolicy.Fail)
                    {
                        diagnostics.CriticalParityViolation = true;
                        return Fail(
                            diagnostics,
                            Xav2LoadErrorCode.ParityContractViolation,
                            $"Parity contract violation: unsupported shader family '{material.ShaderFamily}' for material '{material.Name}'.");
                    }

                    var fromFamily = material.ShaderFamily;
                    material.ShaderFamily = "standard";
                    if (!AddWarningOrFail(
                            diagnostics,
                            options,
                            $"XAV2_SHADER_FAMILY_FALLBACK: material={material.Name}, from={fromFamily}, to={material.ShaderFamily}"))
                    {
                        return false;
                    }
                }

                var hasTyped = HasTypedMaterialPayload(material);
                if (!hasTyped || !string.Equals(material.MaterialParamEncoding, "typed-v4", StringComparison.OrdinalIgnoreCase))
                {
                    diagnostics.MigrationApplied = true;
                    material.MaterialParamEncoding = "typed-v4";
                    material.TypedSchemaVersion = 4;

                    if (!material.TypedColorParams.Exists(p => string.Equals(p.Id, "_BaseColor", StringComparison.Ordinal)))
                    {
                        material.TypedColorParams.Add(new Xav2TypedColorParam
                        {
                            Id = "_BaseColor",
                            R = 1.0f,
                            G = 1.0f,
                            B = 1.0f,
                            A = 1.0f
                        });
                    }

                    if (!string.IsNullOrWhiteSpace(material.BaseColorTextureName) &&
                        !material.TypedTextureParams.Exists(p => string.Equals(p.Slot, "base", StringComparison.OrdinalIgnoreCase)))
                    {
                        material.TypedTextureParams.Add(new Xav2TypedTextureParam
                        {
                            Slot = "base",
                            TextureRef = material.BaseColorTextureName
                        });
                    }
                }
                if (string.IsNullOrWhiteSpace(material.ShaderVariant))
                {
                    material.ShaderVariant = "default";
                }
                if (string.IsNullOrWhiteSpace(material.KeywordSet))
                {
                    material.KeywordSet = "[]";
                }
                if (string.IsNullOrWhiteSpace(material.RenderState))
                {
                    material.RenderState = "auto";
                }
                if (string.IsNullOrWhiteSpace(material.PassFlags))
                {
                    material.PassFlags = "base";
                }

                if (!material.TypedColorParams.Exists(p => string.Equals(p.Id, "_BaseColor", StringComparison.Ordinal)))
                {
                    diagnostics.CriticalParityViolation = true;
                    return Fail(
                        diagnostics,
                        Xav2LoadErrorCode.ParityContractViolation,
                        $"Parity contract violation: missing required typed param _BaseColor for material '{material.Name}'.");
                }
            }

            payload.Manifest.materialParamEncoding = "typed-v4";
            return true;
        }

        private static bool EvaluatePartialCompatibility(
            Xav2AvatarPayload payload,
            Xav2LoadDiagnostics diagnostics,
            Xav2LoadOptions options,
            int formatVersion)
        {
            var meshNameSet = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var mesh in payload.Meshes)
            {
                if (!string.IsNullOrWhiteSpace(mesh.Name))
                {
                    meshNameSet.Add(NormalizeRefKey(mesh.Name));
                }
            }

            var textureNameSet = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var texture in payload.Textures)
            {
                if (!string.IsNullOrWhiteSpace(texture.Name))
                {
                    textureNameSet.Add(NormalizeRefKey(texture.Name));
                }
            }

            var missingMeshRef = false;
            foreach (var meshRef in payload.Manifest.meshRefs)
            {
                if (!meshNameSet.Contains(NormalizeRefKey(meshRef)))
                {
                    missingMeshRef = true;
                    if (!AddWarningOrFail(diagnostics, options, $"XAV2_ASSET_MISSING: meshRef='{meshRef}'"))
                    {
                        return false;
                    }
                }
            }

            var missingTextureRef = false;
            foreach (var textureRef in payload.Manifest.textureRefs)
            {
                if (!textureNameSet.Contains(NormalizeRefKey(textureRef)))
                {
                    missingTextureRef = true;
                    if (!AddWarningOrFail(diagnostics, options, $"XAV2_ASSET_MISSING: textureRef='{textureRef}'"))
                    {
                        return false;
                    }
                }
            }

            foreach (var material in payload.Materials)
            {
                var hasTyped = HasTypedMaterialPayload(material);
                if (!hasTyped)
                {
                    diagnostics.CriticalParityViolation = true;
                    return Fail(
                        diagnostics,
                        Xav2LoadErrorCode.ParityContractViolation,
                        $"Parity contract violation: typed material payload missing for material '{material.Name}'.");
                }

                var fallbackReasons = new List<string>();
                if (!IsCanonicalAlphaMode(material.AlphaMode))
                {
                    fallbackReasons.Add("alpha_mode_defaulted");
                }
                if (fallbackReasons.Count > 0)
                {
                    if (!AddWarningOrFail(
                            diagnostics,
                            options,
                            $"XAV2_MATERIAL_FALLBACK_APPLIED: material={material.Name}, reason={string.Join("|", fallbackReasons)}"))
                    {
                        return false;
                    }
                }
                foreach (var typedTexture in material.TypedTextureParams)
                {
                    if (string.IsNullOrWhiteSpace(typedTexture.TextureRef))
                    {
                        continue;
                    }
                    if (textureNameSet.Contains(NormalizeRefKey(typedTexture.TextureRef)))
                    {
                        continue;
                    }
                    diagnostics.CriticalParityViolation = true;
                    return Fail(
                        diagnostics,
                        Xav2LoadErrorCode.ParityContractViolation,
                        $"Parity contract violation: unresolved typed texture ref for material '{material.Name}', slot '{typedTexture.Slot}', ref '{typedTexture.TextureRef}'.");
                }
            }

            if (payload.Manifest.materialRefs.Count != 0 && payload.Materials.Count == 0)
            {
                if (!AddWarningOrFail(
                        diagnostics,
                        options,
                        "XAV2_ASSET_MISSING: materialRefs exist but no material payloads were parsed."))
                {
                    return false;
                }
                diagnostics.IsPartial = true;
                return true;
            }

            if (payload.SpringBones.Count > 0 || payload.PhysBones.Count > 0)
            {
                var colliderRefSet = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                foreach (var collider in payload.PhysicsColliders)
                {
                    if (!string.IsNullOrWhiteSpace(collider.Name))
                    {
                        colliderRefSet.Add(NormalizeRefKey(collider.Name));
                    }
                }

                foreach (var springBone in payload.SpringBones)
                {
                    foreach (var colliderRef in springBone.ColliderRefs)
                    {
                        if (string.IsNullOrWhiteSpace(colliderRef))
                        {
                            continue;
                        }
                        if (colliderRefSet.Contains(NormalizeRefKey(colliderRef)))
                        {
                            continue;
                        }
                        if (!AddWarningOrFail(
                                diagnostics,
                                options,
                                $"XAV2_PHYSICS_REF_MISSING: springBone={springBone.Name}, collider={colliderRef}"))
                        {
                            return false;
                        }
                    }
                }

                foreach (var physBone in payload.PhysBones)
                {
                    foreach (var colliderRef in physBone.ColliderRefs)
                    {
                        if (string.IsNullOrWhiteSpace(colliderRef))
                        {
                            continue;
                        }
                        if (colliderRefSet.Contains(NormalizeRefKey(colliderRef)))
                        {
                            continue;
                        }
                        if (!AddWarningOrFail(
                                diagnostics,
                                options,
                                $"XAV2_PHYSICS_REF_MISSING: physBone={physBone.Name}, collider={colliderRef}"))
                        {
                            return false;
                        }
                    }
                }

                AddSoftWarning(
                    diagnostics,
                    "XAV2_PHYSICS_COMPONENT_UNAVAILABLE: runtime_simulation_not_implemented");
            }

            if (formatVersion >= 3 && payload.Skins.Count > 0)
            {
                var skinMeshSet = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                foreach (var skin in payload.Skins)
                {
                    skinMeshSet.Add(NormalizeRefKey(skin.MeshName));
                }

                var skeletonMeshSet = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                foreach (var skeleton in payload.Skeletons)
                {
                    var key = NormalizeRefKey(skeleton.MeshName);
                    skeletonMeshSet.Add(key);
                    if (!skinMeshSet.Contains(key))
                    {
                        if (!AddWarningOrFail(
                                diagnostics,
                                options,
                                $"XAV3_SKELETON_MESH_BIND_MISMATCH: skeletonMesh='{skeleton.MeshName}'"))
                        {
                            return false;
                        }
                    }
                }

                foreach (var skin in payload.Skins)
                {
                    if (!skeletonMeshSet.Contains(NormalizeRefKey(skin.MeshName)))
                    {
                        missingMeshRef = true;
                        if (!AddWarningOrFail(
                                diagnostics,
                                options,
                                $"XAV3_SKELETON_PAYLOAD_MISSING: mesh='{skin.MeshName}'"))
                        {
                            return false;
                        }
                    }
                }
            }

            var missingRig = false;
            if (formatVersion >= 4 && payload.Skins.Count > 0)
            {
                var rigMeshSet = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                var rigBoneCountByMesh = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
                foreach (var rig in payload.SkeletonRigs)
                {
                    var key = NormalizeRefKey(rig.MeshName);
                    rigMeshSet.Add(key);
                    rigBoneCountByMesh[key] = rig.Bones?.Count ?? 0;
                }

                foreach (var skin in payload.Skins)
                {
                    var key = NormalizeRefKey(skin.MeshName);
                    if (!rigMeshSet.Contains(key))
                    {
                        missingRig = true;
                        if (!AddWarningOrFail(
                                diagnostics,
                                options,
                                $"XAV4_RIG_MISSING: mesh='{skin.MeshName}'"))
                        {
                            return false;
                        }
                        continue;
                    }

                    var rigBoneCount = rigBoneCountByMesh.TryGetValue(key, out var count) ? count : 0;
                    var skinBoneCount = skin.BoneIndices?.Length ?? 0;
                    if (rigBoneCount < skinBoneCount)
                    {
                        missingRig = true;
                        if (!AddWarningOrFail(
                                diagnostics,
                                options,
                                $"XAV4_RIG_BONE_COUNT_MISMATCH: mesh='{skin.MeshName}', rig={rigBoneCount}, skin={skinBoneCount}"))
                        {
                            return false;
                        }
                    }
                }
            }

            diagnostics.IsPartial = missingMeshRef || missingTextureRef || missingRig;
            return true;
        }

        private static string NormalizeRefKey(string value)
        {
            return string.IsNullOrWhiteSpace(value)
                ? string.Empty
                : value.Replace('\\', '/').Trim().ToLowerInvariant();
        }

        private static string NormalizeShaderFamily(string value)
        {
            var key = NormalizeRefKey(value);
            return string.IsNullOrWhiteSpace(key) ? "legacy" : key;
        }

        private static string InferShaderFamilyFromShaderName(string shaderName)
        {
            var key = NormalizeRefKey(shaderName);
            if (string.Equals(key, "standard", StringComparison.Ordinal))
            {
                return "standard";
            }
            if (key.Contains("mtoon"))
            {
                return "mtoon";
            }
            if (key.Contains("liltoon"))
            {
                return "liltoon";
            }
            if (key.Contains("poiyomi"))
            {
                return "poiyomi";
            }
            return "legacy";
        }

        private static bool HasTypedMaterialPayload(Xav2MaterialPayload material)
        {
            if (material == null)
            {
                return false;
            }

            if (material.MaterialParamEncoding.StartsWith("typed-v", StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }

            return material.TypedFloatParams.Count > 0 ||
                   material.TypedColorParams.Count > 0 ||
                   material.TypedTextureParams.Count > 0;
        }

        private static bool IsParityShaderFamily(string value)
        {
            var key = NormalizeShaderFamily(value);
            return key == "liltoon" || key == "poiyomi" || key == "standard" || key == "mtoon";
        }

        private static bool IsSupportedShaderFamily(string value)
        {
            var key = NormalizeShaderFamily(value);
            return key == "legacy" ||
                   key == "standard" ||
                   key == "mtoon" ||
                   key == "liltoon" ||
                   key == "poiyomi" ||
                   key == "potatoon" ||
                   key == "realtoon";
        }

        private static bool IsCanonicalAlphaMode(string value)
        {
            if (string.IsNullOrWhiteSpace(value))
            {
                return true;
            }

            return string.Equals(value, "OPAQUE", StringComparison.OrdinalIgnoreCase) ||
                   string.Equals(value, "MASK", StringComparison.OrdinalIgnoreCase) ||
                   string.Equals(value, "BLEND", StringComparison.OrdinalIgnoreCase);
        }

        private static bool ContainsAny(string source, params string[] needles)
        {
            if (string.IsNullOrEmpty(source) || needles == null || needles.Length == 0)
            {
                return false;
            }

            foreach (var needle in needles)
            {
                if (!string.IsNullOrEmpty(needle) &&
                    source.IndexOf(needle, StringComparison.Ordinal) >= 0)
                {
                    return true;
                }
            }

            return false;
        }

        private static bool AddWarningOrFail(Xav2LoadDiagnostics diagnostics, Xav2LoadOptions options, string warning)
        {
            AddWarningCode(diagnostics, warning);
            if (options != null && options.StrictValidation)
            {
                return Fail(
                    diagnostics,
                    Xav2LoadErrorCode.StrictValidationFailed,
                    $"Strict validation failed: {warning}");
            }
            diagnostics.Warnings.Add(warning);
            return true;
        }

        private static void AddSoftWarning(Xav2LoadDiagnostics diagnostics, string warning)
        {
            if (diagnostics == null || string.IsNullOrWhiteSpace(warning))
            {
                return;
            }

            AddWarningCode(diagnostics, warning);
            diagnostics.Warnings.Add(warning);
        }

        private static void AddWarningCode(Xav2LoadDiagnostics diagnostics, string warning)
        {
            if (diagnostics == null || string.IsNullOrWhiteSpace(warning))
            {
                return;
            }

            var separator = warning.IndexOf(':');
            if (separator <= 0)
            {
                return;
            }

            var code = warning.Substring(0, separator).Trim();
            if (!string.IsNullOrWhiteSpace(code))
            {
                diagnostics.WarningCodes.Add(code);
            }
        }

        private static bool Fail(Xav2LoadDiagnostics diagnostics, Xav2LoadErrorCode errorCode, string errorMessage)
        {
            diagnostics.ErrorCode = errorCode;
            diagnostics.ErrorMessage = errorMessage;
            return false;
        }

        private static Exception BuildLoadException(string path, Xav2LoadDiagnostics diagnostics)
        {
            var message = string.IsNullOrWhiteSpace(diagnostics.ErrorMessage)
                ? "XAV2 load failed."
                : diagnostics.ErrorMessage;

            return new InvalidDataException(
                $"Failed to load XAV2 '{path}' at stage '{diagnostics.ParserStage}' with '{diagnostics.ErrorCode}': {message}");
        }

        private static bool TryReadAscii(byte[] bytes, ref int cursor, int count, out string value)
        {
            value = string.Empty;
            if (count < 0 || (bytes.Length - cursor) < count)
            {
                return false;
            }
            value = Encoding.ASCII.GetString(bytes, cursor, count);
            cursor += count;
            return true;
        }

        private static bool TryReadUtf8(byte[] bytes, ref int cursor, int count, out string value)
        {
            value = string.Empty;
            if (count < 0 || (bytes.Length - cursor) < count)
            {
                return false;
            }
            value = Encoding.UTF8.GetString(bytes, cursor, count);
            cursor += count;
            return true;
        }

        private static bool TryReadUInt16(byte[] bytes, ref int cursor, out ushort value)
        {
            value = 0;
            if ((bytes.Length - cursor) < 2)
            {
                return false;
            }
            value = BitConverter.ToUInt16(bytes, cursor);
            cursor += 2;
            return true;
        }

        private static bool TryReadUInt32(byte[] bytes, ref int cursor, out uint value)
        {
            value = 0;
            if ((bytes.Length - cursor) < 4)
            {
                return false;
            }
            value = BitConverter.ToUInt32(bytes, cursor);
            cursor += 4;
            return true;
        }

        private static bool TryReadSizedString(BinaryReader br, out string value)
        {
            value = string.Empty;
            if (!TryReadUInt16(br, out var len))
            {
                return false;
            }
            if (len == 0)
            {
                return true;
            }
            if (br.BaseStream.Length - br.BaseStream.Position < len)
            {
                return false;
            }
            value = Encoding.UTF8.GetString(br.ReadBytes(len));
            return true;
        }

        private static bool TryReadBytesWithUInt32Length(BinaryReader br, out byte[] bytes)
        {
            bytes = Array.Empty<byte>();
            if (!TryReadUInt32(br, out var len))
            {
                return false;
            }
            if (len > int.MaxValue)
            {
                return false;
            }
            if (br.BaseStream.Length - br.BaseStream.Position < len)
            {
                return false;
            }
            bytes = br.ReadBytes((int)len);
            return bytes.Length == (int)len;
        }

        private static bool TryReadUInt16(BinaryReader br, out ushort value)
        {
            value = 0;
            if (br.BaseStream.Length - br.BaseStream.Position < 2)
            {
                return false;
            }
            value = br.ReadUInt16();
            return true;
        }

        private static bool TryReadUInt32(BinaryReader br, out uint value)
        {
            value = 0;
            if (br.BaseStream.Length - br.BaseStream.Position < 4)
            {
                return false;
            }
            value = br.ReadUInt32();
            return true;
        }

        private static bool TryReadInt32(BinaryReader br, out int value)
        {
            value = 0;
            if (br.BaseStream.Length - br.BaseStream.Position < 4)
            {
                return false;
            }
            value = br.ReadInt32();
            return true;
        }

        private static bool TryReadSingle(BinaryReader br, out float value)
        {
            value = 0.0f;
            if (br.BaseStream.Length - br.BaseStream.Position < 4)
            {
                return false;
            }
            value = br.ReadSingle();
            return true;
        }

        private static bool TryReadByte(BinaryReader br, out byte value)
        {
            value = 0;
            if (br.BaseStream.Length - br.BaseStream.Position < 1)
            {
                return false;
            }
            value = br.ReadByte();
            return true;
        }

        private static bool TryReadStringList(BinaryReader br, out List<string> values)
        {
            values = new List<string>();
            if (!TryReadUInt16(br, out var count))
            {
                return false;
            }

            for (var i = 0; i < count; i++)
            {
                if (!TryReadSizedString(br, out var value))
                {
                    return false;
                }
                values.Add(value);
            }
            return true;
        }

        private static bool TryReadVector3(BinaryReader br, out float[] values)
        {
            values = new float[3];
            if (!TryReadSingle(br, out values[0]) ||
                !TryReadSingle(br, out values[1]) ||
                !TryReadSingle(br, out values[2]))
            {
                values = Array.Empty<float>();
                return false;
            }

            return true;
        }
    }
}
