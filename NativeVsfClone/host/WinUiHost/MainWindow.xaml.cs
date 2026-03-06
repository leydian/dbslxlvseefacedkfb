using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using HostCore;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Graphics;
using Windows.Storage.Pickers;
using WinRT.Interop;

namespace WinUiHost;

public sealed partial class MainWindow : Window
{
    private sealed record WebcamDeviceItem(string Key, string Label, bool IsAvailable);
    private readonly HostController _controller = new();
    private readonly AvatarThumbnailPipeline _thumbnailPipeline;
    private readonly Stopwatch _frameTimer = Stopwatch.StartNew();
    private readonly DispatcherQueueTimer _timer;
    private readonly DispatcherQueueTimer _uiRefreshTimer;
    private readonly DispatcherQueueTimer _resizeTimer;
    private readonly DispatcherQueueTimer _renderApplyTimer;
    private const float DragPixelsPerYawDegree = 6.0f;
    private const float WheelNotchFovStep = 1.0f;
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
    private bool _isRenderRightDragging;
    private double _lastRenderDragX;
    private bool _syncingRecentAvatarList;
    private HostOnboardingStep? _lastTrackedOnboardingStep;
    private string _recoveryHint = string.Empty;

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
        RenderHost.PointerPressed += RenderHost_PointerPressed;
        RenderHost.PointerMoved += RenderHost_PointerMoved;
        RenderHost.PointerReleased += RenderHost_PointerReleased;
        RenderHost.PointerCanceled += RenderHost_PointerCanceled;
        RenderHost.PointerWheelChanged += RenderHost_PointerWheelChanged;

        _controller.StateChanged += Controller_StateChanged;
        _controller.DiagnosticsUpdated += Controller_DiagnosticsUpdated;
        _controller.ErrorRaised += Controller_ErrorRaised;
        _controller.LoadProgressChanged += Controller_LoadProgressChanged;
        _thumbnailPipeline = new AvatarThumbnailPipeline(async (job, token) =>
        {
            var exePath = Environment.ProcessPath;
            if (string.IsNullOrWhiteSpace(exePath) || !File.Exists(exePath))
            {
                return false;
            }

            return await AvatarThumbnailWorker.RunWorkerProcessAsync(
                exePath,
                job,
                TimeSpan.FromSeconds(20),
                token);
        });
        _thumbnailPipeline.StateChanged += ThumbnailPipeline_StateChanged;
        _thumbnailPipeline.WorkerRunningChanged += ThumbnailPipeline_WorkerRunningChanged;
        _isLogsTabActive = DiagnosticsTabControl.SelectedIndex == 2;
        ApplySessionDefaultsToUi();
        RefreshRecentAvatarList();
        RefreshValidationState();
        RefreshTrackingWebcamDevices();
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

        RenderHost.PointerPressed -= RenderHost_PointerPressed;
        RenderHost.PointerMoved -= RenderHost_PointerMoved;
        RenderHost.PointerReleased -= RenderHost_PointerReleased;
        RenderHost.PointerCanceled -= RenderHost_PointerCanceled;
        RenderHost.PointerWheelChanged -= RenderHost_PointerWheelChanged;
        _timer.Stop();
        _uiRefreshTimer.Stop();
        _resizeTimer.Stop();
        _renderApplyTimer.Stop();
        _thumbnailPipeline.StateChanged -= ThumbnailPipeline_StateChanged;
        _thumbnailPipeline.WorkerRunningChanged -= ThumbnailPipeline_WorkerRunningChanged;
        _ = _controller.Shutdown();
    }

    private async void Initialize_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        if (_controller.SessionState.IsInitialized &&
            !await ConfirmAsync("세션이 이미 초기화되어 있습니다. 다시 초기화하고 활성 출력/아바타를 재설정할까요? (Session is already initialized. Reinitialize and reset active outputs/avatar?)"))
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

        if (!await ConfirmAsync("런타임을 종료하고 렌더/출력을 중지하시겠습니까? (Shutdown runtime and stop rendering/outputs?)"))
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
        var path = AvatarPathTextBox.Text.Trim();
        if (!string.IsNullOrWhiteSpace(path) && File.Exists(path))
        {
            _controller.RecordAvatarSelection(path);
            EnqueueThumbnailGeneration(path, force: false);
            RefreshRecentAvatarList();
        }
        else
        {
            UpdateAvatarPreview(path);
        }
    }

    private void RecentAvatarList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_syncingRecentAvatarList || RecentAvatarListBox.SelectedItem is not ListBoxItem item || item.Tag is not string path)
        {
            return;
        }

        AvatarPathTextBox.Text = path;
    }

    private void RetryAvatarPreview_Click(object sender, RoutedEventArgs e)
    {
        var path = AvatarPathTextBox.Text.Trim();
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
        {
            return;
        }

        EnqueueThumbnailGeneration(path, force: true);
    }

    private void RefreshRecentAvatarList()
    {
        if (RecentAvatarListBox is null)
        {
            return;
        }

        var selectedPath = (RecentAvatarListBox.SelectedItem as ListBoxItem)?.Tag as string ?? AvatarPathTextBox.Text.Trim();
        _syncingRecentAvatarList = true;
        RecentAvatarListBox.Items.Clear();
        foreach (var entry in _controller.GetRecentAvatars())
        {
            var status = entry.ThumbnailStatus switch
            {
                "ready" => "ready",
                "pending" => "pending",
                "failed" => "failed",
                _ => "none",
            };
            var item = new ListBoxItem
            {
                Tag = entry.AvatarPath,
                Content = $"{entry.DisplayName} [{status}]",
            };
            item.ToolTip = entry.AvatarPath;
            RecentAvatarListBox.Items.Add(item);
            if (string.Equals(entry.AvatarPath, selectedPath, StringComparison.OrdinalIgnoreCase))
            {
                RecentAvatarListBox.SelectedItem = item;
            }
        }
        _syncingRecentAvatarList = false;
        UpdateAvatarPreview(AvatarPathTextBox.Text.Trim());
    }

    private void UpdateAvatarPreview(string avatarPath)
    {
        if (AvatarPreviewImage is null || AvatarPreviewStatusText is null || RetryAvatarPreviewButton is null)
        {
            return;
        }

        if (string.IsNullOrWhiteSpace(avatarPath))
        {
            AvatarPreviewImage.Source = null;
            AvatarPreviewStatusText.Text = "선택된 파일 없음";
            RetryAvatarPreviewButton.IsEnabled = false;
            return;
        }

        var recent = _controller.GetRecentAvatars()
            .FirstOrDefault(item => string.Equals(item.AvatarPath, avatarPath, StringComparison.OrdinalIgnoreCase));
        if (recent is not null &&
            string.Equals(recent.ThumbnailStatus, "ready", StringComparison.Ordinal) &&
            File.Exists(recent.ThumbnailPath))
        {
            AvatarPreviewImage.Source = LoadPreviewBitmap(recent.ThumbnailPath);
            AvatarPreviewStatusText.Text = $"미리보기 준비됨: {Path.GetFileName(avatarPath)}";
        }
        else if (recent is not null && string.Equals(recent.ThumbnailStatus, "pending", StringComparison.Ordinal))
        {
            AvatarPreviewImage.Source = null;
            AvatarPreviewStatusText.Text = $"미리보기 생성 중: {Path.GetFileName(avatarPath)}";
        }
        else if (recent is not null && string.Equals(recent.ThumbnailStatus, "failed", StringComparison.Ordinal))
        {
            AvatarPreviewImage.Source = null;
            AvatarPreviewStatusText.Text = $"미리보기 실패: {Path.GetFileName(avatarPath)}";
        }
        else
        {
            AvatarPreviewImage.Source = null;
            AvatarPreviewStatusText.Text = $"미리보기 대기: {Path.GetFileName(avatarPath)}";
        }

        RetryAvatarPreviewButton.IsEnabled = File.Exists(avatarPath) && !_thumbnailPipeline.IsWorkerRunning;
    }

    private static BitmapImage? LoadPreviewBitmap(string path)
    {
        try
        {
            return new BitmapImage(new Uri(path, UriKind.Absolute));
        }
        catch
        {
            return null;
        }
    }

    private void EnqueueThumbnailGeneration(string avatarPath, bool force)
    {
        var normalized = avatarPath.Trim();
        if (string.IsNullOrWhiteSpace(normalized) || !File.Exists(normalized))
        {
            return;
        }

        _ = _thumbnailPipeline.Enqueue(normalized, force);
        UpdateAvatarPreview(normalized);
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
            _recoveryHint = "세션을 먼저 시작하세요.";
            _ = ShowMessageAsync("불러오기 차단 (Load Blocked)", "먼저 세션을 초기화하세요. (Initialize the session first.)");
            return;
        }
        if (!_validationState.AvatarPathValid)
        {
            _recoveryHint = _validationState.AvatarPathError;
            _ = ShowMessageAsync("입력 오류 (Invalid Input)", _validationState.AvatarPathError);
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
            var detail = _controller.GetLastLoadFailureDetails();
            if (string.IsNullOrWhiteSpace(detail))
            {
                var guidance = _controller.GetLastErrorGuidance();
                var technical = NativeCoreInterop.FormatLastError();
                detail = string.IsNullOrWhiteSpace(guidance)
                    ? technical
                    : $"{guidance}\n\n{technical}";
            }
            _recoveryHint = $"아바타 로드 실패({rc}) - 경로/포맷을 확인 후 다시 시도하세요.";
            await ShowMessageAsync("불러오기 실패 (Load Failed)", $"Load failed: {rc}\n\n{detail}");
            return;
        }

        var loadedPath = AvatarPathTextBox.Text.Trim();
        if (!string.IsNullOrWhiteSpace(loadedPath) && File.Exists(loadedPath))
        {
            _controller.RecordAvatarSelection(loadedPath);
            EnqueueThumbnailGeneration(loadedPath, force: false);
            RefreshRecentAvatarList();
        }
        _recoveryHint = string.Empty;
    }

    private void PrimaryAction_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        RefreshValidationState();
        var action = PrimaryActionButton.Tag is HostPrimaryActionKind taggedAction
            ? taggedAction
            : HostUiPolicy.BuildOnboardingState(_controller.SessionState, _controller.Outputs, _controller.OperationState, _validationState).PrimaryAction;
        var onboarding = HostUiPolicy.BuildOnboardingState(_controller.SessionState, _controller.Outputs, _controller.OperationState, _validationState);
        _controller.TrackOnboardingUiEvent(
            "primary_cta_clicked",
            onboarding.Step,
            action,
            onboarding.Actionability,
            onboarding.BlockReasonShort);

        switch (action)
        {
            case HostPrimaryActionKind.InitializeSession:
                Initialize_Click(sender, e);
                break;
            case HostPrimaryActionKind.LoadAvatar:
                Load_Click(sender, e);
                break;
            case HostPrimaryActionKind.StartOutput:
                QuickStartBroadcast_Click(sender, e);
                break;
            default:
                OpenDiagnosticsFromHint_Click(sender, e);
                break;
        }
    }

    private void QuickInitialize_Click(object sender, RoutedEventArgs e)
    {
        Initialize_Click(sender, e);
    }

    private void QuickLoadAvatar_Click(object sender, RoutedEventArgs e)
    {
        Load_Click(sender, e);
    }

    private void QuickStartBroadcast_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        if (!_controller.Outputs.SpoutActive && StartSpoutButton.IsEnabled)
        {
            StartSpout_Click(sender, e);
        }

        if (!_controller.Outputs.SpoutActive &&
            !_controller.Outputs.OscActive &&
            StartOscButton.IsEnabled)
        {
            StartOsc_Click(sender, e);
            return;
        }

        if (!_controller.Outputs.OscActive && StartOscButton.IsEnabled)
        {
            StartOsc_Click(sender, e);
        }
    }

    private void OpenDiagnosticsFromHint_Click(object sender, RoutedEventArgs e)
    {
        var onboarding = HostUiPolicy.BuildOnboardingState(_controller.SessionState, _controller.Outputs, _controller.OperationState, _validationState);
        _controller.TrackOnboardingUiEvent(
            "recovery_action_clicked",
            onboarding.Step,
            onboarding.PrimaryAction,
            onboarding.Actionability,
            string.IsNullOrWhiteSpace(_recoveryHint) ? onboarding.BlockReasonShort : _recoveryHint);
        DiagnosticsTabControl.SelectedIndex = 2;
        _isLogsTabActive = true;
        _pendingLogsRefresh = true;
        ProcessPendingUpdates(force: false);
    }

    private void DismissRecoveryHint_Click(object sender, RoutedEventArgs e)
    {
        _recoveryHint = string.Empty;
        UpdateUiState();
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

        if (!await ConfirmAsync("현재 아바타를 해제하시겠습니까? (Unload active avatar?)"))
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
            !await ConfirmAsync("Spout가 이미 활성입니다. 현재 설정으로 다시 시작할까요? (Spout is active. Restart with current settings?)"))
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

        if (!await ConfirmAsync("Spout 출력을 중지하시겠습니까? (Stop Spout output?)"))
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
            await ShowMessageAsync("입력 오류 (Invalid Input)", _validationState.OscBindPortError);
            return;
        }
        if (!_validationState.OscPublishAddressValid)
        {
            await ShowMessageAsync("입력 오류 (Invalid Input)", _validationState.OscPublishAddressError);
            return;
        }

        if (!ushort.TryParse(OscBindPortTextBox.Text.Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out var bindPort))
        {
            await ShowMessageAsync("입력 오류 (Invalid Input)", "OSC 바인드 포트는 0~65535 정수여야 합니다. (OSC bind port must be an integer between 0 and 65535.)");
            return;
        }

        if (_controller.Outputs.OscActive &&
            !await ConfirmAsync("OSC가 이미 활성입니다. 현재 설정으로 다시 시작할까요? (OSC is active. Restart with current settings?)"))
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

        if (!await ConfirmAsync("OSC 출력을 중지하시겠습니까? (Stop OSC output?)"))
        {
            return;
        }
        _ = _controller.StopOsc();
    }

    private async void StartTracking_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        var currentSelection = (TrackingWebcamDeviceComboBox.SelectedItem as WebcamDeviceItem)?.Key;
        RefreshTrackingWebcamDevices(currentSelection);

        if (!ushort.TryParse(TrackingPortTextBox.Text.Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out var listenPort))
        {
            await ShowMessageAsync("입력 오류 (Invalid Input)", "트래킹 수신 포트는 0~65535 정수여야 합니다. (Tracking listen port must be an integer between 0 and 65535.)");
            return;
        }

        if (!int.TryParse(TrackingInferenceFpsTextBox.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var inferenceFpsCap))
        {
            inferenceFpsCap = 30;
            TrackingInferenceFpsTextBox.Text = "30";
        }
        inferenceFpsCap = Math.Clamp(inferenceFpsCap, 5, 120);
        if (!int.TryParse(TrackingParseWarnThresholdTextBox.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var parseWarnThreshold))
        {
            parseWarnThreshold = 10;
            TrackingParseWarnThresholdTextBox.Text = "10";
        }
        parseWarnThreshold = Math.Clamp(parseWarnThreshold, 1, 10000);
        if (!int.TryParse(TrackingDropWarnThresholdTextBox.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var dropWarnThreshold))
        {
            dropWarnThreshold = 10;
            TrackingDropWarnThresholdTextBox.Text = "10";
        }
        dropWarnThreshold = Math.Clamp(dropWarnThreshold, 1, 10000);

        var sourceType = TrackingSourceComboBox.SelectedIndex switch
        {
            2 => TrackingSourceType.WebcamMediapipe,
            1 => TrackingSourceType.OscIfacial,
            _ => TrackingSourceType.HybridAuto,
        };

        var settings = _controller.GetTrackingInputSettings();
        var cameraKey = (TrackingWebcamDeviceComboBox.SelectedItem as WebcamDeviceItem)?.Key
            ?? settings.CameraDeviceKey;
        _controller.ConfigureTrackingInputSettings(
            listenPort,
            settings.StaleTimeoutMs,
            sourceType,
            cameraKey,
            inferenceFpsCap,
            parseWarnThreshold,
            dropWarnThreshold,
            upperBodyEnabled: TrackingUpperBodyEnabledCheckBox.IsChecked == true);
        var rc = _controller.StartTracking(listenPort, settings.StaleTimeoutMs);
        if (rc != NcResultCode.Ok)
        {
            await ShowMessageAsync("트래킹 (Tracking)", $"Start tracking failed: {rc}");
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

    private async void RecenterTracking_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        var rc = _controller.RecenterTracking();
        if (rc != NcResultCode.Ok)
        {
            await ShowMessageAsync("트래킹 (Tracking)", "리센터는 활성 트래킹 입력이 필요합니다. (Recenter requires active tracking input.)");
        }
    }

    private void RefreshTrackingWebcam_Click(object sender, RoutedEventArgs e)
    {
        var selectedKey = (TrackingWebcamDeviceComboBox.SelectedItem as WebcamDeviceItem)?.Key;
        RefreshTrackingWebcamDevices(selectedKey);
    }

    private void RefreshTrackingWebcamDevices(string? preferredKey = null)
    {
        var devices = _controller.GetAvailableWebcamDevices();
        var items = devices
            .Select(d => new WebcamDeviceItem(
                d.DeviceKey,
                d.IsAvailable
                    ? $"{d.DisplayName} ({(string.IsNullOrWhiteSpace(d.DeviceKey) ? "default" : d.DeviceKey)})"
                    : $"{d.DisplayName} ({d.DeviceKey}) - unavailable",
                d.IsAvailable))
            .ToList();

        TrackingWebcamDeviceComboBox.Items.Clear();
        foreach (var item in items)
        {
            TrackingWebcamDeviceComboBox.Items.Add(item);
        }
        TrackingWebcamDeviceComboBox.DisplayMemberPath = nameof(WebcamDeviceItem.Label);
        var target = preferredKey ?? _controller.GetTrackingInputSettings().CameraDeviceKey;
        var selected = items.FirstOrDefault(x => string.Equals(x.Key, target, StringComparison.OrdinalIgnoreCase))
            ?? items.FirstOrDefault(x => x.IsAvailable)
            ?? items.FirstOrDefault();
        if (selected is not null)
        {
            TrackingWebcamDeviceComboBox.SelectedItem = selected;
        }
    }

    private static string BuildTrackingErrorHint(string lastErrorCode)
    {
        if (string.IsNullOrWhiteSpace(lastErrorCode))
        {
            return string.Empty;
        }

        return lastErrorCode switch
        {
            "TRACKING_PARSE_THRESHOLD_EXCEEDED" => " hint=parse errors exceeded threshold",
            "TRACKING_DROP_THRESHOLD_EXCEEDED" => " hint=dropped packets exceeded threshold",
            "TRACKING_NO_MAPPED_CHANNELS" => " hint=source packet had no mapped channels",
            "TRACKING_MEDIAPIPE_CONFIG_INVALID" => " hint=webcam runtime config invalid",
            "TRACKING_MEDIAPIPE_START_FAILED" => " hint=webcam sidecar start failed",
            "TRACKING_MEDIAPIPE_NO_FRAME" => " hint=webcam sidecar produced no frames",
            "TRACKING_NO_ACTIVE_INPUT_SOURCE" => " hint=no active input source; start iFacial send or enable webcam runtime",
            "TRACKING_IFACIAL_NO_PACKET" => " hint=no iFacial packet received",
            "TRACKING_WEBCAM_RUNTIME_UNAVAILABLE" => " hint=webcam runtime unavailable (python/mediapipe not ready)",
            "TRACKING_WEBCAM_NO_FRAME" => " hint=webcam runtime started but no frame",
            _ when lastErrorCode.StartsWith("NC_SET_TRACKING_FRAME_", StringComparison.Ordinal) => " hint=native tracking submit failed",
            _ when lastErrorCode.StartsWith("NC_SET_EXPRESSION_WEIGHTS_", StringComparison.Ordinal) => " hint=native expression submit failed",
            _ => string.Empty,
        };
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
            ? "사전 점검 통과. 초기화 -> 불러오기 -> 출력 시작 순서로 진행하세요. (Preflight passed. Proceed with Initialize -> Load -> Start outputs.)"
            : "사전 점검 실패 항목: " + string.Join(" | ", failed.Select(x => $"[{x.CheckCode}] {x.Name}: {x.Remediation}"));

        await ShowMessageAsync("사전 점검 결과 (Preflight Result)", sb.ToString());
    }

    private async void ExportDiag_Click(object sender, RoutedEventArgs e)
    {
        var outputDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VsfCloneHost", "diagnostics");
        var path = _controller.ExportDiagnosticsBundle(outputDir);
        await ShowMessageAsync("진단 내보내기 (Export Diagnostics)", $"진단 번들을 생성했습니다:\n{path}\n(Diagnostics bundle created)");
    }

    private async void ExportMetrics_Click(object sender, RoutedEventArgs e)
    {
        var outputDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VsfCloneHost", "metrics");
        var path = _controller.ExportRollingMetricsCsv(Path.Combine(outputDir, $"metrics_{DateTimeOffset.UtcNow:yyyyMMdd_HHmmss}.csv"));
        await ShowMessageAsync("메트릭 내보내기 (Export Metrics)", $"메트릭을 내보냈습니다:\n{path}\n(Metrics exported)");
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
        await ShowMessageAsync("Telemetry 내보내기 (Export Telemetry)", $"Telemetry를 내보냈습니다:\n{path}\n(Telemetry exported)");
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
        _renderApplyTimer.Stop();
        var rc = PushRenderUiState();
        if (rc == NcResultCode.Ok)
        {
            UpdateUiState();
        }
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
            await ShowMessageAsync("입력 오류 (Invalid Input)", "프리셋 이름이 필요합니다. (Preset name is required.)");
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

    private void RenderHost_PointerPressed(object sender, PointerRoutedEventArgs e)
    {
        if (ShouldSkipRenderInteraction())
        {
            return;
        }

        var point = e.GetCurrentPoint(RenderHost);
        if (!point.Properties.IsRightButtonPressed)
        {
            return;
        }

        _isRenderRightDragging = true;
        _lastRenderDragX = point.Position.X;
        _ = RenderHost.CapturePointer(e.Pointer);
        e.Handled = true;
    }

    private void RenderHost_PointerMoved(object sender, PointerRoutedEventArgs e)
    {
        if (!_isRenderRightDragging || ShouldSkipRenderInteraction())
        {
            return;
        }

        var point = e.GetCurrentPoint(RenderHost);
        if (!point.Properties.IsRightButtonPressed)
        {
            EndRenderRightDrag(e.Pointer);
            return;
        }

        var deltaX = point.Position.X - _lastRenderDragX;
        _lastRenderDragX = point.Position.X;
        if (Math.Abs(deltaX) < 0.001)
        {
            return;
        }

        var yawDelta = (float)(deltaX / DragPixelsPerYawDegree);
        ApplyDirectRenderInteraction(yawDelta, 0.0f);
        e.Handled = true;
    }

    private void RenderHost_PointerReleased(object sender, PointerRoutedEventArgs e)
    {
        EndRenderRightDrag(e.Pointer);
    }

    private void RenderHost_PointerCanceled(object sender, PointerRoutedEventArgs e)
    {
        EndRenderRightDrag(e.Pointer);
    }

    private void RenderHost_PointerWheelChanged(object sender, PointerRoutedEventArgs e)
    {
        if (ShouldSkipRenderInteraction())
        {
            return;
        }

        var delta = e.GetCurrentPoint(RenderHost).Properties.MouseWheelDelta;
        if (delta == 0)
        {
            return;
        }

        var notch = delta / 120.0f;
        var fovDelta = -notch * WheelNotchFovStep;
        ApplyDirectRenderInteraction(0.0f, fovDelta);
        e.Handled = true;
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
        _ = DispatcherQueue.TryEnqueue(RefreshRecentAvatarList);
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
            _recoveryHint = guide;
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

    private void ThumbnailPipeline_StateChanged(object? sender, AvatarThumbnailState e)
    {
        _ = DispatcherQueue.TryEnqueue(() =>
        {
            _controller.UpdateRecentAvatarThumbnail(e.AvatarPath, e.ThumbnailPath, e.Status, e.LastError);
            RefreshRecentAvatarList();
            UpdateUiState();
        });
    }

    private void ThumbnailPipeline_WorkerRunningChanged(object? sender, bool e)
    {
        _ = DispatcherQueue.TryEnqueue(() =>
        {
            UpdateAvatarPreview(AvatarPathTextBox.Text.Trim());
            UpdateUiState();
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
        var onboarding = HostUiPolicy.BuildOnboardingState(session, outputs, operation, _validationState);
        var tracking = _controller.TrackingDiagnostics;

        InitializeButton.IsEnabled = uiState.InitializeEnabled;
        ShutdownButton.IsEnabled = uiState.ShutdownEnabled;
        BrowseAvatarButton.IsEnabled = uiState.BrowseAvatarEnabled;
        LoadButton.IsEnabled = uiState.LoadEnabled && !_isLoadRunning;
        CancelLoadButton.IsEnabled = _isLoadRunning;
        UnloadButton.IsEnabled = uiState.UnloadEnabled;
        RecentAvatarListBox.IsEnabled = !operation.IsBusy && !_isLoadRunning;
        RetryAvatarPreviewButton.IsEnabled = !operation.IsBusy && !_isLoadRunning && !_thumbnailPipeline.IsWorkerRunning && File.Exists(AvatarPathTextBox.Text.Trim());
        StartSpoutButton.IsEnabled = uiState.StartSpoutEnabled;
        StopSpoutButton.IsEnabled = uiState.StopSpoutEnabled;
        StartOscButton.IsEnabled = uiState.StartOscEnabled;
        StopOscButton.IsEnabled = uiState.StopOscEnabled;
        QuickInitializeButton.IsEnabled = uiState.InitializeEnabled;
        QuickLoadAvatarButton.IsEnabled = uiState.LoadEnabled && !_isLoadRunning;
        QuickStartBroadcastButton.IsEnabled = (uiState.StartSpoutEnabled || uiState.StartOscEnabled) && !operation.IsBusy;

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
        StartTrackingButton.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        StopTrackingButton.IsEnabled = !operation.IsBusy && tracking.IsActive;
        RecenterTrackingButton.IsEnabled = !operation.IsBusy && tracking.IsActive && !tracking.IsStale;
        TrackingPortTextBox.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingSourceComboBox.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingWebcamDeviceComboBox.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        RefreshTrackingWebcamButton.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingInferenceFpsTextBox.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingParseWarnThresholdTextBox.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingDropWarnThresholdTextBox.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingUpperBodyEnabledCheckBox.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        LoadTimeoutTextBox.IsEnabled = !operation.IsBusy && !_isLoadRunning;
        var trackingSettings = _controller.GetTrackingInputSettings();
        var trackingHint = BuildTrackingErrorHint(tracking.LastErrorCode);
        TrackingStatusText.Text = $"tracking={(tracking.IsActive ? "on" : "off")} source={tracking.SourceType} active={tracking.ActiveSource} source_status={tracking.SourceStatus} format={tracking.DetectedFormat} upper_body_enabled={trackingSettings.UpperBodyEnabled} upper_active={tracking.UpperBodyTrackingActive} upper_conf={tracking.UpperBodyConfidence:F2} upper_age={tracking.UpperBodyPacketAgeMs} upper_status={tracking.UpperBodyStatus} fps={tracking.InputFps:F1} capture_fps={tracking.CaptureFps:F1} infer_ms={tracking.InferenceMsAvg:F1} arkit52={tracking.Arkit52SubmittedCount}/52 strict={tracking.Arkit52StrictCount} fb={tracking.Arkit52FallbackCount} missing={tracking.Arkit52MissingCount} q={tracking.Arkit52QualityScore:F2} qms={tracking.Arkit52QualityStageMs:F2} age_ms={tracking.LastPacketAgeMs} ifacial_age={tracking.IfacialPacketAgeMs} webcam_age={tracking.WebcamPacketAgeMs} stale={tracking.IsStale} backend_ready={tracking.ModelSchemaOk} packets={tracking.ReceivedPackets} dropped={tracking.DroppedPackets} parse_err={tracking.ParseErrors} parse_warn={trackingSettings.ParseErrorWarnThreshold} drop_warn={trackingSettings.DroppedPacketWarnThreshold} fallback={tracking.FallbackCount} calib={tracking.CalibrationState} conf={tracking.ConfidenceSummary} err={tracking.LastErrorCode}{trackingHint}";

        SessionStatusText.Text = $"Session: {statusText.SessionText}";
        AvatarStatusText.Text = $"Avatar: {statusText.AvatarText}";
        RenderStatusText.Text = $"Render: {statusText.RenderText}";
        OutputStatusText.Text = $"Outputs: {statusText.OutputText}";
        BusyStatusText.Text = $"Busy: {statusText.BusyText}";
        TrackStatusText.Text = $"Track: {_controller.GetReleaseTrackStatus()}";
        QuickStatusText.Text = statusText.QuickStatusText;
        QuickNextActionText.Text = onboarding.StepTitle;
        PrimaryActionDescriptionText.Text = onboarding.Instruction;
        NextActionSummaryText.Text = onboarding.NextActionSummary;
        BlockReasonShortText.Text = onboarding.BlockReasonShort;
        BlockReasonShortText.Visibility = string.IsNullOrWhiteSpace(onboarding.BlockReasonShort)
            ? Visibility.Collapsed
            : Visibility.Visible;
        if (onboarding.Actionability == HostActionability.Blocked)
        {
            ActionabilityBadgeText.Text = "BLOCKED";
            ActionabilityBadgeText.Foreground = new Microsoft.UI.Xaml.Media.SolidColorBrush(ColorHelper.FromArgb(255, 165, 107, 26));
        }
        else
        {
            ActionabilityBadgeText.Text = "READY";
            ActionabilityBadgeText.Foreground = new Microsoft.UI.Xaml.Media.SolidColorBrush(ColorHelper.FromArgb(255, 67, 88, 110));
        }

        SetOnboardingStepState(OnboardingStep1Text, session.IsInitialized);
        SetOnboardingStepState(OnboardingStep2Text, session.ActiveAvatarHandle.HasValue);
        SetOnboardingStepState(OnboardingStep3Text, outputs.SpoutActive || outputs.OscActive);
        var onboardingRecovery = string.IsNullOrWhiteSpace(onboarding.BlockReason)
            ? string.Empty
            : $"{onboarding.BlockReason} {onboarding.RecoveryAction}".Trim();
        OnboardingRecoveryText.Text = string.IsNullOrWhiteSpace(_recoveryHint)
            ? onboardingRecovery
            : _recoveryHint;
        OnboardingRecoveryText.Visibility = string.IsNullOrWhiteSpace(OnboardingRecoveryText.Text)
            ? Visibility.Collapsed
            : Visibility.Visible;
        OpenDiagnosticsFromHintButton.Visibility = OnboardingRecoveryText.Visibility;
        DismissRecoveryHintButton.Visibility = OnboardingRecoveryText.Visibility;

        switch (onboarding.PrimaryAction)
        {
            case HostPrimaryActionKind.InitializeSession:
                PrimaryActionButton.Content = "세션 시작";
                PrimaryActionButton.IsEnabled = uiState.InitializeEnabled;
                break;
            case HostPrimaryActionKind.LoadAvatar:
                PrimaryActionButton.Content = "아바타 불러오기";
                PrimaryActionButton.IsEnabled = uiState.LoadEnabled && !_isLoadRunning;
                break;
            case HostPrimaryActionKind.StartOutput:
                PrimaryActionButton.Content = "출력 시작";
                PrimaryActionButton.IsEnabled = (uiState.StartSpoutEnabled || uiState.StartOscEnabled) && !operation.IsBusy;
                break;
            default:
                PrimaryActionButton.Content = "다음 단계 대기";
                PrimaryActionButton.IsEnabled = false;
                break;
        }
        PrimaryActionButton.Tag = onboarding.PrimaryAction;

        if (_lastTrackedOnboardingStep != onboarding.Step)
        {
            _controller.TrackOnboardingUiEvent(
                "onboarding_step_viewed",
                onboarding.Step,
                onboarding.PrimaryAction,
                onboarding.Actionability,
                onboarding.BlockReasonShort);
            _lastTrackedOnboardingStep = onboarding.Step;
        }
        SyncRenderControlsFromState();
        SyncPresetControlsFromState();
    }

    private static void SetOnboardingStepState(TextBlock target, bool completed)
    {
        target.Text = completed ? "완료" : "다음 단계";
    }

    private bool ShouldSkipRenderInteraction()
    {
        return _isSyncingRenderUi || _controller.OperationState.IsBusy;
    }

    private void EndRenderRightDrag(Microsoft.UI.Input.Pointer? pointer)
    {
        if (!_isRenderRightDragging)
        {
            return;
        }

        _isRenderRightDragging = false;
        if (pointer is not null)
        {
            RenderHost.ReleasePointerCapture(pointer);
        }
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
        runtimeSb.AppendLine($"SpoutBackend: {runtime.SpoutBackend}");
        runtimeSb.AppendLine($"SpoutStrictMode: {runtime.SpoutStrictMode}");
        runtimeSb.AppendLine($"SpoutFallbackCount: {runtime.SpoutFallbackCount}");
        runtimeSb.AppendLine($"SpoutLastErrorCode: {runtime.SpoutLastErrorCode}");
        runtimeSb.AppendLine($"NativeCoreModulePath: {NormalizeDiagField(runtime.NativeCoreModulePath)}");
        runtimeSb.AppendLine($"NativeCoreModuleTimestampUtc: {NormalizeDiagField(runtime.NativeCoreModuleTimestampUtc)}");
        runtimeSb.AppendLine($"OscActive: {runtime.OscActive}");
        runtimeSb.AppendLine($"LastFrameMs: {runtime.LastFrameMs:F3}");
        var tracking = _controller.TrackingDiagnostics;
        runtimeSb.AppendLine($"Tracking: active={tracking.IsActive}, source={tracking.SourceType}, active_source={tracking.ActiveSource}, source_status={tracking.SourceStatus}, format={tracking.DetectedFormat}, upper_active={tracking.UpperBodyTrackingActive}, upper_conf={tracking.UpperBodyConfidence:F2}, upper_age_ms={tracking.UpperBodyPacketAgeMs}, upper_status={tracking.UpperBodyStatus}, fps={tracking.InputFps:F1}, capture_fps={tracking.CaptureFps:F1}, infer_ms={tracking.InferenceMsAvg:F1}, arkit52={tracking.Arkit52SubmittedCount}/52, arkit52_strict={tracking.Arkit52StrictCount}, arkit52_fallback={tracking.Arkit52FallbackCount}, arkit52_missing={tracking.Arkit52MissingCount}, arkit52_score={tracking.Arkit52QualityScore:F2}, arkit52_stage_ms={tracking.Arkit52QualityStageMs:F2}, age_ms={tracking.LastPacketAgeMs}, stale={tracking.IsStale}, backend_ready={tracking.ModelSchemaOk}, packets={tracking.ReceivedPackets}, dropped={tracking.DroppedPackets}, parse_err={tracking.ParseErrors}, fallback={tracking.FallbackCount}, calibration={tracking.CalibrationState}, confidence={tracking.ConfidenceSummary}, err={tracking.LastErrorCode}");
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
            avatarSb.AppendLine($"LastWarningCode: {NormalizeDiagField(info.LastWarningCode)}");
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

    private string BuildLogsText()
    {
        var logsSb = new StringBuilder();
        foreach (var log in _controller.LogEntries)
        {
            logsSb.AppendLine($"{log.TimestampUtc:HH:mm:ss.fff} [{log.Source}] {log.ResultCode} {log.Message}");
        }

        return logsSb.ToString();
    }

    private static string NormalizeDiagField(string text)
    {
        return string.IsNullOrWhiteSpace(text) ? "none" : text;
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
            Title = "작업 확인 (Confirm Action)",
            Content = content,
            PrimaryButtonText = "예 (Yes)",
            CloseButtonText = "아니오 (No)",
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
            CloseButtonText = "확인 (OK)",
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
            if (File.Exists(session.AvatarPath))
            {
                EnqueueThumbnailGeneration(session.AvatarPath, force: false);
            }
        }

        SpoutChannelTextBox.Text = session.SpoutChannelName;
        OscBindPortTextBox.Text = session.OscBindPort.ToString(CultureInfo.InvariantCulture);
        OscPublishAddressTextBox.Text = session.OscPublishAddress;
        TrackingPortTextBox.Text = session.Tracking.ListenPort.ToString(CultureInfo.InvariantCulture);
        TrackingSourceComboBox.SelectedIndex = session.Tracking.SourceType switch
        {
            TrackingSourceType.WebcamMediapipe => 2,
            TrackingSourceType.OscIfacial => 1,
            _ => 0,
        };
        TrackingInferenceFpsTextBox.Text = session.Tracking.InferenceFpsCap.ToString(CultureInfo.InvariantCulture);
        TrackingParseWarnThresholdTextBox.Text = session.Tracking.ParseErrorWarnThreshold.ToString(CultureInfo.InvariantCulture);
        TrackingDropWarnThresholdTextBox.Text = session.Tracking.DroppedPacketWarnThreshold.ToString(CultureInfo.InvariantCulture);
        TrackingUpperBodyEnabledCheckBox.IsChecked = session.Tracking.UpperBodyEnabled;
        RefreshTrackingWebcamDevices(session.Tracking.CameraDeviceKey);

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

