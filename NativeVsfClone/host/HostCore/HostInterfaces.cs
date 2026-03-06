using System;

namespace HostCore;

public interface IAvatarSessionService
{
    bool IsInitialized { get; }
    ulong? ActiveAvatarHandle { get; }
    NcAvatarInfo? ActiveAvatarInfo { get; }
    NcAvatarInfo? LastLoadAttemptInfo { get; }
    NcResultCode Initialize();
    NcResultCode Shutdown();
    NcResultCode LoadAvatar(string path);
    NcResultCode UnloadAvatar();
    NcResultCode RefreshAvatarInfo();
}

public interface IRenderLoopService
{
    NcResultCode AttachWindow(IntPtr hwnd, uint width, uint height);
    NcResultCode Resize(uint width, uint height);
    NcResultCode Tick(float deltaTimeSeconds);
    NcResultCode DetachWindow();
}

public interface IOutputService
{
    NcResultCode StartSpout(uint width, uint height, uint fps, string channelName);
    NcResultCode StopSpout();
    NcResultCode StartOsc(ushort bindPort, string publishAddress);
    NcResultCode StopOsc();
}

public interface IRenderPresetStore
{
    RenderPresetStoreModel Load();
    void Save(RenderPresetStoreModel store);
}

public interface IPosePresetStore
{
    PosePresetStoreModel Load();
    void Save(PosePresetStoreModel store);
}

public enum TrackingSourceType
{
    OscIfacial = 0,
    WebcamMediapipe = 1,
    HybridAuto = 2,
}

public enum TrackingSourceLockMode
{
    Auto = 0,
    IfacialLocked = 1,
    WebcamLocked = 2,
}

public enum TrackingLatencyProfile
{
    LowLatency = 0,
    Balanced = 1,
    Stable = 2,
}

public enum PoseFilterProfile
{
    Reactive = 0,
    Balanced = 1,
    Stable = 2,
}

public enum UpperBodySmoothingProfile
{
    Reactive = 0,
    Balanced = 1,
    Stable = 2,
}

public sealed record TrackingStartOptions(
    ushort ListenPort,
    int StaleTimeoutMs,
    TrackingSourceType SourceType,
    string CameraDeviceKey,
    int InferenceFpsCap,
    int ParseErrorWarnThreshold = 10,
    int DroppedPacketWarnThreshold = 10,
    TrackingSourceLockMode SourceLockMode = TrackingSourceLockMode.Auto,
    TrackingLatencyProfile LatencyProfile = TrackingLatencyProfile.Balanced,
    PoseFilterProfile PoseFilterProfile = PoseFilterProfile.Stable,
    float PoseDeadbandDeg = 0.9f,
    bool AutoStabilityTuningEnabled = true,
    bool UpperBodyEnabled = true,
    float UpperBodyStrength = 1.0f,
    UpperBodySmoothingProfile UpperBodySmoothing = UpperBodySmoothingProfile.Balanced);

public sealed record WebcamDeviceOption(
    string DeviceKey,
    string DisplayName,
    bool IsAvailable);

public sealed record TrackingDiagnostics(
    bool IsActive,
    string DetectedFormat,
    double InputFps,
    double CaptureFps,
    double InferenceMsAvg,
    int LastPacketAgeMs,
    bool IsStale,
    ulong ReceivedPackets,
    ulong DroppedPackets,
    ulong ParseErrors,
    string StatusMessage,
    TrackingSourceType SourceType,
    string SourceStatus,
    bool ModelSchemaOk = false,
    string LastErrorCode = "",
    string ActiveSource = "none",
    int FallbackCount = 0,
    string CalibrationState = "idle",
    string ConfidenceSummary = "",
    double LatencyAvgMs = 0.0,
    double LatencyP95Ms = 0.0,
    double CaptureStageMs = 0.0,
    double ParseStageMs = 0.0,
    double SmoothStageMs = 0.0,
    double SubmitStageMs = 0.0,
    TrackingSourceLockMode SourceLockMode = TrackingSourceLockMode.Auto,
    string SwitchBlockedReason = "",
    PoseFilterProfile PoseFilterProfile = PoseFilterProfile.Stable,
    float PoseDeadbandDeg = 0.9f,
    int Arkit52StrictCount = 0,
    int Arkit52FallbackCount = 0,
    int Arkit52SubmittedCount = 0,
    int Arkit52MissingCount = 52,
    string Arkit52MissingKeys = "",
    string Arkit52TopMissingKeys = "",
    double Arkit52QualityScore = 0.0,
    double Arkit52QualityStageMs = 0.0,
    int IfacialPacketAgeMs = int.MaxValue,
    int WebcamPacketAgeMs = int.MaxValue,
    bool UpperBodyTrackingActive = false,
    double UpperBodyConfidence = 0.0,
    int UpperBodyPacketAgeMs = int.MaxValue,
    string UpperBodyActiveSource = "none",
    string UpperBodyStatus = "idle",
    string UpperBodyLastError = "",
    int RecentSourceSwitchCount = 0,
    string LastSourceSwitchReason = "",
    int SourceSwitchCooldownRemainingMs = 0,
    string IfmAcceptedKeySample = "",
    string IfmDroppedKeySample = "");

public readonly record struct TrackingUpperBodyPose(
    bool IsValid,
    float LeftShoulderPitchDeg,
    float RightShoulderPitchDeg,
    float LeftUpperArmPitchDeg,
    float RightUpperArmPitchDeg,
    double Confidence,
    int PacketAgeMs,
    string Status)
{
    public static TrackingUpperBodyPose Neutral(int packetAgeMs = int.MaxValue, string status = "idle")
        => new(false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0, packetAgeMs, status);
}

public interface ITrackingInputService
{
    NcResultCode Start(TrackingStartOptions options);
    NcResultCode Stop();
    NcResultCode Recenter();
    bool TryGetLatestFrame(out NcTrackingFrame frame);
    bool TryGetLatestExpressionWeights(out IReadOnlyDictionary<string, float> weights);
    bool TryGetLatestUpperBodyPose(out TrackingUpperBodyPose pose);
    TrackingDiagnostics GetDiagnostics();
}
