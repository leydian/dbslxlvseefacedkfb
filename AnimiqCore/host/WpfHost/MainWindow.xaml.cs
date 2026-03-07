using System;
using System.Collections.Generic;
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
using System.Windows.Media.Animation;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using HostCore;
using Microsoft.Win32;

namespace WpfHost;

public partial class MainWindow : Window
{
    private sealed record WebcamDeviceItem(string Key, string Label, bool IsAvailable);
    private enum UiSection
    {
        GettingStarted,
        SessionAvatar,
        Render,
        Outputs,
        Tracking,
        PlatformOps,
    }

    private const string UiModeBeginner = "beginner";
    private const string UiModeAdvanced = "advanced";
    private readonly HostController _controller = new();
    internal HostController Controller => _controller;
    private FloatingAvatarWindow? _floatingAvatarWindow;
    private readonly DispatcherTimer _timer = new();
    private readonly DispatcherTimer _uiRefreshTimer = new();
    private readonly DispatcherTimer _resizeTimer = new();
    private readonly DispatcherTimer _renderApplyTimer = new();
    private readonly Stopwatch _frameTimer = Stopwatch.StartNew();
    private const float DragPixelsPerYawDegree = 3.0f;
    private const float WheelNotchFovStep = 1.0f;
    private bool _isSyncingRenderUi;
    private bool _isSyncingPoseUi;
    private bool _isSyncingTrackingPoseFilterUi;
    private bool _isSyncingTrackingBasicUi;
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
    private string _lastRuntimeStaticBlockKey = string.Empty;
    private string _lastRuntimeStaticBlockText = string.Empty;
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
    private bool _isRenderOnlyMode;
    private DateTimeOffset _renderOnlyHintVisibleUntil = DateTimeOffset.MinValue;
    private static readonly TimeSpan RenderOnlyHintDuration = TimeSpan.FromSeconds(3.0);
    private const int WebcamProbeLimit = 10;
    private readonly AvatarThumbnailPipeline _thumbnailPipeline;
    private bool _syncingRecentAvatarList;
    private UiSection _activeSection = UiSection.GettingStarted;
    private bool _diagnosticsPinnedVisible;
    private bool _isDarkTheme;
    private HostOnboardingStep? _lastTrackedOnboardingStep;
    private bool _showTrackingIpv4Hint = true;
    private string _recommendedTrackingIpv4 = string.Empty;
    private const int TrackingDefaultStaleTimeoutMs = 500;
    private const int TrackingDefaultInferenceFps = 30;

    private static string ToPersistSectionKey(UiSection section) => section switch
    {
        UiSection.GettingStarted => "getting_started",
        UiSection.SessionAvatar => "session_avatar",
        UiSection.Render => "render",
        UiSection.Outputs => "outputs",
        UiSection.Tracking => "tracking",
        UiSection.PlatformOps => "platform_ops",
        _ => "getting_started",
    };

    private static UiSection ParseSectionKey(string? value) => value?.Trim().ToLowerInvariant() switch
    {
        "session_avatar" => UiSection.SessionAvatar,
        "render" => UiSection.Render,
        "outputs" => UiSection.Outputs,
        "tracking" => UiSection.Tracking,
        "platform_ops" => UiSection.PlatformOps,
        _ => UiSection.GettingStarted,
    };

    public MainWindow()
    {
        InitializeComponent();
        SourceInitialized += MainWindow_SourceInitialized;
        Closed += MainWindow_Closed;
        SizeChanged += MainWindow_SizeChanged;
        PreviewKeyDown += MainWindow_PreviewKeyDown;

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
        RenderHost.RenderRightDragStarted += RenderHost_RenderRightDragStarted;
        RenderHost.RenderRightDragMoved += RenderHost_RenderRightDragMoved;
        RenderHost.RenderRightDragCompleted += RenderHost_RenderRightDragCompleted;
        RenderHost.RenderMouseWheel += RenderHost_RenderMouseWheel;

        _isLogsTabActive = DiagnosticsTabControl.SelectedIndex == 2;
        ApplyThemeResources();
        ApplySessionDefaultsToUi();
        RefreshRecentAvatarList();
        RefreshValidationState();
        SyncRenderControlsFromState();
        SyncPoseControlsFromState();
        SyncTrackingPoseFilterControlsFromState();
        SyncTrackingBasicControlsFromState();
        SyncPosePresetControlsFromState();
        SyncArmSuggestionControlsFromState();
        SyncArmTuningControlsFromState();
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
        PreviewKeyDown -= MainWindow_PreviewKeyDown;
        RenderHost.RenderRightDragStarted -= RenderHost_RenderRightDragStarted;
        RenderHost.RenderRightDragMoved -= RenderHost_RenderRightDragMoved;
        RenderHost.RenderRightDragCompleted -= RenderHost_RenderRightDragCompleted;
        RenderHost.RenderMouseWheel -= RenderHost_RenderMouseWheel;
        _timer.Stop();
        _uiRefreshTimer.Stop();
        _resizeTimer.Stop();
        _renderApplyTimer.Stop();
        _thumbnailPipeline.StateChanged -= ThumbnailPipeline_StateChanged;
        _thumbnailPipeline.WorkerRunningChanged -= ThumbnailPipeline_WorkerRunningChanged;
        _ = _controller.Shutdown();
    }

    private void Initialize_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        if (_controller.SessionState.IsInitialized &&
            !Confirm("세션이 이미 시작되어 있습니다. 다시 시작하면서 현재 출력/아바타를 초기화할까요?"))
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

        if (!Confirm("세션을 종료하고 현재 출력을 중지할까요?"))
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
            Filter = "Avatar Files (*.vrm;*.vsfavatar;*.miq)|*.vrm;*.vsfavatar;*.miq|All Files (*.*)|*.*",
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

        SyncAvatarFacingControls();
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

    private void AvatarFacingToggle_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy || _isLoadRunning)
        {
            return;
        }

        var path = AvatarPathTextBox.Text.Trim();
        if (string.IsNullOrWhiteSpace(path))
        {
            return;
        }

        var rc = _controller.ToggleAvatarPreviewFlip180(path);
        if (rc != NcResultCode.Ok)
        {
            MessageBox.Show(this, $"아바타 방향 전환에 실패했습니다. ({rc})", "적용 실패", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        RefreshRecentAvatarList();
        SyncRenderControlsFromState();
        SyncAvatarFacingControls();
        UpdateUiState();
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
                ToolTip = entry.AvatarPath,
            };
            RecentAvatarListBox.Items.Add(item);
            if (string.Equals(entry.AvatarPath, selectedPath, StringComparison.OrdinalIgnoreCase))
            {
                RecentAvatarListBox.SelectedItem = item;
            }
        }
        _syncingRecentAvatarList = false;
        UpdateAvatarPreview(AvatarPathTextBox.Text.Trim());
        SyncAvatarFacingControls();
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

    private void SyncAvatarFacingControls()
    {
        if (AvatarFacingToggleButton is null || AvatarFacingStatusText is null)
        {
            return;
        }

        var path = AvatarPathTextBox.Text.Trim();
        var hasPath = !string.IsNullOrWhiteSpace(path);
        var flipEnabled = hasPath && _controller.GetAvatarPreviewFlip180(path);
        AvatarFacingStatusText.Text = flipEnabled ? "저장: 반전(180)" : "저장: 기본";
        AvatarFacingToggleButton.Content = flipEnabled
            ? "앞/뒤 전환 (현재: 반전)"
            : "앞/뒤 전환 (현재: 기본)";
    }

    private static BitmapImage? LoadPreviewBitmap(string path)
    {
        try
        {
            var bitmap = new BitmapImage();
            bitmap.BeginInit();
            bitmap.CacheOption = BitmapCacheOption.OnLoad;
            bitmap.CreateOptions = BitmapCreateOptions.IgnoreImageCache;
            bitmap.UriSource = new Uri(path);
            bitmap.EndInit();
            bitmap.Freeze();
            return bitmap;
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

    private void ThumbnailPipeline_StateChanged(object? sender, AvatarThumbnailState e)
    {
        RunOnUiThread(() =>
        {
            _controller.UpdateRecentAvatarThumbnail(e.AvatarPath, e.ThumbnailPath, e.Status, e.LastError);
            RefreshRecentAvatarList();
            UpdateUiState();
        });
    }

    private void ThumbnailPipeline_WorkerRunningChanged(object? sender, bool e)
    {
        RunOnUiThread(() =>
        {
            UpdateAvatarPreview(AvatarPathTextBox.Text.Trim());
            UpdateUiState();
        });
    }

    private void BeginnerMode_Click(object sender, RoutedEventArgs e)
    {
        SetUiMode(UiModeBeginner, persist: true);
    }

    private void AdvancedMode_Click(object sender, RoutedEventArgs e)
    {
        SetUiMode(UiModeAdvanced, persist: true);
    }

    private void ViewModeToggle_Click(object sender, RoutedEventArgs e)
    {
        var isAdvanced = string.Equals(_uiMode, UiModeAdvanced, StringComparison.Ordinal);
        SetUiMode(isAdvanced ? UiModeBeginner : UiModeAdvanced, persist: true);
    }

    private void ThemeToggle_Click(object sender, RoutedEventArgs e)
    {
        _isDarkTheme = !_isDarkTheme;
        ApplyThemeResources();
        ApplyNavRailState();
        PersistUiWorkspaceState();
    }

    private void NavGettingStarted_Click(object sender, RoutedEventArgs e) => ActivateSection(UiSection.GettingStarted);
    private void NavSessionAvatar_Click(object sender, RoutedEventArgs e) => ActivateSection(UiSection.SessionAvatar);
    private void NavRender_Click(object sender, RoutedEventArgs e) => ActivateSection(UiSection.Render);
    private void NavOutputs_Click(object sender, RoutedEventArgs e) => ActivateSection(UiSection.Outputs);
    private void NavTracking_Click(object sender, RoutedEventArgs e) => ActivateSection(UiSection.Tracking);
    private void NavPlatformOps_Click(object sender, RoutedEventArgs e) => ActivateSection(UiSection.PlatformOps);

    private void PopOutRender_Click(object sender, RoutedEventArgs e)
    {
        if (_floatingAvatarWindow is { IsLoaded: true })
        {
            _floatingAvatarWindow.Activate();
            return;
        }
        _floatingAvatarWindow = new FloatingAvatarWindow(_controller);
        _floatingAvatarWindow.Show();
    }

    private void ToggleDiagnosticsPanel_Click(object sender, RoutedEventArgs e)
    {
        if (!string.Equals(_uiMode, UiModeAdvanced, StringComparison.Ordinal))
        {
            return;
        }

        _diagnosticsPinnedVisible = !_diagnosticsPinnedVisible;
        if (_diagnosticsPinnedVisible)
        {
            _diagnosticsForcedVisible = false;
        }
        ApplyModeVisibility();
        PersistUiWorkspaceState();
    }

    private void RenderOnlyToggle_Click(object sender, RoutedEventArgs e)
    {
        SetRenderOnlyMode(!_isRenderOnlyMode);
    }

    private void MainWindow_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (TryHandleNavRailKeyboard(e) || TryHandleGlobalShortcut(e))
        {
            e.Handled = true;
            return;
        }

        if (e.Key == Key.F11)
        {
            SetRenderOnlyMode(!_isRenderOnlyMode);
            e.Handled = true;
        }
    }

    private bool TryHandleGlobalShortcut(KeyEventArgs e)
    {
        var modifiers = Keyboard.Modifiers;
        if (modifiers != ModifierKeys.Control)
        {
            return false;
        }

        switch (e.Key)
        {
            case Key.D1:
            case Key.NumPad1:
                ActivateSection(UiSection.GettingStarted);
                return true;
            case Key.D2:
            case Key.NumPad2:
                ActivateSection(UiSection.SessionAvatar);
                return true;
            case Key.D3:
            case Key.NumPad3:
                ActivateSection(UiSection.Render);
                return true;
            case Key.D4:
            case Key.NumPad4:
                ActivateSection(UiSection.Outputs);
                return true;
            case Key.D5:
            case Key.NumPad5:
                ActivateSection(UiSection.Tracking);
                return true;
            case Key.D6:
            case Key.NumPad6:
                ActivateSection(UiSection.PlatformOps);
                return true;
            case Key.D:
                ToggleDiagnosticsPanel_Click(this, new RoutedEventArgs());
                return true;
            case Key.T:
                ThemeToggle_Click(this, new RoutedEventArgs());
                return true;
            default:
                return false;
        }
    }

    private bool TryHandleNavRailKeyboard(KeyEventArgs e)
    {
        if (e.OriginalSource is not DependencyObject source)
        {
            return false;
        }

        var navButtons = GetNavButtons().Where(button => button.IsEnabled && button.Visibility == Visibility.Visible).ToList();
        if (navButtons.Count == 0)
        {
            return false;
        }

        var focusedButton = navButtons.FirstOrDefault(button => button.IsKeyboardFocusWithin || ReferenceEquals(button, source));
        if (focusedButton is null)
        {
            return false;
        }

        var index = navButtons.IndexOf(focusedButton);
        if (index < 0)
        {
            return false;
        }

        switch (e.Key)
        {
            case Key.Up:
                navButtons[(index - 1 + navButtons.Count) % navButtons.Count].Focus();
                return true;
            case Key.Down:
                navButtons[(index + 1) % navButtons.Count].Focus();
                return true;
            case Key.Enter:
            case Key.Space:
                focusedButton.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                return true;
            default:
                return false;
        }
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
                if (!string.IsNullOrWhiteSpace(_lastFailureSource))
                {
                    OpenDiagnosticsFromHint_Click(sender, e);
                }
                break;
        }
    }

    private void QuickInitialize_Click(object sender, RoutedEventArgs e)
    {
        Initialize_Click(sender, e);
    }

    private void QuickLoadAvatar_Click(object sender, RoutedEventArgs e)
    {
        RefreshValidationState();
        if (!_validationState.AvatarPathValid && !_controller.SessionState.IsInitialized)
        {
            ActivateSection(UiSection.SessionAvatar);
            return;
        }

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
                $"Loaded nativecore SHA256: {NormalizeDiagField(runtime.NativeCoreModuleSha256)}\n" +
                $"Expected nativecore SHA256: {NormalizeDiagField(runtime.ExpectedNativeCoreModuleSha256)}\n" +
                $"WarningCode: {NormalizeDiagField(runtime.RuntimePathWarningCode)}");
            return;
        }
        if (runtime.RuntimeModuleStaleVsBuildOutput)
        {
            RevealDiagnosticsForFailure("LoadAvatar.RuntimeModuleStale");
            ReportUserFailure(
                "LoadAvatar",
                "Runtime nativecore.dll is older than current build output. Republish dist/wpf and retry.",
                $"Loaded nativecore path: {NormalizeDiagField(runtime.NativeCoreModulePath)}\n" +
                $"Loaded nativecore timestamp(UTC): {NormalizeDiagField(runtime.NativeCoreModuleTimestampUtc)}\n" +
                $"Loaded nativecore SHA256: {NormalizeDiagField(runtime.NativeCoreModuleSha256)}\n" +
                $"Build nativecore path: {NormalizeDiagField(runtime.BuildNativeCoreModulePath)}\n" +
                $"Build nativecore timestamp(UTC): {NormalizeDiagField(runtime.BuildNativeCoreModuleTimestampUtc)}\n" +
                $"Build nativecore SHA256: {NormalizeDiagField(runtime.BuildNativeCoreModuleSha256)}\n" +
                $"Expected nativecore SHA256: {NormalizeDiagField(runtime.ExpectedNativeCoreModuleSha256)}\n" +
                $"WarningCode: {NormalizeDiagField(runtime.RuntimeTimestampWarningCode)}");
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

        var loadedPath = AvatarPathTextBox.Text.Trim();
        if (!string.IsNullOrWhiteSpace(loadedPath) && File.Exists(loadedPath))
        {
            _controller.RecordAvatarSelection(loadedPath);
            EnqueueThumbnailGeneration(loadedPath, force: false);
            RefreshRecentAvatarList();
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

        if (!Confirm("현재 아바타를 내릴까요?"))
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
            !Confirm("화면 출력이 이미 켜져 있습니다. 현재 설정으로 다시 시작할까요?"))
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
            channel = "Animiq";
            SpoutChannelTextBox.Text = channel;
        }
        var rc = _controller.StartSpout(metrics.pixelWidth, metrics.pixelHeight, 60, channel);
        if (rc != NcResultCode.Ok)
        {
            RevealDiagnosticsForFailure("StartSpout");
            ReportUserFailure(
                "StartSpout",
                $"화면 출력 시작에 실패했습니다 ({rc}). 채널 이름을 확인하고 다시 시도해 주세요.",
                $"화면 출력 시작 실패: {rc}");
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

        if (!Confirm("화면 출력을 중지할까요?"))
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
            !Confirm("모션 출력이 이미 켜져 있습니다. 현재 설정으로 다시 시작할까요?"))
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
                $"모션 출력 시작에 실패했습니다 ({rc}). 포트/주소를 확인하고 다시 시도해 주세요.",
                $"모션 출력 시작 실패: {rc}");
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

        if (!Confirm("모션 출력을 중지할까요?"))
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
            inferenceFpsCap = TrackingDefaultInferenceFps;
            TrackingInferenceFpsTextBox.Text = TrackingDefaultInferenceFps.ToString(CultureInfo.InvariantCulture);
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
        var staleTimeoutMs = TrackingStaleTimeoutSlider is null
            ? current.StaleTimeoutMs
            : (int)Math.Round(TrackingStaleTimeoutSlider.Value);
        var cameraKey = (TrackingWebcamDeviceComboBox.SelectedItem as WebcamDeviceItem)?.Key
            ?? current.CameraDeviceKey;
        _controller.ConfigureTrackingInputSettings(
            listenPort,
            staleTimeoutMs,
            sourceType,
            cameraKey,
            inferenceFpsCap,
            parseWarnThreshold,
            dropWarnThreshold,
            sourceLockMode: sourceLockMode,
            latencyProfile: latencyProfile,
            poseFilterProfile: poseFilterProfile,
            poseDeadbandDeg: (float)TrackingPoseDeadbandSlider.Value,
            autoStabilityTuningEnabled: TrackingAutoStabilityCheckBox.IsChecked == true,
            upperBodyEnabled: TrackingUpperBodyEnabledCheckBox.IsChecked == true);

        var rc = _controller.StartTracking(
            listenPort,
            staleTimeoutMs: _controller.GetTrackingInputSettings().StaleTimeoutMs);
        if (rc != NcResultCode.Ok)
        {
            var detail = BuildTrackingStartFailureMessage(rc);
            MessageBox.Show(this, detail, "트래킹 (Tracking)", MessageBoxButton.OK, MessageBoxImage.Warning);
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
        SyncTrackingBasicControlsFromState();
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
        SyncTrackingBasicControlsFromState();
    }

    private void TrackingStabilitySlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (TrackingStabilitySlider is null || TrackingStabilityValueText is null)
        {
            return;
        }

        var value = (int)Math.Round(TrackingStabilitySlider.Value);
        TrackingStabilityValueText.Text = value.ToString(CultureInfo.InvariantCulture);
        if (!_uiReady || _isSyncingTrackingBasicUi || _controller.OperationState.IsBusy || _controller.TrackingDiagnostics.IsActive)
        {
            return;
        }

        var poseFilterProfile = value switch
        {
            <= 33 => PoseFilterProfile.Reactive,
            <= 66 => PoseFilterProfile.Balanced,
            _ => PoseFilterProfile.Stable,
        };
        var latencyProfile = value switch
        {
            <= 33 => TrackingLatencyProfile.LowLatency,
            <= 66 => TrackingLatencyProfile.Balanced,
            _ => TrackingLatencyProfile.Stable,
        };
        var deadbandDeg = (float)Math.Clamp((value / 100.0) * 2.2 + 0.1, 0.0, 3.0);
        var current = _controller.GetTrackingInputSettings();
        _controller.ConfigureTrackingInputSettings(
            current.ListenPort,
            current.StaleTimeoutMs,
            latencyProfile: latencyProfile,
            poseFilterProfile: poseFilterProfile,
            poseDeadbandDeg: deadbandDeg);

        _isSyncingTrackingPoseFilterUi = true;
        TrackingLatencyProfileComboBox.SelectedIndex = latencyProfile switch
        {
            TrackingLatencyProfile.LowLatency => 0,
            TrackingLatencyProfile.Stable => 2,
            _ => 1,
        };
        TrackingPoseFilterProfileComboBox.SelectedIndex = poseFilterProfile switch
        {
            PoseFilterProfile.Reactive => 0,
            PoseFilterProfile.Balanced => 1,
            _ => 2,
        };
        TrackingPoseDeadbandSlider.Value = deadbandDeg;
        TrackingPoseDeadbandValueText.Text = $"{deadbandDeg:F2}\u00b0";
        _isSyncingTrackingPoseFilterUi = false;
    }

    private void TrackingStaleTimeoutSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (TrackingStaleTimeoutSlider is null || TrackingStaleTimeoutValueText is null)
        {
            return;
        }

        var staleTimeoutMs = (int)Math.Round(TrackingStaleTimeoutSlider.Value);
        TrackingStaleTimeoutValueText.Text = staleTimeoutMs.ToString(CultureInfo.InvariantCulture);
        if (!_uiReady || _isSyncingTrackingBasicUi || _controller.OperationState.IsBusy || _controller.TrackingDiagnostics.IsActive)
        {
            return;
        }

        var current = _controller.GetTrackingInputSettings();
        _controller.ConfigureTrackingInputSettings(
            current.ListenPort,
            staleTimeoutMs);
    }

    private void TrackingShowPosition_Changed(object sender, RoutedEventArgs e)
    {
        if (TrackingShowPositionCheckBox is null || DebugOverlayCheckBox is null)
        {
            return;
        }

        if (_isSyncingTrackingBasicUi)
        {
            return;
        }

        if (ShouldSkipRenderInteraction())
        {
            return;
        }

        DebugOverlayCheckBox.IsChecked = TrackingShowPositionCheckBox.IsChecked == true;
        QueueRenderApply();
    }

    private void TrackingResetDefaults_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy || _controller.TrackingDiagnostics.IsActive)
        {
            return;
        }

        TrackingInferenceFpsTextBox.Text = TrackingDefaultInferenceFps.ToString(CultureInfo.InvariantCulture);
        TrackingParseWarnThresholdTextBox.Text = "10";
        TrackingDropWarnThresholdTextBox.Text = "10";
        TrackingSourceLockComboBox.SelectedIndex = 0;
        TrackingSourceComboBox.SelectedIndex = 0;
        TrackingUpperBodyEnabledCheckBox.IsChecked = true;
        TrackingAutoStabilityCheckBox.IsChecked = true;

        _isSyncingTrackingBasicUi = true;
        TrackingStabilitySlider.Value = 70;
        TrackingStabilityValueText.Text = "70";
        TrackingStaleTimeoutSlider.Value = TrackingDefaultStaleTimeoutMs;
        TrackingStaleTimeoutValueText.Text = TrackingDefaultStaleTimeoutMs.ToString(CultureInfo.InvariantCulture);
        _isSyncingTrackingBasicUi = false;

        _isSyncingTrackingPoseFilterUi = true;
        TrackingLatencyProfileComboBox.SelectedIndex = 2;
        TrackingPoseFilterProfileComboBox.SelectedIndex = 2;
        TrackingPoseDeadbandSlider.Value = 0.9;
        TrackingPoseDeadbandValueText.Text = "0.90°";
        _isSyncingTrackingPoseFilterUi = false;

        var current = _controller.GetTrackingInputSettings();
        _controller.ConfigureTrackingInputSettings(
            current.ListenPort,
            TrackingDefaultStaleTimeoutMs,
            sourceType: TrackingSourceType.HybridAuto,
            inferenceFpsCap: TrackingDefaultInferenceFps,
            parseErrorWarnThreshold: 10,
            droppedPacketWarnThreshold: 10,
            sourceLockMode: TrackingSourceLockMode.Auto,
            latencyProfile: TrackingLatencyProfile.Stable,
            poseFilterProfile: PoseFilterProfile.Stable,
            poseDeadbandDeg: 0.9f,
            autoStabilityTuningEnabled: true,
            upperBodyEnabled: true);

        SyncTrackingBasicControlsFromState();
    }

    private void RefreshTrackingWebcamDevices(string? preferredKey = null)
    {
        var devices = _controller.GetAvailableWebcamDevices(maxProbe: WebcamProbeLimit);
        var items = devices
            .Select(d => new WebcamDeviceItem(
                d.DeviceKey,
                d.IsAvailable
                    ? $"{d.DisplayName} ({(string.IsNullOrWhiteSpace(d.DeviceKey) ? "default" : d.DeviceKey)})"
                    : $"{d.DisplayName} ({d.DeviceKey}) - unavailable",
                d.IsAvailable))
            .ToList();
        TrackingWebcamDeviceComboBox.ItemsSource = items;
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

    private void RefreshTrackingIpv4Hint()
    {
        var (recommendedIpv4, allIpv4) = _controller.GetLocalIpv4Hint();
        _recommendedTrackingIpv4 = recommendedIpv4;
        TrackingIpv4RecommendedText.Text = string.IsNullOrWhiteSpace(recommendedIpv4)
            ? "감지 실패 (직접 입력 필요)"
            : recommendedIpv4;
        TrackingIpv4AllText.Text = string.IsNullOrWhiteSpace(allIpv4)
            ? "-"
            : allIpv4;
        TrackingIpv4CopyButton.IsEnabled = !string.IsNullOrWhiteSpace(_recommendedTrackingIpv4);
    }

    private void ApplyTrackingIpv4HintVisibility()
    {
        TrackingIpv4HintPanel.Visibility = _showTrackingIpv4Hint ? Visibility.Visible : Visibility.Collapsed;
        TrackingIpv4ToggleButton.Content = _showTrackingIpv4Hint ? "IPv4 안내 숨기기" : "IPv4 안내 보기";
    }

    private void ToggleTrackingIpv4Hint_Click(object sender, RoutedEventArgs e)
    {
        _showTrackingIpv4Hint = !_showTrackingIpv4Hint;
        ApplyTrackingIpv4HintVisibility();
        _controller.SetUiTrackingIpv4HintVisible(_showTrackingIpv4Hint);
    }

    private void CopyTrackingIpv4_Click(object sender, RoutedEventArgs e)
    {
        if (string.IsNullOrWhiteSpace(_recommendedTrackingIpv4))
        {
            return;
        }

        Clipboard.SetText(_recommendedTrackingIpv4);
    }

    private void SetTrackingWebcamDevicesPending(string? preferredKey = null)
    {
        var target = preferredKey ?? _controller.GetTrackingInputSettings().CameraDeviceKey;
        var items = new[]
        {
            new WebcamDeviceItem(string.Empty, "Default Camera (scan pending)", true),
            new WebcamDeviceItem("0", "Camera 0 (scan pending)", true),
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

    private void CopyRuntime_Click(object sender, RoutedEventArgs e)
    {
        Clipboard.SetText(RuntimeDiagnosticsTextBox.Text);
    }

    private void CopyAvatar_Click(object sender, RoutedEventArgs e)
    {
        Clipboard.SetText(AvatarDiagnosticsTextBox.Text);
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
        var outputDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "AnimiqHost", "diagnostics");
        var path = _controller.ExportDiagnosticsBundle(outputDir);
        MessageBox.Show(this, $"진단 번들을 생성했습니다:\n{path}\n(Diagnostics bundle created)", "진단 내보내기 (Export Diagnostics)", MessageBoxButton.OK, MessageBoxImage.Information);
    }

    private void ExportMetrics_Click(object sender, RoutedEventArgs e)
    {
        var outputDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "AnimiqHost", "metrics");
        var path = _controller.ExportRollingMetricsCsv(Path.Combine(outputDir, $"metrics_{DateTimeOffset.UtcNow:yyyyMMdd_HHmmss}.csv"));
        MessageBox.Show(this, $"메트릭을 내보냈습니다:\n{path}\n(Metrics exported)", "메트릭 내보내기 (Export Metrics)", MessageBoxButton.OK, MessageBoxImage.Information);
    }

    private void ProfileQuality_Click(object sender, RoutedEventArgs e)
    {
        _ = _controller.ApplyRenderProfile("quality");
        CurrentRenderProfileText.Text = "현재: 화질 우선";
    }

    private void ProfilePerformance_Click(object sender, RoutedEventArgs e)
    {
        _ = _controller.ApplyRenderProfile("performance");
        CurrentRenderProfileText.Text = "현재: 성능 우선";
    }

    private void ProfileStability_Click(object sender, RoutedEventArgs e)
    {
        _ = _controller.ApplyRenderProfile("stability");
        CurrentRenderProfileText.Text = "현재: 안정 우선";
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
        var outputDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "AnimiqHost", "telemetry");
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

        _renderApplyTimer.Stop();
        var rc = PushRenderUiState();
        if (rc == NcResultCode.Ok)
        {
            UpdateUiState();
        }
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

    private void LightEulerSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (LightPitchValueText is not null && LightPitchSlider is not null)
        {
            LightPitchValueText.Text = LightPitchSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
        }
        if (LightYawValueText is not null && LightYawSlider is not null)
        {
            LightYawValueText.Text = LightYawSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
        }
        if (ShouldSkipRenderInteraction())
        {
            return;
        }
        QueueRenderApply();
    }

    private void LightIntensitySlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (LightIntensityValueText is not null && LightIntensitySlider is not null)
        {
            LightIntensityValueText.Text = LightIntensitySlider.Value.ToString("F1", CultureInfo.InvariantCulture);
        }
        if (ShouldSkipRenderInteraction())
        {
            return;
        }
        QueueRenderApply();
    }

    private void LightRangeSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (LightRangeValueText is not null && LightRangeSlider is not null)
        {
            LightRangeValueText.Text = LightRangeSlider.Value.ToString("F1", CultureInfo.InvariantCulture);
        }
        if (ShouldSkipRenderInteraction())
        {
            return;
        }
        QueueRenderApply();
    }

    private void SpotAngleSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (SpotAngleValueText is not null && SpotAngleSlider is not null)
        {
            SpotAngleValueText.Text = SpotAngleSlider.Value.ToString("F1", CultureInfo.InvariantCulture);
        }
        if (ShouldSkipRenderInteraction())
        {
            return;
        }
        QueueRenderApply();
    }

    private void ShadowStrengthSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (ShadowStrengthValueText is not null && ShadowStrengthSlider is not null)
        {
            ShadowStrengthValueText.Text = ShadowStrengthSlider.Value.ToString("F2", CultureInfo.InvariantCulture);
        }
        if (ShouldSkipRenderInteraction())
        {
            return;
        }
        QueueRenderApply();
    }

    private void ShadowBiasSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (ShadowBiasValueText is not null && ShadowBiasSlider is not null)
        {
            ShadowBiasValueText.Text = ShadowBiasSlider.Value.ToString("F2", CultureInfo.InvariantCulture);
        }
        if (ShouldSkipRenderInteraction())
        {
            return;
        }
        QueueRenderApply();
    }

    private void AmbientIntensitySlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (AmbientIntensityValueText is not null && AmbientIntensitySlider is not null)
        {
            AmbientIntensityValueText.Text = AmbientIntensitySlider.Value.ToString("F2", CultureInfo.InvariantCulture);
        }
        if (ShouldSkipRenderInteraction())
        {
            return;
        }
        QueueRenderApply();
    }

    private void ShadowEnabled_Changed(object sender, RoutedEventArgs e)
    {
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

    private void ArmPoseSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (ArmBothPitchValueText is not null && ArmBothPitchSlider is not null)
        {
            ArmBothPitchValueText.Text = ArmBothPitchSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
        }
        if (ArmLeftPitchValueText is not null && ArmLeftPitchSlider is not null)
        {
            ArmLeftPitchValueText.Text = ArmLeftPitchSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
        }
        if (ArmRightPitchValueText is not null && ArmRightPitchSlider is not null)
        {
            ArmRightPitchValueText.Text = ArmRightPitchSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
        }
        if (ArmBothPitchSlider is null ||
            ArmLeftPitchSlider is null ||
            ArmRightPitchSlider is null ||
            ArmBothPitchValueText is null ||
            ArmLeftPitchValueText is null ||
            ArmRightPitchValueText is null)
        {
            return;
        }
        if (_isSyncingPoseUi || _controller.OperationState.IsBusy)
        {
            return;
        }

        if (ReferenceEquals(sender, ArmBothPitchSlider))
        {
            _isSyncingPoseUi = true;
            ArmLeftPitchSlider.Value = ArmBothPitchSlider.Value;
            ArmRightPitchSlider.Value = ArmBothPitchSlider.Value;
            ArmLeftPitchValueText.Text = ArmLeftPitchSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
            ArmRightPitchValueText.Text = ArmRightPitchSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
            _isSyncingPoseUi = false;
            ApplyArmPitchOffset(PoseBoneKind.LeftUpperArm, (float)ArmBothPitchSlider.Value);
            ApplyArmPitchOffset(PoseBoneKind.RightUpperArm, (float)ArmBothPitchSlider.Value);
            return;
        }

        if (ReferenceEquals(sender, ArmLeftPitchSlider))
        {
            ApplyArmPitchOffset(PoseBoneKind.LeftUpperArm, (float)ArmLeftPitchSlider.Value);
        }
        else if (ReferenceEquals(sender, ArmRightPitchSlider))
        {
            ApplyArmPitchOffset(PoseBoneKind.RightUpperArm, (float)ArmRightPitchSlider.Value);
        }

        _isSyncingPoseUi = true;
        ArmBothPitchSlider.Value = (ArmLeftPitchSlider.Value + ArmRightPitchSlider.Value) * 0.5;
        ArmBothPitchValueText.Text = ArmBothPitchSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
        _isSyncingPoseUi = false;
    }

    private void ArmTuning_Changed(object sender, RoutedEventArgs e)
    {
        if (!_uiReady || _controller.OperationState.IsBusy || ArmDeadbandSlider is null)
        {
            return;
        }

        if (ArmDeadbandValueText is not null)
        {
            ArmDeadbandValueText.Text = $"{ArmDeadbandSlider.Value:F2}\u00b0";
        }

        var current = _controller.ArmPoseTuning;
        _controller.ConfigureArmPoseTuning(current with
        {
            EnableSmoothing = ArmSmoothingCheckBox.IsChecked == true,
            DeadbandDeg = (float)ArmDeadbandSlider.Value,
        });
    }

    private void ArmTuning_Changed(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        ArmTuning_Changed(sender, new RoutedEventArgs());
    }

    private void ApplySuggestedArmPreset_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        if (SuggestedArmPresetComboBox.SelectedItem is not string selected || string.IsNullOrWhiteSpace(selected))
        {
            return;
        }

        _ = _controller.ApplySuggestedArmPreset(selected);
        SyncPoseControlsFromState();
        SyncArmSuggestionControlsFromState();
    }

    private void SaveSuggestedArmPreset_Click(object sender, RoutedEventArgs e)
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        if (SuggestedArmPresetComboBox.SelectedItem is not string selected || string.IsNullOrWhiteSpace(selected))
        {
            return;
        }

        if (_controller.ApplySuggestedArmPreset(selected) == NcResultCode.Ok)
        {
            var presetName = string.IsNullOrWhiteSpace(PosePresetNameTextBox.Text)
                ? selected
                : PosePresetNameTextBox.Text.Trim();
            if (_controller.SaveOrUpdatePosePreset(presetName))
            {
                SyncPosePresetControlsFromState();
            }
        }
        SyncPoseControlsFromState();
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
        if (_isRenderOnlyMode &&
            RenderOnlyHintOverlay.Visibility == Visibility.Visible &&
            DateTimeOffset.UtcNow > _renderOnlyHintVisibleUntil)
        {
            RenderOnlyHintOverlay.Visibility = Visibility.Collapsed;
        }
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
            RefreshRecentAvatarList();
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
            var isBlockedHint = e.Source.EndsWith(".Blocked", StringComparison.OrdinalIgnoreCase);
            if (!string.IsNullOrWhiteSpace(guide) && !isBlockedHint)
            {
                ShowFailureHint(e.Source, guide);
            }
            else if (isBlockedHint)
            {
                // Avoid sticky beginner hints for transient "operation in progress" blocking.
                if (_beginnerFailureHint.Contains("Current operation must finish", StringComparison.OrdinalIgnoreCase))
                {
                    ClearFailureHint();
                }
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
        var snapshot = _controller.LastSnapshot;
        var tracking = _controller.TrackingDiagnostics;
        var uiState = HostUiPolicy.EvaluateAvailability(
            session,
            outputs,
            operation,
            _validationState,
            _controller.RenderState,
            CameraModeComboBox.SelectedIndex == 2,
            snapshot.Runtime,
            snapshot.AvatarInfo,
            tracking);
        var statusText = HostUiPolicy.BuildStatusText(session, outputs, operation);
        var onboarding = HostUiPolicy.BuildOnboardingState(session, outputs, operation, _validationState);

        InitializeButton.IsEnabled = uiState.InitializeEnabled;
        ShutdownButton.IsEnabled = uiState.ShutdownEnabled;
        BrowseAvatarButton.IsEnabled = uiState.BrowseAvatarEnabled;
        LoadButton.IsEnabled = uiState.LoadEnabled;
        UnloadButton.IsEnabled = uiState.UnloadEnabled;
        CancelLoadButton.IsEnabled = _isLoadRunning;
        RecentAvatarListBox.IsEnabled = !operation.IsBusy && !_isLoadRunning;
        RetryAvatarPreviewButton.IsEnabled = !operation.IsBusy && !_isLoadRunning && !_thumbnailPipeline.IsWorkerRunning && File.Exists(AvatarPathTextBox.Text.Trim());
        AvatarFacingToggleButton.IsEnabled = !operation.IsBusy && !_isLoadRunning && !string.IsNullOrWhiteSpace(AvatarPathTextBox.Text.Trim());
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
        LightPitchSlider.IsEnabled = uiState.RenderControlsEnabled;
        LightYawSlider.IsEnabled = uiState.RenderControlsEnabled;
        LightIntensitySlider.IsEnabled = uiState.RenderControlsEnabled;
        LightRangeSlider.IsEnabled = uiState.RenderControlsEnabled;
        SpotAngleSlider.IsEnabled = uiState.RenderControlsEnabled;
        ShadowStrengthSlider.IsEnabled = uiState.RealtimeShadowEnabled;
        ShadowBiasSlider.IsEnabled = uiState.RealtimeShadowEnabled;
        AmbientIntensitySlider.IsEnabled = uiState.RenderControlsEnabled;
        ShadowEnabledCheckBox.IsEnabled = uiState.RealtimeShadowEnabled;
        MirrorModeCheckBox.IsEnabled = uiState.RenderControlsEnabled;
        DebugOverlayCheckBox.IsEnabled = uiState.RenderControlsEnabled;
        PoseBoneComboBox.IsEnabled = uiState.RenderControlsEnabled;
        PosePitchSlider.IsEnabled = uiState.RenderControlsEnabled;
        PoseYawSlider.IsEnabled = uiState.RenderControlsEnabled;
        PoseRollSlider.IsEnabled = uiState.RenderControlsEnabled;
        ArmBothPitchSlider.IsEnabled = uiState.ArmPoseEnabled;
        ArmLeftPitchSlider.IsEnabled = uiState.ArmPoseEnabled;
        ArmRightPitchSlider.IsEnabled = uiState.ArmPoseEnabled;
        PoseResetBoneButton.IsEnabled = uiState.RenderControlsEnabled;
        PoseResetAllButton.IsEnabled = uiState.RenderControlsEnabled;
        SavePosePresetButton.IsEnabled = uiState.RenderControlsEnabled;
        ApplyPosePresetButton.IsEnabled = uiState.RenderControlsEnabled;
        DeletePosePresetButton.IsEnabled = uiState.RenderControlsEnabled;
        PosePresetNameTextBox.IsEnabled = uiState.RenderControlsEnabled;
        PosePresetComboBox.IsEnabled = uiState.RenderControlsEnabled;
        ArmSmoothingCheckBox.IsEnabled = uiState.ArmPoseEnabled;
        ArmDeadbandSlider.IsEnabled = uiState.ArmPoseEnabled;
        ApplySuggestedArmPresetButton.IsEnabled = uiState.ArmPoseEnabled;
        SaveSuggestedArmPresetButton.IsEnabled = uiState.ArmPoseEnabled;
        SuggestedArmPresetComboBox.IsEnabled = uiState.ArmPoseEnabled;
        SavePresetButton.IsEnabled = uiState.RenderControlsEnabled;
        ApplyPresetButton.IsEnabled = uiState.RenderControlsEnabled;
        DeletePresetButton.IsEnabled = uiState.RenderControlsEnabled;
        ResetRenderButton.IsEnabled = uiState.RenderControlsEnabled;
        PresetNameTextBox.IsEnabled = uiState.RenderControlsEnabled;
        PresetComboBox.IsEnabled = uiState.RenderControlsEnabled;
        RunPreflightButton.IsEnabled = !operation.IsBusy;
        ExportDiagButton.IsEnabled = !operation.IsBusy;
        ExportMetricsButton.IsEnabled = !operation.IsBusy;
        RunQuickRecoveryButton.IsEnabled = !operation.IsBusy && !string.IsNullOrWhiteSpace(tracking.LastErrorCode);
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
        TrackingSourceLockComboBox.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingLatencyProfileComboBox.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingPoseFilterProfileComboBox.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingPoseDeadbandSlider.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingStabilitySlider.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingStaleTimeoutSlider.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingResetDefaultsButton.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingShowPositionCheckBox.IsEnabled = !operation.IsBusy;
        TrackingAutoStabilityCheckBox.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        TrackingUpperBodyEnabledCheckBox.IsEnabled = !operation.IsBusy && !tracking.IsActive;
        LoadTimeoutTextBox.IsEnabled = !operation.IsBusy && !_isLoadRunning;
        LoadButton.IsEnabled = LoadButton.IsEnabled && !_isLoadRunning;
        var armGateTooltip = uiState.ArmPoseEnabled ? null : $"팔 포즈 비활성 ({uiState.ArmPoseReasonCode})";
        ArmBothPitchSlider.ToolTip = armGateTooltip;
        ArmLeftPitchSlider.ToolTip = armGateTooltip;
        ArmRightPitchSlider.ToolTip = armGateTooltip;
        ArmSmoothingCheckBox.ToolTip = armGateTooltip;
        ArmDeadbandSlider.ToolTip = armGateTooltip;
        ApplySuggestedArmPresetButton.ToolTip = armGateTooltip;
        SaveSuggestedArmPresetButton.ToolTip = armGateTooltip;
        SuggestedArmPresetComboBox.ToolTip = armGateTooltip;
        var shadowGateTooltip = uiState.RealtimeShadowEnabled ? null : $"실시간 그림자 비활성 ({uiState.RealtimeShadowReasonCode})";
        ShadowEnabledCheckBox.ToolTip = shadowGateTooltip;
        ShadowStrengthSlider.ToolTip = shadowGateTooltip;
        ShadowBiasSlider.ToolTip = shadowGateTooltip;
        StartTrackingButton.ToolTip = uiState.ExpressionEnabled ? null : $"얼굴 표정 입력 경고 ({uiState.ExpressionReasonCode})";
        var trackingSettings = _controller.GetTrackingInputSettings();
        var trackingHint = BuildTrackingErrorHint(tracking.LastErrorCode);
        TrackingStatusText.Text = $"tracking={(tracking.IsActive ? "on" : "off")} source={tracking.SourceType} lock={tracking.SourceLockMode} active={tracking.ActiveSource} block={tracking.SwitchBlockedReason} source_status={tracking.SourceStatus} format={tracking.DetectedFormat} pose_filter={tracking.PoseFilterProfile} deadband_deg={tracking.PoseDeadbandDeg:F2} auto_stability={trackingSettings.AutoStabilityTuningEnabled} upper_body_enabled={trackingSettings.UpperBodyEnabled} upper_active={tracking.UpperBodyTrackingActive} upper_source={tracking.UpperBodyActiveSource} upper_conf={tracking.UpperBodyConfidence:F2} upper_age={tracking.UpperBodyPacketAgeMs} upper_status={tracking.UpperBodyStatus} fps={tracking.InputFps:F1} capture_fps={tracking.CaptureFps:F1} infer_ms={tracking.InferenceMsAvg:F1} lat_avg={tracking.LatencyAvgMs:F1} lat_p95={tracking.LatencyP95Ms:F1} stage_ms(c/p/s/u)={tracking.CaptureStageMs:F1}/{tracking.ParseStageMs:F1}/{tracking.SmoothStageMs:F1}/{tracking.SubmitStageMs:F1} arkit52={tracking.Arkit52SubmittedCount}/52 strict={tracking.Arkit52StrictCount} fb={tracking.Arkit52FallbackCount} missing={tracking.Arkit52MissingCount} q={tracking.Arkit52QualityScore:F2} qms={tracking.Arkit52QualityStageMs:F2} age_ms={tracking.LastPacketAgeMs} ifacial_age={tracking.IfacialPacketAgeMs} webcam_age={tracking.WebcamPacketAgeMs} stale={tracking.IsStale} backend_ready={tracking.ModelSchemaOk} packets={tracking.ReceivedPackets} dropped={tracking.DroppedPackets} parse_err={tracking.ParseErrors} parse_warn={trackingSettings.ParseErrorWarnThreshold} drop_warn={trackingSettings.DroppedPacketWarnThreshold} fallback={tracking.FallbackCount} switches={tracking.RecentSourceSwitchCount} switch_reason={tracking.LastSourceSwitchReason} switch_cd_ms={tracking.SourceSwitchCooldownRemainingMs} calib={tracking.CalibrationState} conf={tracking.ConfidenceSummary} ifm_keys_ok={tracking.IfmAcceptedKeySample} ifm_keys_drop={tracking.IfmDroppedKeySample} err={tracking.LastErrorCode}{trackingHint}";

        SessionStatusText.Text = statusText.SessionText switch
        {
            "Initialized" => "실행 중",
            _ => "중지",
        };
        AvatarStatusText.Text = statusText.AvatarText switch
        {
            "Loaded" => "불러옴",
            _ => "없음",
        };
        RenderStatusText.Text = statusText.RenderText;
        var advancedMode = string.Equals(_uiMode, UiModeAdvanced, StringComparison.Ordinal);
        OutputStatusText.Text = advancedMode
            ? outputs.SpoutActive && outputs.OscActive
                ? "Spout + OSC 켜짐"
                : outputs.SpoutActive
                    ? "Spout 켜짐"
                    : outputs.OscActive
                        ? "OSC 켜짐"
                        : "꺼짐"
            : (outputs.SpoutActive || outputs.OscActive) ? "켜짐" : "꺼짐";
        BusyStatusText.Text = operation.IsBusy ? operation.CurrentOperation : "대기";
        QuickStatusText.Text = $"세션: {SessionStatusText.Text} | 아바타: {AvatarStatusText.Text} | 출력: {OutputStatusText.Text}";
        QuickNextActionText.Text = onboarding.StepTitle;
        PrimaryActionDescriptionText.Text = onboarding.Instruction;
        NextActionSummaryText.Text = onboarding.NextActionSummary;
        BlockReasonShortText.Text = onboarding.BlockReasonShort;
        BlockReasonShortText.Visibility = string.IsNullOrWhiteSpace(onboarding.BlockReasonShort)
            ? Visibility.Collapsed
            : Visibility.Visible;
        if (onboarding.Actionability == HostActionability.Blocked)
        {
            ActionabilityBadgeText.Text = "대기 필요";
            ActionabilityBadgeText.Foreground = (Brush)FindResource("Color.Warning");
        }
        else
        {
            ActionabilityBadgeText.Text = "진행 가능";
            ActionabilityBadgeText.Foreground = (Brush)FindResource("Color.BadgeNeutralText");
        }

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
        SetOnboardingStepState(OnboardingStep1Text, session.IsInitialized);
        SetOnboardingStepState(OnboardingStep2Text, session.ActiveAvatarHandle.HasValue);
        SetOnboardingStepState(OnboardingStep3Text, outputs.SpoutActive || outputs.OscActive);
        var onboardingRecovery = string.IsNullOrWhiteSpace(onboarding.BlockReason)
            ? string.Empty
            : $"{onboarding.BlockReason} {onboarding.RecoveryAction}".Trim();
        ActionBlockReasonText.Text = string.IsNullOrWhiteSpace(onboardingRecovery)
            ? "없음"
            : onboardingRecovery;
        OnboardingRecoveryText.Text = string.IsNullOrWhiteSpace(_beginnerFailureHint)
            ? onboardingRecovery
            : _beginnerFailureHint;
        OnboardingRecoveryPanel.Visibility = string.IsNullOrWhiteSpace(OnboardingRecoveryText.Text)
            ? Visibility.Collapsed
            : Visibility.Visible;
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
        var flowTiming = _controller.GetUiFlowTimingSnapshot();
        FirstBroadcastTimingText.Text = flowTiming.LatestMs >= 0.0
            ? $"최근: {flowTiming.LatestMs:F0} ms ({flowTiming.OutputKind}, {flowTiming.StartedTimestampUtc})"
            : "최근: 측정 대기";
        FlowMedianTimingText.Text = flowTiming.MedianMs >= 0.0
            ? $"중앙값(최근 {flowTiming.SampleCount}회): {flowTiming.MedianMs:F0} ms"
            : "중앙값(최근 20회): 측정 대기";
        RenderOnlyToggleButton.Content = _isRenderOnlyMode
            ? "일반 UI 복귀 (F11)"
            : "렌더 전용 모드 (F11)";
        RenderOnlyToggleButton.IsEnabled = !operation.IsBusy;

        if (RenderGroup.Visibility == Visibility.Visible)
        {
            SyncRenderControlsFromState();
            SyncPoseControlsFromState();
            SyncPosePresetControlsFromState();
            SyncArmSuggestionControlsFromState();
            SyncArmTuningControlsFromState();
            SyncPresetControlsFromState();
        }

        if (TrackingGroup.Visibility == Visibility.Visible)
        {
            SyncTrackingPoseFilterControlsFromState();
        }

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
        var staticBlockKey = BuildRuntimeStaticBlockKey(snapshot);
        if (!string.Equals(staticBlockKey, _lastRuntimeStaticBlockKey, StringComparison.Ordinal))
        {
            _lastRuntimeStaticBlockText = BuildRuntimeStaticBlock(snapshot);
            _lastRuntimeStaticBlockKey = staticBlockKey;
        }

        var runtimeSb = new StringBuilder();
        runtimeSb.AppendLine($"TimestampUtc: {snapshot.TimestampUtc:O}");
        runtimeSb.AppendLine($"SnapshotVersion: {snapshot.SnapshotVersion}");
        runtimeSb.AppendLine($"LogVersion: {snapshot.LogVersion}");
        runtimeSb.AppendLine($"UiFlowTimingVersion: {snapshot.UiFlowTimingVersion}");
        runtimeSb.AppendLine($"FirstBroadcastStartMs: {(snapshot.FirstBroadcastStartMs >= 0.0 ? snapshot.FirstBroadcastStartMs.ToString("F1", CultureInfo.InvariantCulture) : "n/a")}");
        runtimeSb.AppendLine($"FirstBroadcastStartTimestampUtc: {NormalizeDiagField(snapshot.FirstBroadcastStartTimestamp)}");
        runtimeSb.Append(_lastRuntimeStaticBlockText);
        runtimeSb.AppendLine($"LastFrameMs: {runtime.LastFrameMs:F3}");
        var tracking = _controller.TrackingDiagnostics;
        runtimeSb.AppendLine($"Tracking: active={tracking.IsActive}, source={tracking.SourceType}, lock={tracking.SourceLockMode}, active_source={tracking.ActiveSource}, switch_blocked={tracking.SwitchBlockedReason}, source_status={tracking.SourceStatus}, format={tracking.DetectedFormat}, pose_filter={tracking.PoseFilterProfile}, deadband_deg={tracking.PoseDeadbandDeg:F2}, upper_active={tracking.UpperBodyTrackingActive}, upper_source={tracking.UpperBodyActiveSource}, upper_conf={tracking.UpperBodyConfidence:F2}, upper_age_ms={tracking.UpperBodyPacketAgeMs}, upper_status={tracking.UpperBodyStatus}, fps={tracking.InputFps:F1}, capture_fps={tracking.CaptureFps:F1}, infer_ms={tracking.InferenceMsAvg:F1}, latency_avg_ms={tracking.LatencyAvgMs:F1}, latency_p95_ms={tracking.LatencyP95Ms:F1}, stage_ms(capture/parse/smooth/submit)={tracking.CaptureStageMs:F1}/{tracking.ParseStageMs:F1}/{tracking.SmoothStageMs:F1}/{tracking.SubmitStageMs:F1}, arkit52={tracking.Arkit52SubmittedCount}/52, arkit52_strict={tracking.Arkit52StrictCount}, arkit52_fallback={tracking.Arkit52FallbackCount}, arkit52_missing={tracking.Arkit52MissingCount}, arkit52_score={tracking.Arkit52QualityScore:F2}, arkit52_stage_ms={tracking.Arkit52QualityStageMs:F2}, age_ms={tracking.LastPacketAgeMs}, stale={tracking.IsStale}, backend_ready={tracking.ModelSchemaOk}, packets={tracking.ReceivedPackets}, dropped={tracking.DroppedPackets}, parse_err={tracking.ParseErrors}, fallback={tracking.FallbackCount}, switches={tracking.RecentSourceSwitchCount}, switch_reason={tracking.LastSourceSwitchReason}, switch_cd_ms={tracking.SourceSwitchCooldownRemainingMs}, calibration={tracking.CalibrationState}, confidence={tracking.ConfidenceSummary}, ifm_keys_ok={tracking.IfmAcceptedKeySample}, ifm_keys_drop={tracking.IfmDroppedKeySample}, err={tracking.LastErrorCode}");
        runtimeSb.AppendLine(BuildCommonCauseTriageLine(snapshot, tracking));
        runtimeSb.AppendLine(BuildCommonCauseActionLine(snapshot, tracking));
        runtimeSb.AppendLine($"RenderRc: {snapshot.LastRenderRc}");
        runtimeSb.AppendLine($"LastError: {runtime.LastError}");
        return runtimeSb.ToString();
    }

    private string BuildRuntimeStaticBlockKey(DiagnosticsSnapshot snapshot)
    {
        var runtime = snapshot.Runtime;
        return string.Join(
            "|",
            runtime.RenderReadyAvatarCount,
            snapshot.Session.LogicalWidth.ToString("F3", CultureInfo.InvariantCulture),
            snapshot.Session.LogicalHeight.ToString("F3", CultureInfo.InvariantCulture),
            snapshot.Session.DpiScaleX.ToString("F3", CultureInfo.InvariantCulture),
            snapshot.Session.DpiScaleY.ToString("F3", CultureInfo.InvariantCulture),
            snapshot.Session.RenderWidthPx,
            snapshot.Session.RenderHeightPx,
            snapshot.Render.CameraMode,
            snapshot.Render.FramingTarget.ToString("F3", CultureInfo.InvariantCulture),
            snapshot.Render.Headroom.ToString("F3", CultureInfo.InvariantCulture),
            snapshot.Render.YawDeg.ToString("F1", CultureInfo.InvariantCulture),
            snapshot.Render.FovDeg.ToString("F1", CultureInfo.InvariantCulture),
            snapshot.Render.BackgroundPreset,
            snapshot.Render.MirrorMode,
            snapshot.Render.ShowDebugOverlay,
            runtime.SpoutActive,
            runtime.SpoutBackend,
            runtime.SpoutStrictMode,
            runtime.SpoutFallbackCount,
            runtime.SpoutLastErrorCode,
            runtime.NativeCoreModulePath,
            runtime.NativeCoreModuleTimestampUtc,
            runtime.NativeCoreModuleSha256,
            runtime.BuildNativeCoreModulePath,
            runtime.BuildNativeCoreModuleTimestampUtc,
            runtime.BuildNativeCoreModuleSha256,
            runtime.ExpectedNativeCoreModulePath,
            runtime.ExpectedNativeCoreModuleSha256,
            runtime.RuntimePathMatch,
            runtime.RuntimeHashMatchExpected,
            runtime.RuntimeModuleStaleVsBuildOutput,
            runtime.RuntimePathWarningCode,
            runtime.RuntimeTimestampWarningCode,
            runtime.OscActive);
    }

    private string BuildRuntimeStaticBlock(DiagnosticsSnapshot snapshot)
    {
        var runtime = snapshot.Runtime;
        var runtimeSb = new StringBuilder();
        var gates = HostFeatureGateResolver.Evaluate(snapshot.Runtime, snapshot.AvatarInfo, _controller.TrackingDiagnostics);
        runtimeSb.AppendLine($"RenderReadyAvatars: {runtime.RenderReadyAvatarCount}");
        runtimeSb.AppendLine($"AutoQuality: logical={snapshot.Session.LogicalWidth:F1}x{snapshot.Session.LogicalHeight:F1}, dpi={snapshot.Session.DpiScaleX:F2}x{snapshot.Session.DpiScaleY:F2}, render={snapshot.Session.RenderWidthPx}x{snapshot.Session.RenderHeightPx}");
        runtimeSb.AppendLine($"RenderUi: mode={snapshot.Render.CameraMode}, framing={snapshot.Render.FramingTarget:F2}, headroom={snapshot.Render.Headroom:F2}, yaw={snapshot.Render.YawDeg:F0}, fov={snapshot.Render.FovDeg:F0}, bg={snapshot.Render.BackgroundPreset}, mirror={snapshot.Render.MirrorMode}, debug={snapshot.Render.ShowDebugOverlay}");
        runtimeSb.AppendLine($"FeatureGate: class={gates.CommonClass}, reason={NormalizeDiagField(gates.CommonReasonCode)}, arm={gates.ArmPose.Enabled}/{NormalizeDiagField(gates.ArmPose.ReasonCode)}, shadow={gates.RealtimeShadow.Enabled}/{NormalizeDiagField(gates.RealtimeShadow.ReasonCode)}, expression={gates.Expression.Enabled}/{NormalizeDiagField(gates.Expression.ReasonCode)}");
        runtimeSb.AppendLine($"SpoutActive: {runtime.SpoutActive}");
        runtimeSb.AppendLine($"SpoutBackend: {runtime.SpoutBackend}");
        runtimeSb.AppendLine($"SpoutStrictMode: {runtime.SpoutStrictMode}");
        runtimeSb.AppendLine($"SpoutFallbackCount: {runtime.SpoutFallbackCount}");
        runtimeSb.AppendLine($"SpoutLastErrorCode: {runtime.SpoutLastErrorCode}");
        runtimeSb.AppendLine($"NativeCoreModulePath: {NormalizeDiagField(runtime.NativeCoreModulePath)}");
        runtimeSb.AppendLine($"NativeCoreModuleTimestampUtc: {NormalizeDiagField(runtime.NativeCoreModuleTimestampUtc)}");
        runtimeSb.AppendLine($"NativeCoreModuleSha256: {NormalizeDiagField(runtime.NativeCoreModuleSha256)}");
        runtimeSb.AppendLine($"BuildNativeCoreModulePath: {NormalizeDiagField(runtime.BuildNativeCoreModulePath)}");
        runtimeSb.AppendLine($"BuildNativeCoreModuleTimestampUtc: {NormalizeDiagField(runtime.BuildNativeCoreModuleTimestampUtc)}");
        runtimeSb.AppendLine($"BuildNativeCoreModuleSha256: {NormalizeDiagField(runtime.BuildNativeCoreModuleSha256)}");
        runtimeSb.AppendLine($"ExpectedNativeCoreModulePath: {NormalizeDiagField(runtime.ExpectedNativeCoreModulePath)}");
        runtimeSb.AppendLine($"ExpectedNativeCoreModuleSha256: {NormalizeDiagField(runtime.ExpectedNativeCoreModuleSha256)}");
        runtimeSb.AppendLine($"RuntimePathMatch: {runtime.RuntimePathMatch}");
        runtimeSb.AppendLine($"RuntimeHashMatchExpected: {runtime.RuntimeHashMatchExpected}");
        runtimeSb.AppendLine($"RuntimeModuleStaleVsBuildOutput: {runtime.RuntimeModuleStaleVsBuildOutput}");
        runtimeSb.AppendLine($"RuntimePathWarningCode: {NormalizeDiagField(runtime.RuntimePathWarningCode)}");
        runtimeSb.AppendLine($"RuntimeTimestampWarningCode: {NormalizeDiagField(runtime.RuntimeTimestampWarningCode)}");
        runtimeSb.AppendLine($"OscActive: {runtime.OscActive}");
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
            avatarSb.AppendLine($"FamilyBackendFallbackCount: {info.FamilyBackendFallbackCount}");
            avatarSb.AppendLine($"SelectedFamilyBackend: {NormalizeDiagField(info.SelectedFamilyBackend)}");
            avatarSb.AppendLine($"ActivePasses: {NormalizeDiagField(info.ActivePasses)}");
            avatarSb.AppendLine($"MaterialParityMismatchCount: {info.MaterialParityMismatchCount}");
            avatarSb.AppendLine($"TextureResolveAmbiguousCount: {info.TextureResolveAmbiguousCount}");
            avatarSb.AppendLine($"LastWarningCode: {ResolveWarningCode(info)}");
            avatarSb.AppendLine($"LastWarningSeverity: {NormalizeDiagField(info.LastWarningSeverity)}");
            avatarSb.AppendLine($"LastWarningCategory: {NormalizeDiagField(info.LastWarningCategory)}");
            avatarSb.AppendLine($"ExpressionSummary: {info.LastExpressionSummary}");
            avatarSb.AppendLine($"LastWarning: {info.LastWarning}");
            avatarSb.AppendLine($"LastMaterialDiag: {info.LastMaterialDiag}");
            avatarSb.AppendLine($"MaterialParityLastMismatch: {NormalizeDiagField(info.MaterialParityLastMismatch)}");
            avatarSb.AppendLine($"LastRenderPassSummary: {NormalizeDiagField(info.LastRenderPassSummary)}");
            avatarSb.AppendLine($"LastMissingFeature: {info.LastMissingFeature}");
            var gates = HostFeatureGateResolver.Evaluate(snapshot.Runtime, snapshot.AvatarInfo, _controller.TrackingDiagnostics);
            avatarSb.AppendLine($"FeatureGateArm: enabled={gates.ArmPose.Enabled}, code={NormalizeDiagField(gates.ArmPose.ReasonCode)}, reason={NormalizeDiagField(gates.ArmPose.ReasonText)}");
            avatarSb.AppendLine($"FeatureGateShadow: enabled={gates.RealtimeShadow.Enabled}, code={NormalizeDiagField(gates.RealtimeShadow.ReasonCode)}, reason={NormalizeDiagField(gates.RealtimeShadow.ReasonText)}");
            avatarSb.AppendLine($"FeatureGateExpression: enabled={gates.Expression.Enabled}, code={NormalizeDiagField(gates.Expression.ReasonCode)}, reason={NormalizeDiagField(gates.Expression.ReasonText)}");
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

    private static string BuildCommonCauseTriageLine(DiagnosticsSnapshot snapshot, TrackingDiagnostics tracking)
    {
        var gates = HostFeatureGateResolver.Evaluate(snapshot.Runtime, snapshot.AvatarInfo, tracking);
        var format = snapshot.AvatarInfo?.DetectedFormat.ToString() ?? "none";
        var expressions = snapshot.AvatarInfo?.ExpressionCount ?? 0U;
        var warning = snapshot.AvatarInfo?.LastWarningCode ?? string.Empty;
        return
            $"CommonCauseTriage: class={gates.CommonClass}, reason={NormalizeDiagField(gates.CommonReasonCode)}, format={format}, expressions={expressions}, arm={NormalizeDiagField(gates.ArmPose.ReasonCode)}, shadow={NormalizeDiagField(gates.RealtimeShadow.ReasonCode)}, warning={NormalizeDiagField(warning)}";
    }

    private static string BuildCommonCauseActionLine(DiagnosticsSnapshot snapshot, TrackingDiagnostics tracking)
    {
        var gates = HostFeatureGateResolver.Evaluate(snapshot.Runtime, snapshot.AvatarInfo, tracking);
        var actionHint = HostFeatureGateResolver.ResolveOperatorActionHint(gates.CommonClass, gates.CommonReasonCode);
        return $"CommonCauseAction: {NormalizeDiagField(actionHint)}";
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
        var result = MessageBox.Show(this, message, "작업 확인", MessageBoxButton.YesNo, MessageBoxImage.Question);
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
        LightPitchSlider.Value = render.LightPitchDeg;
        LightPitchValueText.Text = render.LightPitchDeg.ToString("F0", CultureInfo.InvariantCulture);
        LightYawSlider.Value = render.LightYawDeg;
        LightYawValueText.Text = render.LightYawDeg.ToString("F0", CultureInfo.InvariantCulture);
        LightIntensitySlider.Value = render.LightIntensity;
        LightIntensityValueText.Text = render.LightIntensity.ToString("F1", CultureInfo.InvariantCulture);
        LightRangeSlider.Value = render.LightRange;
        LightRangeValueText.Text = render.LightRange.ToString("F1", CultureInfo.InvariantCulture);
        SpotAngleSlider.Value = render.SpotAngleDeg;
        SpotAngleValueText.Text = render.SpotAngleDeg.ToString("F1", CultureInfo.InvariantCulture);
        ShadowStrengthSlider.Value = render.ShadowStrength;
        ShadowStrengthValueText.Text = render.ShadowStrength.ToString("F2", CultureInfo.InvariantCulture);
        ShadowBiasSlider.Value = render.ShadowBias;
        ShadowBiasValueText.Text = render.ShadowBias.ToString("F2", CultureInfo.InvariantCulture);
        AmbientIntensitySlider.Value = render.AmbientIntensity;
        AmbientIntensityValueText.Text = render.AmbientIntensity.ToString("F2", CultureInfo.InvariantCulture);
        ShadowEnabledCheckBox.IsChecked = render.ShadowEnabled;
        if (TrackingShowPositionCheckBox is not null)
        {
            TrackingShowPositionCheckBox.IsChecked = render.ShowDebugOverlay;
        }
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
        ConfigurePosePitchSliderRange(selected);
        PosePitchSlider.Value = current?.PitchDeg ?? 0.0f;
        PoseYawSlider.Value = current?.YawDeg ?? 0.0f;
        PoseRollSlider.Value = current?.RollDeg ?? 0.0f;
        PosePitchValueText.Text = PosePitchSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
        PoseYawValueText.Text = PoseYawSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
        PoseRollValueText.Text = PoseRollSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
        var leftArm = _controller.PoseOffsets.FirstOrDefault(p => p.Bone == PoseBoneKind.LeftUpperArm);
        var rightArm = _controller.PoseOffsets.FirstOrDefault(p => p.Bone == PoseBoneKind.RightUpperArm);
        if (ArmLeftPitchSlider is not null && ArmLeftPitchValueText is not null)
        {
            ArmLeftPitchSlider.Value = leftArm?.PitchDeg ?? 0.0f;
            ArmLeftPitchValueText.Text = ArmLeftPitchSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
        }
        if (ArmRightPitchSlider is not null && ArmRightPitchValueText is not null)
        {
            ArmRightPitchSlider.Value = rightArm?.PitchDeg ?? 0.0f;
            ArmRightPitchValueText.Text = ArmRightPitchSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
        }
        if (ArmBothPitchSlider is not null && ArmBothPitchValueText is not null)
        {
            ArmBothPitchSlider.Value = ((leftArm?.PitchDeg ?? 0.0f) + (rightArm?.PitchDeg ?? 0.0f)) * 0.5f;
            ArmBothPitchValueText.Text = ArmBothPitchSlider.Value.ToString("F0", CultureInfo.InvariantCulture);
        }
        _isSyncingPoseUi = false;
    }

    private void ConfigurePosePitchSliderRange(PoseBoneKind selected)
    {
        if (PosePitchSlider is null)
        {
            return;
        }

        PosePitchSlider.Minimum = -180.0;
        PosePitchSlider.Maximum = 180.0;
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
        SyncTrackingBasicControlsFromState();
    }

    private void SyncTrackingBasicControlsFromState()
    {
        if (TrackingStabilitySlider is null ||
            TrackingStabilityValueText is null ||
            TrackingStaleTimeoutSlider is null ||
            TrackingStaleTimeoutValueText is null ||
            TrackingShowPositionCheckBox is null)
        {
            return;
        }

        var tracking = _controller.GetTrackingInputSettings();
        var baseStability = (tracking.PoseDeadbandDeg / 3.0f) * 100.0f;
        var stability = tracking.PoseFilterProfile switch
        {
            PoseFilterProfile.Reactive => Math.Clamp((baseStability * 0.6f) + 10.0f, 0.0f, 40.0f),
            PoseFilterProfile.Balanced => Math.Clamp((baseStability * 0.8f) + 20.0f, 35.0f, 80.0f),
            _ => Math.Clamp((baseStability * 0.9f) + 35.0f, 65.0f, 100.0f),
        };
        _isSyncingTrackingBasicUi = true;
        TrackingStabilitySlider.Value = stability;
        TrackingStabilityValueText.Text = ((int)Math.Round(stability)).ToString(CultureInfo.InvariantCulture);
        TrackingStaleTimeoutSlider.Value = tracking.StaleTimeoutMs;
        TrackingStaleTimeoutValueText.Text = tracking.StaleTimeoutMs.ToString(CultureInfo.InvariantCulture);
        TrackingShowPositionCheckBox.IsChecked = _controller.RenderState.ShowDebugOverlay;
        _isSyncingTrackingBasicUi = false;
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
        if (selected == PoseBoneKind.LeftUpperArm || selected == PoseBoneKind.RightUpperArm)
        {
            SyncPoseControlsFromState();
        }
    }

    private void ApplyArmPitchOffset(PoseBoneKind bone, float pitchDeg)
    {
        var current = _controller.PoseOffsets.FirstOrDefault(p => p.Bone == bone);
        _ = _controller.SetPoseOffset(
            bone,
            pitchDeg,
            current?.YawDeg ?? 0.0f,
            current?.RollDeg ?? 0.0f);
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
            8 => PoseBoneKind.LeftShoulder,
            9 => PoseBoneKind.RightShoulder,
            10 => PoseBoneKind.LeftLowerArm,
            11 => PoseBoneKind.RightLowerArm,
            12 => PoseBoneKind.LeftHand,
            13 => PoseBoneKind.RightHand,
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

    private void SyncArmSuggestionControlsFromState()
    {
        if (SuggestedArmPresetComboBox is null)
        {
            return;
        }

        var selected = SuggestedArmPresetComboBox.SelectedItem as string;
        SuggestedArmPresetComboBox.Items.Clear();
        foreach (var preset in _controller.SuggestedArmPresets)
        {
            SuggestedArmPresetComboBox.Items.Add(preset.Name);
        }

        if (!string.IsNullOrWhiteSpace(selected))
        {
            foreach (var item in SuggestedArmPresetComboBox.Items)
            {
                if (item is string name &&
                    string.Equals(name, selected, StringComparison.OrdinalIgnoreCase))
                {
                    SuggestedArmPresetComboBox.SelectedItem = item;
                    return;
                }
            }
        }

        if (SuggestedArmPresetComboBox.Items.Count > 0)
        {
            SuggestedArmPresetComboBox.SelectedIndex = 0;
        }
    }

    private void SyncArmTuningControlsFromState()
    {
        var tuning = _controller.ArmPoseTuning;
        if (ArmSmoothingCheckBox is not null)
        {
            ArmSmoothingCheckBox.IsChecked = tuning.EnableSmoothing;
        }
        if (ArmDeadbandSlider is not null)
        {
            ArmDeadbandSlider.Value = tuning.DeadbandDeg;
        }
        if (ArmDeadbandValueText is not null)
        {
            ArmDeadbandValueText.Text = $"{tuning.DeadbandDeg:F2}\u00b0";
        }
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
            LightPitchDeg = (float)LightPitchSlider.Value,
            LightYawDeg = (float)LightYawSlider.Value,
            LightIntensity = (float)LightIntensitySlider.Value,
            LightRange = (float)LightRangeSlider.Value,
            SpotAngleDeg = (float)SpotAngleSlider.Value,
            ShadowStrength = (float)ShadowStrengthSlider.Value,
            ShadowBias = (float)ShadowBiasSlider.Value,
            AmbientIntensity = (float)AmbientIntensitySlider.Value,
            ShadowEnabled = ShadowEnabledCheckBox.IsChecked == true,
        };
        return _controller.ApplyRenderUiState(state);
    }

    private void ApplySessionDefaultsToUi()
    {
        var session = _controller.SessionPersistence;
        _uiMode = UiModeBeginner;
        _activeSection = UiSection.GettingStarted;
        _isDarkTheme = string.Equals(session.UiThemeMode, "dark", StringComparison.OrdinalIgnoreCase);
        _diagnosticsPinnedVisible = false;
        _diagnosticsForcedVisible = false;
        ApplyThemeResources();
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
        TrackingStaleTimeoutSlider.Value = session.Tracking.StaleTimeoutMs;
        TrackingStaleTimeoutValueText.Text = session.Tracking.StaleTimeoutMs.ToString(CultureInfo.InvariantCulture);
        TrackingAutoStabilityCheckBox.IsChecked = session.Tracking.AutoStabilityTuningEnabled;
        TrackingUpperBodyEnabledCheckBox.IsChecked = session.Tracking.UpperBodyEnabled;
        _showTrackingIpv4Hint = session.UiShowTrackingIpv4Hint;
        RefreshTrackingIpv4Hint();
        ApplyTrackingIpv4HintVisibility();
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
        SyncTrackingBasicControlsFromState();
        ApplyModeVisibility();
        FocusPrimaryControlForSection(_activeSection);
    }

    private static string BuildTrackingErrorHint(string lastErrorCode)
    {
        return TrackingErrorHintCatalog.BuildHint(lastErrorCode);
    }

    private string BuildTrackingStartFailureMessage(NcResultCode rc)
    {
        var tracking = _controller.TrackingDiagnostics;
        var hint = BuildTrackingErrorHint(tracking.LastErrorCode);
        var hintLine = string.IsNullOrWhiteSpace(hint) ? string.Empty : $"{Environment.NewLine}{hint.Trim()}";
        var codeLine = string.IsNullOrWhiteSpace(tracking.LastErrorCode) ? string.Empty : $"{Environment.NewLine}ErrorCode: {tracking.LastErrorCode}";
        var statusLine = string.IsNullOrWhiteSpace(tracking.StatusMessage) ? string.Empty : $"{Environment.NewLine}Status: {tracking.StatusMessage}";
        var remediation = tracking.LastErrorCode switch
        {
            "TRACKING_MEDIAPIPE_CONFIG_INVALID" => $"{Environment.NewLine}Action: mediapipe_webcam_sidecar.py 경로와 ANIMIQ_MEDIAPIPE_SIDECAR_SCRIPT 설정을 확인하세요.",
            "TRACKING_MEDIAPIPE_START_FAILED" => $"{Environment.NewLine}Action: ANIMIQ_MEDIAPIPE_PYTHON 설정 또는 tools/setup_tracking_python_venv.ps1 실행 후 재시도하세요.",
            "TRACKING_MEDIAPIPE_NO_FRAME" => $"{Environment.NewLine}Action: 카메라 권한/점유 상태를 확인하고 다른 앱의 카메라 사용을 종료하세요.",
            _ => string.Empty,
        };
        return $"트래킹 시작 실패: {rc} (Start tracking failed: {rc}){codeLine}{statusLine}{hintLine}{remediation}";
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
        if (string.Equals(_uiMode, UiModeBeginner, StringComparison.Ordinal))
        {
            _diagnosticsPinnedVisible = false;
            _diagnosticsForcedVisible = false;
        }

        if (persist)
        {
            _controller.SetUiMode(_uiMode);
        }

        ApplyModeVisibility();
        UpdateUiState();
        if (persist)
        {
            PersistUiWorkspaceState();
        }
    }

    private void SetRenderOnlyMode(bool enabled)
    {
        if (_isRenderOnlyMode == enabled)
        {
            return;
        }

        _isRenderOnlyMode = enabled;
        if (_isRenderOnlyMode)
        {
            _renderOnlyHintVisibleUntil = DateTimeOffset.UtcNow + RenderOnlyHintDuration;
        }

        ApplyModeVisibility();
        UpdateUiState();
        RefreshRenderTargetAfterLayoutChange();
    }

    private void RefreshRenderTargetAfterLayoutChange()
    {
        _ = Dispatcher.BeginInvoke(new Action(() =>
        {
            UpdateRenderMetricsFromHost();
            var state = _controller.SessionState;
            if (!state.IsInitialized || !state.IsWindowAttached)
            {
                return;
            }

            var metrics = GetRenderMetrics();
            _ = _controller.ResizeWindow(metrics.pixelWidth, metrics.pixelHeight);
        }), DispatcherPriority.Background);
    }

    private void ActivateSection(UiSection section)
    {
        var advanced = string.Equals(_uiMode, UiModeAdvanced, StringComparison.Ordinal);
        if (!advanced &&
            (section == UiSection.Render || section == UiSection.Tracking || section == UiSection.PlatformOps))
        {
            section = UiSection.GettingStarted;
        }

        if (_activeSection == section)
        {
            return;
        }

        _activeSection = section;
        if (section == UiSection.Tracking && !_controller.OperationState.IsBusy && !_controller.TrackingDiagnostics.IsActive)
        {
            var selectedKey = (TrackingWebcamDeviceComboBox.SelectedItem as WebcamDeviceItem)?.Key;
            RefreshTrackingWebcamDevices(selectedKey);
            RefreshTrackingIpv4Hint();
        }

        ApplySectionVisibility();
        ApplyNavRailState();
        AnimateSectionTransition();
        FocusPrimaryControlForSection(section);
        PersistUiWorkspaceState();
    }

    private void ApplyThemeResources()
    {
        if (Application.Current.Resources["Color.Surface"] is not SolidColorBrush surface ||
            Application.Current.Resources["Color.Text"] is not SolidColorBrush text)
        {
            return;
        }

        static void SetBrush(string key, string colorHex)
        {
            var color = (Color)ColorConverter.ConvertFromString(colorHex)!;
            if (Application.Current.Resources[key] is SolidColorBrush brush)
            {
                if (!brush.IsFrozen)
                {
                    brush.Color = color;
                }
                else
                {
                    Application.Current.Resources[key] = new SolidColorBrush(color);
                }
            }
        }

        var darkPalette = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["Color.Surface"] = "#0F1722",
            ["Color.SurfaceAlt"] = "#132131",
            ["Color.Card"] = "#17273A",
            ["Color.CardStrong"] = "#1B2E44",
            ["Color.Border"] = "#2D445E",
            ["Color.BorderStrong"] = "#3A5675",
            ["Color.Text"] = "#EAF2FC",
            ["Color.TextMuted"] = "#C1D0E2",
            ["Color.TextSubtle"] = "#9EB2C9",
            ["Color.Primary"] = "#4A93D4",
            ["Color.PrimaryHover"] = "#5AA1E0",
            ["Color.PrimaryPressed"] = "#3E83C2",
            ["Color.TabBg"] = "#142235",
            ["Color.TabActive"] = "#1E324A",
            ["Color.NavRailBg"] = "#122031",
            ["Color.NavItemBg"] = "#16283C",
            ["Color.NavItemActiveBg"] = "#20374F",
            ["Color.NavItemText"] = "#B8CAE0",
            ["Color.NavItemActiveText"] = "#F1F7FF",
            ["Color.OnboardingBarBg"] = "#152B41",
            ["Color.OnboardingBarBorder"] = "#35526F",
            ["Color.OnboardingBarTitle"] = "#E8F2FF",
            ["Color.OnboardingBarBody"] = "#BDD3EA",
            ["Color.BadgeNeutralBg"] = "#1D3248",
            ["Color.BadgeNeutralText"] = "#BED3E8",
            ["Color.RenderShellBg"] = "#0A121D",
            ["Color.RenderShellBorder"] = "#3E5B7A",
            ["Color.StatusBarBg"] = "#0E1A28",
            ["Color.StatusBarBorder"] = "#35506C",
            ["Color.StatusBarLabel"] = "#AFC2D8",
            ["Color.StatusBarValue"] = "#F2F8FF",
            ["Color.PanelInfoBg"] = "#1A2C40",
            ["Color.PanelInfoBorder"] = "#32506D",
            ["Color.PanelInfoText"] = "#C2D8EE",
            ["Color.PanelInfoAltBg"] = "#1A2A3C",
            ["Color.PanelInfoAltBorder"] = "#334E6A",
            ["Color.PanelInfoAltText"] = "#D3E3F4",
            ["Color.PanelSuccessBg"] = "#163224",
            ["Color.PanelSuccessBorder"] = "#2F6A4B",
            ["Color.PanelSuccessTitle"] = "#B9EBCF",
            ["Color.PanelSuccessText"] = "#A3DDBE",
            ["Color.PanelWarningBg"] = "#3A2A16",
            ["Color.PanelWarningBorder"] = "#7A5730",
            ["Color.PanelWarningText"] = "#F2D2A6",
            ["Color.PanelErrorBg"] = "#3A1D22",
            ["Color.PanelErrorBorder"] = "#7C3A44",
            ["Color.PanelErrorText"] = "#F5C1C7",
            ["Color.ValidationErrorText"] = "#FF9FA9",
            ["Color.CardSoftBg"] = "#16293D",
            ["Color.CardSoftBorder"] = "#34506D",
            ["Color.CardSoftBorderStrong"] = "#47668A",
            ["Color.Splitter"] = "#3B5776",
            ["Color.OverlayHintBg"] = "#B30A1524",
            ["Color.OverlayHintBorder"] = "#6C89A7",
            ["Color.OverlayHintText"] = "#F0F7FF",
            ["Color.DebugOverlayBg"] = "#E61A2B3D",
            ["Color.DebugOverlayBorder"] = "#5E7D9C",
            ["Color.DebugOverlayText"] = "#EAF3FE",
            ["Color.StepComplete"] = "#70D996",
            ["Color.StepPending"] = "#B7CBE0",
            ["Color.PanelBase"] = "#1C2D42",
            ["Color.PanelElevated"] = "#1A2B40",
            ["Color.PanelInset"] = "#142438",
            ["Color.FocusRing"] = "#7DB8EC",
            ["Color.DisabledBg"] = "#243447",
            ["Color.DisabledText"] = "#7E94AE",
            ["Color.LogAreaBg"] = "#132335",
            ["Color.LogAreaBorder"] = "#36516D",
            ["Color.GroupBoxHeaderBg"] = "#1B2E44",
        };

        var lightPalette = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["Color.Surface"] = "#F2F6FC",
            ["Color.SurfaceAlt"] = "#EAF1FA",
            ["Color.Card"] = "#F8FBFF",
            ["Color.CardStrong"] = "#FEFFFF",
            ["Color.Border"] = "#C7D7EB",
            ["Color.BorderStrong"] = "#AFC4DE",
            ["Color.Text"] = "#1A2736",
            ["Color.TextMuted"] = "#53687D",
            ["Color.TextSubtle"] = "#6D8297",
            ["Color.Primary"] = "#0B6FC2",
            ["Color.PrimaryHover"] = "#0A62AB",
            ["Color.PrimaryPressed"] = "#084F89",
            ["Color.TabBg"] = "#F0F4FA",
            ["Color.TabActive"] = "#FFFFFF",
            ["Color.NavRailBg"] = "#E8F0FA",
            ["Color.NavItemBg"] = "#F4F9FF",
            ["Color.NavItemActiveBg"] = "#FFFFFF",
            ["Color.NavItemText"] = "#3A4E63",
            ["Color.NavItemActiveText"] = "#1C2530",
            ["Color.OnboardingBarBg"] = "#E4F1FF",
            ["Color.OnboardingBarBorder"] = "#B2D1F0",
            ["Color.OnboardingBarTitle"] = "#0F365B",
            ["Color.OnboardingBarBody"] = "#244B71",
            ["Color.BadgeNeutralBg"] = "#EFF3F9",
            ["Color.BadgeNeutralText"] = "#43586E",
            ["Color.RenderShellBg"] = "#182639",
            ["Color.RenderShellBorder"] = "#5A708B",
            ["Color.StatusBarBg"] = "#24384E",
            ["Color.StatusBarBorder"] = "#546E88",
            ["Color.StatusBarLabel"] = "#D7E5F3",
            ["Color.StatusBarValue"] = "#F4FAFF",
            ["Color.PanelInfoBg"] = "#EEF5FE",
            ["Color.PanelInfoBorder"] = "#D0E0F2",
            ["Color.PanelInfoText"] = "#2F4F6F",
            ["Color.PanelInfoAltBg"] = "#E8F0FA",
            ["Color.PanelInfoAltBorder"] = "#C4D5EA",
            ["Color.PanelInfoAltText"] = "#223A55",
            ["Color.PanelSuccessBg"] = "#EEF6EF",
            ["Color.PanelSuccessBorder"] = "#BFDCC2",
            ["Color.PanelSuccessTitle"] = "#1E5130",
            ["Color.PanelSuccessText"] = "#2C6A3F",
            ["Color.PanelWarningBg"] = "#FFF8EE",
            ["Color.PanelWarningBorder"] = "#F3D2A7",
            ["Color.PanelWarningText"] = "#7C4012",
            ["Color.PanelErrorBg"] = "#FFF6F6",
            ["Color.PanelErrorBorder"] = "#F0B7B7",
            ["Color.PanelErrorText"] = "#7F1D1D",
            ["Color.ValidationErrorText"] = "#B03030",
            ["Color.CardSoftBg"] = "#F5F9FE",
            ["Color.CardSoftBorder"] = "#D7E0EA",
            ["Color.CardSoftBorderStrong"] = "#CCD8E6",
            ["Color.Splitter"] = "#B9C8DB",
            ["Color.OverlayHintBg"] = "#D0101720",
            ["Color.OverlayHintBorder"] = "#5A7088",
            ["Color.OverlayHintText"] = "#E8F0F8",
            ["Color.DebugOverlayBg"] = "#EEFFFFFF",
            ["Color.DebugOverlayBorder"] = "#47627E",
            ["Color.DebugOverlayText"] = "#1A2530",
            ["Color.StepComplete"] = "#227842",
            ["Color.StepPending"] = "#35516B",
            ["Color.PanelBase"] = "#F3F7FD",
            ["Color.PanelElevated"] = "#FFFFFF",
            ["Color.PanelInset"] = "#EDF3FA",
            ["Color.FocusRing"] = "#4698E5",
            ["Color.DisabledBg"] = "#E5ECF5",
            ["Color.DisabledText"] = "#8A9AAF",
            ["Color.LogAreaBg"] = "#F4F7FC",
            ["Color.LogAreaBorder"] = "#C7D4E5",
            ["Color.GroupBoxHeaderBg"] = "#F5F0FF",
        };

        var palette = _isDarkTheme ? darkPalette : lightPalette;
        foreach (var (key, value) in palette)
        {
            SetBrush(key, value);
        }

        ThemeToggleButton.Content = _isDarkTheme ? "라이트 테마" : "다크 테마";
    }

    private void ApplySectionVisibility()
    {
        var advanced = string.Equals(_uiMode, UiModeAdvanced, StringComparison.Ordinal);
        var canUseAdvancedSections = advanced;
        var canUseConsumerSections = advanced || IsBeginnerMode();

        var showGettingStarted = _activeSection == UiSection.GettingStarted;
        var showSessionAvatar = canUseConsumerSections && _activeSection == UiSection.SessionAvatar;
        var showRender = canUseAdvancedSections && _activeSection == UiSection.Render;
        var showOutputs = canUseConsumerSections && _activeSection == UiSection.Outputs;
        var showTracking = canUseAdvancedSections && _activeSection == UiSection.Tracking;
        var showOps = canUseAdvancedSections && _activeSection == UiSection.PlatformOps;

        ModeGroup.Visibility = Visibility.Visible;
        QuickActionsGroup.Visibility = showGettingStarted ? Visibility.Visible : Visibility.Collapsed;
        SessionGroup.Visibility = showSessionAvatar ? Visibility.Visible : Visibility.Collapsed;
        AvatarGroup.Visibility = showSessionAvatar ? Visibility.Visible : Visibility.Collapsed;
        RenderGroup.Visibility = showRender ? Visibility.Visible : Visibility.Collapsed;
        OutputsGroup.Visibility = showOutputs ? Visibility.Visible : Visibility.Collapsed;
        TrackingGroup.Visibility = showTracking ? Visibility.Visible : Visibility.Collapsed;
        PlatformOpsGroup.Visibility = showOps ? Visibility.Visible : Visibility.Collapsed;
        RenderAdvancedExpander.Visibility = showRender && canUseAdvancedSections ? Visibility.Visible : Visibility.Collapsed;
        if (RenderAdvancedExpander.Visibility != Visibility.Visible)
        {
            RenderAdvancedExpander.IsExpanded = false;
        }
    }

    private void ApplyNavRailState()
    {
        var advanced = string.Equals(_uiMode, UiModeAdvanced, StringComparison.Ordinal);
        var navMap = new[]
        {
            (NavGettingStartedButton, UiSection.GettingStarted, true),
            (NavSessionAvatarButton, UiSection.SessionAvatar, true),
            (NavRenderButton, UiSection.Render, advanced),
            (NavOutputsButton, UiSection.Outputs, true),
            (NavTrackingButton, UiSection.Tracking, advanced),
            (NavOpsButton, UiSection.PlatformOps, advanced),
        };

        var activeBg = (Brush)FindResource("Color.NavItemActiveBg");
        var activeFg = (Brush)FindResource("Color.NavItemActiveText");
        var inactiveBg = (Brush)FindResource("Color.NavItemBg");
        var inactiveFg = (Brush)FindResource("Color.NavItemText");
        var border = (Brush)FindResource("Color.Border");

        foreach (var (button, section, enabled) in navMap)
        {
            button.IsEnabled = enabled;
            button.Visibility = !advanced && !enabled ? Visibility.Collapsed : Visibility.Visible;
            var active = section == _activeSection;
            button.Background = active ? activeBg : inactiveBg;
            button.Foreground = active ? activeFg : inactiveFg;
            button.BorderBrush = border;
            button.FontWeight = active ? FontWeights.SemiBold : FontWeights.Normal;
            button.Opacity = enabled ? 1.0 : 0.55;
        }

        ToggleDiagnosticsButton.Visibility = advanced ? Visibility.Visible : Visibility.Collapsed;
        ToggleDiagnosticsButton.Content = _diagnosticsPinnedVisible ? "진단: 열림" : "진단: 닫힘";
    }

    private IEnumerable<Button> GetNavButtons()
    {
        yield return NavGettingStartedButton;
        yield return NavSessionAvatarButton;
        yield return NavRenderButton;
        yield return NavOutputsButton;
        yield return NavTrackingButton;
        yield return NavOpsButton;
        yield return ToggleDiagnosticsButton;
    }

    private void FocusPrimaryControlForSection(UiSection section)
    {
        Control? target = section switch
        {
            UiSection.GettingStarted => PrimaryActionButton,
            UiSection.SessionAvatar => AvatarPathTextBox,
            UiSection.Render => FramingSlider,
            UiSection.Outputs => SpoutChannelTextBox,
            UiSection.Tracking => TrackingPortTextBox,
            UiSection.PlatformOps => RunPreflightButton,
            _ => null,
        };

        if (target is null || target.Visibility != Visibility.Visible || !target.IsEnabled)
        {
            return;
        }

        _ = Dispatcher.BeginInvoke(new Action(() => target.Focus()), DispatcherPriority.Input);
    }

    private void PersistUiWorkspaceState()
    {
        _controller.SetUiWorkspaceState(
            ToPersistSectionKey(_activeSection),
            _isDarkTheme ? "dark" : "light",
            _diagnosticsPinnedVisible);
    }

    private void AnimateSectionTransition()
    {
        if (_controller.OperationState.IsBusy)
        {
            return;
        }

        var animation = new DoubleAnimation
        {
            From = 0.94,
            To = 1.0,
            Duration = TimeSpan.FromMilliseconds(90),
            EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseOut },
        };
        ControlPanelScrollViewer.BeginAnimation(OpacityProperty, animation, HandoffBehavior.SnapshotAndReplace);
    }

    private void ApplyModeVisibility()
    {
        if (_isRenderOnlyMode)
        {
            ControlsColumn.Width = new GridLength(0.0);
            SplitterColumn.Width = new GridLength(0.0);
            ControlPanelFrame.Visibility = Visibility.Collapsed;
            ControlPanelScrollViewer.Visibility = Visibility.Collapsed;
            LeftRailPanel.Visibility = Visibility.Collapsed;
            MainGridSplitter.Visibility = Visibility.Collapsed;
            DiagnosticsRow.Height = new GridLength(0.0);
            DiagnosticsTabControl.Visibility = Visibility.Collapsed;
            StatusBarBorder.Visibility = Visibility.Collapsed;
            RenderOnlyHintOverlay.Visibility = DateTimeOffset.UtcNow <= _renderOnlyHintVisibleUntil
                ? Visibility.Visible
                : Visibility.Collapsed;
            return;
        }

        ControlsColumn.Width = new GridLength(640.0);
        SplitterColumn.Width = new GridLength(14.0);
        ControlPanelFrame.Visibility = Visibility.Visible;
        ControlPanelScrollViewer.Visibility = Visibility.Visible;
        LeftRailPanel.Visibility = Visibility.Visible;
        MainGridSplitter.Visibility = Visibility.Visible;
        StatusBarBorder.Visibility = Visibility.Visible;
        RenderOnlyHintOverlay.Visibility = Visibility.Collapsed;

        var advanced = string.Equals(_uiMode, UiModeAdvanced, StringComparison.Ordinal);
        var beginner = !advanced;
        ViewModeToggleButton.Content = advanced ? "초급 보기로 전환" : "고급 보기로 전환";
        ViewModeToggleButton.FontWeight = advanced ? FontWeights.SemiBold : FontWeights.Normal;

        if (!advanced &&
            _activeSection != UiSection.GettingStarted &&
            _activeSection != UiSection.SessionAvatar &&
            _activeSection != UiSection.Outputs)
        {
            _activeSection = UiSection.GettingStarted;
            FocusPrimaryControlForSection(_activeSection);
        }

        ApplySectionVisibility();
        ApplyNavRailState();
        ApplyStatusBarDensity(advanced);

        var showDiagnostics = advanced && (_diagnosticsForcedVisible || _diagnosticsPinnedVisible);
        DiagnosticsRow.Height = showDiagnostics ? new GridLength(260.0) : new GridLength(0.0);
        DiagnosticsTabControl.Visibility = showDiagnostics ? Visibility.Visible : Visibility.Collapsed;
        BeginnerFailureHintPanel.Visibility = beginner && !string.IsNullOrWhiteSpace(_beginnerFailureHint)
            ? Visibility.Visible
            : Visibility.Collapsed;
        OpenDiagnosticsFromHintButton.Visibility = beginner ? Visibility.Visible : Visibility.Collapsed;
    }

    private void ApplyStatusBarDensity(bool advanced)
    {
        var extraVisibility = advanced ? Visibility.Visible : Visibility.Collapsed;
        RenderStatusLabel.Visibility = extraVisibility;
        RenderStatusText.Visibility = extraVisibility;
        FrameStatusLabel.Visibility = extraVisibility;
        FrameStatusText.Visibility = extraVisibility;
        BusyStatusLabel.Visibility = extraVisibility;
        BusyStatusText.Visibility = extraVisibility;
        ErrorStatusLabel.Visibility = extraVisibility;
        ErrorStatusText.Visibility = extraVisibility;
        TrackStatusLabel.Visibility = extraVisibility;
        TrackStatusText.Visibility = extraVisibility;
    }

    private void SetOnboardingStepState(TextBlock target, bool completed)
    {
        target.Text = completed ? "완료" : "다음 단계";
        var resourceKey = completed ? "Color.StepComplete" : "Color.StepPending";
        target.Foreground = (Brush)FindResource(resourceKey);
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
        if (IsBeginnerMode())
        {
            SetUiMode(UiModeAdvanced, persist: true);
        }

        var onboarding = HostUiPolicy.BuildOnboardingState(_controller.SessionState, _controller.Outputs, _controller.OperationState, _validationState);
        _controller.TrackOnboardingUiEvent(
            "recovery_action_clicked",
            onboarding.Step,
            onboarding.PrimaryAction,
            onboarding.Actionability,
            string.IsNullOrWhiteSpace(_beginnerFailureHint) ? onboarding.BlockReasonShort : _beginnerFailureHint);
        RevealDiagnosticsForFailure(string.IsNullOrWhiteSpace(_lastFailureSource) ? "LoadAvatar" : _lastFailureSource);
    }

    private async void RunQuickRecovery_Click(object sender, RoutedEventArgs e)
    {
        var errorCode = _controller.TrackingDiagnostics.LastErrorCode;
        if (string.IsNullOrWhiteSpace(errorCode))
        {
            MessageBox.Show(this, "현재 자동 복구 가능한 트래킹 오류 코드가 없습니다.", "복구 안내", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }

        var scriptPath = TryResolveToolScriptPath("tracking_error_recovery.ps1");
        if (string.IsNullOrWhiteSpace(scriptPath))
        {
            MessageBox.Show(this, "tracking_error_recovery.ps1 경로를 찾지 못했습니다. tools 폴더를 확인하세요.", "복구 실패", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        RunQuickRecoveryButton.IsEnabled = false;
        try
        {
            var startInfo = new ProcessStartInfo("powershell")
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                WorkingDirectory = Path.GetDirectoryName(scriptPath) ?? Environment.CurrentDirectory,
            };
            startInfo.ArgumentList.Add("-ExecutionPolicy");
            startInfo.ArgumentList.Add("Bypass");
            startInfo.ArgumentList.Add("-File");
            startInfo.ArgumentList.Add(scriptPath);
            startInfo.ArgumentList.Add("-ErrorCode");
            startInfo.ArgumentList.Add(errorCode);
            startInfo.ArgumentList.Add("-Execute");

            using var process = Process.Start(startInfo);
            if (process is null)
            {
                MessageBox.Show(this, "복구 프로세스를 시작하지 못했습니다.", "복구 실패", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            await Task.Run(() => process.WaitForExit(180000));
            if (!process.HasExited)
            {
                try { process.Kill(true); } catch { }
                MessageBox.Show(this, "복구 작업이 시간 제한(180초)을 초과했습니다.", "복구 실패", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            if (process.ExitCode == 0)
            {
                MessageBox.Show(this, $"빠른 복구 실행 완료 (ErrorCode={errorCode}).", "복구 완료", MessageBoxButton.OK, MessageBoxImage.Information);
            }
            else
            {
                MessageBox.Show(this, $"빠른 복구 실행 실패 (exit={process.ExitCode}, ErrorCode={errorCode}).", "복구 실패", MessageBoxButton.OK, MessageBoxImage.Warning);
            }
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, $"복구 실행 중 예외가 발생했습니다: {ex.Message}", "복구 실패", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
        finally
        {
            UpdateUiState();
        }
    }

    private void DismissFailureHint_Click(object sender, RoutedEventArgs e)
    {
        ClearFailureHint();
        UpdateUiState();
    }

    private static string? TryResolveToolScriptPath(string scriptName)
    {
        var seeds = new List<string>
        {
            AppContext.BaseDirectory,
            Environment.CurrentDirectory,
            Directory.GetCurrentDirectory(),
        };

        foreach (var seed in seeds.Where(static p => !string.IsNullOrWhiteSpace(p)).Distinct(StringComparer.OrdinalIgnoreCase))
        {
            var current = seed;
            for (var i = 0; i < 8 && !string.IsNullOrWhiteSpace(current); i++)
            {
                var candidate = Path.Combine(current, "tools", scriptName);
                if (File.Exists(candidate))
                {
                    return candidate;
                }

                var parent = Directory.GetParent(current);
                if (parent is null)
                {
                    break;
                }

                current = parent.FullName;
            }
        }

        return null;
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
        _lastFailureSource = string.Empty;
        BeginnerFailureHintText.Text = string.Empty;
        ApplyModeVisibility();
    }

    private void ReportUserFailure(string source, string beginnerMessage, string advancedMessage)
    {
        ShowFailureHint(source, beginnerMessage);
        var message = IsBeginnerMode() ? beginnerMessage : advancedMessage;
        MessageBox.Show(this, message, "조치 필요", MessageBoxButton.OK, MessageBoxImage.Warning);
    }
}
