using System;
using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Threading.Tasks;
using HostCore;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.Storage.Pickers;
using WinRT.Interop;

namespace WinUiHost;

public sealed partial class MainWindow : Window
{
    private readonly HostController _controller = new();
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
        RenderHost.SizeChanged += RenderHost_SizeChanged;

        _controller.StateChanged += Controller_StateChanged;
        _controller.DiagnosticsUpdated += Controller_DiagnosticsUpdated;
        _controller.ErrorRaised += Controller_ErrorRaised;
        RefreshAll();
    }

    private void MainWindow_Closed(object sender, WindowEventArgs args)
    {
        _timer.Stop();
        _ = _controller.Shutdown();
    }

    private async void Initialize_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.SessionState.IsInitialized &&
            !await ConfirmAsync("Session is already initialized. Reinitialize and reset active outputs/avatar?"))
        {
            return;
        }

        if (_controller.SessionState.IsInitialized)
        {
            _timer.Stop();
            _ = _controller.Shutdown();
        }

        var initRc = _controller.Initialize();
        if (initRc == NcResultCode.Ok)
        {
            var width = (uint)Math.Max(1.0, RenderHost.ActualWidth);
            var height = (uint)Math.Max(1.0, RenderHost.ActualHeight);
            var attachRc = _controller.AttachWindow(_hwnd, width, height);
            if (attachRc == NcResultCode.Ok)
            {
                _timer.Start();
            }
        }
    }

    private async void Shutdown_Click(object sender, RoutedEventArgs e)
    {
        if (!await ConfirmAsync("Shutdown runtime and stop rendering/outputs?"))
        {
            return;
        }

        _timer.Stop();
        _ = _controller.Shutdown();
    }

    private async void BrowseAvatar_Click(object sender, RoutedEventArgs e)
    {
        var picker = new FileOpenPicker();
        picker.FileTypeFilter.Add(".vrm");
        picker.FileTypeFilter.Add(".vxavatar");
        picker.FileTypeFilter.Add(".vsfavatar");
        picker.FileTypeFilter.Add(".vxa2");
        picker.FileTypeFilter.Add(".*");
        InitializeWithWindow.Initialize(picker, _hwnd);

        var file = await picker.PickSingleFileAsync();
        if (file is not null)
        {
            AvatarPathTextBox.Text = file.Path;
        }
    }

    private void Load_Click(object sender, RoutedEventArgs e)
    {
        if (!_controller.SessionState.IsInitialized)
        {
            _ = ShowMessageAsync("Load Blocked", "Initialize the session first.");
            return;
        }

        _ = _controller.LoadAvatar(AvatarPathTextBox.Text.Trim());
    }

    private async void Unload_Click(object sender, RoutedEventArgs e)
    {
        if (!await ConfirmAsync("Unload active avatar?"))
        {
            return;
        }
        _ = _controller.UnloadAvatar();
    }

    private async void StartSpout_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.Outputs.SpoutActive &&
            !await ConfirmAsync("Spout is active. Restart with current settings?"))
        {
            return;
        }

        if (_controller.Outputs.SpoutActive)
        {
            _ = _controller.StopSpout();
        }

        var width = (uint)Math.Max(1.0, RenderHost.ActualWidth);
        var height = (uint)Math.Max(1.0, RenderHost.ActualHeight);
        var channel = SpoutChannelTextBox.Text.Trim();
        if (string.IsNullOrEmpty(channel))
        {
            channel = "VsfClone";
            SpoutChannelTextBox.Text = channel;
        }

        _ = _controller.StartSpout(width, height, 60, channel);
    }

    private async void StopSpout_Click(object sender, RoutedEventArgs e)
    {
        if (!await ConfirmAsync("Stop Spout output?"))
        {
            return;
        }
        _ = _controller.StopSpout();
    }

    private async void StartOsc_Click(object sender, RoutedEventArgs e)
    {
        if (!ushort.TryParse(OscBindPortTextBox.Text.Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out var bindPort))
        {
            await ShowMessageAsync("Invalid Input", "OSC bind port must be an integer between 0 and 65535.");
            return;
        }

        if (_controller.Outputs.OscActive &&
            !await ConfirmAsync("OSC is active. Restart with current settings?"))
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

    private async void StopOsc_Click(object sender, RoutedEventArgs e)
    {
        if (!await ConfirmAsync("Stop OSC output?"))
        {
            return;
        }
        _ = _controller.StopOsc();
    }

    private void CopyLogs_Click(object sender, RoutedEventArgs e)
    {
        var package = new Windows.ApplicationModel.DataTransfer.DataPackage();
        package.SetText(LogsTextBox.Text);
        Windows.ApplicationModel.DataTransfer.Clipboard.SetContent(package);
    }

    private void RenderHost_SizeChanged(object sender, SizeChangedEventArgs e)
    {
        var state = _controller.SessionState;
        if (!state.IsInitialized || !state.IsWindowAttached)
        {
            return;
        }

        var width = (uint)Math.Max(1.0, RenderHost.ActualWidth);
        var height = (uint)Math.Max(1.0, RenderHost.ActualHeight);
        _ = _controller.ResizeWindow(width, height);
    }

    private void Timer_Tick(DispatcherQueueTimer sender, object args)
    {
        if (!_controller.SessionState.IsInitialized || !_controller.SessionState.IsWindowAttached)
        {
            return;
        }

        var elapsed = _frameTimer.Elapsed;
        _frameTimer.Restart();
        _ = _controller.Tick((float)elapsed.TotalSeconds);
    }

    private void Controller_StateChanged(object? sender, EventArgs e) => RefreshAll();
    private void Controller_DiagnosticsUpdated(object? sender, EventArgs e) => RefreshAll();

    private void Controller_ErrorRaised(object? sender, HostLogEntry e)
    {
        ErrorStatusText.Text = $"LastError: {e.Source} {e.ResultCode}";
    }

    private void RefreshAll()
    {
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

        SessionStatusText.Text = $"Session: {(session.IsInitialized ? "Initialized" : "Stopped")}";
        AvatarStatusText.Text = $"Avatar: {(hasAvatar ? "Loaded" : "None")}";
        RenderStatusText.Text = $"Render: {session.LastRenderRc}";
        OutputStatusText.Text = $"Outputs: Spout={(outputs.SpoutActive ? "On" : "Off")} OSC={(outputs.OscActive ? "On" : "Off")}";
    }

    private void UpdateDiagnostics()
    {
        var snapshot = _controller.LastSnapshot;
        var runtime = snapshot.Runtime;
        var avatarInfo = snapshot.AvatarInfo;

        FrameStatusText.Text = $"Frame: {runtime.LastFrameMs:F2} ms";
        ErrorStatusText.Text = $"LastError: {runtime.LastError}";

        var runtimeSb = new StringBuilder();
        runtimeSb.AppendLine($"TimestampUtc: {snapshot.TimestampUtc:O}");
        runtimeSb.AppendLine($"RenderReadyAvatars: {runtime.RenderReadyAvatarCount}");
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

    private async Task<bool> ConfirmAsync(string content)
    {
        var xamlRoot = GetXamlRoot();
        if (xamlRoot is null)
        {
            return false;
        }

        var dialog = new ContentDialog
        {
            XamlRoot = xamlRoot,
            Title = "Confirm Action",
            Content = content,
            PrimaryButtonText = "Yes",
            CloseButtonText = "No",
            DefaultButton = ContentDialogButton.Close,
        };
        var result = await dialog.ShowAsync();
        return result == ContentDialogResult.Primary;
    }

    private async Task ShowMessageAsync(string title, string content)
    {
        var xamlRoot = GetXamlRoot();
        if (xamlRoot is null)
        {
            return;
        }

        var dialog = new ContentDialog
        {
            XamlRoot = xamlRoot,
            Title = title,
            Content = content,
            CloseButtonText = "OK",
        };
        await dialog.ShowAsync();
    }

    private Microsoft.UI.Xaml.XamlRoot? GetXamlRoot()
    {
        return Content is FrameworkElement element ? element.XamlRoot : null;
    }
}
