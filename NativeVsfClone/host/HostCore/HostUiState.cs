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

public enum RenderCameraMode
{
    AutoFitFull = 0,
    AutoFitBust = 1,
    Manual = 2,
}

public enum BackgroundPreset
{
    DarkBlue = 0,
    NeutralGray = 1,
    GreenScreen = 2,
}

public enum PoseBoneKind
{
    Hips = 0,
    Spine = 1,
    Chest = 2,
    UpperChest = 3,
    Neck = 4,
    Head = 5,
    LeftUpperArm = 6,
    RightUpperArm = 7,
}

public sealed record PoseBoneUiOffset(
    PoseBoneKind Bone,
    float PitchDeg,
    float YawDeg,
    float RollDeg);

public sealed record RenderUiState(
    bool BroadcastMode,
    RenderCameraMode CameraMode,
    float FramingTarget,
    float Headroom,
    float YawDeg,
    float FovDeg,
    BackgroundPreset BackgroundPreset,
    bool ShowDebugOverlay,
    bool MirrorMode);

public sealed record DiagnosticsSnapshot(
    DateTimeOffset TimestampUtc,
    long SnapshotVersion,
    long LogVersion,
    HostSessionState Session,
    OutputState Outputs,
    RenderUiState Render,
    TrackingDiagnostics Tracking,
    DiagnosticsModel Runtime,
    NcAvatarInfo? AvatarInfo,
    NcResultCode LastRenderRc);

public sealed record HostLogEntry(
    DateTimeOffset TimestampUtc,
    string Source,
    string Message,
    NcResultCode ResultCode);

public sealed record HostOperationState(
    bool IsBusy,
    string CurrentOperation);

public sealed record HostValidationState(
    bool AvatarPathValid,
    bool OscBindPortValid,
    bool OscPublishAddressValid,
    string AvatarPathError,
    string OscBindPortError,
    string OscPublishAddressError)
{
    public bool IsValid => AvatarPathValid && OscBindPortValid && OscPublishAddressValid;
}
