using System.Text;
using System.Text.Json;

namespace HostCore;

public enum HostErrorCategory
{
    Unknown = 0,
    Initialization = 1,
    InputValidation = 2,
    FileIo = 3,
    Runtime = 4,
    Output = 5,
    Toolchain = 6,
}

public sealed record UserFacingError(
    HostErrorCategory Category,
    string Title,
    string ActionHint,
    string TechnicalDetail);

public sealed record PreflightCheckResult(
    string Name,
    bool Passed,
    string Detail,
    string Remediation);

public sealed record PreflightSummary(
    DateTimeOffset TimestampUtc,
    IReadOnlyList<PreflightCheckResult> Checks)
{
    public bool Passed => Checks.All(c => c.Passed);
}

public sealed record SidecarSettings(
    string ParserMode,
    string SidecarPath,
    int TimeoutMs,
    bool StrictMode);

public sealed record SessionPersistenceModel(
    int Version,
    string AvatarPath,
    string SpoutChannelName,
    ushort OscBindPort,
    string OscPublishAddress,
    SidecarSettings Sidecar,
    string? LastProfileName,
    DateTimeOffset LastUpdatedUtc)
{
    public static SessionPersistenceModel CreateDefault() => new(
        Version: 1,
        AvatarPath: string.Empty,
        SpoutChannelName: "VsfClone",
        OscBindPort: 39539,
        OscPublishAddress: "127.0.0.1:39540",
        Sidecar: new SidecarSettings("sidecar", string.Empty, 15000, false),
        LastProfileName: "quality",
        LastUpdatedUtc: DateTimeOffset.UtcNow);
}

public sealed record FrameMetric(
    DateTimeOffset TimestampUtc,
    float FrameMs,
    uint RenderReadyAvatarCount,
    bool SpoutActive,
    bool OscActive);

public sealed record TelemetrySettings(
    bool OptIn,
    bool RedactSensitiveFields);

public sealed class SessionStateStore
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = true,
    };

    private readonly string _path;

    public SessionStateStore(string? path = null)
    {
        var root = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        _path = path ?? Path.Combine(root, "VsfCloneHost", "session_state.json");
    }

    public SessionPersistenceModel Load()
    {
        if (!File.Exists(_path))
        {
            return SessionPersistenceModel.CreateDefault();
        }

        try
        {
            var json = File.ReadAllText(_path, Encoding.UTF8);
            var model = JsonSerializer.Deserialize<SessionPersistenceModel>(json, JsonOptions);
            return model ?? SessionPersistenceModel.CreateDefault();
        }
        catch
        {
            return SessionPersistenceModel.CreateDefault();
        }
    }

    public void Save(SessionPersistenceModel model)
    {
        var directory = Path.GetDirectoryName(_path);
        if (!string.IsNullOrWhiteSpace(directory))
        {
            Directory.CreateDirectory(directory);
        }

        var json = JsonSerializer.Serialize(model, JsonOptions);
        File.WriteAllText(_path, json, Encoding.UTF8);
    }
}

public sealed class TelemetryService
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = true,
    };

    private readonly List<Dictionary<string, object?>> _events = new();
    private readonly object _sync = new();

    public TelemetrySettings Settings { get; private set; } = new(false, true);

    public void UpdateSettings(bool optIn, bool redactSensitiveFields)
    {
        Settings = new TelemetrySettings(optIn, redactSensitiveFields);
    }

    public void Track(string name, Dictionary<string, object?> payload)
    {
        if (!Settings.OptIn)
        {
            return;
        }

        var record = new Dictionary<string, object?>(StringComparer.OrdinalIgnoreCase)
        {
            ["name"] = name,
            ["timestamp_utc"] = DateTimeOffset.UtcNow.ToString("O"),
        };

        foreach (var kv in payload)
        {
            record[kv.Key] = Settings.RedactSensitiveFields ? Redact(kv.Key, kv.Value) : kv.Value;
        }

        lock (_sync)
        {
            _events.Add(record);
            if (_events.Count > 2000)
            {
                _events.RemoveRange(0, _events.Count - 2000);
            }
        }
    }

    public int Export(string path)
    {
        List<Dictionary<string, object?>> snapshot;
        lock (_sync)
        {
            snapshot = new List<Dictionary<string, object?>>(_events);
        }

        var directory = Path.GetDirectoryName(path);
        if (!string.IsNullOrWhiteSpace(directory))
        {
            Directory.CreateDirectory(directory);
        }

        var json = JsonSerializer.Serialize(snapshot, JsonOptions);
        File.WriteAllText(path, json, Encoding.UTF8);
        return snapshot.Count;
    }

    private static object? Redact(string key, object? value)
    {
        if (value is null)
        {
            return null;
        }

        var lowered = key.ToLowerInvariant();
        if (lowered.Contains("path", StringComparison.Ordinal) ||
            lowered.Contains("address", StringComparison.Ordinal) ||
            lowered.Contains("channel", StringComparison.Ordinal))
        {
            return "***redacted***";
        }

        return value;
    }
}

public static class HostContent
{
    public static string BuildQuickstartText()
    {
        return string.Join(Environment.NewLine, new[]
        {
            "Quickstart",
            "1) Run Preflight and resolve failed checks.",
            "2) Initialize session.",
            "3) Import avatar (.vrm/.vxavatar/.vxa2/.xav2/.vsfavatar).",
            "4) Verify render stats and choose profile (quality/performance/stability).",
            "5) Start outputs (Spout/OSC) and confirm status strip.",
            "",
            "Troubleshooting",
            "- Load fails: verify extension and file existence, then check runtime diagnostics.",
            "- Output mismatch: use output restart and collect diagnostics bundle.",
            "- WinUI blocked: use WPF track and inspect diagnostics manifest.",
        });
    }

    public static string BuildCompatibilityText()
    {
        return string.Join(Environment.NewLine, new[]
        {
            "Compatibility Matrix",
            "- .vrm: supported (runtime mesh/material slice, partial advanced features).",
            "- .vxavatar: supported (MVP parse path).",
            "- .vxa2: supported (manifest + TLV decode path).",
            "- .xav2: supported (vxa2-derived container path).",
            "- .vsfavatar: supported with sidecar-first policy + fallback modes.",
            "",
            "Fallback Policy",
            "- sidecar: fallback to in-house parser on sidecar failure.",
            "- sidecar-strict: no fallback, fail fast with diagnostics.",
            "- inhouse: bypass sidecar process entirely.",
        });
    }
}
