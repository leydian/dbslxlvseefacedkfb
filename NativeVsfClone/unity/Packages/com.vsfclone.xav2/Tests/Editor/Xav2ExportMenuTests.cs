using NUnit.Framework;

namespace VsfClone.Xav2.Editor.Tests
{
    public sealed class Xav2ExportMenuTests
    {
        [Test]
        public void CreateDefaultExportOptions_Strict_UsesCompressionDefaults()
        {
            var options = Xav2ExportMenu.CreateDefaultExportOptions(relaxed: false);

            Assert.That(options.FailOnMissingShader, Is.True);
            Assert.That(options.EnableCompression, Is.True);
            Assert.That(options.CompressionCodec, Is.EqualTo(Xav2CompressionCodec.Lz4));
            Assert.That(options.CompressionLevel, Is.EqualTo(Xav2CompressionLevel.Balanced));
        }

        [Test]
        public void CreateDefaultExportOptions_Relaxed_UsesCompressionDefaults()
        {
            var options = Xav2ExportMenu.CreateDefaultExportOptions(relaxed: true);

            Assert.That(options.FailOnMissingShader, Is.False);
            Assert.That(options.EnableCompression, Is.True);
            Assert.That(options.CompressionCodec, Is.EqualTo(Xav2CompressionCodec.Lz4));
            Assert.That(options.CompressionLevel, Is.EqualTo(Xav2CompressionLevel.Balanced));
        }
    }
}
