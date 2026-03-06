using System;
using System.Collections.Generic;

namespace Animiq.Miq.Editor
{
    public enum MiqCompressionCodec
    {
        Lz4 = 0
    }

    public enum MiqCompressionLevel
    {
        Fast = 0,
        Balanced
    }

    [Serializable]
    public sealed class MiqExportOptions
    {
        public bool FailOnMissingShader = true;
        public List<string> StrictShaderSet = new List<string>()
        {
            "Standard",
            "MToon",
            "lilToon",
            "Poiyomi"
        };
        public bool EnableCompression;
        public MiqCompressionCodec CompressionCodec = MiqCompressionCodec.Lz4;
        public MiqCompressionLevel CompressionLevel = MiqCompressionLevel.Fast;
    }
}
