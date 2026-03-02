using System;

namespace HostCore;

public sealed class AvatarSessionService : IAvatarSessionService
{
    public bool IsInitialized { get; private set; }
    public ulong? ActiveAvatarHandle { get; private set; }
    public NcAvatarInfo? ActiveAvatarInfo { get; private set; }

    public NcResultCode Initialize()
    {
        var options = new NcInitOptions
        {
            ApiVersion = 1,
            Reserved = 0,
        };
        var rc = NativeCoreInterop.nc_initialize(ref options);
        IsInitialized = rc == NcResultCode.Ok;
        return rc;
    }

    public NcResultCode Shutdown()
    {
        ActiveAvatarHandle = null;
        ActiveAvatarInfo = null;
        IsInitialized = false;
        return NativeCoreInterop.nc_shutdown();
    }

    public NcResultCode LoadAvatar(string path)
    {
        var req = new NcAvatarLoadRequest
        {
            Path = path,
            FormatHint = NcAvatarFormatHint.Auto,
            ShaderProfile = 0,
            FallbackPolicy = 0,
        };
        var rc = NativeCoreInterop.nc_load_avatar(ref req, out var handle, out var info);
        if (rc != NcResultCode.Ok)
        {
            return rc;
        }

        rc = NativeCoreInterop.nc_create_render_resources(handle);
        if (rc != NcResultCode.Ok)
        {
            _ = NativeCoreInterop.nc_unload_avatar(handle);
            return rc;
        }

        ActiveAvatarHandle = handle;
        ActiveAvatarInfo = info;
        return NcResultCode.Ok;
    }

    public NcResultCode UnloadAvatar()
    {
        if (!ActiveAvatarHandle.HasValue)
        {
            return NcResultCode.Ok;
        }

        _ = NativeCoreInterop.nc_destroy_render_resources(ActiveAvatarHandle.Value);
        var rc = NativeCoreInterop.nc_unload_avatar(ActiveAvatarHandle.Value);
        ActiveAvatarHandle = null;
        ActiveAvatarInfo = null;
        return rc;
    }

    public NcResultCode RefreshAvatarInfo()
    {
        if (!ActiveAvatarHandle.HasValue)
        {
            ActiveAvatarInfo = null;
            return NcResultCode.Ok;
        }

        var rc = NativeCoreInterop.nc_get_avatar_info(ActiveAvatarHandle.Value, out var info);
        if (rc == NcResultCode.Ok)
        {
            ActiveAvatarInfo = info;
        }
        return rc;
    }
}
