using System;
using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;

namespace HostCore;

public sealed record DiagnosticsModel(
    string LastError,
    uint RenderReadyAvatarCount,
    bool SpoutActive,
    bool OscActive,
    float LastFrameMs,
    float GpuFrameMs,
    float CpuFrameMs,
    float MaterialResolveMs,
    float WorkingSetMb,
    float PrivateMb,
    uint PassCount,
    string SpoutBackend,
    bool SpoutStrictMode,
    ulong SpoutFallbackCount,
    string SpoutLastErrorCode,
    string NativeCoreModulePath,
    string NativeCoreModuleTimestampUtc,
    string NativeCoreModuleSha256,
    string BuildNativeCoreModulePath,
    string BuildNativeCoreModuleTimestampUtc,
    string BuildNativeCoreModuleSha256,
    string ExpectedNativeCoreModulePath,
    string ExpectedNativeCoreModuleSha256,
    bool RuntimePathMatch,
    bool RuntimeHashMatchExpected,
    bool RuntimeModuleStaleVsBuildOutput,
    string RuntimePathWarningCode,
    string RuntimeTimestampWarningCode,
    string MemorySampleStatus)
{
    public static DiagnosticsModel Empty =>
        new(
            string.Empty,
            0U,
            false,
            false,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0U,
            "unknown",
            false,
            0UL,
            string.Empty,
            string.Empty,
            string.Empty,
            string.Empty,
            string.Empty,
            string.Empty,
            string.Empty,
            string.Empty,
            string.Empty,
            false,
            false,
            false,
            "HOST_RUNTIME_PATH_UNKNOWN",
            "none",
            "none");

    public static DiagnosticsModel FromNative(in NcRuntimeStats stats, in NcSpoutDiagnostics spout)
    {
        return FromNative(
            stats,
            spout,
            memoryOverride: null,
            memorySampleStatus: null);
    }

    public static DiagnosticsModel FromNative(
        in NcRuntimeStats stats,
        in NcSpoutDiagnostics spout,
        (float WorkingSetMb, float PrivateMb)? memoryOverride,
        string? memorySampleStatus)
    {
        var nativeCorePath = NativeCoreInterop.GetLoadedNativeCorePath();
        var nativeCoreTimestampUtc = NativeCoreInterop.GetLoadedNativeCoreTimestampUtc();
        var expectedPath = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "nativecore.dll"));
        var buildOutputPath = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "build", "Release", "nativecore.dll"));
        var normalizedLoaded = string.IsNullOrWhiteSpace(nativeCorePath) ? string.Empty : Path.GetFullPath(nativeCorePath);
        var loadedHash = ComputeSha256HexIfFileExists(normalizedLoaded);
        var expectedHash = ComputeSha256HexIfFileExists(expectedPath);
        var buildHash = ComputeSha256HexIfFileExists(buildOutputPath);
        var pathMatch = !string.IsNullOrWhiteSpace(normalizedLoaded) &&
                        string.Equals(normalizedLoaded, expectedPath, StringComparison.OrdinalIgnoreCase);
        var hashMatchExpected = !string.IsNullOrWhiteSpace(loadedHash) &&
                                !string.IsNullOrWhiteSpace(expectedHash) &&
                                string.Equals(loadedHash, expectedHash, StringComparison.OrdinalIgnoreCase);
        var buildTimestampUtc = string.Empty;
        var staleVsBuild = false;
        var timestampWarningCode = "none";
        if (!string.IsNullOrWhiteSpace(buildHash) && !string.IsNullOrWhiteSpace(expectedHash)) {
            staleVsBuild = !string.Equals(expectedHash, buildHash, StringComparison.OrdinalIgnoreCase);
            if (staleVsBuild) {
                timestampWarningCode = "HOST_RUNTIME_DIST_HASH_MISMATCH_BUILD_OUTPUT";
            }
            if (File.Exists(buildOutputPath)) {
                buildTimestampUtc = new FileInfo(buildOutputPath).LastWriteTimeUtc.ToString("o");
            }
        } else if (File.Exists(buildOutputPath) && File.Exists(expectedPath)) {
            var buildItem = new FileInfo(buildOutputPath);
            var distItem = new FileInfo(expectedPath);
            buildTimestampUtc = buildItem.LastWriteTimeUtc.ToString("o");
            staleVsBuild = distItem.LastWriteTimeUtc < buildItem.LastWriteTimeUtc;
            if (staleVsBuild) {
                timestampWarningCode = "HOST_RUNTIME_DIST_OLDER_THAN_BUILD_OUTPUT";
            }
        } else if (!File.Exists(buildOutputPath)) {
            timestampWarningCode = "HOST_RUNTIME_BUILD_OUTPUT_UNKNOWN";
        } else if (!File.Exists(expectedPath)) {
            timestampWarningCode = "HOST_RUNTIME_DIST_PATH_MISSING";
        }
        var warningCode = pathMatch
            ? string.Empty
            : string.IsNullOrWhiteSpace(normalizedLoaded)
                ? "HOST_RUNTIME_PATH_UNKNOWN"
                : "HOST_RUNTIME_MISMATCH_DIST_EXPECTED";
        var workingSetMb = 0.0f;
        var privateMb = 0.0f;
        var resolvedMemorySampleStatus = string.IsNullOrWhiteSpace(memorySampleStatus)
            ? "ok"
            : memorySampleStatus.Trim().ToLowerInvariant();
        if (memoryOverride.HasValue)
        {
            workingSetMb = memoryOverride.Value.WorkingSetMb;
            privateMb = memoryOverride.Value.PrivateMb;
        }
        else
        {
            try
            {
                using var process = Process.GetCurrentProcess();
                workingSetMb = (float)(process.WorkingSet64 / (1024.0 * 1024.0));
                privateMb = (float)(process.PrivateMemorySize64 / (1024.0 * 1024.0));
            }
            catch
            {
                // Keep zeros if process metrics collection fails.
                resolvedMemorySampleStatus = "failed";
            }
        }

        return new DiagnosticsModel(
            LastError: NativeCoreInterop.FormatLastError(),
            RenderReadyAvatarCount: stats.RenderReadyAvatarCount,
            SpoutActive: stats.SpoutActive != 0,
            OscActive: stats.OscActive != 0,
            LastFrameMs: stats.LastFrameMs,
            GpuFrameMs: stats.GpuFrameMs,
            CpuFrameMs: stats.CpuFrameMs,
            MaterialResolveMs: stats.MaterialResolveMs,
            WorkingSetMb: workingSetMb,
            PrivateMb: privateMb,
            PassCount: stats.PassCount,
            SpoutBackend: NativeCoreInterop.FormatSpoutBackend(spout.BackendKind),
            SpoutStrictMode: spout.StrictMode != 0U,
            SpoutFallbackCount: spout.FallbackCount,
            SpoutLastErrorCode: spout.LastErrorCode ?? string.Empty,
            NativeCoreModulePath: nativeCorePath,
            NativeCoreModuleTimestampUtc: nativeCoreTimestampUtc,
            NativeCoreModuleSha256: loadedHash,
            BuildNativeCoreModulePath: buildOutputPath,
            BuildNativeCoreModuleTimestampUtc: buildTimestampUtc,
            BuildNativeCoreModuleSha256: buildHash,
            ExpectedNativeCoreModulePath: expectedPath,
            ExpectedNativeCoreModuleSha256: expectedHash,
            RuntimePathMatch: pathMatch,
            RuntimeHashMatchExpected: hashMatchExpected,
            RuntimeModuleStaleVsBuildOutput: staleVsBuild,
            RuntimePathWarningCode: warningCode,
            RuntimeTimestampWarningCode: timestampWarningCode,
            MemorySampleStatus: resolvedMemorySampleStatus);
    }

    public static DiagnosticsModel Capture()
    {
        if (NativeCoreInterop.nc_get_runtime_stats(out var stats) != NcResultCode.Ok)
        {
            return Empty with { LastError = NativeCoreInterop.FormatLastError() };
        }

        _ = NativeCoreInterop.nc_get_spout_diagnostics(out var spout);
        return FromNative(stats, spout);
    }

    private static string ComputeSha256HexIfFileExists(string? path)
    {
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
        {
            return string.Empty;
        }

        try
        {
            var bytes = File.ReadAllBytes(path);
            return Convert.ToHexString(SHA256.HashData(bytes));
        }
        catch
        {
            return string.Empty;
        }
    }
}
