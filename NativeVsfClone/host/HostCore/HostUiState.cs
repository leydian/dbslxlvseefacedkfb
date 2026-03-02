using System;

namespace HostCore;

public sealed record HostSessionState(
    bool IsInitialized,
    bool IsWindowAttached,
    ulong? ActiveAvatarHandle,
    NcResultCode LastRenderRc,
    double LogicalWidth = 0.0,
    double LogicalHeight = 0.0,
    double DpiScaleX = 1.0,
    double DpiScaleY = 1.0,
    uint RenderWidthPx = 0U,
    uint RenderHeightPx = 0U);

public sealed record OutputState(
    bool SpoutActive,
    bool OscActive,
    string SpoutChannelName,
    uint SpoutWidthPx,
    uint SpoutHeightPx,
    uint SpoutFps,
    ushort OscBindPort,
    string OscPublishAddress);

public sealed record DiagnosticsSnapshot(
    DateTimeOffset TimestampUtc,
    HostSessionState Session,
    OutputState Outputs,
    DiagnosticsModel Runtime,
    NcAvatarInfo? AvatarInfo,
    NcResultCode LastRenderRc);

public sealed record HostLogEntry(
    DateTimeOffset TimestampUtc,
    string Source,
    string Message,
    NcResultCode ResultCode);
