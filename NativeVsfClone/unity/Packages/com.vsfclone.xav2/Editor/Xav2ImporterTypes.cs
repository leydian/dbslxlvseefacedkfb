using System.Collections.Generic;

namespace VsfClone.Xav2.Editor
{
    public enum Xav2ImportCollisionPolicy
    {
        Suffix = 0
    }

    public enum Xav2MaterialRecoveryProfile
    {
        Standard = 0,
        Aggressive
    }

    public enum Xav2RigRecoveryPolicy
    {
        Strict = 0,
        Fallback
    }

    public sealed class Xav2RigDiagnostic
    {
        public string Code = string.Empty;
        public string MeshName = string.Empty;
        public string BoneName = string.Empty;
        public string Message = string.Empty;
    }

    public sealed class Xav2ImportOptions
    {
        public string OutputRoot = "Assets/ImportedXav2";
        public Xav2ImportCollisionPolicy CollisionPolicy = Xav2ImportCollisionPolicy.Suffix;
        public bool StrictValidation;
        public bool FailOnRigDataMissing = true;
        public Xav2MaterialRecoveryProfile MaterialRecoveryProfile = Xav2MaterialRecoveryProfile.Standard;
        public Xav2RigRecoveryPolicy RigRecoveryPolicy = Xav2RigRecoveryPolicy.Strict;
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
        public List<string> RecoverableErrors = new List<string>();
        public List<string> SkippedAssets = new List<string>();
        public List<Xav2RigDiagnostic> RigDiagnostics = new List<Xav2RigDiagnostic>();
        public string RigQuality = "None";
        public string ErrorMessage = string.Empty;
    }
}
