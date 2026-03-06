using System.Diagnostics;
using System.IO.Compression;
using System.Runtime.InteropServices;
using System.Globalization;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Windows.Devices.Enumeration;

namespace HostCore;

public sealed partial class HostController
{
    private const int MaxRecentAvatarEntries = 12;
    private static readonly JsonSerializerOptions FeatureJsonOptions = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = true,
    };

    private readonly SessionStateStore _sessionStore = new();
    private readonly AutoQualityPolicyStore _autoQualityStore = new();
    private readonly TelemetryService _telemetry = new();
    private readonly Queue<FrameMetric> _rollingMetrics = new();
    private readonly string _metricsSessionId = Guid.NewGuid().ToString("N");
    private const string MeasurementSourceLiveTick = "live_tick";
    private const int RollingMetricCapacity = 1800;
    private DateTimeOffset _lastAutoQualityAdjustUtc = DateTimeOffset.MinValue;
    private int _highFrameCount;
    private int _recoveryFrameCount;
    private bool _autoQualityDowngraded;
    private DateTimeOffset _lastProcessMemorySampleUtc = DateTimeOffset.MinValue;
    private float _lastWorkingSetMb;
    private float _lastPrivateMb;
    private string _lastMemorySampleStatus = "none";
    private CancellationTokenSource? _loadCancellation;
    private Task<NcResultCode>? _activeLoadTask;
    private Task<NcResultCode>? _activeLoadWorkerTask;
    private string _activeLoadOperationId = string.Empty;
    private long _loadOperationSequence;
    private SessionPersistenceModel _sessionPersistence = SessionPersistenceModel.CreateDefault();
    private AutoQualityPolicy _autoQualityPolicy = AutoQualityPolicy.CreateDefault();
    private DateTimeOffset? _sessionStartedAtUtc;
    private DateTimeOffset? _initializedAtUtc;
    private DateTimeOffset? _avatarLoadedAtUtc;
    private DateTimeOffset? _outputStartedAtUtc;
    private bool _within3MinSuccess;

    public PreflightSummary? LastPreflight { get; private set; }
    public UserFacingError? LastUserFacingError { get; private set; }
    public event EventHandler<LoadProgressState>? LoadProgressChanged;

    public SessionPersistenceModel SessionPersistence => _sessionPersistence;

    public string GetUiMode() => _sessionPersistence.UiMode;

    public (string activeSection, string themeMode, bool diagnosticsPinned) GetUiWorkspaceState()
        => (_sessionPersistence.UiActiveSection, _sessionPersistence.UiThemeMode, _sessionPersistence.UiDiagnosticsPinned);

    public void SetUiMode(string uiMode)
    {
        var normalized = string.Equals(uiMode?.Trim(), "advanced", StringComparison.OrdinalIgnoreCase)
            ? "advanced"
            : "beginner";
        if (string.Equals(_sessionPersistence.UiMode, normalized, StringComparison.Ordinal))
        {
            return;
        }

        _sessionPersistence = _sessionPersistence with
        {
            UiMode = normalized,
            LastUpdatedUtc = DateTimeOffset.UtcNow,
        };
        PersistSessionSnapshot();
        AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "UiMode", $"mode={normalized}", NcResultCode.Ok), false);
    }

    public void SetUiWorkspaceState(string activeSection, string themeMode, bool diagnosticsPinned)
    {
        var normalizedSection = NormalizeUiSection(activeSection);
        var normalizedTheme = NormalizeThemeMode(themeMode);
        if (string.Equals(_sessionPersistence.UiActiveSection, normalizedSection, StringComparison.Ordinal) &&
            string.Equals(_sessionPersistence.UiThemeMode, normalizedTheme, StringComparison.Ordinal) &&
            _sessionPersistence.UiDiagnosticsPinned == diagnosticsPinned)
        {
            return;
        }

        _sessionPersistence = _sessionPersistence with
        {
            UiActiveSection = normalizedSection,
            UiThemeMode = normalizedTheme,
            UiDiagnosticsPinned = diagnosticsPinned,
            LastUpdatedUtc = DateTimeOffset.UtcNow,
        };
        PersistSessionSnapshot();
    }

    public void SetUiTrackingIpv4HintVisible(bool visible)
    {
        if (_sessionPersistence.UiShowTrackingIpv4Hint == visible)
        {
            return;
        }

        _sessionPersistence = _sessionPersistence with
        {
            UiShowTrackingIpv4Hint = visible,
            LastUpdatedUtc = DateTimeOffset.UtcNow,
        };
        PersistSessionSnapshot();
    }

    public (string recommendedIpv4, string allIpv4) GetLocalIpv4Hint()
    {
        var candidates = EnumerateLocalIpv4Candidates();
        if (candidates.Count == 0)
        {
            return (string.Empty, string.Empty);
        }

        return (candidates[0], string.Join(", ", candidates));
    }

    public void InitializeMvpFeatures()
    {
        _sessionStartedAtUtc ??= DateTimeOffset.UtcNow;
        _sessionPersistence = _sessionStore.Load();
        _autoQualityPolicy = _autoQualityStore.Load();
        Outputs = Outputs with
        {
            SpoutChannelName = string.IsNullOrWhiteSpace(_sessionPersistence.SpoutChannelName) ? Outputs.SpoutChannelName : _sessionPersistence.SpoutChannelName,
            OscBindPort = _sessionPersistence.OscBindPort,
            OscPublishAddress = string.IsNullOrWhiteSpace(_sessionPersistence.OscPublishAddress) ? Outputs.OscPublishAddress : _sessionPersistence.OscPublishAddress,
        };

        ApplySidecarEnvironment(_sessionPersistence.Sidecar);
        if (!string.IsNullOrWhiteSpace(_sessionPersistence.LastProfileName))
        {
            _ = ApplyRenderProfile(_sessionPersistence.LastProfileName);
        }

        TrackOnboardingMilestone("session_started");
    }

    public ImportPlan BuildImportPlan(string path)
    {
        var trimmed = path?.Trim() ?? string.Empty;
        if (string.IsNullOrWhiteSpace(trimmed))
        {
            return new ImportPlan("none", false, "먼저 아바타 파일 경로를 선택하세요.", "경로가 없으면 폴백(Fallback)을 적용할 수 없습니다.");
        }
        if (!File.Exists(trimmed))
        {
            return new ImportPlan("missing-file", false, "선택한 아바타 경로가 존재하지 않습니다. 경로를 확인하세요.", "파일 접근 권한/경로를 확인한 뒤 다시 시도하세요.");
        }

        var ext = Path.GetExtension(trimmed).ToLowerInvariant();
        return ext switch
        {
            ".vrm" => new ImportPlan("vrm", true, "VRM 경로가 선택되었습니다. 기본 런타임 렌더 경로를 사용합니다.", "호환성 경고가 반복되면 XAV2로 변환해 다시 시도하세요."),
            ".xav2" => new ImportPlan("xav2", true, "XAV2 경로가 선택되었습니다. vxa2 기반 컨테이너를 사용합니다.", "불러오기가 막히면 VRM 경로로 재시도하세요."),
            ".vsfavatar" => new ImportPlan("vsfavatar", true, $"VSFAvatar 경로가 선택되었습니다. parser_mode={_sessionPersistence.Sidecar.ParserMode}", "폴백은 parser 정책(sidecar/inhouse/sidecar-strict)을 따릅니다."),
            _ => new ImportPlan("unsupported-extension", false, "지원하지 않는 확장자입니다. 지원 형식: .vrm, .vsfavatar, .xav2", "지원 형식으로 변환한 뒤 다시 시도하세요."),
        };
    }

    public string BuildImportGuidance(string path)
    {
        var plan = BuildImportPlan(path);
        return $"{plan.Guidance} Fallback: {plan.Fallback}";
    }

    public IReadOnlyList<RecentAvatarEntry> GetRecentAvatars()
    {
        return _sessionPersistence.RecentAvatars;
    }

    public void RecordAvatarSelection(string path)
    {
        var normalizedPath = path?.Trim() ?? string.Empty;
        if (string.IsNullOrWhiteSpace(normalizedPath))
        {
            return;
        }

        var recent = UpsertRecentAvatar(
            _sessionPersistence.RecentAvatars,
            normalizedPath,
            thumbnailStatus: null,
            thumbnailPath: null,
            lastError: null);
        _sessionPersistence = _sessionPersistence with
        {
            AvatarPath = normalizedPath,
            RecentAvatars = recent,
            LastUpdatedUtc = DateTimeOffset.UtcNow,
        };
        PersistSessionSnapshot();
    }

    public void UpdateRecentAvatarThumbnail(string avatarPath, string thumbnailPath, string status, string lastError)
    {
        var normalizedPath = avatarPath?.Trim() ?? string.Empty;
        if (string.IsNullOrWhiteSpace(normalizedPath))
        {
            return;
        }

        var normalizedStatus = NormalizeRecentThumbnailStatus(status);
        var recent = UpsertRecentAvatar(
            _sessionPersistence.RecentAvatars,
            normalizedPath,
            thumbnailStatus: normalizedStatus,
            thumbnailPath: thumbnailPath?.Trim() ?? string.Empty,
            lastError: lastError?.Trim() ?? string.Empty);
        _sessionPersistence = _sessionPersistence with
        {
            RecentAvatars = recent,
            LastUpdatedUtc = DateTimeOffset.UtcNow,
        };
        PersistSessionSnapshot();
    }

    private static IReadOnlyList<RecentAvatarEntry> UpsertRecentAvatar(
        IReadOnlyList<RecentAvatarEntry> current,
        string avatarPath,
        string? thumbnailStatus,
        string? thumbnailPath,
        string? lastError)
    {
        var existing = current.FirstOrDefault(item =>
            string.Equals(item.AvatarPath, avatarPath, StringComparison.OrdinalIgnoreCase));
        var entry = new RecentAvatarEntry(
            AvatarPath: avatarPath,
            DisplayName: string.IsNullOrWhiteSpace(existing?.DisplayName)
                ? Path.GetFileNameWithoutExtension(avatarPath)
                : existing!.DisplayName,
            ThumbnailPath: thumbnailPath ?? existing?.ThumbnailPath ?? string.Empty,
            ThumbnailStatus: thumbnailStatus ?? existing?.ThumbnailStatus ?? "none",
            LastUsedUtc: DateTimeOffset.UtcNow,
            LastError: lastError ?? existing?.LastError ?? string.Empty);

        var ordered = new List<RecentAvatarEntry>(MaxRecentAvatarEntries) { entry };
        foreach (var item in current)
        {
            if (ordered.Count >= MaxRecentAvatarEntries)
            {
                break;
            }
            if (string.Equals(item.AvatarPath, avatarPath, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }
            if (!File.Exists(item.AvatarPath))
            {
                continue;
            }
            ordered.Add(item);
        }

        return ordered;
    }

    private static string NormalizeRecentThumbnailStatus(string? value)
    {
        return (value?.Trim().ToLowerInvariant()) switch
        {
            "ready" => "ready",
            "pending" => "pending",
            "failed" => "failed",
            _ => "none",
        };
    }

    public PreflightSummary RunPreflight()
    {
        var checks = new List<PreflightCheckResult>();

        var localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        checks.Add(new PreflightCheckResult(
            "LOCAL_APPDATA_PRESENT",
            "LOCAL_APPDATA",
            !string.IsNullOrWhiteSpace(localAppData),
            string.IsNullOrWhiteSpace(localAppData) ? "LocalAppData path unavailable." : localAppData,
            "Ensure user profile has a writable LocalAppData folder."));

        var localAppDataWritable = false;
        if (!string.IsNullOrWhiteSpace(localAppData))
        {
            try
            {
                Directory.CreateDirectory(localAppData);
                var probeFile = Path.Combine(localAppData, $"vsfclone_preflight_{Guid.NewGuid():N}.tmp");
                File.WriteAllText(probeFile, "ok", Encoding.UTF8);
                File.Delete(probeFile);
                localAppDataWritable = true;
            }
            catch
            {
                localAppDataWritable = false;
            }
        }
        checks.Add(new PreflightCheckResult(
            "LOCAL_APPDATA_WRITABLE",
            "LOCAL_APPDATA_WRITABLE",
            localAppDataWritable,
            localAppDataWritable ? "Verified write access to LocalAppData." : "Failed to create temporary file in LocalAppData.",
            "Grant write permission to LocalAppData and re-run preflight."));

        var sdk8Detected = DetectDotnet8Sdk();
        checks.Add(new PreflightCheckResult(
            "DOTNET_8_SDK",
            "DOTNET_8_SDK",
            sdk8Detected,
            sdk8Detected ? "Detected .NET 8 SDK." : "Could not detect .NET 8 SDK via dotnet --list-sdks.",
            "Install .NET 8 SDK (for host tooling and publish flows)."));

        var nativeDllCandidate = Path.Combine(AppContext.BaseDirectory, "nativecore.dll");
        checks.Add(new PreflightCheckResult(
            "NATIVECORE_DLL",
            "NATIVECORE_DLL",
            File.Exists(nativeDllCandidate),
            File.Exists(nativeDllCandidate) ? nativeDllCandidate : "nativecore.dll not found near host executable.",
            "Build nativecore and place/copy nativecore.dll next to host executable."));

        var sidecarMode = _sessionPersistence.Sidecar.ParserMode;
        checks.Add(new PreflightCheckResult(
            "VSF_PARSER_MODE",
            "VSF_PARSER_MODE",
            sidecarMode is "sidecar" or "inhouse" or "sidecar-strict",
            $"Current mode: {sidecarMode}",
            "Set parser mode to sidecar, inhouse, or sidecar-strict."));

        var diagnosticsPath = Path.Combine(localAppData, "VsfCloneHost", "diagnostics");
        var diagnosticsWritable = false;
        try
        {
            Directory.CreateDirectory(diagnosticsPath);
            var probeFile = Path.Combine(diagnosticsPath, $"preflight_write_{Guid.NewGuid():N}.tmp");
            File.WriteAllText(probeFile, "ok", Encoding.UTF8);
            File.Delete(probeFile);
            diagnosticsWritable = true;
        }
        catch
        {
            diagnosticsWritable = false;
        }
        checks.Add(new PreflightCheckResult(
            "DIAGNOSTICS_OUTPUT_WRITABLE",
            "DIAGNOSTICS_OUTPUT_WRITABLE",
            diagnosticsWritable,
            diagnosticsWritable ? diagnosticsPath : $"Unable to write diagnostics under: {diagnosticsPath}",
            "Verify local disk permissions and available space for diagnostics output."));

        var sidecarPath = _sessionPersistence.Sidecar.SidecarPath?.Trim() ?? string.Empty;
        var sidecarModeNeedsPath = sidecarMode is "sidecar" or "sidecar-strict";
        var sidecarPathValid = !sidecarModeNeedsPath || string.IsNullOrWhiteSpace(sidecarPath) || File.Exists(sidecarPath);
        checks.Add(new PreflightCheckResult(
            "SIDECAR_PATH_VALID",
            "SIDECAR_PATH_VALID",
            sidecarPathValid,
            string.IsNullOrWhiteSpace(sidecarPath) ? "No explicit sidecar path configured." : sidecarPath,
            "Set an existing sidecar executable path or clear the field to use default discovery."));

        LastPreflight = new PreflightSummary(DateTimeOffset.UtcNow, checks);
        AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "Preflight", LastPreflight.Passed ? "PASS" : "FAIL", LastPreflight.Passed ? NcResultCode.Ok : NcResultCode.Unsupported), false);
        return LastPreflight;
    }

    public string ExportDiagnosticsBundle(string outputDirectory)
    {
        var root = string.IsNullOrWhiteSpace(outputDirectory)
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VsfCloneHost", "diagnostics")
            : outputDirectory;
        Directory.CreateDirectory(root);

        var timestamp = DateTimeOffset.UtcNow.ToString("yyyyMMdd_HHmmss");
        var tempDir = Path.Combine(root, $"bundle_{timestamp}");
        Directory.CreateDirectory(tempDir);

        var snapshotPath = Path.Combine(tempDir, "snapshot.json");
        var logsPath = Path.Combine(tempDir, "logs.txt");
        var preflightPath = Path.Combine(tempDir, "preflight.json");
        var quickstartPath = Path.Combine(tempDir, "quickstart.txt");
        var compatibilityPath = Path.Combine(tempDir, "compatibility.txt");
        var telemetryPath = Path.Combine(tempDir, "telemetry.json");
        var onboardingKpiPath = Path.Combine(tempDir, "onboarding_kpi_summary.txt");
        var reproCommandsPath = Path.Combine(tempDir, "repro_commands.txt");
        var environmentPath = Path.Combine(tempDir, "environment_snapshot.json");
        var manifestPath = Path.Combine(tempDir, "diagnostics_manifest.json");

        File.WriteAllText(snapshotPath, JsonSerializer.Serialize(LastSnapshot, FeatureJsonOptions), Encoding.UTF8);
        File.WriteAllText(logsPath, string.Join(Environment.NewLine, LogEntries.Select(x => $"{x.TimestampUtc:O} [{x.Source}] {x.ResultCode} {x.Message}")), Encoding.UTF8);
        File.WriteAllText(preflightPath, JsonSerializer.Serialize(LastPreflight ?? RunPreflight(), FeatureJsonOptions), Encoding.UTF8);
        File.WriteAllText(quickstartPath, HostContent.BuildQuickstartText(), Encoding.UTF8);
        File.WriteAllText(compatibilityPath, HostContent.BuildCompatibilityText(), Encoding.UTF8);
        _ = _telemetry.Export(telemetryPath);
        File.WriteAllText(onboardingKpiPath, BuildOnboardingKpiSummary(), Encoding.UTF8);
        File.WriteAllText(reproCommandsPath, BuildDiagnosticsReproCommands(), Encoding.UTF8);
        File.WriteAllText(environmentPath, JsonSerializer.Serialize(BuildEnvironmentSnapshot(), FeatureJsonOptions), Encoding.UTF8);
        File.WriteAllText(
            manifestPath,
            JsonSerializer.Serialize(
                BuildDiagnosticsManifest(
                    telemetryPath,
                    snapshotPath,
                    preflightPath,
                    environmentPath,
                    reproCommandsPath,
                    onboardingKpiPath),
                FeatureJsonOptions),
            Encoding.UTF8);

        var zipPath = Path.Combine(root, $"diagnostics_bundle_{timestamp}.zip");
        if (File.Exists(zipPath))
        {
            File.Delete(zipPath);
        }

        ZipFile.CreateFromDirectory(tempDir, zipPath, CompressionLevel.Optimal, false);
        AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "DiagnosticsBundle", zipPath, NcResultCode.Ok), false);
        return zipPath;
    }

    private string BuildDiagnosticsReproCommands()
    {
        var sb = new StringBuilder();
        sb.AppendLine("VsfClone Host Repro Commands");
        sb.AppendLine($"GeneratedUtc: {DateTimeOffset.UtcNow:O}");
        sb.AppendLine();
        sb.AppendLine("# Host publish (WPF-only default)");
        sb.AppendLine("powershell -ExecutionPolicy Bypass -File .\\tools\\publish_hosts.ps1");
        sb.AppendLine();
        sb.AppendLine("# Host publish with WinUI diagnostics track");
        sb.AppendLine("powershell -ExecutionPolicy Bypass -File .\\tools\\publish_hosts.ps1 -IncludeWinUi");
        sb.AppendLine();
        sb.AppendLine("# Combined quality baseline");
        sb.AppendLine("powershell -ExecutionPolicy Bypass -File .\\tools\\run_quality_baseline.ps1");
        sb.AppendLine();
        sb.AppendLine("# VSFAvatar gate + trend");
        sb.AppendLine("powershell -ExecutionPolicy Bypass -File .\\tools\\vsfavatar_quality_gate.ps1 -UseFixedSet");
        sb.AppendLine("powershell -ExecutionPolicy Bypass -File .\\tools\\vsfavatar_gated_trend.ps1");
        sb.AppendLine();
        sb.AppendLine("# Release dashboard / readiness");
        sb.AppendLine("powershell -ExecutionPolicy Bypass -File .\\tools\\release_gate_dashboard.ps1");
        sb.AppendLine("powershell -ExecutionPolicy Bypass -File .\\tools\\release_readiness_gate.ps1 -RenderPerfProfile desktop-60 -SoakIterationsPerSample 10 -SoakMinSuccessRatio 1.0 -SoakMinPerSampleSuccessRatio 1.0");
        sb.AppendLine("# Strict tracking contract requires explicit MediaPipe python via env or -MediapipePythonExe");
        sb.AppendLine("$env:VSFCLONE_MEDIAPIPE_PYTHON='C:\\\\path\\\\to\\\\python.exe'; powershell -ExecutionPolicy Bypass -File .\\tools\\release_readiness_gate.ps1 -RenderPerfProfile desktop-60");
        sb.AppendLine();
        sb.AppendLine("# Onboarding KPI summary from telemetry export");
        sb.AppendLine("powershell -ExecutionPolicy Bypass -File .\\tools\\onboarding_kpi_summary.ps1 -TelemetryPath .\\build\\reports\\telemetry_latest.json");
        return sb.ToString();
    }

    private object BuildDiagnosticsManifest(
        string telemetryPath,
        string snapshotPath,
        string preflightPath,
        string environmentPath,
        string reproCommandsPath,
        string onboardingKpiPath)
    {
        var avatarPath = _sessionPersistence.AvatarPath ?? string.Empty;
        var parserMode = _sessionPersistence.Sidecar.ParserMode ?? string.Empty;
        return new
        {
            manifest_version = "2",
            generated_utc = DateTimeOffset.UtcNow.ToString("O"),
            gate_contract_version = "release-readiness-v2",
            session = new
            {
                metrics_session_id = _metricsSessionId,
                ui_mode = _sessionPersistence.UiMode,
                parser_mode = parserMode,
                last_profile_name = _sessionPersistence.LastProfileName,
                avatar_path_sha256 = ComputeStringSha256(avatarPath),
            },
            files = new
            {
                telemetry_sha256 = ComputeFileSha256OrEmpty(telemetryPath),
                snapshot_sha256 = ComputeFileSha256OrEmpty(snapshotPath),
                preflight_sha256 = ComputeFileSha256OrEmpty(preflightPath),
                environment_sha256 = ComputeFileSha256OrEmpty(environmentPath),
                repro_commands_sha256 = ComputeFileSha256OrEmpty(reproCommandsPath),
                onboarding_kpi_sha256 = ComputeFileSha256OrEmpty(onboardingKpiPath),
            },
        };
    }

    private string BuildOnboardingKpiSummary()
    {
        var events = _telemetry.Snapshot();
        var onboardingEvents = events
            .Where(item => string.Equals(ReadString(item, "name"), "onboarding_milestone", StringComparison.OrdinalIgnoreCase))
            .ToList();
        var sessions = new Dictionary<string, SessionKpi>(StringComparer.Ordinal);
        var outputMilestones = 0;
        var outputSuccessMilestones = 0;

        foreach (var item in onboardingEvents)
        {
            var sessionStartedAt = ReadString(item, "session_started_at");
            if (string.IsNullOrWhiteSpace(sessionStartedAt))
            {
                continue;
            }

            if (!sessions.TryGetValue(sessionStartedAt, out var session))
            {
                session = new SessionKpi(sessionStartedAt);
                sessions[sessionStartedAt] = session;
            }

            var milestone = ReadString(item, "milestone");
            if (milestone.StartsWith("output_started:", StringComparison.OrdinalIgnoreCase))
            {
                outputMilestones++;
                session.HasOutputStarted = true;
                if (ReadBool(item, "within_3min_success"))
                {
                    outputSuccessMilestones++;
                    session.Within3MinSuccess = true;
                }
            }
        }

        var sessionCount = sessions.Count;
        var successSessions = sessions.Values.Count(x => x.Within3MinSuccess);
        var outputStartedSessions = sessions.Values.Count(x => x.HasOutputStarted);
        var successRate = sessionCount > 0 ? (100.0 * successSessions / sessionCount) : 0.0;

        var sb = new StringBuilder();
        sb.AppendLine("Onboarding KPI Summary");
        sb.AppendLine($"GeneratedUtc: {DateTimeOffset.UtcNow:O}");
        sb.AppendLine($"SessionCount: {sessionCount}");
        sb.AppendLine($"OutputStartedSessions: {outputStartedSessions}");
        sb.AppendLine($"Within3MinSuccessSessions: {successSessions}");
        sb.AppendLine($"Within3MinSuccessRatePct: {successRate:F2}");
        sb.AppendLine($"OutputStartedMilestones: {outputMilestones}");
        sb.AppendLine($"OutputSuccessMilestones: {outputSuccessMilestones}");
        if (_sessionStartedAtUtc.HasValue)
        {
            sb.AppendLine($"CurrentSessionStartedUtc: {_sessionStartedAtUtc.Value:O}");
        }
        if (_outputStartedAtUtc.HasValue)
        {
            sb.AppendLine($"CurrentSessionOutputStartedUtc: {_outputStartedAtUtc.Value:O}");
            sb.AppendLine($"CurrentSessionWithin3MinSuccess: {_within3MinSuccess}");
        }
        return sb.ToString();
    }

    private static string ReadString(IReadOnlyDictionary<string, object?> item, string key)
    {
        if (!item.TryGetValue(key, out var value) || value is null)
        {
            return string.Empty;
        }

        if (value is string text)
        {
            return text;
        }

        if (value is JsonElement element)
        {
            return element.ValueKind switch
            {
                JsonValueKind.String => element.GetString() ?? string.Empty,
                JsonValueKind.True => "true",
                JsonValueKind.False => "false",
                JsonValueKind.Number => element.ToString(),
                _ => string.Empty,
            };
        }

        return value.ToString() ?? string.Empty;
    }

    private static bool ReadBool(IReadOnlyDictionary<string, object?> item, string key)
    {
        if (!item.TryGetValue(key, out var value) || value is null)
        {
            return false;
        }

        if (value is bool flag)
        {
            return flag;
        }

        if (value is string text && bool.TryParse(text, out var parsed))
        {
            return parsed;
        }

        if (value is JsonElement element)
        {
            return element.ValueKind switch
            {
                JsonValueKind.True => true,
                JsonValueKind.False => false,
                JsonValueKind.String when bool.TryParse(element.GetString(), out var parsedBool) => parsedBool,
                _ => false,
            };
        }

        return false;
    }

    private sealed class SessionKpi
    {
        public SessionKpi(string startedAtUtc)
        {
            StartedAtUtc = startedAtUtc;
        }

        public string StartedAtUtc { get; }
        public bool HasOutputStarted { get; set; }
        public bool Within3MinSuccess { get; set; }
    }

    private static object BuildEnvironmentSnapshot()
    {
        var envSubset = new Dictionary<string, string?>(StringComparer.OrdinalIgnoreCase)
        {
            ["VSF_PARSER_MODE"] = Environment.GetEnvironmentVariable("VSF_PARSER_MODE"),
            ["VSF_SIDECAR_PATH"] = Environment.GetEnvironmentVariable("VSF_SIDECAR_PATH"),
            ["VSF_SIDECAR_TIMEOUT_MS"] = Environment.GetEnvironmentVariable("VSF_SIDECAR_TIMEOUT_MS"),
            ["VSFCLONE_MEDIAPIPE_PYTHON"] = Environment.GetEnvironmentVariable("VSFCLONE_MEDIAPIPE_PYTHON"),
            ["DOTNET_ROOT"] = Environment.GetEnvironmentVariable("DOTNET_ROOT"),
            ["PATH"] = Environment.GetEnvironmentVariable("PATH"),
        };

        return new
        {
            generated_utc = DateTimeOffset.UtcNow.ToString("O"),
            machine = new
            {
                machine_name = Environment.MachineName,
                os = RuntimeInformation.OSDescription,
                os_arch = RuntimeInformation.OSArchitecture.ToString(),
                process_arch = RuntimeInformation.ProcessArchitecture.ToString(),
                framework = RuntimeInformation.FrameworkDescription,
                processor_count = Environment.ProcessorCount,
            },
            process = new
            {
                base_directory = AppContext.BaseDirectory,
                current_directory = Directory.GetCurrentDirectory(),
                command_line = Environment.CommandLine,
            },
            environment = envSubset,
        };
    }

    private static string ComputeFileSha256OrEmpty(string path)
    {
        try
        {
            if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
            {
                return string.Empty;
            }

            using var sha = SHA256.Create();
            using var stream = File.OpenRead(path);
            var hash = sha.ComputeHash(stream);
            return Convert.ToHexString(hash);
        }
        catch
        {
            return string.Empty;
        }
    }

    private static string ComputeStringSha256(string value)
    {
        if (string.IsNullOrEmpty(value))
        {
            return string.Empty;
        }

        using var sha = SHA256.Create();
        var bytes = Encoding.UTF8.GetBytes(value);
        var hash = sha.ComputeHash(bytes);
        return Convert.ToHexString(hash);
    }

    public string ExportRollingMetricsCsv(string outputPath)
    {
        var path = string.IsNullOrWhiteSpace(outputPath)
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VsfCloneHost", "metrics", $"metrics_{DateTimeOffset.UtcNow:yyyyMMdd_HHmmss}.csv")
            : outputPath;

        var directory = Path.GetDirectoryName(path);
        if (!string.IsNullOrWhiteSpace(directory))
        {
            Directory.CreateDirectory(directory);
        }

        var sb = new StringBuilder();
        sb.AppendLine("timestamp_utc,frame_ms,gpu_frame_ms,cpu_frame_ms,material_resolve_ms,pass_count,render_ready_avatar_count,spout_active,osc_active,working_set_mb,private_mb,auto_quality_step,measurement_source,measurement_session_id,memory_sample_status");
        foreach (var item in _rollingMetrics)
        {
            sb.AppendLine(
                string.Join(
                    ",",
                    item.TimestampUtc.ToString("O", CultureInfo.InvariantCulture),
                    item.FrameMs.ToString("F3", CultureInfo.InvariantCulture),
                    item.GpuFrameMs.ToString("F3", CultureInfo.InvariantCulture),
                    item.CpuFrameMs.ToString("F3", CultureInfo.InvariantCulture),
                    item.MaterialResolveMs.ToString("F3", CultureInfo.InvariantCulture),
                    item.PassCount.ToString(CultureInfo.InvariantCulture),
                    item.RenderReadyAvatarCount.ToString(CultureInfo.InvariantCulture),
                    item.SpoutActive.ToString(),
                    item.OscActive.ToString(),
                    item.WorkingSetMb.ToString("F3", CultureInfo.InvariantCulture),
                    item.PrivateMb.ToString("F3", CultureInfo.InvariantCulture),
                    item.AutoQualityStep,
                    item.MeasurementSource,
                    item.MeasurementSessionId,
                    item.MemorySampleStatus));
        }

        File.WriteAllText(path, sb.ToString(), Encoding.UTF8);
        AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "MetricsExport", path, NcResultCode.Ok), false);
        return path;
    }

    public string GetQuickstartText() => HostContent.BuildQuickstartText();

    public string GetCompatibilityText() => HostContent.BuildCompatibilityText();

    public string GetReleaseTrackStatus()
    {
        try
        {
            var candidates = new[]
            {
                Path.Combine(Directory.GetCurrentDirectory(), "build", "reports", "winui", "winui_diagnostic_manifest.json"),
                Path.Combine(Directory.GetCurrentDirectory(), "NativeVsfClone", "build", "reports", "winui", "winui_diagnostic_manifest.json"),
            };
            var path = candidates.FirstOrDefault(File.Exists);
            if (string.IsNullOrWhiteSpace(path))
            {
                return "WPF: READY | WinUI: UNKNOWN (diagnostic manifest not found)";
            }

            var json = File.ReadAllText(path, Encoding.UTF8);
            using var doc = JsonDocument.Parse(json);
            var failureClass = doc.RootElement.TryGetProperty("failure_class", out var fc)
                ? fc.GetString() ?? "unknown"
                : "unknown";
            if (string.Equals(failureClass, "NONE", StringComparison.OrdinalIgnoreCase))
            {
                return "WPF: READY | WinUI: READY";
            }

            return $"WPF: READY | WinUI: BLOCKED ({failureClass})";
        }
        catch
        {
            return "WPF: READY | WinUI: UNKNOWN (failed to parse diagnostic manifest)";
        }
    }

    public NcResultCode ApplyRenderProfile(string profileName)
    {
        var normalized = string.IsNullOrWhiteSpace(profileName) ? "quality" : profileName.Trim().ToLowerInvariant();
        var current = RenderState;
        var next = normalized switch
        {
            "performance" => current with { BroadcastMode = true, CameraMode = RenderCameraMode.AutoFitBust, FramingTarget = 0.70f, Headroom = 0.08f, FovDeg = 32.0f, ShowDebugOverlay = false },
            "stability" => current with { BroadcastMode = true, CameraMode = RenderCameraMode.AutoFitBust, FramingTarget = 0.74f, Headroom = 0.12f, FovDeg = 35.0f, ShowDebugOverlay = false },
            "ultra-parity" => current with { BroadcastMode = true, CameraMode = RenderCameraMode.AutoFitBust, FramingTarget = 0.78f, Headroom = 0.10f, FovDeg = 42.0f, ShowDebugOverlay = false },
            _ => current with { BroadcastMode = true, CameraMode = RenderCameraMode.AutoFitBust, FramingTarget = 0.80f, Headroom = 0.10f, FovDeg = 40.0f, ShowDebugOverlay = false },
        };

        _sessionPersistence = _sessionPersistence with { LastProfileName = normalized };
        PersistSessionSnapshot();
        return ApplyRenderUiState(next);
    }

    public SidecarSettings GetSidecarSettings() => _sessionPersistence.Sidecar;

    public TrackingInputSettings GetTrackingInputSettings() => _sessionPersistence.Tracking;

    public IReadOnlyList<WebcamDeviceOption> GetAvailableWebcamDevices(int maxProbe = 10)
    {
        var cap = Math.Clamp(maxProbe, 1, 64);
        var list = new List<WebcamDeviceOption>(cap + 1)
        {
            new WebcamDeviceOption(string.Empty, "Default Camera", true),
        };
        try
        {
            // Enumerate webcam devices without opening capture handles to avoid virtual-camera crashes.
            var devices = DeviceInformation.FindAllAsync(DeviceClass.VideoCapture)
                .GetAwaiter()
                .GetResult();
            var count = Math.Min(cap, devices.Count);
            if (count == 0)
            {
                list[0] = new WebcamDeviceOption(string.Empty, "Default Camera", false);
                return list;
            }

            for (var i = 0; i < count; i++)
            {
                var displayName = devices[i].Name?.Trim();
                if (string.IsNullOrWhiteSpace(displayName))
                {
                    displayName = $"Camera {i}";
                }

                list.Add(new WebcamDeviceOption(
                    i.ToString(CultureInfo.InvariantCulture),
                    displayName,
                    true));
            }
        }
        catch (Exception ex)
        {
            list[0] = new WebcamDeviceOption(string.Empty, "Default Camera (enumeration failed)", false);
            AddLog(
                new HostLogEntry(
                    DateTimeOffset.UtcNow,
                    "TrackingWebcamEnumerate",
                    $"Windows camera enumeration failed: {ex.Message}",
                    NcResultCode.Internal),
                true);
        }

        return list;
    }

    public void ConfigureTrackingInputSettings(
        ushort listenPort,
        int staleTimeoutMs,
        TrackingSourceType? sourceType = null,
        string? cameraDeviceKey = null,
        int? inferenceFpsCap = null,
        int? parseErrorWarnThreshold = null,
        int? droppedPacketWarnThreshold = null,
        TrackingSourceLockMode? sourceLockMode = null,
        TrackingLatencyProfile? latencyProfile = null,
        PoseFilterProfile? poseFilterProfile = null,
        float? poseDeadbandDeg = null,
        bool? autoStabilityTuningEnabled = null,
        bool? upperBodyEnabled = null,
        float? upperBodyStrength = null,
        UpperBodySmoothingProfile? upperBodySmoothing = null)
    {
        var current = _sessionPersistence.Tracking;
        var resolvedProfile = latencyProfile ?? current.LatencyProfile;
        var resolvedPoseFilter = poseFilterProfile ?? current.PoseFilterProfile;
        var resolvedDeadband = Math.Clamp(
            float.IsFinite(poseDeadbandDeg ?? current.PoseDeadbandDeg) ? (poseDeadbandDeg ?? current.PoseDeadbandDeg) : 0.9f,
            0.0f,
            3.0f);
        var resolvedUpperBodyStrength = Math.Clamp(
            float.IsFinite(upperBodyStrength ?? current.UpperBodyStrength) ? (upperBodyStrength ?? current.UpperBodyStrength) : 1.0f,
            0.0f,
            1.5f);
        var normalized = new TrackingInputSettings(
            listenPort == 0 ? (ushort)49983 : listenPort,
            Math.Clamp(staleTimeoutMs <= 0 ? 500 : staleTimeoutMs, 50, 5000),
            current.LastActive,
            sourceType ?? current.SourceType,
            cameraDeviceKey ?? current.CameraDeviceKey,
            Math.Clamp(inferenceFpsCap ?? current.InferenceFpsCap, 5, 120),
            Math.Clamp(parseErrorWarnThreshold ?? current.ParseErrorWarnThreshold, 1, 10000),
            Math.Clamp(droppedPacketWarnThreshold ?? current.DroppedPacketWarnThreshold, 1, 10000),
            sourceLockMode ?? current.SourceLockMode,
            resolvedProfile,
            resolvedPoseFilter,
            resolvedDeadband,
            autoStabilityTuningEnabled ?? current.AutoStabilityTuningEnabled,
            upperBodyEnabled ?? current.UpperBodyEnabled,
            resolvedUpperBodyStrength,
            upperBodySmoothing ?? current.UpperBodySmoothing);
        _sessionPersistence = _sessionPersistence with
        {
            Tracking = normalized,
            LastUpdatedUtc = DateTimeOffset.UtcNow,
        };
        PersistSessionSnapshot();
        AddLog(
            new HostLogEntry(
                DateTimeOffset.UtcNow,
                "TrackingConfig",
                $"port={normalized.ListenPort}, stale_ms={normalized.StaleTimeoutMs}, source={normalized.SourceType}, lock={normalized.SourceLockMode}, profile={normalized.LatencyProfile}, pose_filter={normalized.PoseFilterProfile}, deadband_deg={normalized.PoseDeadbandDeg:F2}, auto_stability={normalized.AutoStabilityTuningEnabled}, upper_body={normalized.UpperBodyEnabled}, upper_strength={normalized.UpperBodyStrength:F2}, upper_smoothing={normalized.UpperBodySmoothing}, fps_cap={normalized.InferenceFpsCap}, parse_warn={normalized.ParseErrorWarnThreshold}, dropped_warn={normalized.DroppedPacketWarnThreshold}",
                NcResultCode.Ok),
            false);
    }

    public void ConfigureSidecarSettings(SidecarSettings settings)
    {
        var mode = settings.StrictMode ? "sidecar-strict" : settings.ParserMode;
        var normalized = settings with
        {
            ParserMode = string.IsNullOrWhiteSpace(mode) ? "sidecar" : mode.Trim().ToLowerInvariant(),
            TimeoutMs = settings.TimeoutMs <= 0 ? 15000 : settings.TimeoutMs,
        };

        _sessionPersistence = _sessionPersistence with { Sidecar = normalized, LastUpdatedUtc = DateTimeOffset.UtcNow };
        ApplySidecarEnvironment(normalized);
        PersistSessionSnapshot();
        AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "SidecarConfig", $"mode={normalized.ParserMode}, timeout={normalized.TimeoutMs}", NcResultCode.Ok), false);
    }

    private void SetTrackingState(ushort listenPort, int staleTimeoutMs, bool active)
    {
        var current = _sessionPersistence.Tracking;
        var normalized = new TrackingInputSettings(
            listenPort == 0 ? (ushort)49983 : listenPort,
            Math.Clamp(staleTimeoutMs <= 0 ? 500 : staleTimeoutMs, 50, 5000),
            active,
            current.SourceType,
            current.CameraDeviceKey,
            current.InferenceFpsCap,
            current.ParseErrorWarnThreshold,
            current.DroppedPacketWarnThreshold,
            current.SourceLockMode,
            current.LatencyProfile,
            current.PoseFilterProfile,
            current.PoseDeadbandDeg,
            current.AutoStabilityTuningEnabled,
            current.UpperBodyEnabled,
            current.UpperBodyStrength,
            current.UpperBodySmoothing);
        _sessionPersistence = _sessionPersistence with
        {
            Tracking = normalized,
            LastUpdatedUtc = DateTimeOffset.UtcNow,
        };
        PersistSessionSnapshot();
    }

    public void SetTelemetryPolicy(bool optIn, bool redactSensitiveFields)
    {
        _telemetry.UpdateSettings(optIn, redactSensitiveFields);
        AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "TelemetryPolicy", $"opt_in={optIn}, redact={redactSensitiveFields}", NcResultCode.Ok), false);
    }

    public string ExportTelemetry(string outputPath)
    {
        var path = string.IsNullOrWhiteSpace(outputPath)
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VsfCloneHost", "telemetry", $"telemetry_{DateTimeOffset.UtcNow:yyyyMMdd_HHmmss}.json")
            : outputPath;
        var count = _telemetry.Export(path);
        AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "TelemetryExport", $"events={count} path={path}", NcResultCode.Ok), false);
        return path;
    }

    public AutoQualityPolicy GetAutoQualityPolicy() => _autoQualityPolicy;

    public void ConfigureAutoQualityPolicy(AutoQualityPolicy policy)
    {
        _autoQualityPolicy = new AutoQualityPolicy(
            Math.Clamp(policy.HighFrameMsThreshold, 10.0f, 80.0f),
            Math.Clamp(policy.ConsecutiveFrameLimit, 10, 1200),
            Math.Clamp(policy.CooldownSeconds, 5, 300),
            Math.Clamp(policy.RecoveryFrameMsThreshold, 8.0f, Math.Clamp(policy.HighFrameMsThreshold, 10.0f, 80.0f)),
            Math.Clamp(policy.RecoveryConsecutiveFrameLimit, 10, 2400),
            policy.AutoTuneEnabled,
            Math.Clamp(policy.WindowSampleCount, 120, 1800),
            Math.Clamp(policy.DegradeP95FrameMs, 12.0f, 80.0f),
            Math.Clamp(policy.DegradeDropRatio, 0.0f, 1.0f),
            Math.Clamp(policy.RecoverP95FrameMs, 8.0f, Math.Clamp(policy.DegradeP95FrameMs, 12.0f, 80.0f)),
            Math.Clamp(policy.RecoverDropRatio, 0.0f, Math.Clamp(policy.DegradeDropRatio, 0.0f, 1.0f)));
        _autoQualityStore.Save(_autoQualityPolicy);
        AddLog(
            new HostLogEntry(
                DateTimeOffset.UtcNow,
                "AutoQualityPolicy",
                $"threshold_ms={_autoQualityPolicy.HighFrameMsThreshold:F1}, consecutive={_autoQualityPolicy.ConsecutiveFrameLimit}, cooldown_sec={_autoQualityPolicy.CooldownSeconds}, recovery_threshold_ms={_autoQualityPolicy.RecoveryFrameMsThreshold:F1}, recovery_consecutive={_autoQualityPolicy.RecoveryConsecutiveFrameLimit}, auto_tune={_autoQualityPolicy.AutoTuneEnabled}, window={_autoQualityPolicy.WindowSampleCount}, degrade_p95={_autoQualityPolicy.DegradeP95FrameMs:F1}, degrade_drop={_autoQualityPolicy.DegradeDropRatio:F3}, recover_p95={_autoQualityPolicy.RecoverP95FrameMs:F1}, recover_drop={_autoQualityPolicy.RecoverDropRatio:F3}",
                NcResultCode.Ok),
            false);
    }

    public string GetLastErrorGuidance()
    {
        if (LastUserFacingError is null)
        {
            return string.Empty;
        }

        return $"[{LastUserFacingError.ErrorCode}] {LastUserFacingError.Title} | {LastUserFacingError.ActionHint}";
    }

    public string GetLastLoadFailureDetails()
    {
        if (string.IsNullOrWhiteSpace(_lastLoadFailureGuidance) &&
            string.IsNullOrWhiteSpace(_lastLoadFailureTechnical))
        {
            return string.Empty;
        }

        if (string.IsNullOrWhiteSpace(_lastLoadFailureGuidance))
        {
            return _lastLoadFailureTechnical;
        }

        if (string.IsNullOrWhiteSpace(_lastLoadFailureTechnical))
        {
            return _lastLoadFailureGuidance;
        }

        return $"{_lastLoadFailureGuidance}{Environment.NewLine}{Environment.NewLine}{_lastLoadFailureTechnical}";
    }

    public Task<NcResultCode> LoadAvatarAsync(string path, int timeoutMs)
    {
        if (_activeLoadTask is { IsCompleted: false })
        {
            return _activeLoadTask;
        }
        if (_activeLoadWorkerTask is { IsCompleted: false })
        {
            AddLog(
                new HostLogEntry(
                    DateTimeOffset.UtcNow,
                    "LoadAvatarAsync",
                    "previous worker still active; rejecting overlapping load",
                    NcResultCode.InvalidArgument),
                false);
            return Task.FromResult(NcResultCode.InvalidArgument);
        }

        _loadCancellation?.Dispose();
        _loadCancellation = new CancellationTokenSource();
        var token = _loadCancellation.Token;
        var operationId = $"load-{Interlocked.Increment(ref _loadOperationSequence)}";
        _activeLoadOperationId = operationId;
        AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "LoadAvatarAsync", $"started timeout_ms={timeoutMs}", NcResultCode.Ok), false);
        PublishLoadProgress(operationId, "queued", 5, "Load queued.", false);

        _activeLoadTask = Task.Run(async () =>
        {
            Task<NcResultCode>? worker = null;
            var timeoutObserved = false;
            try
            {
                PublishLoadProgress(operationId, "validating", 15, "Validating avatar path and operation state.", false);
                worker = Task.Run(() => LoadAvatar(path));
                _activeLoadWorkerTask = worker;
                PublishLoadProgress(operationId, "loading", 70, "Loading avatar package and runtime payload.", false);
                var normalizedTimeout = TimeSpan.FromMilliseconds(Math.Max(1000, timeoutMs));
                var completed = await Task.WhenAny(worker, Task.Delay(normalizedTimeout));
                if (completed != worker)
                {
                    timeoutObserved = true;
                    AddLog(
                        new HostLogEntry(
                            DateTimeOffset.UtcNow,
                            "LoadTimeoutPendingWorker",
                            $"timeout_ms={timeoutMs}; waiting for worker completion to prevent late state commit races",
                            NcResultCode.Internal),
                        false);
                    PublishLoadProgress(operationId, "timeout-pending", 80, "Load timeout reached; waiting for worker to finish safely.", false);
                }
                if (token.IsCancellationRequested)
                {
                    AddLog(
                        new HostLogEntry(
                            DateTimeOffset.UtcNow,
                            "LoadCancelPendingWorker",
                            $"operation_id={operationId}; waiting for worker completion",
                            NcResultCode.Internal),
                        false);
                    PublishLoadProgress(operationId, "cancel-pending", 80, "Cancel requested; waiting for safe worker completion.", false);
                }

                var rc = await worker;
                if (!string.Equals(_activeLoadOperationId, operationId, StringComparison.Ordinal))
                {
                    AddLog(
                        new HostLogEntry(
                            DateTimeOffset.UtcNow,
                            "LoadLateCompletionDiscarded",
                            $"operation_id={operationId}",
                            rc),
                        false);
                    return rc;
                }
                if (timeoutObserved)
                {
                    AddLog(
                        new HostLogEntry(
                            DateTimeOffset.UtcNow,
                            "LoadCommitAccepted",
                            $"operation_id={operationId}, rc={rc}",
                            rc),
                        false);
                }
                PublishLoadProgress(operationId, "finalizing", 95, "Finalizing avatar runtime state.", false);
                PublishLoadProgress(operationId, "completed", rc == NcResultCode.Ok ? 100 : 0, rc == NcResultCode.Ok ? "Load completed." : $"Load failed: {rc}", true);
                return rc;
            }
            catch (Exception ex)
            {
                AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "LoadAvatarAsync", $"unexpected failure: {ex.GetType().Name}", NcResultCode.Internal), true);
                if (string.Equals(_activeLoadOperationId, operationId, StringComparison.Ordinal))
                {
                    PublishLoadProgress(operationId, "failed", 0, "Load failed unexpectedly.", true);
                }
                return NcResultCode.Internal;
            }
            finally
            {
                if (worker is { IsCompleted: false })
                {
                    try
                    {
                        _ = await worker;
                    }
                    catch
                    {
                        // Worker failure already represented in load state/logs.
                    }
                }

                if (string.Equals(_activeLoadOperationId, operationId, StringComparison.Ordinal))
                {
                    _activeLoadOperationId = string.Empty;
                    _activeLoadTask = null;
                }
                if (_activeLoadWorkerTask == worker)
                {
                    _activeLoadWorkerTask = null;
                }
                RefreshState();
            }
        });

        return _activeLoadTask;
    }

    public void CancelLoadAvatar()
    {
        if (_loadCancellation is null)
        {
            return;
        }

        _loadCancellation.Cancel();
        AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "LoadAvatarAsync", "cancel requested by operator (awaiting safe worker completion)", NcResultCode.Ok), false);
        var operationId = string.IsNullOrWhiteSpace(_activeLoadOperationId) ? "load-cancel" : _activeLoadOperationId;
        PublishLoadProgress(operationId, "cancel-pending", 0, "Cancel requested; waiting for safe load finalization.", false);
    }

    private void RecordFrameMetricAndGuardrails(in NcRuntimeStats stats)
    {
        TryCaptureProcessMemorySample();
        _rollingMetrics.Enqueue(new FrameMetric(
            DateTimeOffset.UtcNow,
            stats.LastFrameMs,
            stats.GpuFrameMs,
            stats.CpuFrameMs,
            stats.MaterialResolveMs,
            stats.PassCount,
            stats.RenderReadyAvatarCount,
            stats.SpoutActive != 0U,
            stats.OscActive != 0U,
            _lastWorkingSetMb,
            _lastPrivateMb,
            GetCurrentAutoQualityStepName(),
            MeasurementSourceLiveTick,
            _metricsSessionId,
            _lastMemorySampleStatus));
        if (_rollingMetrics.Count > RollingMetricCapacity)
        {
            _ = _rollingMetrics.Dequeue();
        }

        if (_autoQualityPolicy.AutoTuneEnabled)
        {
            ApplyAdaptiveAutoQualityFromWindow();
            return;
        }

        if (stats.LastFrameMs > _autoQualityPolicy.HighFrameMsThreshold)
        {
            _highFrameCount++;
            _recoveryFrameCount = 0;
        }
        else
        {
            _highFrameCount = Math.Max(0, _highFrameCount - 1);
            if (stats.LastFrameMs <= _autoQualityPolicy.RecoveryFrameMsThreshold)
            {
                _recoveryFrameCount++;
            }
            else
            {
                _recoveryFrameCount = Math.Max(0, _recoveryFrameCount - 1);
            }
        }

        if (_autoQualityDowngraded &&
            _recoveryFrameCount >= _autoQualityPolicy.RecoveryConsecutiveFrameLimit &&
            (DateTimeOffset.UtcNow - _lastAutoQualityAdjustUtc) >= TimeSpan.FromSeconds(_autoQualityPolicy.CooldownSeconds))
        {
            _lastAutoQualityAdjustUtc = DateTimeOffset.UtcNow;
            _recoveryFrameCount = 0;
            _autoQualityDowngraded = false;
            _ = ApplyRenderProfile("quality");
            AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "AutoQualityGuard", "recovered to quality profile after stable frame time window", NcResultCode.Ok), false);
        }

        if (_highFrameCount < _autoQualityPolicy.ConsecutiveFrameLimit)
        {
            return;
        }
        if ((DateTimeOffset.UtcNow - _lastAutoQualityAdjustUtc) < TimeSpan.FromSeconds(_autoQualityPolicy.CooldownSeconds))
        {
            return;
        }

        _lastAutoQualityAdjustUtc = DateTimeOffset.UtcNow;
        _highFrameCount = 0;
        _recoveryFrameCount = 0;
        _autoQualityDowngraded = true;
        _ = ApplyRenderProfile("performance");
        AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "AutoQualityGuard", "applied performance profile due to sustained frame time pressure", NcResultCode.Ok), false);
    }

    private void TryCaptureProcessMemorySample()
    {
        var now = DateTimeOffset.UtcNow;
        if ((now - _lastProcessMemorySampleUtc) < TimeSpan.FromMilliseconds(500))
        {
            if (_lastMemorySampleStatus == "none")
            {
                _lastMemorySampleStatus = "stale";
            }
            return;
        }

        _lastProcessMemorySampleUtc = now;
        try
        {
            using var proc = Process.GetCurrentProcess();
            _lastWorkingSetMb = (float)(proc.WorkingSet64 / (1024.0 * 1024.0));
            _lastPrivateMb = (float)(proc.PrivateMemorySize64 / (1024.0 * 1024.0));
            _lastMemorySampleStatus = "ok";
        }
        catch
        {
            // Keep last known values if process metrics sampling is unavailable.
            _lastMemorySampleStatus = _lastWorkingSetMb > 0.0f || _lastPrivateMb > 0.0f
                ? "stale"
                : "failed";
        }
    }

    private void ApplyAdaptiveAutoQualityFromWindow()
    {
        if ((DateTimeOffset.UtcNow - _lastAutoQualityAdjustUtc) < TimeSpan.FromSeconds(_autoQualityPolicy.CooldownSeconds))
        {
            return;
        }

        var windowCount = Math.Clamp(_autoQualityPolicy.WindowSampleCount, 120, RollingMetricCapacity);
        var recent = _rollingMetrics
            .Reverse()
            .Take(windowCount)
            .Select(item => (double)item.FrameMs)
            .OrderBy(value => value)
            .ToArray();
        if (recent.Length < windowCount)
        {
            return;
        }

        var p95 = GetPercentile(recent, 95.0);
        var dropCount = recent.Count(value => value > 33.3);
        var dropRatio = dropCount / (double)recent.Length;
        var currentProfile = (_sessionPersistence.LastProfileName ?? "quality").Trim().ToLowerInvariant();
        var currentStep = GetAutoQualityStep(currentProfile);

        var shouldDegrade = p95 > _autoQualityPolicy.DegradeP95FrameMs || dropRatio > _autoQualityPolicy.DegradeDropRatio;
        var shouldRecover = p95 < _autoQualityPolicy.RecoverP95FrameMs && dropRatio < _autoQualityPolicy.RecoverDropRatio;

        if (shouldDegrade && currentStep > 0)
        {
            var nextStep = currentStep - 1;
            _lastAutoQualityAdjustUtc = DateTimeOffset.UtcNow;
            _autoQualityDowngraded = true;
            _ = ApplyRenderProfile(GetAutoQualityProfileForStep(nextStep));
            AddLog(
                new HostLogEntry(
                    DateTimeOffset.UtcNow,
                    "AutoQualityAdaptive",
                    $"degraded step={nextStep} p95_ms={p95:F2} drop_ratio={dropRatio:F3}",
                    NcResultCode.Ok),
                false);
            return;
        }

        if (_autoQualityDowngraded && shouldRecover && currentStep < 2)
        {
            var nextStep = currentStep + 1;
            _lastAutoQualityAdjustUtc = DateTimeOffset.UtcNow;
            _autoQualityDowngraded = nextStep < 2;
            _ = ApplyRenderProfile(GetAutoQualityProfileForStep(nextStep));
            AddLog(
                new HostLogEntry(
                    DateTimeOffset.UtcNow,
                    "AutoQualityAdaptive",
                    $"recovered step={nextStep} p95_ms={p95:F2} drop_ratio={dropRatio:F3}",
                    NcResultCode.Ok),
                false);
        }
    }

    private static double GetPercentile(double[] sortedValues, double percent)
    {
        if (sortedValues.Length == 0)
        {
            return 0.0;
        }
        if (sortedValues.Length == 1)
        {
            return sortedValues[0];
        }

        var rank = (percent / 100.0) * (sortedValues.Length - 1);
        var lower = (int)Math.Floor(rank);
        var upper = (int)Math.Ceiling(rank);
        if (lower == upper)
        {
            return sortedValues[lower];
        }
        var weight = rank - lower;
        return sortedValues[lower] + ((sortedValues[upper] - sortedValues[lower]) * weight);
    }

    private string GetCurrentAutoQualityStepName()
    {
        var profile = (_sessionPersistence.LastProfileName ?? "quality").Trim().ToLowerInvariant();
        return GetAutoQualityStepName(GetAutoQualityStep(profile));
    }

    private static int GetAutoQualityStep(string profileName)
    {
        return profileName switch
        {
            "performance" => 0,
            "stability" => 1,
            _ => 2,
        };
    }

    private static string GetAutoQualityProfileForStep(int step)
    {
        return step switch
        {
            <= 0 => "performance",
            1 => "stability",
            _ => "quality",
        };
    }

    private static string GetAutoQualityStepName(int step)
    {
        return step switch
        {
            <= 0 => "performance",
            1 => "stability",
            _ => "quality",
        };
    }

    private bool ValidateOperationAllowed(string operationName, out NcResultCode blockedRc)
    {
        blockedRc = NcResultCode.Ok;
        if (OperationState.IsBusy)
        {
            blockedRc = NcResultCode.InvalidArgument;
            return false;
        }

        var requiresInit = operationName is "LoadAvatar" or "UnloadAvatar" or "StartSpout" or "StopSpout" or "StartOsc" or "StopOsc" or "AttachWindow" or "ResizeWindow";
        if (requiresInit && !SessionState.IsInitialized)
        {
            blockedRc = NcResultCode.NotInitialized;
            return false;
        }

        var requiresAvatar = operationName is "StartSpout" or "StartOsc" or "UnloadAvatar";
        if (requiresAvatar && !SessionState.ActiveAvatarHandle.HasValue)
        {
            blockedRc = NcResultCode.InvalidArgument;
            return false;
        }

        return true;
    }

    private static string BuildErrorCode(string source, NcResultCode rc)
    {
        var normalizedSource = string.IsNullOrWhiteSpace(source)
            ? "unknown"
            : source.Replace('.', '_').Replace(' ', '_').ToUpperInvariant();
        return $"ERR_{normalizedSource}_{rc.ToString().ToUpperInvariant()}";
    }

    private UserFacingError BuildUserFacingError(string source, NcResultCode rc, string detail)
    {
        var errorCode = BuildErrorCode(source, rc);
        if (source.EndsWith(".Blocked", StringComparison.OrdinalIgnoreCase))
        {
            LastUserFacingError = new UserFacingError(
                errorCode,
                HostErrorCategory.Initialization,
                "Operation blocked by lifecycle policy",
                "Follow the operation order: Initialize -> Load Avatar -> Start Outputs.",
                detail);
            return LastUserFacingError;
        }

        var isLoadOrRenderUnsupported =
            rc == NcResultCode.Unsupported &&
            (source.Contains("LoadAvatar", StringComparison.OrdinalIgnoreCase) ||
             source.Contains("RenderTick", StringComparison.OrdinalIgnoreCase) ||
             detail.Contains("renderable mesh payloads", StringComparison.OrdinalIgnoreCase) ||
             detail.Contains("render resources", StringComparison.OrdinalIgnoreCase));
        var isRenderResourceRecoveryPath =
            source.Contains("RenderTick", StringComparison.OrdinalIgnoreCase) &&
            rc == NcResultCode.Unsupported &&
            detail.Contains("no avatar has render resources", StringComparison.OrdinalIgnoreCase);

        var category = rc switch
        {
            NcResultCode.NotInitialized => HostErrorCategory.Initialization,
            NcResultCode.InvalidArgument => HostErrorCategory.InputValidation,
            NcResultCode.Io => HostErrorCategory.FileIo,
            NcResultCode.Unsupported when isLoadOrRenderUnsupported => HostErrorCategory.Runtime,
            NcResultCode.Unsupported => HostErrorCategory.Toolchain,
            NcResultCode.Internal => source.Contains("Spout", StringComparison.OrdinalIgnoreCase) || source.Contains("Osc", StringComparison.OrdinalIgnoreCase)
                ? HostErrorCategory.Output
                : HostErrorCategory.Runtime,
            _ => HostErrorCategory.Unknown,
        };

        var title = category switch
        {
            HostErrorCategory.Initialization => "Session is not initialized",
            HostErrorCategory.InputValidation => "Input validation failed",
            HostErrorCategory.FileIo => "I/O operation failed",
            HostErrorCategory.Output => "Output runtime operation failed",
            HostErrorCategory.Toolchain => "Toolchain prerequisite missing or unsupported",
            HostErrorCategory.Runtime => "Runtime operation failed",
            _ => "Unknown operation failure",
        };

        var action = category switch
        {
            HostErrorCategory.Initialization => "Initialize session before this action.",
            HostErrorCategory.InputValidation => "Review input fields and retry.",
            HostErrorCategory.FileIo => "Verify file path/access and retry.",
            HostErrorCategory.Output => "Restart output and collect diagnostics bundle.",
            HostErrorCategory.Toolchain => "Run preflight and resolve failed checks.",
            HostErrorCategory.Runtime => "Retry once, then export diagnostics bundle.",
            _ => "Collect diagnostics and inspect runtime logs.",
        };

        if (source.Contains("LoadAvatar", StringComparison.OrdinalIgnoreCase))
        {
            var (parserMode, parserStage, primaryError) = GetLoadFailureContext();
            if (detail.Contains("renderable mesh payloads", StringComparison.OrdinalIgnoreCase))
            {
                title = $"Runtime avatar load failed (format recognized, stage={parserStage})";
                action = "VSFAvatar metadata loaded, but render mesh payload is missing. Export diagnostics and convert via supported runtime path.";
            }
            else
            {
                title = $"Runtime avatar load failed (format recognized, stage={parserStage})";
                action = "Check parser_stage/primary_error details, then retry. If it fails again, export diagnostics and convert to .xav2/.vrm.";
            }
            detail = AppendLoadContextDetail(detail, parserMode, parserStage, primaryError);
        }
        else if (source.Contains("StartSpout", StringComparison.OrdinalIgnoreCase) ||
                 source.Contains("StartOsc", StringComparison.OrdinalIgnoreCase))
        {
            action = "Verify output settings and active avatar state, then restart outputs.";
        }
        else if (isRenderResourceRecoveryPath)
        {
            title = "Render resources are not ready yet";
            action = "Automatic recovery was attempted. Retry once; if it persists, reload avatar then export diagnostics bundle.";
        }
        else if (source.Contains("Preflight", StringComparison.OrdinalIgnoreCase))
        {
            action = "Apply the failed-check remediation from preflight report.";
        }

        LastUserFacingError = new UserFacingError(errorCode, category, title, action, detail);
        return LastUserFacingError;
    }

    private (string ParserMode, string ParserStage, string PrimaryError) GetLoadFailureContext()
    {
        var parserMode = string.IsNullOrWhiteSpace(_sessionPersistence.Sidecar.ParserMode)
            ? "sidecar"
            : _sessionPersistence.Sidecar.ParserMode.Trim();
        var info = _sessionService.LastLoadAttemptInfo;
        if (!info.HasValue)
        {
            return (parserMode, "unknown", "NONE");
        }

        var parserStage = string.IsNullOrWhiteSpace(info.Value.ParserStage)
            ? "unknown"
            : info.Value.ParserStage.Trim();
        var primaryError = string.IsNullOrWhiteSpace(info.Value.PrimaryErrorCode)
            ? "NONE"
            : info.Value.PrimaryErrorCode.Trim();
        return (parserMode, parserStage, primaryError);
    }

    private static string AppendLoadContextDetail(string detail, string parserMode, string parserStage, string primaryError)
    {
        var context = $"load_context: parser_mode={parserMode}, parser_stage={parserStage}, primary_error={primaryError}";
        if (string.IsNullOrWhiteSpace(detail))
        {
            return context;
        }
        return $"{context}{Environment.NewLine}{detail}";
    }

    private void TrackTelemetryEvent(string source, NcResultCode rc)
    {
        _telemetry.Track(
            "host_operation",
            new Dictionary<string, object?>
            {
                ["source"] = source,
                ["result_code"] = rc.ToString(),
                ["session_initialized"] = SessionState.IsInitialized,
                ["avatar_loaded"] = SessionState.ActiveAvatarHandle.HasValue,
                ["spout_active"] = Outputs.SpoutActive,
                ["osc_active"] = Outputs.OscActive,
                ["session_started_at"] = ToIso8601(_sessionStartedAtUtc),
                ["initialized_at"] = ToIso8601(_initializedAtUtc),
                ["avatar_loaded_at"] = ToIso8601(_avatarLoadedAtUtc),
                ["output_started_at"] = ToIso8601(_outputStartedAtUtc),
                ["within_3min_success"] = _within3MinSuccess,
            });
    }

    public void TrackOnboardingUiEvent(
        string eventName,
        HostOnboardingStep step,
        HostPrimaryActionKind primaryAction,
        HostActionability actionability,
        string reason)
    {
        var normalizedName = string.IsNullOrWhiteSpace(eventName)
            ? "onboarding_ui_event"
            : eventName.Trim();
        _telemetry.Track(
            normalizedName,
            new Dictionary<string, object?>
            {
                ["step"] = step.ToString(),
                ["primary_action"] = primaryAction.ToString(),
                ["actionability"] = actionability.ToString(),
                ["reason"] = string.IsNullOrWhiteSpace(reason) ? "none" : reason.Trim(),
                ["session_started_at"] = ToIso8601(_sessionStartedAtUtc),
                ["initialized_at"] = ToIso8601(_initializedAtUtc),
                ["avatar_loaded_at"] = ToIso8601(_avatarLoadedAtUtc),
                ["output_started_at"] = ToIso8601(_outputStartedAtUtc),
                ["within_3min_success"] = _within3MinSuccess,
            });
    }

    private void MarkOnboardingInitialized()
    {
        _initializedAtUtc ??= DateTimeOffset.UtcNow;
        TrackOnboardingMilestone("initialized");
    }

    private void MarkOnboardingAvatarLoaded()
    {
        _avatarLoadedAtUtc ??= DateTimeOffset.UtcNow;
        TrackOnboardingMilestone("avatar_loaded");
    }

    private void MarkOnboardingOutputStarted(string outputType)
    {
        _outputStartedAtUtc ??= DateTimeOffset.UtcNow;
        _within3MinSuccess = IsWithin3Minutes();
        TrackOnboardingMilestone($"output_started:{outputType}");
    }

    private bool IsWithin3Minutes()
    {
        if (!_sessionStartedAtUtc.HasValue || !_outputStartedAtUtc.HasValue)
        {
            return false;
        }

        return (_outputStartedAtUtc.Value - _sessionStartedAtUtc.Value) <= TimeSpan.FromMinutes(3);
    }

    private void TrackOnboardingMilestone(string milestone)
    {
        _telemetry.Track(
            "onboarding_milestone",
            new Dictionary<string, object?>
            {
                ["milestone"] = milestone,
                ["session_started_at"] = ToIso8601(_sessionStartedAtUtc),
                ["initialized_at"] = ToIso8601(_initializedAtUtc),
                ["avatar_loaded_at"] = ToIso8601(_avatarLoadedAtUtc),
                ["output_started_at"] = ToIso8601(_outputStartedAtUtc),
                ["within_3min_success"] = _within3MinSuccess,
            });
    }

    private static string? ToIso8601(DateTimeOffset? value)
    {
        return value?.ToString("O");
    }

    private void PersistSessionSnapshot()
    {
        _sessionPersistence = _sessionPersistence with
        {
            SpoutChannelName = Outputs.SpoutChannelName,
            OscBindPort = Outputs.OscBindPort,
            OscPublishAddress = Outputs.OscPublishAddress,
            LastUpdatedUtc = DateTimeOffset.UtcNow,
        };

        try
        {
            _sessionStore.Save(_sessionPersistence);
        }
        catch
        {
            // Best-effort persistence only.
        }
    }

    private static string NormalizeUiSection(string? value)
    {
        var normalized = value?.Trim().ToLowerInvariant();
        return normalized switch
        {
            "getting_started" => "getting_started",
            "session_avatar" => "session_avatar",
            "render" => "render",
            "outputs" => "outputs",
            "tracking" => "tracking",
            "platform_ops" => "platform_ops",
            _ => "getting_started",
        };
    }

    private static string NormalizeThemeMode(string? value)
    {
        return string.Equals(value?.Trim(), "dark", StringComparison.OrdinalIgnoreCase)
            ? "dark"
            : "light";
    }

    private static IReadOnlyList<string> EnumerateLocalIpv4Candidates()
    {
        var candidates = new List<string>();
        var dedupe = new HashSet<string>(StringComparer.Ordinal);

        try
        {
            foreach (var nic in NetworkInterface.GetAllNetworkInterfaces())
            {
                if (nic.OperationalStatus != OperationalStatus.Up ||
                    nic.NetworkInterfaceType == NetworkInterfaceType.Loopback ||
                    nic.NetworkInterfaceType == NetworkInterfaceType.Tunnel)
                {
                    continue;
                }

                IPInterfaceProperties ipProperties;
                try
                {
                    ipProperties = nic.GetIPProperties();
                }
                catch
                {
                    continue;
                }

                foreach (var unicast in ipProperties.UnicastAddresses)
                {
                    var address = unicast.Address;
                    if (address.AddressFamily != AddressFamily.InterNetwork)
                    {
                        continue;
                    }

                    var text = address.ToString();
                    if (string.IsNullOrWhiteSpace(text) || text == "0.0.0.0")
                    {
                        continue;
                    }

                    if (dedupe.Add(text))
                    {
                        candidates.Add(text);
                    }
                }
            }
        }
        catch
        {
            // Best effort only.
        }

        return candidates;
    }

    private void ApplySidecarEnvironment(SidecarSettings settings)
    {
        if (!string.IsNullOrWhiteSpace(settings.ParserMode))
        {
            Environment.SetEnvironmentVariable("VSF_PARSER_MODE", settings.ParserMode);
        }
        if (!string.IsNullOrWhiteSpace(settings.SidecarPath))
        {
            Environment.SetEnvironmentVariable("VSF_SIDECAR_PATH", settings.SidecarPath);
        }

        Environment.SetEnvironmentVariable("VSF_SIDECAR_TIMEOUT_MS", settings.TimeoutMs.ToString());
    }

    private static bool DetectDotnet8Sdk()
    {
        try
        {
            var psi = new ProcessStartInfo("dotnet", "--list-sdks")
            {
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true,
            };
            using var process = Process.Start(psi);
            if (process is null)
            {
                return false;
            }

            var output = process.StandardOutput.ReadToEnd();
            process.WaitForExit(3000);
            return output.Split('\n').Any(line => line.TrimStart().StartsWith("8.", StringComparison.Ordinal));
        }
        catch
        {
            return false;
        }
    }

    private void PublishLoadProgress(string operationId, string stage, int percent, string message, bool terminal)
    {
        var normalized = new LoadProgressState(
            operationId,
            stage,
            Math.Clamp(percent, 0, 100),
            message,
            terminal);
        LoadProgressChanged?.Invoke(this, normalized);
    }
}
