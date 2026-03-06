using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEngine;
using Animiq.Miq.Runtime;

namespace Animiq.Miq.Editor
{
    public static class MiqCiQuality
    {
        [Serializable]
        private sealed class CompressionReport
        {
            public string unity_version = string.Empty;
            public string uncompressed_path = string.Empty;
            public string compressed_path = string.Empty;
            public long uncompressed_bytes;
            public long compressed_bytes;
            public double size_reduction_pct;
            public int iterations;
            public double uncompressed_load_ms_avg;
            public double compressed_load_ms_avg;
            public double load_delta_pct;
            public int decode_failures;
            public string error_message = string.Empty;
            public string overall_status = "FAIL";
        }

        [Serializable]
        private sealed class ParityEntry
        {
            public string path = string.Empty;
            public string name = string.Empty;
            public bool ok;
            public string error_code = string.Empty;
            public string parser_stage = string.Empty;
            public int warning_code_count;
            public int mesh_count;
            public int material_count;
            public int texture_count;
            public bool is_partial;
        }

        [Serializable]
        private sealed class ParityReport
        {
            public string unity_version = string.Empty;
            public List<ParityEntry> entries = new List<ParityEntry>();
            public string error_message = string.Empty;
            public string overall_status = "FAIL";
        }

        public static void RunCompressionGate()
        {
            var report = new CompressionReport();
            var reportPath = string.Empty;
            try
            {
                report.unity_version = Application.unityVersion;
                var args = Environment.GetCommandLineArgs();
                reportPath = GetArgValue(args, "-miqQualityReportPath");
                var outputDir = GetArgValue(args, "-miqQualityOutputDir");
                var expectedUnityVersion = GetArgValue(args, "-miqExpectedUnityVersion");
                var iterations = ParseIntOrDefault(GetArgValue(args, "-miqCompressionIterations"), 10, 1, 200);

                if (!string.IsNullOrWhiteSpace(expectedUnityVersion) &&
                    !string.Equals(Application.unityVersion, expectedUnityVersion, StringComparison.OrdinalIgnoreCase))
                {
                    throw new InvalidOperationException(
                        "Unity version mismatch. expected='" + expectedUnityVersion + "', actual='" + Application.unityVersion + "'");
                }

                if (string.IsNullOrWhiteSpace(outputDir))
                {
                    outputDir = Path.Combine(Path.GetTempPath(), "miq-ci-quality");
                }
                Directory.CreateDirectory(outputDir);

                var payload = BuildCompressiblePayload();
                var uncompressedPath = Path.Combine(outputDir, "miq_quality_uncompressed.miq");
                var compressedPath = Path.Combine(outputDir, "miq_quality_compressed.miq");

                MiqExporter.Export(uncompressedPath, payload, new MiqExportOptions
                {
                    EnableCompression = false
                });
                MiqExporter.Export(compressedPath, payload, new MiqExportOptions
                {
                    EnableCompression = true,
                    CompressionCodec = MiqCompressionCodec.Lz4,
                    CompressionLevel = MiqCompressionLevel.Balanced
                });

                report.uncompressed_path = uncompressedPath;
                report.compressed_path = compressedPath;
                report.uncompressed_bytes = new FileInfo(uncompressedPath).Length;
                report.compressed_bytes = new FileInfo(compressedPath).Length;
                report.size_reduction_pct = report.uncompressed_bytes <= 0
                    ? 0.0
                    : ((double)(report.uncompressed_bytes - report.compressed_bytes) / report.uncompressed_bytes) * 100.0;
                report.iterations = iterations;

                WarmUpLoad(uncompressedPath);
                WarmUpLoad(compressedPath);
                report.uncompressed_load_ms_avg = MeasureLoadMeanMs(uncompressedPath, iterations, ref report.decode_failures);
                report.compressed_load_ms_avg = MeasureLoadMeanMs(compressedPath, iterations, ref report.decode_failures);
                report.load_delta_pct = report.uncompressed_load_ms_avg <= 0.0
                    ? 0.0
                    : ((report.compressed_load_ms_avg - report.uncompressed_load_ms_avg) / report.uncompressed_load_ms_avg) * 100.0;

                report.overall_status = report.decode_failures == 0 ? "PASS" : "FAIL";
            }
            catch (Exception ex)
            {
                report.error_message = ex.Message ?? "unknown error";
                report.overall_status = "FAIL";
            }
            finally
            {
                WriteReport(reportPath, report);
                EditorApplication.Exit(string.Equals(report.overall_status, "PASS", StringComparison.Ordinal) ? 0 : 1);
            }
        }

        public static void RunParityProbe()
        {
            var report = new ParityReport();
            var reportPath = string.Empty;
            try
            {
                report.unity_version = Application.unityVersion;
                var args = Environment.GetCommandLineArgs();
                reportPath = GetArgValue(args, "-miqParityReportPath");
                var sampleDir = GetArgValue(args, "-miqParitySampleDir");
                var outputDir = GetArgValue(args, "-miqParityOutputDir");
                var expectedUnityVersion = GetArgValue(args, "-miqExpectedUnityVersion");
                var maxExternalSamples = ParseIntOrDefault(GetArgValue(args, "-miqParityMaxExternalSamples"), 5, 0, 50);

                if (!string.IsNullOrWhiteSpace(expectedUnityVersion) &&
                    !string.Equals(Application.unityVersion, expectedUnityVersion, StringComparison.OrdinalIgnoreCase))
                {
                    throw new InvalidOperationException(
                        "Unity version mismatch. expected='" + expectedUnityVersion + "', actual='" + Application.unityVersion + "'");
                }

                if (string.IsNullOrWhiteSpace(outputDir))
                {
                    outputDir = Path.Combine(Path.GetTempPath(), "miq-ci-parity");
                }
                Directory.CreateDirectory(outputDir);

                var samplePaths = new List<string>();
                var payload = BuildCompressiblePayload();
                var generatedUncompressed = Path.Combine(outputDir, "parity_uncompressed.miq");
                var generatedCompressed = Path.Combine(outputDir, "parity_compressed.miq");
                MiqExporter.Export(generatedUncompressed, payload, new MiqExportOptions
                {
                    EnableCompression = false
                });
                MiqExporter.Export(generatedCompressed, payload, new MiqExportOptions
                {
                    EnableCompression = true,
                    CompressionCodec = MiqCompressionCodec.Lz4,
                    CompressionLevel = MiqCompressionLevel.Balanced
                });
                samplePaths.Add(generatedUncompressed);
                samplePaths.Add(generatedCompressed);

                if (!string.IsNullOrWhiteSpace(sampleDir) && Directory.Exists(sampleDir) && maxExternalSamples > 0)
                {
                    var external = Directory.GetFiles(sampleDir, "*.miq", SearchOption.TopDirectoryOnly);
                    Array.Sort(external, StringComparer.OrdinalIgnoreCase);
                    for (var i = 0; i < external.Length && i < maxExternalSamples; i++)
                    {
                        if (!samplePaths.Any(p => string.Equals(p, external[i], StringComparison.OrdinalIgnoreCase)))
                        {
                            samplePaths.Add(external[i]);
                        }
                    }
                }

                foreach (var samplePath in samplePaths)
                {
                    var entry = new ParityEntry
                    {
                        path = samplePath,
                        name = Path.GetFileName(samplePath)
                    };

                    var ok = MiqRuntimeLoader.TryLoad(
                        samplePath,
                        out var payloadResult,
                        out var diagnostics,
                        new MiqLoadOptions
                        {
                            ShaderPolicy = MiqShaderPolicy.Fail
                        });
                    entry.ok = ok;
                    entry.error_code = diagnostics.ErrorCode.ToString();
                    entry.parser_stage = diagnostics.ParserStage ?? string.Empty;
                    entry.warning_code_count = diagnostics.WarningCodes?.Count ?? 0;
                    entry.mesh_count = payloadResult?.Meshes?.Count ?? 0;
                    entry.material_count = payloadResult?.Materials?.Count ?? 0;
                    entry.texture_count = payloadResult?.Textures?.Count ?? 0;
                    entry.is_partial = diagnostics.IsPartial;
                    report.entries.Add(entry);
                }

                var allOk = true;
                foreach (var entry in report.entries)
                {
                    if (!entry.ok || !string.Equals(entry.error_code, MiqLoadErrorCode.None.ToString(), StringComparison.Ordinal))
                    {
                        allOk = false;
                        break;
                    }
                }
                report.overall_status = allOk ? "PASS" : "FAIL";
            }
            catch (Exception ex)
            {
                report.error_message = ex.Message ?? "unknown error";
                report.overall_status = "FAIL";
            }
            finally
            {
                WriteReport(reportPath, report);
                EditorApplication.Exit(string.Equals(report.overall_status, "PASS", StringComparison.Ordinal) ? 0 : 1);
            }
        }

        private static void WarmUpLoad(string path)
        {
            MiqRuntimeLoader.TryLoad(path, out _, out _);
        }

        private static double MeasureLoadMeanMs(string path, int iterations, ref int decodeFailures)
        {
            var sumMs = 0.0;
            for (var i = 0; i < iterations; i++)
            {
                var sw = Stopwatch.StartNew();
                var ok = MiqRuntimeLoader.TryLoad(path, out _, out var diagnostics);
                sw.Stop();
                sumMs += sw.Elapsed.TotalMilliseconds;
                if (!ok || diagnostics.ErrorCode != MiqLoadErrorCode.None)
                {
                    decodeFailures++;
                }
            }
            return iterations <= 0 ? 0.0 : sumMs / iterations;
        }

        private static MiqAvatarPayload BuildCompressiblePayload()
        {
            var payload = new MiqAvatarPayload();
            payload.Manifest.avatarId = "ci-quality-avatar";
            payload.Manifest.displayName = "ci-quality-avatar";

            var vertexBlob = new byte[64 * 1024];
            for (var i = 0; i < vertexBlob.Length; i++)
            {
                vertexBlob[i] = (byte)(i % 8);
            }

            var textureBytes = new byte[256 * 1024];
            for (var i = 0; i < textureBytes.Length; i++)
            {
                textureBytes[i] = (byte)(i % 4);
            }

            payload.Meshes.Add(new MiqMeshPayload
            {
                Name = "mesh_0",
                VertexStride = 48,
                VertexBlob = vertexBlob,
                Indices = new uint[] { 0, 1, 2 },
                MaterialIndex = 0
            });
            payload.Textures.Add(new MiqTexturePayload
            {
                Name = "texture_0",
                Bytes = textureBytes
            });
            payload.Materials.Add(new MiqMaterialPayload
            {
                Name = "material_0",
                ShaderName = "lilToon",
                BaseColorTextureName = "texture_0",
                AlphaMode = "OPAQUE"
            });
            payload.Manifest.meshRefs.Add("mesh_0");
            payload.Manifest.materialRefs.Add("material_0");
            payload.Manifest.textureRefs.Add("texture_0");
            return payload;
        }

        private static void WriteReport<T>(string reportPath, T report)
        {
            if (string.IsNullOrWhiteSpace(reportPath))
            {
                return;
            }

            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(reportPath)) ?? ".");
                File.WriteAllText(reportPath, JsonUtility.ToJson(report, true));
            }
            catch
            {
                // best-effort report write
            }
        }

        private static int ParseIntOrDefault(string raw, int defaultValue, int min, int max)
        {
            if (!int.TryParse(raw, out var value))
            {
                return defaultValue;
            }
            return Math.Max(min, Math.Min(max, value));
        }

        private static string GetArgValue(string[] args, string key)
        {
            if (args == null || string.IsNullOrWhiteSpace(key))
            {
                return null;
            }
            for (var i = 0; i < args.Length - 1; i++)
            {
                if (string.Equals(args[i], key, StringComparison.Ordinal))
                {
                    return args[i + 1];
                }
            }
            return null;
        }
    }
}
