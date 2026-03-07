using System;
using System.Diagnostics;
using System.Windows;
using System.Windows.Media;
using System.Windows.Threading;
using HostCore;

namespace WpfHost;

public partial class FloatingAvatarWindow : Window
{
    private readonly HostController _controller;
    private readonly DispatcherTimer _timer = new();
    private readonly Stopwatch _frameTimer = Stopwatch.StartNew();
    private bool _renderTargetCreated;

    public FloatingAvatarWindow(HostController controller)
    {
        _controller = controller;
        InitializeComponent();
        SourceInitialized += OnSourceInitialized;
        SizeChanged += OnSizeChanged;
        _controller.StateChanged += OnControllerStateChanged;
        _timer.Interval = TimeSpan.FromMilliseconds(16.0);
        _timer.Tick += TimerTick;
    }

    private void OnSourceInitialized(object? sender, EventArgs e)
        => TryCreateRenderTarget();

    private void OnControllerStateChanged(object? sender, EventArgs e)
        => Dispatcher.InvokeAsync(() =>
        {
            if (_controller.SessionState.IsInitialized &&
                _controller.SessionState.ActiveAvatarHandle.HasValue)
                TryCreateRenderTarget();
            else
                DestroyRenderTarget();
        });

    private void TryCreateRenderTarget()
    {
        var hwnd = FloatRenderHost.Hwnd;
        if (_renderTargetCreated || hwnd == IntPtr.Zero) return;
        if (!_controller.SessionState.IsInitialized ||
            !_controller.SessionState.ActiveAvatarHandle.HasValue) return;

        var (w, h) = GetPixelSize();
        var target = new NcWindowRenderTarget { Hwnd = hwnd, Width = w, Height = h };
        var rc = NativeCoreInterop.nc_create_window_render_target(ref target);
        if (rc == NcResultCode.Ok)
        {
            _renderTargetCreated = true;
            _frameTimer.Restart();
            _timer.Start();
        }
    }

    private void DestroyRenderTarget()
    {
        _timer.Stop();
        if (!_renderTargetCreated) return;
        var hwnd = FloatRenderHost.Hwnd;
        if (hwnd != IntPtr.Zero)
            NativeCoreInterop.nc_destroy_window_render_target(hwnd);
        _renderTargetCreated = false;
    }

    private void TimerTick(object? sender, EventArgs e)
    {
        if (!_renderTargetCreated) return;
        var elapsed = _frameTimer.Elapsed;
        _frameTimer.Restart();
        _ = NativeCoreInterop.nc_render_frame_to_window(FloatRenderHost.Hwnd, (float)elapsed.TotalSeconds);
        FloatRenderHost.InvalidateVisual();
    }

    private void OnSizeChanged(object? sender, SizeChangedEventArgs e)
    {
        if (!_renderTargetCreated) return;
        var (w, h) = GetPixelSize();
        var target = new NcWindowRenderTarget { Hwnd = FloatRenderHost.Hwnd, Width = w, Height = h };
        _ = NativeCoreInterop.nc_resize_window_render_target(ref target);
    }

    private void AlwaysOnTop_Changed(object sender, RoutedEventArgs e)
        => Topmost = AlwaysOnTopCheckBox.IsChecked == true;

    private void CloseButton_Click(object sender, RoutedEventArgs e)
        => Close();

    protected override void OnClosed(EventArgs e)
    {
        _controller.StateChanged -= OnControllerStateChanged;
        DestroyRenderTarget();
        base.OnClosed(e);
    }

    private (uint w, uint h) GetPixelSize()
    {
        var dpi = VisualTreeHelper.GetDpi(FloatRenderHost);
        var sx = dpi.DpiScaleX > 0 ? dpi.DpiScaleX : 1.0;
        var sy = dpi.DpiScaleY > 0 ? dpi.DpiScaleY : 1.0;
        return (
            (uint)Math.Max(1, Math.Round(FloatRenderHost.ActualWidth * sx)),
            (uint)Math.Max(1, Math.Round(FloatRenderHost.ActualHeight * sy))
        );
    }
}
