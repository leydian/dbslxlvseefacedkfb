using System;
using System.Collections.Generic;

namespace VsfClone.Xav2.Editor
{
    [Serializable]
    public sealed class Xav2ExportOptions
    {
        public bool FailOnMissingShader = true;
        public List<string> StrictShaderSet = new()
        {
            "lilToon",
            "Poiyomi",
            "potatoon",
            "realtoon"
        };
    }
}
