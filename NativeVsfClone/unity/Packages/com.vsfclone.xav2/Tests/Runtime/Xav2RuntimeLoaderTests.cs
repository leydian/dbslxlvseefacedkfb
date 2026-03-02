using System;
using System.IO;
using System.Text;
using NUnit.Framework;

namespace VsfClone.Xav2.Runtime.Tests
{
    public sealed class Xav2RuntimeLoaderTests
    {
        [Test]
        public void TryLoad_ValidPayload_ReturnsRuntimeReady()
        {
            var path = WriteTempFile(BuildValidXav2Bytes());
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out var payload, out var diagnostics);
                Assert.That(ok, Is.True);
                Assert.That(diagnostics.ErrorCode, Is.EqualTo(Xav2LoadErrorCode.None));
                Assert.That(diagnostics.ParserStage, Is.EqualTo("runtime-ready"));
                Assert.That(payload.Meshes.Count, Is.EqualTo(1));
                Assert.That(payload.Textures.Count, Is.EqualTo(1));
                Assert.That(payload.Materials.Count, Is.EqualTo(1));
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_MagicMismatch_Fails()
        {
            var bytes = BuildValidXav2Bytes();
            bytes[0] = (byte)'B';
            var path = WriteTempFile(bytes);
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out _, out var diagnostics);
                Assert.That(ok, Is.False);
                Assert.That(diagnostics.ErrorCode, Is.EqualTo(Xav2LoadErrorCode.MagicMismatch));
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_UnsupportedVersion_Fails()
        {
            var bytes = BuildValidXav2Bytes();
            bytes[4] = 0x02;
            bytes[5] = 0x00;
            var path = WriteTempFile(bytes);
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out _, out var diagnostics);
                Assert.That(ok, Is.False);
                Assert.That(diagnostics.ErrorCode, Is.EqualTo(Xav2LoadErrorCode.UnsupportedVersion));
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_ManifestTruncated_Fails()
        {
            var bytes = BuildValidXav2Bytes();
            // manifest_size starts at offset 6
            bytes[6] = 0xFF;
            bytes[7] = 0xFF;
            bytes[8] = 0xFF;
            bytes[9] = 0x7F;
            var path = WriteTempFile(bytes);
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out _, out var diagnostics);
                Assert.That(ok, Is.False);
                Assert.That(diagnostics.ErrorCode, Is.EqualTo(Xav2LoadErrorCode.ManifestTruncated));
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_SectionTruncated_Fails()
        {
            var bytes = BuildValidXav2Bytes();
            var truncated = new byte[bytes.Length - 3];
            Array.Copy(bytes, truncated, truncated.Length);
            var path = WriteTempFile(truncated);
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out _, out var diagnostics);
                Assert.That(ok, Is.False);
                Assert.That(diagnostics.ErrorCode, Is.EqualTo(Xav2LoadErrorCode.SectionTruncated));
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_LegacyMaterialWithoutVariant_Parses()
        {
            var path = WriteTempFile(BuildValidXav2Bytes(legacyMaterialFormat: true));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out var payload, out var diagnostics);
                Assert.That(ok, Is.True);
                Assert.That(diagnostics.ErrorCode, Is.EqualTo(Xav2LoadErrorCode.None));
                Assert.That(payload.Materials.Count, Is.EqualTo(1));
                Assert.That(payload.Materials[0].ShaderVariant, Is.EqualTo("default"));
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_UnknownSection_NonStrict_AllowsWithWarning()
        {
            var path = WriteTempFile(BuildValidXav2Bytes(addUnknownSection: true));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out _, out var diagnostics);
                Assert.That(ok, Is.True);
                Assert.That(diagnostics.Warnings.Exists(w => w.Contains("XAV2_UNKNOWN_SECTION")), Is.True);
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_UnknownSection_Strict_Fails()
        {
            var path = WriteTempFile(BuildValidXav2Bytes(addUnknownSection: true));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(
                    path,
                    out _,
                    out var diagnostics,
                    new Xav2LoadOptions { StrictValidation = true });
                Assert.That(ok, Is.False);
                Assert.That(diagnostics.ErrorCode, Is.EqualTo(Xav2LoadErrorCode.StrictValidationFailed));
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_MissingRef_Strict_Fails()
        {
            var path = WriteTempFile(BuildValidXav2Bytes(missingTextureRef: true));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(
                    path,
                    out _,
                    out var diagnostics,
                    new Xav2LoadOptions { StrictValidation = true });
                Assert.That(ok, Is.False);
                Assert.That(diagnostics.ErrorCode, Is.EqualTo(Xav2LoadErrorCode.StrictValidationFailed));
            }
            finally
            {
                File.Delete(path);
            }
        }

        private static byte[] BuildValidXav2Bytes(
            bool addUnknownSection = false,
            bool legacyMaterialFormat = false,
            bool missingTextureRef = false)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);

            var manifestJson = "{" +
                "\"schemaVersion\":1," +
                "\"exporterVersion\":\"0.3.0\"," +
                "\"avatarId\":\"avatar-1\"," +
                "\"displayName\":\"Avatar 1\"," +
                "\"sourceExt\":\".vrm\"," +
                "\"meshRefs\":[\"mesh_0\"]," +
                "\"materialRefs\":[\"material_0\"]," +
                (missingTextureRef
                    ? "\"textureRefs\":[\"texture_0\",\"texture_missing\"],"
                    : "\"textureRefs\":[\"texture_0\"],") +
                "\"strictShaderSet\":[]," +
                "\"hasSkinning\":false," +
                "\"hasBlendShapes\":false" +
                "}";
            var manifestBytes = Encoding.UTF8.GetBytes(manifestJson);

            bw.Write(Encoding.ASCII.GetBytes("XAV2"));
            bw.Write((ushort)1);
            bw.Write((uint)manifestBytes.Length);
            bw.Write(manifestBytes);

            WriteSection(bw, 0x0011, BuildMeshSection("mesh_0", 0, new byte[48], new uint[] { 0, 1, 2 }));
            WriteSection(bw, 0x0002, BuildTextureSection("texture_0", new byte[] { 1, 2, 3, 4 }));
            WriteSection(
                bw,
                0x0003,
                legacyMaterialFormat
                    ? BuildMaterialSectionLegacy("material_0", "lilToon", "texture_0")
                    : BuildMaterialSectionV1("material_0", "lilToon", "default", "texture_0"));
            WriteSection(bw, 0x0012, BuildMaterialParamsSection("material_0", "{}"));

            if (addUnknownSection)
            {
                WriteSection(bw, 0x77EE, new byte[] { 0xAA, 0xBB, 0xCC });
            }

            return ms.ToArray();
        }

        private static byte[] BuildMeshSection(string name, int materialIndex, byte[] vertexBlob, uint[] indices)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, name);
            bw.Write((uint)48);
            bw.Write(materialIndex);
            bw.Write((uint)vertexBlob.Length);
            bw.Write(vertexBlob);
            bw.Write((uint)indices.Length);
            foreach (var idx in indices)
            {
                bw.Write(idx);
            }
            return ms.ToArray();
        }

        private static byte[] BuildTextureSection(string name, byte[] bytes)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, name);
            bw.Write((uint)bytes.Length);
            bw.Write(bytes);
            return ms.ToArray();
        }

        private static byte[] BuildMaterialSectionV1(string name, string shader, string variant, string baseColorTexture)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, name);
            WriteSizedString(bw, shader);
            WriteSizedString(bw, variant);
            WriteSizedString(bw, baseColorTexture);
            WriteSizedString(bw, "OPAQUE");
            bw.Write(0.5f);
            bw.Write((byte)0);
            return ms.ToArray();
        }

        private static byte[] BuildMaterialSectionLegacy(string name, string shader, string baseColorTexture)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, name);
            WriteSizedString(bw, shader);
            WriteSizedString(bw, baseColorTexture);
            WriteSizedString(bw, "OPAQUE");
            bw.Write(0.5f);
            bw.Write((byte)0);
            return ms.ToArray();
        }

        private static byte[] BuildMaterialParamsSection(string name, string json)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, name);
            WriteSizedString(bw, json);
            return ms.ToArray();
        }

        private static void WriteSection(BinaryWriter bw, ushort type, byte[] payload)
        {
            bw.Write(type);
            bw.Write((ushort)0);
            bw.Write((uint)payload.Length);
            bw.Write(payload);
        }

        private static void WriteSizedString(BinaryWriter bw, string value)
        {
            var bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
            bw.Write((ushort)bytes.Length);
            bw.Write(bytes);
        }

        private static string WriteTempFile(byte[] bytes)
        {
            var path = Path.Combine(Path.GetTempPath(), $"xav2-test-{Guid.NewGuid():N}.xav2");
            File.WriteAllBytes(path, bytes);
            return path;
        }
    }
}
