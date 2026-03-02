using System;

namespace HostCore;

public sealed record HostSessionState(
    bool IsInitialized,
    bool IsWindowAttached,
    ulong? ActiveAvatarHandle,
    NcResultCode LastRenderRc);

public sealed record OutputState(
    bool SpoutActive,
    bool OscActive,
    string SpoutChannelName,
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
