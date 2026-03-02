using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using UnityEngine;
using VsfClone.Xav2.Runtime;

namespace VsfClone.Xav2.Editor
{
    public static class Xav2Exporter
    {
        private const ushort SectionTextureBlob = 0x0002;
        private const ushort SectionMaterialOverride = 0x0003;
        private const ushort SectionMeshRenderPayload = 0x0011;
        private const ushort SectionMaterialShaderParams = 0x0012;

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

            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(outputPath)) ?? ".");
            using var fs = new FileStream(outputPath, FileMode.Create, FileAccess.Write, FileShare.None);
            using var bw = new BinaryWriter(fs, Encoding.UTF8);

            var manifestJson = JsonUtility.ToJson(payload.Manifest);
            var manifestBytes = Encoding.UTF8.GetBytes(manifestJson);
            bw.Write(Encoding.ASCII.GetBytes("XAV2"));
            bw.Write((ushort)1);
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
                if (allowed.Count > 0 && !allowed.Contains(mat.ShaderName))
                {
                    throw new InvalidOperationException($"Unsupported shader for strict export: {mat.ShaderName}");
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
