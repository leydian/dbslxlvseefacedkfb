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
        SessionState = new HostSessionState(false, false, null, NcResultCode.Ok);
        Outputs = new OutputState(false, false, "VsfClone", 39539, "127.0.0.1:39540");
        LastSnapshot = BuildSnapshot();
    }

    public HostSessionState SessionState { get; private set; }
    public OutputState Outputs { get; private set; }
    public DiagnosticsSnapshot LastSnapshot { get; private set; }

    public event EventHandler? StateChanged;
    public event EventHandler? DiagnosticsUpdated;
    public event EventHandler<HostLogEntry>? ErrorRaised;

    public IReadOnlyCollection<HostLogEntry> LogEntries => _logs.ToArray();

    public NcResultCode Initialize()
    {
        var rc = _sessionService.Initialize();
        TrackResult("Initialize", rc);
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
        SessionState = new HostSessionState(false, false, null, NcResultCode.Ok);
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
            SessionState.LastRenderRc);
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
            runtime,
            _sessionService.ActiveAvatarInfo,
            SessionState.LastRenderRc);
    }

    private void PublishDiagnostics()
    {
        LastSnapshot = BuildSnapshot();
        DiagnosticsUpdated?.Invoke(this, EventArgs.Empty);
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
}
