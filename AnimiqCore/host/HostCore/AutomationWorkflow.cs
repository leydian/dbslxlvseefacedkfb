using System.Collections.Concurrent;
using System.Globalization;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;

namespace HostCore;

public enum WorkflowNodeKind
{
    TimerTrigger = 0,
    StateTrigger = 1,
    OscTrigger = 2,
    SetExpressionAction = 3,
    SwapAvatarAction = 4,
    SetRenderProfileAction = 5,
    SetOutputStateAction = 6,
    SetSpout2ReceiverStateAction = 7,
    DelayAction = 8,
    CommandTrigger = 9,
    SendOscAction = 10,
    SetExpressionBatchAction = 11,
    SpawnOverlayItemAction = 12,
    ClearOverlayItemsAction = 13,
    ExtensionAction = 14,
}

public sealed class WorkflowNodeModel
{
    public string Id { get; set; } = Guid.NewGuid().ToString("N");
    public WorkflowNodeKind Kind { get; set; } = WorkflowNodeKind.TimerTrigger;
    public string Title { get; set; } = string.Empty;
    public float X { get; set; } = 80.0f;
    public float Y { get; set; } = 80.0f;
    public Dictionary<string, string> Params { get; set; } = new(StringComparer.OrdinalIgnoreCase);
}

public sealed class WorkflowEdgeModel
{
    public string SourceId { get; set; } = string.Empty;
    public string TargetId { get; set; } = string.Empty;
}

public sealed class WorkflowGraphModel
{
    public int Version { get; set; } = 1;
    public string Name { get; set; } = "Automation Graph";
    public bool Enabled { get; set; }
    public int OscListenPort { get; set; } = 49995;
    public List<WorkflowNodeModel> Nodes { get; set; } = new();
    public List<WorkflowEdgeModel> Edges { get; set; } = new();

    public static WorkflowGraphModel CreateDefault()
    {
        var timer = new WorkflowNodeModel
        {
            Id = "trigger_timer_1",
            Kind = WorkflowNodeKind.TimerTrigger,
            Title = "Timer 5s",
            X = 60,
            Y = 60,
            Params = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                ["interval_ms"] = "5000",
            },
        };
        var blink = new WorkflowNodeModel
        {
            Id = "action_blink_1",
            Kind = WorkflowNodeKind.SetExpressionAction,
            Title = "Blink 1.0",
            X = 340,
            Y = 60,
            Params = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                ["name"] = "blink",
                ["value"] = "1.0",
            },
        };
        var delay = new WorkflowNodeModel
        {
            Id = "action_delay_1",
            Kind = WorkflowNodeKind.DelayAction,
            Title = "Delay 200ms",
            X = 560,
            Y = 60,
            Params = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                ["delay_ms"] = "200",
            },
        };
        var neutral = new WorkflowNodeModel
        {
            Id = "action_blink_0",
            Kind = WorkflowNodeKind.SetExpressionAction,
            Title = "Blink 0.0",
            X = 780,
            Y = 60,
            Params = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                ["name"] = "blink",
                ["value"] = "0.0",
            },
        };
        return new WorkflowGraphModel
        {
            Name = "Default Timer Blink",
            Enabled = false,
            Nodes = new List<WorkflowNodeModel> { timer, blink, delay, neutral },
            Edges = new List<WorkflowEdgeModel>
            {
                new() { SourceId = timer.Id, TargetId = blink.Id },
                new() { SourceId = blink.Id, TargetId = delay.Id },
                new() { SourceId = delay.Id, TargetId = neutral.Id },
            },
        };
    }
}

public sealed record WorkflowOscEvent(string Address, float? FloatValue, string StringValue);
public sealed record WorkflowTickContext(
    bool AvatarLoaded,
    bool SpoutActive,
    bool OscActive,
    bool TrackingActive,
    DateTimeOffset UtcNow);

public enum WorkflowActionType
{
    None = 0,
    SetExpression = 1,
    SwapAvatar = 2,
    SetRenderProfile = 3,
    SetOutputState = 4,
    SetSpout2ReceiverState = 5,
    SendOsc = 6,
    SetExpressionBatch = 7,
    SpawnOverlayItem = 8,
    ClearOverlayItems = 9,
    Extension = 10,
}

public sealed class WorkflowActionRequest
{
    public WorkflowActionType ActionType { get; init; } = WorkflowActionType.None;
    public string NodeId { get; init; } = string.Empty;
    public Dictionary<string, string> Params { get; init; } = new(StringComparer.OrdinalIgnoreCase);
}

public sealed class WorkflowExecutionSnapshot
{
    public bool Enabled { get; init; }
    public int NodeCount { get; init; }
    public int EdgeCount { get; init; }
    public int PendingContinuationCount { get; init; }
    public string LastEvent { get; init; } = string.Empty;
    public string LastError { get; init; } = string.Empty;
    public DateTimeOffset LastTickUtc { get; init; } = DateTimeOffset.MinValue;
}

internal sealed class WorkflowStore
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        WriteIndented = true,
    };

    private readonly string _path;
    public WorkflowStore(string? path = null)
    {
        if (!string.IsNullOrWhiteSpace(path))
        {
            _path = path;
            return;
        }

        var root = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "Animiq");
        _path = Path.Combine(root, "workflow_graph.json");
    }

    public WorkflowGraphModel Load()
    {
        try
        {
            if (!File.Exists(_path))
            {
                return WorkflowGraphModel.CreateDefault();
            }

            var json = File.ReadAllText(_path, Encoding.UTF8);
            var model = JsonSerializer.Deserialize<WorkflowGraphModel>(json, JsonOptions);
            return model ?? WorkflowGraphModel.CreateDefault();
        }
        catch
        {
            return WorkflowGraphModel.CreateDefault();
        }
    }

    public void Save(WorkflowGraphModel model)
    {
        var dir = Path.GetDirectoryName(_path);
        if (!string.IsNullOrWhiteSpace(dir) && !Directory.Exists(dir))
        {
            Directory.CreateDirectory(dir);
        }
        var json = JsonSerializer.Serialize(model, JsonOptions);
        File.WriteAllText(_path, json, Encoding.UTF8);
    }
}

internal sealed class WorkflowOscListener : IDisposable
{
    private UdpClient? _udp;
    private int _port;
    private readonly ConcurrentQueue<WorkflowOscEvent> _queue = new();

    public bool IsActive => _udp is not null;
    public int Port => _port;

    public void EnsureStarted(int port)
    {
        if (port <= 0 || port > 65535)
        {
            return;
        }

        if (_udp is not null && _port == port)
        {
            return;
        }

        Stop();
        _udp = new UdpClient(AddressFamily.InterNetwork);
        _udp.Client.Bind(new IPEndPoint(IPAddress.Any, port));
        _udp.Client.Blocking = false;
        _port = port;
    }

    public void Stop()
    {
        try { _udp?.Close(); } catch { }
        _udp = null;
        _port = 0;
    }

    public void Poll()
    {
        if (_udp is null)
        {
            return;
        }

        while (_udp.Available > 0)
        {
            IPEndPoint any = new(IPAddress.Any, 0);
            var packet = _udp.Receive(ref any);
            if (TryParseOscMessage(packet, out var evt))
            {
                _queue.Enqueue(evt);
            }
            else if (TryParseAsciiMessage(packet, out evt))
            {
                _queue.Enqueue(evt);
            }
        }
    }

    public List<WorkflowOscEvent> Drain()
    {
        var outList = new List<WorkflowOscEvent>();
        while (_queue.TryDequeue(out var e))
        {
            outList.Add(e);
        }
        return outList;
    }

    private static bool TryParseAsciiMessage(byte[] data, out WorkflowOscEvent evt)
    {
        evt = new WorkflowOscEvent(string.Empty, null, string.Empty);
        if (data is null || data.Length == 0)
        {
            return false;
        }
        var text = Encoding.UTF8.GetString(data).Trim();
        if (!text.StartsWith('/'))
        {
            return false;
        }
        var tokens = text.Split(' ', 2, StringSplitOptions.RemoveEmptyEntries);
        if (tokens.Length == 1)
        {
            evt = new WorkflowOscEvent(tokens[0], null, string.Empty);
            return true;
        }
        if (float.TryParse(tokens[1], out var f))
        {
            evt = new WorkflowOscEvent(tokens[0], f, string.Empty);
            return true;
        }
        evt = new WorkflowOscEvent(tokens[0], null, tokens[1]);
        return true;
    }

    private static bool TryParseOscMessage(byte[] packet, out WorkflowOscEvent evt)
    {
        evt = new WorkflowOscEvent(string.Empty, null, string.Empty);
        if (packet.Length < 8)
        {
            return false;
        }

        var index = 0;
        if (!TryReadOscString(packet, ref index, out var address) || string.IsNullOrWhiteSpace(address) || !address.StartsWith('/'))
        {
            return false;
        }
        if (!TryReadOscString(packet, ref index, out var typeTag) || string.IsNullOrWhiteSpace(typeTag) || typeTag[0] != ',')
        {
            return false;
        }

        float? floatValue = null;
        string strValue = string.Empty;
        for (var i = 1; i < typeTag.Length; i++)
        {
            switch (typeTag[i])
            {
                case 'f':
                    if (!TryReadFloat(packet, ref index, out var f))
                    {
                        return false;
                    }
                    floatValue = f;
                    break;
                case 'i':
                    if (!TryReadInt32(packet, ref index, out var i32))
                    {
                        return false;
                    }
                    floatValue = i32;
                    break;
                case 's':
                    if (!TryReadOscString(packet, ref index, out var s))
                    {
                        return false;
                    }
                    strValue = s;
                    break;
                case 'T':
                    floatValue = 1.0f;
                    break;
                case 'F':
                    floatValue = 0.0f;
                    break;
                default:
                    return false;
            }
        }

        evt = new WorkflowOscEvent(address, floatValue, strValue);
        return true;
    }

    private static bool TryReadFloat(byte[] p, ref int index, out float value)
    {
        value = 0.0f;
        if (index + 4 > p.Length)
        {
            return false;
        }
        Span<byte> b = stackalloc byte[4];
        b[0] = p[index + 3];
        b[1] = p[index + 2];
        b[2] = p[index + 1];
        b[3] = p[index + 0];
        index += 4;
        value = BitConverter.ToSingle(b);
        return true;
    }

    private static bool TryReadInt32(byte[] p, ref int index, out int value)
    {
        value = 0;
        if (index + 4 > p.Length)
        {
            return false;
        }
        value = (p[index] << 24) | (p[index + 1] << 16) | (p[index + 2] << 8) | p[index + 3];
        index += 4;
        return true;
    }

    private static bool TryReadOscString(byte[] packet, ref int index, out string value)
    {
        value = string.Empty;
        if (index >= packet.Length)
        {
            return false;
        }

        var end = index;
        while (end < packet.Length && packet[end] != 0)
        {
            end++;
        }
        if (end >= packet.Length)
        {
            return false;
        }

        value = Encoding.UTF8.GetString(packet, index, end - index);
        end++;
        while ((end % 4) != 0 && end < packet.Length)
        {
            end++;
        }
        index = end;
        return true;
    }

    public void Dispose()
    {
        Stop();
    }
}

internal sealed class WorkflowEngine : IDisposable
{
    private readonly WorkflowOscListener _osc = new();
    private readonly ConcurrentQueue<string> _commandQueue = new();
    private readonly Dictionary<string, DateTimeOffset> _lastTimerFireUtc = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, bool> _lastStateValue = new(StringComparer.OrdinalIgnoreCase);
    private readonly List<(DateTimeOffset ResumeAtUtc, string NodeId)> _continuations = new();
    private readonly JsonSerializerOptions _json = new() { PropertyNameCaseInsensitive = true, WriteIndented = true };
    private WorkflowGraphModel _graph = WorkflowGraphModel.CreateDefault();
    private string _lastEvent = string.Empty;
    private string _lastError = string.Empty;
    private DateTimeOffset _lastTick = DateTimeOffset.MinValue;

    public WorkflowGraphModel Graph => _graph;

    public void SetGraph(WorkflowGraphModel graph)
    {
        _graph = graph ?? WorkflowGraphModel.CreateDefault();
        _lastTimerFireUtc.Clear();
        _lastStateValue.Clear();
        _continuations.Clear();
        _lastEvent = "graph_set";
        _lastError = string.Empty;
    }

    public void SetGraphFromJson(string json)
    {
        var model = JsonSerializer.Deserialize<WorkflowGraphModel>(json, _json);
        var resolved = model ?? WorkflowGraphModel.CreateDefault();
        ValidateGraph(resolved);
        SetGraph(resolved);
    }

    public string GetGraphAsJson() => JsonSerializer.Serialize(_graph, _json);

    public WorkflowExecutionSnapshot GetSnapshot() => new()
    {
        Enabled = _graph.Enabled,
        NodeCount = _graph.Nodes.Count,
        EdgeCount = _graph.Edges.Count,
        PendingContinuationCount = _continuations.Count,
        LastEvent = _lastEvent,
        LastError = _lastError,
        LastTickUtc = _lastTick,
    };

    public void EnqueueCommand(string command)
    {
        if (string.IsNullOrWhiteSpace(command))
        {
            return;
        }
        _commandQueue.Enqueue(command.Trim());
    }

    public void Tick(
        WorkflowTickContext ctx,
        Action<WorkflowActionRequest> actionCallback)
    {
        _lastTick = ctx.UtcNow;
        if (!_graph.Enabled)
        {
            return;
        }

        try
        {
            _osc.EnsureStarted(_graph.OscListenPort);
            _osc.Poll();
            var oscEvents = _osc.Drain();
            var commands = DrainCommands();
            ExecuteContinuations(ctx, actionCallback);

            foreach (var node in _graph.Nodes)
            {
                if (!IsTriggerNode(node.Kind))
                {
                    continue;
                }
                if (!ShouldFireTrigger(node, ctx, oscEvents, commands))
                {
                    continue;
                }
                _lastEvent = $"trigger:{node.Kind}:{node.Id}";
                ExecuteDownstream(node.Id, ctx, actionCallback);
            }
        }
        catch (Exception ex)
        {
            _lastError = ex.Message;
        }
    }

    private void ExecuteContinuations(WorkflowTickContext ctx, Action<WorkflowActionRequest> actionCallback)
    {
        if (_continuations.Count == 0)
        {
            return;
        }
        var ready = _continuations.Where(c => c.ResumeAtUtc <= ctx.UtcNow).ToList();
        if (ready.Count == 0)
        {
            return;
        }
        _continuations.RemoveAll(c => c.ResumeAtUtc <= ctx.UtcNow);
        foreach (var c in ready)
        {
            ExecuteNode(c.NodeId, ctx, actionCallback, new HashSet<string>(StringComparer.OrdinalIgnoreCase));
        }
    }

    private bool ShouldFireTrigger(
        WorkflowNodeModel node,
        WorkflowTickContext ctx,
        IReadOnlyList<WorkflowOscEvent> oscEvents,
        IReadOnlyList<string> commands)
    {
        switch (node.Kind)
        {
            case WorkflowNodeKind.TimerTrigger:
                var intervalMs = GetIntParam(node, "interval_ms", 1000);
                if (!_lastTimerFireUtc.TryGetValue(node.Id, out var last))
                {
                    _lastTimerFireUtc[node.Id] = ctx.UtcNow;
                    return true;
                }
                if ((ctx.UtcNow - last).TotalMilliseconds >= intervalMs)
                {
                    _lastTimerFireUtc[node.Id] = ctx.UtcNow;
                    return true;
                }
                return false;
            case WorkflowNodeKind.StateTrigger:
                var key = GetStringParam(node, "state", "tracking_active");
                var desired = GetBoolParam(node, "value", true);
                var current = GetStateValue(ctx, key);
                var previousKnown = _lastStateValue.TryGetValue(node.Id, out var previous);
                _lastStateValue[node.Id] = current;
                return !previousKnown || (previous != current && current == desired);
            case WorkflowNodeKind.OscTrigger:
                var address = GetStringParam(node, "address", "/animiq/event/default");
                return oscEvents.Any(e => MatchesOscTrigger(node, e, address));
            case WorkflowNodeKind.CommandTrigger:
                var command = GetStringParam(node, "command", "default");
                return commands.Any(c => MatchesCommand(command, c));
            default:
                return false;
        }
    }

    private static bool MatchesCommand(string pattern, string command)
    {
        var normalizedPattern = string.IsNullOrWhiteSpace(pattern) ? "default" : pattern.Trim();
        if (normalizedPattern.Contains('*') || normalizedPattern.Contains('?'))
        {
            return WildcardMatch(normalizedPattern, command);
        }
        return string.Equals(normalizedPattern, command, StringComparison.OrdinalIgnoreCase);
    }

    private static bool MatchesOscTrigger(WorkflowNodeModel node, WorkflowOscEvent evt, string addressPattern)
    {
        if (!MatchesOscAddress(addressPattern, evt.Address))
        {
            return false;
        }

        if (node.Params.TryGetValue("float_min", out var floatMinRaw) &&
            TryParseInvariantFloat(floatMinRaw, out var floatMin))
        {
            if (!evt.FloatValue.HasValue || evt.FloatValue.Value < floatMin)
            {
                return false;
            }
        }

        if (node.Params.TryGetValue("float_max", out var floatMaxRaw) &&
            TryParseInvariantFloat(floatMaxRaw, out var floatMax))
        {
            if (!evt.FloatValue.HasValue || evt.FloatValue.Value > floatMax)
            {
                return false;
            }
        }

        if (node.Params.TryGetValue("float_equals", out var floatEqualsRaw) &&
            TryParseInvariantFloat(floatEqualsRaw, out var floatEquals))
        {
            if (!evt.FloatValue.HasValue || Math.Abs(evt.FloatValue.Value - floatEquals) > 0.0001f)
            {
                return false;
            }
        }

        var ignoreCase = GetBoolParam(node, "string_ignore_case", true);
        var cmp = ignoreCase ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
        if (node.Params.TryGetValue("string_equals", out var stringEqualsRaw) &&
            !string.IsNullOrWhiteSpace(stringEqualsRaw))
        {
            if (!string.Equals(evt.StringValue, stringEqualsRaw.Trim(), cmp))
            {
                return false;
            }
        }

        if (node.Params.TryGetValue("string_contains", out var stringContainsRaw) &&
            !string.IsNullOrWhiteSpace(stringContainsRaw))
        {
            var needle = stringContainsRaw.Trim();
            if (string.IsNullOrWhiteSpace(evt.StringValue) || evt.StringValue.IndexOf(needle, cmp) < 0)
            {
                return false;
            }
        }

        return true;
    }

    private static bool MatchesOscAddress(string pattern, string address)
    {
        var normalizedPattern = string.IsNullOrWhiteSpace(pattern) ? "/animiq/event/default" : pattern.Trim();
        if (normalizedPattern.Contains('*') || normalizedPattern.Contains('?'))
        {
            return WildcardMatch(normalizedPattern, address);
        }
        return string.Equals(address, normalizedPattern, StringComparison.OrdinalIgnoreCase);
    }

    private static bool WildcardMatch(string pattern, string input)
    {
        if (string.IsNullOrEmpty(pattern))
        {
            return string.IsNullOrEmpty(input);
        }

        var p = 0;
        var s = 0;
        var star = -1;
        var match = 0;

        while (s < input.Length)
        {
            if (p < pattern.Length &&
                (pattern[p] == '?' || char.ToLowerInvariant(pattern[p]) == char.ToLowerInvariant(input[s])))
            {
                p++;
                s++;
                continue;
            }

            if (p < pattern.Length && pattern[p] == '*')
            {
                star = p++;
                match = s;
                continue;
            }

            if (star >= 0)
            {
                p = star + 1;
                s = ++match;
                continue;
            }

            return false;
        }

        while (p < pattern.Length && pattern[p] == '*')
        {
            p++;
        }

        return p == pattern.Length;
    }

    private static bool TryParseInvariantFloat(string? raw, out float value)
    {
        return float.TryParse(raw, NumberStyles.Float, CultureInfo.InvariantCulture, out value);
    }

    private static bool GetStateValue(WorkflowTickContext ctx, string key)
    {
        return key.Trim().ToLowerInvariant() switch
        {
            "avatar_loaded" => ctx.AvatarLoaded,
            "spout_active" => ctx.SpoutActive,
            "osc_active" => ctx.OscActive,
            "output_started" => ctx.SpoutActive || ctx.OscActive,
            _ => ctx.TrackingActive,
        };
    }

    private void ExecuteDownstream(string sourceNodeId, WorkflowTickContext ctx, Action<WorkflowActionRequest> actionCallback)
    {
        foreach (var edge in _graph.Edges.Where(e => string.Equals(e.SourceId, sourceNodeId, StringComparison.OrdinalIgnoreCase)))
        {
            ExecuteNode(edge.TargetId, ctx, actionCallback, new HashSet<string>(StringComparer.OrdinalIgnoreCase));
        }
    }

    private void ExecuteNode(string nodeId, WorkflowTickContext ctx, Action<WorkflowActionRequest> actionCallback, HashSet<string> visited)
    {
        if (!visited.Add(nodeId))
        {
            return;
        }
        var node = _graph.Nodes.FirstOrDefault(n => string.Equals(n.Id, nodeId, StringComparison.OrdinalIgnoreCase));
        if (node is null)
        {
            return;
        }

        if (IsActionNode(node.Kind))
        {
            if (node.Kind == WorkflowNodeKind.DelayAction)
            {
                var delayMs = GetIntParam(node, "delay_ms", 200);
                foreach (var edge in _graph.Edges.Where(e => string.Equals(e.SourceId, node.Id, StringComparison.OrdinalIgnoreCase)))
                {
                    _continuations.Add((ctx.UtcNow.AddMilliseconds(delayMs), edge.TargetId));
                }
                return;
            }

            var req = new WorkflowActionRequest
            {
                ActionType = ToActionType(node.Kind),
                NodeId = node.Id,
                Params = new Dictionary<string, string>(node.Params, StringComparer.OrdinalIgnoreCase),
            };
            actionCallback(req);
        }

        foreach (var edge in _graph.Edges.Where(e => string.Equals(e.SourceId, node.Id, StringComparison.OrdinalIgnoreCase)))
        {
            ExecuteNode(edge.TargetId, ctx, actionCallback, visited);
        }
    }

    private static WorkflowActionType ToActionType(WorkflowNodeKind kind)
    {
        return kind switch
        {
            WorkflowNodeKind.SetExpressionAction => WorkflowActionType.SetExpression,
            WorkflowNodeKind.SwapAvatarAction => WorkflowActionType.SwapAvatar,
            WorkflowNodeKind.SetRenderProfileAction => WorkflowActionType.SetRenderProfile,
            WorkflowNodeKind.SetOutputStateAction => WorkflowActionType.SetOutputState,
            WorkflowNodeKind.SetSpout2ReceiverStateAction => WorkflowActionType.SetSpout2ReceiverState,
            WorkflowNodeKind.SendOscAction => WorkflowActionType.SendOsc,
            WorkflowNodeKind.SetExpressionBatchAction => WorkflowActionType.SetExpressionBatch,
            WorkflowNodeKind.SpawnOverlayItemAction => WorkflowActionType.SpawnOverlayItem,
            WorkflowNodeKind.ClearOverlayItemsAction => WorkflowActionType.ClearOverlayItems,
            WorkflowNodeKind.ExtensionAction => WorkflowActionType.Extension,
            _ => WorkflowActionType.None,
        };
    }

    private static bool IsTriggerNode(WorkflowNodeKind kind) =>
        kind is WorkflowNodeKind.TimerTrigger
            or WorkflowNodeKind.StateTrigger
            or WorkflowNodeKind.OscTrigger
            or WorkflowNodeKind.CommandTrigger;

    private static bool IsActionNode(WorkflowNodeKind kind) =>
        kind is WorkflowNodeKind.SetExpressionAction
            or WorkflowNodeKind.SwapAvatarAction
            or WorkflowNodeKind.SetRenderProfileAction
            or WorkflowNodeKind.SetOutputStateAction
            or WorkflowNodeKind.SetSpout2ReceiverStateAction
            or WorkflowNodeKind.DelayAction
            or WorkflowNodeKind.SendOscAction
            or WorkflowNodeKind.SetExpressionBatchAction
            or WorkflowNodeKind.SpawnOverlayItemAction
            or WorkflowNodeKind.ClearOverlayItemsAction
            or WorkflowNodeKind.ExtensionAction;

    private static int GetIntParam(WorkflowNodeModel node, string key, int fallback)
    {
        if (node.Params.TryGetValue(key, out var raw) && int.TryParse(raw, out var parsed))
        {
            return parsed;
        }
        return fallback;
    }

    private static string GetStringParam(WorkflowNodeModel node, string key, string fallback)
    {
        if (node.Params.TryGetValue(key, out var raw) && !string.IsNullOrWhiteSpace(raw))
        {
            return raw.Trim();
        }
        return fallback;
    }

    private static bool GetBoolParam(WorkflowNodeModel node, string key, bool fallback)
    {
        if (node.Params.TryGetValue(key, out var raw) && bool.TryParse(raw, out var b))
        {
            return b;
        }
        return fallback;
    }

    public void Dispose()
    {
        _osc.Dispose();
    }

    private List<string> DrainCommands()
    {
        var outList = new List<string>();
        while (_commandQueue.TryDequeue(out var command))
        {
            outList.Add(command);
        }
        return outList;
    }

    private static void ValidateGraph(WorkflowGraphModel graph)
    {
        if (graph.Nodes is null || graph.Edges is null)
        {
            throw new InvalidDataException("workflow graph nodes/edges are required");
        }

        var ids = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var node in graph.Nodes)
        {
            if (node is null || string.IsNullOrWhiteSpace(node.Id))
            {
                throw new InvalidDataException("workflow node id is required");
            }
            if (!ids.Add(node.Id))
            {
                throw new InvalidDataException($"duplicate workflow node id: {node.Id}");
            }
        }

        foreach (var edge in graph.Edges)
        {
            if (edge is null || string.IsNullOrWhiteSpace(edge.SourceId) || string.IsNullOrWhiteSpace(edge.TargetId))
            {
                throw new InvalidDataException("workflow edge source/target is required");
            }
            if (!ids.Contains(edge.SourceId) || !ids.Contains(edge.TargetId))
            {
                throw new InvalidDataException($"workflow edge references missing node: {edge.SourceId}->{edge.TargetId}");
            }
        }
    }
}
