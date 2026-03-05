using System.IO;
using UnityEditor;
using UnityEngine;

namespace VsfClone.Xav2.Editor
{
    public static class Xav2ExportMenu
    {
        [MenuItem("Tools/VsfClone/XAV2/Export Selected AvatarRoot", priority = 10)]
        public static void ExportSelectedAvatarRoot()
        {
            ExportSelectedAvatarRootInternal(relaxed: false);
        }

        [MenuItem("Tools/VsfClone/XAV2/Export Selected AvatarRoot (Relaxed)", priority = 11)]
        public static void ExportSelectedAvatarRootRelaxed()
        {
            ExportSelectedAvatarRootInternal(relaxed: true);
        }

        [MenuItem("Tools/VsfClone/XAV2/Export Selected AvatarRoot", validate = true)]
        public static bool ValidateExportSelectedAvatarRoot()
        {
            return Selection.activeGameObject != null;
        }

        [MenuItem("Tools/VsfClone/XAV2/Export Selected AvatarRoot (Relaxed)", validate = true)]
        public static bool ValidateExportSelectedAvatarRootRelaxed()
        {
            return Selection.activeGameObject != null;
        }

        private static void ExportSelectedAvatarRootInternal(bool relaxed)
        {
            var selected = Selection.activeGameObject;
            if (selected == null)
            {
                EditorUtility.DisplayDialog("XAV2 Export", "Select an AvatarRoot GameObject in the scene.", "OK");
                return;
            }

            var defaultName = selected.name + ".xav2";
            var output = EditorUtility.SaveFilePanel("Export XAV2", Application.dataPath, defaultName, "xav2");
            if (string.IsNullOrWhiteSpace(output))
            {
                return;
            }

            var modeLabel = relaxed ? "relaxed" : "strict";
            try
            {
                var options = new Xav2ExportOptions
                {
                    FailOnMissingShader = !relaxed
                };
                Xav2Exporter.Export(output, selected, options);
                Debug.Log($"[XAV2] Export complete ({modeLabel}): {Path.GetFullPath(output)}");
            }
            catch (System.Exception ex)
            {
                Debug.LogError($"[XAV2] Export failed ({modeLabel}): {ex.Message}");
                EditorUtility.DisplayDialog("XAV2 Export Failed", $"[{modeLabel}] {ex.Message}", "OK");
            }
        }
    }
}
