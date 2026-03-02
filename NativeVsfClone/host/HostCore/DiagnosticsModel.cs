namespace HostCore;

public sealed record DiagnosticsModel(
    string LastError,
    uint RenderReadyAvatarCount,
    bool SpoutActive,
    bool OscActive,
    float LastFrameMs)
{
    public static DiagnosticsModel Capture()
    {
        _ = NativeCoreInterop.nc_get_runtime_stats(out var stats);
        return new DiagnosticsModel(
            LastError: NativeCoreInterop.FormatLastError(),
            RenderReadyAvatarCount: stats.RenderReadyAvatarCount,
            SpoutActive: stats.SpoutActive != 0,
            OscActive: stats.OscActive != 0,
            LastFrameMs: stats.LastFrameMs);
    }
}
