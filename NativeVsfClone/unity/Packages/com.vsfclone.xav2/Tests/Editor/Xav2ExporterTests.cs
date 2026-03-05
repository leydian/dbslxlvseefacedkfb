using System;
using System.IO;
using NUnit.Framework;
using VsfClone.Xav2.Runtime;

namespace VsfClone.Xav2.Editor.Tests
{
    public sealed class Xav2ExporterTests
    {
        [Test]
        public void Export_CompressionEnabled_ProducesV5AndSmallerFile()
        {
            var payload = CreateCompressiblePayload();
            var basePath = Path.Combine(Path.GetTempPath(), $"xav2_export_base_{Guid.NewGuid():N}.xav2");
            var compressedPath = Path.Combine(Path.GetTempPath(), $"xav2_export_compressed_{Guid.NewGuid():N}.xav2");
            try
            {
                Xav2Exporter.Export(basePath, payload, new Xav2ExportOptions
                {
                    EnableCompression = false
                });
                Xav2Exporter.Export(compressedPath, payload, new Xav2ExportOptions
                {
                    EnableCompression = true,
                    CompressionCodec = Xav2CompressionCodec.Lz4,
                    CompressionLevel = Xav2CompressionLevel.Balanced
                });

                var baseSize = new FileInfo(basePath).Length;
                var compressedSize = new FileInfo(compressedPath).Length;
                Assert.That(compressedSize, Is.LessThan(baseSize));

                var header = File.ReadAllBytes(compressedPath);
                Assert.That(header.Length, Is.GreaterThanOrEqualTo(6));
                var version = BitConverter.ToUInt16(header, 4);
                Assert.That(version, Is.EqualTo(5));

                var ok = Xav2RuntimeLoader.TryLoad(compressedPath, out var loaded, out var diagnostics);
                Assert.That(ok, Is.True, diagnostics.ErrorMessage);
                Assert.That(loaded.Meshes.Count, Is.EqualTo(1));
                Assert.That(loaded.Textures.Count, Is.EqualTo(1));
                Assert.That(diagnostics.ErrorCode, Is.EqualTo(Xav2LoadErrorCode.None));
            }
            finally
            {
                if (File.Exists(basePath))
                {
                    File.Delete(basePath);
                }
                if (File.Exists(compressedPath))
                {
                    File.Delete(compressedPath);
                }
            }
        }

        [Test]
        public void Export_CompressionDisabled_ProducesV4()
        {
            var payload = CreateCompressiblePayload();
            var path = Path.Combine(Path.GetTempPath(), $"xav2_export_nocompress_{Guid.NewGuid():N}.xav2");
            try
            {
                Xav2Exporter.Export(path, payload, new Xav2ExportOptions
                {
                    EnableCompression = false
                });

                var bytes = File.ReadAllBytes(path);
                var version = BitConverter.ToUInt16(bytes, 4);
                Assert.That(version, Is.EqualTo(4));
            }
            finally
            {
                if (File.Exists(path))
                {
                    File.Delete(path);
                }
            }
        }

        [Test]
        public void Export_WithPhysicsSections_RoundTrips()
        {
            var payload = CreateCompressiblePayload();
            payload.PhysicsColliders.Add(new Xav2PhysicsColliderPayload
            {
                Name = "col_0",
                BonePath = "Root/Bone",
                Shape = Xav2PhysicsColliderShape.Sphere,
                Radius = 0.05f,
                LocalDirection = new[] { 0.0f, 0.0f, 1.0f }
            });
            payload.SpringBones.Add(new Xav2SpringBonePayload
            {
                Name = "sp_0",
                RootBonePath = "Root/Bone",
                BonePaths = { "Root/Bone" },
                ColliderRefs = { "col_0" },
                Stiffness = 0.6f,
                Drag = 0.2f
            });
            payload.PhysBones.Add(new Xav2PhysBonePayload
            {
                Name = "pb_0",
                RootBonePath = "Root/Bone",
                BonePaths = { "Root/Bone" },
                ColliderRefs = { "col_0" },
                Pull = 0.5f,
                Spring = 0.7f
            });

            var path = Path.Combine(Path.GetTempPath(), $"xav2_export_physics_{Guid.NewGuid():N}.xav2");
            try
            {
                Xav2Exporter.Export(path, payload, new Xav2ExportOptions
                {
                    EnableCompression = false
                });

                var ok = Xav2RuntimeLoader.TryLoad(path, out var loaded, out var diagnostics);
                Assert.That(ok, Is.True, diagnostics.ErrorMessage);
                Assert.That(loaded.PhysicsColliders.Count, Is.EqualTo(1));
                Assert.That(loaded.SpringBones.Count, Is.EqualTo(1));
                Assert.That(loaded.PhysBones.Count, Is.EqualTo(1));
                Assert.That(loaded.Manifest.physicsSource, Is.EqualTo("mixed"));
            }
            finally
            {
                if (File.Exists(path))
                {
                    File.Delete(path);
                }
            }
        }

        private static Xav2AvatarPayload CreateCompressiblePayload()
        {
            var payload = new Xav2AvatarPayload();
            payload.Manifest.avatarId = "compress-test";
            payload.Manifest.displayName = "compress-test";

            var vertexBlob = new byte[64 * 1024];
            for (var i = 0; i < vertexBlob.Length; i++)
            {
                vertexBlob[i] = (byte)(i % 8);
            }

            var textureBytes = new byte[128 * 1024];
            for (var i = 0; i < textureBytes.Length; i++)
            {
                textureBytes[i] = (byte)(i % 4);
            }

            payload.Meshes.Add(new Xav2MeshPayload
            {
                Name = "mesh_0",
                VertexStride = 48,
                VertexBlob = vertexBlob,
                Indices = new uint[] { 0, 1, 2 },
                MaterialIndex = 0
            });
            payload.Textures.Add(new Xav2TexturePayload
            {
                Name = "texture_0",
                Bytes = textureBytes
            });
            payload.Materials.Add(new Xav2MaterialPayload
            {
                Name = "material_0",
                ShaderName = "lilToon",
                BaseColorTextureName = "texture_0",
                AlphaMode = "OPAQUE"
            });

            payload.Manifest.meshRefs.Add("mesh_0");
            payload.Manifest.materialRefs.Add("material_0");
            payload.Manifest.textureRefs.Add("texture_0");
            return payload;
        }
    }
}
