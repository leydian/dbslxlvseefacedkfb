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

    private IntPtr _hwnd = IntPtr.Zero;

    public IntPtr Hwnd => _hwnd;

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
}
