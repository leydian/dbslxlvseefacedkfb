using System.IO;
using System.Text;
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;

namespace VsfClone.Xav2.Editor
{
    public static class Xav2ImportMenu
    {
        [MenuItem("Tools/VsfClone/XAV2/Import XAV2...", priority = 20)]
        public static void ImportXav2()
        {
            var inputPath = EditorUtility.OpenFilePanel("Import XAV2", Application.dataPath, "xav2");
            if (string.IsNullOrWhiteSpace(inputPath))
            {
                return;
            }

            var report = Xav2Importer.Import(inputPath, new Xav2ImportOptions());
            if (!report.Success)
            {
                var error = string.IsNullOrWhiteSpace(report.ErrorMessage)
                    ? "Unknown import error."
                    : report.ErrorMessage;
                Debug.LogError($"[XAV2] Import failed: {error}");
                EditorUtility.DisplayDialog("XAV2 Import Failed", error, "OK");
                return;
            }

            var sb = new StringBuilder();
            sb.AppendLine($"Prefab: {report.PrefabPath}");
            sb.AppendLine($"Assets created: {report.CreatedAssets.Count}");
            sb.AppendLine($"Rig quality: {report.RigQuality}");
            if (report.IsPartial)
            {
                sb.AppendLine("Result: partial import (see warnings)");
            }
            if (report.Warnings.Count > 0)
            {
                sb.AppendLine($"Warnings: {report.Warnings.Count}");
            }
            if (report.RecoverableErrors.Count > 0)
            {
                sb.AppendLine($"Recoverable errors: {report.RecoverableErrors.Count}");
            }
            if (report.RigDiagnostics.Count > 0)
            {
                sb.AppendLine($"Rig diagnostics: {report.RigDiagnostics.Count}");
            }

            var message = sb.ToString().TrimEnd();
            Debug.Log($"[XAV2] Import completed.\n{message}");

            if (report.Warnings.Count > 0)
            {
                var warnings = string.Join("\n", report.Warnings);
                EditorUtility.DisplayDialog("XAV2 Import Completed with Warnings", $"{message}\n\n{warnings}", "OK");
            }
            else
            {
                EditorUtility.DisplayDialog("XAV2 Import Completed", message, "OK");
            }

            var prefab = AssetDatabase.LoadAssetAtPath<GameObject>(report.PrefabPath);
            if (prefab != null)
            {
                Selection.activeObject = prefab;
                EditorGUIUtility.PingObject(prefab);
            }
        }

        [MenuItem("Tools/VsfClone/XAV2/Diagnose Rig (Strict/Fallback)...", priority = 21)]
        public static void DiagnoseRigStrictVsFallback()
        {
            var inputPath = EditorUtility.OpenFilePanel("Diagnose XAV2 Rig", Application.dataPath, "xav2");
            if (string.IsNullOrWhiteSpace(inputPath))
            {
                return;
            }

            const string strictRoot = "Assets/ImportedXav2DiagStrict";
            const string fallbackRoot = "Assets/ImportedXav2DiagFallback";

            var strictOptions = new Xav2ImportOptions
            {
                OutputRoot = strictRoot,
                FailOnRigDataMissing = true,
                RigRecoveryPolicy = Xav2RigRecoveryPolicy.Strict
            };
            var fallbackOptions = new Xav2ImportOptions
            {
                OutputRoot = fallbackRoot,
                FailOnRigDataMissing = false,
                RigRecoveryPolicy = Xav2RigRecoveryPolicy.Fallback
            };

            Xav2ImportReport strictReport = null;
            Xav2ImportReport fallbackReport = null;
            try
            {
                EditorUtility.DisplayProgressBar("XAV2 Rig Diagnosis", "Running strict import...", 0.15f);
                strictReport = Xav2Importer.Import(inputPath, strictOptions);

                if (EditorUtility.DisplayCancelableProgressBar("XAV2 Rig Diagnosis", "Running fallback import...", 0.65f))
                {
                    EditorUtility.DisplayDialog("XAV2 Rig Diagnosis", "Diagnosis cancelled by user.", "OK");
                    return;
                }

                fallbackReport = Xav2Importer.Import(inputPath, fallbackOptions);
            }
            catch (System.Exception ex)
            {
                strictReport ??= new Xav2ImportReport
                {
                    Success = false,
                    ErrorMessage = $"Strict diagnosis threw exception: {ex.Message}"
                };
                fallbackReport ??= new Xav2ImportReport
                {
                    Success = false,
                    ErrorMessage = $"Fallback diagnosis not completed: {ex.Message}"
                };
            }
            finally
            {
                EditorUtility.ClearProgressBar();
            }

            var summary = BuildRigDiagnosticSummary(inputPath, strictReport, fallbackReport);
            Debug.Log($"[XAV2] Rig diagnosis completed.\n{summary}");
            EditorUtility.DisplayDialog("XAV2 Rig Diagnosis", summary, "OK");
        }

        private static string BuildRigDiagnosticSummary(
            string inputPath,
            Xav2ImportReport strictReport,
            Xav2ImportReport fallbackReport)
        {
            var sb = new StringBuilder();
            sb.AppendLine($"Source: {Path.GetFileName(inputPath)}");
            AppendPolicySummary(sb, "Strict", strictReport);
            AppendPolicySummary(sb, "Fallback", fallbackReport);
            sb.AppendLine("Decision guide:");
            if (!strictReport.Success && fallbackReport.Success)
            {
                sb.AppendLine("- Strict failed and fallback succeeded: fix rig source data, then re-export.");
            }
            else if (strictReport.Success && strictReport.RigDiagnostics.Count > 0)
            {
                sb.AppendLine("- Strict succeeded with rig diagnostics: investigate rig warnings before shipping.");
            }
            else if (strictReport.Success && strictReport.RigDiagnostics.Count == 0)
            {
                sb.AppendLine("- Strict succeeded without rig diagnostics: rig data is likely healthy.");
            }
            else
            {
                sb.AppendLine("- Both paths failed: inspect warning/error codes and source file integrity first.");
            }

            return sb.ToString().TrimEnd();
        }

        private static void AppendPolicySummary(StringBuilder sb, string policyName, Xav2ImportReport report)
        {
            sb.AppendLine($"{policyName}: success={report.Success}, partial={report.IsPartial}, rigQuality={report.RigQuality}");
            if (!report.Success && !string.IsNullOrWhiteSpace(report.ErrorMessage))
            {
                sb.AppendLine($"  error: {report.ErrorMessage}");
            }

            sb.AppendLine($"  warnings={report.Warnings.Count}, recoverable={report.RecoverableErrors.Count}, rigDiagnostics={report.RigDiagnostics.Count}");
            var rigCodes = CollectRigCodes(report);
            sb.AppendLine($"  rigCodes={(rigCodes.Count > 0 ? string.Join(", ", rigCodes) : "none")}");
        }

        private static List<string> CollectRigCodes(Xav2ImportReport report)
        {
            var result = new List<string>();
            var seen = new HashSet<string>();

            for (var i = 0; i < report.WarningCodes.Count; i++)
            {
                var code = report.WarningCodes[i];
                if (!string.IsNullOrWhiteSpace(code) && code.StartsWith("XAV4_RIG_") && seen.Add(code))
                {
                    result.Add(code);
                }
            }

            for (var i = 0; i < report.Warnings.Count; i++)
            {
                var warning = report.Warnings[i];
                var marker = "XAV4_RIG_";
                var idx = warning.IndexOf(marker, System.StringComparison.Ordinal);
                if (idx < 0)
                {
                    continue;
                }

                var end = warning.IndexOf(':', idx);
                var code = end > idx ? warning.Substring(idx, end - idx) : warning.Substring(idx);
                if (seen.Add(code))
                {
                    result.Add(code);
                }
            }

            for (var i = 0; i < report.RigDiagnostics.Count; i++)
            {
                var code = report.RigDiagnostics[i].Code;
                if (!string.IsNullOrWhiteSpace(code) && seen.Add(code))
                {
                    result.Add(code);
                }
            }

            return result;
        }
    }
}
