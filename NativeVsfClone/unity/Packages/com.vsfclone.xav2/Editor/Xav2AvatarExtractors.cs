using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using UnityEngine;
using VsfClone.Xav2.Runtime;

namespace VsfClone.Xav2.Editor
{
    public interface IXav2AvatarExtractor
    {
        Xav2AvatarPayload Extract(GameObject avatarRoot, Xav2ExportOptions options);
    }

    // Uses the scene AvatarRoot shape produced by UniVRM import workflows.
    public sealed class UniVrmAvatarExtractor : IXav2AvatarExtractor
    {
        private const int TargetVertexStride = 48; // pos3 + nrm3 + uv2 + tan4

        public Xav2AvatarPayload Extract(GameObject avatarRoot, Xav2ExportOptions options)
        {
            if (avatarRoot == null)
            {
                throw new ArgumentNullException(nameof(avatarRoot));
            }

            var payload = new Xav2AvatarPayload();
            payload.Manifest.avatarId = avatarRoot.name;
            payload.Manifest.displayName = avatarRoot.name;
            payload.Manifest.sourceExt = ".vrm";

            var materialIndexById = new Dictionary<int, int>();
            var textureNameSet = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

            var renderers = avatarRoot.GetComponentsInChildren<SkinnedMeshRenderer>(true);
            var meshCounter = 0;
            foreach (var smr in renderers)
            {
                var mesh = smr.sharedMesh;
                if (mesh == null || mesh.vertexCount == 0)
                {
                    continue;
                }
                var vertexBlob = BuildVertexBlob(mesh);
                var materials = smr.sharedMaterials ?? Array.Empty<Material>();
                var subMeshCount = Mathf.Max(mesh.subMeshCount, 1);
                for (var subMeshIndex = 0; subMeshIndex < subMeshCount; subMeshIndex++)
                {
                    var meshName = $"{mesh.name}_sm{subMeshIndex}_{meshCounter++}";
                    var mat = (subMeshIndex < materials.Length) ? materials[subMeshIndex] : null;
                    var materialIndex = EnsureMaterial(mat, payload, materialIndexById, textureNameSet);
                    var indices = mesh.subMeshCount > 0
                        ? mesh.GetIndices(Mathf.Min(subMeshIndex, mesh.subMeshCount - 1))
                        : BuildSequentialIndices(mesh.vertexCount);
                    var meshPayload = new Xav2MeshPayload
                    {
                        Name = meshName,
                        VertexStride = TargetVertexStride,
                        MaterialIndex = materialIndex,
                        VertexBlob = vertexBlob,
                        Indices = ToUIntIndices(indices)
                    };
                    payload.Meshes.Add(meshPayload);
                    payload.Manifest.meshRefs.Add(meshName);

                    var skinPayload = BuildSkinPayload(meshName, mesh, smr);
                    if (skinPayload != null)
                    {
                        payload.Skins.Add(skinPayload);
                    }
                    var blendShapePayload = BuildBlendShapePayload(meshName, mesh);
                    if (blendShapePayload != null)
                    {
                        payload.BlendShapes.Add(blendShapePayload);
                    }
                }
            }

            payload.Manifest.hasSkinning = payload.Skins.Count > 0;
            payload.Manifest.hasBlendShapes = payload.BlendShapes.Count > 0;
            payload.Manifest.strictShaderSet = new List<string>(options?.StrictShaderSet ?? new List<string>());
            return payload;
        }

        private static Xav2SkinPayload BuildSkinPayload(string meshName, Mesh mesh, SkinnedMeshRenderer smr)
        {
            var boneWeights = mesh.boneWeights;
            if (boneWeights == null || boneWeights.Length == 0)
            {
                return null;
            }
            var payload = new Xav2SkinPayload
            {
                MeshName = meshName
            };

            var bones = smr.bones ?? Array.Empty<Transform>();
            payload.BoneIndices = new int[bones.Length];
            for (var i = 0; i < bones.Length; i++)
            {
                payload.BoneIndices[i] = i;
            }

            var bindPoses = mesh.bindposes;
            payload.BindPoses16xN = new float[bindPoses.Length * 16];
            for (var i = 0; i < bindPoses.Length; i++)
            {
                var o = i * 16;
                var m = bindPoses[i];
                payload.BindPoses16xN[o + 0] = m.m00; payload.BindPoses16xN[o + 1] = m.m01; payload.BindPoses16xN[o + 2] = m.m02; payload.BindPoses16xN[o + 3] = m.m03;
                payload.BindPoses16xN[o + 4] = m.m10; payload.BindPoses16xN[o + 5] = m.m11; payload.BindPoses16xN[o + 6] = m.m12; payload.BindPoses16xN[o + 7] = m.m13;
                payload.BindPoses16xN[o + 8] = m.m20; payload.BindPoses16xN[o + 9] = m.m21; payload.BindPoses16xN[o + 10] = m.m22; payload.BindPoses16xN[o + 11] = m.m23;
                payload.BindPoses16xN[o + 12] = m.m30; payload.BindPoses16xN[o + 13] = m.m31; payload.BindPoses16xN[o + 14] = m.m32; payload.BindPoses16xN[o + 15] = m.m33;
            }
            payload.SkinWeightBlob = BuildSkinWeightBlob(boneWeights);
            return payload;
        }

        private static Xav2BlendShapePayload BuildBlendShapePayload(string meshName, Mesh mesh)
        {
            if (mesh.blendShapeCount <= 0)
            {
                return null;
            }
            var payload = new Xav2BlendShapePayload { MeshName = meshName };
            var vertexCount = mesh.vertexCount;
            var dv = new Vector3[vertexCount];
            var dn = new Vector3[vertexCount];
            var dt = new Vector3[vertexCount];

            for (var shapeIndex = 0; shapeIndex < mesh.blendShapeCount; shapeIndex++)
            {
                var shapeName = mesh.GetBlendShapeName(shapeIndex);
                var frameCount = mesh.GetBlendShapeFrameCount(shapeIndex);
                for (var frameIndex = 0; frameIndex < frameCount; frameIndex++)
                {
                    Array.Clear(dv, 0, dv.Length);
                    Array.Clear(dn, 0, dn.Length);
                    Array.Clear(dt, 0, dt.Length);
                    var weight = mesh.GetBlendShapeFrameWeight(shapeIndex, frameIndex);
                    mesh.GetBlendShapeFrameVertices(shapeIndex, frameIndex, dv, dn, dt);
                    payload.Frames.Add(new Xav2BlendShapeFramePayload
                    {
                        Name = $"{shapeName}#{frameIndex}",
                        Weight = weight,
                        DeltaVertices = ToVector3Bytes(dv),
                        DeltaNormals = ToVector3Bytes(dn),
                        DeltaTangents = ToVector3Bytes(dt)
                    });
                }
            }
            return payload;
        }

        private static uint[] ToUIntIndices(int[] indices)
        {
            var outIndices = new uint[indices.Length];
            for (var i = 0; i < indices.Length; i++)
            {
                outIndices[i] = (uint)Mathf.Max(indices[i], 0);
            }
            return outIndices;
        }

        private static int[] BuildSequentialIndices(int vertexCount)
        {
            var indices = new int[Mathf.Max(vertexCount, 0)];
            for (var i = 0; i < indices.Length; i++)
            {
                indices[i] = i;
            }
            return indices;
        }

        private static int EnsureMaterial(
            Material material,
            Xav2AvatarPayload payload,
            Dictionary<int, int> materialIndexById,
            HashSet<string> textureNameSet)
        {
            if (material == null)
            {
                var fallbackName = "Default";
                var fallbackIndex = payload.Materials.FindIndex(m => string.Equals(m.Name, fallbackName, StringComparison.Ordinal));
                if (fallbackIndex >= 0)
                {
                    return fallbackIndex;
                }
                payload.Materials.Add(new Xav2MaterialPayload
                {
                    Name = fallbackName,
                    ShaderName = "MToon (minimal)",
                    ShaderVariant = "default",
                    AlphaMode = "OPAQUE",
                    ShaderParamsJson = "{\"schema\":1,\"shader\":\"MToon (minimal)\",\"keywords\":[],\"properties\":[]}"
                });
                payload.Manifest.materialRefs.Add(fallbackName);
                return payload.Materials.Count - 1;
            }

            var id = material.GetInstanceID();
            if (materialIndexById.TryGetValue(id, out var existing))
            {
                return existing;
            }

            var textureName = string.Empty;
            if (material.HasProperty("_MainTex"))
            {
                var mainTex = material.GetTexture("_MainTex");
                if (mainTex is Texture2D tex2d)
                {
                    textureName = tex2d.name;
                    if (textureNameSet.Add(textureName))
                    {
                        payload.Textures.Add(new Xav2TexturePayload
                        {
                            Name = textureName,
                            Bytes = EncodeTextureSafe(tex2d)
                        });
                        payload.Manifest.textureRefs.Add(textureName);
                    }
                }
            }

            var shaderName = material.shader != null ? material.shader.name : "UnknownShader";
            var item = new Xav2MaterialPayload
            {
                Name = material.name,
                ShaderName = shaderName,
                ShaderVariant = ResolveShaderVariant(shaderName),
                BaseColorTextureName = textureName,
                AlphaMode = material.HasProperty("_Mode") && material.GetFloat("_Mode") >= 3.0f ? "BLEND" : "OPAQUE",
                AlphaCutoff = material.HasProperty("_Cutoff") ? material.GetFloat("_Cutoff") : 0.5f,
                DoubleSided = material.HasProperty("_Cull") && Mathf.Approximately(material.GetFloat("_Cull"), 0.0f),
                ShaderParamsJson = BuildShaderParamsJson(material)
            };
            payload.Materials.Add(item);
            payload.Manifest.materialRefs.Add(item.Name);
            var index = payload.Materials.Count - 1;
            materialIndexById[id] = index;
            return index;
        }

        private static string ResolveShaderVariant(string shaderName)
        {
            if (shaderName.IndexOf("lilToon", StringComparison.OrdinalIgnoreCase) >= 0)
            {
                return "lilToon";
            }
            if (shaderName.IndexOf("Poiyomi", StringComparison.OrdinalIgnoreCase) >= 0)
            {
                return "Poiyomi";
            }
            if (shaderName.IndexOf("potatoon", StringComparison.OrdinalIgnoreCase) >= 0)
            {
                return "potatoon";
            }
            if (shaderName.IndexOf("realtoon", StringComparison.OrdinalIgnoreCase) >= 0)
            {
                return "realtoon";
            }
            return "other";
        }

        [Serializable]
        private sealed class ShaderParamPack
        {
            public int schema = 1;
            public string shader = string.Empty;
            public List<string> keywords = new List<string>();
            public List<string> properties = new List<string>();
        }

        private static string BuildShaderParamsJson(Material material)
        {
            var pack = new ShaderParamPack
            {
                shader = material.shader != null ? material.shader.name : "UnknownShader",
                keywords = new List<string>(material.shaderKeywords ?? Array.Empty<string>())
            };

            var floatProps = new[] { "_Cutoff", "_Mode", "_ZWrite", "_BumpScale", "_Metallic", "_Glossiness" };
            foreach (var p in floatProps)
            {
                if (material.HasProperty(p))
                {
                    pack.properties.Add($"{p}={material.GetFloat(p):0.######}");
                }
            }
            var colorProps = new[] { "_Color", "_BaseColor", "_ShadeColor", "_EmissionColor" };
            foreach (var p in colorProps)
            {
                if (material.HasProperty(p))
                {
                    var c = material.GetColor(p);
                    pack.properties.Add($"{p}=({c.r:0.###},{c.g:0.###},{c.b:0.###},{c.a:0.###})");
                }
            }
            return JsonUtility.ToJson(pack);
        }

        private static byte[] BuildVertexBlob(Mesh mesh)
        {
            var vertices = mesh.vertices;
            var normals = mesh.normals;
            var uvs = mesh.uv;
            var tangents = mesh.tangents;
            using var ms = new MemoryStream(vertices.Length * TargetVertexStride);
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            for (var i = 0; i < vertices.Length; i++)
            {
                var v = vertices[i];
                var n = (normals != null && i < normals.Length) ? normals[i] : Vector3.up;
                var uv = (uvs != null && i < uvs.Length) ? uvs[i] : Vector2.zero;
                var t = (tangents != null && i < tangents.Length) ? tangents[i] : new Vector4(1, 0, 0, 1);
                bw.Write(v.x); bw.Write(v.y); bw.Write(v.z);
                bw.Write(n.x); bw.Write(n.y); bw.Write(n.z);
                bw.Write(uv.x); bw.Write(uv.y);
                bw.Write(t.x); bw.Write(t.y); bw.Write(t.z); bw.Write(t.w);
            }
            return ms.ToArray();
        }

        private static byte[] ToVector3Bytes(Vector3[] values)
        {
            using var ms = new MemoryStream(values.Length * 12);
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            foreach (var v in values)
            {
                bw.Write(v.x);
                bw.Write(v.y);
                bw.Write(v.z);
            }
            return ms.ToArray();
        }

        private static byte[] BuildSkinWeightBlob(BoneWeight[] boneWeights)
        {
            using var ms = new MemoryStream(boneWeights.Length * 32);
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            foreach (var bw4 in boneWeights)
            {
                bw.Write(bw4.boneIndex0);
                bw.Write(bw4.boneIndex1);
                bw.Write(bw4.boneIndex2);
                bw.Write(bw4.boneIndex3);
                bw.Write(bw4.weight0);
                bw.Write(bw4.weight1);
                bw.Write(bw4.weight2);
                bw.Write(bw4.weight3);
            }
            return ms.ToArray();
        }

        private static byte[] EncodeTextureSafe(Texture2D texture)
        {
            if (texture == null)
            {
                return Array.Empty<byte>();
            }
            try
            {
                return texture.EncodeToPNG();
            }
            catch
            {
                return Array.Empty<byte>();
            }
        }
    }
}
