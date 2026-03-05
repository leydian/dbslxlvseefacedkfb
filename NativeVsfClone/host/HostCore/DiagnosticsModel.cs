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
    string NativeCoreModuleTimestampUtc)
{
    public static DiagnosticsModel Empty =>
        new(string.Empty, 0U, false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0U, "unknown", false, 0UL, string.Empty, string.Empty, string.Empty);

    public static DiagnosticsModel FromNative(in NcRuntimeStats stats, in NcSpoutDiagnostics spout)
    {
        var nativeCorePath = NativeCoreInterop.GetLoadedNativeCorePath();
        var nativeCoreTimestampUtc = NativeCoreInterop.GetLoadedNativeCoreTimestampUtc();
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
            NativeCoreModuleTimestampUtc: nativeCoreTimestampUtc);
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
