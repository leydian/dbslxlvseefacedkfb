using System;
using System.Runtime.InteropServices;
using System.Windows.Interop;

namespace WpfHost;

public sealed class RenderHwndHost : HwndHost
{
    private const int WsChild = 0x40000000;
    private const int WsVisible = 0x10000000;
    private const int WsClipSiblings = 0x04000000;
    private const int WsClipChildren = 0x02000000;
    private const int WmRButtonDown = 0x0204;
    private const int WmRButtonUp = 0x0205;
    private const int WmMouseMove = 0x0200;
    private const int WmMouseWheel = 0x020A;

    private IntPtr _hwnd = IntPtr.Zero;
    private bool _rightDragActive;

    public IntPtr Hwnd => _hwnd;

    public event EventHandler<RenderMouseDragEventArgs>? RenderRightDragStarted;
    public event EventHandler<RenderMouseDragEventArgs>? RenderRightDragMoved;
    public event EventHandler<RenderMouseDragEventArgs>? RenderRightDragCompleted;
    public event EventHandler<RenderMouseWheelEventArgs>? RenderMouseWheel;

    protected override HandleRef BuildWindowCore(HandleRef hwndParent)
    {
        _hwnd = CreateWindowEx(
            0,
            "static",
            string.Empty,
            WsChild | WsVisible | WsClipSiblings | WsClipChildren,
            0,
            0,
            1,
            1,
            hwndParent.Handle,
            IntPtr.Zero,
            IntPtr.Zero,
            IntPtr.Zero);

        if (_hwnd == IntPtr.Zero)
        {
            throw new InvalidOperationException("Failed to create render host child window.");
        }

        return new HandleRef(this, _hwnd);
    }

    protected override void DestroyWindowCore(HandleRef hwnd)
    {
        _ = DestroyWindow(hwnd.Handle);
        _hwnd = IntPtr.Zero;
    }

    protected override IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        switch (msg)
        {
            case WmRButtonDown:
            {
                _rightDragActive = true;
                _ = SetCapture(hwnd);
                var (x, y) = GetPointFromLParam(lParam);
                RenderRightDragStarted?.Invoke(this, new RenderMouseDragEventArgs(x, y));
                break;
            }
            case WmMouseMove:
            {
                if (_rightDragActive)
                {
                    var (x, y) = GetPointFromLParam(lParam);
                    RenderRightDragMoved?.Invoke(this, new RenderMouseDragEventArgs(x, y));
                }
                break;
            }
            case WmRButtonUp:
            {
                var (x, y) = GetPointFromLParam(lParam);
                if (_rightDragActive)
                {
                    RenderRightDragCompleted?.Invoke(this, new RenderMouseDragEventArgs(x, y));
                }
                _rightDragActive = false;
                _ = ReleaseCapture();
                break;
            }
            case WmMouseWheel:
            {
                var delta = GetWheelDeltaFromWParam(wParam);
                RenderMouseWheel?.Invoke(this, new RenderMouseWheelEventArgs(delta));
                break;
            }
        }

        return base.WndProc(hwnd, msg, wParam, lParam, ref handled);
    }

    protected override void OnWindowPositionChanged(System.Windows.Rect rcBoundingBox)
    {
        base.OnWindowPositionChanged(rcBoundingBox);
        if (_hwnd != IntPtr.Zero)
        {
            _ = MoveWindow(
                _hwnd,
                0,
                0,
                Math.Max(1, (int)rcBoundingBox.Width),
                Math.Max(1, (int)rcBoundingBox.Height),
                true);
        }
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateWindowEx(
        int exStyle,
        string className,
        string windowName,
        int style,
        int x,
        int y,
        int width,
        int height,
        IntPtr hwndParent,
        IntPtr hMenu,
        IntPtr hInstance,
        IntPtr lpParam);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DestroyWindow(IntPtr hwnd);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool MoveWindow(
        IntPtr hwnd,
        int x,
        int y,
        int nWidth,
        int nHeight,
        [MarshalAs(UnmanagedType.Bool)] bool repaint);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern IntPtr SetCapture(IntPtr hwnd);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ReleaseCapture();

    private static (int x, int y) GetPointFromLParam(IntPtr lParam)
    {
        var value = unchecked((int)(long)lParam);
        var x = (short)(value & 0xFFFF);
        var y = (short)((value >> 16) & 0xFFFF);
        return (x, y);
    }

    private static int GetWheelDeltaFromWParam(IntPtr wParam)
    {
        var value = unchecked((int)(long)wParam);
        return (short)((value >> 16) & 0xFFFF);
    }
}

public sealed record RenderMouseDragEventArgs(int X, int Y);
public sealed record RenderMouseWheelEventArgs(int Delta);
