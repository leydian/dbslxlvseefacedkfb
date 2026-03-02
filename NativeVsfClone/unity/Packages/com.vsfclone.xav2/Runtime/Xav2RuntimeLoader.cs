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
                        ShaderVariant = "default"
                    };
                    var cursor = secStream.Position;
                    var parsedWithVariant = false;
                    try
                    {
                        material.ShaderVariant = ReadSizedString(secReader);
                        material.BaseColorTextureName = ReadSizedString(secReader);
                        material.AlphaMode = ReadSizedString(secReader);
                        material.AlphaCutoff = secReader.ReadSingle();
                        material.DoubleSided = secReader.ReadByte() != 0;
                        parsedWithVariant = secStream.Position == secStream.Length;
                    }
                    catch
                    {
                        parsedWithVariant = false;
                    }

                    if (!parsedWithVariant)
                    {
                        secStream.Position = cursor;
                        material.ShaderVariant = "default";
                        material.BaseColorTextureName = ReadSizedString(secReader);
                        material.AlphaMode = ReadSizedString(secReader);
                        material.AlphaCutoff = secReader.ReadSingle();
                        material.DoubleSided = secReader.ReadByte() != 0;
                    }
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
                    continue;
                }

                if (type == SectionSkinPayload)
                {
                    var skin = new Xav2SkinPayload
                    {
                        MeshName = ReadSizedString(secReader)
                    };
                    var boneCount = secReader.ReadUInt32();
                    skin.BoneIndices = new int[boneCount];
                    for (var i = 0; i < boneCount; i++)
                    {
                        skin.BoneIndices[i] = secReader.ReadInt32();
                    }
                    var bindPoseFloatCount = secReader.ReadUInt32();
                    skin.BindPoses16xN = new float[bindPoseFloatCount];
                    for (var i = 0; i < bindPoseFloatCount; i++)
                    {
                        skin.BindPoses16xN[i] = secReader.ReadSingle();
                    }
                    var blobSize = secReader.ReadUInt32();
                    skin.SkinWeightBlob = secReader.ReadBytes((int)blobSize);
                    payload.Skins.Add(skin);
                    continue;
                }

                if (type == SectionBlendShapePayload)
                {
                    var blend = new Xav2BlendShapePayload
                    {
                        MeshName = ReadSizedString(secReader)
                    };
                    var frameCount = secReader.ReadUInt32();
                    for (var i = 0; i < frameCount; i++)
                    {
                        var frame = new Xav2BlendShapeFramePayload
                        {
                            Name = ReadSizedString(secReader),
                            Weight = secReader.ReadSingle()
                        };
                        var dvSize = secReader.ReadUInt32();
                        frame.DeltaVertices = secReader.ReadBytes((int)dvSize);
                        var dnSize = secReader.ReadUInt32();
                        frame.DeltaNormals = secReader.ReadBytes((int)dnSize);
                        var dtSize = secReader.ReadUInt32();
                        frame.DeltaTangents = secReader.ReadBytes((int)dtSize);
                        blend.Frames.Add(frame);
                    }
                    payload.BlendShapes.Add(blend);
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
