using System.Globalization;
using System.Net.Sockets;
using System.Text.Json;

namespace HostCore;

public sealed record SpoutReceiverDiagnosticsSnapshot(
    NcResultCode ResultCode,
    bool Active,
    string ChannelName,
    string LastErrorCode);

public sealed record OverlayItemCommand(
    string Kind,
    string Text,
    string ColorHex,
    int DurationMs,
    string Anchor,
    double OffsetX,
    double OffsetY);

public sealed partial class HostController
{
    private readonly WorkflowStore _workflowStore = new();
    private readonly WorkflowEngine _workflowEngine = new();
    private readonly Queue<OverlayItemCommand> _overlayItemCommands = new();
    private readonly object _overlaySync = new();
    private readonly AutomationExtensionRegistry _automationExtensions = new();
    private bool _workflowLoaded;
    private bool _spoutReceiverActive;
    private string _spoutReceiverChannel = string.Empty;
    private bool _spoutReceiverAutoReconnect;
    private DateTimeOffset _lastSpoutReceiverReconnectAttemptUtc = DateTimeOffset.MinValue;

    public WorkflowGraphModel GetAutomationGraph()
    {
        EnsureAutomationLoaded();
        return _workflowEngine.Graph;
    }

    public string GetAutomationGraphJson()
    {
        EnsureAutomationLoaded();
        return _workflowEngine.GetGraphAsJson();
    }

    public bool SetAutomationGraphJson(string json, out string error)
    {
        error = string.Empty;
        try
        {
            _workflowEngine.SetGraphFromJson(json);
            _workflowLoaded = true;
            _workflowStore.Save(_workflowEngine.Graph);
            AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "AutomationGraph", "graph updated", NcResultCode.Ok), false);
            return true;
        }
        catch (Exception ex)
        {
            error = ex.Message;
            AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "AutomationGraph", $"graph update failed: {ex.Message}", NcResultCode.InvalidArgument), true);
            return false;
        }
    }

    public WorkflowExecutionSnapshot GetAutomationSnapshot()
    {
        EnsureAutomationLoaded();
        return _workflowEngine.GetSnapshot();
    }

    public void SetAutomationEnabled(bool enabled)
    {
        EnsureAutomationLoaded();
        var model = _workflowEngine.Graph;
        model.Enabled = enabled;
        _workflowEngine.SetGraph(model);
        _workflowStore.Save(model);
        AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "Automation", $"enabled={enabled}", NcResultCode.Ok), false);
    }

    public void TickAutomation()
    {
        EnsureAutomationLoaded();
        var ctx = new WorkflowTickContext(
            AvatarLoaded: _sessionService.ActiveAvatarHandle.HasValue,
            SpoutActive: Outputs.SpoutActive,
            OscActive: Outputs.OscActive,
            TrackingActive: _trackingDiagnostics.IsActive,
            UtcNow: DateTimeOffset.UtcNow);
        _workflowEngine.Tick(ctx, ExecuteAutomationAction);
        TryAutoReconnectSpoutReceiver();
    }

    public bool IsSpoutReceiverActive() => _spoutReceiverActive;
    public string SpoutReceiverChannelName() => _spoutReceiverChannel;

    public NcResultCode StartSpoutReceiver(string channelName)
    {
        var channel = string.IsNullOrWhiteSpace(channelName) ? "VTubeStudio" : channelName.Trim();
        var options = new NcSpoutReceiverOptions
        {
            ChannelName = channel,
            ForceLinear = 1U,
        };
        var rc = NativeCoreInterop.nc_start_spout_receiver(ref options);
        TrackResult("StartSpoutReceiver", rc);
        if (rc == NcResultCode.Ok)
        {
            _spoutReceiverActive = true;
            _spoutReceiverChannel = channel;
        }
        return rc;
    }

    public NcResultCode StopSpoutReceiver()
    {
        var rc = NativeCoreInterop.nc_stop_spout_receiver();
        TrackResult("StopSpoutReceiver", rc);
        _spoutReceiverActive = false;
        _spoutReceiverChannel = string.Empty;
        return rc;
    }

    public SpoutReceiverDiagnosticsSnapshot GetSpoutReceiverDiagnostics()
    {
        var rc = NativeCoreInterop.nc_get_spout_receiver_diagnostics(out var diag);
        if (rc != NcResultCode.Ok)
        {
            return new SpoutReceiverDiagnosticsSnapshot(
                ResultCode: rc,
                Active: _spoutReceiverActive,
                ChannelName: _spoutReceiverChannel,
                LastErrorCode: string.Empty);
        }

        _spoutReceiverActive = diag.Active != 0U;
        _spoutReceiverChannel = diag.ChannelName ?? string.Empty;
        return new SpoutReceiverDiagnosticsSnapshot(
            ResultCode: rc,
            Active: _spoutReceiverActive,
            ChannelName: _spoutReceiverChannel,
            LastErrorCode: diag.LastErrorCode ?? string.Empty);
    }

    public void SetSpoutReceiverAutoReconnect(bool enabled)
    {
        _spoutReceiverAutoReconnect = enabled;
    }

    public bool SpoutReceiverAutoReconnectEnabled() => _spoutReceiverAutoReconnect;

    public void TriggerAutomationCommand(string command)
    {
        EnsureAutomationLoaded();
        _workflowEngine.EnqueueCommand(command);
        AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "AutomationCommand", $"command={command}", NcResultCode.Ok), false);
    }

    public void RegisterAutomationExtension(IAutomationExtension extension)
    {
        _automationExtensions.Register(extension);
    }

    public IReadOnlyCollection<string> GetAutomationExtensionIds()
    {
        return _automationExtensions.ListIds();
    }

    public List<OverlayItemCommand> DequeueOverlayItemCommands()
    {
        lock (_overlaySync)
        {
            var result = _overlayItemCommands.ToList();
            _overlayItemCommands.Clear();
            return result;
        }
    }

    private void EnsureAutomationLoaded()
    {
        if (_workflowLoaded)
        {
            return;
        }
        _workflowEngine.SetGraph(_workflowStore.Load());
        _workflowLoaded = true;
    }

    private void ExecuteAutomationAction(WorkflowActionRequest request)
    {
        try
        {
            switch (request.ActionType)
            {
                case WorkflowActionType.SetExpression:
                    ExecuteActionSetExpression(request);
                    break;
                case WorkflowActionType.SwapAvatar:
                    ExecuteActionSwapAvatar(request);
                    break;
                case WorkflowActionType.SetRenderProfile:
                    ExecuteActionRenderProfile(request);
                    break;
                case WorkflowActionType.SetOutputState:
                    ExecuteActionOutputState(request);
                    break;
                case WorkflowActionType.SetSpout2ReceiverState:
                    ExecuteActionSpoutReceiver(request);
                    break;
                case WorkflowActionType.SendOsc:
                    ExecuteActionSendOsc(request);
                    break;
                case WorkflowActionType.SetExpressionBatch:
                    ExecuteActionSetExpressionBatch(request);
                    break;
                case WorkflowActionType.SpawnOverlayItem:
                    ExecuteActionSpawnOverlayItem(request);
                    break;
                case WorkflowActionType.ClearOverlayItems:
                    ExecuteActionClearOverlayItems();
                    break;
                case WorkflowActionType.Extension:
                    ExecuteActionExtension(request);
                    break;
            }
        }
        catch (Exception ex)
        {
            AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "AutomationAction", $"{request.ActionType} failed: {ex.Message}", NcResultCode.Internal), true);
        }
    }

    private void ExecuteActionSetExpression(WorkflowActionRequest request)
    {
        var name = request.Params.TryGetValue("name", out var n) ? n?.Trim() ?? string.Empty : string.Empty;
        if (string.IsNullOrWhiteSpace(name))
        {
            return;
        }
        var valueRaw = request.Params.TryGetValue("value", out var v) ? v : "0";
        if (!float.TryParse(valueRaw, NumberStyles.Float, CultureInfo.InvariantCulture, out var value))
        {
            value = 0.0f;
        }
        value = Math.Clamp(value, 0.0f, 1.0f);
        var payload = new[]
        {
            new NcExpressionWeight { Name = name, Weight = value },
        };
        var rc = NativeCoreInterop.nc_set_expression_weights(payload, 1U);
        TrackResult("AutomationSetExpression", rc);
    }

    private void ExecuteActionSwapAvatar(WorkflowActionRequest request)
    {
        var path = request.Params.TryGetValue("path", out var p) ? p?.Trim() ?? string.Empty : string.Empty;
        if (string.IsNullOrWhiteSpace(path))
        {
            return;
        }

        var preDelayMs = request.Params.TryGetValue("pre_delay_ms", out var preRaw) && int.TryParse(preRaw, out var pre) ? Math.Clamp(pre, 0, 10000) : 0;
        var postDelayMs = request.Params.TryGetValue("post_delay_ms", out var postRaw) && int.TryParse(postRaw, out var post) ? Math.Clamp(post, 0, 10000) : 0;
        var fallback = request.Params.TryGetValue("fallback_path", out var fallbackRaw) ? fallbackRaw?.Trim() ?? string.Empty : string.Empty;
        var preserveOutputs = request.Params.TryGetValue("preserve_outputs", out var preserveRaw) && bool.TryParse(preserveRaw, out var preserve) && preserve;
        var beforeSpout = Outputs.SpoutActive;
        var beforeOsc = Outputs.OscActive;

        _ = Task.Run(async () =>
        {
            if (preDelayMs > 0)
            {
                await Task.Delay(preDelayMs).ConfigureAwait(false);
            }

            var rc = LoadAvatar(path);
            if (rc != NcResultCode.Ok && !string.IsNullOrWhiteSpace(fallback))
            {
                _ = LoadAvatar(fallback);
            }

            if (postDelayMs > 0)
            {
                await Task.Delay(postDelayMs).ConfigureAwait(false);
            }

            if (preserveOutputs)
            {
                if (beforeSpout && !Outputs.SpoutActive)
                {
                    _ = StartSpout(
                        Outputs.SpoutWidthPx > 0 ? Outputs.SpoutWidthPx : 1280U,
                        Outputs.SpoutHeightPx > 0 ? Outputs.SpoutHeightPx : 720U,
                        Outputs.SpoutFps > 0 ? Outputs.SpoutFps : 60U,
                        string.IsNullOrWhiteSpace(Outputs.SpoutChannelName) ? "Animiq" : Outputs.SpoutChannelName);
                }

                if (beforeOsc && !Outputs.OscActive)
                {
                    _ = StartOsc(Outputs.OscBindPort, Outputs.OscPublishAddress);
                }
            }
        });
    }

    private void ExecuteActionRenderProfile(WorkflowActionRequest request)
    {
        var profile = request.Params.TryGetValue("profile", out var p) ? p?.Trim() ?? "quality" : "quality";
        _ = ApplyRenderProfile(profile);
    }

    private void ExecuteActionOutputState(WorkflowActionRequest request)
    {
        var spoutOn = request.Params.TryGetValue("spout_on", out var spRaw) && bool.TryParse(spRaw, out var sp) && sp;
        var oscOn = request.Params.TryGetValue("osc_on", out var oscRaw) && bool.TryParse(oscRaw, out var os) && os;

        if (spoutOn && !Outputs.SpoutActive)
        {
            _ = StartSpout(
                Outputs.SpoutWidthPx > 0 ? Outputs.SpoutWidthPx : 1280U,
                Outputs.SpoutHeightPx > 0 ? Outputs.SpoutHeightPx : 720U,
                Outputs.SpoutFps > 0 ? Outputs.SpoutFps : 60U,
                string.IsNullOrWhiteSpace(Outputs.SpoutChannelName) ? "Animiq" : Outputs.SpoutChannelName);
        }
        else if (!spoutOn && Outputs.SpoutActive)
        {
            _ = StopSpout();
        }

        if (oscOn && !Outputs.OscActive)
        {
            _ = StartOsc(Outputs.OscBindPort, Outputs.OscPublishAddress);
        }
        else if (!oscOn && Outputs.OscActive)
        {
            _ = StopOsc();
        }
    }

    private void ExecuteActionSpoutReceiver(WorkflowActionRequest request)
    {
        var enabled = request.Params.TryGetValue("enabled", out var enabledRaw) && bool.TryParse(enabledRaw, out var en) && en;
        var channel = request.Params.TryGetValue("channel", out var ch) ? ch?.Trim() ?? "VTubeStudio" : "VTubeStudio";
        var autoReconnect = request.Params.TryGetValue("auto_reconnect", out var autoRaw) && bool.TryParse(autoRaw, out var auto) && auto;
        SetSpoutReceiverAutoReconnect(autoReconnect);
        if (enabled)
        {
            _ = StartSpoutReceiver(channel);
        }
        else
        {
            _ = StopSpoutReceiver();
        }
    }

    private void ExecuteActionSendOsc(WorkflowActionRequest request)
    {
        var address = request.Params.TryGetValue("address", out var addrRaw) ? addrRaw?.Trim() ?? "/animiq/event/default" : "/animiq/event/default";
        var value = request.Params.TryGetValue("value", out var valueRaw) ? valueRaw?.Trim() ?? "1" : "1";
        var destination = request.Params.TryGetValue("destination", out var destRaw) ? destRaw?.Trim() ?? Outputs.OscPublishAddress : Outputs.OscPublishAddress;
        if (!TryParseHostPort(destination, out var host, out var port))
        {
            AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "AutomationSendOsc", $"invalid destination={destination}", NcResultCode.InvalidArgument), true);
            return;
        }

        var payload = $"{address} {value}";
        var bytes = System.Text.Encoding.UTF8.GetBytes(payload);
        using var udp = new UdpClient();
        udp.Send(bytes, bytes.Length, host, port);
        AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "AutomationSendOsc", $"sent {address} to {host}:{port}", NcResultCode.Ok), false);
    }

    private void ExecuteActionSetExpressionBatch(WorkflowActionRequest request)
    {
        var payload = new List<NcExpressionWeight>();
        if (request.Params.TryGetValue("values_json", out var jsonRaw) && !string.IsNullOrWhiteSpace(jsonRaw))
        {
            try
            {
                var map = JsonSerializer.Deserialize<Dictionary<string, float>>(jsonRaw);
                if (map is not null)
                {
                    payload.AddRange(map.Select(static pair => new NcExpressionWeight
                    {
                        Name = pair.Key,
                        Weight = Math.Clamp(pair.Value, 0.0f, 1.0f),
                    }));
                }
            }
            catch
            {
                // Fallback to prefixed key parsing below.
            }
        }

        foreach (var pair in request.Params)
        {
            if (!pair.Key.StartsWith("expr_", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }
            var name = pair.Key[5..];
            if (string.IsNullOrWhiteSpace(name))
            {
                continue;
            }
            if (!float.TryParse(pair.Value, NumberStyles.Float, CultureInfo.InvariantCulture, out var value))
            {
                continue;
            }
            payload.Add(new NcExpressionWeight
            {
                Name = name,
                Weight = Math.Clamp(value, 0.0f, 1.0f),
            });
        }

        if (payload.Count == 0)
        {
            return;
        }

        var rc = NativeCoreInterop.nc_set_expression_weights(payload.ToArray(), (uint)payload.Count);
        TrackResult("AutomationSetExpressionBatch", rc);
    }

    private void ExecuteActionSpawnOverlayItem(WorkflowActionRequest request)
    {
        var text = request.Params.TryGetValue("text", out var textRaw) ? textRaw?.Trim() ?? string.Empty : string.Empty;
        var color = request.Params.TryGetValue("color", out var colorRaw) ? colorRaw?.Trim() ?? "#FFFFFFFF" : "#FFFFFFFF";
        var anchor = request.Params.TryGetValue("anchor", out var anchorRaw) ? anchorRaw?.Trim() ?? "top" : "top";
        var kind = request.Params.TryGetValue("kind", out var kindRaw) ? kindRaw?.Trim() ?? "text" : "text";
        var durationMs = request.Params.TryGetValue("duration_ms", out var durationRaw) && int.TryParse(durationRaw, out var duration)
            ? Math.Clamp(duration, 200, 20000)
            : 2500;
        var offsetX = request.Params.TryGetValue("offset_x", out var offsetXRaw) && double.TryParse(offsetXRaw, NumberStyles.Float, CultureInfo.InvariantCulture, out var ox)
            ? ox
            : 0.0;
        var offsetY = request.Params.TryGetValue("offset_y", out var offsetYRaw) && double.TryParse(offsetYRaw, NumberStyles.Float, CultureInfo.InvariantCulture, out var oy)
            ? oy
            : 0.0;

        lock (_overlaySync)
        {
            _overlayItemCommands.Enqueue(new OverlayItemCommand(kind, text, color, durationMs, anchor, offsetX, offsetY));
        }
    }

    private void ExecuteActionClearOverlayItems()
    {
        lock (_overlaySync)
        {
            _overlayItemCommands.Enqueue(new OverlayItemCommand("clear", string.Empty, "#00000000", 0, "top", 0.0, 0.0));
        }
    }

    private void ExecuteActionExtension(WorkflowActionRequest request)
    {
        var extensionId = request.Params.TryGetValue("extension_id", out var idRaw) ? idRaw?.Trim() ?? string.Empty : string.Empty;
        if (!_automationExtensions.TryExecute(extensionId, request.Params))
        {
            AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "AutomationExtension", $"extension not found: {extensionId}", NcResultCode.InvalidArgument), true);
        }
    }

    private void TryAutoReconnectSpoutReceiver()
    {
        if (!_spoutReceiverAutoReconnect)
        {
            return;
        }

        var now = DateTimeOffset.UtcNow;
        if ((now - _lastSpoutReceiverReconnectAttemptUtc).TotalSeconds < 3.0)
        {
            return;
        }
        _lastSpoutReceiverReconnectAttemptUtc = now;

        var diag = GetSpoutReceiverDiagnostics();
        if (diag.Active || string.IsNullOrWhiteSpace(_spoutReceiverChannel))
        {
            return;
        }

        _ = StartSpoutReceiver(_spoutReceiverChannel);
    }

    private static bool TryParseHostPort(string text, out string host, out int port)
    {
        host = "127.0.0.1";
        port = 0;
        if (string.IsNullOrWhiteSpace(text))
        {
            return false;
        }

        var parts = text.Split(':', StringSplitOptions.TrimEntries);
        if (parts.Length != 2 || string.IsNullOrWhiteSpace(parts[0]) || !int.TryParse(parts[1], out port))
        {
            return false;
        }

        if (port <= 0 || port > 65535)
        {
            return false;
        }

        host = parts[0];
        return true;
    }
}
