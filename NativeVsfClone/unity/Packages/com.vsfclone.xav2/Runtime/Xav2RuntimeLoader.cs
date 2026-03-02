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

        public static Xav2AvatarPayload Load(string path)
        {
            using var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
            using var br = new BinaryReader(fs, Encoding.UTF8);

            var magic = Encoding.ASCII.GetString(br.ReadBytes(4));
            if (!string.Equals(magic, "XAV2", StringComparison.Ordinal))
            {
                throw new InvalidDataException("XAV2 magic mismatch.");
            }
            var version = br.ReadUInt16();
            if (version != 1)
            {
                throw new InvalidDataException($"Unsupported XAV2 version: {version}");
            }
            var manifestSize = br.ReadUInt32();
            var manifestJson = Encoding.UTF8.GetString(br.ReadBytes((int)manifestSize));
            var payload = new Xav2AvatarPayload
            {
                Manifest = JsonUtility.FromJson<Xav2Manifest>(manifestJson) ?? new Xav2Manifest()
            };

            var materialsByName = new Dictionary<string, Xav2MaterialPayload>(StringComparer.OrdinalIgnoreCase);
            while (fs.Position < fs.Length)
            {
                var type = br.ReadUInt16();
                _ = br.ReadUInt16(); // flags
                var size = br.ReadUInt32();
                var sectionBytes = br.ReadBytes((int)size);
                using var secStream = new MemoryStream(sectionBytes);
                using var secReader = new BinaryReader(secStream, Encoding.UTF8);

                if (type == SectionMeshRenderPayload)
                {
                    var mesh = new Xav2MeshPayload
                    {
                        Name = ReadSizedString(secReader),
                        VertexStride = secReader.ReadUInt32(),
                        MaterialIndex = secReader.ReadInt32()
                    };
                    var vbSize = secReader.ReadUInt32();
                    mesh.VertexBlob = secReader.ReadBytes((int)vbSize);
                    var indexCount = secReader.ReadUInt32();
                    mesh.Indices = new uint[indexCount];
                    for (var i = 0; i < indexCount; i++)
                    {
                        mesh.Indices[i] = secReader.ReadUInt32();
                    }
                    payload.Meshes.Add(mesh);
                    continue;
                }

                if (type == SectionTextureBlob)
                {
                    var texture = new Xav2TexturePayload
                    {
                        Name = ReadSizedString(secReader)
                    };
                    var blobSize = secReader.ReadUInt32();
                    texture.Bytes = secReader.ReadBytes((int)blobSize);
                    payload.Textures.Add(texture);
                    continue;
                }

                if (type == SectionMaterialOverride)
                {
                    var material = new Xav2MaterialPayload
                    {
                        Name = ReadSizedString(secReader),
                        ShaderName = ReadSizedString(secReader),
                        BaseColorTextureName = ReadSizedString(secReader),
                        AlphaMode = ReadSizedString(secReader),
                        AlphaCutoff = secReader.ReadSingle(),
                        DoubleSided = secReader.ReadByte() != 0
                    };
                    materialsByName[material.Name] = material;
                    continue;
                }

                if (type == SectionMaterialShaderParams)
                {
                    var name = ReadSizedString(secReader);
                    var paramsJson = ReadSizedString(secReader);
                    if (!materialsByName.TryGetValue(name, out var material))
                    {
                        material = new Xav2MaterialPayload { Name = name };
                        materialsByName[name] = material;
                    }
                    material.ShaderParamsJson = paramsJson;
                }
            }

            foreach (var mat in materialsByName.Values)
            {
                payload.Materials.Add(mat);
            }
            return payload;
        }

        private static string ReadSizedString(BinaryReader br)
        {
            var len = br.ReadUInt16();
            if (len == 0)
            {
                return string.Empty;
            }
            return Encoding.UTF8.GetString(br.ReadBytes(len));
        }
    }
}
