using System;

namespace HostCore;

public sealed class RenderLoopService
{
    private IntPtr _hwnd = IntPtr.Zero;
    private uint _width;
    private uint _height;

    public NcResultCode AttachWindow(IntPtr hwnd, uint width, uint height)
    {
        _hwnd = hwnd;
        _width = width;
        _height = height;
        var target = new NcWindowRenderTarget
        {
            Hwnd = hwnd,
            Width = Math.Max(width, 1U),
            Height = Math.Max(height, 1U),
        };
        return NativeCoreInterop.nc_create_window_render_target(ref target);
    }

    public NcResultCode Resize(uint width, uint height)
    {
        if (_hwnd == IntPtr.Zero)
        {
            return NcResultCode.InvalidArgument;
        }

        _width = width;
        _height = height;
        var target = new NcWindowRenderTarget
        {
            Hwnd = _hwnd,
            Width = Math.Max(width, 1U),
            Height = Math.Max(height, 1U),
        };
        return NativeCoreInterop.nc_resize_window_render_target(ref target);
    }

    public NcResultCode Tick(float deltaTimeSeconds)
    {
        if (_hwnd == IntPtr.Zero)
        {
            return NcResultCode.InvalidArgument;
        }
        return NativeCoreInterop.nc_render_frame_to_window(_hwnd, deltaTimeSeconds);
    }

    public NcResultCode DetachWindow()
    {
        if (_hwnd == IntPtr.Zero)
        {
            return NcResultCode.Ok;
        }
        var rc = NativeCoreInterop.nc_destroy_window_render_target(_hwnd);
        _hwnd = IntPtr.Zero;
        _width = 0;
        _height = 0;
        return rc;
    }
}
