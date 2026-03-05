using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using UnityEditor;
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
        private const uint FeatureCutout = 1U << 0;
        private const uint FeatureTransparent = 1U << 1;
        private const uint FeatureNormalMap = 1U << 2;
        private const uint FeatureEmission = 1U << 3;
        private const uint FeatureRim = 1U << 4;
        private const uint FeatureShade = 1U << 5;
        private const uint FeatureMatCap = 1U << 6;

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
                        var skeletonPayload = BuildSkeletonPayload(meshName, mesh, smr);
                        if (skeletonPayload != null)
                        {
                            payload.Skeletons.Add(skeletonPayload);
                        }
                    }
                    var blendShapePayload = BuildBlendShapePayload(meshName, mesh);
                    if (blendShapePayload != null)
                    {
                        payload.BlendShapes.Add(blendShapePayload);
                    }

                    var rigPayload = BuildSkeletonRigPayload(meshName, smr);
                    if (rigPayload != null)
                    {
                        payload.SkeletonRigs.Add(rigPayload);
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

        private static Xav2SkeletonPayload BuildSkeletonPayload(string meshName, Mesh mesh, SkinnedMeshRenderer smr)
        {
            var bindPoses = mesh.bindposes;
            if (bindPoses == null || bindPoses.Length == 0)
            {
                return null;
            }

            var payload = new Xav2SkeletonPayload
            {
                MeshName = meshName,
                BoneMatrices16xN = new float[bindPoses.Length * 16]
            };

            var bones = smr.bones ?? Array.Empty<Transform>();
            var rootWorldToLocal = (smr.rootBone != null)
                ? smr.rootBone.worldToLocalMatrix
                : smr.transform.worldToLocalMatrix;

            for (var i = 0; i < bindPoses.Length; i++)
            {
                Matrix4x4 skinMatrix;
                if (i < bones.Length && bones[i] != null)
                {
                    skinMatrix = bones[i].localToWorldMatrix * rootWorldToLocal;
                }
                else
                {
                    skinMatrix = Matrix4x4.identity;
                }

                var o = i * 16;
                payload.BoneMatrices16xN[o + 0] = skinMatrix.m00; payload.BoneMatrices16xN[o + 1] = skinMatrix.m01; payload.BoneMatrices16xN[o + 2] = skinMatrix.m02; payload.BoneMatrices16xN[o + 3] = skinMatrix.m03;
                payload.BoneMatrices16xN[o + 4] = skinMatrix.m10; payload.BoneMatrices16xN[o + 5] = skinMatrix.m11; payload.BoneMatrices16xN[o + 6] = skinMatrix.m12; payload.BoneMatrices16xN[o + 7] = skinMatrix.m13;
                payload.BoneMatrices16xN[o + 8] = skinMatrix.m20; payload.BoneMatrices16xN[o + 9] = skinMatrix.m21; payload.BoneMatrices16xN[o + 10] = skinMatrix.m22; payload.BoneMatrices16xN[o + 11] = skinMatrix.m23;
                payload.BoneMatrices16xN[o + 12] = skinMatrix.m30; payload.BoneMatrices16xN[o + 13] = skinMatrix.m31; payload.BoneMatrices16xN[o + 14] = skinMatrix.m32; payload.BoneMatrices16xN[o + 15] = skinMatrix.m33;
            }
            return payload;
        }

        private static Xav2SkeletonRigPayload BuildSkeletonRigPayload(string meshName, SkinnedMeshRenderer smr)
        {
            var bones = smr.bones ?? Array.Empty<Transform>();
            if (bones.Length == 0)
            {
                return null;
            }

            var rig = new Xav2SkeletonRigPayload
            {
                MeshName = meshName
            };

            for (var i = 0; i < bones.Length; i++)
            {
                var bone = bones[i];
                var matrix = (bone != null)
                    ? Matrix4x4.TRS(bone.localPosition, bone.localRotation, bone.localScale)
                    : Matrix4x4.identity;
                var parentIndex = -1;
                if (bone != null && bone.parent != null)
                {
                    parentIndex = Array.IndexOf(bones, bone.parent);
                }

                rig.Bones.Add(new Xav2RigBonePayload
                {
                    Name = bone != null ? bone.name : $"Bone_{i}",
                    ParentIndex = parentIndex,
                    LocalMatrix16 = new float[]
                    {
                        matrix.m00, matrix.m01, matrix.m02, matrix.m03,
                        matrix.m10, matrix.m11, matrix.m12, matrix.m13,
                        matrix.m20, matrix.m21, matrix.m22, matrix.m23,
                        matrix.m30, matrix.m31, matrix.m32, matrix.m33
                    }
                });
            }

            return rig;
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
                    ShaderFamily = "legacy",
                    MaterialParamEncoding = "legacy-json",
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

            var shaderName = material.shader != null ? material.shader.name : "UnknownShader";
            var shaderVariant = ResolveShaderVariant(shaderName);
            var shaderFamily = ResolveShaderFamily(shaderVariant);
            var baseTextureName = EnsureTextureRefForProperty(
                material,
                payload,
                textureNameSet,
                "_MainTex",
                "_BaseMap",
                "_BaseColorMap");
            var item = new Xav2MaterialPayload
            {
                Name = BuildUniqueMaterialName(material, id),
                ShaderName = shaderName,
                ShaderVariant = shaderVariant,
                ShaderFamily = shaderFamily,
                MaterialParamEncoding = shaderFamily == "liltoon" ? "typed-v3" : "legacy-json",
                BaseColorTextureName = baseTextureName,
                AlphaMode = ResolveAlphaMode(material),
                AlphaCutoff = ResolveAlphaCutoff(material),
                DoubleSided = material.HasProperty("_Cull") && Mathf.Approximately(material.GetFloat("_Cull"), 0.0f),
                ShaderParamsJson = BuildShaderParamsJson(material)
            };
            if (shaderFamily == "liltoon")
            {
                item.TypedSchemaVersion = 3;
            }
            if (item.AlphaMode == "MASK")
            {
                item.FeatureFlags |= FeatureCutout;
            }
            if (item.AlphaMode == "BLEND")
            {
                item.FeatureFlags |= FeatureTransparent;
            }

            if (shaderFamily == "liltoon")
            {
                AddTypedColor(item, material, "_BaseColor", "_BaseColor", "_Color");
                AddTypedColor(item, material, "_ShadeColor", "_ShadeColor");
                AddTypedColor(item, material, "_EmissionColor", "_EmissionColor");
                AddTypedColor(item, material, "_RimColor", "_RimColor");
                AddTypedColor(item, material, "_MatCapColor", "_MatCapColor", "_MatCapTexColor");

                AddTypedFloat(item, material, "_Cutoff", "_Cutoff");
                AddTypedFloat(item, material, "_BumpScale", "_BumpScale");
                AddTypedFloat(item, material, "_RimFresnelPower", "_RimFresnelPower");
                AddTypedFloat(item, material, "_RimLightingMix", "_RimLightingMix");
                AddTypedFloat(item, material, "_EmissionStrength", "_EmissionMapStrength", "_EmissionStrength");
                AddTypedFloat(item, material, "_MatCapBlend", "_MatCapBlend", "_MatCapBlendUV1", "_MatCapStrength");

                AddTypedTexture(item, "base", baseTextureName);
                var shadeTextureName = EnsureTextureRefForProperty(material, payload, textureNameSet, "_ShadeTexture", "_ShadowColorTex");
                AddTypedTexture(item, "shade", shadeTextureName);
                var normalTextureName = EnsureTextureRefForProperty(material, payload, textureNameSet, "_BumpMap", "_NormalMap");
                AddTypedTexture(item, "normal", normalTextureName);
                var emissionTextureName = EnsureTextureRefForProperty(material, payload, textureNameSet, "_EmissionMap");
                AddTypedTexture(item, "emission", emissionTextureName);
                var maskTextureName = EnsureTextureRefForProperty(material, payload, textureNameSet, "_Main2ndTex", "_Main2ndBlendMask");
                AddTypedTexture(item, "mask", maskTextureName);
                var rimTextureName = EnsureTextureRefForProperty(material, payload, textureNameSet, "_RimColorTex", "_RimTex");
                AddTypedTexture(item, "rim", rimTextureName);
                var matcapTextureName = EnsureTextureRefForProperty(material, payload, textureNameSet, "_MatCapTex", "_MatCapTexture", "_MatCapBlendMask");
                AddTypedTexture(item, "matcap", matcapTextureName);

                if (!string.IsNullOrEmpty(shadeTextureName) || HasTypedColor(item, "_ShadeColor"))
                {
                    item.FeatureFlags |= FeatureShade;
                }
                if (!string.IsNullOrEmpty(normalTextureName))
                {
                    item.FeatureFlags |= FeatureNormalMap;
                }
                if (!string.IsNullOrEmpty(emissionTextureName) || HasTypedColor(item, "_EmissionColor"))
                {
                    item.FeatureFlags |= FeatureEmission;
                }
                if (!string.IsNullOrEmpty(rimTextureName) || HasTypedColor(item, "_RimColor"))
                {
                    item.FeatureFlags |= FeatureRim;
                }
                if (!string.IsNullOrEmpty(matcapTextureName) || HasTypedColor(item, "_MatCapColor"))
                {
                    item.FeatureFlags |= FeatureMatCap;
                }
            }
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

        private static string ResolveShaderFamily(string shaderVariant)
        {
            return string.Equals(shaderVariant, "lilToon", StringComparison.OrdinalIgnoreCase)
                ? "liltoon"
                : "legacy";
        }

        private static bool HasTypedColor(Xav2MaterialPayload payload, string id)
        {
            return payload.TypedColorParams.Exists(p => string.Equals(p.Id, id, StringComparison.Ordinal));
        }

        private static void AddTypedFloat(Xav2MaterialPayload payload, Material material, string id, params string[] sourceProps)
        {
            foreach (var prop in sourceProps)
            {
                if (!material.HasProperty(prop))
                {
                    continue;
                }
                payload.TypedFloatParams.Add(new Xav2TypedFloatParam
                {
                    Id = id,
                    Value = material.GetFloat(prop)
                });
                return;
            }
        }

        private static void AddTypedColor(Xav2MaterialPayload payload, Material material, string id, params string[] sourceProps)
        {
            foreach (var prop in sourceProps)
            {
                if (!material.HasProperty(prop))
                {
                    continue;
                }
                var c = material.GetColor(prop);
                payload.TypedColorParams.Add(new Xav2TypedColorParam
                {
                    Id = id,
                    R = c.r,
                    G = c.g,
                    B = c.b,
                    A = c.a
                });
                return;
            }
        }

        private static void AddTypedTexture(Xav2MaterialPayload payload, string slot, string textureRef)
        {
            if (string.IsNullOrWhiteSpace(textureRef))
            {
                return;
            }
            payload.TypedTextureParams.Add(new Xav2TypedTextureParam
            {
                Slot = slot,
                TextureRef = textureRef
            });
        }

        private static string EnsureTextureRefForProperty(
            Material material,
            Xav2AvatarPayload payload,
            HashSet<string> textureNameSet,
            params string[] propertyCandidates)
        {
            if (material == null || payload == null || textureNameSet == null || propertyCandidates == null)
            {
                return string.Empty;
            }

            foreach (var prop in propertyCandidates)
            {
                if (string.IsNullOrWhiteSpace(prop) || !material.HasProperty(prop))
                {
                    continue;
                }
                if (!(material.GetTexture(prop) is Texture2D tex2d) || tex2d == null)
                {
                    continue;
                }

                var encoded = EncodeTextureSafe(tex2d);
                if (encoded.Length == 0)
                {
                    Debug.LogWarning($"[XAV2] Texture encode failed for material '{material.name}', texture '{tex2d.name}'.");
                    continue;
                }

                var textureName = BuildUniqueTextureName(tex2d);
                if (textureNameSet.Add(textureName))
                {
                    payload.Textures.Add(new Xav2TexturePayload
                    {
                        Name = textureName,
                        Bytes = encoded
                    });
                    payload.Manifest.textureRefs.Add(textureName);
                }
                return textureName;
            }

            return string.Empty;
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

            var floatProps = new[]
            {
                "_Cutoff", "_Mode", "_ZWrite", "_BumpScale",
                "_Metallic", "_Glossiness", "_RimFresnelPower",
                "_RimLightingMix", "_EmissionMapStrength", "_EmissionStrength",
                "_MatCapBlend", "_MatCapBlendUV1", "_MatCapStrength"
            };
            foreach (var p in floatProps)
            {
                if (material.HasProperty(p))
                {
                    pack.properties.Add($"{p}={material.GetFloat(p):0.######}");
                }
            }
            var colorProps = new[] { "_Color", "_BaseColor", "_ShadeColor", "_EmissionColor", "_RimColor", "_MatCapColor", "_MatCapTexColor" };
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
                return EncodeTextureViaRenderTexture(texture);
            }
        }

        private static string BuildUniqueTextureName(Texture2D texture)
        {
            if (texture == null)
            {
                return "Texture#null";
            }

            var baseName = string.IsNullOrWhiteSpace(texture.name) ? "Texture" : texture.name;
            var assetPath = AssetDatabase.GetAssetPath(texture);
            if (!string.IsNullOrWhiteSpace(assetPath))
            {
                return $"{baseName}@{assetPath.Replace('\\', '/')}";
            }
            return $"{baseName}#{texture.GetInstanceID()}";
        }

        private static string BuildUniqueMaterialName(Material material, int materialId)
        {
            if (material == null)
            {
                return "Material#null";
            }

            var baseName = string.IsNullOrWhiteSpace(material.name) ? "Material" : material.name;
            var assetPath = AssetDatabase.GetAssetPath(material);
            if (!string.IsNullOrWhiteSpace(assetPath))
            {
                return $"{baseName}@{assetPath.Replace('\\', '/')}";
            }
            return $"{baseName}#{materialId}";
        }

        private static string ResolveAlphaMode(Material material)
        {
            if (material == null)
            {
                return "OPAQUE";
            }

            var renderType = material.GetTag("RenderType", false, string.Empty);
            if (!string.IsNullOrEmpty(renderType))
            {
                if (renderType.IndexOf("TransparentCutout", StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    return "MASK";
                }
                if (renderType.IndexOf("Transparent", StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    return "BLEND";
                }
            }

            if (material.renderQueue >= 3000)
            {
                return "BLEND";
            }
            if (material.renderQueue >= 2450)
            {
                return "MASK";
            }

            if ((material.HasProperty("_AlphaClip") && material.GetFloat("_AlphaClip") > 0.5f) ||
                (material.HasProperty("_UseAlphaClipping") && material.GetFloat("_UseAlphaClipping") > 0.5f) ||
                (material.HasProperty("_Cutoff") && material.GetFloat("_Cutoff") > 0.001f))
            {
                return "MASK";
            }

            if ((material.HasProperty("_Mode") && material.GetFloat("_Mode") >= 3.0f) ||
                (material.HasProperty("_Surface") && material.GetFloat("_Surface") >= 1.0f) ||
                (material.HasProperty("_BlendMode") && material.GetFloat("_BlendMode") > 0.0f))
            {
                return "BLEND";
            }

            return "OPAQUE";
        }

        private static float ResolveAlphaCutoff(Material material)
        {
            if (material == null)
            {
                return 0.5f;
            }
            if (material.HasProperty("_Cutoff"))
            {
                return Mathf.Clamp01(material.GetFloat("_Cutoff"));
            }
            if (material.HasProperty("_AlphaCutoff"))
            {
                return Mathf.Clamp01(material.GetFloat("_AlphaCutoff"));
            }
            return 0.5f;
        }

        private static byte[] EncodeTextureViaRenderTexture(Texture2D source)
        {
            if (source == null || source.width <= 0 || source.height <= 0)
            {
                return Array.Empty<byte>();
            }

            var prev = RenderTexture.active;
            var rt = RenderTexture.GetTemporary(
                source.width,
                source.height,
                0,
                RenderTextureFormat.ARGB32,
                RenderTextureReadWrite.sRGB);
            Texture2D readable = null;
            try
            {
                Graphics.Blit(source, rt);
                RenderTexture.active = rt;
                readable = new Texture2D(source.width, source.height, TextureFormat.RGBA32, false);
                readable.ReadPixels(new Rect(0, 0, source.width, source.height), 0, 0);
                readable.Apply(false, false);
                return readable.EncodeToPNG();
            }
            catch
            {
                return Array.Empty<byte>();
            }
            finally
            {
                RenderTexture.active = prev;
                if (readable != null)
                {
                    UnityEngine.Object.DestroyImmediate(readable);
                }
                RenderTexture.ReleaseTemporary(rt);
            }
        }
    }
}
