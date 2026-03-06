using System.IO;
using UnityEditor;
using UnityEngine;

namespace Animiq.Miq.Editor
{
    public static class MiqExportMenu
    {
        [MenuItem("Tools/Animiq/MIQ/Export Selected AvatarRoot", priority = 10)]
        public static void ExportSelectedAvatarRoot()
        {
            ExportSelectedAvatarRootInternal(relaxed: true);
        }

        [MenuItem("Tools/Animiq/MIQ/Export Selected AvatarRoot (Strict)", priority = 11)]
        public static void ExportSelectedAvatarRootStrict()
        {
            ExportSelectedAvatarRootInternal(relaxed: false);
        }

        [MenuItem("Tools/Animiq/MIQ/Export Selected AvatarRoot", validate = true)]
        public static bool ValidateExportSelectedAvatarRoot()
        {
            return Selection.activeGameObject != null;
        }

        [MenuItem("Tools/Animiq/MIQ/Export Selected AvatarRoot (Strict)", validate = true)]
        public static bool ValidateExportSelectedAvatarRootStrict()
        {
            return Selection.activeGameObject != null;
        }

        private static void ExportSelectedAvatarRootInternal(bool relaxed)
        {
            var selected = Selection.activeGameObject;
            if (selected == null)
            {
                EditorUtility.DisplayDialog("MIQ Export", "Select an AvatarRoot GameObject in the scene.", "OK");
                return;
            }

            var defaultName = selected.name + ".miq";
            var output = EditorUtility.SaveFilePanel("Export MIQ", Application.dataPath, defaultName, "miq");
            if (string.IsNullOrWhiteSpace(output))
            {
                return;
            }

            var modeLabel = relaxed ? "relaxed" : "strict";
            try
            {
                var options = CreateDefaultExportOptions(relaxed);
                MiqExporter.Export(output, selected, options);
                var compressionLabel = options.EnableCompression
                    ? $"{options.CompressionCodec.ToString().ToLowerInvariant()}/{options.CompressionLevel.ToString().ToLowerInvariant()}"
                    : "off";
                Debug.Log($"[MIQ] Export complete ({modeLabel}, compression:{compressionLabel}): {Path.GetFullPath(output)}");
            }
            catch (System.Exception ex)
            {
                Debug.LogError($"[MIQ] Export failed ({modeLabel}): {ex.Message}");
                EditorUtility.DisplayDialog("MIQ Export Failed", $"[{modeLabel}] {ex.Message}", "OK");
            }
        }

        internal static MiqExportOptions CreateDefaultExportOptions(bool relaxed)
        {
            return new MiqExportOptions
            {
                FailOnMissingShader = !relaxed,
                EnableCompression = true,
                CompressionCodec = MiqCompressionCodec.Lz4,
                CompressionLevel = MiqCompressionLevel.Balanced
            };
        }
    }
}
