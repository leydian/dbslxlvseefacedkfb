using System;
using System.IO;
using UnityEditor;
using UnityEngine;
using Animiq.Miq.Editor;

namespace Animiq.Miq.Samples.Editor
{
    public static class MiqRoundtripSampleMenu
    {
        [MenuItem("Tools/Animiq/MIQ Samples/Export Selected To Temp And Reimport")]
        public static void ExportSelectedToTempAndReimport()
        {
            var selected = Selection.activeGameObject;
            if (selected == null)
            {
                EditorUtility.DisplayDialog("MIQ Sample", "Select an avatar root GameObject first.", "OK");
                return;
            }

            var tempPath = Path.Combine(Path.GetTempPath(), $"{selected.name}_{Guid.NewGuid():N}.miq");
            try
            {
                MiqExporter.Export(tempPath, selected, new MiqExportOptions
                {
                    EnableCompression = true,
                    CompressionCodec = MiqCompressionCodec.Lz4,
                    CompressionLevel = MiqCompressionLevel.Balanced
                });

                var importOptions = new MiqImportOptions
                {
                    OutputRoot = "Assets/ImportedMiqSample"
                };
                var report = MiqImporter.Import(tempPath, importOptions);
                if (!report.Success)
                {
                    EditorUtility.DisplayDialog("MIQ Sample", $"Import failed: {report.ErrorMessage}", "OK");
                    return;
                }

                AssetDatabase.Refresh();
                EditorGUIUtility.PingObject(AssetDatabase.LoadAssetAtPath<UnityEngine.Object>(report.PrefabPath));
                EditorUtility.DisplayDialog("MIQ Sample", $"Roundtrip success:\n{report.PrefabPath}", "OK");
            }
            catch (Exception ex)
            {
                EditorUtility.DisplayDialog("MIQ Sample", $"Roundtrip failed: {ex.Message}", "OK");
                throw;
            }
            finally
            {
                if (File.Exists(tempPath))
                {
                    File.Delete(tempPath);
                }
            }
        }
    }
}
