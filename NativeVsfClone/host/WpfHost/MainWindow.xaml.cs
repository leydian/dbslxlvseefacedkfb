using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;
using HostCore;
using Microsoft.Win32;

namespace WpfHost;

public partial class MainWindow : Window
{
    private sealed record WebcamDeviceItem(string Key, string Label);
    private const string UiModeBeginner = "beginner";
    private const string UiModeAdvanced = "advanced";
    private readonly HostController _controller = new();
    private readonly DispatcherTimer _timer = new();
    private readonly DispatcherTimer _uiRefreshTimer = new();
    private readonly DispatcherTimer _resizeTimer = new();
    private readonly DispatcherTimer _renderApplyTimer = new();
    private readonly Stopwatch _frameTimer = Stopwatch.StartNew();
    private const float DragPixelsPerYawDegree = 6.0f;
    private const float WheelNotchFovStep = 1.0f;
    private bool _isSyncingRenderUi;
    private bool _isSyncingPoseUi;
    private bool _isSyncingTrackingPoseFilterUi;
    private bool _isSyncingPresetUi;
    private bool _isSyncingPosePresetUi;
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
    private HostValidationState _validationState = new(true, true, true, string.Empty, string.Empty, string.Empty);
    private bool _uiReady;
    private string _uiMode = UiModeBeginner;
    private bool _diagnosticsForcedVisible;
    private string _beginnerFailureHint = string.Empty;
    private string _lastFailureSource = string.Empty;
    private bool _isRenderRightDragging;
    private int _lastRenderDragX;
    private const int WebcamProbeLimit = 10;

    public MainWindow()
    {
        InitializeComponent();
        SourceInitialized += MainWindow_SourceInitialized;
        Closed += MainWindow_Closed;
        SizeChanged += MainWindow_SizeChanged;

        _timer.Interval = TimeSpan.FromMilliseconds(16.0);
        _timer.Tick += Timer_Tick;

        _uiRefreshTimer.Interval = TimeSpan.FromMilliseconds(100.0);
        _uiRefreshTimer.Tick += UiRefreshTimer_Tick;

        _resizeTimer.Interval = TimeSpan.FromMilliseconds(90.0);
        _resizeTimer.Tick += ResizeTimer_Tick;

        _renderApplyTimer.Interval = TimeSpan.FromMilliseconds(100.0);
        _renderApplyTimer.Tick += RenderApplyTimer_Tick;

        _controller.StateChanged += Controller_StateChanged;
        _controller.DiagnosticsUpdated += Controller_DiagnosticsUpdated;
        _controller.ErrorRaised += Controller_ErrorRaised;
        _controller.LoadProgressChanged += Controller_LoadProgressChanged;
        RenderHost.RenderRightDragStarted += RenderHost_RenderRightDragStarted;
        RenderHost.RenderRightDragMoved += RenderHost_RenderRightDragMoved;
        RenderHost.RenderRightDragCompleted += RenderHost_RenderRightDragCompleted;
        RenderHost.RenderMouseWheel += RenderHost_RenderMouseWheel;

        _isLogsTabActive = DiagnosticsTabControl.SelectedIndex == 2;
        ApplySessionDefaultsToUi();
        RefreshValidationState();
        SyncRenderControlsFromState();
        SyncPoseControlsFromState();
        SyncTrackingPoseFilterControlsFromState();
        SyncPosePresetControlsFromState();
        MarkAllDirty(includeLogs: true);
        ProcessPendingUpdates(force: true);
        RefreshGuides();
        _uiReady = true;
        _uiRefreshTimer.Start();
    }

    private void MainWindow_SourceInitialized(object? sender, EventArgs e)
    {
        UpdateRenderMetricsFromHost();
    }

    private void MainWindow_SizeChanged(object sender, SizeChangedEventArgs e)
    {
        if (RenderHost.Hwnd == IntPtr.Zero)
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
        RenderHost.RenderRightDragStarted -= RenderHost_RenderRightDragStarted;
        RenderHost.RenderRightDragMoved -= RenderHost_RenderRightDragMoved;
        RenderHost.RenderRightDragCompleted -= RenderHost_RenderRightDragCompleted;
        RenderHost.RenderMouseWheel -= RenderHost_RenderMouseWheel;
        _timer.Stop();
        _uiRefreshTimer.Stop();
        _resizeTimer.Stop();
        _renderApplyTimer.Stop();
        _ = _controller.Shutdown();
    }

    private void Initialize_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

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
        var renderHwnd = RenderHost.Hwnd;
        if (initRc == NcResultCode.Ok && renderHwnd != IntPtr.Zero)
        {
            var metrics = GetRenderMetrics();
            _controller.UpdateRenderMetrics(metrics.logicalWidth, metrics.logicalHeight, metrics.dpiScaleX, metrics.dpiScaleY, metrics.pixelWidth, metrics.pixelHeight);
            _ = PushRenderUiState();
            var attachRc = _controller.AttachWindow(renderHwnd, metrics.pixelWidth, metrics.pixelHeight);
            if (attachRc == NcResultCode.Ok)
            {
                _timer.Start();
            }
        }

        MarkAllDirty(includeLogs: true);
        ProcessPendingUpdates(force: true);
    }

    private void Shutdown_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        if (!Confirm("런타임을 종료하고 렌더/출력을 중지하시겠습니까? (Shutdown runtime and stop rendering/outputs?)"))
        {
            return;
        }

        _timer.Stop();
        _ = _controller.Shutdown();
        MarkAllDirty(includeLogs: true);
    }

    private void BrowseAvatar_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            CheckFileExists = true,
            CheckPathExists = true,
            Filter = "Avatar Files (*.vrm;*.vsfavatar;*.xav2)|*.vrm;*.vsfavatar;*.xav2|All Files (*.*)|*.*",
        };

        if (dialog.ShowDialog(this) == true)
        {
            AvatarPathTextBox.Text = dialog.FileName;
        }
    }

    private void InputTextChanged(object sender, TextChangedEventArgs e)
    {
        if (!_uiReady)
        {
            return;
        }

        RefreshValidationState();
        UpdateUiState();
    }

    private void BeginnerMode_Click(object sender, RoutedEventArgs e)
    {
        SetUiMode(UiModeBeginner, persist: true);
    }

    private void AdvancedMode_Click(object sender, RoutedEventArgs e)
    {
        SetUiMode(UiModeAdvanced, persist: true);
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
            MessageBox.Show(this, "먼저 세션을 초기화하세요. (Initialize the session first.)", "불러오기 차단 (Load Blocked)", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }
        if (!_validationState.AvatarPathValid)
        {
            MessageBox.Show(this, _validationState.AvatarPathError, "입력 오류 (Invalid Input)", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }
        var runtime = _controller.LastSnapshot.Runtime;
        if (!runtime.RuntimePathMatch)
        {
            RevealDiagnosticsForFailure("LoadAvatar.RuntimePathMismatch");
            ReportUserFailure(
                "LoadAvatar",
                $"Runtime path mismatch ({runtime.RuntimePathWarningCode}). Launch the dist/wpf build and retry.",
                $"Loaded nativecore path: {NormalizeDiagField(runtime.NativeCoreModulePath)}\n" +
                $"Expected nativecore path: {NormalizeDiagField(runtime.ExpectedNativeCoreModulePath)}\n" +
                $"WarningCode: {NormalizeDiagField(runtime.RuntimePathWarningCode)}");
            return;
        }

        if (!int.TryParse(LoadTimeoutTextBox.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var timeoutMs))
        {
            timeoutMs = 20000;
            LoadTimeoutTextBox.Text = "20000";
        }

        _isLoadRunning = true;
        UpdateUiState();
        var rc = await _controller.LoadAvatarAsync(AvatarPathTextBox.Text.Trim(), timeoutMs);
        _isLoadRunning = false;
        UpdateUiState();
        if (rc != NcResultCode.Ok)
        {
            RevealDiagnosticsForFailure("LoadAvatar");
            var detail = _controller.GetLastLoadFailureDetails();
            if (string.IsNullOrWhiteSpace(detail))
            {
                var guidance = _controller.GetLastErrorGuidance();
                var technical = NativeCoreInterop.FormatLastError();
                detail = string.IsNullOrWhiteSpace(guidance)
                    ? technical
                    : $"{guidance}\n\n{technical}";
            }
            ReportUserFailure(
                "LoadAvatar",
                $"Avatar load failed ({rc}). Check the selected file and try Load again.",
                $"Load failed: {rc}\n\n{detail}");
            return;
        }

        SyncRenderControlsFromState();
        ProcessPendingUpdates(force: true);
        ClearFailureHint();
    }

    private void CancelLoad_Click(object sender, RoutedEventArgs e)
    {
        _controller.CancelLoadAvatar();
    }

    private void Unload_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        if (!Confirm("현재 아바타를 해제하시겠습니까? (Unload active avatar?)"))
        {
            return;
        }
        _ = _controller.UnloadAvatar();
    }

    private void StartSpout_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

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
        var rc = _controller.StartSpout(metrics.pixelWidth, metrics.pixelHeight, 60, channel);
        if (rc != NcResultCode.Ok)
        {
            RevealDiagnosticsForFailure("StartSpout");
            ReportUserFailure(
                "StartSpout",
                $"Spout output failed to start ({rc}). Check channel name and retry.",
                $"Spout start failed: {rc}");
            return;
        }

        ClearFailureHint();
    }

    private void StopSpout_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        if (!Confirm("Spout 출력을 중지하시겠습니까? (Stop Spout output?)"))
        {
            return;
        }
        _ = _controller.StopSpout();
    }

    private void StartOsc_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        RefreshValidationState();
        if (!_validationState.OscBindPortValid)
        {
            MessageBox.Show(this, _validationState.OscBindPortError, "입력 오류 (Invalid Input)", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }
        if (!_validationState.OscPublishAddressValid)
        {
            MessageBox.Show(this, _validationState.OscPublishAddressError, "입력 오류 (Invalid Input)", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        if (!ushort.TryParse(OscBindPortTextBox.Text.Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out var bindPort))
        {
            MessageBox.Show(this, "OSC 바인드 포트는 0~65535 정수여야 합니다. (OSC bind port must be an integer between 0 and 65535.)", "입력 오류 (Invalid Input)", MessageBoxButton.OK, MessageBoxImage.Warning);
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
        var rc = _controller.StartOsc(bindPort, publishAddress);
        if (rc != NcResultCode.Ok)
        {
            RevealDiagnosticsForFailure("StartOsc");
            ReportUserFailure(
                "StartOsc",
                $"OSC output failed to start ({rc}). Check port/address and retry.",
                $"OSC start failed: {rc}");
            return;
        }

        ClearFailureHint();
    }

    private void StopOsc_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        if (!Confirm("Stop OSC output?"))
        {
            return;
        }
        _ = _controller.StopOsc();
    }

    private void StartTracking_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        // Defer webcam probing to user action path to avoid startup-time crashes from unstable virtual camera filters.
        var currentSelection = (TrackingWebcamDeviceComboBox.SelectedItem as WebcamDeviceItem)?.Key;
        RefreshTrackingWebcamDevices(currentSelection);

        if (!ushort.TryParse(TrackingPortTextBox.Text.Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out var listenPort))
        {
            MessageBox.Show(this, "트래킹 수신 포트는 0~65535여야 합니다. (Tracking listen port must be 0-65535.)", "입력 오류 (Invalid Input)", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        if (!int.TryParse(TrackingInferenceFpsTextBox.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var inferenceFpsCap))
        {
            inferenceFpsCap = 30;
            TrackingInferenceFpsTextBox.Text = "30";
        }
        inferenceFpsCap = Math.Clamp(inferenceFpsCap, 5, 120);

        var sourceType = TrackingSourceComboBox.SelectedIndex == 1
            ? TrackingSourceType.WebcamMediapipe
            : TrackingSourceType.OscIfacial;
        var sourceLockMode = TrackingSourceLockComboBox.SelectedIndex switch
        {
            1 => TrackingSourceLockMode.IfacialLocked,
            2 => TrackingSourceLockMode.WebcamLocked,
            _ => TrackingSourceLockMode.Auto,
        };
        var latencyProfile = TrackingLatencyProfileComboBox.SelectedIndex switch
        {
            0 => TrackingLatencyProfile.LowLatency,
            2 => TrackingLatencyProfile.Stable,
            _ => TrackingLatencyProfile.Balanced,
        };
        var poseFilterProfile = TrackingPoseFilterProfileComboBox.SelectedIndex switch
        {
            0 => PoseFilterProfile.Reactive,
            1 => PoseFilterProfile.Balanced,
            _ => PoseFilterProfile.Stable,
        };

        var current = _controller.GetTrackingInputSettings();
        var cameraKey = (TrackingWebcamDeviceComboBox.SelectedItem as WebcamDeviceItem)?.Key
            ?? current.CameraDeviceKey;
        _controller.ConfigureTrackingInputSettings(
            listenPort,
            current.StaleTimeoutMs,
            sourceType,
            cameraKey,
            inferenceFpsCap,
            sourceLockMode: sourceLockMode,
            latencyProfile: latencyProfile,
            poseFilterProfile: poseFilterProfile,
            poseDeadbandDeg: (float)TrackingPoseDeadbandSlider.Value);

        var rc = _controller.StartTracking(
            listenPort,
            staleTimeoutMs: _controller.GetTrackingInputSettings().StaleTimeoutMs);
        if (rc != NcResultCode.Ok)
        {
            MessageBox.Show(this, $"트래킹 시작 실패: {rc} (Start tracking failed: {rc})", "트래킹 (Tracking)", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }
    }

    private void StopTracking_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        _ = _controller.StopTracking();
    }

    private void RecenterTracking_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        var rc = _controller.RecenterTracking();
        if (rc != NcResultCode.Ok)
        {
            MessageBox.Show(this, "리센터는 활성 트래킹 입력이 필요합니다. (Recenter requires active tracking input.)", "트래킹 (Tracking)", MessageBoxButton.OK, MessageBoxImage.Information);
        }
    }

    private void RefreshTrackingWebcam_Click(object sender, RoutedEventArgs e)
    {
        var selectedKey = (TrackingWebcamDeviceComboBox.SelectedItem as WebcamDeviceItem)?.Key;
        RefreshTrackingWebcamDevices(selectedKey);
    }

    private void TrackingPoseFilterProfile_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (!_uiReady || _isSyncingTrackingPoseFilterUi || _controller.OperationState.IsBusy || _controller.TrackingDiagnostics.IsActive)
        {
            return;
        }

        var current = _controller.GetTrackingInputSettings();
        var profile = TrackingPoseFilterProfileComboBox.SelectedIndex switch
        {
            0 => PoseFilterProfile.Reactive,
            1 => PoseFilterProfile.Balanced,
            _ => PoseFilterProfile.Stable,
        };

        _controller.ConfigureTrackingInputSettings(
            current.ListenPort,
            current.StaleTimeoutMs,
            poseFilterProfile: profile);
    }

    private void TrackingPoseDeadbandSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        var deadbandSlider = TrackingPoseDeadbandSlider;
        if (deadbandSlider is null)
        {
            return;
        }

        if (TrackingPoseDeadbandValueText is not null)
        {
            TrackingPoseDeadbandValueText.Text = $"{deadbandSlider.Value:F2}\u00b0";
        }

        if (!_uiReady || _isSyncingTrackingPoseFilterUi || _controller.OperationState.IsBusy || _controller.TrackingDiagnostics.IsActive)
        {
            return;
        }

        var current = _controller.GetTrackingInputSettings();
        _controller.ConfigureTrackingInputSettings(
            current.ListenPort,
            current.StaleTimeoutMs,
            poseDeadbandDeg: (float)deadbandSlider.Value);
    }

    private void RefreshTrackingWebcamDevices(string? preferredKey = null)
    {
        var devices = _controller.GetAvailableWebcamDevices(maxProbe: WebcamProbeLimit);
        var items = devices
            .Select(d => new WebcamDeviceItem(
                d.DeviceKey,
                d.IsAvailable
                    ? $"{d.DisplayName} ({(string.IsNullOrWhiteSpace(d.DeviceKey) ? "default" : d.DeviceKey)})"
                    : $"{d.DisplayName} ({d.DeviceKey}) - unavailable"))
            .ToList();
        TrackingWebcamDeviceComboBox.ItemsSource = items;
        TrackingWebcamDeviceComboBox.DisplayMemberPath = nameof(WebcamDeviceItem.Label);
        var target = preferredKey ?? _controller.GetTrackingInputSettings().CameraDeviceKey;
        var selected = items.FirstOrDefault(x => string.Equals(x.Key, target, StringComparison.OrdinalIgnoreCase))
            ?? items.FirstOrDefault();
        if (selected is not null)
        {
            TrackingWebcamDeviceComboBox.SelectedItem = selected;
        }
    }

    private void SetTrackingWebcamDevicesPending(string? preferredKey = null)
    {
        var target = preferredKey ?? _controller.GetTrackingInputSettings().CameraDeviceKey;
        var items = new[]
        {
            new WebcamDeviceItem(string.Empty, "Default Camera (scan pending)"),
            new WebcamDeviceItem("0", "Camera 0 (scan pending)"),
        };
        TrackingWebcamDeviceComboBox.ItemsSource = items;
        TrackingWebcamDeviceComboBox.DisplayMemberPath = nameof(WebcamDeviceItem.Label);
        var selected = items.FirstOrDefault(x => string.Equals(x.Key, target, StringComparison.OrdinalIgnoreCase))
            ?? items[0];
        TrackingWebcamDeviceComboBox.SelectedItem = selected;
    }

    private void CopyLogs_Click(object sender, RoutedEventArgs e)
    {
        Clipboard.SetText(LogsTextBox.Text);
    }

    private void RunPreflight_Click(object sender, RoutedEventArgs e)
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
            ? "Preflight passed. Continue in this order: Initialize -> Load -> Start output."
            : "Preflight failed items: " + string.Join(" | ", failed.Select(x => $"[{x.CheckCode}] {x.Name}: {x.Remediation}"));
        if (failed.Count > 0)
        {
            RevealDiagnosticsForFailure("Preflight");
        }
        MessageBox.Show(this, sb.ToString(), "사전 점검 결과 (Preflight Result)", MessageBoxButton.OK, preflight.Passed ? MessageBoxImage.Information : MessageBoxImage.Warning);
    }

    private void ExportDiag_Click(object sender, RoutedEventArgs e)
    {
        var outputDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VsfCloneHost", "diagnostics");
        var path = _controller.ExportDiagnosticsBundle(outputDir);
        MessageBox.Show(this, $"진단 번들을 생성했습니다:\n{path}\n(Diagnostics bundle created)", "진단 내보내기 (Export Diagnostics)", MessageBoxButton.OK, MessageBoxImage.Information);
    }

    private void ExportMetrics_Click(object sender, RoutedEventArgs e)
    {
        var outputDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VsfCloneHost", "metrics");
        var path = _controller.ExportRollingMetricsCsv(Path.Combine(outputDir, $"metrics_{DateTimeOffset.UtcNow:yyyyMMdd_HHmmss}.csv"));
        MessageBox.Show(this, $"메트릭을 내보냈습니다:\n{path}\n(Metrics exported)", "메트릭 내보내기 (Export Metrics)", MessageBoxButton.OK, MessageBoxImage.Information);
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
        var mode = (ParserModeComboBox.SelectedItem as ComboBoxItem)?.Content?.ToString() ?? "sidecar";
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

    private void ExportTelemetry_Click(object sender, RoutedEventArgs e)
    {
        var outputDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VsfCloneHost", "telemetry");
        var path = _controller.ExportTelemetry(Path.Combine(outputDir, $"telemetry_{DateTimeOffset.UtcNow:yyyyMMdd_HHmmss}.json"));
        MessageBox.Show(this, $"Telemetry를 내보냈습니다:\n{path}\n(Telemetry exported)", "Telemetry 내보내기 (Export Telemetry)", MessageBoxButton.OK, MessageBoxImage.Information);
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

    private void FramingSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
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

    private void HeadroomSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
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

    private void YawSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
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

    private void FovSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
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

    private void PoseBone_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_isSyncingPoseUi)
        {
            return;
        }
        SyncPoseControlsFromState();
    }

    private void PoseSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (PosePitchValueText is not null && PosePitchSlider is not null)
        {
            PosePitchValueText.Text = PosePitchSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
        }
        if (PoseYawValueText is not null && PoseYawSlider is not null)
        {
            PoseYawValueText.Text = PoseYawSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
        }
        if (PoseRollValueText is not null && PoseRollSlider is not null)
        {
            PoseRollValueText.Text = PoseRollSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
        }
        if (_isSyncingPoseUi || _controller.OperationState.IsBusy)
        {
            return;
        }

        ApplySelectedPoseOffset();
    }

    private void PoseResetBone_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }
        var bone = GetSelectedPoseBone();
        if (bone is null)
        {
            return;
        }
        _ = _controller.ResetPoseOffset(bone.Value);
        SyncPoseControlsFromState();
    }

    private void PoseResetAll_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }
        _ = _controller.ResetAllPoseOffsets();
        SyncPoseControlsFromState();
    }

    private void SavePosePreset_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        var name = PosePresetNameTextBox.Text.Trim();
        if (string.IsNullOrWhiteSpace(name))
        {
            MessageBox.Show(this, "포즈 프리셋 이름이 필요합니다. (Pose preset name is required.)", "입력 오류 (Invalid Input)", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        if (_controller.SaveOrUpdatePosePreset(name))
        {
            SyncPosePresetControlsFromState();
        }
    }

    private void ApplyPosePreset_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        if (PosePresetComboBox.SelectedItem is not string name || string.IsNullOrWhiteSpace(name))
        {
            return;
        }

        _ = _controller.ApplyPosePreset(name);
        SyncPoseControlsFromState();
        SyncPosePresetControlsFromState();
        SyncTrackingPoseFilterControlsFromState();
    }

    private void DeletePosePreset_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        if (PosePresetComboBox.SelectedItem is not string name || string.IsNullOrWhiteSpace(name))
        {
            return;
        }

        if (_controller.DeletePosePreset(name))
        {
            SyncPosePresetControlsFromState();
        }
    }

    private void SavePreset_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        var name = PresetNameTextBox.Text.Trim();
        if (string.IsNullOrWhiteSpace(name))
        {
            MessageBox.Show(this, "프리셋 이름이 필요합니다. (Preset name is required.)", "입력 오류 (Invalid Input)", MessageBoxButton.OK, MessageBoxImage.Warning);
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

    private void DeletePreset_Click(object sender, RoutedEventArgs e)
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
            MessageBox.Show(this, "프리셋은 최소 1개가 필요하여 삭제할 수 없습니다. (Cannot delete preset. At least one preset must remain.)", "삭제 차단 (Delete Blocked)", MessageBoxButton.OK, MessageBoxImage.Information);
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

    private void DiagnosticsTabControl_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (sender is not TabControl tabControl)
        {
            return;
        }

        _isLogsTabActive = tabControl.SelectedIndex == 2;
        if (_isLogsTabActive)
        {
            _pendingLogsRefresh = true;
            ProcessPendingUpdates(force: false);
        }
    }

    private void RenderApplyTimer_Tick(object? sender, EventArgs e)
    {
        _renderApplyTimer.Stop();
        if (IsLoadOperationActive())
        {
            return;
        }
        _ = PushRenderUiState();
    }

    private void RenderHost_RenderRightDragStarted(object? sender, RenderMouseDragEventArgs e)
    {
        if (ShouldSkipRenderInteraction())
        {
            return;
        }

        _isRenderRightDragging = true;
        _lastRenderDragX = e.X;
        Mouse.OverrideCursor = Cursors.SizeWE;
    }

    private void RenderHost_RenderRightDragMoved(object? sender, RenderMouseDragEventArgs e)
    {
        if (!_isRenderRightDragging || ShouldSkipRenderInteraction())
        {
            return;
        }

        var deltaX = e.X - _lastRenderDragX;
        _lastRenderDragX = e.X;
        if (deltaX == 0)
        {
            return;
        }

        var yawDelta = deltaX / DragPixelsPerYawDegree;
        ApplyDirectRenderInteraction(yawDelta, 0.0f);
    }

    private void RenderHost_RenderRightDragCompleted(object? sender, RenderMouseDragEventArgs e)
    {
        _isRenderRightDragging = false;
        Mouse.OverrideCursor = null;
    }

    private void RenderHost_RenderMouseWheel(object? sender, RenderMouseWheelEventArgs e)
    {
        if (ShouldSkipRenderInteraction() || e.Delta == 0)
        {
            return;
        }

        var notch = e.Delta / 120.0f;
        var fovDelta = -notch * WheelNotchFovStep;
        ApplyDirectRenderInteraction(0.0f, fovDelta);
    }

    private void QueueRenderApply()
    {
        if (IsLoadOperationActive())
        {
            return;
        }
        _renderApplyTimer.Stop();
        _renderApplyTimer.Start();
    }

    private void UiRefreshTimer_Tick(object? sender, EventArgs e)
    {
        ProcessPendingUpdates(force: false);
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

        // Native rendering currently targets the window HWND, so WPF controls
        // need explicit invalidation to keep visual layering stable.
        RenderHost.InvalidateVisual();
        InvalidateVisual();
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

    private void Controller_StateChanged(object? sender, EventArgs e)
    {
        RunOnUiThread(() =>
        {
            _pendingUiStateRefresh = true;
            _pendingRuntimeRefresh = true;
            _pendingAvatarRefresh = true;
            _pendingLogsRefresh = true;
        });
    }

    private void Controller_DiagnosticsUpdated(object? sender, EventArgs e)
    {
        RunOnUiThread(() =>
        {
            _pendingRuntimeRefresh = true;
            _pendingAvatarRefresh = true;
            _pendingLogsRefresh = true;
        });
    }

    private void Controller_ErrorRaised(object? sender, HostLogEntry e)
    {
        RunOnUiThread(() =>
        {
            ErrorStatusText.Text = $"{e.Source}: {e.ResultCode}";
            RevealDiagnosticsForFailure(e.Source);
            _pendingLogsRefresh = true;
            var guide = _controller.GetLastErrorGuidance();
            if (!string.IsNullOrWhiteSpace(guide))
            {
                ShowFailureHint(e.Source, guide);
            }
        });
    }

    private void Controller_LoadProgressChanged(object? sender, LoadProgressState e)
    {
        RunOnUiThread(() =>
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

    private void RunOnUiThread(Action action)
    {
        if (Dispatcher.CheckAccess())
        {
            action();
            return;
        }

        _ = Dispatcher.BeginInvoke(action);
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
        var nextAction = HostUiPolicy.BuildNextActionHint(session, outputs, operation, _validationState);
        var tracking = _controller.TrackingDiagnostics;

        InitializeButton.IsEnabled = uiState.InitializeEnabled;
        ShutdownButton.IsEnabled = uiState.ShutdownEnabled;
        BrowseAvatarButton.IsEnabled = uiState.BrowseAvatarEnabled;
        LoadButton.IsEnabled = uiState.LoadEnabled;
        UnloadButton.IsEnabled = uiState.UnloadEnabled;
        CancelLoadButton.IsEnabled = _isLoadRunning;
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
        PoseBoneComboBox.IsEnabled = uiState.RenderControlsEnabled;
        PosePitchSlider.IsEnabled = uiState.RenderControlsEnabled;
        PoseYawSlider.IsEnabled = uiState.RenderControlsEnabled;
        PoseRollSlider.IsEnabled = uiState.RenderControlsEnabled;
        PoseResetBoneButton.IsEnabled = uiState.RenderControlsEnabled;
        PoseResetAllButton.IsEnabled = uiState.RenderControlsEnabled;
        SavePosePresetButton.IsEnabled = uiState.RenderControlsEnabled;
        ApplyPosePresetButton.IsEnabled = uiState.RenderControlsEnabled;
        DeletePosePresetButton.IsEnabled = uiState.RenderControlsEnabled;
        PosePresetNameTextBox.IsEnabled = uiState.RenderControlsEnabled;
        PosePresetComboBox.IsEnabled = uiState.RenderControlsEnabled;
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
        StartTrackingButton.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        StopTrackingButton.IsEnabled = !operation.IsBusy && tracking.IsActive;
        RecenterTrackingButton.IsEnabled = !operation.IsBusy && tracking.IsActive && !tracking.IsStale;
        TrackingPortTextBox.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingSourceComboBox.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingWebcamDeviceComboBox.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        RefreshTrackingWebcamButton.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingInferenceFpsTextBox.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingSourceLockComboBox.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingLatencyProfileComboBox.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingPoseFilterProfileComboBox.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingPoseDeadbandSlider.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        LoadTimeoutTextBox.IsEnabled = !operation.IsBusy && !_isLoadRunning;
        LoadButton.IsEnabled = LoadButton.IsEnabled && !_isLoadRunning;
        TrackingStatusText.Text = $"tracking={(tracking.IsActive ? "on" : "off")} source={tracking.SourceType} lock={tracking.SourceLockMode} active={tracking.ActiveSource} block={tracking.SwitchBlockedReason} source_status={tracking.SourceStatus} format={tracking.DetectedFormat} pose_filter={tracking.PoseFilterProfile} deadband_deg={tracking.PoseDeadbandDeg:F2} fps={tracking.InputFps:F1} capture_fps={tracking.CaptureFps:F1} infer_ms={tracking.InferenceMsAvg:F1} lat_avg={tracking.LatencyAvgMs:F1} lat_p95={tracking.LatencyP95Ms:F1} stage_ms(c/p/s/u)={tracking.CaptureStageMs:F1}/{tracking.ParseStageMs:F1}/{tracking.SmoothStageMs:F1}/{tracking.SubmitStageMs:F1} age_ms={tracking.LastPacketAgeMs} stale={tracking.IsStale} backend_ready={tracking.ModelSchemaOk} packets={tracking.ReceivedPackets} dropped={tracking.DroppedPackets} parse_err={tracking.ParseErrors} fallback={tracking.FallbackCount} calib={tracking.CalibrationState} conf={tracking.ConfidenceSummary} err={tracking.LastErrorCode}";

        SessionStatusText.Text = statusText.SessionText;
        AvatarStatusText.Text = statusText.AvatarText;
        RenderStatusText.Text = statusText.RenderText;
        OutputStatusText.Text = statusText.OutputText;
        BusyStatusText.Text = statusText.BusyText;
        QuickStatusText.Text = statusText.QuickStatusText;
        QuickNextActionText.Text = $"{nextAction.Title}: {nextAction.Instruction}";

        SyncRenderControlsFromState();
        SyncPoseControlsFromState();
        SyncTrackingPoseFilterControlsFromState();
        SyncPosePresetControlsFromState();
        SyncPresetControlsFromState();
        ApplyModeVisibility();
    }

    private bool ShouldSkipRenderInteraction()
    {
        return _isSyncingRenderUi || _controller.OperationState.IsBusy;
    }

    private bool IsLoadOperationActive()
    {
        return _isLoadRunning ||
               (_controller.OperationState.IsBusy &&
                string.Equals(_controller.OperationState.CurrentOperation, "LoadAvatar", StringComparison.Ordinal));
    }

    private void ApplyDirectRenderInteraction(float yawDelta, float fovDelta)
    {
        if (ShouldSkipRenderInteraction())
        {
            return;
        }

        var current = _controller.RenderState;
        var next = current with
        {
            CameraMode = RenderCameraMode.Manual,
            YawDeg = current.YawDeg + yawDelta,
            FovDeg = current.FovDeg + fovDelta,
        };
        _ = _controller.ApplyRenderUiState(next);
        UpdateUiState();
    }

    private void RefreshValidationState()
    {
        if (AvatarPathTextBox is null ||
            OscBindPortTextBox is null ||
            OscPublishAddressTextBox is null ||
            AvatarPathValidationText is null ||
            OscBindValidationText is null ||
            OscPublishValidationText is null)
        {
            return;
        }

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

        FrameStatusText.Text = $"{runtime.LastFrameMs:F2} ms";
        ErrorStatusText.Text = runtime.LastError;
        TrackStatusText.Text = _controller.GetReleaseTrackStatus();

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
        runtimeSb.AppendLine($"SpoutBackend: {runtime.SpoutBackend}");
        runtimeSb.AppendLine($"SpoutStrictMode: {runtime.SpoutStrictMode}");
        runtimeSb.AppendLine($"SpoutFallbackCount: {runtime.SpoutFallbackCount}");
        runtimeSb.AppendLine($"SpoutLastErrorCode: {runtime.SpoutLastErrorCode}");
        runtimeSb.AppendLine($"NativeCoreModulePath: {NormalizeDiagField(runtime.NativeCoreModulePath)}");
        runtimeSb.AppendLine($"NativeCoreModuleTimestampUtc: {NormalizeDiagField(runtime.NativeCoreModuleTimestampUtc)}");
        runtimeSb.AppendLine($"ExpectedNativeCoreModulePath: {NormalizeDiagField(runtime.ExpectedNativeCoreModulePath)}");
        runtimeSb.AppendLine($"RuntimePathMatch: {runtime.RuntimePathMatch}");
        runtimeSb.AppendLine($"RuntimePathWarningCode: {NormalizeDiagField(runtime.RuntimePathWarningCode)}");
        runtimeSb.AppendLine($"OscActive: {runtime.OscActive}");
        runtimeSb.AppendLine($"LastFrameMs: {runtime.LastFrameMs:F3}");
        var tracking = _controller.TrackingDiagnostics;
        runtimeSb.AppendLine($"Tracking: active={tracking.IsActive}, source={tracking.SourceType}, lock={tracking.SourceLockMode}, active_source={tracking.ActiveSource}, switch_blocked={tracking.SwitchBlockedReason}, source_status={tracking.SourceStatus}, format={tracking.DetectedFormat}, pose_filter={tracking.PoseFilterProfile}, deadband_deg={tracking.PoseDeadbandDeg:F2}, fps={tracking.InputFps:F1}, capture_fps={tracking.CaptureFps:F1}, infer_ms={tracking.InferenceMsAvg:F1}, latency_avg_ms={tracking.LatencyAvgMs:F1}, latency_p95_ms={tracking.LatencyP95Ms:F1}, stage_ms(capture/parse/smooth/submit)={tracking.CaptureStageMs:F1}/{tracking.ParseStageMs:F1}/{tracking.SmoothStageMs:F1}/{tracking.SubmitStageMs:F1}, age_ms={tracking.LastPacketAgeMs}, stale={tracking.IsStale}, backend_ready={tracking.ModelSchemaOk}, packets={tracking.ReceivedPackets}, dropped={tracking.DroppedPackets}, parse_err={tracking.ParseErrors}, fallback={tracking.FallbackCount}, calibration={tracking.CalibrationState}, confidence={tracking.ConfidenceSummary}, err={tracking.LastErrorCode}");
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
            avatarSb.AppendLine($"WarningCount: {info.WarningCount}");
            avatarSb.AppendLine($"WarningCodeCount: {info.WarningCodeCount}");
            avatarSb.AppendLine($"CriticalWarningCount: {info.CriticalWarningCount}");
            avatarSb.AppendLine($"MaterialDiagCount: {info.MaterialDiagCount}");
            avatarSb.AppendLine($"MaterialModes: opaque={info.OpaqueMaterialCount}, mask={info.MaskMaterialCount}, blend={info.BlendMaterialCount}");
            avatarSb.AppendLine($"LastWarningCode: {ResolveWarningCode(info)}");
            avatarSb.AppendLine($"LastWarningSeverity: {NormalizeDiagField(info.LastWarningSeverity)}");
            avatarSb.AppendLine($"LastWarningCategory: {NormalizeDiagField(info.LastWarningCategory)}");
            avatarSb.AppendLine($"ExpressionSummary: {info.LastExpressionSummary}");
            avatarSb.AppendLine($"LastWarning: {info.LastWarning}");
            avatarSb.AppendLine($"LastMaterialDiag: {info.LastMaterialDiag}");
            avatarSb.AppendLine($"LastRenderPassSummary: {NormalizeDiagField(info.LastRenderPassSummary)}");
            avatarSb.AppendLine($"LastMissingFeature: {info.LastMissingFeature}");
        }

        return avatarSb.ToString();
    }

    private static string ResolveWarningCode(NcAvatarInfo info)
    {
        if (!string.IsNullOrWhiteSpace(info.LastWarningCode))
        {
            return info.LastWarningCode;
        }
        return ExtractWarningCode(info.LastWarning);
    }

    private static string NormalizeDiagField(string text)
    {
        return string.IsNullOrWhiteSpace(text) ? "none" : text;
    }

    private static string ExtractWarningCode(string warningText)
    {
        if (string.IsNullOrWhiteSpace(warningText))
        {
            return "none";
        }

        var firstColon = warningText.IndexOf(':');
        if (firstColon < 0)
        {
            return "unknown";
        }

        var firstToken = warningText.Substring(0, firstColon).Trim();
        if (string.IsNullOrWhiteSpace(firstToken))
        {
            return "unknown";
        }
        return firstToken;
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

    private bool Confirm(string message)
    {
        var result = MessageBox.Show(this, message, "작업 확인 (Confirm Action)", MessageBoxButton.YesNo, MessageBoxImage.Question);
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

    private void SyncPoseControlsFromState()
    {
        if (PoseBoneComboBox is null || PosePitchSlider is null || PoseYawSlider is null || PoseRollSlider is null)
        {
            return;
        }
        _isSyncingPoseUi = true;
        if (PoseBoneComboBox.SelectedIndex < 0)
        {
            PoseBoneComboBox.SelectedIndex = 0;
        }

        var selected = GetSelectedPoseBone() ?? PoseBoneKind.Hips;
        var current = _controller.PoseOffsets.FirstOrDefault(p => p.Bone == selected);
        PosePitchSlider.Value = current?.PitchDeg ?? 0.0f;
        PoseYawSlider.Value = current?.YawDeg ?? 0.0f;
        PoseRollSlider.Value = current?.RollDeg ?? 0.0f;
        PosePitchValueText.Text = PosePitchSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
        PoseYawValueText.Text = PoseYawSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
        PoseRollValueText.Text = PoseRollSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
        _isSyncingPoseUi = false;
    }

    private void SyncTrackingPoseFilterControlsFromState()
    {
        if (TrackingPoseFilterProfileComboBox is null || TrackingPoseDeadbandSlider is null || TrackingPoseDeadbandValueText is null)
        {
            return;
        }

        var tracking = _controller.GetTrackingInputSettings();
        _isSyncingTrackingPoseFilterUi = true;
        TrackingPoseFilterProfileComboBox.SelectedIndex = tracking.PoseFilterProfile switch
        {
            PoseFilterProfile.Reactive => 0,
            PoseFilterProfile.Balanced => 1,
            _ => 2,
        };
        TrackingPoseDeadbandSlider.Value = tracking.PoseDeadbandDeg;
        TrackingPoseDeadbandValueText.Text = $"{tracking.PoseDeadbandDeg:F2}\u00b0";
        _isSyncingTrackingPoseFilterUi = false;
    }

    private void ApplySelectedPoseOffset()
    {
        var selected = GetSelectedPoseBone();
        if (selected is null)
        {
            return;
        }
        _ = _controller.SetPoseOffset(
            selected.Value,
            (float)PosePitchSlider.Value,
            (float)PoseYawSlider.Value,
            (float)PoseRollSlider.Value);
    }

    private PoseBoneKind? GetSelectedPoseBone()
    {
        return PoseBoneComboBox.SelectedIndex switch
        {
            0 => PoseBoneKind.Hips,
            1 => PoseBoneKind.Spine,
            2 => PoseBoneKind.Chest,
            3 => PoseBoneKind.UpperChest,
            4 => PoseBoneKind.Neck,
            5 => PoseBoneKind.Head,
            6 => PoseBoneKind.LeftUpperArm,
            7 => PoseBoneKind.RightUpperArm,
            _ => null,
        };
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

    private void SyncPosePresetControlsFromState()
    {
        if (_isSyncingPosePresetUi)
        {
            return;
        }

        _isSyncingPosePresetUi = true;
        var selectedName = _controller.SelectedPosePresetName ?? string.Empty;
        PosePresetComboBox.Items.Clear();
        foreach (var preset in _controller.PosePresets)
        {
            PosePresetComboBox.Items.Add(preset.Name);
        }

        var matched = false;
        foreach (var item in PosePresetComboBox.Items)
        {
            if (item is string name &&
                string.Equals(name, selectedName, StringComparison.OrdinalIgnoreCase))
            {
                PosePresetComboBox.SelectedItem = item;
                matched = true;
                break;
            }
        }

        if (!matched && PosePresetComboBox.Items.Count > 0)
        {
            PosePresetComboBox.SelectedIndex = 0;
            selectedName = PosePresetComboBox.SelectedItem as string ?? selectedName;
        }

        if (!string.IsNullOrWhiteSpace(selectedName))
        {
            PosePresetNameTextBox.Text = selectedName;
        }
        _isSyncingPosePresetUi = false;
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
        _uiMode = string.Equals(session.UiMode, UiModeAdvanced, StringComparison.OrdinalIgnoreCase)
            ? UiModeAdvanced
            : UiModeBeginner;
        _diagnosticsForcedVisible = string.Equals(_uiMode, UiModeAdvanced, StringComparison.Ordinal);
        if (!string.IsNullOrWhiteSpace(session.AvatarPath))
        {
            AvatarPathTextBox.Text = session.AvatarPath;
        }

        SpoutChannelTextBox.Text = session.SpoutChannelName;
        OscBindPortTextBox.Text = session.OscBindPort.ToString(CultureInfo.InvariantCulture);
        OscPublishAddressTextBox.Text = session.OscPublishAddress;
        TrackingPortTextBox.Text = session.Tracking.ListenPort.ToString(CultureInfo.InvariantCulture);
        TrackingSourceComboBox.SelectedIndex = session.Tracking.SourceType == TrackingSourceType.WebcamMediapipe ? 1 : 0;
        TrackingInferenceFpsTextBox.Text = session.Tracking.InferenceFpsCap.ToString(CultureInfo.InvariantCulture);
        TrackingSourceLockComboBox.SelectedIndex = session.Tracking.SourceLockMode switch
        {
            TrackingSourceLockMode.IfacialLocked => 1,
            TrackingSourceLockMode.WebcamLocked => 2,
            _ => 0,
        };
        TrackingLatencyProfileComboBox.SelectedIndex = session.Tracking.LatencyProfile switch
        {
            TrackingLatencyProfile.LowLatency => 0,
            TrackingLatencyProfile.Stable => 2,
            _ => 1,
        };
        TrackingPoseFilterProfileComboBox.SelectedIndex = session.Tracking.PoseFilterProfile switch
        {
            PoseFilterProfile.Reactive => 0,
            PoseFilterProfile.Balanced => 1,
            _ => 2,
        };
        TrackingPoseDeadbandSlider.Value = session.Tracking.PoseDeadbandDeg;
        TrackingPoseDeadbandValueText.Text = $"{session.Tracking.PoseDeadbandDeg:F2}\u00b0";
        SetTrackingWebcamDevicesPending(session.Tracking.CameraDeviceKey);

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
        ApplyModeVisibility();
    }

    private void RefreshGuides()
    {
        GuidesTextBox.Text = _controller.GetQuickstartText() + Environment.NewLine + Environment.NewLine + _controller.GetCompatibilityText();
    }

    private void SetUiMode(string mode, bool persist)
    {
        var normalized = string.Equals(mode, UiModeAdvanced, StringComparison.OrdinalIgnoreCase)
            ? UiModeAdvanced
            : UiModeBeginner;
        _uiMode = normalized;
        if (string.Equals(_uiMode, UiModeAdvanced, StringComparison.Ordinal))
        {
            _diagnosticsForcedVisible = true;
        }

        if (persist)
        {
            _controller.SetUiMode(_uiMode);
        }

        ApplyModeVisibility();
        UpdateUiState();
    }

    private void ApplyModeVisibility()
    {
        var advanced = string.Equals(_uiMode, UiModeAdvanced, StringComparison.Ordinal);
        var beginner = !advanced;
        BeginnerModeButton.IsEnabled = !advanced;
        AdvancedModeButton.IsEnabled = advanced ? false : true;
        BeginnerModeButton.FontWeight = advanced ? FontWeights.Normal : FontWeights.SemiBold;
        AdvancedModeButton.FontWeight = advanced ? FontWeights.SemiBold : FontWeights.Normal;

        TrackingGroup.Visibility = advanced ? Visibility.Visible : Visibility.Collapsed;
        PlatformOpsGroup.Visibility = advanced ? Visibility.Visible : Visibility.Collapsed;
        RenderAdvancedExpander.Visibility = advanced ? Visibility.Visible : Visibility.Collapsed;
        RenderAdvancedExpander.IsExpanded = advanced;

        var showDiagnostics = advanced || _diagnosticsForcedVisible;
        DiagnosticsRow.Height = showDiagnostics ? new GridLength(260.0) : new GridLength(0.0);
        DiagnosticsTabControl.Visibility = showDiagnostics ? Visibility.Visible : Visibility.Collapsed;
        BeginnerFailureHintPanel.Visibility = beginner && !string.IsNullOrWhiteSpace(_beginnerFailureHint)
            ? Visibility.Visible
            : Visibility.Collapsed;
        OpenDiagnosticsFromHintButton.Visibility = beginner ? Visibility.Visible : Visibility.Collapsed;
    }

    private void RevealDiagnosticsForFailure(string source)
    {
        _lastFailureSource = source;
        _diagnosticsForcedVisible = true;
        var tabIndex = 2;
        if (source.Contains("LoadAvatar", StringComparison.OrdinalIgnoreCase))
        {
            tabIndex = 1;
        }
        else if (source.Contains("Preflight", StringComparison.OrdinalIgnoreCase))
        {
            tabIndex = 3;
        }
        else if (source.Contains("StartSpout", StringComparison.OrdinalIgnoreCase) ||
                 source.Contains("StartOsc", StringComparison.OrdinalIgnoreCase) ||
                 source.Contains("Render", StringComparison.OrdinalIgnoreCase))
        {
            tabIndex = 0;
        }

        ApplyModeVisibility();
        DiagnosticsTabControl.SelectedIndex = tabIndex;
        _isLogsTabActive = tabIndex == 2;
        _pendingRuntimeRefresh = true;
        _pendingAvatarRefresh = true;
        _pendingLogsRefresh = true;
    }

    private void OpenDiagnosticsFromHint_Click(object sender, RoutedEventArgs e)
    {
        RevealDiagnosticsForFailure(string.IsNullOrWhiteSpace(_lastFailureSource) ? "LoadAvatar" : _lastFailureSource);
    }

    private bool IsBeginnerMode()
    {
        return string.Equals(_uiMode, UiModeBeginner, StringComparison.Ordinal);
    }

    private void ShowFailureHint(string source, string message)
    {
        _lastFailureSource = source;
        _beginnerFailureHint = message;
        BeginnerFailureHintText.Text = message;
        ApplyModeVisibility();
    }

    private void ClearFailureHint()
    {
        _beginnerFailureHint = string.Empty;
        BeginnerFailureHintText.Text = string.Empty;
        ApplyModeVisibility();
    }

    private void ReportUserFailure(string source, string beginnerMessage, string advancedMessage)
    {
        ShowFailureHint(source, beginnerMessage);
        var message = IsBeginnerMode() ? beginnerMessage : advancedMessage;
        MessageBox.Show(this, message, "조치 필요 (Action Required)", MessageBoxButton.OK, MessageBoxImage.Warning);
    }
}


