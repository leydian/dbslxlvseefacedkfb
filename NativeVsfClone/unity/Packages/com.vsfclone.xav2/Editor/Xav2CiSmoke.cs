using System;
using System.IO;
using UnityEditor;
using UnityEngine;
using VsfClone.Xav2.Runtime;

namespace VsfClone.Xav2.Editor
{
    public static class Xav2CiSmoke
    {
        [Serializable]
        private sealed class SmokeReport
        {
            public string unity_version = string.Empty;
            public bool export_smoke_passed;
            public bool load_smoke_passed;
            public string diagnostics_error_code = string.Empty;
            public string diagnostics_parser_stage = string.Empty;
            public string error_message = string.Empty;
            public string overall_status = "FAIL";
        }

        public static void Run()
        {
            var report = new SmokeReport();
            GameObject avatarRoot = null;
            Mesh generatedMesh = null;
            Material generatedMaterial = null;
            string reportPath = null;
            try
            {
                report.unity_version = Application.unityVersion;
                var args = Environment.GetCommandLineArgs();
                var outputPath = GetArgValue(args, "-xav2SmokeOutputPath");
                reportPath = GetArgValue(args, "-xav2SmokeReportPath");
                var expectedUnityVersion = GetArgValue(args, "-xav2ExpectedUnityVersion");

                if (!string.IsNullOrWhiteSpace(expectedUnityVersion) &&
                    !string.Equals(Application.unityVersion, expectedUnityVersion, StringComparison.OrdinalIgnoreCase))
                {
                    throw new InvalidOperationException(
                        "Unity version mismatch. expected='" + expectedUnityVersion + "', actual='" + Application.unityVersion + "'");
                }

                if (string.IsNullOrWhiteSpace(outputPath))
                {
                    outputPath = Path.Combine(Path.GetTempPath(), "xav2-ci-smoke.xav2");
                }

                avatarRoot = BuildMinimalAvatarRoot();
                var smr = avatarRoot.GetComponentInChildren<SkinnedMeshRenderer>(true);
                if (smr != null)
                {
                    generatedMesh = smr.sharedMesh;
                    generatedMaterial = smr.sharedMaterial;
                }
                var options = new Xav2ExportOptions();
                options.FailOnMissingShader = false;
                Xav2Exporter.Export(outputPath, avatarRoot, options);

                var outFile = new FileInfo(outputPath);
                report.export_smoke_passed = outFile.Exists && outFile.Length > 0;
                if (!report.export_smoke_passed)
                {
                    throw new InvalidOperationException("XAV2 export smoke failed to create output.");
                }

                Xav2LoadDiagnostics diagnostics;
                Xav2AvatarPayload payload;
                var ok = Xav2RuntimeLoader.TryLoad(outputPath, out payload, out diagnostics);
                report.diagnostics_error_code = diagnostics.ErrorCode.ToString();
                report.diagnostics_parser_stage = diagnostics.ParserStage ?? string.Empty;
                report.load_smoke_passed =
                    ok &&
                    diagnostics.ErrorCode == Xav2LoadErrorCode.None &&
                    string.Equals(diagnostics.ParserStage, "runtime-ready", StringComparison.Ordinal);

                if (!report.load_smoke_passed)
                {
                    throw new InvalidOperationException(
                        "XAV2 load smoke failed. code='" + diagnostics.ErrorCode + "', stage='" + diagnostics.ParserStage + "'");
                }

                report.overall_status = "PASS";
            }
            catch (Exception ex)
            {
                report.error_message = ex.Message ?? "unknown error";
                report.overall_status = "FAIL";
            }
            finally
            {
                if (avatarRoot != null)
                {
                    UnityEngine.Object.DestroyImmediate(avatarRoot);
                }
                if (generatedMesh != null)
                {
                    UnityEngine.Object.DestroyImmediate(generatedMesh);
                }
                if (generatedMaterial != null)
                {
                    UnityEngine.Object.DestroyImmediate(generatedMaterial);
                }

                if (!string.IsNullOrWhiteSpace(reportPath))
                {
                    try
                    {
                        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(reportPath)) ?? ".");
                        File.WriteAllText(reportPath, JsonUtility.ToJson(report, true));
                    }
                    catch
                    {
                        // best-effort report write
                    }
                }

                EditorApplication.Exit(string.Equals(report.overall_status, "PASS", StringComparison.Ordinal) ? 0 : 1);
            }
        }

        private static string GetArgValue(string[] args, string key)
        {
            if (args == null || string.IsNullOrWhiteSpace(key))
            {
                return null;
            }
            for (var i = 0; i < args.Length - 1; i++)
            {
                if (string.Equals(args[i], key, StringComparison.Ordinal))
                {
                    return args[i + 1];
                }
            }
            return null;
        }

        private static GameObject BuildMinimalAvatarRoot()
        {
            var root = new GameObject("AvatarRoot_CI");
            var bone = new GameObject("Bone_0");
            bone.transform.SetParent(root.transform, false);

            var meshNode = new GameObject("MeshNode");
            meshNode.transform.SetParent(root.transform, false);

            var smr = meshNode.AddComponent<SkinnedMeshRenderer>();
            var mesh = new Mesh();
            mesh.name = "CiSmokeMesh";
            mesh.vertices = new[]
            {
                new Vector3(0.0f, 0.0f, 0.0f),
                new Vector3(0.0f, 1.0f, 0.0f),
                new Vector3(1.0f, 0.0f, 0.0f)
            };
            mesh.normals = new[]
            {
                Vector3.forward,
                Vector3.forward,
                Vector3.forward
            };
            mesh.uv = new[]
            {
                new Vector2(0.0f, 0.0f),
                new Vector2(0.0f, 1.0f),
                new Vector2(1.0f, 0.0f)
            };
            mesh.tangents = new[]
            {
                new Vector4(1.0f, 0.0f, 0.0f, 1.0f),
                new Vector4(1.0f, 0.0f, 0.0f, 1.0f),
                new Vector4(1.0f, 0.0f, 0.0f, 1.0f)
            };
            mesh.triangles = new[] { 0, 1, 2 };
            mesh.bindposes = new[] { Matrix4x4.identity };
            mesh.boneWeights = new[]
            {
                new BoneWeight { boneIndex0 = 0, weight0 = 1.0f },
                new BoneWeight { boneIndex0 = 0, weight0 = 1.0f },
                new BoneWeight { boneIndex0 = 0, weight0 = 1.0f }
            };

            smr.sharedMesh = mesh;
            smr.bones = new[] { bone.transform };
            smr.rootBone = bone.transform;
            var shader = Shader.Find("Standard");
            if (shader == null)
            {
                shader = Shader.Find("Legacy Shaders/Diffuse");
            }
            if (shader == null)
            {
                throw new InvalidOperationException("No suitable built-in shader found for CI smoke material.");
            }
            smr.sharedMaterial = new Material(shader);

            return root;
        }
    }
}
