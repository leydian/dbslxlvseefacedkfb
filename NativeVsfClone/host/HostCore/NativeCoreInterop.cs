using System;
using System.Runtime.InteropServices;
using System.Text;

namespace HostCore;

public enum NcResultCode : uint
{
    Ok = 0,
    NotInitialized = 1,
    InvalidArgument = 2,
    Io = 3,
    Unsupported = 4,
    Internal = 5,
}

public enum NcAvatarFormatHint : uint
{
    Auto = 0,
    Vrm = 1,
    VxAvatar = 2,
    VsfAvatar = 3,
    Vxa2 = 4,
}

[StructLayout(LayoutKind.Sequential)]
public struct NcInitOptions
{
    public uint ApiVersion;
    public uint Reserved;
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
public struct NcAvatarLoadRequest
{
    public string Path;
    public NcAvatarFormatHint FormatHint;
    public uint ShaderProfile;
    public uint FallbackPolicy;
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
public struct NcAvatarInfo
{
    public ulong Handle;
    public NcAvatarFormatHint DetectedFormat;
    public uint CompatLevel;
    public uint MeshCount;
    public uint MaterialCount;
    public uint MeshPayloadCount;
    public uint MaterialPayloadCount;
    public uint TexturePayloadCount;
    public uint FormatSectionCount;
    public uint FormatDecodedSectionCount;
    public uint FormatUnknownSectionCount;
    public uint WarningCount;
    public uint MissingFeatureCount;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
    public string DisplayName;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
    public string SourcePath;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
    public string ParserStage;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
    public string PrimaryErrorCode;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
    public string LastWarning;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
    public string LastMissingFeature;
}

[StructLayout(LayoutKind.Sequential)]
public struct NcSpoutOptions
{
    public uint Width;
    public uint Height;
    public uint Fps;
    [MarshalAs(UnmanagedType.LPStr)]
    public string ChannelName;
}

[StructLayout(LayoutKind.Sequential)]
public struct NcOscOptions
{
    public ushort BindPort;
    [MarshalAs(UnmanagedType.LPStr)]
    public string PublishAddress;
}

[StructLayout(LayoutKind.Sequential)]
public struct NcWindowRenderTarget
{
    public IntPtr Hwnd;
    public uint Width;
    public uint Height;
}

[StructLayout(LayoutKind.Sequential)]
public struct NcRuntimeStats
{
    public uint RenderReadyAvatarCount;
    public uint SpoutActive;
    public uint OscActive;
    public float LastFrameMs;
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
public struct NcErrorInfo
{
    public NcResultCode Code;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
    public string Subsystem;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
    public string Message;
    public byte Recoverable;
}

public static class NativeCoreInterop
{
    private const string DllName = "nativecore.dll";

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_initialize(ref NcInitOptions options);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_shutdown();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern NcResultCode nc_load_avatar(
        ref NcAvatarLoadRequest request,
        out ulong outHandle,
        out NcAvatarInfo outInfo);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_unload_avatar(ulong handle);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_create_render_resources(ulong handle);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_destroy_render_resources(ulong handle);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_create_window_render_target(ref NcWindowRenderTarget target);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_resize_window_render_target(ref NcWindowRenderTarget target);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_destroy_window_render_target(IntPtr hwnd);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_render_frame_to_window(IntPtr hwnd, float deltaTimeSeconds);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_start_spout(ref NcSpoutOptions options);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_stop_spout();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_start_osc(ref NcOscOptions options);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_stop_osc();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_get_last_error(out NcErrorInfo outError);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_get_runtime_stats(out NcRuntimeStats outStats);

    public static string FormatLastError()
    {
        var rc = nc_get_last_error(out var err);
        if (rc != NcResultCode.Ok)
        {
            return $"last_error unavailable: {rc}";
        }

        var recoverable = err.Recoverable == 0 ? "fatal" : "recoverable";
        return $"{err.Code} [{err.Subsystem}] {err.Message} ({recoverable})";
    }
}
