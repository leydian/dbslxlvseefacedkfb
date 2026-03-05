namespace HostCore;

public sealed record DiagnosticsModel(
    string LastError,
    uint RenderReadyAvatarCount,
    bool SpoutActive,
    bool OscActive,
    float LastFrameMs,
    string SpoutBackend,
    bool SpoutStrictMode,
    ulong SpoutFallbackCount,
    string SpoutLastErrorCode)
{
    public static DiagnosticsModel Empty =>
        new(string.Empty, 0U, false, false, 0.0f, "unknown", false, 0UL, string.Empty);

    public static DiagnosticsModel FromNative(in NcRuntimeStats stats, in NcSpoutDiagnostics spout)
    {
        return new DiagnosticsModel(
            LastError: NativeCoreInterop.FormatLastError(),
            RenderReadyAvatarCount: stats.RenderReadyAvatarCount,
            SpoutActive: stats.SpoutActive != 0,
            OscActive: stats.OscActive != 0,
            LastFrameMs: stats.LastFrameMs,
            SpoutBackend: NativeCoreInterop.FormatSpoutBackend(spout.BackendKind),
            SpoutStrictMode: spout.StrictMode != 0U,
            SpoutFallbackCount: spout.FallbackCount,
            SpoutLastErrorCode: spout.LastErrorCode ?? string.Empty);
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
