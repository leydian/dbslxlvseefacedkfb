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
            ExportSelectedAvatarRootInternal(relaxed: true);
        }

        [MenuItem("Tools/VsfClone/XAV2/Export Selected AvatarRoot (Strict)", priority = 11)]
        public static void ExportSelectedAvatarRootStrict()
        {
            ExportSelectedAvatarRootInternal(relaxed: false);
        }

        [MenuItem("Tools/VsfClone/XAV2/Export Selected AvatarRoot", validate = true)]
        public static bool ValidateExportSelectedAvatarRoot()
        {
            return Selection.activeGameObject != null;
        }

        [MenuItem("Tools/VsfClone/XAV2/Export Selected AvatarRoot (Strict)", validate = true)]
        public static bool ValidateExportSelectedAvatarRootStrict()
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
                var options = CreateDefaultExportOptions(relaxed);
                Xav2Exporter.Export(output, selected, options);
                var compressionLabel = options.EnableCompression
                    ? $"{options.CompressionCodec.ToString().ToLowerInvariant()}/{options.CompressionLevel.ToString().ToLowerInvariant()}"
                    : "off";
                Debug.Log($"[XAV2] Export complete ({modeLabel}, compression:{compressionLabel}): {Path.GetFullPath(output)}");
            }
            catch (System.Exception ex)
            {
                Debug.LogError($"[XAV2] Export failed ({modeLabel}): {ex.Message}");
                EditorUtility.DisplayDialog("XAV2 Export Failed", $"[{modeLabel}] {ex.Message}", "OK");
            }
        }

        internal static Xav2ExportOptions CreateDefaultExportOptions(bool relaxed)
        {
            return new Xav2ExportOptions
            {
                FailOnMissingShader = !relaxed,
                EnableCompression = true,
                CompressionCodec = Xav2CompressionCodec.Lz4,
                CompressionLevel = Xav2CompressionLevel.Balanced
            };
        }
    }
}
