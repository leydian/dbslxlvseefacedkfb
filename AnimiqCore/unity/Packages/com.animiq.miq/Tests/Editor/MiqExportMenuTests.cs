using NUnit.Framework;

namespace Animiq.Miq.Editor.Tests
{
    public sealed class MiqExportMenuTests
    {
        [Test]
        public void CreateDefaultExportOptions_Strict_UsesCompressionDefaults()
        {
            var options = MiqExportMenu.CreateDefaultExportOptions(relaxed: false);

            Assert.That(options.FailOnMissingShader, Is.True);
            Assert.That(options.EnableCompression, Is.True);
            Assert.That(options.CompressionCodec, Is.EqualTo(MiqCompressionCodec.Lz4));
            Assert.That(options.CompressionLevel, Is.EqualTo(MiqCompressionLevel.Balanced));
        }

        [Test]
        public void CreateDefaultExportOptions_Relaxed_UsesCompressionDefaults()
        {
            var options = MiqExportMenu.CreateDefaultExportOptions(relaxed: true);

            Assert.That(options.FailOnMissingShader, Is.False);
            Assert.That(options.EnableCompression, Is.True);
            Assert.That(options.CompressionCodec, Is.EqualTo(MiqCompressionCodec.Lz4));
            Assert.That(options.CompressionLevel, Is.EqualTo(MiqCompressionLevel.Balanced));
        }
    }
}
