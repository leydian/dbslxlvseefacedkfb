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
    string ErrorCode,
    HostErrorCategory Category,
    string Title,
    string ActionHint,
    string TechnicalDetail);

public sealed record PreflightCheckResult(
    string CheckCode,
    string Name,
    bool Passed,
    string Detail,
    string Remediation);

public sealed record ImportPlan(
    string Route,
    bool IsSupported,
    string Guidance,
    string Fallback);

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

public sealed record TrackingInputSettings(
    ushort ListenPort,
    int StaleTimeoutMs,
    bool LastActive,
    TrackingSourceType SourceType,
    string WebcamDeviceId,
    string OnnxModelPath,
    int InferenceFpsCap);

public sealed record SessionPersistenceModel(
    int Version,
    string AvatarPath,
    string SpoutChannelName,
    ushort OscBindPort,
    string OscPublishAddress,
    SidecarSettings Sidecar,
    TrackingInputSettings Tracking,
    string? LastProfileName,
    string UiMode,
    DateTimeOffset LastUpdatedUtc)
{
    public static SessionPersistenceModel CreateDefault() => new(
        Version: 4,
        AvatarPath: string.Empty,
        SpoutChannelName: "VsfClone",
        OscBindPort: 39539,
        OscPublishAddress: "127.0.0.1:39540",
        Sidecar: new SidecarSettings("sidecar", string.Empty, 15000, false),
        Tracking: new TrackingInputSettings(49983, 500, false, TrackingSourceType.OscIfacial, string.Empty, string.Empty, 30),
        LastProfileName: "quality",
        UiMode: "beginner",
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

public sealed record LoadProgressState(
    string OperationId,
    string Stage,
    int Percent,
    string Message,
    bool IsTerminal);

public sealed record AutoQualityPolicy(
    float HighFrameMsThreshold,
    int ConsecutiveFrameLimit,
    int CooldownSeconds,
    float RecoveryFrameMsThreshold,
    int RecoveryConsecutiveFrameLimit)
{
    public static AutoQualityPolicy CreateDefault() => new(
        HighFrameMsThreshold: 28.0f,
        ConsecutiveFrameLimit: 120,
        CooldownSeconds: 30,
        RecoveryFrameMsThreshold: 22.0f,
        RecoveryConsecutiveFrameLimit: 240);
}

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
            if (model is not null)
            {
                return Normalize(model);
            }
        }
        catch
        {
            // Try v1 fallback shape below.
        }

        try
        {
            var json = File.ReadAllText(_path, Encoding.UTF8);
            var legacy = JsonSerializer.Deserialize<SessionPersistenceModelV1>(json, JsonOptions);
            if (legacy is not null)
            {
                return Normalize(new SessionPersistenceModel(
                    Version: 4,
                    AvatarPath: legacy.AvatarPath,
                    SpoutChannelName: legacy.SpoutChannelName,
                    OscBindPort: legacy.OscBindPort,
                    OscPublishAddress: legacy.OscPublishAddress,
                    Sidecar: legacy.Sidecar,
                    Tracking: new TrackingInputSettings(49983, 500, false, TrackingSourceType.OscIfacial, string.Empty, string.Empty, 30),
                    LastProfileName: legacy.LastProfileName,
                    UiMode: "beginner",
                    LastUpdatedUtc: legacy.LastUpdatedUtc));
            }
        }
        catch
        {
            // Fall through to defaults.
        }

        return SessionPersistenceModel.CreateDefault();
    }

    public void Save(SessionPersistenceModel model)
    {
        var directory = Path.GetDirectoryName(_path);
        if (!string.IsNullOrWhiteSpace(directory))
        {
            Directory.CreateDirectory(directory);
        }

        var json = JsonSerializer.Serialize(Normalize(model), JsonOptions);
        File.WriteAllText(_path, json, Encoding.UTF8);
    }

    private static SessionPersistenceModel Normalize(SessionPersistenceModel model)
    {
        var mode = NormalizeUiMode(model.UiMode);
        var lastUpdated = model.LastUpdatedUtc == default ? DateTimeOffset.UtcNow : model.LastUpdatedUtc;
        return model with
        {
            Version = Math.Max(4, model.Version),
            Tracking = NormalizeTracking(model.Tracking),
            UiMode = mode,
            LastUpdatedUtc = lastUpdated,
        };
    }

    private static TrackingInputSettings NormalizeTracking(TrackingInputSettings? value)
    {
        if (value is null)
        {
            return new TrackingInputSettings(49983, 500, false, TrackingSourceType.OscIfacial, string.Empty, string.Empty, 30);
        }

        var port = value.ListenPort == 0 ? (ushort)49983 : value.ListenPort;
        var stale = Math.Clamp(value.StaleTimeoutMs <= 0 ? 500 : value.StaleTimeoutMs, 50, 5000);
        var sourceType = Enum.IsDefined(typeof(TrackingSourceType), value.SourceType)
            ? value.SourceType
            : TrackingSourceType.OscIfacial;
        var fpsCap = Math.Clamp(value.InferenceFpsCap <= 0 ? 30 : value.InferenceFpsCap, 5, 120);
        return new TrackingInputSettings(
            port,
            stale,
            value.LastActive,
            sourceType,
            value.WebcamDeviceId ?? string.Empty,
            value.OnnxModelPath ?? string.Empty,
            fpsCap);
    }

    private static string NormalizeUiMode(string value)
    {
        return string.Equals(value?.Trim(), "advanced", StringComparison.OrdinalIgnoreCase)
            ? "advanced"
            : "beginner";
    }

    private sealed record SessionPersistenceModelV1(
        int Version,
        string AvatarPath,
        string SpoutChannelName,
        ushort OscBindPort,
        string OscPublishAddress,
        SidecarSettings Sidecar,
        string? LastProfileName,
        DateTimeOffset LastUpdatedUtc);
}

public sealed class AutoQualityPolicyStore
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = true,
    };

    private readonly string _path;

    public AutoQualityPolicyStore(string? path = null)
    {
        var root = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        _path = path ?? Path.Combine(root, "VsfCloneHost", "auto_quality_policy.json");
    }

    public AutoQualityPolicy Load()
    {
        if (!File.Exists(_path))
        {
            return AutoQualityPolicy.CreateDefault();
        }

        try
        {
            var json = File.ReadAllText(_path, Encoding.UTF8);
            var model = JsonSerializer.Deserialize<AutoQualityPolicy>(json, JsonOptions);
            if (model is null)
            {
                return AutoQualityPolicy.CreateDefault();
            }

            return Normalize(model);
        }
        catch
        {
            return AutoQualityPolicy.CreateDefault();
        }
    }

    public void Save(AutoQualityPolicy policy)
    {
        var normalized = Normalize(policy);
        var directory = Path.GetDirectoryName(_path);
        if (!string.IsNullOrWhiteSpace(directory))
        {
            Directory.CreateDirectory(directory);
        }

        var json = JsonSerializer.Serialize(normalized, JsonOptions);
        File.WriteAllText(_path, json, Encoding.UTF8);
    }

    private static AutoQualityPolicy Normalize(AutoQualityPolicy value)
    {
        var frame = Math.Clamp(value.HighFrameMsThreshold, 10.0f, 80.0f);
        var count = Math.Clamp(value.ConsecutiveFrameLimit, 10, 1200);
        var cooldown = Math.Clamp(value.CooldownSeconds, 5, 300);
        var recoveryFrame = Math.Clamp(value.RecoveryFrameMsThreshold, 8.0f, frame);
        var recoveryCount = Math.Clamp(value.RecoveryConsecutiveFrameLimit, 10, 2400);
        return new AutoQualityPolicy(frame, count, cooldown, recoveryFrame, recoveryCount);
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
            "빠른 시작 (Quickstart)",
            "1) 사전 점검(Preflight)을 실행하고 실패 항목을 먼저 해결하세요.",
            "2) 세션 초기화(Initialize) 버튼을 누르세요.",
            "3) 아바타 불러오기(Load Avatar): .vrm / .xav2 / .vsfavatar",
            "4) 화면 프레임 상태를 확인하고 프로필을 선택하세요: 품질(quality) / 성능(performance) / 안정(stability)",
            "5) 출력 시작(Start Outputs): Spout/OSC 상태가 켜졌는지 확인하세요.",
            "",
            "문제 해결 (Troubleshooting)",
            "- 로드 실패(Load failed): 파일 경로/확장자를 확인하고 진단(Runtime/Avatar)을 확인하세요.",
            "- 출력 불일치(Output mismatch): 출력을 다시 시작하고 진단 번들(Export Diagnostics)을 수집하세요.",
            "- WinUI 차단(WinUI blocked): WPF 트랙을 사용하고 진단 매니페스트를 확인하세요.",
        });
    }

    public static string BuildCompatibilityText()
    {
        return string.Join(Environment.NewLine, new[]
        {
            "호환성 매트릭스 (Compatibility Matrix)",
            "- .vrm: 지원 (기본 메쉬/재질 경로, 일부 고급 기능은 제한될 수 있음)",
            "- .xav2: 지원 (vxa2 기반 컨테이너 경로)",
            "- .vsfavatar: 지원 (sidecar 우선 + fallback 정책)",
            "",
            "폴백 정책 (Fallback Policy)",
            "- sidecar: sidecar 실패 시 in-house 파서로 자동 전환",
            "- sidecar-strict: 폴백 없이 즉시 실패하고 진단 정보 제공",
            "- inhouse: sidecar를 사용하지 않고 내부 파서만 사용",
        });
    }
}
