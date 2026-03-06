using System.IO;
using System.Text;
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;

namespace Animiq.Miq.Editor
{
    public static class MiqImportMenu
    {
        [MenuItem("Tools/Animiq/MIQ/Import MIQ...", priority = 20)]
        public static void ImportMiq()
        {
            var inputPath = EditorUtility.OpenFilePanel("Import MIQ", Application.dataPath, "miq");
            if (string.IsNullOrWhiteSpace(inputPath))
            {
                return;
            }

            var report = MiqImporter.Import(inputPath, new MiqImportOptions());
            if (!report.Success)
            {
                var error = string.IsNullOrWhiteSpace(report.ErrorMessage)
                    ? "Unknown import error."
                    : report.ErrorMessage;
                Debug.LogError($"[MIQ] Import failed: {error}");
                EditorUtility.DisplayDialog("MIQ Import Failed", error, "OK");
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
            Debug.Log($"[MIQ] Import completed.\n{message}");

            if (report.Warnings.Count > 0)
            {
                var warnings = string.Join("\n", report.Warnings);
                EditorUtility.DisplayDialog("MIQ Import Completed with Warnings", $"{message}\n\n{warnings}", "OK");
            }
            else
            {
                EditorUtility.DisplayDialog("MIQ Import Completed", message, "OK");
            }

            var prefab = AssetDatabase.LoadAssetAtPath<GameObject>(report.PrefabPath);
            if (prefab != null)
            {
                Selection.activeObject = prefab;
                EditorGUIUtility.PingObject(prefab);
            }
        }

        [MenuItem("Tools/Animiq/MIQ/Diagnose Rig (Strict/Fallback)...", priority = 21)]
        public static void DiagnoseRigStrictVsFallback()
        {
            var inputPath = EditorUtility.OpenFilePanel("Diagnose MIQ Rig", Application.dataPath, "miq");
            if (string.IsNullOrWhiteSpace(inputPath))
            {
                return;
            }

            const string strictRoot = "Assets/ImportedMiqDiagStrict";
            const string fallbackRoot = "Assets/ImportedMiqDiagFallback";

            var strictOptions = new MiqImportOptions
            {
                OutputRoot = strictRoot,
                FailOnRigDataMissing = true,
                RigRecoveryPolicy = MiqRigRecoveryPolicy.Strict
            };
            var fallbackOptions = new MiqImportOptions
            {
                OutputRoot = fallbackRoot,
                FailOnRigDataMissing = false,
                RigRecoveryPolicy = MiqRigRecoveryPolicy.Fallback
            };

            MiqImportReport strictReport = null;
            MiqImportReport fallbackReport = null;
            try
            {
                EditorUtility.DisplayProgressBar("MIQ Rig Diagnosis", "Running strict import...", 0.15f);
                strictReport = MiqImporter.Import(inputPath, strictOptions);

                if (EditorUtility.DisplayCancelableProgressBar("MIQ Rig Diagnosis", "Running fallback import...", 0.65f))
                {
                    EditorUtility.DisplayDialog("MIQ Rig Diagnosis", "Diagnosis cancelled by user.", "OK");
                    return;
                }

                fallbackReport = MiqImporter.Import(inputPath, fallbackOptions);
            }
            catch (System.Exception ex)
            {
                strictReport ??= new MiqImportReport
                {
                    Success = false,
                    ErrorMessage = $"Strict diagnosis threw exception: {ex.Message}"
                };
                fallbackReport ??= new MiqImportReport
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
            Debug.Log($"[MIQ] Rig diagnosis completed.\n{summary}");
            EditorUtility.DisplayDialog("MIQ Rig Diagnosis", summary, "OK");
        }

        private static string BuildRigDiagnosticSummary(
            string inputPath,
            MiqImportReport strictReport,
            MiqImportReport fallbackReport)
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

        private static void AppendPolicySummary(StringBuilder sb, string policyName, MiqImportReport report)
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

        private static List<string> CollectRigCodes(MiqImportReport report)
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
