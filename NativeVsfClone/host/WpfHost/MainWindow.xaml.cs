using System;
using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using HostCore;
using Microsoft.Win32;

namespace WpfHost;

public partial class MainWindow : Window
{
    private readonly HostController _controller = new();
    private readonly DispatcherTimer _timer = new();
    private readonly DispatcherTimer _resizeTimer = new();
    private readonly Stopwatch _frameTimer = Stopwatch.StartNew();
    private IntPtr _hwnd = IntPtr.Zero;

    public MainWindow()
    {
        InitializeComponent();
        SourceInitialized += MainWindow_SourceInitialized;
        Closed += MainWindow_Closed;
        SizeChanged += MainWindow_SizeChanged;
        _timer.Interval = TimeSpan.FromMilliseconds(16.0);
        _timer.Tick += Timer_Tick;
        _resizeTimer.Interval = TimeSpan.FromMilliseconds(90.0);
        _resizeTimer.Tick += ResizeTimer_Tick;

        _controller.StateChanged += Controller_StateChanged;
        _controller.DiagnosticsUpdated += Controller_DiagnosticsUpdated;
        _controller.ErrorRaised += Controller_ErrorRaised;
        RefreshAll();
    }

    private void MainWindow_SourceInitialized(object? sender, EventArgs e)
    {
        _hwnd = new WindowInteropHelper(this).Handle;
        UpdateRenderMetricsFromHost();
    }

    private void MainWindow_SizeChanged(object sender, SizeChangedEventArgs e)
    {
        if (_hwnd == IntPtr.Zero)
        {
            return;
        }

        var state = _controller.SessionState;
        if (!state.IsInitialized || !state.IsWindowAttached)
        {
            UpdateRenderMetricsFromHost();
            return;
        }

        UpdateRenderMetricsFromHost();
        _resizeTimer.Stop();
        _resizeTimer.Start();
    }

    private void MainWindow_Closed(object? sender, EventArgs e)
    {
        _timer.Stop();
        _resizeTimer.Stop();
        _ = _controller.Shutdown();
    }

    private void Initialize_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.SessionState.IsInitialized &&
            !Confirm("Session is already initialized. Reinitialize and reset active outputs/avatar?"))
        {
            return;
        }

        if (_controller.SessionState.IsInitialized)
        {
            _timer.Stop();
            _ = _controller.Shutdown();
        }

        var initRc = _controller.Initialize();
        if (initRc == NcResultCode.Ok && _hwnd != IntPtr.Zero)
        {
            var metrics = GetRenderMetrics();
            _controller.UpdateRenderMetrics(metrics.logicalWidth, metrics.logicalHeight, metrics.dpiScaleX, metrics.dpiScaleY, metrics.pixelWidth, metrics.pixelHeight);
            var attachRc = _controller.AttachWindow(_hwnd, metrics.pixelWidth, metrics.pixelHeight);
            if (attachRc == NcResultCode.Ok)
            {
                _timer.Start();
            }
        }
    }

    private void Shutdown_Click(object sender, RoutedEventArgs e)
    {
        if (!Confirm("Shutdown runtime and stop rendering/outputs?"))
        {
            return;
        }

        _timer.Stop();
        _ = _controller.Shutdown();
    }

    private void BrowseAvatar_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            CheckFileExists = true,
            CheckPathExists = true,
            Filter = "Avatar Files (*.vrm;*.vxavatar;*.vsfavatar;*.vxa2)|*.vrm;*.vxavatar;*.vsfavatar;*.vxa2|All Files (*.*)|*.*",
        };

        if (dialog.ShowDialog(this) == true)
        {
            AvatarPathTextBox.Text = dialog.FileName;
        }
    }

    private void Load_Click(object sender, RoutedEventArgs e)
    {
        if (!_controller.SessionState.IsInitialized)
        {
            MessageBox.Show(this, "Initialize the session first.", "Load Blocked", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        _ = _controller.LoadAvatar(AvatarPathTextBox.Text.Trim());
    }

    private void Unload_Click(object sender, RoutedEventArgs e)
    {
        if (!Confirm("Unload active avatar?"))
        {
            return;
        }
        _ = _controller.UnloadAvatar();
    }

    private void StartSpout_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.Outputs.SpoutActive &&
            !Confirm("Spout is active. Restart with current settings?"))
        {
            return;
        }

        if (_controller.Outputs.SpoutActive)
        {
            _ = _controller.StopSpout();
        }

        var metrics = GetRenderMetrics();
        _controller.UpdateRenderMetrics(metrics.logicalWidth, metrics.logicalHeight, metrics.dpiScaleX, metrics.dpiScaleY, metrics.pixelWidth, metrics.pixelHeight);
        var channel = SpoutChannelTextBox.Text.Trim();
        if (string.IsNullOrEmpty(channel))
        {
            channel = "VsfClone";
            SpoutChannelTextBox.Text = channel;
        }
        _ = _controller.StartSpout(metrics.pixelWidth, metrics.pixelHeight, 60, channel);
    }

    private void StopSpout_Click(object sender, RoutedEventArgs e)
    {
        if (!Confirm("Stop Spout output?"))
        {
            return;
        }
        _ = _controller.StopSpout();
    }

    private void StartOsc_Click(object sender, RoutedEventArgs e)
    {
        if (!ushort.TryParse(OscBindPortTextBox.Text.Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out var bindPort))
        {
            MessageBox.Show(this, "OSC bind port must be an integer between 0 and 65535.", "Invalid Input", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        if (_controller.Outputs.OscActive &&
            !Confirm("OSC is active. Restart with current settings?"))
        {
            return;
        }

        if (_controller.Outputs.OscActive)
        {
            _ = _controller.StopOsc();
        }

        var publishAddress = OscPublishAddressTextBox.Text.Trim();
        _ = _controller.StartOsc(bindPort, publishAddress);
    }

    private void StopOsc_Click(object sender, RoutedEventArgs e)
    {
        if (!Confirm("Stop OSC output?"))
        {
            return;
        }
        _ = _controller.StopOsc();
    }

    private void CopyLogs_Click(object sender, RoutedEventArgs e)
    {
        Clipboard.SetText(LogsTextBox.Text);
    }

    private void Timer_Tick(object? sender, EventArgs e)
    {
        if (!_controller.SessionState.IsInitialized || !_controller.SessionState.IsWindowAttached)
        {
            return;
        }

        var elapsed = _frameTimer.Elapsed;
        _frameTimer.Restart();
        _ = _controller.Tick((float)elapsed.TotalSeconds);
    }

    private void ResizeTimer_Tick(object? sender, EventArgs e)
    {
        _resizeTimer.Stop();
        var state = _controller.SessionState;
        if (!state.IsInitialized || !state.IsWindowAttached)
        {
            return;
        }

        var metrics = GetRenderMetrics();
        _controller.UpdateRenderMetrics(metrics.logicalWidth, metrics.logicalHeight, metrics.dpiScaleX, metrics.dpiScaleY, metrics.pixelWidth, metrics.pixelHeight);
        _ = _controller.ResizeWindow(metrics.pixelWidth, metrics.pixelHeight);
    }

    private void Controller_StateChanged(object? sender, EventArgs e) => RefreshAll();
    private void Controller_DiagnosticsUpdated(object? sender, EventArgs e) => RefreshAll();

    private void Controller_ErrorRaised(object? sender, HostLogEntry e)
    {
        ErrorStatusText.Text = $"{e.Source}: {e.ResultCode}";
    }

    private void RefreshAll()
    {
        UpdateRenderMetricsFromHost();
        UpdateUiState();
        UpdateDiagnostics();
    }

    private void UpdateUiState()
    {
        var session = _controller.SessionState;
        var outputs = _controller.Outputs;
        var hasAvatar = session.ActiveAvatarHandle.HasValue;

        InitializeButton.IsEnabled = !session.IsInitialized;
        ShutdownButton.IsEnabled = session.IsInitialized;
        BrowseAvatarButton.IsEnabled = session.IsInitialized;
        LoadButton.IsEnabled = session.IsInitialized;
        UnloadButton.IsEnabled = session.IsInitialized && hasAvatar;
        StartSpoutButton.IsEnabled = session.IsInitialized && hasAvatar && !outputs.SpoutActive;
        StopSpoutButton.IsEnabled = outputs.SpoutActive;
        StartOscButton.IsEnabled = session.IsInitialized && hasAvatar && !outputs.OscActive;
        StopOscButton.IsEnabled = outputs.OscActive;

        SessionStatusText.Text = session.IsInitialized ? "Initialized" : "Stopped";
        AvatarStatusText.Text = hasAvatar ? "Loaded" : "None";
        RenderStatusText.Text = $"{session.LastRenderRc} {session.RenderWidthPx}x{session.RenderHeightPx}";
        OutputStatusText.Text = $"Spout={(outputs.SpoutActive ? "On" : "Off")} OSC={(outputs.OscActive ? "On" : "Off")}";
    }

    private void UpdateDiagnostics()
    {
        var snapshot = _controller.LastSnapshot;
        var runtime = snapshot.Runtime;
        var avatarInfo = snapshot.AvatarInfo;

        FrameStatusText.Text = $"{runtime.LastFrameMs:F2} ms";
        ErrorStatusText.Text = runtime.LastError;

        var runtimeSb = new StringBuilder();
        runtimeSb.AppendLine($"TimestampUtc: {snapshot.TimestampUtc:O}");
        runtimeSb.AppendLine($"RenderReadyAvatars: {runtime.RenderReadyAvatarCount}");
        runtimeSb.AppendLine($"AutoQuality: logical={snapshot.Session.LogicalWidth:F1}x{snapshot.Session.LogicalHeight:F1}, dpi={snapshot.Session.DpiScaleX:F2}x{snapshot.Session.DpiScaleY:F2}, render={snapshot.Session.RenderWidthPx}x{snapshot.Session.RenderHeightPx}");
        runtimeSb.AppendLine($"SpoutActive: {runtime.SpoutActive}");
        runtimeSb.AppendLine($"OscActive: {runtime.OscActive}");
        runtimeSb.AppendLine($"LastFrameMs: {runtime.LastFrameMs:F3}");
        runtimeSb.AppendLine($"RenderRc: {snapshot.LastRenderRc}");
        runtimeSb.AppendLine($"LastError: {runtime.LastError}");
        RuntimeDiagnosticsTextBox.Text = runtimeSb.ToString();

        var avatarSb = new StringBuilder();
        avatarSb.AppendLine($"AvatarHandle: {snapshot.Session.ActiveAvatarHandle?.ToString() ?? "none"}");
        if (avatarInfo.HasValue)
        {
            var info = avatarInfo.Value;
            avatarSb.AppendLine($"DisplayName: {info.DisplayName}");
            avatarSb.AppendLine($"Format: {info.DetectedFormat}");
            avatarSb.AppendLine($"ParserStage: {info.ParserStage}");
            avatarSb.AppendLine($"PrimaryErrorCode: {info.PrimaryErrorCode}");
            avatarSb.AppendLine($"MeshPayloads: {info.MeshPayloadCount}");
            avatarSb.AppendLine($"MaterialPayloads: {info.MaterialPayloadCount}");
            avatarSb.AppendLine($"TexturePayloads: {info.TexturePayloadCount}");
            avatarSb.AppendLine($"Expressions: {info.ExpressionCount}");
            avatarSb.AppendLine($"DrawCalls: {info.LastRenderDrawCalls}");
            avatarSb.AppendLine($"ExpressionSummary: {info.LastExpressionSummary}");
            avatarSb.AppendLine($"LastWarning: {info.LastWarning}");
            avatarSb.AppendLine($"LastMissingFeature: {info.LastMissingFeature}");
        }
        AvatarDiagnosticsTextBox.Text = avatarSb.ToString();

        var logsSb = new StringBuilder();
        foreach (var log in _controller.LogEntries)
        {
            logsSb.AppendLine($"{log.TimestampUtc:HH:mm:ss.fff} [{log.Source}] {log.ResultCode} {log.Message}");
        }
        LogsTextBox.Text = logsSb.ToString();
    }

    private bool Confirm(string message)
    {
        var result = MessageBox.Show(this, message, "Confirm Action", MessageBoxButton.YesNo, MessageBoxImage.Question);
        return result == MessageBoxResult.Yes;
    }

    private void UpdateRenderMetricsFromHost()
    {
        if (!IsLoaded)
        {
            return;
        }

        var metrics = GetRenderMetrics();
        _controller.UpdateRenderMetrics(metrics.logicalWidth, metrics.logicalHeight, metrics.dpiScaleX, metrics.dpiScaleY, metrics.pixelWidth, metrics.pixelHeight);
    }

    private (uint pixelWidth, uint pixelHeight, double logicalWidth, double logicalHeight, double dpiScaleX, double dpiScaleY) GetRenderMetrics()
    {
        var logicalWidth = Math.Max(1.0, RenderHost.ActualWidth);
        var logicalHeight = Math.Max(1.0, RenderHost.ActualHeight);
        var dpi = VisualTreeHelper.GetDpi(RenderHost);
        var dpiScaleX = dpi.DpiScaleX > 0.0 ? dpi.DpiScaleX : 1.0;
        var dpiScaleY = dpi.DpiScaleY > 0.0 ? dpi.DpiScaleY : 1.0;
        var pixelWidth = (uint)Math.Max(1.0, Math.Round(logicalWidth * dpiScaleX));
        var pixelHeight = (uint)Math.Max(1.0, Math.Round(logicalHeight * dpiScaleY));
        return (pixelWidth, pixelHeight, logicalWidth, logicalHeight, dpiScaleX, dpiScaleY);
    }
}
