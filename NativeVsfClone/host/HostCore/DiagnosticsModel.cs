using System;
using System.IO;

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
    uint PassCount,
    string SpoutBackend,
    bool SpoutStrictMode,
    ulong SpoutFallbackCount,
    string SpoutLastErrorCode,
    string NativeCoreModulePath,
    string NativeCoreModuleTimestampUtc,
    string ExpectedNativeCoreModulePath,
    bool RuntimePathMatch,
    string RuntimePathWarningCode)
{
    public static DiagnosticsModel Empty =>
        new(string.Empty, 0U, false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0U, "unknown", false, 0UL, string.Empty, string.Empty, string.Empty, string.Empty, false, "HOST_RUNTIME_PATH_UNKNOWN");

    public static DiagnosticsModel FromNative(in NcRuntimeStats stats, in NcSpoutDiagnostics spout)
    {
        var nativeCorePath = NativeCoreInterop.GetLoadedNativeCorePath();
        var nativeCoreTimestampUtc = NativeCoreInterop.GetLoadedNativeCoreTimestampUtc();
        var expectedPath = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "nativecore.dll"));
        var normalizedLoaded = string.IsNullOrWhiteSpace(nativeCorePath) ? string.Empty : Path.GetFullPath(nativeCorePath);
        var pathMatch = !string.IsNullOrWhiteSpace(normalizedLoaded) &&
                        string.Equals(normalizedLoaded, expectedPath, StringComparison.OrdinalIgnoreCase);
        var warningCode = pathMatch
            ? string.Empty
            : string.IsNullOrWhiteSpace(normalizedLoaded)
                ? "HOST_RUNTIME_PATH_UNKNOWN"
                : "HOST_RUNTIME_MISMATCH_DIST_EXPECTED";
        return new DiagnosticsModel(
            LastError: NativeCoreInterop.FormatLastError(),
            RenderReadyAvatarCount: stats.RenderReadyAvatarCount,
            SpoutActive: stats.SpoutActive != 0,
            OscActive: stats.OscActive != 0,
            LastFrameMs: stats.LastFrameMs,
            GpuFrameMs: stats.GpuFrameMs,
            CpuFrameMs: stats.CpuFrameMs,
            MaterialResolveMs: stats.MaterialResolveMs,
            PassCount: stats.PassCount,
            SpoutBackend: NativeCoreInterop.FormatSpoutBackend(spout.BackendKind),
            SpoutStrictMode: spout.StrictMode != 0U,
            SpoutFallbackCount: spout.FallbackCount,
            SpoutLastErrorCode: spout.LastErrorCode ?? string.Empty,
            NativeCoreModulePath: nativeCorePath,
            NativeCoreModuleTimestampUtc: nativeCoreTimestampUtc,
            ExpectedNativeCoreModulePath: expectedPath,
            RuntimePathMatch: pathMatch,
            RuntimePathWarningCode: warningCode);
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
}
