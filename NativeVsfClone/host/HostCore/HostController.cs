using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;

namespace HostCore;

public sealed partial class HostController
{
    private const int MaxLogEntries = 200;
    private static readonly TimeSpan TickDiagnosticsPublishInterval = TimeSpan.FromMilliseconds(100);
    private static readonly TimeSpan RuntimeCaptureRefreshInterval = TimeSpan.FromMilliseconds(250);

    private readonly IAvatarSessionService _sessionService;
    private readonly IRenderLoopService _renderLoopService;
    private readonly IOutputService _outputService;
    private readonly IRenderPresetStore _presetStore;
    private readonly IPosePresetStore _posePresetStore;
    private readonly ITrackingInputService _trackingInputService;
    private readonly object _runtimeSync = new();
    private readonly Queue<HostLogEntry> _logs = new();
    private RenderPresetStoreModel _presetStoreModel;
    private PosePresetStoreModel _posePresetStoreModel;
    private NcRenderQualityOptions _renderOptions;
    private long _snapshotVersion;
    private long _logVersion;
    private bool _windowAttached;
    private IntPtr _windowHandle = IntPtr.Zero;
    private bool _renderTickSuppressedByLoad;
    private bool _desiredSpoutActive;
    private bool _desiredOscActive;
    private DateTimeOffset _lastOutputStateSyncLogUtc = DateTimeOffset.MinValue;
    private DateTimeOffset _lastOutputRecoveryAttemptUtc = DateTimeOffset.MinValue;
    private string _lastLoadFailureGuidance = string.Empty;
    private string _lastLoadFailureTechnical = string.Empty;
    private TrackingDiagnostics _trackingDiagnostics = new(false, "unknown", 0.0, 0.0, 0.0, int.MaxValue, true, 0, 0, 0, "stopped", TrackingSourceType.OscIfacial, "idle");
    private List<PoseBoneUiOffset> _poseOffsets = BuildDefaultPoseOffsets();
    private string _lastTrackingFormat = "unknown";
    private ulong _lastTrackingParseErrors;
    private bool _lastTrackingActive;
    private DiagnosticsModel _runtimeDiagnostics = DiagnosticsModel.Empty;
    private DateTimeOffset _lastDiagnosticsPublishUtc = DateTimeOffset.MinValue;
    private DateTimeOffset _lastRuntimeCaptureUtc = DateTimeOffset.MinValue;
    private ArmPoseTuningSettings _armPoseTuning = new(
        EnableSmoothing: true,
        SmoothingTauMs: 80.0f,
        DeadbandDeg: 0.8f,
        SoftClampDeg: 55.0f,
        HardClampMinDeg: -80.0f,
        HardClampMaxDeg: 85.0f,
        MaxDegreesPerSecond: 420.0f);
    private readonly Dictionary<PoseBoneKind, ArmPoseFilterState> _armPoseFilterState = new();
    private readonly List<ArmPoseSample> _armPoseHistory = new();
    private List<SuggestedArmPreset> _suggestedArmPresets = new();
    private long _armPoseInputVersion;
    private long _armPoseCapturedVersion;
    private DateTimeOffset _lastArmPoseInputUtc = DateTimeOffset.MinValue;

    private const int ArmPoseHistoryCapacity = 20;
    private const int ArmPoseSuggestionTopK = 3;
    private static readonly TimeSpan ArmPoseCaptureDelay = TimeSpan.FromMilliseconds(500);
    private const float ArmPoseSuggestionMinDeltaDeg = 1.0f;
    private const float ArmPoseSuggestionQuantizeDeg = 5.0f;

    public HostController()
        : this(new AvatarSessionService(), new RenderLoopService(), new OutputService(), new RenderPresetStore(), new PosePresetStore(), new TrackingInputService())
    {
    }

    public HostController(
        IAvatarSessionService sessionService,
        IRenderLoopService renderLoopService,
        IOutputService outputService,
        IRenderPresetStore presetStore,
        ITrackingInputService? trackingInputService = null)
        : this(sessionService, renderLoopService, outputService, presetStore, new PosePresetStore(), trackingInputService)
    {
    }

    public HostController(
        IAvatarSessionService sessionService,
        IRenderLoopService renderLoopService,
        IOutputService outputService,
        IRenderPresetStore presetStore,
        IPosePresetStore posePresetStore,
        ITrackingInputService? trackingInputService = null)
    {
        _sessionService = sessionService;
        _renderLoopService = renderLoopService;
        _outputService = outputService;
        _presetStore = presetStore;
        _posePresetStore = posePresetStore;
        _trackingInputService = trackingInputService ?? new TrackingInputService();
        _renderOptions = NativeCoreInterop.BuildBroadcastPreset();
        _presetStoreModel = EnsurePresetStoreModel(_presetStore.Load());
        _posePresetStoreModel = EnsurePosePresetStoreModel(_posePresetStore.Load());
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
        CaptureRuntimeDiagnostics(force: true);
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
    public IReadOnlyList<PosePresetModel> PosePresets => _posePresetStoreModel.Presets;
    public string? SelectedPosePresetName => _posePresetStoreModel.LastSelectedPresetName;
    public TrackingDiagnostics TrackingDiagnostics => _trackingDiagnostics;
    public IReadOnlyList<PoseBoneUiOffset> PoseOffsets => _poseOffsets;
    public ArmPoseTuningSettings ArmPoseTuning => _armPoseTuning;
    public IReadOnlyList<SuggestedArmPreset> SuggestedArmPresets => _suggestedArmPresets;

    public NcResultCode Initialize()
    {
        return ExecuteOperation("Initialize", () =>
        {
            var rc = _sessionService.Initialize();
            TrackResult("Initialize", rc);
            if (rc == NcResultCode.Ok)
            {
                MarkOnboardingInitialized();
                ApplyRenderOptionsInternal("ApplyRenderOptionsInit");
                ApplyPoseOffsetsInternal("ApplyPoseOffsetsInit");
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
            _poseOffsets = BuildDefaultPoseOffsets();
            _ = NativeCoreInterop.nc_clear_pose_offsets();
            SessionState = new HostSessionState(false, false, null, NcResultCode.Ok, 0.0, 0.0, 1.0, 1.0, 0U, 0U);
            _ = PublishDiagnostics(force: true, forceRuntimeRefresh: true);
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
            RecordAvatarSelection(normalizedPath);
            var previousHandle = _sessionService.ActiveAvatarHandle;

            var rc = _sessionService.LoadAvatar(normalizedPath);
            TrackResult("LoadAvatar", rc);
            if (rc == NcResultCode.Ok)
            {
                MarkOnboardingAvatarLoaded();
                if (previousHandle.HasValue)
                {
                    TrackResult("AvatarSwapCommitted", NcResultCode.Ok);
                }
                // Re-apply host-side render controls after avatar load in case
                // the native side resets camera/quality state during load.
                ApplyRenderOptionsInternal("ApplyRenderOptionsLoadAvatar");
                ApplyPoseOffsetsInternal("ApplyPoseOffsetsLoadAvatar");
                _lastLoadFailureGuidance = string.Empty;
                _lastLoadFailureTechnical = string.Empty;
            }
            else if (previousHandle.HasValue && _sessionService.ActiveAvatarHandle == previousHandle)
            {
                AddLog(
                    new HostLogEntry(
                        DateTimeOffset.UtcNow,
                        "AvatarSwapAborted",
                        $"load failed and active avatar preserved (rc={rc})",
                        rc),
                    false);
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
            if (rc == NcResultCode.Ok)
            {
                MarkOnboardingOutputStarted("spout");
            }
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
            if (rc == NcResultCode.Ok)
            {
                MarkOnboardingOutputStarted("osc");
            }
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
                trackingSettings.CameraDeviceKey,
                trackingSettings.InferenceFpsCap,
                trackingSettings.ParseErrorWarnThreshold,
                trackingSettings.DroppedPacketWarnThreshold,
                trackingSettings.SourceLockMode,
                trackingSettings.LatencyProfile,
                trackingSettings.PoseFilterProfile,
                trackingSettings.PoseDeadbandDeg);
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
            _renderOptions.YawDeg = Clamp(current.YawDeg, -180.0f, 180.0f);
            _renderOptions.FovDeg = Clamp(current.FovDeg, 20.0f, 70.0f);
            _renderOptions.ShowDebugOverlay = current.ShowDebugOverlay ? 1U : 0U;
            ApplyBackgroundPreset(ref _renderOptions, current.BackgroundPreset);
            ApplySelectedQualityProfile(ref _renderOptions);
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
            ApplySelectedQualityProfile(ref _renderOptions);
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

    public NcResultCode SetPoseOffset(PoseBoneKind bone, float pitchDeg, float yawDeg, float rollDeg)
    {
        var idx = FindPoseIndex(bone);
        if (idx < 0)
        {
            return NcResultCode.InvalidArgument;
        }

        var clampedYaw = Clamp(yawDeg, -45.0f, 45.0f);
        var clampedRoll = Clamp(rollDeg, -45.0f, 45.0f);
        var clampedPitch = Clamp(pitchDeg, -45.0f, 45.0f);
        if (bone == PoseBoneKind.LeftUpperArm || bone == PoseBoneKind.RightUpperArm)
        {
            clampedPitch = ProcessArmPitchInput(bone, pitchDeg);
            _armPoseInputVersion++;
            _lastArmPoseInputUtc = DateTimeOffset.UtcNow;
        }

        _poseOffsets[idx] = new PoseBoneUiOffset(
            bone,
            clampedPitch,
            clampedYaw,
            clampedRoll);
        var rc = ApplyPoseOffsetsInternal("SetPoseOffset");
        RefreshState();
        return rc;
    }

    public NcResultCode ResetPoseOffset(PoseBoneKind bone)
    {
        return SetPoseOffset(bone, 0.0f, 0.0f, 0.0f);
    }

    public NcResultCode ResetAllPoseOffsets()
    {
        _poseOffsets = BuildDefaultPoseOffsets();
        _armPoseFilterState.Clear();
        var rc = ApplyPoseOffsetsInternal("ResetAllPoseOffsets");
        RefreshState();
        return rc;
    }

    public void ConfigureArmPoseTuning(ArmPoseTuningSettings tuning)
    {
        _armPoseTuning = tuning with
        {
            SmoothingTauMs = Clamp(tuning.SmoothingTauMs, 10.0f, 240.0f),
            DeadbandDeg = Clamp(tuning.DeadbandDeg, 0.0f, 3.0f),
            SoftClampDeg = Clamp(tuning.SoftClampDeg, 20.0f, 75.0f),
            HardClampMinDeg = Clamp(tuning.HardClampMinDeg, -90.0f, -40.0f),
            HardClampMaxDeg = Clamp(tuning.HardClampMaxDeg, 40.0f, 90.0f),
            MaxDegreesPerSecond = Clamp(tuning.MaxDegreesPerSecond, 60.0f, 720.0f),
        };
        if (_armPoseTuning.HardClampMinDeg >= _armPoseTuning.HardClampMaxDeg - 1.0f)
        {
            _armPoseTuning = _armPoseTuning with { HardClampMinDeg = -80.0f, HardClampMaxDeg = 85.0f };
        }
        _armPoseFilterState.Clear();
        RefreshState();
    }

    public NcResultCode ApplySuggestedArmPreset(string presetName)
    {
        var preset = _suggestedArmPresets.FirstOrDefault(
            p => string.Equals(p.Name, presetName, StringComparison.OrdinalIgnoreCase));
        if (preset is null)
        {
            return NcResultCode.InvalidArgument;
        }

        var left = _poseOffsets.FirstOrDefault(static p => p.Bone == PoseBoneKind.LeftUpperArm);
        var right = _poseOffsets.FirstOrDefault(static p => p.Bone == PoseBoneKind.RightUpperArm);
        var leftRc = SetPoseOffset(PoseBoneKind.LeftUpperArm, preset.LeftPitchDeg, left?.YawDeg ?? 0.0f, left?.RollDeg ?? 0.0f);
        var rightRc = SetPoseOffset(PoseBoneKind.RightUpperArm, preset.RightPitchDeg, right?.YawDeg ?? 0.0f, right?.RollDeg ?? 0.0f);
        return leftRc != NcResultCode.Ok ? leftRc : rightRc;
    }

    public PosePresetModel CreatePosePreset(string name)
    {
        var normalizedName = NormalizePresetName(name);
        var tracking = _sessionPersistence.Tracking;
        return new PosePresetModel(
            normalizedName,
            PosePresetStoreModel.CloneOffsets(_poseOffsets),
            tracking.PoseFilterProfile,
            tracking.PoseDeadbandDeg);
    }

    public bool SaveOrUpdatePosePreset(string name)
    {
        var normalizedName = NormalizePresetName(name);
        if (string.IsNullOrEmpty(normalizedName))
        {
            return false;
        }

        var preset = CreatePosePreset(normalizedName);
        var index = _posePresetStoreModel.Presets.FindIndex(
            p => string.Equals(p.Name, normalizedName, StringComparison.OrdinalIgnoreCase));
        if (index >= 0)
        {
            _posePresetStoreModel.Presets[index] = preset;
        }
        else
        {
            _posePresetStoreModel.Presets.Add(preset);
        }

        _posePresetStoreModel.LastSelectedPresetName = preset.Name;
        PersistPosePresetStore();
        RefreshState();
        return true;
    }

    public NcResultCode ApplyPosePreset(string name)
    {
        var normalizedName = NormalizePresetName(name);
        var preset = _posePresetStoreModel.Presets.FirstOrDefault(
            p => string.Equals(p.Name, normalizedName, StringComparison.OrdinalIgnoreCase));
        if (preset is null)
        {
            return NcResultCode.InvalidArgument;
        }

        _posePresetStoreModel.LastSelectedPresetName = preset.Name;
        _poseOffsets = PosePresetStoreModel.CloneOffsets(preset.Offsets);
        _armPoseFilterState.Clear();
        PersistPosePresetStore();
        var rc = ApplyPoseOffsetsInternal("ApplyPosePreset");

        var currentTracking = _sessionPersistence.Tracking;
        ConfigureTrackingInputSettings(
            currentTracking.ListenPort,
            currentTracking.StaleTimeoutMs,
            poseFilterProfile: preset.FilterProfile,
            poseDeadbandDeg: preset.PoseDeadbandDeg);

        RefreshState();
        return rc;
    }

    public bool DeletePosePreset(string name)
    {
        var normalizedName = NormalizePresetName(name);
        var index = _posePresetStoreModel.Presets.FindIndex(
            p => string.Equals(p.Name, normalizedName, StringComparison.OrdinalIgnoreCase));
        if (index < 0 || _posePresetStoreModel.Presets.Count <= 1)
        {
            return false;
        }

        var removedName = _posePresetStoreModel.Presets[index].Name;
        _posePresetStoreModel.Presets.RemoveAt(index);
        if (string.Equals(_posePresetStoreModel.LastSelectedPresetName, removedName, StringComparison.OrdinalIgnoreCase))
        {
            _posePresetStoreModel.LastSelectedPresetName = _posePresetStoreModel.Presets[0].Name;
        }

        PersistPosePresetStore();
        RefreshState();
        return true;
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
        MaybeCaptureArmPoseSample();
        if (!Monitor.TryEnter(_runtimeSync))
        {
            if (OperationState.IsBusy &&
                string.Equals(OperationState.CurrentOperation, "LoadAvatar", StringComparison.Ordinal) &&
                !_renderTickSuppressedByLoad)
            {
                TrackResult("TickSuppressedByLoad", NcResultCode.Ok);
                _renderTickSuppressedByLoad = true;
            }
            return SessionState.LastRenderRc;
        }

        try
        {
        var suppressRenderDuringLoad =
            OperationState.IsBusy &&
            string.Equals(OperationState.CurrentOperation, "LoadAvatar", StringComparison.Ordinal);
        if (suppressRenderDuringLoad)
        {
            if (!_renderTickSuppressedByLoad)
            {
                TrackResult("RenderTickSkippedDuringLoad", NcResultCode.Ok);
                _renderTickSuppressedByLoad = true;
            }
        }
        else if (_renderTickSuppressedByLoad)
        {
            TrackResult("RenderTickResumedAfterLoad", NcResultCode.Ok);
            _renderTickSuppressedByLoad = false;
        }

        if (_trackingInputService.TryGetLatestFrame(out var trackingFrame))
        {
            var trackingRc = NativeCoreInterop.nc_set_tracking_frame(ref trackingFrame);
            if (trackingRc != NcResultCode.Ok)
            {
                _trackingDiagnostics = _trackingDiagnostics with { LastErrorCode = $"NC_SET_TRACKING_FRAME_{trackingRc}" };
                TrackResult("SetTrackingFrame", trackingRc);
            }
            else if (!string.IsNullOrWhiteSpace(_trackingDiagnostics.LastErrorCode) &&
                     _trackingDiagnostics.LastErrorCode.StartsWith("NC_SET_TRACKING_FRAME_", StringComparison.Ordinal))
            {
                _trackingDiagnostics = _trackingDiagnostics with { LastErrorCode = string.Empty };
            }
        }
        else
        {
            // Keep render visibility stable when tracking input is absent.
            var neutralFrame = BuildNeutralTrackingFrame();
            var trackingRc = NativeCoreInterop.nc_set_tracking_frame(ref neutralFrame);
            if (trackingRc != NcResultCode.Ok)
            {
                _trackingDiagnostics = _trackingDiagnostics with { LastErrorCode = $"NC_SET_TRACKING_FRAME_{trackingRc}" };
                TrackResult("SetTrackingFrameNeutral", trackingRc);
            }
        }
        int? arkit52SubmittedCount = null;
        int? arkit52MissingCount = null;
        string? arkit52MissingKeys = null;
        if (_trackingInputService.TryGetLatestExpressionWeights(out var expressionWeights) &&
            expressionWeights.Count > 0)
        {
            var submittedCount = 0;
            var arkit52Missing = new List<string>();
            foreach (var channel in Arkit52Channels.NormalizedOrder)
            {
                if (expressionWeights.ContainsKey(channel))
                {
                    submittedCount++;
                }
                else
                {
                    arkit52Missing.Add(channel);
                }
            }

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
                    _trackingDiagnostics = _trackingDiagnostics with { LastErrorCode = $"NC_SET_EXPRESSION_WEIGHTS_{exprRc}" };
                    TrackResult("SetExpressionWeights", exprRc);
                }
                else if (!string.IsNullOrWhiteSpace(_trackingDiagnostics.LastErrorCode) &&
                         _trackingDiagnostics.LastErrorCode.StartsWith("NC_SET_EXPRESSION_WEIGHTS_", StringComparison.Ordinal))
                {
                    _trackingDiagnostics = _trackingDiagnostics with { LastErrorCode = string.Empty };
                }
            }
            arkit52SubmittedCount = submittedCount;
            arkit52MissingCount = arkit52Missing.Count;
            arkit52MissingKeys = string.Join(",", arkit52Missing);
        }
        _trackingDiagnostics = _trackingInputService.GetDiagnostics();
        if (arkit52SubmittedCount.HasValue && arkit52MissingCount.HasValue)
        {
            _trackingDiagnostics = _trackingDiagnostics with
            {
                Arkit52SubmittedCount = arkit52SubmittedCount.Value,
                Arkit52MissingCount = arkit52MissingCount.Value,
                Arkit52MissingKeys = arkit52MissingKeys ?? string.Empty,
            };
        }
        ReconcileTrackingDiagnostics();

        var rc = NcResultCode.Ok;
        if (!suppressRenderDuringLoad)
        {
            if (_sessionService.ActiveAvatarHandle.HasValue)
            {
                rc = _renderLoopService.Tick(deltaTimeSeconds);
                if (rc == NcResultCode.Unsupported)
                {
                    var detail = NativeCoreInterop.FormatLastError();
                    if (detail.Contains("no avatar has render resources", StringComparison.OrdinalIgnoreCase))
                    {
                        var recoverRc = NativeCoreInterop.nc_create_render_resources(_sessionService.ActiveAvatarHandle.Value);
                        TrackResult("RecoverRenderResourcesOnRenderTick", recoverRc);
                        if (recoverRc == NcResultCode.Ok)
                        {
                            rc = _renderLoopService.Tick(deltaTimeSeconds);
                            if (rc == NcResultCode.Ok)
                            {
                                TrackResult("RenderTickRecovered", NcResultCode.Ok);
                            }
                        }
                    }
                }
                SessionState = SessionState with { LastRenderRc = rc };
                if (rc != NcResultCode.Ok)
                {
                    TrackResult("RenderTick", rc);
                }
            }
            else
            {
                SessionState = SessionState with { LastRenderRc = NcResultCode.Ok };
            }
        }

        var refreshRc = _sessionService.RefreshAvatarInfo();
        if (refreshRc != NcResultCode.Ok)
        {
            TrackResult("RefreshAvatarInfo", refreshRc);
        }

        if (NativeCoreInterop.nc_get_runtime_stats(out var runtimeStats) == NcResultCode.Ok)
        {
            CaptureRuntimeDiagnostics(runtimeStats);

            if (!suppressRenderDuringLoad &&
                _sessionService.ActiveAvatarHandle.HasValue &&
                runtimeStats.RenderReadyAvatarCount == 0U)
            {
                var recoverRc = NativeCoreInterop.nc_create_render_resources(_sessionService.ActiveAvatarHandle.Value);
                TrackResult("RecoverRenderResources", recoverRc);
            }

            ReconcileRuntimeOutputState(runtimeStats);
            RecordFrameMetricAndGuardrails(runtimeStats);
        }
        else
        {
            _runtimeDiagnostics = _runtimeDiagnostics with { LastError = NativeCoreInterop.FormatLastError() };
            _lastRuntimeCaptureUtc = DateTimeOffset.UtcNow;
        }

        RefreshStateFastPath();
        return rc;
        }
        finally
        {
            Monitor.Exit(_runtimeSync);
        }
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
        _ = PublishDiagnostics(force: true, forceRuntimeRefresh: true);
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
        _ = PublishDiagnostics(force: true, forceRuntimeRefresh: true);
        StateChanged?.Invoke(this, EventArgs.Empty);
    }

    private DiagnosticsSnapshot BuildSnapshot()
    {
        return new DiagnosticsSnapshot(
            DateTimeOffset.UtcNow,
            _snapshotVersion,
            _logVersion,
            SessionState,
            Outputs,
            RenderState,
            _trackingDiagnostics,
            _runtimeDiagnostics,
            _sessionService.ActiveAvatarInfo,
            SessionState.LastRenderRc);
    }

    private bool PublishDiagnostics(bool force, bool forceRuntimeRefresh)
    {
        var now = DateTimeOffset.UtcNow;
        if (!force &&
            (now - _lastDiagnosticsPublishUtc) < TickDiagnosticsPublishInterval &&
            LastSnapshot.LogVersion == _logVersion)
        {
            return false;
        }

        CaptureRuntimeDiagnostics(force: forceRuntimeRefresh);
        _snapshotVersion++;
        LastSnapshot = BuildSnapshot();
        _lastDiagnosticsPublishUtc = now;
        DiagnosticsUpdated?.Invoke(this, EventArgs.Empty);
        return true;
    }

    private void RefreshStateFastPath()
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
        _ = PublishDiagnostics(force: false, forceRuntimeRefresh: false);
    }

    private void CaptureRuntimeDiagnostics(in NcRuntimeStats stats)
    {
        _ = NativeCoreInterop.nc_get_spout_diagnostics(out var spout);
        _runtimeDiagnostics = DiagnosticsModel.FromNative(stats, spout);
        _lastRuntimeCaptureUtc = DateTimeOffset.UtcNow;
    }

    private void CaptureRuntimeDiagnostics(bool force)
    {
        var now = DateTimeOffset.UtcNow;
        if (!force && (now - _lastRuntimeCaptureUtc) < RuntimeCaptureRefreshInterval)
        {
            return;
        }

        if (NativeCoreInterop.nc_get_runtime_stats(out var stats) == NcResultCode.Ok)
        {
            CaptureRuntimeDiagnostics(stats);
            return;
        }

        _runtimeDiagnostics = DiagnosticsModel.Empty with { LastError = NativeCoreInterop.FormatLastError() };
        _lastRuntimeCaptureUtc = now;
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
            YawDeg = Clamp(state.YawDeg, -180.0f, 180.0f),
            FovDeg = Clamp(state.FovDeg, 20.0f, 70.0f),
            ShowDebugOverlay = state.ShowDebugOverlay ? 1U : 0U,
        };
        ApplyBackgroundPreset(ref options, state.BackgroundPreset);
        return options;
    }

    private void ApplySelectedQualityProfile(ref NcRenderQualityOptions options)
    {
        var profile = _sessionPersistence.LastProfileName?.Trim().ToLowerInvariant();
        options.QualityProfile = profile switch
        {
            "ultra-parity" => (uint)NcRenderQualityProfile.UltraParity,
            "quality" => (uint)NcRenderQualityProfile.Balanced,
            "stability" => (uint)NcRenderQualityProfile.Balanced,
            "performance" => (uint)NcRenderQualityProfile.Balanced,
            _ => (uint)NcRenderQualityProfile.Default,
        };
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
            YawDeg = Clamp(state.YawDeg, -180.0f, 180.0f),
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

    private static PosePresetStoreModel EnsurePosePresetStoreModel(PosePresetStoreModel store)
    {
        if (store.Presets.Count == 0)
        {
            return PosePresetStoreModel.CreateDefault();
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

    private void PersistPosePresetStore()
    {
        _posePresetStore.Save(_posePresetStoreModel);
    }

    private static float Clamp(float value, float min, float max)
    {
        return Math.Min(max, Math.Max(min, value));
    }

    private float ProcessArmPitchInput(PoseBoneKind bone, float rawPitchDeg)
    {
        var now = DateTimeOffset.UtcNow;
        var hardClamped = Clamp(rawPitchDeg, _armPoseTuning.HardClampMinDeg, _armPoseTuning.HardClampMaxDeg);
        var target = ApplyArmSoftClamp(hardClamped);
        if (!_armPoseFilterState.TryGetValue(bone, out var state))
        {
            state = new ArmPoseFilterState(target, now);
        }

        var dtSeconds = Math.Max((float)(now - state.LastUpdateUtc).TotalSeconds, 1.0f / 120.0f);
        var maxDelta = _armPoseTuning.MaxDegreesPerSecond * dtSeconds;
        var delta = target - state.FilteredPitchDeg;
        if (Math.Abs(delta) > maxDelta)
        {
            target = state.FilteredPitchDeg + MathF.Sign(delta) * maxDelta;
        }

        float filtered;
        if (_armPoseTuning.EnableSmoothing)
        {
            if (Math.Abs(target - state.FilteredPitchDeg) < _armPoseTuning.DeadbandDeg)
            {
                filtered = state.FilteredPitchDeg;
            }
            else
            {
                var tauSeconds = _armPoseTuning.SmoothingTauMs / 1000.0f;
                var alpha = dtSeconds / (tauSeconds + dtSeconds);
                filtered = state.FilteredPitchDeg + alpha * (target - state.FilteredPitchDeg);
            }
        }
        else
        {
            filtered = target;
        }

        filtered = Clamp(filtered, _armPoseTuning.HardClampMinDeg, _armPoseTuning.HardClampMaxDeg);
        _armPoseFilterState[bone] = new ArmPoseFilterState(filtered, now);
        return filtered;
    }

    private float ApplyArmSoftClamp(float pitchDeg)
    {
        var soft = Math.Abs(_armPoseTuning.SoftClampDeg);
        var hardMin = _armPoseTuning.HardClampMinDeg;
        var hardMax = _armPoseTuning.HardClampMaxDeg;
        if (pitchDeg > soft)
        {
            var t = (pitchDeg - soft) / Math.Max(0.0001f, hardMax - soft);
            pitchDeg = soft + (hardMax - soft) * (1.0f - MathF.Exp(-t * 2.2f));
        }
        else if (pitchDeg < -soft)
        {
            var t = ((-pitchDeg) - soft) / Math.Max(0.0001f, (-hardMin) - soft);
            pitchDeg = -soft - (((-hardMin) - soft) * (1.0f - MathF.Exp(-t * 2.2f)));
        }
        return Clamp(pitchDeg, hardMin, hardMax);
    }

    private void MaybeCaptureArmPoseSample()
    {
        if (_armPoseInputVersion == _armPoseCapturedVersion)
        {
            return;
        }

        var now = DateTimeOffset.UtcNow;
        if (now - _lastArmPoseInputUtc < ArmPoseCaptureDelay)
        {
            return;
        }

        var left = _poseOffsets.FirstOrDefault(static p => p.Bone == PoseBoneKind.LeftUpperArm);
        var right = _poseOffsets.FirstOrDefault(static p => p.Bone == PoseBoneKind.RightUpperArm);
        if (left is null || right is null)
        {
            _armPoseCapturedVersion = _armPoseInputVersion;
            return;
        }

        if (_armPoseHistory.Count > 0)
        {
            var last = _armPoseHistory[^1];
            if (Math.Abs(last.LeftPitchDeg - left.PitchDeg) < ArmPoseSuggestionMinDeltaDeg &&
                Math.Abs(last.RightPitchDeg - right.PitchDeg) < ArmPoseSuggestionMinDeltaDeg)
            {
                _armPoseCapturedVersion = _armPoseInputVersion;
                return;
            }
        }

        _armPoseHistory.Add(new ArmPoseSample(left.PitchDeg, right.PitchDeg, now));
        if (_armPoseHistory.Count > ArmPoseHistoryCapacity)
        {
            _armPoseHistory.RemoveRange(0, _armPoseHistory.Count - ArmPoseHistoryCapacity);
        }
        _suggestedArmPresets = BuildSuggestedArmPresets(_armPoseHistory);
        _armPoseCapturedVersion = _armPoseInputVersion;
        RefreshState();
    }

    private static List<SuggestedArmPreset> BuildSuggestedArmPresets(List<ArmPoseSample> history)
    {
        var buckets = new Dictionary<string, (int Count, DateTimeOffset LastUsed, float LeftSum, float RightSum)>(StringComparer.Ordinal);
        foreach (var sample in history)
        {
            var qLeft = Quantize(sample.LeftPitchDeg, ArmPoseSuggestionQuantizeDeg);
            var qRight = Quantize(sample.RightPitchDeg, ArmPoseSuggestionQuantizeDeg);
            var key = $"{qLeft:F0}:{qRight:F0}";
            if (!buckets.TryGetValue(key, out var b))
            {
                b = (0, DateTimeOffset.MinValue, 0.0f, 0.0f);
            }
            b.Count += 1;
            b.LeftSum += sample.LeftPitchDeg;
            b.RightSum += sample.RightPitchDeg;
            if (sample.TimestampUtc > b.LastUsed)
            {
                b.LastUsed = sample.TimestampUtc;
            }
            buckets[key] = b;
        }

        var ordered = buckets
            .OrderByDescending(static x => x.Value.Count)
            .ThenByDescending(static x => x.Value.LastUsed)
            .Take(ArmPoseSuggestionTopK)
            .ToList();
        var output = new List<SuggestedArmPreset>(ordered.Count);
        var usedNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var item in ordered)
        {
            var left = item.Value.LeftSum / item.Value.Count;
            var right = item.Value.RightSum / item.Value.Count;
            var name = BuildArmPoseName((left + right) * 0.5f);
            if (!usedNames.Add(name))
            {
                var suffix = 2;
                while (!usedNames.Add($"{name} {suffix}"))
                {
                    suffix++;
                }
                name = $"{name} {suffix}";
            }
            output.Add(new SuggestedArmPreset(name, left, right, item.Value.Count, item.Value.LastUsed));
        }
        return output;
    }

    private static float Quantize(float value, float step)
    {
        if (step <= 0.0001f)
        {
            return value;
        }
        return MathF.Round(value / step) * step;
    }

    private static string BuildArmPoseName(float bothPitch)
    {
        if (bothPitch >= 60.0f)
        {
            return "Full Raise";
        }
        if (bothPitch >= 25.0f)
        {
            return "Half Raise";
        }
        if (bothPitch >= 6.0f)
        {
            return "Slight Raise";
        }
        if (bothPitch <= -15.0f)
        {
            return "Down Relax";
        }
        return "T Relax";
    }

    private sealed record ArmPoseFilterState(float FilteredPitchDeg, DateTimeOffset LastUpdateUtc);
    private sealed record ArmPoseSample(float LeftPitchDeg, float RightPitchDeg, DateTimeOffset TimestampUtc);

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

    private NcResultCode ApplyPoseOffsetsInternal(string source)
    {
        if (!_sessionService.IsInitialized)
        {
            return NcResultCode.Ok;
        }

        var payload = _poseOffsets.Select(static p => new NcPoseBoneOffset
        {
            BoneId = ToNativePoseBone(p.Bone),
            PitchDeg = p.PitchDeg,
            YawDeg = p.YawDeg,
            RollDeg = p.RollDeg,
        }).ToArray();
        var rc = NativeCoreInterop.nc_set_pose_offsets(payload, (uint)payload.Length);
        TrackResult(source, rc);
        return rc;
    }

    private static List<PoseBoneUiOffset> BuildDefaultPoseOffsets()
    {
        return new List<PoseBoneUiOffset>
        {
            new(PoseBoneKind.Hips, 0.0f, 0.0f, 0.0f),
            new(PoseBoneKind.Spine, 0.0f, 0.0f, 0.0f),
            new(PoseBoneKind.Chest, 0.0f, 0.0f, 0.0f),
            new(PoseBoneKind.UpperChest, 0.0f, 0.0f, 0.0f),
            new(PoseBoneKind.Neck, 0.0f, 0.0f, 0.0f),
            new(PoseBoneKind.Head, 0.0f, 0.0f, 0.0f),
            new(PoseBoneKind.LeftUpperArm, 0.0f, 0.0f, 0.0f),
            new(PoseBoneKind.RightUpperArm, 0.0f, 0.0f, 0.0f),
        };
    }

    private int FindPoseIndex(PoseBoneKind bone)
    {
        return _poseOffsets.FindIndex(p => p.Bone == bone);
    }

    private static NcPoseBoneId ToNativePoseBone(PoseBoneKind bone)
    {
        return bone switch
        {
            PoseBoneKind.Hips => NcPoseBoneId.Hips,
            PoseBoneKind.Spine => NcPoseBoneId.Spine,
            PoseBoneKind.Chest => NcPoseBoneId.Chest,
            PoseBoneKind.UpperChest => NcPoseBoneId.UpperChest,
            PoseBoneKind.Neck => NcPoseBoneId.Neck,
            PoseBoneKind.Head => NcPoseBoneId.Head,
            PoseBoneKind.LeftUpperArm => NcPoseBoneId.LeftUpperArm,
            PoseBoneKind.RightUpperArm => NcPoseBoneId.RightUpperArm,
            _ => NcPoseBoneId.Unknown,
        };
    }

    private static NcTrackingFrame BuildNeutralTrackingFrame()
    {
        return new NcTrackingFrame
        {
            HeadPosX = 0.0f,
            HeadPosY = 0.0f,
            HeadPosZ = 0.0f,
            HeadRotX = 0.0f,
            HeadRotY = 0.0f,
            HeadRotZ = 0.0f,
            HeadRotW = 1.0f,
            EyeGazeLX = 0.0f,
            EyeGazeLY = 0.0f,
            EyeGazeLZ = 0.0f,
            EyeGazeRX = 0.0f,
            EyeGazeRY = 0.0f,
            EyeGazeRZ = 0.0f,
            BlinkL = 0.0f,
            BlinkR = 0.0f,
            MouthOpen = 0.0f,
        };
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
            lock (_runtimeSync)
            {
                return action();
            }
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
        _ = PublishDiagnostics(force: true, forceRuntimeRefresh: true);
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
            error = "Unsupported avatar file extension. Supported: .vrm, .vsfavatar, .xav2. If your file is .xva2, rename/re-export to .xav2.";
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

    private void ReconcileRuntimeOutputState(in NcRuntimeStats runtimeStats)
    {
        if (OperationState.IsBusy || !_sessionService.IsInitialized)
        {
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
