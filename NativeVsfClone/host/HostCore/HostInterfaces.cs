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

public sealed record TrackingStartOptions(
    ushort ListenPort,
    int StaleTimeoutMs);

public sealed record TrackingDiagnostics(
    bool IsActive,
    string DetectedFormat,
    double InputFps,
    int LastPacketAgeMs,
    bool IsStale,
    ulong ReceivedPackets,
    ulong DroppedPackets,
    ulong ParseErrors,
    string StatusMessage);

public interface ITrackingInputService
{
    NcResultCode Start(TrackingStartOptions options);
    NcResultCode Stop();
    NcResultCode Recenter();
    bool TryGetLatestFrame(out NcTrackingFrame frame);
    TrackingDiagnostics GetDiagnostics();
}
