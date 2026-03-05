using System.Collections.Generic;

namespace VsfClone.Xav2.Editor
{
    public enum Xav2ImportCollisionPolicy
    {
        Suffix = 0
    }

    public sealed class Xav2ImportOptions
    {
        public string OutputRoot = "Assets/ImportedXav2";
        public Xav2ImportCollisionPolicy CollisionPolicy = Xav2ImportCollisionPolicy.Suffix;
        public bool StrictValidation;
    }

    public sealed class Xav2ImportReport
    {
        public bool Success;
        public bool IsPartial;
        public string SourcePath = string.Empty;
        public string OutputDirectory = string.Empty;
        public string PrefabPath = string.Empty;
        public List<string> CreatedAssets = new List<string>();
        public List<string> Warnings = new List<string>();
        public List<string> WarningCodes = new List<string>();
        public string ErrorMessage = string.Empty;
    }
}
