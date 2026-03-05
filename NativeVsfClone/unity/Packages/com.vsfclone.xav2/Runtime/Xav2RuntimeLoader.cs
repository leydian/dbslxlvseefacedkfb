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

        public static Xav2AvatarPayload Load(string path)
        {
            if (TryLoad(path, out var payload, out var diagnostics))
            {
                return payload;
            }
            throw BuildLoadException(path, diagnostics);
        }

        public static bool TryLoad(string path, out Xav2AvatarPayload payload, out Xav2LoadDiagnostics diagnostics)
        {
            return TryLoad(path, out payload, out diagnostics, new Xav2LoadOptions());
        }

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
            if (version != 1 && version != 2)
            {
                return Fail(diagnostics, Xav2LoadErrorCode.UnsupportedVersion, $"Unsupported XAV2 version: {version}");
            }

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
                if (sectionFlags != 0)
                {
                    if (!AddWarningOrFail(
                            diagnostics,
                            options,
                            $"XAV2_SECTION_FLAGS_NONZERO: type=0x{sectionType:X4}, flags={sectionFlags}"))
                    {
                        return false;
                    }
                }

                if (!TryParseSection(
                        sectionType,
                        bytes,
                        sectionOffset,
                        sectionLength,
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

            if (!EvaluatePartialCompatibility(payload, diagnostics, options))
            {
                return false;
            }
            diagnostics.ParserStage = "runtime-ready";
            return true;
        }

        private static bool TryParseSection(
            ushort sectionType,
            byte[] bytes,
            int sectionOffset,
            int sectionLength,
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

        private static void NormalizeManifest(Xav2Manifest manifest)
        {
            manifest.avatarId ??= string.Empty;
            manifest.displayName ??= string.Empty;
            manifest.sourceExt ??= ".vrm";
            manifest.exporterVersion ??= "0.3.0";
            manifest.meshRefs ??= new List<string>();
            manifest.materialRefs ??= new List<string>();
            manifest.textureRefs ??= new List<string>();
            manifest.strictShaderSet ??= new List<string>();
            if (manifest.schemaVersion == 0U)
            {
                manifest.schemaVersion = 1U;
            }
        }

        private static bool EvaluatePartialCompatibility(
            Xav2AvatarPayload payload,
            Xav2LoadDiagnostics diagnostics,
            Xav2LoadOptions options)
        {
            var meshNameSet = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var mesh in payload.Meshes)
            {
                if (!string.IsNullOrWhiteSpace(mesh.Name))
                {
                    meshNameSet.Add(mesh.Name);
                }
            }

            var textureNameSet = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var texture in payload.Textures)
            {
                if (!string.IsNullOrWhiteSpace(texture.Name))
                {
                    textureNameSet.Add(texture.Name);
                }
            }

            var missingMeshRef = false;
            foreach (var meshRef in payload.Manifest.meshRefs)
            {
                if (!meshNameSet.Contains(meshRef))
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
                if (!textureNameSet.Contains(textureRef))
                {
                    missingTextureRef = true;
                    if (!AddWarningOrFail(diagnostics, options, $"XAV2_ASSET_MISSING: textureRef='{textureRef}'"))
                    {
                        return false;
                    }
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

            diagnostics.IsPartial = missingMeshRef || missingTextureRef;
            return true;
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
    }
}
