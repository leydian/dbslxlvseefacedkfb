using System;
using System.IO;
using UnityEditor;
using UnityEngine;
using VsfClone.Xav2.Editor;

namespace VsfClone.Xav2.Samples.Editor
{
    public static class Xav2RoundtripSampleMenu
    {
        [MenuItem("Tools/VsfClone/XAV2 Samples/Export Selected To Temp And Reimport")]
        public static void ExportSelectedToTempAndReimport()
        {
            var selected = Selection.activeGameObject;
            if (selected == null)
            {
                EditorUtility.DisplayDialog("XAV2 Sample", "Select an avatar root GameObject first.", "OK");
                return;
            }

            var tempPath = Path.Combine(Path.GetTempPath(), $"{selected.name}_{Guid.NewGuid():N}.xav2");
            try
            {
                Xav2Exporter.Export(tempPath, selected, new Xav2ExportOptions
                {
                    EnableCompression = true,
                    CompressionCodec = Xav2CompressionCodec.Lz4,
                    CompressionLevel = Xav2CompressionLevel.Balanced
                });

                var importOptions = new Xav2ImportOptions
                {
                    OutputRoot = "Assets/ImportedXav2Sample"
                };
                var report = Xav2Importer.Import(tempPath, importOptions);
                if (!report.Success)
                {
                    EditorUtility.DisplayDialog("XAV2 Sample", $"Import failed: {report.ErrorMessage}", "OK");
                    return;
                }

                AssetDatabase.Refresh();
                EditorGUIUtility.PingObject(AssetDatabase.LoadAssetAtPath<UnityEngine.Object>(report.PrefabPath));
                EditorUtility.DisplayDialog("XAV2 Sample", $"Roundtrip success:\n{report.PrefabPath}", "OK");
            }
            catch (Exception ex)
            {
                EditorUtility.DisplayDialog("XAV2 Sample", $"Roundtrip failed: {ex.Message}", "OK");
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
