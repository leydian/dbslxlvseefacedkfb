using System.Diagnostics;
using System.IO.Compression;
using System.Text;
using System.Text.Json;

namespace HostCore;

public sealed partial class HostController
{
    private static readonly JsonSerializerOptions FeatureJsonOptions = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = true,
    };

    private readonly SessionStateStore _sessionStore = new();
    private readonly AutoQualityPolicyStore _autoQualityStore = new();
    private readonly TelemetryService _telemetry = new();
    private readonly List<FrameMetric> _rollingMetrics = new();
    private DateTimeOffset _lastAutoQualityAdjustUtc = DateTimeOffset.MinValue;
    private int _highFrameCount;
    private CancellationTokenSource? _loadCancellation;
    private Task<NcResultCode>? _activeLoadTask;
    private SessionPersistenceModel _sessionPersistence = SessionPersistenceModel.CreateDefault();
    private AutoQualityPolicy _autoQualityPolicy = AutoQualityPolicy.CreateDefault();

    public PreflightSummary? LastPreflight { get; private set; }
    public UserFacingError? LastUserFacingError { get; private set; }
    public event EventHandler<LoadProgressState>? LoadProgressChanged;

    public SessionPersistenceModel SessionPersistence => _sessionPersistence;

    public void InitializeMvpFeatures()
    {
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
    }

    public string BuildImportGuidance(string path)
    {
        var trimmed = path?.Trim() ?? string.Empty;
        if (string.IsNullOrWhiteSpace(trimmed))
        {
            return "Select an avatar file path first.";
        }
        if (!File.Exists(trimmed))
        {
            return "Selected avatar path does not exist. Verify path and try again.";
        }

        var ext = Path.GetExtension(trimmed).ToLowerInvariant();
        return ext switch
        {
            ".vrm" => "VRM route selected: runtime-ready mesh/material path. Fallback: convert to XAV2 if runtime compatibility warnings appear.",
            ".vxavatar" => "VXAvatar route selected: MVP container parser. Fallback: export to VXA2/XAV2 path when available.",
            ".vxa2" => "VXA2 route selected: manifest + TLV decode path. Fallback: validate package and retry with XAV2.",
            ".xav2" => "XAV2 route selected: vxa2-derived runtime container. Recommended for deterministic runtime loading.",
            ".vsfavatar" => $"VSFAvatar route selected: parser mode={_sessionPersistence.Sidecar.ParserMode}. Fallback behavior follows current parser policy.",
            _ => "Unknown extension. Supported: .vrm, .vxavatar, .vxa2, .xav2, .vsfavatar",
        };
    }

    public PreflightSummary RunPreflight()
    {
        var checks = new List<PreflightCheckResult>();

        var localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        checks.Add(new PreflightCheckResult(
            "LOCAL_APPDATA",
            !string.IsNullOrWhiteSpace(localAppData),
            string.IsNullOrWhiteSpace(localAppData) ? "LocalAppData path unavailable." : localAppData,
            "Ensure user profile has a writable LocalAppData folder."));

        var sdk8Detected = DetectDotnet8Sdk();
        checks.Add(new PreflightCheckResult(
            "DOTNET_8_SDK",
            sdk8Detected,
            sdk8Detected ? "Detected .NET 8 SDK." : "Could not detect .NET 8 SDK via dotnet --list-sdks.",
            "Install .NET 8 SDK (for host tooling and publish flows)."));

        var nativeDllCandidate = Path.Combine(AppContext.BaseDirectory, "nativecore.dll");
        checks.Add(new PreflightCheckResult(
            "NATIVECORE_DLL",
            File.Exists(nativeDllCandidate),
            File.Exists(nativeDllCandidate) ? nativeDllCandidate : "nativecore.dll not found near host executable.",
            "Build nativecore and place/copy nativecore.dll next to host executable."));

        var sidecarMode = _sessionPersistence.Sidecar.ParserMode;
        checks.Add(new PreflightCheckResult(
            "VSF_PARSER_MODE",
            sidecarMode is "sidecar" or "inhouse" or "sidecar-strict",
            $"Current mode: {sidecarMode}",
            "Set parser mode to sidecar, inhouse, or sidecar-strict."));

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

        File.WriteAllText(snapshotPath, JsonSerializer.Serialize(LastSnapshot, FeatureJsonOptions), Encoding.UTF8);
        File.WriteAllText(logsPath, string.Join(Environment.NewLine, LogEntries.Select(x => $"{x.TimestampUtc:O} [{x.Source}] {x.ResultCode} {x.Message}")), Encoding.UTF8);
        File.WriteAllText(preflightPath, JsonSerializer.Serialize(LastPreflight ?? RunPreflight(), FeatureJsonOptions), Encoding.UTF8);
        File.WriteAllText(quickstartPath, HostContent.BuildQuickstartText(), Encoding.UTF8);
        File.WriteAllText(compatibilityPath, HostContent.BuildCompatibilityText(), Encoding.UTF8);
        _ = _telemetry.Export(telemetryPath);

        var zipPath = Path.Combine(root, $"diagnostics_bundle_{timestamp}.zip");
        if (File.Exists(zipPath))
        {
            File.Delete(zipPath);
        }

        ZipFile.CreateFromDirectory(tempDir, zipPath, CompressionLevel.Optimal, false);
        AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "DiagnosticsBundle", zipPath, NcResultCode.Ok), false);
        return zipPath;
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
        sb.AppendLine("timestamp_utc,frame_ms,render_ready_avatar_count,spout_active,osc_active");
        foreach (var item in _rollingMetrics)
        {
            sb.AppendLine($"{item.TimestampUtc:O},{item.FrameMs:F3},{item.RenderReadyAvatarCount},{item.SpoutActive},{item.OscActive}");
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
            _ => current with { BroadcastMode = true, CameraMode = RenderCameraMode.AutoFitBust, FramingTarget = 0.80f, Headroom = 0.10f, FovDeg = 40.0f, ShowDebugOverlay = false },
        };

        _sessionPersistence = _sessionPersistence with { LastProfileName = normalized };
        PersistSessionSnapshot();
        return ApplyRenderUiState(next);
    }

    public SidecarSettings GetSidecarSettings() => _sessionPersistence.Sidecar;

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
            Math.Clamp(policy.CooldownSeconds, 5, 300));
        _autoQualityStore.Save(_autoQualityPolicy);
        AddLog(
            new HostLogEntry(
                DateTimeOffset.UtcNow,
                "AutoQualityPolicy",
                $"threshold_ms={_autoQualityPolicy.HighFrameMsThreshold:F1}, consecutive={_autoQualityPolicy.ConsecutiveFrameLimit}, cooldown_sec={_autoQualityPolicy.CooldownSeconds}",
                NcResultCode.Ok),
            false);
    }

    public string GetLastErrorGuidance()
    {
        if (LastUserFacingError is null)
        {
            return string.Empty;
        }

        return $"{LastUserFacingError.Title} | {LastUserFacingError.ActionHint}";
    }

    public Task<NcResultCode> LoadAvatarAsync(string path, int timeoutMs)
    {
        if (_activeLoadTask is { IsCompleted: false })
        {
            return _activeLoadTask;
        }

        _loadCancellation?.Dispose();
        _loadCancellation = new CancellationTokenSource();
        var token = _loadCancellation.Token;
        AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "LoadAvatarAsync", $"started timeout_ms={timeoutMs}", NcResultCode.Ok), false);
        PublishLoadProgress("queued", 5, "Load queued.", false);

        _activeLoadTask = Task.Run(async () =>
        {
            try
            {
                PublishLoadProgress("validating", 15, "Validating avatar path and operation state.", false);
                var worker = Task.Run(() => LoadAvatar(path), token);
                PublishLoadProgress("loading", 70, "Loading avatar package and runtime payload.", false);
                var rc = await worker.WaitAsync(TimeSpan.FromMilliseconds(Math.Max(1000, timeoutMs)));
                PublishLoadProgress("finalizing", 95, "Finalizing avatar runtime state.", false);
                PublishLoadProgress("completed", rc == NcResultCode.Ok ? 100 : 0, rc == NcResultCode.Ok ? "Load completed." : $"Load failed: {rc}", true);
                return rc;
            }
            catch (OperationCanceledException)
            {
                AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "LoadAvatarAsync", "cancel requested", NcResultCode.Internal), false);
                PublishLoadProgress("canceled", 0, "Load canceled.", true);
                return NcResultCode.Internal;
            }
            catch (TimeoutException)
            {
                AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "LoadAvatarAsync", "timeout exceeded", NcResultCode.Internal), true);
                PublishLoadProgress("timeout", 0, "Load timed out.", true);
                return NcResultCode.Internal;
            }
            finally
            {
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
        AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "LoadAvatarAsync", "cancel requested by operator", NcResultCode.Ok), false);
        PublishLoadProgress("canceling", 0, "Cancel requested.", false);
    }

    private void RecordFrameMetricAndGuardrails()
    {
        var statsRc = NativeCoreInterop.nc_get_runtime_stats(out var stats);
        if (statsRc != NcResultCode.Ok)
        {
            return;
        }

        _rollingMetrics.Add(new FrameMetric(
            DateTimeOffset.UtcNow,
            stats.LastFrameMs,
            stats.RenderReadyAvatarCount,
            stats.SpoutActive != 0U,
            stats.OscActive != 0U));
        if (_rollingMetrics.Count > 1800)
        {
            _rollingMetrics.RemoveRange(0, _rollingMetrics.Count - 1800);
        }

        if (stats.LastFrameMs > _autoQualityPolicy.HighFrameMsThreshold)
        {
            _highFrameCount++;
        }
        else
        {
            _highFrameCount = Math.Max(0, _highFrameCount - 1);
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
        _ = ApplyRenderProfile("performance");
        AddLog(new HostLogEntry(DateTimeOffset.UtcNow, "AutoQualityGuard", "applied performance profile due to sustained frame time pressure", NcResultCode.Ok), false);
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

    private UserFacingError BuildUserFacingError(string source, NcResultCode rc, string detail)
    {
        if (source.EndsWith(".Blocked", StringComparison.OrdinalIgnoreCase))
        {
            LastUserFacingError = new UserFacingError(
                HostErrorCategory.Initialization,
                "Operation blocked by lifecycle policy",
                "Follow the operation order: Initialize -> Load Avatar -> Start Outputs.",
                detail);
            return LastUserFacingError;
        }

        var category = rc switch
        {
            NcResultCode.NotInitialized => HostErrorCategory.Initialization,
            NcResultCode.InvalidArgument => HostErrorCategory.InputValidation,
            NcResultCode.Io => HostErrorCategory.FileIo,
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
            action = "Check avatar file extension/path, then retry. If it fails again, export diagnostics.";
        }
        else if (source.Contains("StartSpout", StringComparison.OrdinalIgnoreCase) ||
                 source.Contains("StartOsc", StringComparison.OrdinalIgnoreCase))
        {
            action = "Verify output settings and active avatar state, then restart outputs.";
        }
        else if (source.Contains("Preflight", StringComparison.OrdinalIgnoreCase))
        {
            action = "Apply the failed-check remediation from preflight report.";
        }

        LastUserFacingError = new UserFacingError(category, title, action, detail);
        return LastUserFacingError;
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
            });
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

    private void PublishLoadProgress(string stage, int percent, string message, bool terminal)
    {
        var normalized = new LoadProgressState(
            stage,
            Math.Clamp(percent, 0, 100),
            message,
            terminal);
        LoadProgressChanged?.Invoke(this, normalized);
    }
}
