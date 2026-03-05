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
            bytes[4] = 0x06;
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
        public void TryLoad_TypedMaterialParams_Parses()
        {
            var path = WriteTempFile(BuildValidXav2Bytes(addTypedMaterialSection: true));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out var payload, out var diagnostics);
                Assert.That(ok, Is.True);
                Assert.That(diagnostics.ErrorCode, Is.EqualTo(Xav2LoadErrorCode.None));
                Assert.That(payload.Materials.Count, Is.EqualTo(1));
                var material = payload.Materials[0];
                Assert.That(material.MaterialParamEncoding, Is.EqualTo("typed-v3"));
                Assert.That(material.TypedSchemaVersion, Is.EqualTo(3));
                Assert.That(material.ShaderFamily, Is.EqualTo("liltoon"));
                Assert.That(material.TypedFloatParams.Exists(p => p.Id == "_Cutoff"), Is.True);
                Assert.That(material.TypedColorParams.Exists(p => p.Id == "_BaseColor"), Is.True);
                Assert.That(material.TypedTextureParams.Exists(p => p.Slot == "base"), Is.True);
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_TypedMaterialParams_AdvancedLilToonEntries_Parses()
        {
            var path = WriteTempFile(BuildValidXav2Bytes(addTypedMaterialSection: true, typedIncludeAdvancedEntries: true));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out var payload, out var diagnostics);
                Assert.That(ok, Is.True);
                Assert.That(diagnostics.ErrorCode, Is.EqualTo(Xav2LoadErrorCode.None));
                var material = payload.Materials[0];
                Assert.That(material.TypedFloatParams.Exists(p => p.Id == "_MatCapBlend"), Is.True);
                Assert.That(material.TypedFloatParams.Exists(p => p.Id == "_EmissionStrength"), Is.True);
                Assert.That(material.TypedColorParams.Exists(p => p.Id == "_MatCapColor"), Is.True);
                Assert.That(material.TypedTextureParams.Exists(p => p.Slot == "matcap"), Is.True);
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_TypedMaterialParams_AdvancedPoiyomiEntries_Parses()
        {
            var path = WriteTempFile(
                BuildValidXav2Bytes(
                    addTypedMaterialSection: true,
                    typedShaderFamilyOverride: "poiyomi",
                    typedIncludeAdvancedEntries: true));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out var payload, out var diagnostics);
                Assert.That(ok, Is.True);
                Assert.That(diagnostics.ErrorCode, Is.EqualTo(Xav2LoadErrorCode.None));
                Assert.That(diagnostics.WarningCodes, Does.Not.Contain("XAV2_MATERIAL_TYPED_UNSUPPORTED_SHADER_FAMILY"));
                var material = payload.Materials[0];
                Assert.That(material.ShaderFamily, Is.EqualTo("poiyomi"));
                Assert.That(material.TypedFloatParams.Exists(p => p.Id == "_MatCapBlend"), Is.True);
                Assert.That(material.TypedFloatParams.Exists(p => p.Id == "_EmissionStrength"), Is.True);
                Assert.That(material.TypedColorParams.Exists(p => p.Id == "_MatCapColor"), Is.True);
                Assert.That(material.TypedTextureParams.Exists(p => p.Slot == "matcap"), Is.True);
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_TypedMaterialParamsV2_LegacyShape_Parses()
        {
            var path = WriteTempFile(BuildValidXav2Bytes(addTypedMaterialSection: true, typedSchemaVersion: 2));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out var payload, out var diagnostics);
                Assert.That(ok, Is.True);
                Assert.That(diagnostics.ErrorCode, Is.EqualTo(Xav2LoadErrorCode.None));
                Assert.That(diagnostics.MigrationApplied, Is.True);
                Assert.That(payload.Materials.Count, Is.EqualTo(1));
                var material = payload.Materials[0];
                Assert.That(material.MaterialParamEncoding, Is.EqualTo("typed-v3"));
                Assert.That(material.TypedSchemaVersion, Is.EqualTo(3));
                Assert.That(material.TypedTextureParams.Exists(p => p.Slot == "base"), Is.True);
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_TypedMaterialTextureRefMissing_Warns()
        {
            var path = WriteTempFile(BuildValidXav2Bytes(addTypedMaterialSection: true, unresolvedTypedTextureRef: true));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out _, out var diagnostics);
                Assert.That(ok, Is.True);
                Assert.That(
                    diagnostics.Warnings.Exists(w => w.Contains("XAV2_MATERIAL_TYPED_TEXTURE_UNRESOLVED")),
                    Is.True);
                Assert.That(diagnostics.WarningCodes, Does.Contain("XAV2_MATERIAL_TYPED_TEXTURE_UNRESOLVED"));
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_TypedMaterialTextureRef_NormalizedMatch_DoesNotWarn()
        {
            var path = WriteTempFile(
                BuildValidXav2Bytes(
                    addTypedMaterialSection: true,
                    textureRefName: "FaceTex@Assets/Avatars/Face.png",
                    typedBaseTextureRefOverride: "facetex@assets\\avatars\\face.png"));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out _, out var diagnostics);
                Assert.That(ok, Is.True);
                Assert.That(
                    diagnostics.Warnings.Exists(w => w.Contains("XAV2_MATERIAL_TYPED_TEXTURE_UNRESOLVED")),
                    Is.False);
                Assert.That(diagnostics.WarningCodes, Does.Not.Contain("XAV2_MATERIAL_TYPED_TEXTURE_UNRESOLVED"));
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_TypedMaterialSupportedShaderFamily_DoesNotWarnUnsupported()
        {
            var path = WriteTempFile(
                BuildValidXav2Bytes(
                    addTypedMaterialSection: true,
                    typedShaderFamilyOverride: "poiyomi"));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out _, out var diagnostics);
                Assert.That(ok, Is.True);
                Assert.That(
                    diagnostics.Warnings.Exists(w => w.Contains("XAV2_MATERIAL_TYPED_UNSUPPORTED_SHADER_FAMILY")),
                    Is.False);
                Assert.That(diagnostics.WarningCodes, Does.Not.Contain("XAV2_MATERIAL_TYPED_UNSUPPORTED_SHADER_FAMILY"));
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_TypedMaterialUnsupportedShaderFamily_Fails()
        {
            var path = WriteTempFile(
                BuildValidXav2Bytes(
                    addTypedMaterialSection: true,
                    typedShaderFamilyOverride: "unsupported-family"));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out _, out var diagnostics);
                Assert.That(ok, Is.False);
                Assert.That(diagnostics.ErrorCode, Is.EqualTo(Xav2LoadErrorCode.ParityContractViolation));
                Assert.That(diagnostics.CriticalParityViolation, Is.True);
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_TypedMaterialMissingBaseColor_Strict_Fails()
        {
            var path = WriteTempFile(
                BuildValidXav2Bytes(
                    addTypedMaterialSection: true,
                    typedIncludeBaseColor: false));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(
                    path,
                    out _,
                    out var diagnostics,
                    new Xav2LoadOptions { StrictValidation = true });
                Assert.That(ok, Is.False);
                Assert.That(diagnostics.ErrorCode, Is.EqualTo(Xav2LoadErrorCode.StrictValidationFailed));
                Assert.That(diagnostics.Warnings.Exists(w => w.Contains("XAV2_MATERIAL_TYPED_MISSING_REQUIRED_PARAM")), Is.True);
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_V3SkinWithoutSkeleton_Warns()
        {
            var path = WriteTempFile(BuildValidXav2Bytes(addSkinSection: true));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out _, out var diagnostics);
                Assert.That(ok, Is.True);
                Assert.That(
                    diagnostics.Warnings.Exists(w => w.Contains("XAV3_SKELETON_PAYLOAD_MISSING")),
                    Is.True);
                Assert.That(diagnostics.WarningCodes, Does.Contain("XAV3_SKELETON_PAYLOAD_MISSING"));
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_V4SkinWithoutRig_Warns()
        {
            var path = WriteTempFile(BuildValidXav2Bytes(addSkinSection: true, formatVersion: 4));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out _, out var diagnostics);
                Assert.That(ok, Is.True);
                Assert.That(
                    diagnostics.Warnings.Exists(w => w.Contains("XAV4_RIG_MISSING")),
                    Is.True);
                Assert.That(diagnostics.WarningCodes, Does.Contain("XAV4_RIG_MISSING"));
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_V4SkinWithRig_DoesNotWarnMissingRig()
        {
            var path = WriteTempFile(BuildValidXav2Bytes(addSkinSection: true, addRigSection: true, formatVersion: 4));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out var payload, out var diagnostics);
                Assert.That(ok, Is.True);
                Assert.That(
                    diagnostics.Warnings.Exists(w => w.Contains("XAV4_RIG_MISSING")),
                    Is.False);
                Assert.That(payload.SkeletonRigs.Count, Is.EqualTo(1));
                Assert.That(payload.SkeletonRigs[0].Bones.Count, Is.EqualTo(1));
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_V4RigDuplicateBoneName_WarnsSchemaInvalid()
        {
            var path = WriteTempFile(BuildValidXav2Bytes(addSkinSection: true, addRigSection: true, formatVersion: 4, rigDuplicateBoneName: true));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out _, out var diagnostics);
                Assert.That(ok, Is.True);
                Assert.That(diagnostics.WarningCodes, Does.Contain("XAV4_RIG_SCHEMA_INVALID"));
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_V4RigCycle_Strict_Fails()
        {
            var path = WriteTempFile(BuildValidXav2Bytes(addSkinSection: true, addRigSection: true, formatVersion: 4, rigCycle: true));
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
        public void TryLoad_CompressedMeshSection_V5_Parses()
        {
            var path = WriteTempFile(BuildValidXav2Bytes(formatVersion: 5, compressMeshSection: true));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out var payload, out var diagnostics);
                Assert.That(ok, Is.True);
                Assert.That(diagnostics.ErrorCode, Is.EqualTo(Xav2LoadErrorCode.None));
                Assert.That(payload.Meshes.Count, Is.EqualTo(1));
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_CompressedMeshSectionCorrupt_FailsDecode()
        {
            var path = WriteTempFile(
                BuildValidXav2Bytes(
                    formatVersion: 5,
                    compressMeshSection: true,
                    corruptCompressedSection: true));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out _, out var diagnostics);
                Assert.That(ok, Is.False);
                Assert.That(diagnostics.ErrorCode, Is.EqualTo(Xav2LoadErrorCode.CompressionDecodeFailed));
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_CompressedMeshSectionEnvelopeTruncated_FailsDecode()
        {
            var path = WriteTempFile(
                BuildValidXav2Bytes(
                    formatVersion: 5,
                    compressMeshSection: true,
                    truncateCompressedEnvelope: true));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out _, out var diagnostics);
                Assert.That(ok, Is.False);
                Assert.That(diagnostics.ErrorCode, Is.EqualTo(Xav2LoadErrorCode.CompressionDecodeFailed));
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_CompressedMeshSectionV4_FailsSchemaInvalid()
        {
            var path = WriteTempFile(
                BuildValidXav2Bytes(
                    formatVersion: 4,
                    compressMeshSection: true));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out _, out var diagnostics);
                Assert.That(ok, Is.False);
                Assert.That(diagnostics.ErrorCode, Is.EqualTo(Xav2LoadErrorCode.SectionSchemaInvalid));
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
                Assert.That(diagnostics.MigrationApplied, Is.True);
                Assert.That(payload.Materials.Count, Is.EqualTo(1));
                Assert.That(payload.Materials[0].ShaderVariant, Is.EqualTo("default"));
                Assert.That(payload.Materials[0].MaterialParamEncoding, Is.EqualTo("typed-v3"));
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
                Assert.That(diagnostics.WarningCodes, Does.Contain("XAV2_UNKNOWN_SECTION"));
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_UnknownSection_IgnorePolicy_AllowsWithoutWarning()
        {
            var path = WriteTempFile(BuildValidXav2Bytes(addUnknownSection: true));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(
                    path,
                    out _,
                    out var diagnostics,
                    new Xav2LoadOptions { UnknownSectionPolicy = Xav2UnknownSectionPolicy.Ignore });
                Assert.That(ok, Is.True);
                Assert.That(diagnostics.Warnings.Exists(w => w.Contains("XAV2_UNKNOWN_SECTION")), Is.False);
                Assert.That(diagnostics.WarningCodes, Does.Not.Contain("XAV2_UNKNOWN_SECTION"));
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_UnknownSection_FailPolicy_Fails()
        {
            var path = WriteTempFile(BuildValidXav2Bytes(addUnknownSection: true));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(
                    path,
                    out _,
                    out var diagnostics,
                    new Xav2LoadOptions { UnknownSectionPolicy = Xav2UnknownSectionPolicy.Fail });
                Assert.That(ok, Is.False);
                Assert.That(diagnostics.ErrorCode, Is.EqualTo(Xav2LoadErrorCode.UnknownSectionNotAllowed));
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
        public void TryLoad_PhysicsSections_ParseAndWarnRuntimeUnavailable()
        {
            var path = WriteTempFile(BuildValidXav2Bytes(addPhysicsSections: true));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out var payload, out var diagnostics);
                Assert.That(ok, Is.True);
                Assert.That(payload.PhysicsColliders.Count, Is.EqualTo(1));
                Assert.That(payload.SpringBones.Count, Is.EqualTo(1));
                Assert.That(payload.PhysBones.Count, Is.EqualTo(1));
                Assert.That(diagnostics.WarningCodes, Does.Contain("XAV2_PHYSICS_COMPONENT_UNAVAILABLE"));
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Test]
        public void TryLoad_PhysicsColliderRefMissing_Warns()
        {
            var path = WriteTempFile(BuildValidXav2Bytes(addPhysicsSections: true, missingPhysicsColliderRef: true));
            try
            {
                var ok = Xav2RuntimeLoader.TryLoad(path, out _, out var diagnostics);
                Assert.That(ok, Is.True);
                Assert.That(diagnostics.WarningCodes, Does.Contain("XAV2_PHYSICS_REF_MISSING"));
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
            bool missingTextureRef = false,
            bool addTypedMaterialSection = false,
            bool unresolvedTypedTextureRef = false,
            string textureRefName = "texture_0",
            string typedBaseTextureRefOverride = null,
            string typedShaderFamilyOverride = null,
            bool typedIncludeBaseColor = true,
            bool typedIncludeAdvancedEntries = false,
            bool addSkinSection = false,
            bool addSkeletonSection = false,
            bool addRigSection = false,
            ushort formatVersion = 3,
            bool rigDuplicateBoneName = false,
            bool rigCycle = false,
            bool compressMeshSection = false,
            bool corruptCompressedSection = false,
            bool truncateCompressedEnvelope = false,
            ushort typedSchemaVersion = 3,
            bool addPhysicsSections = false,
            bool missingPhysicsColliderRef = false)
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
                    ? $"\"textureRefs\":[\"{textureRefName}\",\"texture_missing\"],"
                    : $"\"textureRefs\":[\"{textureRefName}\"],") +
                "\"strictShaderSet\":[]," +
                "\"hasSkinning\":false," +
                "\"hasBlendShapes\":false," +
                "\"hasSpringBones\":" + (addPhysicsSections ? "true" : "false") + "," +
                "\"hasPhysBones\":" + (addPhysicsSections ? "true" : "false") + "," +
                "\"physicsSchemaVersion\":1," +
                "\"physicsSource\":\"" + (addPhysicsSections ? "mixed" : "none") + "\"" +
                "}";
            var manifestBytes = Encoding.UTF8.GetBytes(manifestJson);

            bw.Write(Encoding.ASCII.GetBytes("XAV2"));
            bw.Write(formatVersion);
            bw.Write((uint)manifestBytes.Length);
            bw.Write(manifestBytes);

            var meshPayload = BuildMeshSection("mesh_0", 0, new byte[48], new uint[] { 0, 1, 2 });
            if (compressMeshSection)
            {
                if (!Xav2Lz4Codec.TryCompress(meshPayload, out var compressed, preferRatio: true))
                {
                    throw new InvalidOperationException("failed to build compressed test payload");
                }
                var expectedLength = meshPayload.Length + (corruptCompressedSection ? 13 : 0);
                var envelopeSize = truncateCompressedEnvelope ? 2 : (compressed.Length + 4);
                var envelope = new byte[envelopeSize];
                envelope[0] = (byte)(expectedLength & 0xFF);
                envelope[1] = (byte)((expectedLength >> 8) & 0xFF);
                if (!truncateCompressedEnvelope)
                {
                    envelope[2] = (byte)((expectedLength >> 16) & 0xFF);
                    envelope[3] = (byte)((expectedLength >> 24) & 0xFF);
                    Buffer.BlockCopy(compressed, 0, envelope, 4, compressed.Length);
                }
                WriteSection(bw, 0x0011, envelope, 0x0001);
            }
            else
            {
                WriteSection(bw, 0x0011, meshPayload);
            }
            WriteSection(bw, 0x0002, BuildTextureSection(textureRefName, new byte[] { 1, 2, 3, 4 }));
            WriteSection(
                bw,
                0x0003,
                legacyMaterialFormat
                    ? BuildMaterialSectionLegacy("material_0", "lilToon", textureRefName)
                    : BuildMaterialSectionV1("material_0", "lilToon", "default", textureRefName));
            WriteSection(bw, 0x0012, BuildMaterialParamsSection("material_0", "{}"));
            if (addTypedMaterialSection)
            {
                WriteSection(
                    bw,
                    0x0015,
                    BuildMaterialTypedParamsSection(
                        "material_0",
                        typedShaderFamilyOverride ?? "liltoon",
                        typedIncludeBaseColor,
                        typedIncludeAdvancedEntries,
                        typedSchemaVersion,
                        unresolvedTypedTextureRef
                            ? "texture_missing_typed"
                            : (typedBaseTextureRefOverride ?? textureRefName)));
            }
            if (addSkinSection)
            {
                WriteSection(bw, 0x0013, BuildSkinSection("mesh_0"));
                if (addSkeletonSection)
                {
                    WriteSection(bw, 0x0016, BuildSkeletonSection("mesh_0"));
                }
                if (addRigSection)
                {
                    WriteSection(bw, 0x0017, BuildRigSection("mesh_0", rigDuplicateBoneName, rigCycle));
                }
            }

            if (addUnknownSection)
            {
                WriteSection(bw, 0x77EE, new byte[] { 0xAA, 0xBB, 0xCC });
            }

            if (addPhysicsSections)
            {
                WriteSection(bw, 0x001A, BuildPhysicsColliderSection("col_0", "Root/Bone"));
                WriteSection(bw, 0x0018, BuildSpringBoneSection("sp_0", "Root/Bone", missingPhysicsColliderRef ? "missing_col" : "col_0"));
                WriteSection(bw, 0x0019, BuildPhysBoneSection("pb_0", "Root/Bone", missingPhysicsColliderRef ? "missing_col" : "col_0"));
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

        private static byte[] BuildMaterialTypedParamsSection(
            string name,
            string shaderFamily,
            bool includeBaseColor,
            bool includeAdvancedEntries,
            ushort schemaVersion,
            string baseTextureRef)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, name);
            WriteSizedString(bw, shaderFamily);
            bw.Write((uint)0x00000021); // cutout + shade
            if (schemaVersion >= 3)
            {
                bw.Write(schemaVersion);
            }

            var floatCount = (ushort)(includeAdvancedEntries ? 3 : 1);
            bw.Write(floatCount);
            WriteSizedString(bw, "_Cutoff");
            bw.Write(0.5f);
            if (includeAdvancedEntries)
            {
                WriteSizedString(bw, "_EmissionStrength");
                bw.Write(1.15f);
                WriteSizedString(bw, "_MatCapBlend");
                bw.Write(0.45f);
            }

            var colorCount = (ushort)((includeBaseColor ? 1 : 0) + (includeAdvancedEntries ? 1 : 0));
            bw.Write(colorCount);
            if (includeBaseColor)
            {
                WriteSizedString(bw, "_BaseColor");
                bw.Write(1.0f);
                bw.Write(1.0f);
                bw.Write(1.0f);
                bw.Write(1.0f);
            }
            if (includeAdvancedEntries)
            {
                WriteSizedString(bw, "_MatCapColor");
                bw.Write(0.7f);
                bw.Write(0.7f);
                bw.Write(0.85f);
                bw.Write(1.0f);
            }

            bw.Write((ushort)(includeAdvancedEntries ? 2 : 1));
            WriteSizedString(bw, "base");
            WriteSizedString(bw, baseTextureRef);
            if (includeAdvancedEntries)
            {
                WriteSizedString(bw, "matcap");
                WriteSizedString(bw, baseTextureRef);
            }

            return ms.ToArray();
        }

        private static byte[] BuildSkinSection(string meshName)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, meshName);
            bw.Write((uint)1);
            bw.Write(0);
            bw.Write((uint)16);
            for (var i = 0; i < 16; i++)
            {
                bw.Write((i % 5) == 0 ? 1.0f : 0.0f);
            }
            var skinWeightBlob = new byte[32];
            WriteFloat32(skinWeightBlob, 16, 1.0f);
            bw.Write((uint)skinWeightBlob.Length);
            bw.Write(skinWeightBlob);
            return ms.ToArray();
        }

        private static byte[] BuildSkeletonSection(string meshName)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, meshName);
            bw.Write((uint)16);
            for (var i = 0; i < 16; i++)
            {
                bw.Write((i % 5) == 0 ? 1.0f : 0.0f);
            }
            return ms.ToArray();
        }

        private static byte[] BuildRigSection(string meshName, bool duplicateBoneName, bool cycle)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, meshName);
            bw.Write((uint)2);
            WriteSizedString(bw, "Root");
            bw.Write(-1);
            bw.Write((uint)16);
            for (var i = 0; i < 16; i++)
            {
                bw.Write((i % 5) == 0 ? 1.0f : 0.0f);
            }
            WriteSizedString(bw, duplicateBoneName ? "Root" : "Spine");
            bw.Write(cycle ? 1 : 0);
            bw.Write((uint)16);
            for (var i = 0; i < 16; i++)
            {
                bw.Write((i % 5) == 0 ? 1.0f : 0.0f);
            }
            return ms.ToArray();
        }

        private static byte[] BuildPhysicsColliderSection(string name, string bonePath)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, name);
            WriteSizedString(bw, bonePath);
            bw.Write((byte)0);
            bw.Write(0.05f);
            bw.Write(0.0f);
            bw.Write(0.0f); bw.Write(0.0f); bw.Write(0.0f);
            bw.Write(0.0f); bw.Write(0.0f); bw.Write(1.0f);
            return ms.ToArray();
        }

        private static byte[] BuildSpringBoneSection(string name, string rootPath, string colliderRef)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, name);
            WriteSizedString(bw, rootPath);
            bw.Write((ushort)1);
            WriteSizedString(bw, rootPath);
            bw.Write(0.6f);
            bw.Write(0.3f);
            bw.Write(0.02f);
            bw.Write(0.0f); bw.Write(-1.0f); bw.Write(0.0f);
            bw.Write((ushort)1);
            WriteSizedString(bw, colliderRef);
            bw.Write((byte)1);
            return ms.ToArray();
        }

        private static byte[] BuildPhysBoneSection(string name, string rootPath, string colliderRef)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms, Encoding.UTF8, true);
            WriteSizedString(bw, name);
            WriteSizedString(bw, rootPath);
            bw.Write((ushort)1);
            WriteSizedString(bw, rootPath);
            bw.Write(0.4f);
            bw.Write(0.8f);
            bw.Write(0.1f);
            bw.Write(0.03f);
            bw.Write(0.0f); bw.Write(-1.0f); bw.Write(0.0f);
            bw.Write((ushort)1);
            WriteSizedString(bw, colliderRef);
            bw.Write((byte)1);
            return ms.ToArray();
        }

        private static void WriteFloat32(byte[] bytes, int offset, float value)
        {
            var raw = BitConverter.GetBytes(value);
            Array.Copy(raw, 0, bytes, offset, 4);
        }

        private static void WriteSection(BinaryWriter bw, ushort type, byte[] payload, ushort flags = 0)
        {
            bw.Write(type);
            bw.Write(flags);
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
