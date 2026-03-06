using System.Collections.Generic;

namespace Animiq.Miq.Editor
{
    public enum MiqImportCollisionPolicy
    {
        Suffix = 0
    }

    public enum MiqMaterialRecoveryProfile
    {
        Standard = 0,
        Aggressive
    }

    public enum MiqRigRecoveryPolicy
    {
        Strict = 0,
        Fallback
    }

    public sealed class MiqRigDiagnostic
    {
        public string Code = string.Empty;
        public string MeshName = string.Empty;
        public string BoneName = string.Empty;
        public string Message = string.Empty;
    }

    public sealed class MiqImportOptions
    {
        public string OutputRoot = "Assets/ImportedMiq";
        public MiqImportCollisionPolicy CollisionPolicy = MiqImportCollisionPolicy.Suffix;
        public bool StrictValidation;
        public bool FailOnRigDataMissing = true;
        public MiqMaterialRecoveryProfile MaterialRecoveryProfile = MiqMaterialRecoveryProfile.Standard;
        public MiqRigRecoveryPolicy RigRecoveryPolicy = MiqRigRecoveryPolicy.Strict;
    }

    public sealed class MiqImportReport
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
        public List<MiqRigDiagnostic> RigDiagnostics = new List<MiqRigDiagnostic>();
        public string RigQuality = "None";
        public string ErrorMessage = string.Empty;
    }
}
