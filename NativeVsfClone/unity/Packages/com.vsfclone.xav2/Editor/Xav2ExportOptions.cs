using System;
using System.Collections.Generic;

namespace VsfClone.Xav2.Editor
{
    public enum Xav2CompressionCodec
    {
        Lz4 = 0
    }

    public enum Xav2CompressionLevel
    {
        Fast = 0,
        Balanced
    }

    [Serializable]
    public sealed class Xav2ExportOptions
    {
        public bool FailOnMissingShader = true;
        public List<string> StrictShaderSet = new List<string>()
        {
            "lilToon",
            "Poiyomi",
            "potatoon",
            "realtoon"
        };
        public bool EnableCompression;
        public Xav2CompressionCodec CompressionCodec = Xav2CompressionCodec.Lz4;
        public Xav2CompressionLevel CompressionLevel = Xav2CompressionLevel.Fast;
    }
}
