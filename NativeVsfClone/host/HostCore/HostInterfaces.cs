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

public enum TrackingSourceType
{
    OscIfacial = 0,
    WebcamMediapipe = 1,
}

public sealed record TrackingStartOptions(
    ushort ListenPort,
    int StaleTimeoutMs,
    TrackingSourceType SourceType,
    string CameraDeviceKey,
    int InferenceFpsCap,
    int ParseErrorWarnThreshold = 10,
    int DroppedPacketWarnThreshold = 10);

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
    string ConfidenceSummary = "");

public interface ITrackingInputService
{
    NcResultCode Start(TrackingStartOptions options);
    NcResultCode Stop();
    NcResultCode Recenter();
    bool TryGetLatestFrame(out NcTrackingFrame frame);
    bool TryGetLatestExpressionWeights(out IReadOnlyDictionary<string, float> weights);
    TrackingDiagnostics GetDiagnostics();
}
