using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using HostCore;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Windows.Graphics;
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
    private bool _isLoadRunning;
    private readonly IntPtr _hwnd;
    private readonly AppWindow? _appWindow;
    private HostValidationState _validationState = new(true, true, true, string.Empty, string.Empty, string.Empty);
    private const int MinWindowWidth = 1240;
    private const int MinWindowHeight = 760;

    public MainWindow()
    {
        InitializeComponent();
        _hwnd = WindowNative.GetWindowHandle(this);
        _appWindow = ConfigureWindowBounds();
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
        _controller.LoadProgressChanged += Controller_LoadProgressChanged;
        _isLogsTabActive = DiagnosticsTabControl.SelectedIndex == 2;
        ApplySessionDefaultsToUi();
        RefreshValidationState();
        SyncRenderControlsFromState();
        MarkAllDirty(includeLogs: true);
        ProcessPendingUpdates(force: true);
        RefreshGuides();
        _uiRefreshTimer.Start();
    }

    private void MainWindow_Closed(object sender, WindowEventArgs args)
    {
        if (_appWindow is not null)
        {
            _appWindow.Changed -= AppWindow_Changed;
        }

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
        picker.FileTypeFilter.Add(".vsfavatar");
        picker.FileTypeFilter.Add(".xav2");
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

    private async void Load_Click(object sender, RoutedEventArgs e)
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

        if (!int.TryParse(LoadTimeoutTextBox.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var timeoutMs))
        {
            timeoutMs = 20000;
            LoadTimeoutTextBox.Text = "20000";
        }

        _isLoadRunning = true;
        UpdateUiState();
        var importPlan = _controller.BuildImportPlan(AvatarPathTextBox.Text.Trim());
        SessionStatusText.Text = $"Session: {importPlan.Guidance} Fallback: {importPlan.Fallback}";
        var rc = await _controller.LoadAvatarAsync(AvatarPathTextBox.Text.Trim(), timeoutMs);
        _isLoadRunning = false;
        UpdateUiState();
        if (rc != NcResultCode.Ok)
        {
            var guidance = _controller.GetLastErrorGuidance();
            var technical = NativeCoreInterop.FormatLastError();
            var detail = string.IsNullOrWhiteSpace(guidance)
                ? technical
                : $"{guidance}\n\n{technical}";
            await ShowMessageAsync("Load Failed", $"Load failed: {rc}\n\n{detail}");
        }
    }

    private void CancelLoad_Click(object sender, RoutedEventArgs e)
    {
        _controller.CancelLoadAvatar();
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

    private async void RunPreflight_Click(object sender, RoutedEventArgs e)
    {
        var preflight = _controller.RunPreflight();
        var sb = new StringBuilder();
        sb.AppendLine($"Preflight: {(preflight.Passed ? "PASS" : "FAIL")}");
        foreach (var c in preflight.Checks)
        {
            sb.AppendLine($"- {(c.Passed ? "PASS" : "FAIL")} [{c.CheckCode}] {c.Name}: {c.Detail}");
            if (!c.Passed)
            {
                sb.AppendLine($"  remediation: {c.Remediation}");
            }
        }

        var failed = preflight.Checks.Where(x => !x.Passed).ToList();
        PreflightHintText.Text = failed.Count == 0
            ? "Preflight passed. Proceed with Initialize -> Load -> Start outputs."
            : "Preflight failed checks: " + string.Join(" | ", failed.Select(x => $"[{x.CheckCode}] {x.Name}: {x.Remediation}"));

        await ShowMessageAsync("Preflight Result", sb.ToString());
    }

    private async void ExportDiag_Click(object sender, RoutedEventArgs e)
    {
        var outputDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VsfCloneHost", "diagnostics");
        var path = _controller.ExportDiagnosticsBundle(outputDir);
        await ShowMessageAsync("Export Diagnostics", $"Diagnostics bundle created:\n{path}");
    }

    private async void ExportMetrics_Click(object sender, RoutedEventArgs e)
    {
        var outputDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VsfCloneHost", "metrics");
        var path = _controller.ExportRollingMetricsCsv(Path.Combine(outputDir, $"metrics_{DateTimeOffset.UtcNow:yyyyMMdd_HHmmss}.csv"));
        await ShowMessageAsync("Export Metrics", $"Metrics exported:\n{path}");
    }

    private void ProfileQuality_Click(object sender, RoutedEventArgs e)
    {
        _ = _controller.ApplyRenderProfile("quality");
    }

    private void ProfilePerformance_Click(object sender, RoutedEventArgs e)
    {
        _ = _controller.ApplyRenderProfile("performance");
    }

    private void ProfileStability_Click(object sender, RoutedEventArgs e)
    {
        _ = _controller.ApplyRenderProfile("stability");
    }

    private void ApplySidecar_Click(object sender, RoutedEventArgs e)
    {
        var mode = ParserModeComboBox.SelectedItem as string ?? "sidecar";
        if (!int.TryParse(SidecarTimeoutTextBox.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var timeoutMs))
        {
            timeoutMs = 15000;
            SidecarTimeoutTextBox.Text = "15000";
        }

        _controller.ConfigureSidecarSettings(new SidecarSettings(
            ParserMode: mode,
            SidecarPath: SidecarPathTextBox.Text.Trim(),
            TimeoutMs: timeoutMs,
            StrictMode: SidecarStrictCheckBox.IsChecked == true));
    }

    private void ApplyTelemetry_Click(object sender, RoutedEventArgs e)
    {
        _controller.SetTelemetryPolicy(
            TelemetryOptInCheckBox.IsChecked == true,
            TelemetryRedactCheckBox.IsChecked == true);
    }

    private async void ExportTelemetry_Click(object sender, RoutedEventArgs e)
    {
        var outputDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VsfCloneHost", "telemetry");
        var path = _controller.ExportTelemetry(Path.Combine(outputDir, $"telemetry_{DateTimeOffset.UtcNow:yyyyMMdd_HHmmss}.json"));
        await ShowMessageAsync("Export Telemetry", $"Telemetry exported:\n{path}");
    }

    private void ApplyAutoQualityPolicy_Click(object sender, RoutedEventArgs e)
    {
        if (!float.TryParse(AutoQualityThresholdTextBox.Text.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out var threshold))
        {
            threshold = 28.0f;
            AutoQualityThresholdTextBox.Text = "28.0";
        }
        if (!int.TryParse(AutoQualityConsecutiveTextBox.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var consecutive))
        {
            consecutive = 120;
            AutoQualityConsecutiveTextBox.Text = "120";
        }
        if (!int.TryParse(AutoQualityCooldownTextBox.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var cooldown))
        {
            cooldown = 30;
            AutoQualityCooldownTextBox.Text = "30";
        }

        if (!float.TryParse(AutoQualityRecoveryThresholdTextBox.Text.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out var recoveryThreshold))
        {
            recoveryThreshold = 22.0f;
            AutoQualityRecoveryThresholdTextBox.Text = "22.0";
        }
        if (!int.TryParse(AutoQualityRecoveryConsecutiveTextBox.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var recoveryConsecutive))
        {
            recoveryConsecutive = 240;
            AutoQualityRecoveryConsecutiveTextBox.Text = "240";
        }

        _controller.ConfigureAutoQualityPolicy(new AutoQualityPolicy(threshold, consecutive, cooldown, recoveryThreshold, recoveryConsecutive));
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
        var guide = _controller.GetLastErrorGuidance();
        if (!string.IsNullOrWhiteSpace(guide))
        {
            PreflightHintText.Text = guide;
        }
    }

    private void Controller_LoadProgressChanged(object? sender, LoadProgressState e)
    {
        DispatcherQueue.TryEnqueue(() =>
        {
            LoadProgressBar.Value = e.Percent;
            LoadProgressText.Text = $"Load progress [{e.OperationId}]: {e.Stage} ({e.Percent}%) - {e.Message}";
            if (e.IsTerminal)
            {
                _isLoadRunning = false;
                UpdateUiState();
            }
        });
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
        var uiState = HostUiPolicy.EvaluateAvailability(
            session,
            outputs,
            operation,
            _validationState,
            _controller.RenderState,
            CameraModeComboBox.SelectedIndex == 2);
        var statusText = HostUiPolicy.BuildStatusText(session, outputs, operation);

        InitializeButton.IsEnabled = uiState.InitializeEnabled;
        ShutdownButton.IsEnabled = uiState.ShutdownEnabled;
        BrowseAvatarButton.IsEnabled = uiState.BrowseAvatarEnabled;
        LoadButton.IsEnabled = uiState.LoadEnabled && !_isLoadRunning;
        CancelLoadButton.IsEnabled = _isLoadRunning;
        UnloadButton.IsEnabled = uiState.UnloadEnabled;
        StartSpoutButton.IsEnabled = uiState.StartSpoutEnabled;
        StopSpoutButton.IsEnabled = uiState.StopSpoutEnabled;
        StartOscButton.IsEnabled = uiState.StartOscEnabled;
        StopOscButton.IsEnabled = uiState.StopOscEnabled;

        BroadcastModeCheckBox.IsEnabled = uiState.RenderControlsEnabled;
        CameraModeComboBox.IsEnabled = uiState.RenderControlsEnabled;
        FramingSlider.IsEnabled = uiState.RenderControlsEnabled;
        HeadroomSlider.IsEnabled = uiState.RenderControlsEnabled;
        YawSlider.IsEnabled = uiState.RenderControlsEnabled && uiState.ManualCameraMode;
        FovSlider.IsEnabled = uiState.RenderControlsEnabled && uiState.ManualCameraMode;
        BackgroundPresetComboBox.IsEnabled = uiState.RenderControlsEnabled;
        MirrorModeCheckBox.IsEnabled = uiState.RenderControlsEnabled;
        DebugOverlayCheckBox.IsEnabled = uiState.RenderControlsEnabled;
        SavePresetButton.IsEnabled = uiState.RenderControlsEnabled;
        ApplyPresetButton.IsEnabled = uiState.RenderControlsEnabled;
        DeletePresetButton.IsEnabled = uiState.RenderControlsEnabled;
        ResetRenderButton.IsEnabled = uiState.RenderControlsEnabled;
        PresetNameTextBox.IsEnabled = uiState.RenderControlsEnabled;
        PresetComboBox.IsEnabled = uiState.RenderControlsEnabled;
        RunPreflightButton.IsEnabled = !operation.IsBusy;
        ExportDiagButton.IsEnabled = !operation.IsBusy;
        ExportMetricsButton.IsEnabled = !operation.IsBusy;
        ParserModeComboBox.IsEnabled = !operation.IsBusy;
        SidecarPathTextBox.IsEnabled = !operation.IsBusy;
        SidecarTimeoutTextBox.IsEnabled = !operation.IsBusy;
        SidecarStrictCheckBox.IsEnabled = !operation.IsBusy;
        TelemetryOptInCheckBox.IsEnabled = !operation.IsBusy;
        TelemetryRedactCheckBox.IsEnabled = !operation.IsBusy;
        LoadTimeoutTextBox.IsEnabled = !operation.IsBusy && !_isLoadRunning;

        SessionStatusText.Text = $"Session: {statusText.SessionText}";
        AvatarStatusText.Text = $"Avatar: {statusText.AvatarText}";
        RenderStatusText.Text = $"Render: {statusText.RenderText}";
        OutputStatusText.Text = $"Outputs: {statusText.OutputText}";
        BusyStatusText.Text = $"Busy: {statusText.BusyText}";
        TrackStatusText.Text = $"Track: {_controller.GetReleaseTrackStatus()}";
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

    private AppWindow? ConfigureWindowBounds()
    {
        var windowId = Win32Interop.GetWindowIdFromWindow(_hwnd);
        var appWindow = AppWindow.GetFromWindowId(windowId);
        if (appWindow is null)
        {
            return null;
        }

        appWindow.Changed += AppWindow_Changed;
        var targetWidth = Math.Max(MinWindowWidth, appWindow.Size.Width);
        var targetHeight = Math.Max(MinWindowHeight, appWindow.Size.Height);
        appWindow.Resize(new SizeInt32(targetWidth, targetHeight));
        return appWindow;
    }

    private void AppWindow_Changed(AppWindow sender, AppWindowChangedEventArgs args)
    {
        if (!args.DidSizeChange)
        {
            return;
        }

        var targetWidth = Math.Max(MinWindowWidth, sender.Size.Width);
        var targetHeight = Math.Max(MinWindowHeight, sender.Size.Height);
        if (targetWidth == sender.Size.Width && targetHeight == sender.Size.Height)
        {
            return;
        }

        sender.Resize(new SizeInt32(targetWidth, targetHeight));
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

    private void ApplySessionDefaultsToUi()
    {
        var session = _controller.SessionPersistence;
        if (!string.IsNullOrWhiteSpace(session.AvatarPath))
        {
            AvatarPathTextBox.Text = session.AvatarPath;
        }

        SpoutChannelTextBox.Text = session.SpoutChannelName;
        OscBindPortTextBox.Text = session.OscBindPort.ToString(CultureInfo.InvariantCulture);
        OscPublishAddressTextBox.Text = session.OscPublishAddress;

        SidecarPathTextBox.Text = session.Sidecar.SidecarPath;
        SidecarTimeoutTextBox.Text = session.Sidecar.TimeoutMs.ToString(CultureInfo.InvariantCulture);
        SidecarStrictCheckBox.IsChecked = session.Sidecar.StrictMode;
        ParserModeComboBox.SelectedIndex = session.Sidecar.ParserMode switch
        {
            "inhouse" => 1,
            "sidecar-strict" => 2,
            _ => 0,
        };

        var aq = _controller.GetAutoQualityPolicy();
        AutoQualityThresholdTextBox.Text = aq.HighFrameMsThreshold.ToString("F1", CultureInfo.InvariantCulture);
        AutoQualityConsecutiveTextBox.Text = aq.ConsecutiveFrameLimit.ToString(CultureInfo.InvariantCulture);
        AutoQualityCooldownTextBox.Text = aq.CooldownSeconds.ToString(CultureInfo.InvariantCulture);
        AutoQualityRecoveryThresholdTextBox.Text = aq.RecoveryFrameMsThreshold.ToString("F1", CultureInfo.InvariantCulture);
        AutoQualityRecoveryConsecutiveTextBox.Text = aq.RecoveryConsecutiveFrameLimit.ToString(CultureInfo.InvariantCulture);
    }

    private void RefreshGuides()
    {
        GuidesTextBox.Text = _controller.GetQuickstartText() + Environment.NewLine + Environment.NewLine + _controller.GetCompatibilityText();
    }
}
