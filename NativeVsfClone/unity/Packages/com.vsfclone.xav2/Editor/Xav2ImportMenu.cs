using System.IO;
using System.Text;
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
            if (report.IsPartial)
            {
                sb.AppendLine("Result: partial import (see warnings)");
            }
            if (report.Warnings.Count > 0)
            {
                sb.AppendLine($"Warnings: {report.Warnings.Count}");
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
    }
}
