using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace HostCore;

public sealed partial class HostController
{
    private const int MaxLogEntries = 200;

    private readonly IAvatarSessionService _sessionService;
    private readonly IRenderLoopService _renderLoopService;
    private readonly IOutputService _outputService;
    private readonly IRenderPresetStore _presetStore;
    private readonly ITrackingInputService _trackingInputService;
    private readonly Queue<HostLogEntry> _logs = new();
    private RenderPresetStoreModel _presetStoreModel;
    private NcRenderQualityOptions _renderOptions;
    private long _snapshotVersion;
    private long _logVersion;
    private bool _windowAttached;
    private IntPtr _windowHandle = IntPtr.Zero;
    private bool _desiredSpoutActive;
    private bool _desiredOscActive;
    private DateTimeOffset _lastOutputStateSyncLogUtc = DateTimeOffset.MinValue;
    private DateTimeOffset _lastOutputRecoveryAttemptUtc = DateTimeOffset.MinValue;
    private string _lastLoadFailureGuidance = string.Empty;
    private string _lastLoadFailureTechnical = string.Empty;
    private TrackingDiagnostics _trackingDiagnostics = new(false, "unknown", 0.0, int.MaxValue, true, 0, 0, 0, "stopped", TrackingSourceType.OscIfacial, "idle");
    private string _lastTrackingFormat = "unknown";
    private ulong _lastTrackingParseErrors;
    private bool _lastTrackingActive;

    public HostController()
        : this(new AvatarSessionService(), new RenderLoopService(), new OutputService(), new RenderPresetStore(), new TrackingInputService())
    {
    }

    public HostController(
        IAvatarSessionService sessionService,
        IRenderLoopService renderLoopService,
        IOutputService outputService,
        IRenderPresetStore presetStore,
        ITrackingInputService? trackingInputService = null)
    {
        _sessionService = sessionService;
        _renderLoopService = renderLoopService;
        _outputService = outputService;
        _presetStore = presetStore;
        _trackingInputService = trackingInputService ?? new TrackingInputService();
        _renderOptions = NativeCoreInterop.BuildBroadcastPreset();
        _presetStoreModel = EnsurePresetStoreModel(_presetStore.Load());
        SessionState = new HostSessionState(false, false, null, NcResultCode.Ok);
        Outputs = new OutputState(false, false, "VsfClone", 0U, 0U, 60U, 39539, "127.0.0.1:39540");
        _desiredSpoutActive = false;
        _desiredOscActive = false;
        RenderState = BuildRenderUiState(_renderOptions, true, BackgroundPreset.DarkBlue, false);
        if (TryGetSelectedPreset(out var selectedPreset))
        {
            RenderState = ToRenderUiState(selectedPreset);
            _renderOptions = ToNativeOptions(RenderState);
        }
        OperationState = new HostOperationState(false, string.Empty);
        LastSnapshot = BuildSnapshot();
        InitializeMvpFeatures();
    }

    public HostSessionState SessionState { get; private set; }
    public OutputState Outputs { get; private set; }
    public RenderUiState RenderState { get; private set; }
    public HostOperationState OperationState { get; private set; }
    public DiagnosticsSnapshot LastSnapshot { get; private set; }

    public event EventHandler? StateChanged;
    public event EventHandler? DiagnosticsUpdated;
    public event EventHandler<HostLogEntry>? ErrorRaised;

    public IReadOnlyCollection<HostLogEntry> LogEntries => _logs.ToArray();
    public IReadOnlyList<RenderPresetModel> RenderPresets => _presetStoreModel.Presets;
    public string? SelectedRenderPresetName => _presetStoreModel.LastSelectedPresetName;
    public TrackingDiagnostics TrackingDiagnostics => _trackingDiagnostics;

    public NcResultCode Initialize()
    {
        return ExecuteOperation("Initialize", () =>
        {
            var rc = _sessionService.Initialize();
            TrackResult("Initialize", rc);
            if (rc == NcResultCode.Ok)
            {
                ApplyRenderOptionsInternal("ApplyRenderOptionsInit");
            }

            RefreshState();
            return rc;
        });
    }

    public NcResultCode Shutdown()
    {
        return ExecuteOperation("Shutdown", () =>
        {
            if (_windowAttached)
            {
                TrackResult("DetachWindow", _renderLoopService.DetachWindow());
                _windowAttached = false;
                _windowHandle = IntPtr.Zero;
            }

            if (Outputs.SpoutActive)
            {
                TrackResult("StopSpout", _outputService.StopSpout());
                Outputs = Outputs with { SpoutActive = false };
            }
            _desiredSpoutActive = false;

            if (Outputs.OscActive)
            {
                TrackResult("StopOsc", _outputService.StopOsc());
                Outputs = Outputs with { OscActive = false };
            }
            _desiredOscActive = false;

            if (_sessionService.ActiveAvatarHandle.HasValue)
            {
                TrackResult("UnloadAvatar", _sessionService.UnloadAvatar());
            }

            TrackResult("StopTracking", _trackingInputService.Stop());
            _trackingDiagnostics = _trackingInputService.GetDiagnostics();

            var rc = _sessionService.Shutdown();
            TrackResult("Shutdown", rc);
            _renderOptions = NativeCoreInterop.BuildBroadcastPreset();
            RenderState = BuildRenderUiState(_renderOptions, true, BackgroundPreset.DarkBlue, false);
            SessionState = new HostSessionState(false, false, null, NcResultCode.Ok, 0.0, 0.0, 1.0, 1.0, 0U, 0U);
            PublishDiagnostics();
            StateChanged?.Invoke(this, EventArgs.Empty);
            return rc;
        });
    }

    public NcResultCode AttachWindow(IntPtr hwnd, uint width, uint height)
    {
        return ExecuteOperation("AttachWindow", () =>
        {
            _windowHandle = hwnd;
            var rc = _renderLoopService.AttachWindow(hwnd, width, height);
            _windowAttached = rc == NcResultCode.Ok;
            TrackResult("AttachWindow", rc);
            if (rc == NcResultCode.Ok)
            {
                ApplyRenderOptionsInternal("ApplyRenderOptionsAttach");
            }

            RefreshState();
            return rc;
        });
    }

    public NcResultCode ResizeWindow(uint width, uint height)
    {
        return ExecuteOperation("ResizeWindow", () =>
        {
            if (!_windowAttached)
            {
                return NcResultCode.InvalidArgument;
            }

            var rc = _renderLoopService.Resize(width, height);
            TrackResult("ResizeWindow", rc);
            if (rc == NcResultCode.Ok)
            {
                ApplyRenderOptionsInternal("ApplyRenderOptionsResize");
            }
            if (rc == NcResultCode.Ok && Outputs.SpoutActive)
            {
                AutoReconfigureSpout(width, height);
            }

            RefreshState();
            return rc;
        });
    }

    public NcResultCode LoadAvatar(string path)
    {
        return ExecuteOperation("LoadAvatar", () =>
        {
            var normalizedPath = path?.Trim() ?? string.Empty;
            _sessionPersistence = _sessionPersistence with { AvatarPath = normalizedPath, LastUpdatedUtc = DateTimeOffset.UtcNow };
            PersistSessionSnapshot();
            if (_sessionService.ActiveAvatarHandle.HasValue)
            {
                TrackResult("UnloadAvatar", _sessionService.UnloadAvatar());
            }

            var rc = _sessionService.LoadAvatar(normalizedPath);
            TrackResult("LoadAvatar", rc);
            if (rc == NcResultCode.Ok)
            {
                // Re-apply host-side render controls after avatar load in case
                // the native side resets camera/quality state during load.
                ApplyRenderOptionsInternal("ApplyRenderOptionsLoadAvatar");
                _lastLoadFailureGuidance = string.Empty;
                _lastLoadFailureTechnical = string.Empty;
            }

            RefreshState();
            return rc;
        });
    }

    public NcResultCode UnloadAvatar()
    {
        return ExecuteOperation("UnloadAvatar", () =>
        {
            var rc = _sessionService.UnloadAvatar();
            TrackResult("UnloadAvatar", rc);
            RefreshState();
            return rc;
        });
    }

    public NcResultCode StartSpout(uint width, uint height, uint fps, string channelName)
    {
        return ExecuteOperation("StartSpout", () =>
        {
            var rc = _outputService.StartSpout(width, height, fps, channelName);
            TrackResult("StartSpout", rc);
            _desiredSpoutActive = rc == NcResultCode.Ok;
            Outputs = Outputs with
            {
                SpoutActive = rc == NcResultCode.Ok,
                SpoutChannelName = channelName,
                SpoutWidthPx = width,
                SpoutHeightPx = height,
                SpoutFps = fps,
            };
            RefreshState();
            return rc;
        });
    }

    public NcResultCode StopSpout()
    {
        return ExecuteOperation("StopSpout", () =>
        {
            var rc = _outputService.StopSpout();
            TrackResult("StopSpout", rc);
            _desiredSpoutActive = false;
            Outputs = Outputs with { SpoutActive = false };
            RefreshState();
            return rc;
        });
    }

    public NcResultCode StartOsc(ushort bindPort, string publishAddress)
    {
        return ExecuteOperation("StartOsc", () =>
        {
            var rc = _outputService.StartOsc(bindPort, publishAddress);
            TrackResult("StartOsc", rc);
            _desiredOscActive = rc == NcResultCode.Ok;
            Outputs = Outputs with
            {
                OscActive = rc == NcResultCode.Ok,
                OscBindPort = bindPort,
                OscPublishAddress = publishAddress,
            };
            RefreshState();
            return rc;
        });
    }

    public NcResultCode StopOsc()
    {
        return ExecuteOperation("StopOsc", () =>
        {
            var rc = _outputService.StopOsc();
            TrackResult("StopOsc", rc);
            _desiredOscActive = false;
            Outputs = Outputs with { OscActive = false };
            RefreshState();
            return rc;
        });
    }

    public NcResultCode StartTracking(
        ushort listenPort,
        int staleTimeoutMs)
    {
        return ExecuteOperation("StartTracking", () =>
        {
            var trackingSettings = _sessionPersistence.Tracking;
            var options = new TrackingStartOptions(
                listenPort == 0 ? (ushort)49983 : listenPort,
                staleTimeoutMs,
                trackingSettings.SourceType,
                trackingSettings.WebcamDeviceId,
                trackingSettings.OnnxModelPath,
                trackingSettings.InferenceFpsCap);
            var rc = _trackingInputService.Start(options);
            _trackingDiagnostics = _trackingInputService.GetDiagnostics();
            if (rc == NcResultCode.Ok)
            {
                SetTrackingState(listenPort, staleTimeoutMs, true);
            }
            TrackResult("StartTracking", rc);
            RefreshState();
            return rc;
        });
    }

    public NcResultCode StopTracking()
    {
        return ExecuteOperation("StopTracking", () =>
        {
            var rc = _trackingInputService.Stop();
            _trackingDiagnostics = _trackingInputService.GetDiagnostics();
            if (rc == NcResultCode.Ok)
            {
                SetTrackingState(_sessionPersistence.Tracking.ListenPort, _sessionPersistence.Tracking.StaleTimeoutMs, false);
            }
            TrackResult("StopTracking", rc);
            RefreshState();
            return rc;
        });
    }

    public NcResultCode RecenterTracking()
    {
        return ExecuteOperation("RecenterTracking", () =>
        {
            var rc = _trackingInputService.Recenter();
            _trackingDiagnostics = _trackingInputService.GetDiagnostics();
            TrackResult("RecenterTracking", rc);
            RefreshState();
            return rc;
        });
    }

    public NcResultCode SetBroadcastMode(bool enabled)
    {
        return ExecuteOperation("SetBroadcastMode", () =>
        {
            var current = RenderState;
            _renderOptions = enabled ? NativeCoreInterop.BuildBroadcastPreset() : NativeCoreInterop.BuildDebugPreset();
            _renderOptions.CameraMode = ToNativeCameraMode(current.CameraMode);
            _renderOptions.FramingTarget = Clamp(current.FramingTarget, 0.35f, 0.95f);
            _renderOptions.Headroom = Clamp(current.Headroom, 0.0f, 0.5f);
            _renderOptions.YawDeg = Clamp(current.YawDeg, -45.0f, 45.0f);
            _renderOptions.FovDeg = Clamp(current.FovDeg, 20.0f, 70.0f);
            _renderOptions.ShowDebugOverlay = current.ShowDebugOverlay ? 1U : 0U;
            ApplyBackgroundPreset(ref _renderOptions, current.BackgroundPreset);
            RenderState = current with { BroadcastMode = enabled };
            var rc = ApplyRenderOptionsInternal("SetBroadcastMode");
            RefreshState();
            return rc;
        });
    }

    public NcResultCode ApplyRenderUiState(RenderUiState state)
    {
        return ExecuteOperation("ApplyRenderUiState", () =>
        {
            var normalized = NormalizeRenderState(state);
            _renderOptions = ToNativeOptions(normalized);
            RenderState = normalized;
            var rc = ApplyRenderOptionsInternal("ApplyRenderUiState");
            RefreshState();
            return rc;
        });
    }

    public RenderPresetModel CreatePreset(string name)
    {
        var normalizedName = NormalizePresetName(name);
        return new RenderPresetModel(
            normalizedName,
            RenderState.BroadcastMode,
            RenderState.CameraMode,
            RenderState.FramingTarget,
            RenderState.Headroom,
            RenderState.YawDeg,
            RenderState.FovDeg,
            RenderState.BackgroundPreset,
            RenderState.ShowDebugOverlay,
            RenderState.MirrorMode);
    }

    public bool SaveOrUpdateRenderPreset(string name)
    {
        var normalizedName = NormalizePresetName(name);
        if (string.IsNullOrEmpty(normalizedName))
        {
            return false;
        }

        var preset = CreatePreset(normalizedName);
        var index = _presetStoreModel.Presets.FindIndex(
            p => string.Equals(p.Name, normalizedName, StringComparison.OrdinalIgnoreCase));
        if (index >= 0)
        {
            _presetStoreModel.Presets[index] = preset;
        }
        else
        {
            _presetStoreModel.Presets.Add(preset);
        }

        _presetStoreModel.LastSelectedPresetName = preset.Name;
        PersistPresetStore();
        RefreshState();
        return true;
    }

    public NcResultCode ApplyRenderPreset(string name)
    {
        var normalizedName = NormalizePresetName(name);
        var preset = _presetStoreModel.Presets.FirstOrDefault(
            p => string.Equals(p.Name, normalizedName, StringComparison.OrdinalIgnoreCase));
        if (preset is null)
        {
            return NcResultCode.InvalidArgument;
        }

        _presetStoreModel.LastSelectedPresetName = preset.Name;
        PersistPresetStore();
        return ApplyRenderUiState(ToRenderUiState(preset));
    }

    public bool DeleteRenderPreset(string name)
    {
        var normalizedName = NormalizePresetName(name);
        var index = _presetStoreModel.Presets.FindIndex(
            p => string.Equals(p.Name, normalizedName, StringComparison.OrdinalIgnoreCase));
        if (index < 0 || _presetStoreModel.Presets.Count <= 1)
        {
            return false;
        }

        var removedName = _presetStoreModel.Presets[index].Name;
        _presetStoreModel.Presets.RemoveAt(index);
        if (string.Equals(_presetStoreModel.LastSelectedPresetName, removedName, StringComparison.OrdinalIgnoreCase))
        {
            _presetStoreModel.LastSelectedPresetName = _presetStoreModel.Presets[0].Name;
        }

        PersistPresetStore();
        RefreshState();
        return true;
    }

    public NcResultCode ResetRenderDefaults()
    {
        var defaultPreset = _presetStoreModel.Presets.FirstOrDefault(
            p => string.Equals(p.Name, "Broadcast Default", StringComparison.OrdinalIgnoreCase))
            ?? _presetStoreModel.Presets.First();
        _presetStoreModel.LastSelectedPresetName = defaultPreset.Name;
        PersistPresetStore();
        return ApplyRenderUiState(ToRenderUiState(defaultPreset));
    }

    public HostValidationState ValidateInputs(string avatarPath, string oscBindPortText, string oscPublishAddress)
    {
        var avatarValid = TryValidateAvatarPath(avatarPath, out var avatarError);
        var bindPortValid = TryValidateOscBindPort(oscBindPortText, out var bindPortError);
        var publishValid = TryValidateOscPublishAddress(oscPublishAddress, out var publishError);
        return new HostValidationState(
            avatarValid,
            bindPortValid,
            publishValid,
            avatarError,
            bindPortError,
            publishError);
    }

    public NcResultCode Tick(float deltaTimeSeconds)
    {
        if (_trackingInputService.TryGetLatestFrame(out var trackingFrame))
        {
            var trackingRc = NativeCoreInterop.nc_set_tracking_frame(ref trackingFrame);
            if (trackingRc != NcResultCode.Ok)
            {
                TrackResult("SetTrackingFrame", trackingRc);
            }
        }
        if (_trackingInputService.TryGetLatestExpressionWeights(out var expressionWeights) &&
            expressionWeights.Count > 0)
        {
            var payload = expressionWeights
                .Where(static pair => !IsPoseChannel(pair.Key))
                .Select(static pair => new NcExpressionWeight
                {
                    Name = pair.Key,
                    Weight = Math.Clamp(pair.Value, 0.0f, 1.0f),
                })
                .ToArray();
            if (payload.Length > 0)
            {
                var exprRc = NativeCoreInterop.nc_set_expression_weights(payload, (uint)payload.Length);
                if (exprRc != NcResultCode.Ok)
                {
                    TrackResult("SetExpressionWeights", exprRc);
                }
            }
        }
        _trackingDiagnostics = _trackingInputService.GetDiagnostics();
        ReconcileTrackingDiagnostics();

        if (_sessionService.ActiveAvatarHandle.HasValue)
        {
            if (NativeCoreInterop.nc_get_runtime_stats(out var statsRc) == NcResultCode.Ok &&
                statsRc.RenderReadyAvatarCount == 0U)
            {
                var recoverRc = NativeCoreInterop.nc_create_render_resources(_sessionService.ActiveAvatarHandle.Value);
                TrackResult("RecoverRenderResources", recoverRc);
            }
        }

        var rc = _renderLoopService.Tick(deltaTimeSeconds);
        SessionState = SessionState with { LastRenderRc = rc };
        if (rc != NcResultCode.Ok)
        {
            TrackResult("RenderTick", rc);
        }

        var refreshRc = _sessionService.RefreshAvatarInfo();
        if (refreshRc != NcResultCode.Ok)
        {
            TrackResult("RefreshAvatarInfo", refreshRc);
        }

        ReconcileRuntimeOutputState();
        RecordFrameMetricAndGuardrails();
        RefreshState();
        return rc;
    }

    public void RefreshState()
    {
        SessionState = new HostSessionState(
            _sessionService.IsInitialized,
            _windowAttached && _windowHandle != IntPtr.Zero,
            _sessionService.ActiveAvatarHandle,
            SessionState.LastRenderRc,
            SessionState.LogicalWidth,
            SessionState.LogicalHeight,
            SessionState.DpiScaleX,
            SessionState.DpiScaleY,
            SessionState.RenderWidthPx,
            SessionState.RenderHeightPx);
        PublishDiagnostics();
        PersistSessionSnapshot();
        StateChanged?.Invoke(this, EventArgs.Empty);
    }

    public void UpdateRenderMetrics(
        double logicalWidth,
        double logicalHeight,
        double dpiScaleX,
        double dpiScaleY,
        uint renderWidthPx,
        uint renderHeightPx)
    {
        var normalizedLogicalWidth = Math.Max(1.0, logicalWidth);
        var normalizedLogicalHeight = Math.Max(1.0, logicalHeight);
        var normalizedDpiX = dpiScaleX > 0.0 ? dpiScaleX : 1.0;
        var normalizedDpiY = dpiScaleY > 0.0 ? dpiScaleY : 1.0;
        var normalizedRenderWidth = Math.Max(1U, renderWidthPx);
        var normalizedRenderHeight = Math.Max(1U, renderHeightPx);

        var changed =
            Math.Abs(SessionState.LogicalWidth - normalizedLogicalWidth) > 0.01 ||
            Math.Abs(SessionState.LogicalHeight - normalizedLogicalHeight) > 0.01 ||
            Math.Abs(SessionState.DpiScaleX - normalizedDpiX) > 0.001 ||
            Math.Abs(SessionState.DpiScaleY - normalizedDpiY) > 0.001 ||
            SessionState.RenderWidthPx != normalizedRenderWidth ||
            SessionState.RenderHeightPx != normalizedRenderHeight;
        if (!changed)
        {
            return;
        }

        SessionState = SessionState with
        {
            LogicalWidth = normalizedLogicalWidth,
            LogicalHeight = normalizedLogicalHeight,
            DpiScaleX = normalizedDpiX,
            DpiScaleY = normalizedDpiY,
            RenderWidthPx = normalizedRenderWidth,
            RenderHeightPx = normalizedRenderHeight,
        };
        PublishDiagnostics();
        StateChanged?.Invoke(this, EventArgs.Empty);
    }

    private DiagnosticsSnapshot BuildSnapshot()
    {
        var runtime = DiagnosticsModel.Capture();
        return new DiagnosticsSnapshot(
            DateTimeOffset.UtcNow,
            _snapshotVersion,
            _logVersion,
            SessionState,
            Outputs,
            RenderState,
            _trackingDiagnostics,
            runtime,
            _sessionService.ActiveAvatarInfo,
            SessionState.LastRenderRc);
    }

    private void PublishDiagnostics()
    {
        _snapshotVersion++;
        LastSnapshot = BuildSnapshot();
        DiagnosticsUpdated?.Invoke(this, EventArgs.Empty);
    }

    private NcResultCode ApplyRenderOptionsInternal(string source)
    {
        if (!_sessionService.IsInitialized)
        {
            return NcResultCode.Ok;
        }

        var rc = NativeCoreInterop.nc_set_render_quality_options(ref _renderOptions);
        TrackResult(source, rc);
        if (rc != NcResultCode.Ok)
        {
            return rc;
        }

        if (NativeCoreInterop.nc_get_render_quality_options(out var applied) == NcResultCode.Ok)
        {
            _renderOptions = applied;
            RenderState = BuildRenderUiState(
                applied,
                RenderState.BroadcastMode,
                InferBackgroundPreset(applied),
                RenderState.MirrorMode);
        }

        return rc;
    }

    private static NcRenderQualityOptions ToNativeOptions(RenderUiState state)
    {
        var options = new NcRenderQualityOptions
        {
            CameraMode = ToNativeCameraMode(state.CameraMode),
            FramingTarget = Clamp(state.FramingTarget, 0.35f, 0.95f),
            Headroom = Clamp(state.Headroom, 0.0f, 0.5f),
            YawDeg = Clamp(state.YawDeg, -45.0f, 45.0f),
            FovDeg = Clamp(state.FovDeg, 20.0f, 70.0f),
            ShowDebugOverlay = state.ShowDebugOverlay ? 1U : 0U,
        };
        ApplyBackgroundPreset(ref options, state.BackgroundPreset);
        return options;
    }

    private static NcCameraMode ToNativeCameraMode(RenderCameraMode mode)
    {
        return mode switch
        {
            RenderCameraMode.AutoFitFull => NcCameraMode.AutoFitFull,
            RenderCameraMode.Manual => NcCameraMode.Manual,
            _ => NcCameraMode.AutoFitBust,
        };
    }

    private static RenderCameraMode ToRenderCameraMode(NcCameraMode mode)
    {
        return mode switch
        {
            NcCameraMode.AutoFitFull => RenderCameraMode.AutoFitFull,
            NcCameraMode.Manual => RenderCameraMode.Manual,
            _ => RenderCameraMode.AutoFitBust,
        };
    }

    private static void ApplyBackgroundPreset(ref NcRenderQualityOptions options, BackgroundPreset preset)
    {
        switch (preset)
        {
            case BackgroundPreset.NeutralGray:
                options.BackgroundR = 0.55f;
                options.BackgroundG = 0.55f;
                options.BackgroundB = 0.55f;
                options.BackgroundA = 1.0f;
                break;
            case BackgroundPreset.GreenScreen:
                options.BackgroundR = 0.0f;
                options.BackgroundG = 1.0f;
                options.BackgroundB = 0.0f;
                options.BackgroundA = 1.0f;
                break;
            default:
                options.BackgroundR = 0.08f;
                options.BackgroundG = 0.12f;
                options.BackgroundB = 0.18f;
                options.BackgroundA = 1.0f;
                break;
        }
    }

    private static BackgroundPreset InferBackgroundPreset(NcRenderQualityOptions options)
    {
        if (Math.Abs(options.BackgroundG - 1.0f) < 0.001f &&
            Math.Abs(options.BackgroundR) < 0.001f &&
            Math.Abs(options.BackgroundB) < 0.001f)
        {
            return BackgroundPreset.GreenScreen;
        }
        if (Math.Abs(options.BackgroundR - options.BackgroundG) < 0.02f &&
            Math.Abs(options.BackgroundG - options.BackgroundB) < 0.02f)
        {
            return BackgroundPreset.NeutralGray;
        }
        return BackgroundPreset.DarkBlue;
    }

    private static RenderUiState BuildRenderUiState(
        NcRenderQualityOptions options,
        bool broadcastMode,
        BackgroundPreset preset,
        bool mirrorMode)
    {
        return new RenderUiState(
            broadcastMode,
            ToRenderCameraMode(options.CameraMode),
            options.FramingTarget,
            options.Headroom,
            options.YawDeg,
            options.FovDeg,
            preset,
            options.ShowDebugOverlay != 0U,
            mirrorMode);
    }

    private static RenderUiState ToRenderUiState(RenderPresetModel preset)
    {
        return NormalizeRenderState(
            new RenderUiState(
                preset.BroadcastMode,
                preset.CameraMode,
                preset.FramingTarget,
                preset.Headroom,
                preset.YawDeg,
                preset.FovDeg,
                preset.BackgroundPreset,
                preset.ShowDebugOverlay,
                preset.MirrorMode));
    }

    private static RenderUiState NormalizeRenderState(RenderUiState state)
    {
        return state with
        {
            FramingTarget = Clamp(state.FramingTarget, 0.35f, 0.95f),
            Headroom = Clamp(state.Headroom, 0.0f, 0.5f),
            YawDeg = Clamp(state.YawDeg, -45.0f, 45.0f),
            FovDeg = Clamp(state.FovDeg, 20.0f, 70.0f),
        };
    }

    private static RenderPresetStoreModel EnsurePresetStoreModel(RenderPresetStoreModel store)
    {
        if (store.Presets.Count == 0)
        {
            return RenderPresetStoreModel.CreateDefault();
        }

        if (string.IsNullOrWhiteSpace(store.LastSelectedPresetName))
        {
            store.LastSelectedPresetName = store.Presets[0].Name;
        }
        return store;
    }

    private bool TryGetSelectedPreset(out RenderPresetModel preset)
    {
        var selectedName = _presetStoreModel.LastSelectedPresetName;
        preset = _presetStoreModel.Presets.First();
        if (string.IsNullOrWhiteSpace(selectedName))
        {
            return false;
        }

        var found = _presetStoreModel.Presets.FirstOrDefault(
            p => string.Equals(p.Name, selectedName, StringComparison.OrdinalIgnoreCase));
        if (found is null)
        {
            return false;
        }

        preset = found;
        return true;
    }

    private static string NormalizePresetName(string name)
    {
        return string.IsNullOrWhiteSpace(name) ? string.Empty : name.Trim();
    }

    private void PersistPresetStore()
    {
        _presetStore.Save(_presetStoreModel);
    }

    private static float Clamp(float value, float min, float max)
    {
        return Math.Min(max, Math.Max(min, value));
    }

    private static bool IsPoseChannel(string key)
    {
        if (string.IsNullOrWhiteSpace(key))
        {
            return true;
        }

        return key.Equals("headyaw", StringComparison.OrdinalIgnoreCase) ||
               key.Equals("headpitch", StringComparison.OrdinalIgnoreCase) ||
               key.Equals("headroll", StringComparison.OrdinalIgnoreCase) ||
               key.Equals("headposx", StringComparison.OrdinalIgnoreCase) ||
               key.Equals("headposy", StringComparison.OrdinalIgnoreCase) ||
               key.Equals("headposz", StringComparison.OrdinalIgnoreCase);
    }

    private void TrackResult(string source, NcResultCode rc)
    {
        TrackTelemetryEvent(source, rc);
        if (rc == NcResultCode.Ok)
        {
            AddLog(new HostLogEntry(DateTimeOffset.UtcNow, source, "ok", rc), false);
            return;
        }

        if (source.Contains("Tracking", StringComparison.OrdinalIgnoreCase))
        {
            var trackingMessage = string.IsNullOrWhiteSpace(_trackingDiagnostics.StatusMessage)
                ? "tracking operation failed"
                : _trackingDiagnostics.StatusMessage;
            var trackingEntry = new HostLogEntry(DateTimeOffset.UtcNow, source, trackingMessage, rc);
            AddLog(trackingEntry, true);
            return;
        }

        var detail = NativeCoreInterop.FormatLastError();
        var userFacing = BuildUserFacingError(source, rc, detail);
        if (source.Contains("LoadAvatar", StringComparison.OrdinalIgnoreCase))
        {
            _lastLoadFailureGuidance = $"[{userFacing.ErrorCode}] {userFacing.Title} | {userFacing.ActionHint}";
            _lastLoadFailureTechnical = BuildLoadFailureTechnical(detail);
        }
        var entry = new HostLogEntry(DateTimeOffset.UtcNow, source, $"{userFacing.Title}: {userFacing.ActionHint} | {userFacing.TechnicalDetail}", rc);
        AddLog(entry, true);
    }

    private string BuildLoadFailureTechnical(string detail)
    {
        var parts = new List<string>();
        var info = _sessionService.LastLoadAttemptInfo;
        if (info.HasValue)
        {
            parts.Add(
                $"AvatarInfo: format={info.Value.DetectedFormat}, compat={info.Value.CompatLevel}, parser_stage={info.Value.ParserStage}, primary_error={info.Value.PrimaryErrorCode}, mesh_payloads={info.Value.MeshPayloadCount}, materials={info.Value.MaterialCount}");
        }

        if (!string.IsNullOrWhiteSpace(detail))
        {
            parts.Add(detail);
        }
        return string.Join(Environment.NewLine, parts);
    }

    private void AddLog(HostLogEntry entry, bool raiseError)
    {
        _logVersion++;
        _logs.Enqueue(entry);
        while (_logs.Count > MaxLogEntries)
        {
            _ = _logs.Dequeue();
        }

        if (raiseError)
        {
            ErrorRaised?.Invoke(this, entry);
        }
    }

    private NcResultCode ExecuteOperation(string operationName, Func<NcResultCode> action)
    {
        if (!ValidateOperationAllowed(operationName, out var blockedRc))
        {
            TrackResult($"{operationName}.Blocked", blockedRc);
            return blockedRc;
        }

        SetOperationState(true, operationName);
        try
        {
            return action();
        }
        finally
        {
            SetOperationState(false, string.Empty);
        }
    }

    private void SetOperationState(bool isBusy, string operationName)
    {
        if (OperationState.IsBusy == isBusy &&
            string.Equals(OperationState.CurrentOperation, operationName, StringComparison.Ordinal))
        {
            return;
        }

        OperationState = new HostOperationState(isBusy, operationName);
        PublishDiagnostics();
        StateChanged?.Invoke(this, EventArgs.Empty);
    }

    private static bool TryValidateAvatarPath(string path, out string error)
    {
        var trimmed = path?.Trim() ?? string.Empty;
        if (string.IsNullOrWhiteSpace(trimmed))
        {
            error = "Avatar path is required.";
            return false;
        }

        var extension = Path.GetExtension(trimmed).ToLowerInvariant();
        if (extension is not ".vrm" and not ".vsfavatar" and not ".xav2")
        {
            error = "Unsupported avatar file extension.";
            return false;
        }

        if (!File.Exists(trimmed))
        {
            error = "Avatar file does not exist.";
            return false;
        }

        error = string.Empty;
        return true;
    }

    private static bool TryValidateOscBindPort(string bindPortText, out string error)
    {
        var trimmed = bindPortText?.Trim() ?? string.Empty;
        if (!ushort.TryParse(trimmed, out _))
        {
            error = "OSC bind port must be 0-65535.";
            return false;
        }

        error = string.Empty;
        return true;
    }

    private static bool TryValidateOscPublishAddress(string publishAddress, out string error)
    {
        var trimmed = publishAddress?.Trim() ?? string.Empty;
        if (string.IsNullOrWhiteSpace(trimmed))
        {
            error = "OSC publish address is required.";
            return false;
        }

        var parts = trimmed.Split(':', StringSplitOptions.TrimEntries);
        if (parts.Length != 2 || string.IsNullOrWhiteSpace(parts[0]) || !ushort.TryParse(parts[1], out _))
        {
            error = "Use host:port format, for example 127.0.0.1:39540.";
            return false;
        }

        error = string.Empty;
        return true;
    }

    private void AutoReconfigureSpout(uint width, uint height)
    {
        if (!Outputs.SpoutActive)
        {
            return;
        }
        if (Outputs.SpoutWidthPx == width && Outputs.SpoutHeightPx == height)
        {
            return;
        }

        var stopRc = _outputService.StopSpout();
        TrackResult("SpoutAutoStop", stopRc);
        if (stopRc != NcResultCode.Ok)
        {
            return;
        }

        var fps = Outputs.SpoutFps > 0U ? Outputs.SpoutFps : 60U;
        var channel = string.IsNullOrWhiteSpace(Outputs.SpoutChannelName) ? "VsfClone" : Outputs.SpoutChannelName;
        var startRc = _outputService.StartSpout(width, height, fps, channel);
        TrackResult("SpoutAutoStart", startRc);
        _desiredSpoutActive = startRc == NcResultCode.Ok;
        Outputs = Outputs with
        {
            SpoutActive = startRc == NcResultCode.Ok,
            SpoutWidthPx = width,
            SpoutHeightPx = height,
            SpoutFps = fps,
            SpoutChannelName = channel,
        };
        AddLog(
            new HostLogEntry(
                DateTimeOffset.UtcNow,
                "SpoutAutoReconfigure",
                $"target={width}x{height} fps={fps}",
                startRc),
            false);
    }

    private void ReconcileRuntimeOutputState()
    {
        if (OperationState.IsBusy || !_sessionService.IsInitialized)
        {
            return;
        }

        var statsRc = NativeCoreInterop.nc_get_runtime_stats(out var runtimeStats);
        if (statsRc != NcResultCode.Ok)
        {
            TrackResult("RuntimeStatsSync", statsRc);
            return;
        }

        var runtimeSpoutActive = runtimeStats.SpoutActive != 0U;
        var runtimeOscActive = runtimeStats.OscActive != 0U;
        if (Outputs.SpoutActive == runtimeSpoutActive &&
            Outputs.OscActive == runtimeOscActive)
        {
            return;
        }

        var now = DateTimeOffset.UtcNow;
        if ((now - _lastOutputStateSyncLogUtc) >= TimeSpan.FromSeconds(2))
        {
            AddLog(
                new HostLogEntry(
                    now,
                    "OutputStateSync",
                    $"mismatch detected (ui: spout={Outputs.SpoutActive}, osc={Outputs.OscActive}; runtime: spout={runtimeSpoutActive}, osc={runtimeOscActive})",
                    NcResultCode.Ok),
                false);
            _lastOutputStateSyncLogUtc = now;
        }

        Outputs = Outputs with
        {
            SpoutActive = runtimeSpoutActive,
            OscActive = runtimeOscActive,
        };

        if ((now - _lastOutputRecoveryAttemptUtc) < TimeSpan.FromSeconds(4))
        {
            return;
        }
        _lastOutputRecoveryAttemptUtc = now;

        if (_desiredSpoutActive != runtimeSpoutActive)
        {
            ReconcileSpout(runtimeSpoutActive);
        }
        if (_desiredOscActive != runtimeOscActive)
        {
            ReconcileOsc(runtimeOscActive);
        }
    }

    private void ReconcileTrackingDiagnostics()
    {
        if (_trackingDiagnostics.IsActive != _lastTrackingActive)
        {
            AddLog(
                new HostLogEntry(
                    DateTimeOffset.UtcNow,
                    "TrackingState",
                    _trackingDiagnostics.IsActive ? "active" : "inactive",
                    NcResultCode.Ok),
                false);
            _lastTrackingActive = _trackingDiagnostics.IsActive;
        }

        var currentFormat = string.IsNullOrWhiteSpace(_trackingDiagnostics.DetectedFormat)
            ? "unknown"
            : _trackingDiagnostics.DetectedFormat.Trim();
        if (!string.Equals(currentFormat, _lastTrackingFormat, StringComparison.OrdinalIgnoreCase))
        {
            AddLog(
                new HostLogEntry(
                    DateTimeOffset.UtcNow,
                    "TrackingFormatDetected",
                    currentFormat,
                    NcResultCode.Ok),
                false);
            _lastTrackingFormat = currentFormat;
        }

        if (_trackingDiagnostics.ParseErrors > _lastTrackingParseErrors)
        {
            AddLog(
                new HostLogEntry(
                    DateTimeOffset.UtcNow,
                    "TrackingParseError",
                    $"parse_errors={_trackingDiagnostics.ParseErrors}, dropped={_trackingDiagnostics.DroppedPackets}",
                    NcResultCode.InvalidArgument),
                false);
            _lastTrackingParseErrors = _trackingDiagnostics.ParseErrors;
        }
    }

    private void ReconcileSpout(bool runtimeSpoutActive)
    {
        NcResultCode rc;
        if (_desiredSpoutActive)
        {
            var width = Outputs.SpoutWidthPx > 0U ? Outputs.SpoutWidthPx : Math.Max(1U, SessionState.RenderWidthPx);
            var height = Outputs.SpoutHeightPx > 0U ? Outputs.SpoutHeightPx : Math.Max(1U, SessionState.RenderHeightPx);
            var fps = Outputs.SpoutFps > 0U ? Outputs.SpoutFps : 60U;
            var channel = string.IsNullOrWhiteSpace(Outputs.SpoutChannelName) ? "VsfClone" : Outputs.SpoutChannelName;
            rc = _outputService.StartSpout(width, height, fps, channel);
            TrackResult("OutputStateSyncSpoutStart", rc);
        }
        else if (runtimeSpoutActive)
        {
            rc = _outputService.StopSpout();
            TrackResult("OutputStateSyncSpoutStop", rc);
        }
        else
        {
            return;
        }

        var statsRc = NativeCoreInterop.nc_get_runtime_stats(out var statsAfter);
        if (statsRc == NcResultCode.Ok)
        {
            Outputs = Outputs with { SpoutActive = statsAfter.SpoutActive != 0U };
            AddLog(
                new HostLogEntry(
                    DateTimeOffset.UtcNow,
                    "OutputStateSyncSpout",
                    $"desired={_desiredSpoutActive}, runtime_after={Outputs.SpoutActive}",
                    rc),
                false);
            return;
        }

        TrackResult("OutputStateSyncSpoutStats", statsRc);
    }

    private void ReconcileOsc(bool runtimeOscActive)
    {
        NcResultCode rc;
        if (_desiredOscActive)
        {
            var bindPort = Outputs.OscBindPort;
            var publishAddress = string.IsNullOrWhiteSpace(Outputs.OscPublishAddress) ? "127.0.0.1:39540" : Outputs.OscPublishAddress;
            rc = _outputService.StartOsc(bindPort, publishAddress);
            TrackResult("OutputStateSyncOscStart", rc);
        }
        else if (runtimeOscActive)
        {
            rc = _outputService.StopOsc();
            TrackResult("OutputStateSyncOscStop", rc);
        }
        else
        {
            return;
        }

        var statsRc = NativeCoreInterop.nc_get_runtime_stats(out var statsAfter);
        if (statsRc == NcResultCode.Ok)
        {
            Outputs = Outputs with { OscActive = statsAfter.OscActive != 0U };
            AddLog(
                new HostLogEntry(
                    DateTimeOffset.UtcNow,
                    "OutputStateSyncOsc",
                    $"desired={_desiredOscActive}, runtime_after={Outputs.OscActive}",
                    rc),
                false);
            return;
        }

        TrackResult("OutputStateSyncOscStats", statsRc);
    }
}
