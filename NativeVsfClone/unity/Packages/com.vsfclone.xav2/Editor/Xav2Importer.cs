using System;
using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEngine;
using VsfClone.Xav2.Runtime;

namespace VsfClone.Xav2.Editor
{
    public static class Xav2Importer
    {
        public static Xav2ImportReport Import(string xav2Path, Xav2ImportOptions options)
        {
            var report = new Xav2ImportReport
            {
                SourcePath = xav2Path ?? string.Empty
            };

            if (string.IsNullOrWhiteSpace(xav2Path) || !File.Exists(xav2Path))
            {
                report.ErrorMessage = $"XAV2 file not found: '{xav2Path}'.";
                return report;
            }

            options ??= new Xav2ImportOptions();
            if (options.CollisionPolicy != Xav2ImportCollisionPolicy.Suffix)
            {
                report.ErrorMessage = $"Unsupported collision policy: {options.CollisionPolicy}";
                return report;
            }

            var loadOptions = new Xav2LoadOptions
            {
                StrictValidation = options.StrictValidation,
                UnknownSectionPolicy = Xav2UnknownSectionPolicy.Warn
            };

            if (!Xav2RuntimeLoader.TryLoad(xav2Path, out var payload, out var diagnostics, loadOptions))
            {
                report.ErrorMessage =
                    $"Load failed ({diagnostics.ErrorCode}) at '{diagnostics.ParserStage}': {diagnostics.ErrorMessage}";
                report.WarningCodes.AddRange(diagnostics.WarningCodes);
                report.Warnings.AddRange(diagnostics.Warnings);
                return report;
            }

            report.IsPartial = diagnostics.IsPartial;
            report.WarningCodes.AddRange(diagnostics.WarningCodes);
            report.Warnings.AddRange(diagnostics.Warnings);

            var avatarId = SanitizeName(string.IsNullOrWhiteSpace(payload.Manifest.avatarId)
                ? Path.GetFileNameWithoutExtension(xav2Path)
                : payload.Manifest.avatarId);
            var outputRoot = NormalizeAssetPath(options.OutputRoot);
            var outputDir = NormalizeAssetPath($"{outputRoot}/{avatarId}");
            report.OutputDirectory = outputDir;

            EnsureFolder(outputRoot);
            EnsureFolder(outputDir);
            var texturesDir = EnsureChildFolder(outputDir, "Textures");
            var materialsDir = EnsureChildFolder(outputDir, "Materials");
            var meshesDir = EnsureChildFolder(outputDir, "Meshes");
            var prefabsDir = EnsureChildFolder(outputDir, "Prefabs");

            Dictionary<string, Texture2D> textureByRef;
            try
            {
                textureByRef = CreateTextureAssets(payload, texturesDir, report);
            }
            catch (Exception ex)
            {
                report.ErrorMessage = $"Texture import failed: {ex.Message}";
                return report;
            }

            Dictionary<string, Material> materialByName;
            try
            {
                materialByName = CreateMaterialAssets(payload, materialsDir, textureByRef, report, options);
            }
            catch (Exception ex)
            {
                report.ErrorMessage = $"Material import failed: {ex.Message}";
                return report;
            }

            Dictionary<string, Mesh> meshByName;
            try
            {
                meshByName = CreateMeshAssets(payload, meshesDir, report);
            }
            catch (Exception ex)
            {
                report.ErrorMessage = $"Mesh import failed: {ex.Message}";
                return report;
            }

            var instanceRoot = BuildPrefabHierarchy(payload, meshByName, materialByName, report, options);
            if (instanceRoot == null)
            {
                report.ErrorMessage = "Prefab hierarchy build failed.";
                return report;
            }

            var prefabName = SanitizeName(string.IsNullOrWhiteSpace(payload.Manifest.displayName)
                ? avatarId
                : payload.Manifest.displayName);
            var prefabPath = AssetDatabase.GenerateUniqueAssetPath($"{prefabsDir}/{prefabName}.prefab");
            var prefab = PrefabUtility.SaveAsPrefabAsset(instanceRoot, prefabPath);
            UnityEngine.Object.DestroyImmediate(instanceRoot);

            if (prefab == null)
            {
                report.ErrorMessage = "Failed to save prefab asset.";
                return report;
            }

            report.PrefabPath = prefabPath;
            report.CreatedAssets.Add(prefabPath);
            if (report.RecoverableErrors.Count > 0 || report.SkippedAssets.Count > 0)
            {
                report.IsPartial = true;
            }
            report.Success = true;
            AssetDatabase.SaveAssets();
            AssetDatabase.Refresh();
            return report;
        }

        private static Dictionary<string, Texture2D> CreateTextureAssets(
            Xav2AvatarPayload payload,
            string texturesDir,
            Xav2ImportReport report)
        {
            var result = new Dictionary<string, Texture2D>(StringComparer.OrdinalIgnoreCase);
            if (payload == null)
            {
                return result;
            }

            for (var i = 0; i < payload.Textures.Count; i++)
            {
                var src = payload.Textures[i];
                var baseName = SanitizeName(string.IsNullOrWhiteSpace(src.Name) ? $"Texture_{i}" : src.Name);
                if (src.Bytes == null || src.Bytes.Length == 0)
                {
                    report.Warnings.Add($"XAV2_IMPORT_TEXTURE_EMPTY: texture={src.Name}");
                    continue;
                }

                var texturePath = AssetDatabase.GenerateUniqueAssetPath($"{texturesDir}/{baseName}.png");
                var absolutePath = ToAbsolutePath(texturePath);
                var absDir = Path.GetDirectoryName(absolutePath);
                if (!string.IsNullOrWhiteSpace(absDir))
                {
                    Directory.CreateDirectory(absDir);
                }

                try
                {
                    File.WriteAllBytes(absolutePath, src.Bytes);
                }
                catch (Exception ex)
                {
                    report.Warnings.Add($"XAV2_IMPORT_TEXTURE_WRITE_FAILED: texture={src.Name}, error={ex.Message}");
                    continue;
                }

                AssetDatabase.ImportAsset(texturePath, ImportAssetOptions.ForceSynchronousImport);
                var texture = AssetDatabase.LoadAssetAtPath<Texture2D>(texturePath);
                if (texture == null)
                {
                    report.Warnings.Add($"XAV2_IMPORT_TEXTURE_LOAD_FAILED: texture={src.Name}");
                    continue;
                }

                result[NormalizeRef(src.Name)] = texture;
                report.CreatedAssets.Add(texturePath);
            }

            return result;
        }

        private static Dictionary<string, Material> CreateMaterialAssets(
            Xav2AvatarPayload payload,
            string materialsDir,
            IReadOnlyDictionary<string, Texture2D> textureByRef,
            Xav2ImportReport report,
            Xav2ImportOptions options)
        {
            var result = new Dictionary<string, Material>(StringComparer.OrdinalIgnoreCase);
            if (payload == null)
            {
                return result;
            }

            for (var i = 0; i < payload.Materials.Count; i++)
            {
                var src = payload.Materials[i];
                try
                {
                    var shader = ResolveShader(src, report);
                    var material = new Material(shader)
                    {
                        name = SanitizeName(string.IsNullOrWhiteSpace(src.Name) ? $"Material_{i}" : src.Name)
                    };

                    ApplyMaterialValues(material, src, textureByRef, report, options);

                    var materialPath = AssetDatabase.GenerateUniqueAssetPath($"{materialsDir}/{material.name}.mat");
                    AssetDatabase.CreateAsset(material, materialPath);
                    report.CreatedAssets.Add(materialPath);
                    result[NormalizeRef(src.Name)] = material;
                }
                catch (Exception ex)
                {
                    report.RecoverableErrors.Add($"XAV2_IMPORT_MATERIAL_FAILED: material={src.Name}, error={ex.Message}");
                    report.SkippedAssets.Add($"material:{src.Name}");
                }
            }

            return result;
        }

        private static Dictionary<string, Mesh> CreateMeshAssets(Xav2AvatarPayload payload, string meshesDir, Xav2ImportReport report)
        {
            var result = new Dictionary<string, Mesh>(StringComparer.OrdinalIgnoreCase);
            if (payload == null)
            {
                return result;
            }

            var skinByMesh = new Dictionary<string, Xav2SkinPayload>(StringComparer.OrdinalIgnoreCase);
            foreach (var skin in payload.Skins)
            {
                skinByMesh[NormalizeRef(skin.MeshName)] = skin;
            }

            var blendByMesh = new Dictionary<string, Xav2BlendShapePayload>(StringComparer.OrdinalIgnoreCase);
            foreach (var blend in payload.BlendShapes)
            {
                blendByMesh[NormalizeRef(blend.MeshName)] = blend;
            }

            for (var i = 0; i < payload.Meshes.Count; i++)
            {
                var src = payload.Meshes[i];
                var meshName = string.IsNullOrWhiteSpace(src.Name) ? $"Mesh_{i}" : src.Name;
                try
                {
                    var mesh = BuildMesh(src, meshName, report);
                    if (mesh == null)
                    {
                        report.SkippedAssets.Add($"mesh:{meshName}");
                        continue;
                    }

                    if (skinByMesh.TryGetValue(NormalizeRef(src.Name), out var skin))
                    {
                        ApplySkin(mesh, skin, report);
                    }

                    if (blendByMesh.TryGetValue(NormalizeRef(src.Name), out var blend))
                    {
                        ApplyBlendShapes(mesh, blend, report);
                    }

                    var meshPath = AssetDatabase.GenerateUniqueAssetPath($"{meshesDir}/{SanitizeName(meshName)}.asset");
                    AssetDatabase.CreateAsset(mesh, meshPath);
                    report.CreatedAssets.Add(meshPath);
                    result[NormalizeRef(src.Name)] = mesh;
                }
                catch (Exception ex)
                {
                    report.RecoverableErrors.Add($"XAV2_IMPORT_MESH_FAILED: mesh={meshName}, error={ex.Message}");
                    report.SkippedAssets.Add($"mesh:{meshName}");
                }
            }

            return result;
        }

        private static GameObject BuildPrefabHierarchy(
            Xav2AvatarPayload payload,
            IReadOnlyDictionary<string, Mesh> meshByName,
            IReadOnlyDictionary<string, Material> materialByName,
            Xav2ImportReport report,
            Xav2ImportOptions options)
        {
            if (payload == null)
            {
                return null;
            }

            var rootName = SanitizeName(string.IsNullOrWhiteSpace(payload.Manifest.displayName)
                ? payload.Manifest.avatarId
                : payload.Manifest.displayName);
            if (string.IsNullOrWhiteSpace(rootName))
            {
                rootName = "ImportedAvatar";
            }

            var root = new GameObject(rootName);

            var skinByMesh = new Dictionary<string, Xav2SkinPayload>(StringComparer.OrdinalIgnoreCase);
            foreach (var skin in payload.Skins)
            {
                skinByMesh[NormalizeRef(skin.MeshName)] = skin;
            }

            var rigByMesh = new Dictionary<string, Xav2SkeletonRigPayload>(StringComparer.OrdinalIgnoreCase);
            foreach (var rig in payload.SkeletonRigs)
            {
                rigByMesh[NormalizeRef(rig.MeshName)] = rig;
            }

            var skinnedMeshCount = 0;
            var fullRigCount = 0;

            for (var i = 0; i < payload.Meshes.Count; i++)
            {
                var meshPayload = payload.Meshes[i];
                if (!meshByName.TryGetValue(NormalizeRef(meshPayload.Name), out var mesh) || mesh == null)
                {
                    report.Warnings.Add($"XAV2_IMPORT_MESH_INSTANCE_MISSING: mesh={meshPayload.Name}");
                    continue;
                }

                var child = new GameObject(SanitizeName(meshPayload.Name));
                child.transform.SetParent(root.transform, false);

                var material = ResolveMaterialForMesh(payload, materialByName, meshPayload.MaterialIndex);
                if (skinByMesh.TryGetValue(NormalizeRef(meshPayload.Name), out var skin) &&
                    skin.BoneIndices != null &&
                    skin.BoneIndices.Length > 0)
                {
                    skinnedMeshCount++;
                    var smr = child.AddComponent<SkinnedMeshRenderer>();
                    smr.sharedMesh = mesh;
                    smr.sharedMaterial = material;
                    if (rigByMesh.TryGetValue(NormalizeRef(meshPayload.Name), out var rig) &&
                        rig.Bones != null &&
                        rig.Bones.Count > 0)
                    {
                        var bones = CreateSkeletonBonesFromRig(root.transform, rig, report);
                        smr.bones = MapSkinBones(bones, skin.BoneIndices);
                        smr.rootBone = ResolveRootBone(bones, rig, root.transform);
                        fullRigCount++;
                    }
                    else
                    {
                        report.Warnings.Add($"XAV4_RIG_MISSING: mesh='{meshPayload.Name}'");
                        report.SkippedAssets.Add($"rig:{meshPayload.Name}");
                        if (options.FailOnRigDataMissing)
                        {
                            UnityEngine.Object.DestroyImmediate(root);
                            report.ErrorMessage = $"Rig payload missing for skinned mesh '{meshPayload.Name}'.";
                            return null;
                        }

                        var bones = CreateSkeletonBones(root.transform, skin.BoneIndices.Length);
                        smr.bones = bones;
                        smr.rootBone = bones.Length > 0 ? bones[0] : root.transform;
                    }
                }
                else
                {
                    var mf = child.AddComponent<MeshFilter>();
                    mf.sharedMesh = mesh;
                    var mr = child.AddComponent<MeshRenderer>();
                    mr.sharedMaterial = material;
                }
            }

            if (skinnedMeshCount == 0)
            {
                report.RigQuality = "None";
            }
            else
            {
                report.RigQuality = (fullRigCount == skinnedMeshCount) ? "Full" : "Partial";
            }

            return root;
        }

        private static Transform[] CreateSkeletonBones(Transform parent, int boneCount)
        {
            if (boneCount <= 0)
            {
                return Array.Empty<Transform>();
            }

            var bones = new Transform[boneCount];
            for (var i = 0; i < boneCount; i++)
            {
                var bone = new GameObject($"Bone_{i}").transform;
                bone.SetParent(parent, false);
                bones[i] = bone;
            }

            return bones;
        }

        private static Transform[] CreateSkeletonBonesFromRig(Transform parent, Xav2SkeletonRigPayload rig, Xav2ImportReport report)
        {
            if (rig == null || rig.Bones == null || rig.Bones.Count == 0)
            {
                return Array.Empty<Transform>();
            }

            var bones = new Transform[rig.Bones.Count];
            for (var i = 0; i < rig.Bones.Count; i++)
            {
                bones[i] = new GameObject(SanitizeName(rig.Bones[i].Name)).transform;
            }

            for (var i = 0; i < rig.Bones.Count; i++)
            {
                var parentIndex = rig.Bones[i].ParentIndex;
                var parentTransform = (parentIndex >= 0 && parentIndex < bones.Length)
                    ? bones[parentIndex]
                    : parent;
                bones[i].SetParent(parentTransform, false);

                if (TryParseMatrix(rig.Bones[i].LocalMatrix16, out var matrix))
                {
                    ApplyLocalMatrix(bones[i], matrix);
                }
                else
                {
                    report.Warnings.Add($"XAV4_RIG_MATRIX_INVALID: mesh={rig.MeshName}, bone={rig.Bones[i].Name}");
                }
            }

            return bones;
        }

        private static Transform[] MapSkinBones(Transform[] rigBones, int[] skinBoneIndices)
        {
            if (skinBoneIndices == null || skinBoneIndices.Length == 0)
            {
                return rigBones ?? Array.Empty<Transform>();
            }

            var mapped = new Transform[skinBoneIndices.Length];
            for (var i = 0; i < skinBoneIndices.Length; i++)
            {
                var index = skinBoneIndices[i];
                mapped[i] = (index >= 0 && rigBones != null && index < rigBones.Length)
                    ? rigBones[index]
                    : null;
            }
            return mapped;
        }

        private static Transform ResolveRootBone(Transform[] bones, Xav2SkeletonRigPayload rig, Transform fallback)
        {
            if (bones == null || bones.Length == 0 || rig == null || rig.Bones == null)
            {
                return fallback;
            }

            for (var i = 0; i < rig.Bones.Count && i < bones.Length; i++)
            {
                if (rig.Bones[i].ParentIndex < 0 && bones[i] != null)
                {
                    return bones[i];
                }
            }

            return bones[0] ?? fallback;
        }

        private static bool TryParseMatrix(float[] raw, out Matrix4x4 matrix)
        {
            matrix = Matrix4x4.identity;
            if (raw == null || raw.Length != 16)
            {
                return false;
            }

            matrix = new Matrix4x4(
                new Vector4(raw[0], raw[1], raw[2], raw[3]),
                new Vector4(raw[4], raw[5], raw[6], raw[7]),
                new Vector4(raw[8], raw[9], raw[10], raw[11]),
                new Vector4(raw[12], raw[13], raw[14], raw[15]));
            return true;
        }

        private static void ApplyLocalMatrix(Transform target, Matrix4x4 matrix)
        {
            var position = new Vector3(matrix.m03, matrix.m13, matrix.m23);
            var x = new Vector3(matrix.m00, matrix.m10, matrix.m20);
            var y = new Vector3(matrix.m01, matrix.m11, matrix.m21);
            var z = new Vector3(matrix.m02, matrix.m12, matrix.m22);

            var scale = new Vector3(x.magnitude, y.magnitude, z.magnitude);
            if (scale.x <= 1e-6f) scale.x = 1.0f;
            if (scale.y <= 1e-6f) scale.y = 1.0f;
            if (scale.z <= 1e-6f) scale.z = 1.0f;

            var forward = z / scale.z;
            var upwards = y / scale.y;
            var rotation = (forward.sqrMagnitude > 1e-8f && upwards.sqrMagnitude > 1e-8f)
                ? Quaternion.LookRotation(forward, upwards)
                : Quaternion.identity;

            target.localPosition = position;
            target.localRotation = rotation;
            target.localScale = scale;
        }

        private static Material ResolveMaterialForMesh(
            Xav2AvatarPayload payload,
            IReadOnlyDictionary<string, Material> materialByName,
            int materialIndex)
        {
            if (payload == null || payload.Materials == null || payload.Materials.Count == 0)
            {
                return null;
            }

            if (materialIndex >= 0 && materialIndex < payload.Materials.Count)
            {
                var key = NormalizeRef(payload.Materials[materialIndex].Name);
                if (materialByName.TryGetValue(key, out var resolved))
                {
                    return resolved;
                }
            }

            foreach (var material in payload.Materials)
            {
                if (materialByName.TryGetValue(NormalizeRef(material.Name), out var fallback))
                {
                    return fallback;
                }
            }

            return null;
        }

        private static Mesh BuildMesh(Xav2MeshPayload src, string meshName, Xav2ImportReport report)
        {
            if (src == null)
            {
                return null;
            }

            var stride = (int)src.VertexStride;
            if (stride < 48)
            {
                report.Warnings.Add($"XAV2_IMPORT_VERTEX_STRIDE_UNSUPPORTED: mesh={meshName}, stride={stride}");
                return null;
            }
            if (src.VertexBlob == null || src.VertexBlob.Length < stride || (src.VertexBlob.Length % stride) != 0)
            {
                report.Warnings.Add($"XAV2_IMPORT_VERTEX_BLOB_INVALID: mesh={meshName}");
                return null;
            }

            var vertexCount = src.VertexBlob.Length / stride;
            var vertices = new Vector3[vertexCount];
            var normals = new Vector3[vertexCount];
            var uvs = new Vector2[vertexCount];
            var tangents = new Vector4[vertexCount];

            for (var i = 0; i < vertexCount; i++)
            {
                var o = i * stride;
                vertices[i] = new Vector3(
                    ReadSingle(src.VertexBlob, o + 0),
                    ReadSingle(src.VertexBlob, o + 4),
                    ReadSingle(src.VertexBlob, o + 8));
                normals[i] = new Vector3(
                    ReadSingle(src.VertexBlob, o + 12),
                    ReadSingle(src.VertexBlob, o + 16),
                    ReadSingle(src.VertexBlob, o + 20));
                uvs[i] = new Vector2(
                    ReadSingle(src.VertexBlob, o + 24),
                    ReadSingle(src.VertexBlob, o + 28));
                tangents[i] = new Vector4(
                    ReadSingle(src.VertexBlob, o + 32),
                    ReadSingle(src.VertexBlob, o + 36),
                    ReadSingle(src.VertexBlob, o + 40),
                    ReadSingle(src.VertexBlob, o + 44));
            }

            var mesh = new Mesh
            {
                name = SanitizeName(meshName),
                vertices = vertices,
                normals = normals,
                uv = uvs,
                tangents = tangents,
                indexFormat = vertexCount > 65535 ? UnityEngine.Rendering.IndexFormat.UInt32 : UnityEngine.Rendering.IndexFormat.UInt16
            };

            if (src.Indices == null || src.Indices.Length == 0)
            {
                mesh.triangles = BuildSequentialIndices(vertexCount);
                report.Warnings.Add($"XAV2_IMPORT_INDICES_MISSING: mesh={meshName}");
            }
            else
            {
                var triangles = new int[src.Indices.Length];
                for (var i = 0; i < src.Indices.Length; i++)
                {
                    triangles[i] = Mathf.Clamp((int)src.Indices[i], 0, Math.Max(vertexCount - 1, 0));
                }

                if ((triangles.Length % 3) != 0)
                {
                    report.Warnings.Add($"XAV2_IMPORT_INDICES_NON_TRIANGLE: mesh={meshName}");
                    var triangleCount = (triangles.Length / 3) * 3;
                    Array.Resize(ref triangles, triangleCount);
                }
                mesh.triangles = triangles;
            }

            mesh.RecalculateBounds();
            return mesh;
        }

        private static int[] BuildSequentialIndices(int vertexCount)
        {
            var count = (vertexCount / 3) * 3;
            var indices = new int[count];
            for (var i = 0; i < count; i++)
            {
                indices[i] = i;
            }

            return indices;
        }

        private static void ApplySkin(Mesh mesh, Xav2SkinPayload skin, Xav2ImportReport report)
        {
            if (mesh == null || skin == null)
            {
                return;
            }

            var bindPoses = ParseMatrices(skin.BindPoses16xN);
            if (bindPoses.Length > 0)
            {
                mesh.bindposes = bindPoses;
            }

            var boneWeights = ParseBoneWeights(skin.SkinWeightBlob, mesh.vertexCount, report, skin.MeshName);
            if (boneWeights.Length == mesh.vertexCount)
            {
                mesh.boneWeights = boneWeights;
            }
        }

        private static void ApplyBlendShapes(Mesh mesh, Xav2BlendShapePayload blend, Xav2ImportReport report)
        {
            if (mesh == null || blend == null)
            {
                return;
            }

            foreach (var frame in blend.Frames)
            {
                if (!TryParseVector3Array(frame.DeltaVertices, mesh.vertexCount, out var dv) ||
                    !TryParseVector3Array(frame.DeltaNormals, mesh.vertexCount, out var dn) ||
                    !TryParseVector3Array(frame.DeltaTangents, mesh.vertexCount, out var dt))
                {
                    report.Warnings.Add($"XAV2_IMPORT_BLENDSHAPE_INVALID: mesh={blend.MeshName}, frame={frame.Name}");
                    continue;
                }

                var frameName = string.IsNullOrWhiteSpace(frame.Name) ? "BlendShape" : frame.Name;
                mesh.AddBlendShapeFrame(frameName, frame.Weight, dv, dn, dt);
            }
        }

        private static BoneWeight[] ParseBoneWeights(byte[] blob, int vertexCount, Xav2ImportReport report, string meshName)
        {
            if (blob == null || blob.Length == 0)
            {
                return Array.Empty<BoneWeight>();
            }

            const int stride = 32;
            if (blob.Length != vertexCount * stride)
            {
                report.Warnings.Add(
                    $"XAV2_IMPORT_SKIN_WEIGHT_COUNT_MISMATCH: mesh={meshName}, expected={vertexCount * stride}, actual={blob.Length}");
                return Array.Empty<BoneWeight>();
            }

            var weights = new BoneWeight[vertexCount];
            for (var i = 0; i < vertexCount; i++)
            {
                var o = i * stride;
                weights[i].boneIndex0 = BitConverter.ToInt32(blob, o + 0);
                weights[i].boneIndex1 = BitConverter.ToInt32(blob, o + 4);
                weights[i].boneIndex2 = BitConverter.ToInt32(blob, o + 8);
                weights[i].boneIndex3 = BitConverter.ToInt32(blob, o + 12);
                weights[i].weight0 = ReadSingle(blob, o + 16);
                weights[i].weight1 = ReadSingle(blob, o + 20);
                weights[i].weight2 = ReadSingle(blob, o + 24);
                weights[i].weight3 = ReadSingle(blob, o + 28);
            }

            return weights;
        }

        private static Matrix4x4[] ParseMatrices(float[] values)
        {
            if (values == null || values.Length < 16 || (values.Length % 16) != 0)
            {
                return Array.Empty<Matrix4x4>();
            }

            var count = values.Length / 16;
            var matrices = new Matrix4x4[count];
            for (var i = 0; i < count; i++)
            {
                var o = i * 16;
                matrices[i] = new Matrix4x4(
                    new Vector4(values[o + 0], values[o + 1], values[o + 2], values[o + 3]),
                    new Vector4(values[o + 4], values[o + 5], values[o + 6], values[o + 7]),
                    new Vector4(values[o + 8], values[o + 9], values[o + 10], values[o + 11]),
                    new Vector4(values[o + 12], values[o + 13], values[o + 14], values[o + 15]));
            }

            return matrices;
        }

        private static bool TryParseVector3Array(byte[] blob, int vertexCount, out Vector3[] values)
        {
            values = Array.Empty<Vector3>();
            if (blob == null || blob.Length != vertexCount * 12)
            {
                return false;
            }

            values = new Vector3[vertexCount];
            for (var i = 0; i < vertexCount; i++)
            {
                var o = i * 12;
                values[i] = new Vector3(
                    ReadSingle(blob, o + 0),
                    ReadSingle(blob, o + 4),
                    ReadSingle(blob, o + 8));
            }

            return true;
        }

        private static Shader ResolveShader(Xav2MaterialPayload source, Xav2ImportReport report)
        {
            var shader = Shader.Find(source.ShaderName);
            if (shader != null)
            {
                return shader;
            }

            if (string.Equals(source.ShaderFamily, "liltoon", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(source.ShaderVariant, "lilToon", StringComparison.OrdinalIgnoreCase))
            {
                shader = Shader.Find("lilToon");
                if (shader != null)
                {
                    return shader;
                }
            }

            shader = Shader.Find("Standard") ?? Shader.Find("Universal Render Pipeline/Lit");
            shader ??= Shader.Find("Sprites/Default");
            shader ??= Shader.Find("Unlit/Texture");

            report.Warnings.Add($"XAV2_IMPORT_SHADER_FALLBACK: material={source.Name}, shader={source.ShaderName}");
            if (shader == null)
            {
                report.RecoverableErrors.Add($"XAV2_IMPORT_SHADER_UNRESOLVED: material={source.Name}");
            }
            return shader ?? Shader.Find("Hidden/InternalErrorShader");
        }

        private static void ApplyMaterialValues(
            Material material,
            Xav2MaterialPayload source,
            IReadOnlyDictionary<string, Texture2D> textureByRef,
            Xav2ImportReport report,
            Xav2ImportOptions options)
        {
            if (material == null || source == null)
            {
                return;
            }

            if (!string.IsNullOrWhiteSpace(source.BaseColorTextureName) &&
                textureByRef.TryGetValue(NormalizeRef(source.BaseColorTextureName), out var baseTexture))
            {
                if (material.HasProperty("_MainTex"))
                {
                    material.SetTexture("_MainTex", baseTexture);
                }
                if (material.HasProperty("_BaseMap"))
                {
                    material.SetTexture("_BaseMap", baseTexture);
                }
            }

            foreach (var typed in source.TypedTextureParams)
            {
                if (!textureByRef.TryGetValue(NormalizeRef(typed.TextureRef), out var tex))
                {
                    continue;
                }

                ApplyTypedTexture(material, typed.Slot, tex);
            }

            foreach (var typedColor in source.TypedColorParams)
            {
                ApplyTypedColor(material, typedColor);
            }

            foreach (var typedFloat in source.TypedFloatParams)
            {
                ApplyTypedFloat(material, typedFloat);
            }

            if (string.Equals(source.AlphaMode, "BLEND", StringComparison.OrdinalIgnoreCase))
            {
                SetupMaterialAlphaMode(material, "BLEND", source.AlphaCutoff, options.MaterialRecoveryProfile);
            }
            else if (string.Equals(source.AlphaMode, "MASK", StringComparison.OrdinalIgnoreCase))
            {
                SetupMaterialAlphaMode(material, "MASK", source.AlphaCutoff, options.MaterialRecoveryProfile);
            }
            else
            {
                SetupMaterialAlphaMode(material, "OPAQUE", source.AlphaCutoff, options.MaterialRecoveryProfile);
            }

            if (source.DoubleSided && material.HasProperty("_Cull"))
            {
                material.SetFloat("_Cull", 0.0f);
            }

            if (!string.IsNullOrWhiteSpace(source.BaseColorTextureName) &&
                !textureByRef.ContainsKey(NormalizeRef(source.BaseColorTextureName)))
            {
                report.Warnings.Add($"XAV2_IMPORT_TEXTURE_UNRESOLVED: material={source.Name}, texture={source.BaseColorTextureName}");
            }
        }

        private static void ApplyTypedTexture(Material material, string slot, Texture texture)
        {
            if (material == null || texture == null || string.IsNullOrWhiteSpace(slot))
            {
                return;
            }

            if (string.Equals(slot, "base", StringComparison.OrdinalIgnoreCase))
            {
                if (material.HasProperty("_BaseMap")) material.SetTexture("_BaseMap", texture);
                if (material.HasProperty("_MainTex")) material.SetTexture("_MainTex", texture);
                return;
            }
            if (string.Equals(slot, "normal", StringComparison.OrdinalIgnoreCase))
            {
                if (material.HasProperty("_BumpMap")) material.SetTexture("_BumpMap", texture);
                if (material.HasProperty("_NormalMap")) material.SetTexture("_NormalMap", texture);
                return;
            }
            if (string.Equals(slot, "emission", StringComparison.OrdinalIgnoreCase))
            {
                if (material.HasProperty("_EmissionMap")) material.SetTexture("_EmissionMap", texture);
                return;
            }
            if (string.Equals(slot, "shade", StringComparison.OrdinalIgnoreCase))
            {
                if (material.HasProperty("_ShadeTexture")) material.SetTexture("_ShadeTexture", texture);
                return;
            }
            if (string.Equals(slot, "rim", StringComparison.OrdinalIgnoreCase))
            {
                if (material.HasProperty("_RimTex")) material.SetTexture("_RimTex", texture);
            }
        }

        private static void ApplyTypedColor(Material material, Xav2TypedColorParam value)
        {
            if (material == null || value == null)
            {
                return;
            }

            var color = new Color(value.R, value.G, value.B, value.A);
            if (string.Equals(value.Id, "_BaseColor", StringComparison.Ordinal) && material.HasProperty("_BaseColor"))
            {
                material.SetColor("_BaseColor", color);
                if (material.HasProperty("_Color"))
                {
                    material.SetColor("_Color", color);
                }
                return;
            }

            if (material.HasProperty(value.Id))
            {
                material.SetColor(value.Id, color);
            }
        }

        private static void ApplyTypedFloat(Material material, Xav2TypedFloatParam value)
        {
            if (material == null || value == null || string.IsNullOrWhiteSpace(value.Id))
            {
                return;
            }

            if (material.HasProperty(value.Id))
            {
                material.SetFloat(value.Id, value.Value);
            }
        }

        private static void SetupMaterialAlphaMode(
            Material material,
            string alphaMode,
            float cutoff,
            Xav2MaterialRecoveryProfile recoveryProfile)
        {
            if (material == null)
            {
                return;
            }

            var isBlend = string.Equals(alphaMode, "BLEND", StringComparison.OrdinalIgnoreCase);
            var isMask = string.Equals(alphaMode, "MASK", StringComparison.OrdinalIgnoreCase);

            if (material.HasProperty("_Surface"))
            {
                material.SetFloat("_Surface", isBlend ? 1.0f : 0.0f);
            }

            if (material.HasProperty("_Mode"))
            {
                material.SetFloat("_Mode", isBlend ? 3.0f : (isMask ? 1.0f : 0.0f));
            }

            if (material.HasProperty("_AlphaClip"))
            {
                material.SetFloat("_AlphaClip", isMask ? 1.0f : 0.0f);
            }

            if (material.HasProperty("_Cutoff"))
            {
                material.SetFloat("_Cutoff", Mathf.Clamp01(cutoff));
            }

            if (isBlend)
            {
                material.SetOverrideTag("RenderType", "Transparent");
                material.EnableKeyword("_ALPHABLEND_ON");
                material.DisableKeyword("_ALPHATEST_ON");
                material.renderQueue = 3000;
                if (material.HasProperty("_SrcBlend")) material.SetFloat("_SrcBlend", (float)UnityEngine.Rendering.BlendMode.SrcAlpha);
                if (material.HasProperty("_DstBlend")) material.SetFloat("_DstBlend", (float)UnityEngine.Rendering.BlendMode.OneMinusSrcAlpha);
                if (material.HasProperty("_ZWrite")) material.SetFloat("_ZWrite", 0.0f);
                return;
            }

            if (isMask)
            {
                material.SetOverrideTag("RenderType", "TransparentCutout");
                material.EnableKeyword("_ALPHATEST_ON");
                material.DisableKeyword("_ALPHABLEND_ON");
                material.renderQueue = 2450;
                if (material.HasProperty("_ZWrite")) material.SetFloat("_ZWrite", 1.0f);
                return;
            }

            material.SetOverrideTag("RenderType", "Opaque");
            material.DisableKeyword("_ALPHATEST_ON");
            material.DisableKeyword("_ALPHABLEND_ON");
            material.renderQueue = 2000;
            if (material.HasProperty("_ZWrite")) material.SetFloat("_ZWrite", 1.0f);

            if (recoveryProfile == Xav2MaterialRecoveryProfile.Aggressive && material.HasProperty("_Blend"))
            {
                material.SetFloat("_Blend", 0.0f);
            }
        }

        private static string EnsureChildFolder(string parent, string child)
        {
            var path = NormalizeAssetPath($"{parent}/{child}");
            EnsureFolder(path);
            return path;
        }

        private static void EnsureFolder(string assetPath)
        {
            var normalized = NormalizeAssetPath(assetPath);
            if (AssetDatabase.IsValidFolder(normalized))
            {
                return;
            }

            var parts = normalized.Split('/');
            if (parts.Length == 0 || !string.Equals(parts[0], "Assets", StringComparison.Ordinal))
            {
                throw new InvalidOperationException($"Import output must be under Assets/: '{assetPath}'.");
            }

            var current = "Assets";
            for (var i = 1; i < parts.Length; i++)
            {
                if (string.IsNullOrWhiteSpace(parts[i]))
                {
                    continue;
                }

                var candidate = current + "/" + parts[i];
                if (!AssetDatabase.IsValidFolder(candidate))
                {
                    AssetDatabase.CreateFolder(current, parts[i]);
                }

                current = candidate;
            }
        }

        private static string ToAbsolutePath(string assetPath)
        {
            var projectRoot = Directory.GetParent(Application.dataPath)?.FullName ?? string.Empty;
            var relative = NormalizeAssetPath(assetPath);
            return Path.Combine(projectRoot, relative.Replace('/', Path.DirectorySeparatorChar));
        }

        private static float ReadSingle(byte[] bytes, int offset)
        {
            return BitConverter.ToSingle(bytes, offset);
        }

        private static string NormalizeAssetPath(string path)
        {
            var normalized = string.IsNullOrWhiteSpace(path) ? "Assets/ImportedXav2" : path.Trim();
            normalized = normalized.Replace('\\', '/');
            if (!normalized.StartsWith("Assets", StringComparison.Ordinal))
            {
                normalized = "Assets/" + normalized.TrimStart('/');
            }
            return normalized.TrimEnd('/');
        }

        private static string NormalizeRef(string value)
        {
            if (string.IsNullOrWhiteSpace(value))
            {
                return string.Empty;
            }

            return value.Replace('\\', '/').Trim().ToLowerInvariant();
        }

        private static string SanitizeName(string raw)
        {
            if (string.IsNullOrWhiteSpace(raw))
            {
                return "Unnamed";
            }

            var sanitized = raw.Replace('\\', '_').Replace('/', '_').Replace(':', '_');
            sanitized = sanitized.Replace('*', '_').Replace('?', '_').Replace('"', '_');
            sanitized = sanitized.Replace('<', '_').Replace('>', '_').Replace('|', '_');
            return string.IsNullOrWhiteSpace(sanitized) ? "Unnamed" : sanitized;
        }
    }
}
