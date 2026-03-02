using System;
using System.Diagnostics;
using System.Text;
using HostCore;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using WinRT.Interop;

namespace WinUiHost;

public sealed partial class MainWindow : Window
{
    private readonly AvatarSessionService _session = new();
    private readonly RenderLoopService _renderLoop = new();
    private readonly OutputService _outputs = new();
    private readonly Stopwatch _frameTimer = Stopwatch.StartNew();
    private readonly DispatcherQueueTimer _timer;
    private readonly IntPtr _hwnd;

    public MainWindow()
    {
        InitializeComponent();
        _hwnd = WindowNative.GetWindowHandle(this);
        _timer = DispatcherQueue.GetForCurrentThread().CreateTimer();
        _timer.Interval = TimeSpan.FromMilliseconds(16.0);
        _timer.Tick += Timer_Tick;
        Closed += MainWindow_Closed;
    }

    private void MainWindow_Closed(object sender, WindowEventArgs args)
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
            var attachRc = _renderLoop.AttachWindow(_hwnd, 1280, 720);
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
        _ = _outputs.StartSpout(1280, 720, 60, "VsfClone");
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

    private void Timer_Tick(DispatcherQueueTimer sender, object args)
    {
        var elapsed = _frameTimer.Elapsed;
        _frameTimer.Restart();
        _ = _renderLoop.Tick((float)elapsed.TotalSeconds);
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
        }
        sb.AppendLine($"RenderReadyAvatars: {d.RenderReadyAvatarCount}");
        sb.AppendLine($"SpoutActive: {d.SpoutActive}");
        sb.AppendLine($"OscActive: {d.OscActive}");
        sb.AppendLine($"LastFrameMs: {d.LastFrameMs:F3}");
        sb.AppendLine($"LastError: {d.LastError}");
        DiagnosticsTextBox.Text = sb.ToString();
    }
}
