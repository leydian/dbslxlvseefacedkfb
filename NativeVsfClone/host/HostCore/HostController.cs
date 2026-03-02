using System;
using System.Collections.Generic;

namespace HostCore;

public sealed class HostController
{
    private const int MaxLogEntries = 200;

    private readonly IAvatarSessionService _sessionService;
    private readonly IRenderLoopService _renderLoopService;
    private readonly IOutputService _outputService;
    private readonly Queue<HostLogEntry> _logs = new();
    private NcRenderQualityOptions _renderOptions;
    private bool _windowAttached;
    private IntPtr _windowHandle = IntPtr.Zero;

    public HostController()
        : this(new AvatarSessionService(), new RenderLoopService(), new OutputService())
    {
    }

    public HostController(
        IAvatarSessionService sessionService,
        IRenderLoopService renderLoopService,
        IOutputService outputService)
    {
        _sessionService = sessionService;
        _renderLoopService = renderLoopService;
        _outputService = outputService;
        _renderOptions = NativeCoreInterop.BuildBroadcastPreset();
        SessionState = new HostSessionState(false, false, null, NcResultCode.Ok);
        Outputs = new OutputState(false, false, "VsfClone", 0U, 0U, 60U, 39539, "127.0.0.1:39540");
        RenderState = BuildRenderUiState(_renderOptions, true, BackgroundPreset.DarkBlue, false);
        LastSnapshot = BuildSnapshot();
    }

    public HostSessionState SessionState { get; private set; }
    public OutputState Outputs { get; private set; }
    public RenderUiState RenderState { get; private set; }
    public DiagnosticsSnapshot LastSnapshot { get; private set; }

    public event EventHandler? StateChanged;
    public event EventHandler? DiagnosticsUpdated;
    public event EventHandler<HostLogEntry>? ErrorRaised;

    public IReadOnlyCollection<HostLogEntry> LogEntries => _logs.ToArray();

    public NcResultCode Initialize()
    {
        var rc = _sessionService.Initialize();
        TrackResult("Initialize", rc);
        if (rc == NcResultCode.Ok)
        {
            ApplyRenderOptionsInternal("ApplyRenderOptionsInit");
        }
        RefreshState();
        return rc;
    }

    public NcResultCode Shutdown()
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

        if (Outputs.OscActive)
        {
            TrackResult("StopOsc", _outputService.StopOsc());
            Outputs = Outputs with { OscActive = false };
        }

        if (_sessionService.ActiveAvatarHandle.HasValue)
        {
            TrackResult("UnloadAvatar", _sessionService.UnloadAvatar());
        }

        var rc = _sessionService.Shutdown();
        TrackResult("Shutdown", rc);
        _renderOptions = NativeCoreInterop.BuildBroadcastPreset();
        RenderState = BuildRenderUiState(_renderOptions, true, BackgroundPreset.DarkBlue, false);
        SessionState = new HostSessionState(false, false, null, NcResultCode.Ok, 0.0, 0.0, 1.0, 1.0, 0U, 0U);
        PublishDiagnostics();
        StateChanged?.Invoke(this, EventArgs.Empty);
        return rc;
    }

    public NcResultCode AttachWindow(IntPtr hwnd, uint width, uint height)
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
    }

    public NcResultCode ResizeWindow(uint width, uint height)
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
    }

    public NcResultCode LoadAvatar(string path)
    {
        if (_sessionService.ActiveAvatarHandle.HasValue)
        {
            TrackResult("UnloadAvatar", _sessionService.UnloadAvatar());
        }

        var rc = _sessionService.LoadAvatar(path);
        TrackResult("LoadAvatar", rc);
        RefreshState();
        return rc;
    }

    public NcResultCode UnloadAvatar()
    {
        var rc = _sessionService.UnloadAvatar();
        TrackResult("UnloadAvatar", rc);
        RefreshState();
        return rc;
    }

    public NcResultCode StartSpout(uint width, uint height, uint fps, string channelName)
    {
        var rc = _outputService.StartSpout(width, height, fps, channelName);
        TrackResult("StartSpout", rc);
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
    }

    public NcResultCode StopSpout()
    {
        var rc = _outputService.StopSpout();
        TrackResult("StopSpout", rc);
        Outputs = Outputs with { SpoutActive = false };
        RefreshState();
        return rc;
    }

    public NcResultCode StartOsc(ushort bindPort, string publishAddress)
    {
        var rc = _outputService.StartOsc(bindPort, publishAddress);
        TrackResult("StartOsc", rc);
        Outputs = Outputs with
        {
            OscActive = rc == NcResultCode.Ok,
            OscBindPort = bindPort,
            OscPublishAddress = publishAddress,
        };
        RefreshState();
        return rc;
    }

    public NcResultCode StopOsc()
    {
        var rc = _outputService.StopOsc();
        TrackResult("StopOsc", rc);
        Outputs = Outputs with { OscActive = false };
        RefreshState();
        return rc;
    }

    public NcResultCode SetBroadcastMode(bool enabled)
    {
        var preferredPreset = RenderState.BackgroundPreset;
        _renderOptions = enabled ? NativeCoreInterop.BuildBroadcastPreset() : NativeCoreInterop.BuildDebugPreset();
        ApplyBackgroundPreset(ref _renderOptions, preferredPreset);
        RenderState = BuildRenderUiState(_renderOptions, enabled, preferredPreset, RenderState.MirrorMode);
        var rc = ApplyRenderOptionsInternal("SetBroadcastMode");
        RefreshState();
        return rc;
    }

    public NcResultCode ApplyRenderUiState(RenderUiState state)
    {
        _renderOptions = new NcRenderQualityOptions
        {
            CameraMode = ToNativeCameraMode(state.CameraMode),
            FramingTarget = state.FramingTarget,
            Headroom = state.Headroom,
            YawDeg = state.YawDeg,
            FovDeg = state.FovDeg,
            ShowDebugOverlay = state.ShowDebugOverlay ? 1U : 0U,
        };
        ApplyBackgroundPreset(ref _renderOptions, state.BackgroundPreset);
        RenderState = state;
        var rc = ApplyRenderOptionsInternal("ApplyRenderUiState");
        RefreshState();
        return rc;
    }

    public NcResultCode Tick(float deltaTimeSeconds)
    {
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
            SessionState,
            Outputs,
            RenderState,
            runtime,
            _sessionService.ActiveAvatarInfo,
            SessionState.LastRenderRc);
    }

    private void PublishDiagnostics()
    {
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

    private void TrackResult(string source, NcResultCode rc)
    {
        if (rc == NcResultCode.Ok)
        {
            AddLog(new HostLogEntry(DateTimeOffset.UtcNow, source, "ok", rc), false);
            return;
        }

        var detail = NativeCoreInterop.FormatLastError();
        var entry = new HostLogEntry(DateTimeOffset.UtcNow, source, detail, rc);
        AddLog(entry, true);
    }

    private void AddLog(HostLogEntry entry, bool raiseError)
    {
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
}
