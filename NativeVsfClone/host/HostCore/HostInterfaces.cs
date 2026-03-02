using System;

namespace HostCore;

public interface IAvatarSessionService
{
    bool IsInitialized { get; }
    ulong? ActiveAvatarHandle { get; }
    NcAvatarInfo? ActiveAvatarInfo { get; }
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
