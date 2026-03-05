using System;
using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Threading.Tasks;
using HostCore;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Windows.Storage.Pickers;
using WinRT.Interop;

namespace WinUiHost;

public sealed partial class MainWindow : Window
{
    private readonly HostController _controller = new();
    private readonly Stopwatch _frameTimer = Stopwatch.StartNew();
    private readonly DispatcherQueueTimer _timer;
    private readonly DispatcherQueueTimer _uiRefreshTimer;
    private readonly DispatcherQueueTimer _resizeTimer;
    private readonly DispatcherQueueTimer _renderApplyTimer;
    private bool _isSyncingRenderUi;
    private bool _isSyncingPresetUi;
    private bool _isLogsTabActive;
    private bool _pendingUiStateRefresh;
    private bool _pendingRuntimeRefresh;
    private bool _pendingAvatarRefresh;
    private bool _pendingLogsRefresh;
    private long _lastRuntimeSnapshotVersion = -1;
    private long _lastAvatarSnapshotVersion = -1;
    private long _lastLogVersion = -1;
    private string _lastRuntimeText = string.Empty;
    private string _lastAvatarText = string.Empty;
    private string _lastLogsText = string.Empty;
    private readonly IntPtr _hwnd;
    private HostValidationState _validationState = new(true, true, true, string.Empty, string.Empty, string.Empty);

    public MainWindow()
    {
        InitializeComponent();
        _hwnd = WindowNative.GetWindowHandle(this);
        _timer = DispatcherQueue.GetForCurrentThread().CreateTimer();
        _timer.Interval = TimeSpan.FromMilliseconds(16.0);
        _timer.Tick += Timer_Tick;
        _uiRefreshTimer = DispatcherQueue.GetForCurrentThread().CreateTimer();
        _uiRefreshTimer.Interval = TimeSpan.FromMilliseconds(100.0);
        _uiRefreshTimer.Tick += UiRefreshTimer_Tick;
        _resizeTimer = DispatcherQueue.GetForCurrentThread().CreateTimer();
        _resizeTimer.Interval = TimeSpan.FromMilliseconds(90.0);
        _resizeTimer.IsRepeating = false;
        _resizeTimer.Tick += ResizeTimer_Tick;
        _renderApplyTimer = DispatcherQueue.GetForCurrentThread().CreateTimer();
        _renderApplyTimer.Interval = TimeSpan.FromMilliseconds(100.0);
        _renderApplyTimer.IsRepeating = false;
        _renderApplyTimer.Tick += RenderApplyTimer_Tick;
        Closed += MainWindow_Closed;
        RenderHost.SizeChanged += RenderHost_SizeChanged;

        _controller.StateChanged += Controller_StateChanged;
        _controller.DiagnosticsUpdated += Controller_DiagnosticsUpdated;
        _controller.ErrorRaised += Controller_ErrorRaised;
        _isLogsTabActive = DiagnosticsTabControl.SelectedIndex == 2;
        RefreshValidationState();
        SyncRenderControlsFromState();
        MarkAllDirty(includeLogs: true);
        ProcessPendingUpdates(force: true);
        _uiRefreshTimer.Start();
    }

    private void MainWindow_Closed(object sender, WindowEventArgs args)
    {
        _timer.Stop();
        _uiRefreshTimer.Stop();
        _resizeTimer.Stop();
        _renderApplyTimer.Stop();
        _ = _controller.Shutdown();
    }

    private async void Initialize_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

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
            var metrics = GetRenderMetrics();
            _controller.UpdateRenderMetrics(metrics.logicalWidth, metrics.logicalHeight, metrics.dpiScaleX, metrics.dpiScaleY, metrics.pixelWidth, metrics.pixelHeight);
            _ = PushRenderUiState();
            var attachRc = _controller.AttachWindow(_hwnd, metrics.pixelWidth, metrics.pixelHeight);
            if (attachRc == NcResultCode.Ok)
            {
                _timer.Start();
            }
        }

        MarkAllDirty(includeLogs: true);
        ProcessPendingUpdates(force: true);
    }

    private async void Shutdown_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        if (!await ConfirmAsync("Shutdown runtime and stop rendering/outputs?"))
        {
            return;
        }

        _timer.Stop();
        _ = _controller.Shutdown();
        MarkAllDirty(includeLogs: true);
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

    private void InputTextChanged(object sender, TextChangedEventArgs e)
    {
        RefreshValidationState();
        UpdateUiState();
    }

    private void Load_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        RefreshValidationState();
        if (!_controller.SessionState.IsInitialized)
        {
            _ = ShowMessageAsync("Load Blocked", "Initialize the session first.");
            return;
        }
        if (!_validationState.AvatarPathValid)
        {
            _ = ShowMessageAsync("Invalid Input", _validationState.AvatarPathError);
            return;
        }

        _ = _controller.LoadAvatar(AvatarPathTextBox.Text.Trim());
    }

    private async void Unload_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        if (!await ConfirmAsync("Unload active avatar?"))
        {
            return;
        }
        _ = _controller.UnloadAvatar();
    }

    private async void StartSpout_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        if (_controller.Outputs.SpoutActive &&
            !await ConfirmAsync("Spout is active. Restart with current settings?"))
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

    private async void StopSpout_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        if (!await ConfirmAsync("Stop Spout output?"))
        {
            return;
        }
        _ = _controller.StopSpout();
    }

    private async void StartOsc_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        RefreshValidationState();
        if (!_validationState.OscBindPortValid)
        {
            await ShowMessageAsync("Invalid Input", _validationState.OscBindPortError);
            return;
        }
        if (!_validationState.OscPublishAddressValid)
        {
            await ShowMessageAsync("Invalid Input", _validationState.OscPublishAddressError);
            return;
        }

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
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

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

    private void BroadcastMode_Changed(object sender, RoutedEventArgs e)
    {
        if (ShouldSkipRenderInteraction())
        {
            return;
        }

        _ = _controller.SetBroadcastMode(BroadcastModeCheckBox.IsChecked == true);
        QueueRenderApply();
    }

    private void CameraMode_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (ShouldSkipRenderInteraction())
        {
            return;
        }
        UpdateUiState();
        QueueRenderApply();
    }

    private void FramingSlider_ValueChanged(object sender, RangeBaseValueChangedEventArgs e)
    {
        if (FramingValueText is not null && FramingSlider is not null)
        {
            FramingValueText.Text = FramingSlider.Value.ToString("F2", CultureInfo.InvariantCulture);
        }
        if (ShouldSkipRenderInteraction())
        {
            return;
        }
        QueueRenderApply();
    }

    private void HeadroomSlider_ValueChanged(object sender, RangeBaseValueChangedEventArgs e)
    {
        if (HeadroomValueText is not null && HeadroomSlider is not null)
        {
            HeadroomValueText.Text = HeadroomSlider.Value.ToString("F2", CultureInfo.InvariantCulture);
        }
        if (ShouldSkipRenderInteraction())
        {
            return;
        }
        QueueRenderApply();
    }

    private void YawSlider_ValueChanged(object sender, RangeBaseValueChangedEventArgs e)
    {
        if (YawValueText is not null && YawSlider is not null)
        {
            YawValueText.Text = YawSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
        }
        if (ShouldSkipRenderInteraction())
        {
            return;
        }
        QueueRenderApply();
    }

    private void FovSlider_ValueChanged(object sender, RangeBaseValueChangedEventArgs e)
    {
        if (FovValueText is not null && FovSlider is not null)
        {
            FovValueText.Text = FovSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
        }
        if (ShouldSkipRenderInteraction())
        {
            return;
        }
        QueueRenderApply();
    }

    private void BackgroundPreset_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (ShouldSkipRenderInteraction())
        {
            return;
        }
        QueueRenderApply();
    }

    private void MirrorMode_Changed(object sender, RoutedEventArgs e)
    {
        if (ShouldSkipRenderInteraction())
        {
            return;
        }
        QueueRenderApply();
    }

    private void DebugOverlay_Changed(object sender, RoutedEventArgs e)
    {
        if (ShouldSkipRenderInteraction())
        {
            return;
        }
        QueueRenderApply();
    }

    private async void SavePreset_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        var name = PresetNameTextBox.Text.Trim();
        if (string.IsNullOrWhiteSpace(name))
        {
            await ShowMessageAsync("Invalid Input", "Preset name is required.");
            return;
        }

        if (_controller.SaveOrUpdateRenderPreset(name))
        {
            SyncPresetControlsFromState();
        }
    }

    private void ApplyPreset_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        if (PresetComboBox.SelectedItem is not string name || string.IsNullOrWhiteSpace(name))
        {
            return;
        }

        _ = _controller.ApplyRenderPreset(name);
        SyncPresetControlsFromState();
    }

    private async void DeletePreset_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        if (PresetComboBox.SelectedItem is not string name || string.IsNullOrWhiteSpace(name))
        {
            return;
        }

        if (!_controller.DeleteRenderPreset(name))
        {
            await ShowMessageAsync("Delete Blocked", "Cannot delete preset. At least one preset must remain.");
            return;
        }

        SyncPresetControlsFromState();
    }

    private void ResetRender_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        _ = _controller.ResetRenderDefaults();
        SyncPresetControlsFromState();
    }

    private void RenderApplyTimer_Tick(DispatcherQueueTimer sender, object args)
    {
        _renderApplyTimer.Stop();
        _ = PushRenderUiState();
    }

    private void QueueRenderApply()
    {
        _renderApplyTimer.Stop();
        _renderApplyTimer.Start();
    }

    private void RenderHost_SizeChanged(object sender, SizeChangedEventArgs e)
    {
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

    private void ResizeTimer_Tick(DispatcherQueueTimer sender, object args)
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

    private void DiagnosticsTabControl_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (sender is not TabView tabView)
        {
            return;
        }

        _isLogsTabActive = tabView.SelectedIndex == 2;
        if (_isLogsTabActive)
        {
            _pendingLogsRefresh = true;
            ProcessPendingUpdates(force: false);
        }
    }

    private void UiRefreshTimer_Tick(DispatcherQueueTimer sender, object args)
    {
        ProcessPendingUpdates(force: false);
    }

    private void Controller_StateChanged(object? sender, EventArgs e)
    {
        _pendingUiStateRefresh = true;
        _pendingRuntimeRefresh = true;
        _pendingAvatarRefresh = true;
        _pendingLogsRefresh = true;
    }

    private void Controller_DiagnosticsUpdated(object? sender, EventArgs e)
    {
        _pendingRuntimeRefresh = true;
        _pendingAvatarRefresh = true;
        _pendingLogsRefresh = true;
    }

    private void Controller_ErrorRaised(object? sender, HostLogEntry e)
    {
        ErrorStatusText.Text = $"LastError: {e.Source} {e.ResultCode}";
        _pendingLogsRefresh = true;
    }

    private void MarkAllDirty(bool includeLogs)
    {
        _pendingUiStateRefresh = true;
        _pendingRuntimeRefresh = true;
        _pendingAvatarRefresh = true;
        _pendingLogsRefresh = includeLogs;
    }

    private void ProcessPendingUpdates(bool force)
    {
        if (_pendingUiStateRefresh || force)
        {
            UpdateRenderMetricsFromHost();
            RefreshValidationState();
            UpdateUiState();
            _pendingUiStateRefresh = false;
        }

        UpdateDiagnostics(force);
    }

    private void UpdateUiState()
    {
        var session = _controller.SessionState;
        var outputs = _controller.Outputs;
        var operation = _controller.OperationState;
        var hasAvatar = session.ActiveAvatarHandle.HasValue;
        var isBusy = operation.IsBusy;

        InitializeButton.IsEnabled = !session.IsInitialized && !isBusy;
        ShutdownButton.IsEnabled = session.IsInitialized && !isBusy;
        BrowseAvatarButton.IsEnabled = session.IsInitialized && !isBusy;
        LoadButton.IsEnabled = session.IsInitialized && !isBusy && _validationState.AvatarPathValid;
        UnloadButton.IsEnabled = session.IsInitialized && hasAvatar && !isBusy;
        StartSpoutButton.IsEnabled = session.IsInitialized && hasAvatar && !outputs.SpoutActive && !isBusy;
        StopSpoutButton.IsEnabled = outputs.SpoutActive && !isBusy;
        StartOscButton.IsEnabled = session.IsInitialized && hasAvatar && !outputs.OscActive && !isBusy && _validationState.OscBindPortValid && _validationState.OscPublishAddressValid;
        StopOscButton.IsEnabled = outputs.OscActive && !isBusy;
        var renderControlsEnabled = session.IsInitialized && !isBusy;
        var manualCameraMode = CameraModeComboBox.SelectedIndex == 2 || _controller.RenderState.CameraMode == RenderCameraMode.Manual;
        BroadcastModeCheckBox.IsEnabled = renderControlsEnabled;
        CameraModeComboBox.IsEnabled = renderControlsEnabled;
        FramingSlider.IsEnabled = renderControlsEnabled;
        HeadroomSlider.IsEnabled = renderControlsEnabled;
        YawSlider.IsEnabled = renderControlsEnabled && manualCameraMode;
        FovSlider.IsEnabled = renderControlsEnabled && manualCameraMode;
        BackgroundPresetComboBox.IsEnabled = renderControlsEnabled;
        MirrorModeCheckBox.IsEnabled = renderControlsEnabled;
        DebugOverlayCheckBox.IsEnabled = renderControlsEnabled;
        SavePresetButton.IsEnabled = renderControlsEnabled;
        ApplyPresetButton.IsEnabled = renderControlsEnabled;
        DeletePresetButton.IsEnabled = renderControlsEnabled;
        ResetRenderButton.IsEnabled = renderControlsEnabled;
        PresetNameTextBox.IsEnabled = renderControlsEnabled;
        PresetComboBox.IsEnabled = renderControlsEnabled;

        SessionStatusText.Text = $"Session: {(session.IsInitialized ? "Initialized" : "Stopped")}";
        AvatarStatusText.Text = $"Avatar: {(hasAvatar ? "Loaded" : "None")}";
        RenderStatusText.Text = $"Render: {session.LastRenderRc} {session.RenderWidthPx}x{session.RenderHeightPx}";
        OutputStatusText.Text = $"Outputs: Spout={(outputs.SpoutActive ? "On" : "Off")} OSC={(outputs.OscActive ? "On" : "Off")}";
        BusyStatusText.Text = $"Busy: {(isBusy ? operation.CurrentOperation : "Idle")}";
        SyncRenderControlsFromState();
        SyncPresetControlsFromState();
    }

    private bool ShouldSkipRenderInteraction()
    {
        return _isSyncingRenderUi || _controller.OperationState.IsBusy;
    }

    private void RefreshValidationState()
    {
        _validationState = _controller.ValidateInputs(
            AvatarPathTextBox.Text,
            OscBindPortTextBox.Text,
            OscPublishAddressTextBox.Text);

        AvatarPathValidationText.Text = _validationState.AvatarPathValid ? string.Empty : _validationState.AvatarPathError;
        OscBindValidationText.Text = _validationState.OscBindPortValid ? string.Empty : _validationState.OscBindPortError;
        OscPublishValidationText.Text = _validationState.OscPublishAddressValid ? string.Empty : _validationState.OscPublishAddressError;
    }

    private void UpdateDiagnostics(bool force)
    {
        var snapshot = _controller.LastSnapshot;
        var runtime = snapshot.Runtime;

        FrameStatusText.Text = $"Frame: {runtime.LastFrameMs:F2} ms";
        ErrorStatusText.Text = $"LastError: {runtime.LastError}";

        var runtimeChanged = force || _pendingRuntimeRefresh || snapshot.SnapshotVersion != _lastRuntimeSnapshotVersion;
        if (runtimeChanged)
        {
            var runtimeText = BuildRuntimeText(snapshot);
            if (!string.Equals(runtimeText, _lastRuntimeText, StringComparison.Ordinal))
            {
                RuntimeDiagnosticsTextBox.Text = runtimeText;
                DebugOverlayText.Text = runtimeText;
                _lastRuntimeText = runtimeText;
            }

            DebugOverlayPanel.Visibility = snapshot.Render.ShowDebugOverlay ? Visibility.Visible : Visibility.Collapsed;
            _lastRuntimeSnapshotVersion = snapshot.SnapshotVersion;
            _pendingRuntimeRefresh = false;
        }

        var avatarChanged = force || _pendingAvatarRefresh || snapshot.SnapshotVersion != _lastAvatarSnapshotVersion;
        if (avatarChanged)
        {
            var avatarText = BuildAvatarText(snapshot);
            if (!string.Equals(avatarText, _lastAvatarText, StringComparison.Ordinal))
            {
                AvatarDiagnosticsTextBox.Text = avatarText;
                _lastAvatarText = avatarText;
            }

            _lastAvatarSnapshotVersion = snapshot.SnapshotVersion;
            _pendingAvatarRefresh = false;
        }

        if ((force || _isLogsTabActive) && (force || _pendingLogsRefresh || snapshot.LogVersion != _lastLogVersion))
        {
            var logsText = BuildLogsText();
            if (!string.Equals(logsText, _lastLogsText, StringComparison.Ordinal))
            {
                LogsTextBox.Text = logsText;
                _lastLogsText = logsText;
            }

            _lastLogVersion = snapshot.LogVersion;
            _pendingLogsRefresh = false;
        }
    }

    private string BuildRuntimeText(DiagnosticsSnapshot snapshot)
    {
        var runtime = snapshot.Runtime;
        var runtimeSb = new StringBuilder();
        runtimeSb.AppendLine($"TimestampUtc: {snapshot.TimestampUtc:O}");
        runtimeSb.AppendLine($"SnapshotVersion: {snapshot.SnapshotVersion}");
        runtimeSb.AppendLine($"LogVersion: {snapshot.LogVersion}");
        runtimeSb.AppendLine($"RenderReadyAvatars: {runtime.RenderReadyAvatarCount}");
        runtimeSb.AppendLine($"AutoQuality: logical={snapshot.Session.LogicalWidth:F1}x{snapshot.Session.LogicalHeight:F1}, dpi={snapshot.Session.DpiScaleX:F2}x{snapshot.Session.DpiScaleY:F2}, render={snapshot.Session.RenderWidthPx}x{snapshot.Session.RenderHeightPx}");
        runtimeSb.AppendLine($"RenderUi: mode={snapshot.Render.CameraMode}, framing={snapshot.Render.FramingTarget:F2}, headroom={snapshot.Render.Headroom:F2}, yaw={snapshot.Render.YawDeg:F0}, fov={snapshot.Render.FovDeg:F0}, bg={snapshot.Render.BackgroundPreset}, mirror={snapshot.Render.MirrorMode}, debug={snapshot.Render.ShowDebugOverlay}");
        runtimeSb.AppendLine($"SpoutActive: {runtime.SpoutActive}");
        runtimeSb.AppendLine($"OscActive: {runtime.OscActive}");
        runtimeSb.AppendLine($"LastFrameMs: {runtime.LastFrameMs:F3}");
        runtimeSb.AppendLine($"RenderRc: {snapshot.LastRenderRc}");
        runtimeSb.AppendLine($"LastError: {runtime.LastError}");
        return runtimeSb.ToString();
    }

    private string BuildAvatarText(DiagnosticsSnapshot snapshot)
    {
        var avatarSb = new StringBuilder();
        avatarSb.AppendLine($"AvatarHandle: {snapshot.Session.ActiveAvatarHandle?.ToString() ?? "none"}");
        if (snapshot.AvatarInfo.HasValue)
        {
            var info = snapshot.AvatarInfo.Value;
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

        return avatarSb.ToString();
    }

    private string BuildLogsText()
    {
        var logsSb = new StringBuilder();
        foreach (var log in _controller.LogEntries)
        {
            logsSb.AppendLine($"{log.TimestampUtc:HH:mm:ss.fff} [{log.Source}] {log.ResultCode} {log.Message}");
        }

        return logsSb.ToString();
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

    private void UpdateRenderMetricsFromHost()
    {
        var metrics = GetRenderMetrics();
        _controller.UpdateRenderMetrics(metrics.logicalWidth, metrics.logicalHeight, metrics.dpiScaleX, metrics.dpiScaleY, metrics.pixelWidth, metrics.pixelHeight);
    }

    private (uint pixelWidth, uint pixelHeight, double logicalWidth, double logicalHeight, double dpiScaleX, double dpiScaleY) GetRenderMetrics()
    {
        var logicalWidth = Math.Max(1.0, RenderHost.ActualWidth);
        var logicalHeight = Math.Max(1.0, RenderHost.ActualHeight);
        var scale = RenderHost.XamlRoot?.RasterizationScale ?? 1.0;
        var dpiScaleX = scale > 0.0 ? scale : 1.0;
        var dpiScaleY = dpiScaleX;
        var pixelWidth = (uint)Math.Max(1.0, Math.Round(logicalWidth * dpiScaleX));
        var pixelHeight = (uint)Math.Max(1.0, Math.Round(logicalHeight * dpiScaleY));
        return (pixelWidth, pixelHeight, logicalWidth, logicalHeight, dpiScaleX, dpiScaleY);
    }

    private void SyncRenderControlsFromState()
    {
        var render = _controller.RenderState;
        _isSyncingRenderUi = true;
        BroadcastModeCheckBox.IsChecked = render.BroadcastMode;
        CameraModeComboBox.SelectedIndex = render.CameraMode switch
        {
            RenderCameraMode.AutoFitFull => 0,
            RenderCameraMode.Manual => 2,
            _ => 1,
        };
        FramingSlider.Value = render.FramingTarget;
        FramingValueText.Text = render.FramingTarget.ToString("F2", CultureInfo.InvariantCulture);
        HeadroomSlider.Value = render.Headroom;
        HeadroomValueText.Text = render.Headroom.ToString("F2", CultureInfo.InvariantCulture);
        YawSlider.Value = render.YawDeg;
        YawValueText.Text = render.YawDeg.ToString("F0", CultureInfo.InvariantCulture);
        FovSlider.Value = render.FovDeg;
        FovValueText.Text = render.FovDeg.ToString("F0", CultureInfo.InvariantCulture);
        BackgroundPresetComboBox.SelectedIndex = render.BackgroundPreset switch
        {
            BackgroundPreset.NeutralGray => 1,
            BackgroundPreset.GreenScreen => 2,
            _ => 0,
        };
        MirrorModeCheckBox.IsChecked = render.MirrorMode;
        DebugOverlayCheckBox.IsChecked = render.ShowDebugOverlay;
        _isSyncingRenderUi = false;
    }

    private void SyncPresetControlsFromState()
    {
        if (_isSyncingPresetUi)
        {
            return;
        }

        _isSyncingPresetUi = true;
        var selectedName = _controller.SelectedRenderPresetName ?? string.Empty;
        PresetComboBox.Items.Clear();
        foreach (var preset in _controller.RenderPresets)
        {
            PresetComboBox.Items.Add(preset.Name);
        }

        var matched = false;
        foreach (var item in PresetComboBox.Items)
        {
            if (item is string name &&
                string.Equals(name, selectedName, StringComparison.OrdinalIgnoreCase))
            {
                PresetComboBox.SelectedItem = item;
                matched = true;
                break;
            }
        }

        if (!matched && PresetComboBox.Items.Count > 0)
        {
            PresetComboBox.SelectedIndex = 0;
            selectedName = PresetComboBox.SelectedItem as string ?? selectedName;
        }

        if (!string.IsNullOrWhiteSpace(selectedName))
        {
            PresetNameTextBox.Text = selectedName;
        }
        _isSyncingPresetUi = false;
    }

    private NcResultCode PushRenderUiState()
    {
        var preset = BackgroundPresetComboBox.SelectedIndex switch
        {
            1 => BackgroundPreset.NeutralGray,
            2 => BackgroundPreset.GreenScreen,
            _ => BackgroundPreset.DarkBlue,
        };
        var cameraMode = CameraModeComboBox.SelectedIndex switch
        {
            0 => RenderCameraMode.AutoFitFull,
            2 => RenderCameraMode.Manual,
            _ => RenderCameraMode.AutoFitBust,
        };
        var state = _controller.RenderState with
        {
            BroadcastMode = BroadcastModeCheckBox.IsChecked == true,
            CameraMode = cameraMode,
            FramingTarget = (float)FramingSlider.Value,
            Headroom = (float)HeadroomSlider.Value,
            YawDeg = (float)YawSlider.Value,
            FovDeg = (float)FovSlider.Value,
            BackgroundPreset = preset,
            MirrorMode = MirrorModeCheckBox.IsChecked == true,
            ShowDebugOverlay = DebugOverlayCheckBox.IsChecked == true,
        };
        return _controller.ApplyRenderUiState(state);
    }
}
