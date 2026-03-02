using System;
using System.Diagnostics;
using System.Text;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Threading;
using HostCore;

namespace WpfHost;

public partial class MainWindow : Window
{
    private readonly AvatarSessionService _session = new();
    private readonly RenderLoopService _renderLoop = new();
    private readonly OutputService _outputs = new();
    private readonly DispatcherTimer _timer = new();
    private readonly Stopwatch _frameTimer = Stopwatch.StartNew();
    private IntPtr _hwnd = IntPtr.Zero;
    private NcResultCode _lastRenderRc = NcResultCode.Ok;

    public MainWindow()
    {
        InitializeComponent();
        SourceInitialized += MainWindow_SourceInitialized;
        Closed += MainWindow_Closed;
        SizeChanged += MainWindow_SizeChanged;
        _timer.Interval = TimeSpan.FromMilliseconds(16.0);
        _timer.Tick += Timer_Tick;
    }

    private void MainWindow_SourceInitialized(object? sender, EventArgs e)
    {
        _hwnd = new WindowInteropHelper(this).Handle;
    }

    private void MainWindow_SizeChanged(object sender, SizeChangedEventArgs e)
    {
        if (_hwnd == IntPtr.Zero)
        {
            return;
        }

        var width = (uint)Math.Max(1.0, RenderHost.ActualWidth);
        var height = (uint)Math.Max(1.0, RenderHost.ActualHeight);
        _ = _renderLoop.Resize(width, height);
    }

    private void MainWindow_Closed(object? sender, EventArgs e)
    {
        _timer.Stop();
        _ = _renderLoop.DetachWindow();
        _ = _session.UnloadAvatar();
        _ = _outputs.StopSpout();
        _ = _outputs.StopOsc();
        _ = _session.Shutdown();
    }

    private void Initialize_Click(object sender, RoutedEventArgs e)
    {
        var initRc = _session.Initialize();
        if (initRc == NcResultCode.Ok)
        {
            var width = (uint)Math.Max(1.0, RenderHost.ActualWidth);
            var height = (uint)Math.Max(1.0, RenderHost.ActualHeight);
            var attachRc = _renderLoop.AttachWindow(_hwnd, width, height);
            if (attachRc == NcResultCode.Ok)
            {
                _timer.Start();
            }
        }
        RefreshDiagnostics();
    }

    private void Load_Click(object sender, RoutedEventArgs e)
    {
        _ = _session.UnloadAvatar();
        _ = _session.LoadAvatar(AvatarPathTextBox.Text);
        RefreshDiagnostics();
    }

    private void Unload_Click(object sender, RoutedEventArgs e)
    {
        _ = _session.UnloadAvatar();
        RefreshDiagnostics();
    }

    private void StartSpout_Click(object sender, RoutedEventArgs e)
    {
        var width = (uint)Math.Max(1.0, RenderHost.ActualWidth);
        var height = (uint)Math.Max(1.0, RenderHost.ActualHeight);
        _ = _outputs.StartSpout(width, height, 60, "VsfClone");
        RefreshDiagnostics();
    }

    private void StopSpout_Click(object sender, RoutedEventArgs e)
    {
        _ = _outputs.StopSpout();
        RefreshDiagnostics();
    }

    private void StartOsc_Click(object sender, RoutedEventArgs e)
    {
        _ = _outputs.StartOsc(39539, "127.0.0.1:39540");
        RefreshDiagnostics();
    }

    private void StopOsc_Click(object sender, RoutedEventArgs e)
    {
        _ = _outputs.StopOsc();
        RefreshDiagnostics();
    }

    private void Timer_Tick(object? sender, EventArgs e)
    {
        var elapsed = _frameTimer.Elapsed;
        _frameTimer.Restart();
        _lastRenderRc = _renderLoop.Tick((float)elapsed.TotalSeconds);
        _ = _session.RefreshAvatarInfo();
        RefreshDiagnostics();
    }

    private void RefreshDiagnostics()
    {
        var d = DiagnosticsModel.Capture();
        var sb = new StringBuilder();
        sb.AppendLine($"AvatarHandle: {_session.ActiveAvatarHandle?.ToString() ?? "none"}");
        if (_session.ActiveAvatarInfo.HasValue)
        {
            var info = _session.ActiveAvatarInfo.Value;
            sb.AppendLine($"ParserStage: {info.ParserStage}");
            sb.AppendLine($"Format: {info.DetectedFormat}");
            sb.AppendLine($"MeshPayloads: {info.MeshPayloadCount}");
            sb.AppendLine($"MaterialPayloads: {info.MaterialPayloadCount}");
            sb.AppendLine($"TexturePayloads: {info.TexturePayloadCount}");
            sb.AppendLine($"Expressions: {info.ExpressionCount}");
            sb.AppendLine($"DrawCalls: {info.LastRenderDrawCalls}");
            sb.AppendLine($"ExpressionSummary: {info.LastExpressionSummary}");
        }
        sb.AppendLine($"RenderRc: {_lastRenderRc}");
        sb.AppendLine($"RenderReadyAvatars: {d.RenderReadyAvatarCount}");
        sb.AppendLine($"SpoutActive: {d.SpoutActive}");
        sb.AppendLine($"OscActive: {d.OscActive}");
        sb.AppendLine($"LastFrameMs: {d.LastFrameMs:F3}");
        sb.AppendLine($"LastError: {d.LastError}");
        DiagnosticsTextBox.Text = sb.ToString();
    }
}
