using System.Globalization;
using System.Text.Json;

namespace HostCore;

public sealed partial class HostController
{
    private readonly WorkflowStore _workflowStore = new();
    private readonly WorkflowEngine _workflowEngine = new();
    private bool _workflowLoaded;
    private bool _spoutReceiverActive;
    private string _spoutReceiverChannel = string.Empty;

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
        _ = LoadAvatar(path);
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
        if (enabled)
        {
            _ = StartSpoutReceiver(channel);
        }
        else
        {
            _ = StopSpoutReceiver();
        }
    }
}
