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
            var selected = Selection.activeGameObject;
            if (selected == null)
            {
                EditorUtility.DisplayDialog("XAV2 Export", "씬에서 AvatarRoot(GameObject)를 선택하세요.", "확인");
                return;
            }
            var defaultName = selected.name + ".xav2";
            var output = EditorUtility.SaveFilePanel("Export XAV2", Application.dataPath, defaultName, "xav2");
            if (string.IsNullOrWhiteSpace(output))
            {
                return;
            }
            try
            {
                var options = new Xav2ExportOptions();
                Xav2Exporter.Export(output, selected, options);
                Debug.Log($"[XAV2] Export complete: {Path.GetFullPath(output)}");
            }
            catch (System.Exception ex)
            {
                Debug.LogError($"[XAV2] Export failed: {ex.Message}");
                EditorUtility.DisplayDialog("XAV2 Export Failed", ex.Message, "확인");
            }
        }

        [MenuItem("Tools/VsfClone/XAV2/Export Selected AvatarRoot", validate = true)]
        public static bool ValidateExportSelectedAvatarRoot()
        {
            return Selection.activeGameObject != null;
        }
    }
}
