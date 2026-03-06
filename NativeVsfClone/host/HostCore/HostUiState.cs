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
    LeftShoulder = 8,
    RightShoulder = 9,
    LeftLowerArm = 10,
    RightLowerArm = 11,
    LeftHand = 12,
    RightHand = 13,
}

public sealed record PoseBoneUiOffset(
    PoseBoneKind Bone,
    float PitchDeg,
    float YawDeg,
    float RollDeg);

public sealed record ArmPoseTuningSettings(
    bool EnableSmoothing,
    float SmoothingTauMs,
    float DeadbandDeg,
    float SoftClampDeg,
    float HardClampMinDeg,
    float HardClampMaxDeg,
    float MaxDegreesPerSecond);

public sealed record SuggestedArmPreset(
    string Name,
    float LeftPitchDeg,
    float RightPitchDeg,
    float Score,
    DateTimeOffset LastUsedUtc);

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
    long UiFlowTimingVersion,
    double FirstBroadcastStartMs,
    string FirstBroadcastStartTimestamp,
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

public enum HostOnboardingStep
{
    Initialize = 0,
    LoadAvatar = 1,
    StartOutput = 2,
    Ready = 3,
    Blocked = 4,
}

public enum HostPrimaryActionKind
{
    None = 0,
    InitializeSession = 1,
    LoadAvatar = 2,
    StartOutput = 3,
}

public enum HostActionability
{
    Immediate = 0,
    Blocked = 1,
}

public sealed record HostOnboardingState(
    HostOnboardingStep Step,
    HostPrimaryActionKind PrimaryAction,
    string StepTitle,
    string Instruction,
    string BlockReason,
    string RecoveryAction,
    string NextActionSummary = "",
    string BlockReasonShort = "",
    HostActionability Actionability = HostActionability.Immediate);
