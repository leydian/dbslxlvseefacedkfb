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

            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(outputPath)) ?? ".");
            using var fs = new FileStream(outputPath, FileMode.Create, FileAccess.Write, FileShare.None);
            using var bw = new BinaryWriter(fs, Encoding.UTF8);

            var manifestJson = JsonUtility.ToJson(payload.Manifest);
            var manifestBytes = Encoding.UTF8.GetBytes(manifestJson);
            bw.Write(Encoding.ASCII.GetBytes("XAV2"));
            bw.Write((ushort)2);
            bw.Write((uint)manifestBytes.Length);
            bw.Write(manifestBytes);

            foreach (var mesh in payload.Meshes)
            {
                WriteSection(bw, SectionMeshRenderPayload, BuildMeshPayload(mesh));
            }
            foreach (var texture in payload.Textures)
            {
                WriteSection(bw, SectionTextureBlob, BuildTexturePayload(texture));
            }
            foreach (var material in payload.Materials)
            {
                WriteSection(bw, SectionMaterialOverride, BuildMaterialPayload(material));
                WriteSection(bw, SectionMaterialShaderParams, BuildMaterialParamsPayload(material));
            }
            foreach (var skin in payload.Skins)
            {
                WriteSection(bw, SectionSkinPayload, BuildSkinPayload(skin));
            }
            foreach (var blendShape in payload.BlendShapes)
            {
                WriteSection(bw, SectionBlendShapePayload, BuildBlendShapePayload(blendShape));
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

        private static void WriteSection(BinaryWriter bw, ushort type, byte[] payload)
        {
            bw.Write(type);
            bw.Write((ushort)0);
            bw.Write((uint)payload.Length);
            bw.Write(payload);
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
