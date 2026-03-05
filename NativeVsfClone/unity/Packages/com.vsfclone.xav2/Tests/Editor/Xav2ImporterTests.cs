using System;
using System.IO;
using NUnit.Framework;
using UnityEditor;
using UnityEngine;
using VsfClone.Xav2.Runtime;

namespace VsfClone.Xav2.Editor.Tests
{
    public sealed class Xav2ImporterTests
    {
        private const string TestOutputRoot = "Assets/__Xav2ImportTestsOut";

        [SetUp]
        public void SetUp()
        {
            AssetDatabase.DeleteAsset(TestOutputRoot);
            AssetDatabase.Refresh();
        }

        [TearDown]
        public void TearDown()
        {
            AssetDatabase.DeleteAsset(TestOutputRoot);
            AssetDatabase.Refresh();
        }

        [Test]
        public void Import_CreatesPrefabAndAssets()
        {
            var xav2Path = CreateSampleXav2();
            try
            {
                var report = Xav2Importer.Import(xav2Path, new Xav2ImportOptions { OutputRoot = TestOutputRoot });

                Assert.That(report.Success, Is.True, report.ErrorMessage);
                Assert.That(report.PrefabPath, Is.Not.Empty);
                Assert.That(AssetDatabase.LoadAssetAtPath<GameObject>(report.PrefabPath), Is.Not.Null);
                Assert.That(report.CreatedAssets.Count, Is.GreaterThanOrEqualTo(3));
            }
            finally
            {
                if (File.Exists(xav2Path))
                {
                    File.Delete(xav2Path);
                }
            }
        }

        [Test]
        public void Import_Twice_UsesUniqueSuffixPaths()
        {
            var xav2Path = CreateSampleXav2();
            try
            {
                var options = new Xav2ImportOptions { OutputRoot = TestOutputRoot };
                var first = Xav2Importer.Import(xav2Path, options);
                var second = Xav2Importer.Import(xav2Path, options);

                Assert.That(first.Success, Is.True, first.ErrorMessage);
                Assert.That(second.Success, Is.True, second.ErrorMessage);
                Assert.That(first.PrefabPath, Is.Not.EqualTo(second.PrefabPath));
            }
            finally
            {
                if (File.Exists(xav2Path))
                {
                    File.Delete(xav2Path);
                }
            }
        }

        private static string CreateSampleXav2()
        {
            var payload = new Xav2AvatarPayload();
            payload.Manifest.avatarId = "TestAvatar";
            payload.Manifest.displayName = "TestAvatar";

            payload.Meshes.Add(new Xav2MeshPayload
            {
                Name = "Body",
                VertexStride = 48,
                VertexBlob = CreateVertexBlob(),
                Indices = new uint[] { 0, 1, 2 },
                MaterialIndex = 0
            });

            var textureBytes = CreateTextureBytes();
            payload.Textures.Add(new Xav2TexturePayload
            {
                Name = "BaseTex",
                Bytes = textureBytes
            });

            payload.Materials.Add(new Xav2MaterialPayload
            {
                Name = "BodyMat",
                ShaderName = "Standard",
                BaseColorTextureName = "BaseTex",
                AlphaMode = "OPAQUE"
            });

            payload.Manifest.meshRefs.Add("Body");
            payload.Manifest.materialRefs.Add("BodyMat");
            payload.Manifest.textureRefs.Add("BaseTex");

            var path = Path.Combine(Path.GetTempPath(), $"xav2_import_test_{Guid.NewGuid():N}.xav2");
            Xav2Exporter.Export(path, payload, new Xav2ExportOptions());
            return path;
        }

        private static byte[] CreateTextureBytes()
        {
            var tex = new Texture2D(2, 2, TextureFormat.RGBA32, false);
            tex.SetPixels(new[]
            {
                Color.red, Color.green,
                Color.blue, Color.white
            });
            tex.Apply(false, false);
            var bytes = tex.EncodeToPNG();
            UnityEngine.Object.DestroyImmediate(tex);
            return bytes;
        }

        private static byte[] CreateVertexBlob()
        {
            var vertices = new[]
            {
                new Vector3(0, 0, 0),
                new Vector3(0, 1, 0),
                new Vector3(1, 0, 0)
            };
            var normals = new[]
            {
                Vector3.forward,
                Vector3.forward,
                Vector3.forward
            };
            var uvs = new[]
            {
                new Vector2(0, 0),
                new Vector2(0, 1),
                new Vector2(1, 0)
            };
            var tangents = new[]
            {
                new Vector4(1, 0, 0, 1),
                new Vector4(1, 0, 0, 1),
                new Vector4(1, 0, 0, 1)
            };

            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms);
            for (var i = 0; i < vertices.Length; i++)
            {
                bw.Write(vertices[i].x); bw.Write(vertices[i].y); bw.Write(vertices[i].z);
                bw.Write(normals[i].x); bw.Write(normals[i].y); bw.Write(normals[i].z);
                bw.Write(uvs[i].x); bw.Write(uvs[i].y);
                bw.Write(tangents[i].x); bw.Write(tangents[i].y); bw.Write(tangents[i].z); bw.Write(tangents[i].w);
            }
            return ms.ToArray();
        }
    }
}
