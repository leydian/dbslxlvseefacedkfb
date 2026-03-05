using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using UnityEngine;
using VsfClone.Xav2.Runtime;

namespace VsfClone.Xav2.Editor
{
    public static class Xav2Exporter
    {
        private const uint DefaultSchemaVersion = 1U;
        private const string DefaultExporterVersion = "0.3.0";
        private const ushort SectionTextureBlob = 0x0002;
        private const ushort SectionMaterialOverride = 0x0003;
        private const ushort SectionMeshRenderPayload = 0x0011;
        private const ushort SectionMaterialShaderParams = 0x0012;
        private const ushort SectionSkinPayload = 0x0013;
        private const ushort SectionBlendShapePayload = 0x0014;
        private const ushort SectionMaterialTypedParams = 0x0015;
        private const ushort SectionSkeletonPosePayload = 0x0016;
        private const ushort SectionSkeletonRigPayload = 0x0017;
        private const ushort SectionFlagPayloadCompressedLz4 = 0x0001;

        public static void Export(string outputPath, GameObject avatarRoot, Xav2ExportOptions options)
        {
            if (avatarRoot == null)
            {
                throw new ArgumentNullException(nameof(avatarRoot));
            }
            var extractor = new UniVrmAvatarExtractor();
            var payload = extractor.Extract(avatarRoot, options ?? new Xav2ExportOptions());
            Export(outputPath, payload, options ?? new Xav2ExportOptions());
        }

        public static void Export(string outputPath, Xav2AvatarPayload payload, Xav2ExportOptions options)
        {
            if (payload == null)
            {
                throw new ArgumentNullException(nameof(payload));
            }
            if (options == null)
            {
                throw new ArgumentNullException(nameof(options));
            }
            ValidateShaderPolicy(payload, options);
            EnsureManifestDefaults(payload, options);
            ValidateV4SkinningCoverage(payload);

            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(outputPath)) ?? ".");
            using var fs = new FileStream(outputPath, FileMode.Create, FileAccess.Write, FileShare.None);
            using var bw = new BinaryWriter(fs, Encoding.UTF8);

            var manifestJson = JsonUtility.ToJson(payload.Manifest);
            var manifestBytes = Encoding.UTF8.GetBytes(manifestJson);
            bw.Write(Encoding.ASCII.GetBytes("XAV2"));
            var fileVersion = options.EnableCompression ? (ushort)5 : (ushort)4;
            bw.Write(fileVersion);
            bw.Write((uint)manifestBytes.Length);
            bw.Write(manifestBytes);

            foreach (var mesh in payload.Meshes)
            {
                WriteSection(bw, SectionMeshRenderPayload, BuildMeshPayload(mesh), options);
            }
            foreach (var texture in payload.Textures)
            {
                WriteSection(bw, SectionTextureBlob, BuildTexturePayload(texture), options);
            }
            foreach (var material in payload.Materials)
            {
                WriteSection(bw, SectionMaterialOverride, BuildMaterialPayload(material), options);
                WriteSection(bw, SectionMaterialShaderParams, BuildMaterialParamsPayload(material), options);
                if (material.TypedFloatParams.Count > 0 || material.TypedColorParams.Count > 0 || material.TypedTextureParams.Count > 0)
                {
                    WriteSection(bw, SectionMaterialTypedParams, BuildMaterialTypedParamsPayload(material), options);
                }
            }
            foreach (var skin in payload.Skins)
            {
                WriteSection(bw, SectionSkinPayload, BuildSkinPayload(skin), options);
            }
            foreach (var skeleton in payload.Skeletons)
            {
                WriteSection(bw, SectionSkeletonPosePayload, BuildSkeletonPayload(skeleton), options);
            }
            foreach (var rig in payload.SkeletonRigs)
            {
                WriteSection(bw, SectionSkeletonRigPayload, BuildSkeletonRigPayload(rig), options);
            }
            foreach (var blendShape in payload.BlendShapes)
            {
                WriteSection(bw, SectionBlendShapePayload, BuildBlendShapePayload(blendShape), options);
            }
        }

        private static void EnsureManifestDefaults(Xav2AvatarPayload payload, Xav2ExportOptions options)
        {
            payload.Manifest.schemaVersion =
                payload.Manifest.schemaVersion == 0U ? DefaultSchemaVersion : payload.Manifest.schemaVersion;
            if (string.IsNullOrWhiteSpace(payload.Manifest.exporterVersion))
            {
                payload.Manifest.exporterVersion = DefaultExporterVersion;
            }
            if (payload.Manifest.meshRefs == null)
            {
                payload.Manifest.meshRefs = new List<string>();
            }
            if (payload.Manifest.materialRefs == null)
            {
                payload.Manifest.materialRefs = new List<string>();
            }
            if (payload.Manifest.textureRefs == null)
            {
                payload.Manifest.textureRefs = new List<string>();
            }
            if (payload.Manifest.strictShaderSet == null)
            {
                payload.Manifest.strictShaderSet = new List<string>();
            }
            payload.Manifest.avatarId = string.IsNullOrWhiteSpace(payload.Manifest.avatarId) ? "avatar" : payload.Manifest.avatarId;
            payload.Manifest.displayName = string.IsNullOrWhiteSpace(payload.Manifest.displayName)
                ? payload.Manifest.avatarId
                : payload.Manifest.displayName;
            payload.Manifest.sourceExt = string.IsNullOrWhiteSpace(payload.Manifest.sourceExt) ? ".vrm" : payload.Manifest.sourceExt;

            if (payload.Manifest.meshRefs.Count == 0)
            {
                foreach (var mesh in payload.Meshes)
                {
                    payload.Manifest.meshRefs.Add(mesh.Name ?? string.Empty);
                }
            }
            if (payload.Manifest.materialRefs.Count == 0)
            {
                foreach (var material in payload.Materials)
                {
                    payload.Manifest.materialRefs.Add(material.Name ?? string.Empty);
                }
            }
            if (payload.Manifest.textureRefs.Count == 0)
            {
                foreach (var texture in payload.Textures)
                {
                    payload.Manifest.textureRefs.Add(texture.Name ?? string.Empty);
                }
            }

            payload.Manifest.hasSkinning = payload.Skins.Count > 0;
            payload.Manifest.hasBlendShapes = payload.BlendShapes.Count > 0;
            payload.Manifest.strictShaderSet = new List<string>(options.StrictShaderSet ?? new List<string>());
            payload.Manifest.materialParamEncoding =
                payload.Materials.Exists(m => m.MaterialParamEncoding == "typed-v3" ||
                                              m.MaterialParamEncoding == "typed-v2" ||
                                              m.TypedFloatParams.Count > 0 ||
                                              m.TypedColorParams.Count > 0 ||
                                              m.TypedTextureParams.Count > 0)
                    ? "typed-v3"
                    : "legacy-json";
        }

        private static void ValidateShaderPolicy(Xav2AvatarPayload payload, Xav2ExportOptions options)
        {
            if (!options.FailOnMissingShader)
            {
                return;
            }
            var allowed = new HashSet<string>(options.StrictShaderSet ?? new List<string>(), StringComparer.OrdinalIgnoreCase);
            foreach (var mat in payload.Materials)
            {
                if (allowed.Count == 0)
                {
                    continue;
                }
                var valid = false;
                foreach (var token in allowed)
                {
                    if (!string.IsNullOrWhiteSpace(token) &&
                        mat.ShaderName.IndexOf(token, StringComparison.OrdinalIgnoreCase) >= 0)
                    {
                        valid = true;
                        break;
                    }
                }
                if (!valid)
                {
                    throw new InvalidOperationException(
                        $"XAV2 strict shader policy violation: material='{mat.Name}', shader='{mat.ShaderName}'.");
                }
            }
        }

        private static void ValidateV4SkinningCoverage(Xav2AvatarPayload payload)
        {
            if (payload.Skins == null || payload.Skins.Count == 0)
            {
                return;
            }

            var skeletonByMesh = new Dictionary<string, Xav2SkeletonPayload>(StringComparer.OrdinalIgnoreCase);
            foreach (var skeleton in payload.Skeletons ?? new List<Xav2SkeletonPayload>())
            {
                var key = NormalizeRefKey(skeleton.MeshName);
                if (string.IsNullOrWhiteSpace(key))
                {
                    continue;
                }
                skeletonByMesh[key] = skeleton;
            }

            foreach (var skin in payload.Skins)
            {
                var key = NormalizeRefKey(skin.MeshName);
                if (string.IsNullOrWhiteSpace(key))
                {
                    throw new InvalidOperationException("XAV3 skeleton validation failed: skin mesh name is empty.");
                }
                if (!skeletonByMesh.TryGetValue(key, out var skeleton))
                {
                    throw new InvalidOperationException(
                        $"XAV3 skeleton validation failed: skeleton payload missing for mesh '{skin.MeshName}'.");
                }

                var bindPoseCount = (skin.BindPoses16xN?.Length ?? 0) / 16;
                var skeletonCount = (skeleton.BoneMatrices16xN?.Length ?? 0) / 16;
                if ((skin.BindPoses16xN == null || (skin.BindPoses16xN.Length % 16) != 0) ||
                    (skeleton.BoneMatrices16xN == null || (skeleton.BoneMatrices16xN.Length % 16) != 0))
                {
                    throw new InvalidOperationException(
                        $"XAV3 skeleton validation failed: matrix payload shape is invalid for mesh '{skin.MeshName}'.");
                }
                if (skeletonCount < bindPoseCount)
                {
                    throw new InvalidOperationException(
                        $"XAV3 skeleton validation failed: skeleton matrix count ({skeletonCount}) < bindpose count ({bindPoseCount}) for mesh '{skin.MeshName}'.");
                }
            }

            var rigByMesh = new Dictionary<string, Xav2SkeletonRigPayload>(StringComparer.OrdinalIgnoreCase);
            foreach (var rig in payload.SkeletonRigs ?? new List<Xav2SkeletonRigPayload>())
            {
                var key = NormalizeRefKey(rig.MeshName);
                if (string.IsNullOrWhiteSpace(key))
                {
                    continue;
                }
                rigByMesh[key] = rig;
            }

            foreach (var skin in payload.Skins)
            {
                var key = NormalizeRefKey(skin.MeshName);
                if (!rigByMesh.TryGetValue(key, out var rig))
                {
                    throw new InvalidOperationException(
                        $"XAV4 rig validation failed: skeleton rig payload missing for mesh '{skin.MeshName}'.");
                }

                var rigBoneCount = rig.Bones?.Count ?? 0;
                var skinBoneCount = skin.BoneIndices?.Length ?? 0;
                if (rigBoneCount == 0 || rigBoneCount < skinBoneCount)
                {
                    throw new InvalidOperationException(
                        $"XAV4 rig validation failed: rig bone count ({rigBoneCount}) < skin bone count ({skinBoneCount}) for mesh '{skin.MeshName}'.");
                }

                ValidateRigGraph(rig, skin.MeshName);
            }
        }

        private static void ValidateRigGraph(Xav2SkeletonRigPayload rig, string meshName)
        {
            if (rig == null || rig.Bones == null || rig.Bones.Count == 0)
            {
                throw new InvalidOperationException($"XAV4 rig validation failed: empty rig for mesh '{meshName}'.");
            }

            var nameSet = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            for (var i = 0; i < rig.Bones.Count; i++)
            {
                var bone = rig.Bones[i];
                if (string.IsNullOrWhiteSpace(bone.Name))
                {
                    throw new InvalidOperationException($"XAV4 rig validation failed: empty bone name at index {i} for mesh '{meshName}'.");
                }
                if (!nameSet.Add(bone.Name))
                {
                    throw new InvalidOperationException($"XAV4 rig validation failed: duplicate bone name '{bone.Name}' for mesh '{meshName}'.");
                }

                if (bone.ParentIndex < -1 || bone.ParentIndex >= rig.Bones.Count || bone.ParentIndex == i)
                {
                    throw new InvalidOperationException(
                        $"XAV4 rig validation failed: invalid parent index {bone.ParentIndex} at bone '{bone.Name}' for mesh '{meshName}'.");
                }
            }

            for (var i = 0; i < rig.Bones.Count; i++)
            {
                var visited = new HashSet<int>();
                var cursor = i;
                while (cursor >= 0)
                {
                    if (!visited.Add(cursor))
                    {
                        throw new InvalidOperationException(
                            $"XAV4 rig validation failed: parent cycle detected at bone '{rig.Bones[i].Name}' for mesh '{meshName}'.");
                    }
                    cursor = rig.Bones[cursor].ParentIndex;
                }
            }
        }

        private static string NormalizeRefKey(string value)
        {
            return string.IsNullOrWhiteSpace(value)
                ? string.Empty
                : value.Replace('\\', '/').Trim().ToLowerInvariant();
        }

        private static byte[] BuildMeshPayload(Xav2MeshPayload mesh)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, mesh.Name);
            bw.Write(mesh.VertexStride);
            bw.Write(mesh.MaterialIndex);
            bw.Write((uint)mesh.VertexBlob.Length);
            bw.Write(mesh.VertexBlob);
            bw.Write((uint)mesh.Indices.Length);
            foreach (var index in mesh.Indices)
            {
                bw.Write(index);
            }
            return ms.ToArray();
        }

        private static byte[] BuildTexturePayload(Xav2TexturePayload texture)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, texture.Name);
            bw.Write((uint)texture.Bytes.Length);
            bw.Write(texture.Bytes);
            return ms.ToArray();
        }

        private static byte[] BuildMaterialPayload(Xav2MaterialPayload material)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, material.Name);
            WriteSizedString(bw, material.ShaderName);
            WriteSizedString(bw, string.IsNullOrEmpty(material.ShaderVariant) ? "default" : material.ShaderVariant);
            WriteSizedString(bw, material.BaseColorTextureName ?? string.Empty);
            WriteSizedString(bw, string.IsNullOrEmpty(material.AlphaMode) ? "OPAQUE" : material.AlphaMode);
            bw.Write(material.AlphaCutoff);
            bw.Write((byte)(material.DoubleSided ? 1 : 0));
            return ms.ToArray();
        }

        private static byte[] BuildMaterialParamsPayload(Xav2MaterialPayload material)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, material.Name);
            WriteSizedString(bw, string.IsNullOrEmpty(material.ShaderParamsJson) ? "{}" : material.ShaderParamsJson);
            return ms.ToArray();
        }

        private static byte[] BuildMaterialTypedParamsPayload(Xav2MaterialPayload material)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, material.Name);
            WriteSizedString(bw, string.IsNullOrWhiteSpace(material.ShaderFamily) ? "legacy" : material.ShaderFamily);
            bw.Write(material.FeatureFlags);
            var schemaVersion = material.TypedSchemaVersion == 0 ? (ushort)3 : material.TypedSchemaVersion;
            if (schemaVersion >= 3)
            {
                bw.Write(schemaVersion);
            }

            bw.Write((ushort)material.TypedFloatParams.Count);
            foreach (var p in material.TypedFloatParams)
            {
                WriteSizedString(bw, p.Id ?? string.Empty);
                bw.Write(p.Value);
            }

            bw.Write((ushort)material.TypedColorParams.Count);
            foreach (var p in material.TypedColorParams)
            {
                WriteSizedString(bw, p.Id ?? string.Empty);
                bw.Write(p.R);
                bw.Write(p.G);
                bw.Write(p.B);
                bw.Write(p.A);
            }

            bw.Write((ushort)material.TypedTextureParams.Count);
            foreach (var p in material.TypedTextureParams)
            {
                WriteSizedString(bw, p.Slot ?? string.Empty);
                WriteSizedString(bw, p.TextureRef ?? string.Empty);
            }
            return ms.ToArray();
        }

        private static byte[] BuildSkinPayload(Xav2SkinPayload skin)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, skin.MeshName);
            bw.Write((uint)skin.BoneIndices.Length);
            foreach (var bone in skin.BoneIndices)
            {
                bw.Write(bone);
            }
            bw.Write((uint)skin.BindPoses16xN.Length);
            foreach (var value in skin.BindPoses16xN)
            {
                bw.Write(value);
            }
            bw.Write((uint)skin.SkinWeightBlob.Length);
            bw.Write(skin.SkinWeightBlob);
            return ms.ToArray();
        }

        private static byte[] BuildBlendShapePayload(Xav2BlendShapePayload blendShape)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, blendShape.MeshName);
            bw.Write((uint)blendShape.Frames.Count);
            foreach (var frame in blendShape.Frames)
            {
                WriteSizedString(bw, frame.Name);
                bw.Write(frame.Weight);
                bw.Write((uint)frame.DeltaVertices.Length);
                bw.Write(frame.DeltaVertices);
                bw.Write((uint)frame.DeltaNormals.Length);
                bw.Write(frame.DeltaNormals);
                bw.Write((uint)frame.DeltaTangents.Length);
                bw.Write(frame.DeltaTangents);
            }
            return ms.ToArray();
        }

        private static byte[] BuildSkeletonPayload(Xav2SkeletonPayload skeleton)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, skeleton.MeshName);
            bw.Write((uint)skeleton.BoneMatrices16xN.Length);
            foreach (var value in skeleton.BoneMatrices16xN)
            {
                bw.Write(value);
            }
            return ms.ToArray();
        }

        private static byte[] BuildSkeletonRigPayload(Xav2SkeletonRigPayload rig)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, rig.MeshName);
            bw.Write((uint)(rig.Bones?.Count ?? 0));
            foreach (var bone in rig.Bones ?? new List<Xav2RigBonePayload>())
            {
                WriteSizedString(bw, bone.Name);
                bw.Write(bone.ParentIndex);
                var matrix = bone.LocalMatrix16 ?? Array.Empty<float>();
                bw.Write((uint)matrix.Length);
                foreach (var value in matrix)
                {
                    bw.Write(value);
                }
            }
            return ms.ToArray();
        }

        private static void WriteSection(BinaryWriter bw, ushort type, byte[] payload, Xav2ExportOptions options)
        {
            var flags = (ushort)0;
            var sectionPayload = payload ?? Array.Empty<byte>();
            if (ShouldCompressSection(type, sectionPayload, options))
            {
                var preferRatio = options.CompressionLevel == Xav2CompressionLevel.Balanced;
                if (Xav2Lz4Codec.TryCompress(sectionPayload, out var compressed, preferRatio) &&
                    compressed.Length + 4 < sectionPayload.Length)
                {
                    flags |= SectionFlagPayloadCompressedLz4;
                    sectionPayload = BuildCompressedEnvelope(sectionPayload.Length, compressed);
                }
            }

            bw.Write(type);
            bw.Write(flags);
            bw.Write((uint)sectionPayload.Length);
            bw.Write(sectionPayload);
        }

        private static bool ShouldCompressSection(ushort sectionType, byte[] payload, Xav2ExportOptions options)
        {
            if (options == null || !options.EnableCompression || options.CompressionCodec != Xav2CompressionCodec.Lz4)
            {
                return false;
            }

            if (payload == null || payload.Length < 256)
            {
                return false;
            }

            return sectionType == SectionMeshRenderPayload ||
                   sectionType == SectionTextureBlob ||
                   sectionType == SectionSkinPayload ||
                   sectionType == SectionBlendShapePayload;
        }

        private static byte[] BuildCompressedEnvelope(int uncompressedLength, byte[] compressed)
        {
            var envelope = new byte[4 + (compressed?.Length ?? 0)];
            envelope[0] = (byte)(uncompressedLength & 0xFF);
            envelope[1] = (byte)((uncompressedLength >> 8) & 0xFF);
            envelope[2] = (byte)((uncompressedLength >> 16) & 0xFF);
            envelope[3] = (byte)((uncompressedLength >> 24) & 0xFF);
            if (compressed != null && compressed.Length > 0)
            {
                Buffer.BlockCopy(compressed, 0, envelope, 4, compressed.Length);
            }
            return envelope;
        }

        private static void WriteSizedString(BinaryWriter bw, string s)
        {
            var value = s ?? string.Empty;
            var bytes = Encoding.UTF8.GetBytes(value);
            if (bytes.Length > ushort.MaxValue)
            {
                throw new InvalidOperationException($"String too long for XAV2 field: {value}");
            }
            bw.Write((ushort)bytes.Length);
            bw.Write(bytes);
        }
    }
}
