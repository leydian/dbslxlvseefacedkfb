using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Diagnostics.CodeAnalysis;

namespace HostCore;

public sealed class TrackingInputService : ITrackingInputService
{
    private const ushort DefaultListenPort = 49983;
    private const int DefaultStaleTimeoutMs = 500;
    private const int NoActiveInputWarnDelayMs = 3000;
    private const int NoPacketRebindThresholdMs = 5000;
    private const int NoPacketRebindCooldownMs = 3000;
    private const int ReceiveWatchdogPollMs = 250;
    private const int CalibrationWarmupFrames = 90;
    private const int LatencySampleWindow = 240;

    private readonly object _sync = new();
    private readonly Dictionary<string, float> _expressionCache = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, float> _expressionSnapshot = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, int> _ifmAcceptedKeyCounts = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, int> _ifmDroppedKeyCounts = new(StringComparer.OrdinalIgnoreCase);

    private UdpClient? _udpClient;
    private CancellationTokenSource? _cts;
    private Task? _receiveTask;
    private Task? _receiveWatchdogTask;
    private TrackingStartOptions _options = new(DefaultListenPort, DefaultStaleTimeoutMs, TrackingSourceType.HybridAuto, string.Empty, 30, 10, 10, TrackingSourceLockMode.Auto, TrackingLatencyProfile.Balanced);
    private TrackingDiagnostics _diagnostics = new(false, "unknown", 0.0, 0.0, 0.0, int.MaxValue, true, 0, 0, 0, "stopped", TrackingSourceType.HybridAuto, "idle");

    private DateTimeOffset _lastPacketUtc = DateTimeOffset.MinValue;
    private DateTimeOffset _lastFpsSampleUtc = DateTimeOffset.MinValue;
    private DateTimeOffset _lastCaptureSampleUtc = DateTimeOffset.MinValue;
    private int _fpsSampleCount;
    private int _captureSampleCount;
    private double _smoothedInputFps;
    private double _smoothedCaptureFps;
    private double _smoothedInferenceMs;
    private double _smoothedCaptureStageMs;
    private double _smoothedParseStageMs;
    private double _smoothedSmoothStageMs;
    private double _smoothedSubmitStageMs;
    private readonly Queue<double> _latencySamples = new();
    private readonly List<double> _latencySortedSamples = new(LatencySampleWindow);
    private double _latencySampleSum;
    private double _latencyAvgMs;
    private double _latencyP95Ms;
    private string _switchBlockedReason = string.Empty;
    private string _udpBindMode = "unknown";
    private DateTimeOffset _lastUdpRebindAttemptUtc = DateTimeOffset.MinValue;
    private int _receiveLoopRestartCount;

    private NcTrackingFrame _rawFrame = BuildNeutralFrame();
    private NcTrackingFrame _smoothedFrame = BuildNeutralFrame();
    private NcTrackingFrame _lastOutputFrame = BuildNeutralFrame();
    private bool _hasFrame;
    private bool _hasSmoothedFrame;
    private bool _hasHeadYpr;
    private float _rawHeadYaw;
    private float _rawHeadPitch;
    private float _rawHeadRoll;
    private float _smoothedHeadYaw;
    private float _smoothedHeadPitch;
    private float _smoothedHeadRoll;

    private float _headPosOffsetX;
    private float _headPosOffsetY;
    private float _headPosOffsetZ;
    private bool _hasRefYpr;
    private float _refYaw;
    private float _refPitch;
    private float _refRoll;
    private Process? _mediapipeProcess;
    private Task? _mediapipeStdoutTask;
    private Task? _mediapipeStderrTask;
    private MediapipeFramePacket? _latestMediapipePacket;
    private bool _hasMediapipePacket;
    private string _latestMediapipeError = string.Empty;
    private long _lastMediapipeFrameId;
    private DateTimeOffset _lastIfacialPacketUtc = DateTimeOffset.MinValue;
    private DateTimeOffset _lastWebcamPacketUtc = DateTimeOffset.MinValue;
    private TrackingRuntimeSource _activeRuntimeSource = TrackingRuntimeSource.None;
    private int _fallbackCount;
    private int _ifacialRecoveryStreak;
    private int _ifacialConsecutiveFailures;
    private int _ifacialFallbackAgeMs = 650;
    private int _ifacialRecoveryAgeMs = 180;
    private int _ifacialRecoveryStreakRequired = 10;
    private int _calibrationFrames;
    private readonly Dictionary<string, float> _calibrationBaseline = new(StringComparer.OrdinalIgnoreCase);
    private string _calibrationState = "idle";
    private float _poseAlpha = 0.35f;
    private float _expressionAlpha = 0.60f;
    private float _poseDeadbandDeg = 0.9f;
    private float _poseAdaptiveMinAlpha = 0.18f;
    private float _poseAdaptiveMaxAlpha = 0.52f;
    private float _poseAdaptiveGain = 0.024f;
    private bool _upperBodyEnabled = true;
    private float _upperBodyStrength = 1.0f;
    private UpperBodySmoothingProfile _upperBodySmoothing = UpperBodySmoothingProfile.Balanced;
    private float _upperBodyAlpha = 0.35f;
    private DateTimeOffset _lastUpperBodyPacketUtc = DateTimeOffset.MinValue;
    private DateTimeOffset _lastUpperBodyOscPacketUtc = DateTimeOffset.MinValue;
    private DateTimeOffset _lastUpperBodyWebcamPacketUtc = DateTimeOffset.MinValue;
    private float _rawLeftShoulderPitch;
    private float _rawRightShoulderPitch;
    private float _rawLeftUpperArmPitch;
    private float _rawRightUpperArmPitch;
    private float _smoothedLeftShoulderPitch;
    private float _smoothedRightShoulderPitch;
    private float _smoothedLeftUpperArmPitch;
    private float _smoothedRightUpperArmPitch;
    private bool _hasSmoothedUpperBody;
    private double _upperBodyConfidence;
    private string _upperBodyActiveSource = "none";
    private string _upperBodyStatus = "idle";
    private string _upperBodyLastError = string.Empty;
    private int _recenterStabilizeFramesRemaining;
    private DateTimeOffset _trackingStartedUtc = DateTimeOffset.MinValue;
    private bool _webcamRuntimeUnavailable;
    private bool _autoStabilityTuningEnabled = true;
    private DateTimeOffset _lastSourceSwitchUtc = DateTimeOffset.MinValue;
    private DateTimeOffset _activeSourceSinceUtc = DateTimeOffset.MinValue;
    private DateTimeOffset _sourceSwitchWindowStartUtc = DateTimeOffset.MinValue;
    private int _recentSourceSwitchCount;
    private string _lastSourceSwitchReason = string.Empty;
    private DateTimeOffset _sourceSwitchCooldownUntilUtc = DateTimeOffset.MinValue;
    private DateTimeOffset _lastStabilityTuneUtc = DateTimeOffset.MinValue;

    public NcResultCode Start(TrackingStartOptions options)
    {
        lock (_sync)
        {
            if (_receiveTask is not null)
            {
                return NcResultCode.Ok;
            }

            _options = options with
            {
                ListenPort = options.ListenPort == 0 ? DefaultListenPort : options.ListenPort,
                StaleTimeoutMs = Math.Clamp(options.StaleTimeoutMs <= 0 ? DefaultStaleTimeoutMs : options.StaleTimeoutMs, 50, 5000),
                SourceType = options.SourceType,
                CameraDeviceKey = options.CameraDeviceKey?.Trim() ?? string.Empty,
                InferenceFpsCap = Math.Clamp(options.InferenceFpsCap <= 0 ? 30 : options.InferenceFpsCap, 5, 120),
                ParseErrorWarnThreshold = Math.Clamp(options.ParseErrorWarnThreshold <= 0 ? 10 : options.ParseErrorWarnThreshold, 1, 10000),
                DroppedPacketWarnThreshold = Math.Clamp(options.DroppedPacketWarnThreshold <= 0 ? 10 : options.DroppedPacketWarnThreshold, 1, 10000),
                SourceLockMode = Enum.IsDefined(typeof(TrackingSourceLockMode), options.SourceLockMode)
                    ? options.SourceLockMode
                    : TrackingSourceLockMode.Auto,
                LatencyProfile = Enum.IsDefined(typeof(TrackingLatencyProfile), options.LatencyProfile)
                    ? options.LatencyProfile
                    : TrackingLatencyProfile.Balanced,
                AutoStabilityTuningEnabled = options.SourceType == TrackingSourceType.HybridAuto && options.AutoStabilityTuningEnabled,
                UpperBodyEnabled = options.UpperBodyEnabled,
                UpperBodyStrength = Math.Clamp(float.IsFinite(options.UpperBodyStrength) ? options.UpperBodyStrength : 1.0f, 0.0f, 1.5f),
                UpperBodySmoothing = Enum.IsDefined(typeof(UpperBodySmoothingProfile), options.UpperBodySmoothing)
                    ? options.UpperBodySmoothing
                    : UpperBodySmoothingProfile.Balanced,
            };
            ApplyLatencyProfileTuning(_options.LatencyProfile);
            ApplyPoseFilterTuning(_options.PoseFilterProfile, _options.PoseDeadbandDeg);
            ApplyUpperBodySmoothingTuning(_options.UpperBodySmoothing);
            _upperBodyEnabled = _options.UpperBodyEnabled;
            _upperBodyStrength = _options.UpperBodyStrength;
            _upperBodySmoothing = _options.UpperBodySmoothing;
            _autoStabilityTuningEnabled = _options.AutoStabilityTuningEnabled;
            ResetRuntimeState();
            _trackingStartedUtc = DateTimeOffset.UtcNow;
            _webcamRuntimeUnavailable = false;

            if (_options.SourceType == TrackingSourceType.WebcamMediapipe)
            {
                _cts = new CancellationTokenSource();
                var initRc = InitializeWebcamRuntime();
                if (initRc != NcResultCode.Ok)
                {
                    _cts.Dispose();
                    _cts = null;
                    return initRc;
                }

                _receiveTask = Task.Run(() => WebcamLoopAsync(_cts.Token));
                _activeRuntimeSource = TrackingRuntimeSource.Webcam;
                _activeSourceSinceUtc = DateTimeOffset.UtcNow;
                _diagnostics = _diagnostics with
                {
                    IsActive = true,
                    DetectedFormat = "webcam-mediapipe",
                    SourceStatus = BuildWebcamSourceStatus("starting"),
                    StatusMessage = "starting:webcam-mediapipe",
                    ActiveSource = "webcam",
                    ConfidenceSummary = BuildConfidenceSummary(),
                };
                return NcResultCode.Ok;
            }

            try
            {
                _udpClient = CreateUdpListener(_options.ListenPort, out _udpBindMode);
            }
            catch (Exception ex)
            {
                _diagnostics = _diagnostics with
                {
                    IsActive = false,
                    SourceType = _options.SourceType,
                    SourceStatus = $"bind-failed:{ex.Message}",
                    StatusMessage = $"bind failed: {ex.Message}",
                };
                return NcResultCode.Io;
            }

            _cts = new CancellationTokenSource();
            _receiveTask = Task.Run(() => ReceiveLoopAsync(_cts.Token));
            _receiveWatchdogTask = Task.Run(() => ReceiveWatchdogLoopAsync(_cts.Token));
            _activeRuntimeSource = TrackingRuntimeSource.Ifacial;
            _activeSourceSinceUtc = DateTimeOffset.UtcNow;
            _diagnostics = _diagnostics with
            {
                IsActive = true,
                SourceType = _options.SourceType,
                SourceStatus = $"udp-listening:{_options.ListenPort}:{_udpBindMode}",
                StatusMessage = $"listening:{_options.ListenPort} ({_udpBindMode})",
                ActiveSource = "ifacial",
                ConfidenceSummary = BuildConfidenceSummary(),
            };

            if (_options.SourceType != TrackingSourceType.HybridAuto)
            {
                _diagnostics = _diagnostics with
                {
                    SourceStatus = "ifacial-active",
                    StatusMessage = $"listening:{_options.ListenPort} ({_udpBindMode})",
                };
                return NcResultCode.Ok;
            }

            // Hybrid mode: start webcam sidecar as fallback path, but do not fail start when unavailable.
            var fallbackRc = InitializeWebcamRuntime();
            if (fallbackRc == NcResultCode.Ok)
            {
                _ = Task.Run(() => WebcamLoopAsync(_cts.Token));
                _diagnostics = _diagnostics with
                {
                    IsActive = true,
                    SourceStatus = "ifacial-active:webcam-fallback-ready",
                    StatusMessage = $"listening:{_options.ListenPort} ({_udpBindMode}); fallback=webcam-ready",
                };
            }
            else
            {
                _webcamRuntimeUnavailable = true;
                _options = _options with
                {
                    SourceType = TrackingSourceType.OscIfacial,
                    SourceLockMode = TrackingSourceLockMode.IfacialLocked,
                    AutoStabilityTuningEnabled = false,
                };
                _diagnostics = _diagnostics with
                {
                    IsActive = true,
                    SourceType = TrackingSourceType.OscIfacial,
                    SourceStatus = "ifacial-active:webcam-fallback-disabled",
                    StatusMessage = $"listening:{_options.ListenPort} ({_udpBindMode}); fallback=disabled(webcam-runtime-unavailable)",
                    LastErrorCode = string.Empty,
                    ActiveSource = "ifacial",
                };
            }
            return NcResultCode.Ok;
        }
    }

    private static UdpClient CreateUdpListener(ushort listenPort, out string bindMode)
    {
        try
        {
            var ipv4 = new UdpClient(AddressFamily.InterNetwork);
            ipv4.Client.ExclusiveAddressUse = false;
            ipv4.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
            ipv4.Client.ReceiveTimeout = ReceiveWatchdogPollMs;
            ipv4.Client.Bind(new IPEndPoint(IPAddress.Any, listenPort));
            bindMode = "udp4";
            return ipv4;
        }
        catch (SocketException)
        {
            // Fallback to dual-stack listener for environments where IPv4-only bind fails.
        }

        try
        {
            var dualStack = new UdpClient(AddressFamily.InterNetworkV6);
            dualStack.Client.ExclusiveAddressUse = false;
            dualStack.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
            dualStack.Client.DualMode = true;
            dualStack.Client.ReceiveTimeout = ReceiveWatchdogPollMs;
            dualStack.Client.Bind(new IPEndPoint(IPAddress.IPv6Any, listenPort));
            bindMode = "udp6-dual";
            return dualStack;
        }
        catch (SocketException)
        {
            // Fall through to final bind attempt.
        }

        var finalIpv4 = new UdpClient(AddressFamily.InterNetwork);
        finalIpv4.Client.ExclusiveAddressUse = false;
        finalIpv4.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
        finalIpv4.Client.ReceiveTimeout = ReceiveWatchdogPollMs;
        finalIpv4.Client.Bind(new IPEndPoint(IPAddress.Any, listenPort));
        bindMode = "udp4-final";
        return finalIpv4;
    }

    public NcResultCode Stop()
    {
        lock (_sync)
        {
            _cts?.Cancel();
            _cts?.Dispose();
            _cts = null;

            try
            {
                _udpClient?.Close();
            }
            catch
            {
                // Best effort close.
            }

            _udpClient = null;
            _receiveTask = null;
            _receiveWatchdogTask = null;
            DisposeWebcamRuntime();
            _activeRuntimeSource = TrackingRuntimeSource.None;
            _diagnostics = _diagnostics with
            {
                IsActive = false,
                LastPacketAgeMs = int.MaxValue,
                IsStale = true,
                SourceStatus = "stopped",
                StatusMessage = "stopped",
                ActiveSource = "none",
                IfacialPacketAgeMs = int.MaxValue,
                WebcamPacketAgeMs = int.MaxValue,
                ConfidenceSummary = BuildConfidenceSummary(),
                UpperBodyTrackingActive = false,
                UpperBodyConfidence = 0.0,
                UpperBodyPacketAgeMs = int.MaxValue,
                UpperBodyActiveSource = "none",
                UpperBodyStatus = _upperBodyEnabled ? "stopped" : "disabled",
                UpperBodyLastError = string.Empty,
            };
            return NcResultCode.Ok;
        }
    }

    public NcResultCode Recenter()
    {
        lock (_sync)
        {
            if (!_hasFrame)
            {
                return NcResultCode.InvalidArgument;
            }

            _headPosOffsetX = _smoothedFrame.HeadPosX;
            _headPosOffsetY = _smoothedFrame.HeadPosY;
            _headPosOffsetZ = _smoothedFrame.HeadPosZ;
            if (_hasHeadYpr)
            {
                _hasRefYpr = true;
                _refYaw = _smoothedHeadYaw;
                _refPitch = _smoothedHeadPitch;
                _refRoll = _smoothedHeadRoll;
            }

            _diagnostics = _diagnostics with { StatusMessage = "recentered" };
            _recenterStabilizeFramesRemaining = 10;
            return NcResultCode.Ok;
        }
    }

    public bool TryGetLatestFrame(out NcTrackingFrame frame)
    {
        lock (_sync)
        {
            var submitWatch = Stopwatch.StartNew();
            if (!_hasFrame)
            {
                frame = BuildNeutralFrame();
                return false;
            }

            var ageMs = _lastPacketUtc == DateTimeOffset.MinValue
                ? int.MaxValue
                : (int)Math.Max(0.0, (DateTimeOffset.UtcNow - _lastPacketUtc).TotalMilliseconds);
            var ifacialAgeMs = GetPacketAgeMs(_lastIfacialPacketUtc);
            var webcamAgeMs = GetPacketAgeMs(_lastWebcamPacketUtc);
            var stale = ageMs > _options.StaleTimeoutMs;
            EvaluateSourceArbitration(ageMs);
            var now = DateTimeOffset.UtcNow;
            MaybeApplyAutoStabilityTuning(now);
            submitWatch.Stop();
            UpdateSubmitStageMs(submitWatch.Elapsed.TotalMilliseconds);
            if (!stale)
            {
                RecordLatencySample(_smoothedCaptureStageMs + _smoothedParseStageMs + _smoothedSmoothStageMs + _smoothedSubmitStageMs + ageMs);
            }
            _diagnostics = _diagnostics with
            {
                LastPacketAgeMs = ageMs,
                IsStale = stale,
                InputFps = _smoothedInputFps,
                CaptureFps = _smoothedCaptureFps,
                InferenceMsAvg = _smoothedInferenceMs,
                SourceStatus = stale ? "stale-reset-to-neutral" : _diagnostics.SourceStatus,
                StatusMessage = stale ? "stale (reset to neutral)" : _diagnostics.StatusMessage,
                ActiveSource = _diagnostics.IsActive ? ToActiveSourceLabel(_activeRuntimeSource) : "none",
                FallbackCount = _fallbackCount,
                IfacialPacketAgeMs = ifacialAgeMs,
                WebcamPacketAgeMs = webcamAgeMs,
                CalibrationState = _calibrationState,
                ConfidenceSummary = BuildConfidenceSummary(),
                LatencyAvgMs = _latencyAvgMs,
                LatencyP95Ms = _latencyP95Ms,
                CaptureStageMs = _smoothedCaptureStageMs,
                ParseStageMs = _smoothedParseStageMs,
                SmoothStageMs = _smoothedSmoothStageMs,
                SubmitStageMs = _smoothedSubmitStageMs,
                SourceLockMode = _options.SourceLockMode,
                SwitchBlockedReason = _switchBlockedReason,
                PoseFilterProfile = _options.PoseFilterProfile,
                PoseDeadbandDeg = _poseDeadbandDeg,
                UpperBodyTrackingActive = _upperBodyEnabled && _upperBodyConfidence >= 0.12,
                UpperBodyConfidence = _upperBodyConfidence,
                UpperBodyPacketAgeMs = GetPacketAgeMs(_lastUpperBodyPacketUtc),
                UpperBodyActiveSource = ResolveUpperBodyActiveSourceLabel(),
                UpperBodyStatus = _upperBodyStatus,
                UpperBodyLastError = _upperBodyLastError,
                RecentSourceSwitchCount = GetRecentSourceSwitchCount(now),
                LastSourceSwitchReason = _lastSourceSwitchReason,
                SourceSwitchCooldownRemainingMs = _sourceSwitchCooldownUntilUtc <= now
                    ? 0
                    : Math.Max(0, (int)(_sourceSwitchCooldownUntilUtc - now).TotalMilliseconds),
                IfmAcceptedKeySample = BuildIfmKeySampleSummary(mapped: true),
                IfmDroppedKeySample = BuildIfmKeySampleSummary(mapped: false),
            };
            ApplyNoInputWarningIfNeeded(ifacialAgeMs, webcamAgeMs);
            ApplySourceSwitchWarningIfNeeded(now);

            frame = stale ? BuildNeutralFrame() : _lastOutputFrame;
            return true;
        }
    }

    public bool TryGetLatestExpressionWeights(out IReadOnlyDictionary<string, float> weights)
    {
        lock (_sync)
        {
            if (_expressionSnapshot.Count == 0)
            {
                weights = new Dictionary<string, float>(StringComparer.OrdinalIgnoreCase);
                return false;
            }

            weights = new Dictionary<string, float>(_expressionSnapshot, StringComparer.OrdinalIgnoreCase);
            return true;
        }
    }

    public bool TryGetLatestUpperBodyPose(out TrackingUpperBodyPose pose)
    {
        lock (_sync)
        {
            if (!_upperBodyEnabled)
            {
                pose = TrackingUpperBodyPose.Neutral(int.MaxValue, "disabled");
                return false;
            }

            var preferred = ResolvePreferredUpperBodySource();
            var ageMs = preferred switch
            {
                TrackingRuntimeSource.Ifacial => GetPacketAgeMs(_lastUpperBodyOscPacketUtc),
                TrackingRuntimeSource.Webcam => GetPacketAgeMs(_lastUpperBodyWebcamPacketUtc),
                _ => int.MaxValue,
            };
            if (ageMs == int.MaxValue || ageMs > _options.StaleTimeoutMs)
            {
                // Hybrid fallback: use alternate fresh upper-body source when preferred source has no data.
                var fallback = preferred == TrackingRuntimeSource.Ifacial ? TrackingRuntimeSource.Webcam : TrackingRuntimeSource.Ifacial;
                var fallbackAge = fallback == TrackingRuntimeSource.Ifacial
                    ? GetPacketAgeMs(_lastUpperBodyOscPacketUtc)
                    : GetPacketAgeMs(_lastUpperBodyWebcamPacketUtc);
                if (fallbackAge <= _options.StaleTimeoutMs)
                {
                    preferred = fallback;
                    ageMs = fallbackAge;
                }
            }

            if (ageMs == int.MaxValue)
            {
                _upperBodyActiveSource = "none";
                pose = TrackingUpperBodyPose.Neutral(int.MaxValue, "no-upper-body-source");
                return false;
            }

            if (ageMs > _options.StaleTimeoutMs)
            {
                // Decay to neutral when fresh upper-body input is not available.
                var decayAlpha = _autoStabilityTuningEnabled ? 0.13f : 0.18f;
                _smoothedLeftShoulderPitch = Ema(_smoothedLeftShoulderPitch, 0.0f, decayAlpha);
                _smoothedRightShoulderPitch = Ema(_smoothedRightShoulderPitch, 0.0f, decayAlpha);
                _smoothedLeftUpperArmPitch = Ema(_smoothedLeftUpperArmPitch, 0.0f, decayAlpha);
                _smoothedRightUpperArmPitch = Ema(_smoothedRightUpperArmPitch, 0.0f, decayAlpha);
                var hasResidual = HasUpperBodyResidual();
                _upperBodyStatus = hasResidual ? "stale-decay" : "stale-neutral";
                _upperBodyConfidence = Math.Clamp(_upperBodyConfidence * (_autoStabilityTuningEnabled ? 0.88 : 0.82), 0.0, 1.0);
                _upperBodyActiveSource = "none";
                pose = new TrackingUpperBodyPose(
                    hasResidual,
                    _smoothedLeftShoulderPitch,
                    _smoothedRightShoulderPitch,
                    _smoothedLeftUpperArmPitch,
                    _smoothedRightUpperArmPitch,
                    _upperBodyConfidence,
                    ageMs,
                    _upperBodyStatus);
                return hasResidual;
            }

            if (_autoStabilityTuningEnabled && _upperBodyConfidence < 0.20)
            {
                // Low-confidence upper-body updates are softened to avoid visible twitching.
                _smoothedLeftShoulderPitch = Ema(_smoothedLeftShoulderPitch, 0.0f, 0.10f);
                _smoothedRightShoulderPitch = Ema(_smoothedRightShoulderPitch, 0.0f, 0.10f);
                _smoothedLeftUpperArmPitch = Ema(_smoothedLeftUpperArmPitch, 0.0f, 0.10f);
                _smoothedRightUpperArmPitch = Ema(_smoothedRightUpperArmPitch, 0.0f, 0.10f);
                _upperBodyStatus = "low-confidence-decay";
            }

            _upperBodyActiveSource = preferred == TrackingRuntimeSource.Ifacial ? "osc" : "webcam";
            _lastUpperBodyPacketUtc = preferred == TrackingRuntimeSource.Ifacial ? _lastUpperBodyOscPacketUtc : _lastUpperBodyWebcamPacketUtc;
            pose = new TrackingUpperBodyPose(
                _upperBodyConfidence >= 0.12,
                _smoothedLeftShoulderPitch,
                _smoothedRightShoulderPitch,
                _smoothedLeftUpperArmPitch,
                _smoothedRightUpperArmPitch,
                _upperBodyConfidence,
                ageMs,
                _upperBodyStatus);
            return pose.IsValid;
        }
    }

    public TrackingDiagnostics GetDiagnostics()
    {
        lock (_sync)
        {
            var ageMs = _lastPacketUtc == DateTimeOffset.MinValue
                ? int.MaxValue
                : (int)Math.Max(0.0, (DateTimeOffset.UtcNow - _lastPacketUtc).TotalMilliseconds);
            var ifacialAgeMs = GetPacketAgeMs(_lastIfacialPacketUtc);
            var webcamAgeMs = GetPacketAgeMs(_lastWebcamPacketUtc);
            var stale = ageMs > _options.StaleTimeoutMs;
            EvaluateSourceArbitration(ageMs);
            var now = DateTimeOffset.UtcNow;
            MaybeApplyAutoStabilityTuning(now);
            _diagnostics = _diagnostics with
            {
                LastPacketAgeMs = ageMs,
                IsStale = stale,
                InputFps = _smoothedInputFps,
                CaptureFps = _smoothedCaptureFps,
                InferenceMsAvg = _smoothedInferenceMs,
                SourceType = _options.SourceType,
                ActiveSource = _diagnostics.IsActive ? ToActiveSourceLabel(_activeRuntimeSource) : "none",
                FallbackCount = _fallbackCount,
                IfacialPacketAgeMs = ifacialAgeMs,
                WebcamPacketAgeMs = webcamAgeMs,
                CalibrationState = _calibrationState,
                ConfidenceSummary = BuildConfidenceSummary(),
                LatencyAvgMs = _latencyAvgMs,
                LatencyP95Ms = _latencyP95Ms,
                CaptureStageMs = _smoothedCaptureStageMs,
                ParseStageMs = _smoothedParseStageMs,
                SmoothStageMs = _smoothedSmoothStageMs,
                SubmitStageMs = _smoothedSubmitStageMs,
                SourceLockMode = _options.SourceLockMode,
                SwitchBlockedReason = _switchBlockedReason,
                PoseFilterProfile = _options.PoseFilterProfile,
                PoseDeadbandDeg = _poseDeadbandDeg,
                UpperBodyTrackingActive = _upperBodyEnabled && _upperBodyConfidence >= 0.12,
                UpperBodyConfidence = _upperBodyConfidence,
                UpperBodyPacketAgeMs = GetPacketAgeMs(_lastUpperBodyPacketUtc),
                UpperBodyActiveSource = ResolveUpperBodyActiveSourceLabel(),
                UpperBodyStatus = _upperBodyStatus,
                UpperBodyLastError = _upperBodyLastError,
                RecentSourceSwitchCount = GetRecentSourceSwitchCount(now),
                LastSourceSwitchReason = _lastSourceSwitchReason,
                SourceSwitchCooldownRemainingMs = _sourceSwitchCooldownUntilUtc <= now
                    ? 0
                    : Math.Max(0, (int)(_sourceSwitchCooldownUntilUtc - now).TotalMilliseconds),
            };
            ApplyNoInputWarningIfNeeded(ifacialAgeMs, webcamAgeMs);
            ApplySourceSwitchWarningIfNeeded(now);
            return _diagnostics;
        }
    }

    private void ResetRuntimeState()
    {
        _expressionCache.Clear();
        _expressionSnapshot.Clear();
        _ifmAcceptedKeyCounts.Clear();
        _ifmDroppedKeyCounts.Clear();
        _rawFrame = BuildNeutralFrame();
        _smoothedFrame = BuildNeutralFrame();
        _lastOutputFrame = BuildNeutralFrame();
        _hasFrame = false;
        _hasSmoothedFrame = false;
        _hasHeadYpr = false;
        _rawHeadYaw = 0.0f;
        _rawHeadPitch = 0.0f;
        _rawHeadRoll = 0.0f;
        _smoothedHeadYaw = 0.0f;
        _smoothedHeadPitch = 0.0f;
        _smoothedHeadRoll = 0.0f;
        _headPosOffsetX = 0.0f;
        _headPosOffsetY = 0.0f;
        _headPosOffsetZ = 0.0f;
        _hasRefYpr = false;
        _refYaw = 0.0f;
        _refPitch = 0.0f;
        _refRoll = 0.0f;
        _lastPacketUtc = DateTimeOffset.MinValue;
        _lastFpsSampleUtc = DateTimeOffset.MinValue;
        _lastCaptureSampleUtc = DateTimeOffset.MinValue;
        _fpsSampleCount = 0;
        _captureSampleCount = 0;
        _smoothedInputFps = 0.0;
        _smoothedCaptureFps = 0.0;
        _smoothedInferenceMs = 0.0;
        _smoothedCaptureStageMs = 0.0;
        _smoothedParseStageMs = 0.0;
        _smoothedSmoothStageMs = 0.0;
        _smoothedSubmitStageMs = 0.0;
        _latencySamples.Clear();
        _latencySortedSamples.Clear();
        _latencySampleSum = 0.0;
        _latencyAvgMs = 0.0;
        _latencyP95Ms = 0.0;
        _switchBlockedReason = string.Empty;
        _lastIfacialPacketUtc = DateTimeOffset.MinValue;
        _lastWebcamPacketUtc = DateTimeOffset.MinValue;
        _lastUpperBodyPacketUtc = DateTimeOffset.MinValue;
        _lastUpperBodyOscPacketUtc = DateTimeOffset.MinValue;
        _lastUpperBodyWebcamPacketUtc = DateTimeOffset.MinValue;
        _rawLeftShoulderPitch = 0.0f;
        _rawRightShoulderPitch = 0.0f;
        _rawLeftUpperArmPitch = 0.0f;
        _rawRightUpperArmPitch = 0.0f;
        _smoothedLeftShoulderPitch = 0.0f;
        _smoothedRightShoulderPitch = 0.0f;
        _smoothedLeftUpperArmPitch = 0.0f;
        _smoothedRightUpperArmPitch = 0.0f;
        _hasSmoothedUpperBody = false;
        _upperBodyConfidence = 0.0;
        _upperBodyActiveSource = "none";
        _upperBodyStatus = _upperBodyEnabled ? "initializing" : "disabled";
        _upperBodyLastError = string.Empty;
        _activeRuntimeSource = TrackingRuntimeSource.None;
        _fallbackCount = 0;
        _ifacialRecoveryStreak = 0;
        _ifacialConsecutiveFailures = 0;
        _calibrationFrames = 0;
        _calibrationBaseline.Clear();
        _calibrationState = "calibrating";
        _latestMediapipePacket = null;
        _hasMediapipePacket = false;
        _latestMediapipeError = string.Empty;
        _lastMediapipeFrameId = 0;
        _recenterStabilizeFramesRemaining = 0;
        _trackingStartedUtc = DateTimeOffset.MinValue;
        _webcamRuntimeUnavailable = false;
        _lastSourceSwitchUtc = DateTimeOffset.MinValue;
        _activeSourceSinceUtc = DateTimeOffset.UtcNow;
        _sourceSwitchWindowStartUtc = DateTimeOffset.MinValue;
        _recentSourceSwitchCount = 0;
        _lastSourceSwitchReason = string.Empty;
        _sourceSwitchCooldownUntilUtc = DateTimeOffset.MinValue;
        _lastStabilityTuneUtc = DateTimeOffset.MinValue;
        _lastUdpRebindAttemptUtc = DateTimeOffset.MinValue;
        _receiveLoopRestartCount = 0;
        _diagnostics = new TrackingDiagnostics(
            true,
            "unknown",
            0.0,
            0.0,
            0.0,
            int.MaxValue,
            true,
            0,
            0,
            0,
            "listening",
            _options.SourceType,
            "initializing",
            false,
            string.Empty,
            "none",
            0,
            _calibrationState,
            "ifacial=0.00,webcam=0.00",
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            _options.SourceLockMode,
            string.Empty,
            _options.PoseFilterProfile,
            _poseDeadbandDeg,
            RecentSourceSwitchCount: 0,
            LastSourceSwitchReason: string.Empty,
            SourceSwitchCooldownRemainingMs: 0);
    }

    private async Task ReceiveLoopAsync(CancellationToken token)
    {
        while (!token.IsCancellationRequested)
        {
            UdpReceiveResult result;
            try
            {
                UdpClient? activeClient;
                lock (_sync)
                {
                    activeClient = _udpClient;
                }

                if (activeClient is null)
                {
                    break;
                }

                var socket = activeClient.Client;
                if (!socket.Poll(ReceiveWatchdogPollMs * 1000, SelectMode.SelectRead))
                {
                    MarkReceiveTimeoutNoPacket();
                    TryRebindIfacialSocketWhenNoPackets();
                    continue;
                }

                if (socket.Available <= 0)
                {
                    MarkReceiveTimeoutNoPacket();
                    TryRebindIfacialSocketWhenNoPackets();
                    continue;
                }

                IPEndPoint remote = new(IPAddress.Any, 0);
                var buffer = activeClient.Receive(ref remote);
                result = new UdpReceiveResult(buffer, remote);
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (ObjectDisposedException)
            {
                break;
            }
            catch (Exception ex)
            {
                lock (_sync)
                {
                    _diagnostics = _diagnostics with { StatusMessage = $"receive failed: {ex.Message}" };
                }
                TryRebindIfacialSocketWhenNoPackets();
                await Task.Delay(10, CancellationToken.None);
                continue;
            }

            lock (_sync)
            {
                var receivedPackets = _diagnostics.ReceivedPackets + 1;
                _diagnostics = _diagnostics with { ReceivedPackets = receivedPackets };

                var parseWatch = Stopwatch.StartNew();
                if (!TryParsePacket(result.Buffer, out var updates, out var formatName, out var parseFailure))
                {
                parseWatch.Stop();
                UpdateParseStageMs(parseWatch.Elapsed.TotalMilliseconds);
                var packetSig = BuildPacketSignature(result.Buffer);
                var packetSigShort = BuildPacketSignatureShort(result.Buffer);
                var parseErrors = _diagnostics.ParseErrors + 1;
                var parseThresholdExceeded = parseErrors >= (ulong)_options.ParseErrorWarnThreshold;
                var (sourceStatus, statusMessage, lastErrorCode) = parseFailure switch
                {
                    PacketParseFailure.ProtocolMismatchVmc => (
                        parseThresholdExceeded ? "udp-parse-threshold-exceeded:vmc" : "udp-parse-failed:vmc",
                        $"packet parse failed (likely VMC protocol input) sig={packetSig}",
                        $"TRACKING_PROTOCOL_MISMATCH_VMC[{packetSigShort}]"),
                    PacketParseFailure.UnsupportedTypeTag => (
                        parseThresholdExceeded ? "udp-parse-threshold-exceeded:typetag" : "udp-parse-failed:typetag",
                        $"packet parse failed (unsupported OSC type tag) sig={packetSig}",
                        $"TRACKING_OSC_TYPE_UNSUPPORTED[{packetSigShort}]"),
                    PacketParseFailure.IfmMalformed => (
                        parseThresholdExceeded ? "udp-parse-threshold-exceeded:ifm-malformed" : "udp-parse-failed:ifm-malformed",
                        $"packet parse failed (malformed iFacial payload) sig={packetSig}",
                        $"TRACKING_IFM_MALFORMED[{packetSigShort}]"),
                    PacketParseFailure.IfmUnsupportedVersion => (
                        parseThresholdExceeded ? "udp-parse-threshold-exceeded:ifm-version" : "udp-parse-failed:ifm-version",
                        $"packet parse failed (unsupported iFacial interface version) sig={packetSig}",
                        $"TRACKING_IFM_UNSUPPORTED_VERSION[{packetSigShort}]"),
                    _ => (
                        parseThresholdExceeded ? "udp-parse-threshold-exceeded" : "udp-parse-failed",
                        $"packet parse failed sig={packetSig}",
                        parseThresholdExceeded
                            ? $"TRACKING_PARSE_THRESHOLD_EXCEEDED[{packetSigShort}]"
                            : $"TRACKING_PARSE_FAILED[{packetSigShort}]"),
                };
                _diagnostics = _diagnostics with
                {
                    ParseErrors = parseErrors,
                    DroppedPackets = _diagnostics.DroppedPackets + 1,
                    SourceStatus = sourceStatus,
                    StatusMessage = statusMessage,
                    LastErrorCode = lastErrorCode,
                };
                _ifacialConsecutiveFailures++;
                continue;
            }
                parseWatch.Stop();
                UpdateParseStageMs(parseWatch.Elapsed.TotalMilliseconds);

                if (!ApplyUpdates(updates))
                {
                _diagnostics = _diagnostics with
                {
                    DroppedPackets = _diagnostics.DroppedPackets + 1,
                    SourceStatus = (_diagnostics.DroppedPackets + 1 >= (ulong)_options.DroppedPacketWarnThreshold)
                        ? "udp-drop-threshold-exceeded"
                        : "udp-no-mapped-channels",
                    StatusMessage = "packet dropped (no mapped channels)",
                    LastErrorCode = (_diagnostics.DroppedPackets + 1 >= (ulong)_options.DroppedPacketWarnThreshold)
                        ? "TRACKING_DROP_THRESHOLD_EXCEEDED"
                        : "TRACKING_NO_MAPPED_CHANNELS",
                };
                _ifacialConsecutiveFailures++;
                continue;
            }

                _ifacialConsecutiveFailures = 0;
                _lastIfacialPacketUtc = DateTimeOffset.UtcNow;
                EvaluateSourceArbitration(0);
                if (ShouldConsumeIfacialFrame())
                {
                    _lastPacketUtc = _lastIfacialPacketUtc;
                    _hasFrame = true;
                    UpdateInputFps();
                    UpdateCaptureStageMs(0.0);
                    var smoothWatch = Stopwatch.StartNew();
                    ApplySmoothing();
                    smoothWatch.Stop();
                    UpdateSmoothStageMs(smoothWatch.Elapsed.TotalMilliseconds);
                    AdvanceCalibration();
                }
                var hadReceiveTimeout = _diagnostics.SourceStatus.StartsWith("receive-timeout", StringComparison.Ordinal) ||
                                        _diagnostics.SourceStatus.StartsWith("receive-rebind", StringComparison.Ordinal);
                _diagnostics = _diagnostics with
                {
                    DetectedFormat = formatName,
                    SourceStatus = _activeRuntimeSource == TrackingRuntimeSource.Ifacial
                        ? (hadReceiveTimeout ? "ifacial-active:receive-recovered" : "ifacial-active")
                        : "ifacial-recovering",
                    StatusMessage = hadReceiveTimeout ? $"receiving:{formatName}:recovered" : $"receiving:{formatName}",
                    LastErrorCode = string.Empty,
                    ActiveSource = ToActiveSourceLabel(_activeRuntimeSource),
                    FallbackCount = _fallbackCount,
                    CalibrationState = _calibrationState,
                    ConfidenceSummary = BuildConfidenceSummary(),
                };
            }
        }
    }

    private static string BuildPacketSignature(byte[] packet)
    {
        if (packet.Length == 0)
        {
            return "empty";
        }

        var take = Math.Min(packet.Length, 16);
        var hex = Convert.ToHexString(packet, 0, take);
        var asciiChars = new char[Math.Min(packet.Length, 32)];
        for (var i = 0; i < asciiChars.Length; i++)
        {
            var b = packet[i];
            asciiChars[i] = b >= 32 && b <= 126 ? (char)b : '.';
        }

        return $"len={packet.Length} hex={hex} ascii={new string(asciiChars)}";
    }

    private static string BuildPacketSignatureShort(byte[] packet)
    {
        if (packet.Length == 0)
        {
            return "L0";
        }

        var takeHex = Math.Min(packet.Length, 6);
        var hex = Convert.ToHexString(packet, 0, takeHex);
        var takeAscii = Math.Min(packet.Length, 8);
        Span<char> ascii = stackalloc char[takeAscii];
        for (var i = 0; i < takeAscii; i++)
        {
            var b = packet[i];
            ascii[i] = b >= 32 && b <= 126 ? (char)b : '.';
        }

        return $"L{packet.Length}-H{hex}-A{new string(ascii)}";
    }

    private async Task ReceiveWatchdogLoopAsync(CancellationToken token)
    {
        while (!token.IsCancellationRequested)
        {
            try
            {
                await Task.Delay(1000, token);
            }
            catch (OperationCanceledException)
            {
                break;
            }

            lock (_sync)
            {
                if (_cts is null || _udpClient is null || _receiveTask is null)
                {
                    continue;
                }

                if (!_receiveTask.IsCompleted)
                {
                    continue;
                }

                _receiveLoopRestartCount++;
                _receiveTask = Task.Run(() => ReceiveLoopAsync(_cts.Token));
                _diagnostics = _diagnostics with
                {
                    SourceStatus = $"receive-loop-restarted:{_receiveLoopRestartCount}",
                    StatusMessage = $"receive loop restarted ({_receiveLoopRestartCount})",
                    LastErrorCode = string.Empty,
                };
            }
        }
    }

    private void TryRebindIfacialSocketWhenNoPackets()
    {
        lock (_sync)
        {
            if (_udpClient is null)
            {
                return;
            }

            var noPacketAgeMs = GetPacketAgeMs(_lastIfacialPacketUtc);
            if (noPacketAgeMs < NoPacketRebindThresholdMs)
            {
                return;
            }

            var now = DateTimeOffset.UtcNow;
            if (_lastUdpRebindAttemptUtc != DateTimeOffset.MinValue &&
                (now - _lastUdpRebindAttemptUtc).TotalMilliseconds < NoPacketRebindCooldownMs)
            {
                return;
            }

            _lastUdpRebindAttemptUtc = now;
            try
            {
                _udpClient.Close();
            }
            catch
            {
                // Best-effort close.
            }

            try
            {
                _udpClient = CreateUdpListener(_options.ListenPort, out _udpBindMode);
                _diagnostics = _diagnostics with
                {
                    SourceStatus = $"receive-rebind:{_options.ListenPort}:{_udpBindMode}",
                    StatusMessage = $"receive-rebind:{_options.ListenPort} ({_udpBindMode}) after no-packet={noPacketAgeMs}ms",
                    LastErrorCode = string.Empty,
                };
            }
            catch (Exception ex)
            {
                _diagnostics = _diagnostics with
                {
                    SourceStatus = "receive-rebind-failed",
                    StatusMessage = $"receive rebind failed: {ex.Message}",
                    LastErrorCode = "TRACKING_UDP_REBIND_FAILED",
                };
            }
        }
    }

    private void MarkReceiveTimeoutNoPacket()
    {
        lock (_sync)
        {
            if (_udpClient is null || _options.SourceType == TrackingSourceType.WebcamMediapipe)
            {
                return;
            }

            if (_lastIfacialPacketUtc != DateTimeOffset.MinValue)
            {
                return;
            }

            if (_diagnostics.SourceStatus.StartsWith("receive-timeout", StringComparison.Ordinal))
            {
                return;
            }

            _diagnostics = _diagnostics with
            {
                SourceStatus = $"receive-timeout:{_options.ListenPort}",
                StatusMessage = $"receive timeout:{_options.ListenPort} waiting packet",
                LastErrorCode = string.Empty,
            };
        }
    }

    private async Task WebcamLoopAsync(CancellationToken token)
    {
        var pollDelay = TimeSpan.FromMilliseconds(4);

        while (!token.IsCancellationRequested)
        {
            NcResultCode loopRc = NcResultCode.Ok;
            string? loopError = null;
            MediapipeFramePacket? packet = null;
            try
            {
                lock (_sync)
                {
                    if (_mediapipeProcess is null)
                    {
                        loopRc = NcResultCode.NotInitialized;
                        loopError = "mediapipe sidecar not initialized";
                    }
                    else if (_mediapipeProcess.HasExited)
                    {
                        loopRc = NcResultCode.Io;
                        loopError = string.IsNullOrWhiteSpace(_latestMediapipeError)
                            ? $"mediapipe sidecar exited (code={_mediapipeProcess.ExitCode})"
                            : $"mediapipe sidecar exited (code={_mediapipeProcess.ExitCode}): {_latestMediapipeError}";
                    }
                    else if (!_hasMediapipePacket || _latestMediapipePacket is null)
                    {
                        loopRc = NcResultCode.Io;
                        loopError = "mediapipe frame stream not ready";
                    }
                    else
                    {
                        packet = _latestMediapipePacket;
                    }
                }

                if (packet is not null)
                {
                    lock (_sync)
                    {
                        _lastWebcamPacketUtc = DateTimeOffset.UtcNow;
                        EvaluateSourceArbitration(0);
                    }

                    ApplyMediapipeUpperBodyOnly(packet);
                    if (ShouldConsumeWebcamFrame())
                    {
                        ApplyMediapipePacket(packet);
                    }
                }
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (Exception ex)
            {
                loopRc = NcResultCode.Internal;
                loopError = ex.Message;
            }

            if (loopRc != NcResultCode.Ok)
            {
                lock (_sync)
                {
                    _diagnostics = _diagnostics with
                    {
                        DroppedPackets = _diagnostics.DroppedPackets + 1,
                        SourceStatus = _activeRuntimeSource == TrackingRuntimeSource.Webcam
                            ? BuildWebcamSourceStatus("fallback-active")
                            : BuildWebcamSourceStatus("standby"),
                        StatusMessage = $"webcam error: {loopError}",
                        LastErrorCode = $"TRACKING_MEDIAPIPE_{loopRc}".ToUpperInvariant(),
                        ActiveSource = ToActiveSourceLabel(_activeRuntimeSource),
                        ConfidenceSummary = BuildConfidenceSummary(),
                    };
                    if (_upperBodyEnabled)
                    {
                        _upperBodyLastError = "TRACKING_WEBCAM_UPPER_BODY_PACKET_ERROR";
                        _upperBodyStatus = "packet-error";
                    }
                }
            }

            try
            {
                await Task.Delay(pollDelay, token);
            }
            catch (OperationCanceledException)
            {
                break;
            }
        }
    }

    private string BuildWebcamSourceStatus(string stage)
    {
        var device = string.IsNullOrWhiteSpace(_options.CameraDeviceKey) ? "default-device" : _options.CameraDeviceKey;
        return $"webcam-mediapipe:{stage}:{device}:fps_cap={_options.InferenceFpsCap}";
    }

    private NcResultCode InitializeWebcamRuntime()
    {
        DisposeWebcamRuntime();
        try
        {
            var launch = BuildMediapipeSidecarLaunchConfig();
            if (!launch.IsValid)
            {
                _diagnostics = _diagnostics with
                {
                    IsActive = false,
                    SourceStatus = BuildWebcamSourceStatus("sidecar-config-invalid"),
                    StatusMessage = launch.ErrorMessage,
                    ModelSchemaOk = false,
                    LastErrorCode = "TRACKING_MEDIAPIPE_CONFIG_INVALID",
                };
                return NcResultCode.InvalidArgument;
            }

            var process = StartMediapipeSidecar(launch);
            if (process is null)
            {
                _diagnostics = _diagnostics with
                {
                    IsActive = false,
                    SourceStatus = BuildWebcamSourceStatus("sidecar-start-failed"),
                    StatusMessage = string.IsNullOrWhiteSpace(_latestMediapipeError)
                        ? "mediapipe sidecar start failed"
                        : $"mediapipe sidecar start failed: {_latestMediapipeError}",
                    ModelSchemaOk = false,
                    LastErrorCode = "TRACKING_MEDIAPIPE_START_FAILED",
                };
                return NcResultCode.NotInitialized;
            }

            _mediapipeProcess = process;
            _mediapipeStdoutTask = Task.Run(() => ReadMediapipeStdoutLoopAsync(process, _cts?.Token ?? CancellationToken.None));
            _mediapipeStderrTask = Task.Run(() => ReadMediapipeStderrLoopAsync(process, _cts?.Token ?? CancellationToken.None));

            var waitWatch = Stopwatch.StartNew();
            while (waitWatch.Elapsed < TimeSpan.FromSeconds(2.5))
            {
                lock (_sync)
                {
                    if (_hasMediapipePacket)
                    {
                        break;
                    }
                    if (_mediapipeProcess?.HasExited == true)
                    {
                        break;
                    }
                }

                Thread.Sleep(40);
            }

            lock (_sync)
            {
                if (!_hasMediapipePacket)
                {
                    var sidecarExited = _mediapipeProcess?.HasExited == true;
                    var sidecarExitCode = _mediapipeProcess?.HasExited == true
                        ? _mediapipeProcess.ExitCode
                        : -1;
                    var normalizedError = _latestMediapipeError.Trim();
                    var sourceStage = "sidecar-no-frames";
                    var status = string.IsNullOrWhiteSpace(normalizedError)
                        ? "mediapipe sidecar started but no frame received"
                        : $"mediapipe sidecar no frame: {normalizedError}";
                    var errorCode = "TRACKING_MEDIAPIPE_NO_FRAME";
                    if (sidecarExited)
                    {
                        sourceStage = "sidecar-exited";
                        status = string.IsNullOrWhiteSpace(normalizedError)
                            ? $"mediapipe sidecar exited before first frame (code={sidecarExitCode})"
                            : $"mediapipe sidecar exited before first frame (code={sidecarExitCode}): {normalizedError}";
                        errorCode = "TRACKING_MEDIAPIPE_START_FAILED";
                    }

                    _diagnostics = _diagnostics with
                    {
                        IsActive = false,
                        SourceStatus = BuildWebcamSourceStatus(sourceStage),
                        StatusMessage = status,
                        ModelSchemaOk = false,
                        LastErrorCode = errorCode,
                    };
                    return NcResultCode.Io;
                }
            }

            _diagnostics = _diagnostics with
            {
                ModelSchemaOk = true,
                LastErrorCode = string.Empty,
                SourceStatus = BuildWebcamSourceStatus("initialized"),
                StatusMessage = "webcam mediapipe runtime initialized",
            };
            return NcResultCode.Ok;
        }
        catch (Exception ex)
        {
            DisposeWebcamRuntime();
            _diagnostics = _diagnostics with
            {
                IsActive = false,
                SourceStatus = BuildWebcamSourceStatus("init-failed"),
                StatusMessage = $"webcam init failed: {ex.Message}",
                ModelSchemaOk = false,
                LastErrorCode = NcResultCode.Internal.ToString(),
            };
            return NcResultCode.Internal;
        }
    }

    private void DisposeWebcamRuntime()
    {
        try
        {
            _mediapipeProcess?.Kill(true);
        }
        catch
        {
            // Best effort.
        }

        _mediapipeProcess?.Dispose();
        _mediapipeProcess = null;
        _mediapipeStdoutTask = null;
        _mediapipeStderrTask = null;
        _latestMediapipePacket = null;
        _hasMediapipePacket = false;
        _latestMediapipeError = string.Empty;
        _lastMediapipeFrameId = 0;
    }

    private void ApplyMediapipePacket(MediapipeFramePacket packet)
    {
        lock (_sync)
        {
            _rawFrame = BuildNeutralFrame();
            _rawHeadYaw = packet.HeadYawDeg;
            _rawHeadPitch = packet.HeadPitchDeg;
            _rawHeadRoll = packet.HeadRollDeg;
            _hasHeadYpr = true;
            _rawFrame.HeadPosX = packet.HeadPosX;
            _rawFrame.HeadPosY = packet.HeadPosY;
            _rawFrame.HeadPosZ = packet.HeadPosZ;
            ApplyMediapipeBlendshapeResult(packet);

            _lastPacketUtc = _lastWebcamPacketUtc == DateTimeOffset.MinValue ? DateTimeOffset.UtcNow : _lastWebcamPacketUtc;
            _hasFrame = true;
            UpdateInputFps();
            UpdateCaptureFps(packet.CaptureFps);
            UpdateInferenceMs(packet.InferenceMs);
            var captureStageMs = packet.InferenceMs;
            if (packet.SourceTimestampUnixMs > 0)
            {
                try
                {
                    var sourceUtc = DateTimeOffset.FromUnixTimeMilliseconds(packet.SourceTimestampUnixMs);
                    captureStageMs = Math.Max(captureStageMs, Math.Max(0.0, (_lastPacketUtc - sourceUtc).TotalMilliseconds));
                }
                catch (ArgumentOutOfRangeException)
                {
                    // keep fallback metric from inference time
                }
            }
            UpdateCaptureStageMs(captureStageMs);
            var smoothWatch = Stopwatch.StartNew();
            ApplySmoothing();
            smoothWatch.Stop();
            UpdateSmoothStageMs(smoothWatch.Elapsed.TotalMilliseconds);
            AdvanceCalibration();
            _diagnostics = _diagnostics with
            {
                DetectedFormat = "webcam-mediapipe",
                InputFps = _smoothedInputFps,
                CaptureFps = _smoothedCaptureFps,
                InferenceMsAvg = _smoothedInferenceMs,
                SourceStatus = _options.SourceType == TrackingSourceType.HybridAuto
                    ? BuildWebcamSourceStatus("fallback-active")
                    : BuildWebcamSourceStatus("receiving"),
                StatusMessage = _options.SourceType == TrackingSourceType.HybridAuto
                    ? "receiving:webcam-mediapipe (fallback)"
                    : "receiving:webcam-mediapipe",
                ModelSchemaOk = true,
                LastErrorCode = string.Empty,
                ActiveSource = ToActiveSourceLabel(_activeRuntimeSource),
                FallbackCount = _fallbackCount,
                CalibrationState = _calibrationState,
                ConfidenceSummary = BuildConfidenceSummary(),
                LatencyAvgMs = _latencyAvgMs,
                LatencyP95Ms = _latencyP95Ms,
                CaptureStageMs = _smoothedCaptureStageMs,
                ParseStageMs = _smoothedParseStageMs,
                SmoothStageMs = _smoothedSmoothStageMs,
                SubmitStageMs = _smoothedSubmitStageMs,
                SourceLockMode = _options.SourceLockMode,
                SwitchBlockedReason = _switchBlockedReason,
            };
        }
    }

    private void ApplyMediapipeUpperBodyOnly(MediapipeFramePacket packet)
    {
        lock (_sync)
        {
            UpdateUpperBodyPoseFromPacket(packet);
        }
    }

    private void UpdateUpperBodyPoseFromPacket(MediapipeFramePacket packet)
    {
        var packetUtc = _lastWebcamPacketUtc == DateTimeOffset.MinValue ? DateTimeOffset.UtcNow : _lastWebcamPacketUtc;
        UpdateUpperBodyPoseFromValues(
            packet.LeftShoulderPitchDeg,
            packet.RightShoulderPitchDeg,
            packet.LeftUpperArmPitchDeg,
            packet.RightUpperArmPitchDeg,
            packet.UpperBodyConfidence,
            packetUtc,
            "webcam");
    }

    private void UpdateUpperBodyPoseFromValues(
        float leftShoulderPitchDeg,
        float rightShoulderPitchDeg,
        float leftUpperArmPitchDeg,
        float rightUpperArmPitchDeg,
        float confidence,
        DateTimeOffset packetUtc,
        string sourceLabel)
    {
        if (!_upperBodyEnabled)
        {
            _upperBodyStatus = "disabled";
            _upperBodyConfidence = 0.0;
            return;
        }

        _rawLeftShoulderPitch = ClampShoulderPitch(leftShoulderPitchDeg);
        _rawRightShoulderPitch = ClampShoulderPitch(rightShoulderPitchDeg);
        _rawLeftUpperArmPitch = ClampUpperArmPitch(leftUpperArmPitchDeg);
        _rawRightUpperArmPitch = ClampUpperArmPitch(rightUpperArmPitchDeg);

        if (!_hasSmoothedUpperBody)
        {
            _smoothedLeftShoulderPitch = _rawLeftShoulderPitch;
            _smoothedRightShoulderPitch = _rawRightShoulderPitch;
            _smoothedLeftUpperArmPitch = _rawLeftUpperArmPitch;
            _smoothedRightUpperArmPitch = _rawRightUpperArmPitch;
            _hasSmoothedUpperBody = true;
        }
        else
        {
            _smoothedLeftShoulderPitch = Ema(_smoothedLeftShoulderPitch, _rawLeftShoulderPitch, _upperBodyAlpha);
            _smoothedRightShoulderPitch = Ema(_smoothedRightShoulderPitch, _rawRightShoulderPitch, _upperBodyAlpha);
            _smoothedLeftUpperArmPitch = Ema(_smoothedLeftUpperArmPitch, _rawLeftUpperArmPitch, _upperBodyAlpha);
            _smoothedRightUpperArmPitch = Ema(_smoothedRightUpperArmPitch, _rawRightUpperArmPitch, _upperBodyAlpha);
        }

        _smoothedLeftShoulderPitch = ClampShoulderPitch(_smoothedLeftShoulderPitch * _upperBodyStrength);
        _smoothedRightShoulderPitch = ClampShoulderPitch(_smoothedRightShoulderPitch * _upperBodyStrength);
        _smoothedLeftUpperArmPitch = ClampUpperArmPitch(_smoothedLeftUpperArmPitch * _upperBodyStrength);
        _smoothedRightUpperArmPitch = ClampUpperArmPitch(_smoothedRightUpperArmPitch * _upperBodyStrength);

        _upperBodyConfidence = Math.Clamp(confidence, 0.0f, 1.0f);
        if (string.Equals(sourceLabel, "osc", StringComparison.Ordinal))
        {
            _lastUpperBodyOscPacketUtc = packetUtc;
        }
        else
        {
            _lastUpperBodyWebcamPacketUtc = packetUtc;
        }
        _lastUpperBodyPacketUtc = packetUtc;
        _upperBodyActiveSource = sourceLabel;
        _upperBodyStatus = _upperBodyConfidence >= 0.12 ? "active" : "low-confidence";
        _upperBodyLastError = string.Empty;
    }

    private void ApplyMediapipeBlendshapeResult(MediapipeFramePacket packet)
    {
        for (var i = 0; i < Arkit52Channels.CanonicalOrder.Count; i++)
        {
            var key = NormalizeKey(Arkit52Channels.CanonicalOrder[i]);
            _expressionCache[key] = 0.0f;
        }

        _expressionCache["eyeblinkleft"] = ApplyAdaptiveCalibration("eyeblinkleft", Clamp01(packet.BlinkLeft));
        _expressionCache["eyeblinkright"] = ApplyAdaptiveCalibration("eyeblinkright", Clamp01(packet.BlinkRight));
        _expressionCache["jawopen"] = ApplyAdaptiveCalibration("jawopen", Clamp01(packet.MouthOpen));
        _expressionCache["mouthsmileleft"] = ApplyAdaptiveCalibration("mouthsmileleft", Clamp01(packet.Smile));
        _expressionCache["mouthsmileright"] = ApplyAdaptiveCalibration("mouthsmileright", Clamp01(packet.Smile));

        foreach (var pair in packet.BlendshapeWeights)
        {
            var key = NormalizeKey(pair.Key);
            if (string.IsNullOrWhiteSpace(key))
            {
                continue;
            }

            _expressionCache[key] = ApplyAdaptiveCalibration(key, Clamp01(pair.Value));
        }

        _rawFrame.BlinkL = _expressionCache["eyeblinkleft"];
        _rawFrame.BlinkR = _expressionCache["eyeblinkright"];
        _rawFrame.MouthOpen = _expressionCache["jawopen"];
        SnapshotExpressionWeights();
    }

    private MediapipeSidecarLaunchConfig BuildMediapipeSidecarLaunchConfig()
    {
        var scriptPath = ResolveMediapipeSidecarScriptPath(out var searchedPaths);
        if (string.IsNullOrWhiteSpace(scriptPath))
        {
            return new MediapipeSidecarLaunchConfig(
                false,
                string.Empty,
                string.Empty,
                $"mediapipe_webcam_sidecar.py not found. set ANIMIQ_MEDIAPIPE_SIDECAR_SCRIPT to an absolute path. searched=[{string.Join(", ", searchedPaths)}]");
        }

        var pythonLaunch = ResolveMediapipePythonLaunchConfig();
        if (!pythonLaunch.IsValid)
        {
            return new MediapipeSidecarLaunchConfig(
                false,
                string.Empty,
                string.Empty,
                pythonLaunch.ErrorMessage);
        }

        var cameraArg = string.IsNullOrWhiteSpace(_options.CameraDeviceKey) ? "0" : _options.CameraDeviceKey.Trim();
        var sidecarArgs = string.Create(
            CultureInfo.InvariantCulture,
            $"\"{scriptPath}\" --camera \"{cameraArg}\" --fps {_options.InferenceFpsCap}");
        var launchArgs = string.IsNullOrWhiteSpace(pythonLaunch.ArgumentPrefix)
            ? sidecarArgs
            : $"{pythonLaunch.ArgumentPrefix} {sidecarArgs}";
        return new MediapipeSidecarLaunchConfig(true, pythonLaunch.Executable, launchArgs, string.Empty);
    }

    private static string ResolveMediapipeSidecarScriptPath(out IReadOnlyList<string> searchedPaths)
    {
        var candidates = new List<string>(capacity: 4);
        var envPath = Environment.GetEnvironmentVariable("ANIMIQ_MEDIAPIPE_SIDECAR_SCRIPT");
        if (!string.IsNullOrWhiteSpace(envPath))
        {
            candidates.Add(envPath.Trim());
        }

        candidates.Add(Path.Combine(AppContext.BaseDirectory, "tools", "mediapipe_webcam_sidecar.py"));
        candidates.Add(Path.Combine(AppContext.BaseDirectory, "mediapipe_webcam_sidecar.py"));
        candidates.Add(Path.Combine(Environment.CurrentDirectory, "tools", "mediapipe_webcam_sidecar.py"));
        searchedPaths = candidates;

        foreach (var candidate in candidates)
        {
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }

        return string.Empty;
    }

    private MediapipePythonLaunchConfig ResolveMediapipePythonLaunchConfig()
    {
        var attempted = new List<string>();
        foreach (var candidate in BuildMediapipePythonCandidates())
        {
            if (TryProbePythonCandidate(candidate, out var probeFailure))
            {
                return new MediapipePythonLaunchConfig(true, candidate.Executable, candidate.ArgumentPrefix, string.Empty);
            }

            attempted.Add($"{candidate.DisplayName}: {probeFailure}");
        }

        var detail = attempted.Count == 0
            ? "no python candidate configured"
            : string.Join(" | ", attempted);
        return new MediapipePythonLaunchConfig(
            false,
            string.Empty,
            string.Empty,
            $"python runtime not available for webcam tracking. set ANIMIQ_MEDIAPIPE_PYTHON or run tools/setup_tracking_python_venv.ps1. tried=[{detail}]");
    }

    private static IEnumerable<MediapipePythonCandidate> BuildMediapipePythonCandidates()
    {
        var yielded = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        void AddCandidate(List<MediapipePythonCandidate> list, string executable, string argumentPrefix, string displayName)
        {
            var normalizedExe = NormalizeExecutableToken(executable);
            if (string.IsNullOrWhiteSpace(normalizedExe))
            {
                return;
            }

            var normalizedPrefix = argumentPrefix?.Trim() ?? string.Empty;
            var dedupeKey = $"{normalizedExe}|{normalizedPrefix}";
            if (!yielded.Add(dedupeKey))
            {
                return;
            }

            list.Add(new MediapipePythonCandidate(normalizedExe, normalizedPrefix, displayName));
        }

        var candidates = new List<MediapipePythonCandidate>(capacity: 6);
        var envPython = Environment.GetEnvironmentVariable("ANIMIQ_MEDIAPIPE_PYTHON");
        if (!string.IsNullOrWhiteSpace(envPython))
        {
            AddCandidate(candidates, envPython, string.Empty, "env:ANIMIQ_MEDIAPIPE_PYTHON");
        }

        AddCandidate(candidates, "py", "-3", "py -3");

        var venvCandidates = new[]
        {
            Path.Combine(AppContext.BaseDirectory, ".venv", "Scripts", "python.exe"),
            Path.Combine(Environment.CurrentDirectory, ".venv", "Scripts", "python.exe"),
        };
        foreach (var venvCandidate in venvCandidates)
        {
            if (File.Exists(venvCandidate))
            {
                AddCandidate(candidates, venvCandidate, string.Empty, $".venv ({venvCandidate})");
            }
        }

        AddCandidate(candidates, "python", string.Empty, "python");
        return candidates;
    }

    private static string NormalizeExecutableToken(string executable)
    {
        var normalized = executable.Trim();
        if (normalized.Length >= 2 && normalized[0] == '"' && normalized[^1] == '"')
        {
            normalized = normalized[1..^1].Trim();
        }

        return normalized;
    }

    private static bool TryProbePythonCandidate(MediapipePythonCandidate candidate, out string failure)
    {
        failure = string.Empty;
        try
        {
            var versionArgs = string.IsNullOrWhiteSpace(candidate.ArgumentPrefix)
                ? "--version"
                : $"{candidate.ArgumentPrefix} --version";
            var psi = new ProcessStartInfo(candidate.Executable, versionArgs)
            {
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true,
                StandardOutputEncoding = Encoding.UTF8,
                StandardErrorEncoding = Encoding.UTF8,
            };

            using var process = Process.Start(psi);
            if (process is null)
            {
                failure = "process start returned null";
                return false;
            }

            if (!process.WaitForExit(1800))
            {
                try
                {
                    process.Kill(true);
                }
                catch
                {
                    // best effort only
                }

                failure = "version probe timeout";
                return false;
            }

            if (process.ExitCode != 0)
            {
                var stderr = process.StandardError.ReadToEnd().Trim();
                var stdout = process.StandardOutput.ReadToEnd().Trim();
                failure = $"exit={process.ExitCode}; stderr={stderr}; stdout={stdout}";
                return false;
            }

            return true;
        }
        catch (Exception ex)
        {
            failure = ex.Message;
            return false;
        }
    }

    private Process? StartMediapipeSidecar(MediapipeSidecarLaunchConfig launch)
    {
        try
        {
            var psi = new ProcessStartInfo(launch.Executable, launch.Arguments)
            {
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true,
                StandardOutputEncoding = Encoding.UTF8,
                StandardErrorEncoding = Encoding.UTF8,
            };

            var process = Process.Start(psi);
            if (process is null)
            {
                _latestMediapipeError = "failed to start mediapipe process";
                return null;
            }

            return process;
        }
        catch (Exception ex)
        {
            _latestMediapipeError = ex.Message;
            return null;
        }
    }

    private async Task ReadMediapipeStdoutLoopAsync(Process process, CancellationToken token)
    {
        try
        {
            while (!token.IsCancellationRequested && !process.HasExited)
            {
                var line = await process.StandardOutput.ReadLineAsync(token);
                if (string.IsNullOrWhiteSpace(line))
                {
                    continue;
                }

                if (!TryParseMediapipePacket(line, out var packet))
                {
                    lock (_sync)
                    {
                        _diagnostics = _diagnostics with
                        {
                            ParseErrors = _diagnostics.ParseErrors + 1,
                            SourceStatus = BuildWebcamSourceStatus("parse-failed"),
                            StatusMessage = "mediapipe packet parse failed",
                            LastErrorCode = "TRACKING_MEDIAPIPE_PACKET_PARSE_FAILED",
                        };
                    }
                    continue;
                }

                lock (_sync)
                {
                    UpdateParseStageMs(packet.ParseMs);
                    _latestMediapipePacket = packet;
                    _hasMediapipePacket = true;
                    _diagnostics = _diagnostics with
                    {
                        ReceivedPackets = _diagnostics.ReceivedPackets + 1,
                    };
                }
            }
        }
        catch (OperationCanceledException)
        {
            // Normal shutdown path.
        }
        catch (Exception ex)
        {
            lock (_sync)
            {
                _latestMediapipeError = ex.Message;
                _diagnostics = _diagnostics with
                {
                    SourceStatus = BuildWebcamSourceStatus("read-failed"),
                    StatusMessage = $"mediapipe read failed: {ex.Message}",
                    LastErrorCode = "TRACKING_MEDIAPIPE_READ_FAILED",
                };
            }
        }
    }

    private async Task ReadMediapipeStderrLoopAsync(Process process, CancellationToken token)
    {
        try
        {
            while (!token.IsCancellationRequested && !process.HasExited)
            {
                var line = await process.StandardError.ReadLineAsync(token);
                if (string.IsNullOrWhiteSpace(line))
                {
                    continue;
                }

                lock (_sync)
                {
                    _latestMediapipeError = line.Trim();
                }
            }
        }
        catch (OperationCanceledException)
        {
            // Normal shutdown path.
        }
        catch
        {
            // Diagnostics only.
        }
    }

    private bool TryParseMediapipePacket(string jsonLine, [NotNullWhen(true)] out MediapipeFramePacket? packet)
    {
        packet = null;
        var parseWatch = Stopwatch.StartNew();
        try
        {
            using var doc = JsonDocument.Parse(jsonLine);
            var root = doc.RootElement;

            var schemaVersion = 0;
            if (!TryReadNumber(root, "schema_version", out var schemaVersionRaw))
            {
                return false;
            }
            schemaVersion = (int)schemaVersionRaw;
            if (schemaVersion <= 0)
            {
                return false;
            }

            if (!TryReadNumber(root, "frame_id", out var frameIdRaw))
            {
                return false;
            }
            var frameId = (long)frameIdRaw;
            if (frameId <= _lastMediapipeFrameId)
            {
                return false;
            }

            if (!TryReadNumber(root, "yaw_deg", out var yawDeg) ||
                !TryReadNumber(root, "pitch_deg", out var pitchDeg) ||
                !TryReadNumber(root, "roll_deg", out var rollDeg) ||
                !TryReadNumber(root, "head_pos_x", out var headPosX) ||
                !TryReadNumber(root, "head_pos_y", out var headPosY) ||
                !TryReadNumber(root, "head_pos_z", out var headPosZ) ||
                !TryReadNumber(root, "blink_l", out var blinkL) ||
                !TryReadNumber(root, "blink_r", out var blinkR) ||
                !TryReadNumber(root, "mouth_open", out var mouthOpen) ||
                !TryReadNumber(root, "smile", out var smile))
            {
                return false;
            }

            var captureFps = 0.0;
            var inferenceMs = 0.0;
            var sourceConfidence = 0.75;
            var sourceTimestampUnixMs = 0L;
            var leftShoulderPitchDeg = 0.0;
            var rightShoulderPitchDeg = 0.0;
            var leftUpperArmPitchDeg = 0.0;
            var rightUpperArmPitchDeg = 0.0;
            var upperBodyConfidence = 0.0;
            _ = TryReadNumber(root, "capture_fps", out captureFps);
            _ = TryReadNumber(root, "inference_ms", out inferenceMs);
            _ = TryReadNumber(root, "confidence", out sourceConfidence);
            _ = TryReadNumber(root, "left_shoulder_pitch_deg", out leftShoulderPitchDeg);
            _ = TryReadNumber(root, "right_shoulder_pitch_deg", out rightShoulderPitchDeg);
            _ = TryReadNumber(root, "left_upperarm_pitch_deg", out leftUpperArmPitchDeg);
            _ = TryReadNumber(root, "right_upperarm_pitch_deg", out rightUpperArmPitchDeg);
            _ = TryReadNumber(root, "upper_body_confidence", out upperBodyConfidence);
            if (TryReadNumber(root, "source_ts_unix_ms", out var sourceTsRaw))
            {
                sourceTimestampUnixMs = (long)sourceTsRaw;
            }

            var blendshapes = new Dictionary<string, float>(StringComparer.OrdinalIgnoreCase);
            if (root.TryGetProperty("blendshapes", out var blendRoot) &&
                blendRoot.ValueKind == JsonValueKind.Object)
            {
                foreach (var prop in blendRoot.EnumerateObject())
                {
                    if (prop.Value.ValueKind == JsonValueKind.Number && prop.Value.TryGetDouble(out var value))
                    {
                        blendshapes[prop.Name] = Clamp01((float)value);
                    }
                }
            }

            _lastMediapipeFrameId = frameId;
            parseWatch.Stop();
            packet = new MediapipeFramePacket(
                schemaVersion,
                frameId,
                (float)yawDeg,
                (float)pitchDeg,
                (float)rollDeg,
                (float)headPosX,
                (float)headPosY,
                (float)headPosZ,
                Clamp01((float)blinkL),
                Clamp01((float)blinkR),
                Clamp01((float)mouthOpen),
                Clamp01((float)smile),
                captureFps <= 0.0 ? 0.0 : captureFps,
                inferenceMs <= 0.0 ? 0.0 : inferenceMs,
                Clamp01((float)sourceConfidence),
                sourceTimestampUnixMs,
                parseWatch.Elapsed.TotalMilliseconds,
                blendshapes,
                (float)leftShoulderPitchDeg,
                (float)rightShoulderPitchDeg,
                (float)leftUpperArmPitchDeg,
                (float)rightUpperArmPitchDeg,
                Clamp01((float)upperBodyConfidence));
            return true;
        }
        catch
        {
            return false;
        }
    }

    private static bool TryReadNumber(JsonElement root, string propertyName, out double value)
    {
        value = 0.0;
        if (!root.TryGetProperty(propertyName, out var element))
        {
            return false;
        }

        if (element.ValueKind == JsonValueKind.Number)
        {
            return element.TryGetDouble(out value);
        }

        if (element.ValueKind == JsonValueKind.String)
        {
            return double.TryParse(element.GetString(), NumberStyles.Float, CultureInfo.InvariantCulture, out value);
        }

        return false;
    }

    private bool TryParsePacket(
        byte[] packet,
        out List<KeyValuePair<string, float>> updates,
        out string formatName,
        out PacketParseFailure parseFailure)
    {
        updates = new List<KeyValuePair<string, float>>();
        formatName = "unknown";
        parseFailure = PacketParseFailure.Unknown;

        if (packet.Length == 0)
        {
            parseFailure = PacketParseFailure.EmptyPacket;
            return false;
        }

        if (IsOscBundle(packet))
        {
            formatName = "format-b";
            if (TryParseBundle(packet, updates, out parseFailure))
            {
                return true;
            }

            parseFailure = parseFailure == PacketParseFailure.Unknown
                ? PacketParseFailure.UnsupportedPacket
                : parseFailure;
            return false;
        }

        if (!TryParseOscMessage(packet, out var message, out parseFailure))
        {
            if (TryParseIfmPacket(packet, out updates, out formatName, out parseFailure))
            {
                return true;
            }
            if (parseFailure == PacketParseFailure.Unknown && LooksLikeVmcPacket(packet))
            {
                parseFailure = PacketParseFailure.ProtocolMismatchVmc;
            }
            return false;
        }

        if (TryExtractFormatA(message, updates))
        {
            formatName = "format-a";
            return true;
        }

        if (TryExtractFormatB(message, updates))
        {
            formatName = "format-b";
            return true;
        }

        if (LooksLikeVmcAddress(message.Address))
        {
            parseFailure = PacketParseFailure.ProtocolMismatchVmc;
            return false;
        }

        parseFailure = PacketParseFailure.NoMappedChannels;
        return false;
    }

    private static bool IsOscBundle(byte[] packet)
    {
        if (packet.Length < 8)
        {
            return false;
        }

        return packet[0] == (byte)'#' &&
               packet[1] == (byte)'b' &&
               packet[2] == (byte)'u' &&
               packet[3] == (byte)'n' &&
               packet[4] == (byte)'d' &&
               packet[5] == (byte)'l' &&
               packet[6] == (byte)'e' &&
               packet[7] == 0;
    }

    private bool TryParseBundle(byte[] packet, List<KeyValuePair<string, float>> updates, out PacketParseFailure parseFailure)
    {
        parseFailure = PacketParseFailure.Unknown;
        // bundle = "#bundle\0" + timetag(8) + element[size+payload]*
        if (packet.Length < 16)
        {
            parseFailure = PacketParseFailure.UnsupportedPacket;
            return false;
        }

        var index = 16; // skip header + timetag
        var any = false;
        while (index + 4 <= packet.Length)
        {
            var size = ReadInt32BigEndian(packet, index);
            index += 4;
            if (size <= 0 || index + size > packet.Length)
            {
                parseFailure = PacketParseFailure.UnsupportedPacket;
                return false;
            }

            var slice = new byte[size];
            Buffer.BlockCopy(packet, index, slice, 0, size);
            index += size;

            if (IsOscBundle(slice))
            {
                if (!TryParseBundle(slice, updates, out parseFailure))
                {
                    return false;
                }
                any = true;
                continue;
            }

            if (!TryParseOscMessage(slice, out var message, out parseFailure))
            {
                if (parseFailure == PacketParseFailure.Unknown && LooksLikeVmcPacket(slice))
                {
                    parseFailure = PacketParseFailure.ProtocolMismatchVmc;
                }
                continue;
            }
            if (TryExtractFormatA(message, updates) || TryExtractFormatB(message, updates))
            {
                any = true;
                continue;
            }

            if (LooksLikeVmcAddress(message.Address))
            {
                parseFailure = PacketParseFailure.ProtocolMismatchVmc;
            }
        }

        if (!any && parseFailure == PacketParseFailure.Unknown)
        {
            parseFailure = PacketParseFailure.NoMappedChannels;
        }
        return any;
    }

    private static int ReadInt32BigEndian(byte[] buffer, int index)
    {
        return (buffer[index] << 24) |
               (buffer[index + 1] << 16) |
               (buffer[index + 2] << 8) |
               buffer[index + 3];
    }

    private static bool TryParseOscMessage(byte[] packet, out OscMessage message, out PacketParseFailure parseFailure)
    {
        parseFailure = PacketParseFailure.Unknown;
        message = new OscMessage(string.Empty, string.Empty, Array.Empty<OscValue>());
        if (packet.Length < 8)
        {
            parseFailure = PacketParseFailure.UnsupportedPacket;
            return false;
        }

        var index = 0;
        if (!TryReadOscString(packet, ref index, out var address))
        {
            parseFailure = PacketParseFailure.UnsupportedPacket;
            return false;
        }
        if (!TryReadOscString(packet, ref index, out var typeTag))
        {
            parseFailure = PacketParseFailure.UnsupportedPacket;
            return false;
        }
        if (string.IsNullOrWhiteSpace(typeTag) || typeTag[0] != ',')
        {
            parseFailure = PacketParseFailure.UnsupportedPacket;
            return false;
        }

        var values = new List<OscValue>();
        for (var i = 1; i < typeTag.Length; i++)
        {
            var tag = typeTag[i];
            switch (tag)
            {
                case 'f':
                    if (index + 4 > packet.Length)
                    {
                        parseFailure = PacketParseFailure.UnsupportedPacket;
                        return false;
                    }
                    var raw = ((uint)packet[index] << 24) |
                              ((uint)packet[index + 1] << 16) |
                              ((uint)packet[index + 2] << 8) |
                              packet[index + 3];
                    index += 4;
                    var f = BitConverter.Int32BitsToSingle((int)raw);
                    if (float.IsNaN(f) || float.IsInfinity(f))
                    {
                        parseFailure = PacketParseFailure.UnsupportedPacket;
                        return false;
                    }
                    values.Add(new OscValue(OscValueKind.Float, f, string.Empty));
                    break;
                case 'i':
                    if (index + 4 > packet.Length)
                    {
                        parseFailure = PacketParseFailure.UnsupportedPacket;
                        return false;
                    }
                    var i32 = ReadInt32BigEndian(packet, index);
                    index += 4;
                    values.Add(new OscValue(OscValueKind.Float, i32, string.Empty));
                    break;
                case 'd':
                    if (index + 8 > packet.Length)
                    {
                        parseFailure = PacketParseFailure.UnsupportedPacket;
                        return false;
                    }
                    var dRaw = ((ulong)packet[index] << 56) |
                               ((ulong)packet[index + 1] << 48) |
                               ((ulong)packet[index + 2] << 40) |
                               ((ulong)packet[index + 3] << 32) |
                               ((ulong)packet[index + 4] << 24) |
                               ((ulong)packet[index + 5] << 16) |
                               ((ulong)packet[index + 6] << 8) |
                               packet[index + 7];
                    index += 8;
                    var d = BitConverter.Int64BitsToDouble((long)dRaw);
                    if (double.IsNaN(d) || double.IsInfinity(d))
                    {
                        parseFailure = PacketParseFailure.UnsupportedPacket;
                        return false;
                    }
                    values.Add(new OscValue(OscValueKind.Float, (float)d, string.Empty));
                    break;
                case 'h':
                    if (index + 8 > packet.Length)
                    {
                        parseFailure = PacketParseFailure.UnsupportedPacket;
                        return false;
                    }
                    var i64 = ((long)packet[index] << 56) |
                              ((long)packet[index + 1] << 48) |
                              ((long)packet[index + 2] << 40) |
                              ((long)packet[index + 3] << 32) |
                              ((long)packet[index + 4] << 24) |
                              ((long)packet[index + 5] << 16) |
                              ((long)packet[index + 6] << 8) |
                              packet[index + 7];
                    index += 8;
                    values.Add(new OscValue(OscValueKind.Float, i64, string.Empty));
                    break;
                case 's':
                    if (!TryReadOscString(packet, ref index, out var s))
                    {
                        parseFailure = PacketParseFailure.UnsupportedPacket;
                        return false;
                    }
                    values.Add(new OscValue(OscValueKind.String, 0.0f, s));
                    break;
                case 'T':
                    values.Add(new OscValue(OscValueKind.Float, 1.0f, string.Empty));
                    break;
                case 'F':
                case 'N':
                case 'I':
                    values.Add(new OscValue(OscValueKind.Float, 0.0f, string.Empty));
                    break;
                default:
                    parseFailure = PacketParseFailure.UnsupportedTypeTag;
                    return false;
            }
        }

        message = new OscMessage(address, typeTag, values);
        return true;
    }

    private static bool TryReadOscString(byte[] packet, ref int index, out string value)
    {
        value = string.Empty;
        if (index < 0 || index >= packet.Length)
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
        while ((end % 4) != 0)
        {
            end++;
        }
        if (end > packet.Length)
        {
            return false;
        }

        index = end;
        return true;
    }

    private static bool LooksLikeVmcPacket(byte[] packet)
    {
        if (packet.Length < 2 || packet[0] != (byte)'/')
        {
            return false;
        }

        var maxProbeLength = Math.Min(packet.Length, 96);
        var firstNul = Array.IndexOf(packet, (byte)0, 0, maxProbeLength);
        var textLength = firstNul > 0 ? firstNul : maxProbeLength;
        if (textLength <= 0)
        {
            return false;
        }

        var address = Encoding.UTF8.GetString(packet, 0, textLength);
        return LooksLikeVmcAddress(address);
    }

    private static bool LooksLikeVmcAddress(string address)
    {
        if (string.IsNullOrWhiteSpace(address))
        {
            return false;
        }

        return address.StartsWith("/VMC/", StringComparison.OrdinalIgnoreCase) ||
               address.StartsWith("VMC/", StringComparison.OrdinalIgnoreCase);
    }

    private bool TryParseIfmPacket(
        byte[] packet,
        out List<KeyValuePair<string, float>> updates,
        out string formatName,
        out PacketParseFailure parseFailure)
    {
        updates = new List<KeyValuePair<string, float>>();
        formatName = "unknown";
        parseFailure = PacketParseFailure.Unknown;

        if (packet.Length == 0)
        {
            parseFailure = PacketParseFailure.EmptyPacket;
            return false;
        }

        var payload = Encoding.UTF8.GetString(packet).Trim('\0', ' ', '\r', '\n', '\t');
        if (string.IsNullOrWhiteSpace(payload))
        {
            parseFailure = PacketParseFailure.IfmMalformed;
            return false;
        }

        if (payload[0] == '{' || payload[0] == '[')
        {
            return TryParseIfmJsonPayload(payload, updates, out formatName, out parseFailure);
        }

        // iFacialMocap native text format: "key&value|key&value|...|=headRotX&val&headRotY&val&..."
        if (TryParseIfmNativePacket(payload, updates))
        {
            formatName = ExtractIfmVersion(payload) == 2 ? "ifm-v2" : "ifm-v1";
            parseFailure = PacketParseFailure.Unknown;
            return true;
        }

        return TryParseIfmDelimitedPayload(payload, updates, out formatName, out parseFailure);
    }

    private bool TryParseIfmJsonPayload(
        string payload,
        List<KeyValuePair<string, float>> updates,
        out string formatName,
        out PacketParseFailure parseFailure)
    {
        formatName = "ifm-v1";
        parseFailure = PacketParseFailure.Unknown;

        try
        {
            using var doc = JsonDocument.Parse(payload);
            if (doc.RootElement.ValueKind == JsonValueKind.Array)
            {
                var values = doc.RootElement.EnumerateArray().ToArray();
                if (values.Length < Arkit52Channels.CanonicalOrder.Count)
                {
                    parseFailure = PacketParseFailure.IfmMalformed;
                    return false;
                }

                for (var i = 0; i < Arkit52Channels.CanonicalOrder.Count; i++)
                {
                    if (values[i].ValueKind != JsonValueKind.Number || !values[i].TryGetSingle(out var value))
                    {
                        continue;
                    }

                    AddIfmUpdate(updates, Arkit52Channels.CanonicalOrder[i], value);
                }

                if (updates.Count == 0)
                {
                    parseFailure = PacketParseFailure.NoMappedChannels;
                    return false;
                }

                formatName = "ifm-v2";
                return true;
            }

            if (doc.RootElement.ValueKind != JsonValueKind.Object)
            {
                parseFailure = PacketParseFailure.IfmMalformed;
                return false;
            }

            var root = doc.RootElement;
            var version = ExtractIfmVersion(root);
            if (version > 2)
            {
                parseFailure = PacketParseFailure.IfmUnsupportedVersion;
                return false;
            }

            formatName = version == 2 ? "ifm-v2" : "ifm-v1";
            foreach (var prop in root.EnumerateObject())
            {
                if (prop.Value.ValueKind == JsonValueKind.Number && prop.Value.TryGetSingle(out var value))
                {
                    AddIfmUpdate(updates, prop.Name, value);
                    continue;
                }

                if (prop.Value.ValueKind == JsonValueKind.Object &&
                    prop.Name.Equals("blendshapes", StringComparison.OrdinalIgnoreCase))
                {
                    foreach (var shape in prop.Value.EnumerateObject())
                    {
                        if (shape.Value.ValueKind != JsonValueKind.Number || !shape.Value.TryGetSingle(out var shapeValue))
                        {
                            continue;
                        }

                        AddIfmUpdate(updates, shape.Name, shapeValue);
                    }
                }
            }

            if (updates.Count == 0)
            {
                parseFailure = PacketParseFailure.NoMappedChannels;
                return false;
            }
            return true;
        }
        catch (JsonException)
        {
            parseFailure = PacketParseFailure.IfmMalformed;
            return false;
        }
    }

    private bool TryParseIfmDelimitedPayload(
        string payload,
        List<KeyValuePair<string, float>> updates,
        out string formatName,
        out PacketParseFailure parseFailure)
    {
        formatName = "ifm-v1";
        parseFailure = PacketParseFailure.Unknown;

        var matchedPair = false;
        var ifmVersion = ExtractIfmVersion(payload);
        if (ifmVersion > 2)
        {
            parseFailure = PacketParseFailure.IfmUnsupportedVersion;
            return false;
        }

        foreach (Match match in IfmDelimitedPairRegex.Matches(payload))
        {
            var key = match.Groups[1].Value;
            if (key.Equals("version", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            if (!float.TryParse(match.Groups[2].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out var value))
            {
                continue;
            }

            matchedPair = true;
            AddIfmUpdate(updates, key, value);
        }

        if (!matchedPair)
        {
            var tokens = payload.Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
            for (var i = 0; i + 1 < tokens.Length; i += 2)
            {
                if (!float.TryParse(tokens[i + 1], NumberStyles.Float, CultureInfo.InvariantCulture, out var value))
                {
                    continue;
                }

                if (tokens[i].Equals("version", StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }

                matchedPair = true;
                AddIfmUpdate(updates, tokens[i], value);
            }
        }

        if (ifmVersion == 2)
        {
            formatName = "ifm-v2";
        }

        if (updates.Count > 0)
        {
            return true;
        }

        parseFailure = matchedPair ? PacketParseFailure.NoMappedChannels : PacketParseFailure.IfmMalformed;
        return false;
    }

    private bool TryParseIfmNativePacket(string payload, List<KeyValuePair<string, float>> updates)
    {
        // Fast-fail: '&' is the key&value separator in the native format
        if (!payload.Contains('&')) return false;

        // Split blend section from head section at "|="
        string blendSection = payload;
        string? headSection = null;

        var eqIdx = payload.IndexOf("|=", StringComparison.Ordinal);
        if (eqIdx >= 0)
        {
            blendSection = payload[..eqIdx];
            headSection = payload[(eqIdx + 2)..];
        }
        else if (payload.StartsWith('='))
        {
            blendSection = string.Empty;
            headSection = payload[1..];
        }

        int count = 0;

        // Blendshapes: "key&value" pairs separated by "|"
        foreach (var field in blendSection.Split('|',
            StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            var amp = field.IndexOf('&');
            if (amp <= 0 || amp == field.Length - 1) continue;
            var key = field[..amp];
            if (key.Equals("version", StringComparison.OrdinalIgnoreCase)) continue;
            if (!float.TryParse(field[(amp + 1)..], NumberStyles.Float, CultureInfo.InvariantCulture, out var val)) continue;
            if (AddIfmUpdate(updates, key, val)) count++;
        }

        // Head section: "headRotX&-12.5&headRotY&3.2&headRotZ&1.1&headPosX&0&headPosY&0&headPosZ&0"
        if (headSection is not null)
        {
            var tokens = headSection.Split('&',
                StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
            for (var i = 0; i + 1 < tokens.Length; i += 2)
            {
                if (!float.TryParse(tokens[i + 1], NumberStyles.Float, CultureInfo.InvariantCulture, out var val)) continue;
                if (AddIfmUpdate(updates, tokens[i], val)) count++;
            }
        }

        return count > 0;
    }

    private static int ExtractIfmVersion(string payload)
    {
        var versionMatch = IfmVersionRegex.Match(payload);
        if (!versionMatch.Success)
        {
            return 1;
        }

        return int.TryParse(versionMatch.Groups[1].Value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var version)
            ? version
            : 1;
    }

    private static int ExtractIfmVersion(JsonElement root)
    {
        if (!root.TryGetProperty("version", out var element))
        {
            return 1;
        }

        if (element.ValueKind == JsonValueKind.Number && element.TryGetInt32(out var versionNumber))
        {
            return versionNumber;
        }

        if (element.ValueKind == JsonValueKind.String &&
            int.TryParse(element.GetString(), NumberStyles.Integer, CultureInfo.InvariantCulture, out versionNumber))
        {
            return versionNumber;
        }

        return 1;
    }

    private bool AddIfmUpdate(List<KeyValuePair<string, float>> updates, string rawKey, float value)
    {
        if (!TryNormalizeIfmKey(rawKey, out var normalized))
        {
            RecordIfmKey(rawKey, mapped: false);
            return false;
        }

        RecordIfmKey(rawKey, mapped: true);
        updates.Add(new KeyValuePair<string, float>(normalized, value));
        return true;
    }

    private void RecordIfmKey(string rawKey, bool mapped)
    {
        var normalizedRaw = NormalizeKey(rawKey);
        if (string.IsNullOrWhiteSpace(normalizedRaw))
        {
            return;
        }

        const int maxDistinctKeys = 256;
        var target = mapped ? _ifmAcceptedKeyCounts : _ifmDroppedKeyCounts;
        if (!target.TryGetValue(normalizedRaw, out var count))
        {
            if (target.Count >= maxDistinctKeys)
            {
                return;
            }

            target[normalizedRaw] = 1;
            return;
        }

        target[normalizedRaw] = count + 1;
    }

    private string BuildIfmKeySampleSummary(bool mapped, int topN = 5)
    {
        var source = mapped ? _ifmAcceptedKeyCounts : _ifmDroppedKeyCounts;
        if (source.Count == 0)
        {
            return string.Empty;
        }

        return string.Join(
            ",",
            source
                .OrderByDescending(static pair => pair.Value)
                .ThenBy(static pair => pair.Key, StringComparer.OrdinalIgnoreCase)
                .Take(Math.Max(1, topN))
                .Select(static pair => $"{pair.Key}:{pair.Value}"));
    }

    private static bool TryNormalizeIfmKey(string rawKey, [NotNullWhen(true)] out string? normalizedKey)
    {
        normalizedKey = null;
        var normalized = NormalizeKey(rawKey);
        if (string.IsNullOrWhiteSpace(normalized))
        {
            return false;
        }

        normalized = StripIfmKnownPrefixTokens(normalized);
        if (string.IsNullOrWhiteSpace(normalized))
        {
            return false;
        }

        normalizedKey = normalized switch
        {
            "blinkl" or "blinkleft" or "eyecloseleft" => "eyeblinkleft",
            "blinkr" or "blinkright" or "eyecloseright" => "eyeblinkright",
            "mouthopen" or "visemeaa" or "aa" => "jawopen",
            "headrotx" or "headrotationx" or "rotationx" => "headyaw",
            "headroty" or "headrotationy" or "rotationy" => "headpitch",
            "headrotz" or "headrotationz" or "rotationz" => "headroll",
            "leftshoulder" => "leftshoulderpitch",
            "rightshoulder" => "rightshoulderpitch",
            "leftupperarm" => "leftupperarmpitch",
            "rightupperarm" => "rightupperarmpitch",
            _ => normalized,
        };

        if (TryExpandIfmLeftRightAlias(normalizedKey, out var expanded))
        {
            normalizedKey = expanded;
        }

        if (Arkit52Channels.NormalizedSet.Contains(normalizedKey) ||
            normalizedKey is "headyaw" or "headpitch" or "headroll" or "headposx" or "headposy" or "headposz" or
                "leftshoulderpitch" or "rightshoulderpitch" or "leftupperarmpitch" or "rightupperarmpitch")
        {
            return true;
        }

        normalizedKey = null;
        return false;
    }

    private static string StripIfmKnownPrefixTokens(string normalized)
    {
        if (string.IsNullOrWhiteSpace(normalized))
        {
            return string.Empty;
        }

        var current = normalized;
        while (true)
        {
            var next = current switch
            {
                _ when current.StartsWith("blendshapes", StringComparison.Ordinal) && current.Length > "blendshapes".Length
                    => current["blendshapes".Length..],
                _ when current.StartsWith("blendshape", StringComparison.Ordinal) && current.Length > "blendshape".Length
                    => current["blendshape".Length..],
                _ when current.StartsWith("facial", StringComparison.Ordinal) && current.Length > "facial".Length
                    => current["facial".Length..],
                _ when current.StartsWith("face", StringComparison.Ordinal) && current.Length > "face".Length
                    => current["face".Length..],
                _ when current.StartsWith("bs", StringComparison.Ordinal) && current.Length > "bs".Length
                    => current["bs".Length..],
                _ => current,
            };

            if (ReferenceEquals(next, current) || next == current)
            {
                break;
            }

            current = next;
        }

        return current;
    }

    private static bool TryExpandIfmLeftRightAlias(string normalized, [NotNullWhen(true)] out string? expanded)
    {
        expanded = null;
        if (string.IsNullOrWhiteSpace(normalized) || normalized.Length < 2)
        {
            return false;
        }

        var suffix = normalized[^1];
        if (suffix is not ('l' or 'r'))
        {
            return false;
        }

        var stem = normalized[..^1];
        if (!IfmLeftRightAliasStems.Contains(stem))
        {
            return false;
        }

        expanded = stem + (suffix == 'l' ? "left" : "right");
        return true;
    }

    private bool TryExtractFormatA(OscMessage message, List<KeyValuePair<string, float>> updates)
    {
        if (message.Values.Count != 1 || message.Values[0].Kind != OscValueKind.Float)
        {
            return false;
        }

        var key = NormalizeKey(ExtractAddressTail(message.Address));
        if (string.IsNullOrWhiteSpace(key))
        {
            return false;
        }

        updates.Add(new KeyValuePair<string, float>(key, message.Values[0].FloatValue));
        return true;
    }

    private bool TryExtractFormatB(OscMessage message, List<KeyValuePair<string, float>> updates)
    {
        if (message.Values.Count == 0)
        {
            return false;
        }

        // Some iPhone tracking senders provide a plain 52-float vector payload.
        // Map it directly by canonical ARKit order.
        if (message.Values.Count >= Arkit52Channels.CanonicalOrder.Count)
        {
            var allFloat = true;
            for (var i = 0; i < Arkit52Channels.CanonicalOrder.Count; i++)
            {
                if (message.Values[i].Kind != OscValueKind.Float)
                {
                    allFloat = false;
                    break;
                }
            }

            if (allFloat)
            {
                for (var i = 0; i < Arkit52Channels.CanonicalOrder.Count; i++)
                {
                    updates.Add(new KeyValuePair<string, float>(
                        NormalizeKey(Arkit52Channels.CanonicalOrder[i]),
                        message.Values[i].FloatValue));
                }
                return true;
            }
        }

        // pair pattern: [string key, float value]*
        if ((message.Values.Count % 2) == 0)
        {
            var paired = true;
            for (var i = 0; i < message.Values.Count; i += 2)
            {
                if (message.Values[i].Kind != OscValueKind.String || message.Values[i + 1].Kind != OscValueKind.Float)
                {
                    paired = false;
                    break;
                }

                var key = NormalizeKey(message.Values[i].StringValue);
                if (string.IsNullOrWhiteSpace(key))
                {
                    continue;
                }
                updates.Add(new KeyValuePair<string, float>(key, message.Values[i + 1].FloatValue));
            }

            if (paired && updates.Count > 0)
            {
                return true;
            }
        }

        // multi-float vector payload by address
        var tail = NormalizeKey(ExtractAddressTail(message.Address));
        if (tail is "head" or "headrotation" or "headrot")
        {
            if (message.Values.Count < 3 ||
                message.Values[0].Kind != OscValueKind.Float ||
                message.Values[1].Kind != OscValueKind.Float ||
                message.Values[2].Kind != OscValueKind.Float)
            {
                return false;
            }

            updates.Add(new KeyValuePair<string, float>("headyaw", message.Values[0].FloatValue));
            updates.Add(new KeyValuePair<string, float>("headpitch", message.Values[1].FloatValue));
            updates.Add(new KeyValuePair<string, float>("headroll", message.Values[2].FloatValue));
            return true;
        }

        if (tail is "headpos" or "position")
        {
            if (message.Values.Count < 3 ||
                message.Values[0].Kind != OscValueKind.Float ||
                message.Values[1].Kind != OscValueKind.Float ||
                message.Values[2].Kind != OscValueKind.Float)
            {
                return false;
            }

            updates.Add(new KeyValuePair<string, float>("headposx", message.Values[0].FloatValue));
            updates.Add(new KeyValuePair<string, float>("headposy", message.Values[1].FloatValue));
            updates.Add(new KeyValuePair<string, float>("headposz", message.Values[2].FloatValue));
            return true;
        }

        if (tail is "upperbody" or "armspitch")
        {
            if (message.Values.Count < 4 ||
                message.Values[0].Kind != OscValueKind.Float ||
                message.Values[1].Kind != OscValueKind.Float ||
                message.Values[2].Kind != OscValueKind.Float ||
                message.Values[3].Kind != OscValueKind.Float)
            {
                return false;
            }

            updates.Add(new KeyValuePair<string, float>("leftshoulderpitch", message.Values[0].FloatValue));
            updates.Add(new KeyValuePair<string, float>("rightshoulderpitch", message.Values[1].FloatValue));
            updates.Add(new KeyValuePair<string, float>("leftupperarmpitch", message.Values[2].FloatValue));
            updates.Add(new KeyValuePair<string, float>("rightupperarmpitch", message.Values[3].FloatValue));
            return true;
        }

        return false;
    }

    private bool ApplyUpdates(List<KeyValuePair<string, float>> updates)
    {
        var mapped = false;
        foreach (var pair in updates)
        {
            if (ApplyMappedValue(pair.Key, pair.Value))
            {
                mapped = true;
            }
        }

        if (mapped)
        {
            SnapshotExpressionWeights();
        }
        return mapped;
    }

    private void SnapshotExpressionWeights()
    {
        _expressionSnapshot.Clear();
        foreach (var pair in _expressionCache)
        {
            _expressionSnapshot[pair.Key] = Clamp01(pair.Value);
        }
    }

    private bool ApplyMappedValue(string key, float value)
    {
        if (float.IsNaN(value) || float.IsInfinity(value))
        {
            return false;
        }

        var normalized = NormalizeKey(key);
        if (string.IsNullOrWhiteSpace(normalized))
        {
            return false;
        }

        if (IsUpperBodyChannel(normalized))
        {
            return ApplyUpperBodyMappedValue(normalized, value);
        }

        _expressionCache[normalized] = value;
        switch (normalized)
        {
            case "eyeblinkleft":
                _rawFrame.BlinkL = ApplyAdaptiveCalibration(normalized, Clamp01(value));
                _expressionCache[normalized] = _rawFrame.BlinkL;
                return true;
            case "eyeblinkright":
                _rawFrame.BlinkR = ApplyAdaptiveCalibration(normalized, Clamp01(value));
                _expressionCache[normalized] = _rawFrame.BlinkR;
                return true;
            case "jawopen":
                _rawFrame.MouthOpen = ApplyAdaptiveCalibration(normalized, Clamp01(value));
                _expressionCache[normalized] = _rawFrame.MouthOpen;
                return true;
            case "headyaw":
                _rawHeadYaw = value;
                _hasHeadYpr = true;
                return true;
            case "headpitch":
                _rawHeadPitch = value;
                _hasHeadYpr = true;
                return true;
            case "headroll":
                _rawHeadRoll = value;
                _hasHeadYpr = true;
                return true;
            case "headposx":
                _rawFrame.HeadPosX = value;
                return true;
            case "headposy":
                _rawFrame.HeadPosY = value;
                return true;
            case "headposz":
                _rawFrame.HeadPosZ = value;
                return true;
            default:
                _expressionCache[normalized] = ApplyAdaptiveCalibration(normalized, Clamp01(value));
                return true;
        }
    }

    private bool ApplyUpperBodyMappedValue(string normalizedKey, float value)
    {
        switch (normalizedKey)
        {
            case "leftshoulderpitch":
            case "lshoulderpitch":
                _rawLeftShoulderPitch = value;
                break;
            case "rightshoulderpitch":
            case "rshoulderpitch":
                _rawRightShoulderPitch = value;
                break;
            case "leftupperarmpitch":
            case "lupperarmpitch":
            case "lupparmpitch":
                _rawLeftUpperArmPitch = value;
                break;
            case "rightupperarmpitch":
            case "rupperarmpitch":
            case "rupparmpitch":
                _rawRightUpperArmPitch = value;
                break;
            default:
                return false;
        }

        var packetUtc = DateTimeOffset.UtcNow;
        UpdateUpperBodyPoseFromValues(
            _rawLeftShoulderPitch,
            _rawRightShoulderPitch,
            _rawLeftUpperArmPitch,
            _rawRightUpperArmPitch,
            1.0f,
            packetUtc,
            "osc");
        return true;
    }

    private float ApplyAdaptiveCalibration(string key, float value)
    {
        var normalized = NormalizeKey(key);
        if (string.IsNullOrWhiteSpace(normalized))
        {
            return Clamp01(value);
        }
        var profile = GetCalibrationProfile(normalized);

        if (!_calibrationBaseline.TryGetValue(normalized, out var baseline))
        {
            baseline = value;
            _calibrationBaseline[normalized] = baseline;
        }
        else
        {
            var alpha = _calibrationFrames < CalibrationWarmupFrames
                ? profile.WarmupAlpha
                : profile.SteadyAlpha;
            baseline = Ema(baseline, value, alpha);
            _calibrationBaseline[normalized] = baseline;
        }

        if (_calibrationFrames < CalibrationWarmupFrames)
        {
            _calibrationState = "calibrating";
            return Clamp01(value);
        }

        var denom = MathF.Max(profile.MinDenominator, 1.0f - baseline);
        var normalizedValue = (value - baseline) / denom;
        _calibrationState = "stable";
        return Clamp01(normalizedValue);
    }

    private static ChannelCalibrationProfile GetCalibrationProfile(string normalizedChannel)
    {
        if (normalizedChannel.StartsWith("eye", StringComparison.Ordinal) ||
            normalizedChannel.StartsWith("blink", StringComparison.Ordinal))
        {
            return new ChannelCalibrationProfile(0.08f, 0.02f, 0.12f);
        }

        if (normalizedChannel.StartsWith("mouth", StringComparison.Ordinal) ||
            normalizedChannel.StartsWith("jaw", StringComparison.Ordinal) ||
            normalizedChannel.StartsWith("tongue", StringComparison.Ordinal))
        {
            return new ChannelCalibrationProfile(0.05f, 0.015f, 0.16f);
        }

        if (normalizedChannel.StartsWith("brow", StringComparison.Ordinal) ||
            normalizedChannel.StartsWith("nose", StringComparison.Ordinal) ||
            normalizedChannel.StartsWith("cheek", StringComparison.Ordinal))
        {
            return new ChannelCalibrationProfile(0.04f, 0.012f, 0.18f);
        }

        return new ChannelCalibrationProfile(0.06f, 0.01f, 0.18f);
    }

    private void AdvanceCalibration()
    {
        _calibrationFrames = Math.Clamp(_calibrationFrames + 1, 0, 1000000);
        if (_calibrationFrames < CalibrationWarmupFrames)
        {
            _calibrationState = "calibrating";
        }
        else if (_calibrationFrames > (CalibrationWarmupFrames * 5))
        {
            _calibrationState = "stable";
        }
    }

    private void EvaluateSourceArbitration(int currentAgeMs)
    {
        _switchBlockedReason = string.Empty;
        var now = DateTimeOffset.UtcNow;
        if (_options.SourceType == TrackingSourceType.WebcamMediapipe)
        {
            _activeRuntimeSource = TrackingRuntimeSource.Webcam;
            return;
        }
        if (_options.SourceType == TrackingSourceType.OscIfacial)
        {
            _activeRuntimeSource = TrackingRuntimeSource.Ifacial;
            return;
        }

        if (_options.SourceLockMode == TrackingSourceLockMode.IfacialLocked)
        {
            _activeRuntimeSource = TrackingRuntimeSource.Ifacial;
            if (_lastWebcamPacketUtc != DateTimeOffset.MinValue)
            {
                _switchBlockedReason = "source-lock:ifacial";
            }
            return;
        }

        if (_options.SourceLockMode == TrackingSourceLockMode.WebcamLocked)
        {
            _activeRuntimeSource = TrackingRuntimeSource.Webcam;
            if (_lastIfacialPacketUtc != DateTimeOffset.MinValue)
            {
                _switchBlockedReason = "source-lock:webcam";
            }
            return;
        }

        var ifacialAge = _lastIfacialPacketUtc == DateTimeOffset.MinValue
            ? int.MaxValue
            : (int)Math.Max(0.0, (now - _lastIfacialPacketUtc).TotalMilliseconds);
        var webcamAge = _lastWebcamPacketUtc == DateTimeOffset.MinValue
            ? int.MaxValue
            : (int)Math.Max(0.0, (now - _lastWebcamPacketUtc).TotalMilliseconds);
        var ageMs = currentAgeMs > 0 ? currentAgeMs : ifacialAge;
        var ifacialConfidence = ComputeIfacialConfidence(ifacialAge);
        var webcamConfidence = ComputeWebcamConfidence(webcamAge);
        var cooldownActive = _autoStabilityTuningEnabled && now < _sourceSwitchCooldownUntilUtc;
        var currentSourceHoldMs = _activeSourceSinceUtc == DateTimeOffset.MinValue
            ? int.MaxValue
            : (int)Math.Max(0.0, (now - _activeSourceSinceUtc).TotalMilliseconds);
        var minHoldMs = _autoStabilityTuningEnabled ? 1600 : 700;
        var fallbackFailureThreshold = _autoStabilityTuningEnabled ? 3 : 4;

        if (_autoStabilityTuningEnabled && _activeRuntimeSource != TrackingRuntimeSource.None)
        {
            _switchBlockedReason = cooldownActive
                ? $"switch-cooldown:{Math.Max(0, (int)(_sourceSwitchCooldownUntilUtc - now).TotalMilliseconds)}ms"
                : string.Empty;
        }

        var shouldFallback = (ageMs >= _ifacialFallbackAgeMs ||
                              _ifacialConsecutiveFailures >= fallbackFailureThreshold ||
                              (_autoStabilityTuningEnabled && ifacialConfidence < 0.22 && _ifacialConsecutiveFailures >= 2)) &&
                             webcamAge < (_options.StaleTimeoutMs * 2) &&
                             webcamConfidence >= (_autoStabilityTuningEnabled ? 0.20 : 0.0);
        if (_autoStabilityTuningEnabled && (cooldownActive || currentSourceHoldMs < minHoldMs))
        {
            shouldFallback = false;
        }
        if (shouldFallback && _activeRuntimeSource != TrackingRuntimeSource.Webcam)
        {
            _activeRuntimeSource = TrackingRuntimeSource.Webcam;
            _fallbackCount++;
            _ifacialRecoveryStreak = 0;
            RegisterSourceSwitch(now, "fallback:webcam");
        }

        if (_activeRuntimeSource == TrackingRuntimeSource.Webcam)
        {
            var ifacialRecovered = ifacialAge <= _ifacialRecoveryAgeMs &&
                                   _ifacialConsecutiveFailures == 0 &&
                                   ifacialConfidence >= (_autoStabilityTuningEnabled ? 0.32 : 0.0);
            if (ifacialRecovered)
            {
                _ifacialRecoveryStreak++;
                if (_ifacialRecoveryStreak >= _ifacialRecoveryStreakRequired)
                {
                    if (!_autoStabilityTuningEnabled || (!cooldownActive && currentSourceHoldMs >= minHoldMs))
                    {
                        _activeRuntimeSource = TrackingRuntimeSource.Ifacial;
                        _ifacialRecoveryStreak = 0;
                        RegisterSourceSwitch(now, "recover:ifacial");
                    }
                }
            }
            else
            {
                _ifacialRecoveryStreak = 0;
            }
        }
        else if (_activeRuntimeSource == TrackingRuntimeSource.None)
        {
            _activeRuntimeSource = ifacialAge < int.MaxValue ? TrackingRuntimeSource.Ifacial : TrackingRuntimeSource.Webcam;
            _activeSourceSinceUtc = now;
        }
    }

    private bool ShouldConsumeIfacialFrame()
    {
        if (_options.SourceType == TrackingSourceType.WebcamMediapipe)
        {
            return false;
        }
        if (_options.SourceType == TrackingSourceType.OscIfacial)
        {
            return true;
        }

        if (_options.SourceLockMode == TrackingSourceLockMode.IfacialLocked)
        {
            return true;
        }
        if (_options.SourceLockMode == TrackingSourceLockMode.WebcamLocked)
        {
            return false;
        }
        return _activeRuntimeSource != TrackingRuntimeSource.Webcam;
    }

    private bool ShouldConsumeWebcamFrame()
    {
        if (_options.SourceType == TrackingSourceType.WebcamMediapipe)
        {
            return true;
        }
        if (_options.SourceType == TrackingSourceType.OscIfacial)
        {
            return false;
        }

        if (_options.SourceLockMode == TrackingSourceLockMode.IfacialLocked)
        {
            return false;
        }
        if (_options.SourceLockMode == TrackingSourceLockMode.WebcamLocked)
        {
            return true;
        }
        return _activeRuntimeSource == TrackingRuntimeSource.Webcam;
    }

    private string BuildConfidenceSummary()
    {
        var now = DateTimeOffset.UtcNow;
        var ifacialAge = _lastIfacialPacketUtc == DateTimeOffset.MinValue
            ? int.MaxValue
            : (int)Math.Max(0.0, (now - _lastIfacialPacketUtc).TotalMilliseconds);
        var webcamAge = _lastWebcamPacketUtc == DateTimeOffset.MinValue
            ? int.MaxValue
            : (int)Math.Max(0.0, (now - _lastWebcamPacketUtc).TotalMilliseconds);

        var ifacialConfidence = ComputeIfacialConfidence(ifacialAge);
        if (_ifacialConsecutiveFailures > 0)
        {
            ifacialConfidence *= Math.Clamp(1.0 - (_ifacialConsecutiveFailures * 0.15), 0.0, 1.0);
        }

        var webcamConfidence = ComputeWebcamConfidence(webcamAge);

        return string.Create(
            CultureInfo.InvariantCulture,
            $"ifacial={ifacialConfidence:F2},webcam={webcamConfidence:F2}");
    }

    private static double ComputeIfacialConfidence(int ifacialAgeMs)
    {
        return ifacialAgeMs == int.MaxValue
            ? 0.0
            : Math.Clamp(1.0 - (ifacialAgeMs / 900.0), 0.0, 1.0);
    }

    private double ComputeWebcamConfidence(int webcamAgeMs)
    {
        return (_latestMediapipePacket is null)
            ? (webcamAgeMs == int.MaxValue ? 0.0 : Math.Clamp(1.0 - (webcamAgeMs / 1200.0), 0.0, 1.0))
            : Math.Clamp(_latestMediapipePacket.SourceConfidence, 0.0, 1.0);
    }

    private void RegisterSourceSwitch(DateTimeOffset now, string reason)
    {
        if (_sourceSwitchWindowStartUtc == DateTimeOffset.MinValue ||
            (now - _sourceSwitchWindowStartUtc).TotalSeconds > 30.0)
        {
            _sourceSwitchWindowStartUtc = now;
            _recentSourceSwitchCount = 0;
        }

        _recentSourceSwitchCount++;
        _lastSourceSwitchReason = reason;
        _lastSourceSwitchUtc = now;
        _activeSourceSinceUtc = now;
        if (_autoStabilityTuningEnabled)
        {
            _sourceSwitchCooldownUntilUtc = now.AddMilliseconds(900);
        }
    }

    private int GetRecentSourceSwitchCount(DateTimeOffset now)
    {
        if (_sourceSwitchWindowStartUtc == DateTimeOffset.MinValue)
        {
            return 0;
        }

        if ((now - _sourceSwitchWindowStartUtc).TotalSeconds > 30.0)
        {
            _sourceSwitchWindowStartUtc = now;
            _recentSourceSwitchCount = 0;
            return 0;
        }

        return _recentSourceSwitchCount;
    }

    private void MaybeApplyAutoStabilityTuning(DateTimeOffset now)
    {
        if (!_autoStabilityTuningEnabled || _options.SourceType != TrackingSourceType.HybridAuto)
        {
            _poseDeadbandDeg = Math.Clamp(_options.PoseDeadbandDeg, 0.0f, 3.0f);
            return;
        }

        if (_lastStabilityTuneUtc != DateTimeOffset.MinValue &&
            (now - _lastStabilityTuneUtc).TotalMilliseconds < 500.0)
        {
            return;
        }
        _lastStabilityTuneUtc = now;

        var received = Math.Max(1.0, _diagnostics.ReceivedPackets);
        var parseRatio = _diagnostics.ParseErrors / received;
        var dropRatio = _diagnostics.DroppedPackets / received;
        var switchRate = GetRecentSourceSwitchCount(now) / 30.0;
        var instabilityScore = (parseRatio * 1.4) + (dropRatio * 1.0) + (switchRate * 0.7);
        var baseDeadband = Math.Clamp(_options.PoseDeadbandDeg, 0.0f, 3.0f);

        float targetDeadband;
        if (instabilityScore >= 0.30)
        {
            targetDeadband = MathF.Min(2.4f, baseDeadband + 0.70f);
        }
        else if (instabilityScore >= 0.15)
        {
            targetDeadband = MathF.Min(2.0f, baseDeadband + 0.35f);
        }
        else if (instabilityScore <= 0.05)
        {
            targetDeadband = MathF.Max(0.0f, baseDeadband - 0.12f);
        }
        else
        {
            targetDeadband = baseDeadband;
        }

        _poseDeadbandDeg = Ema(_poseDeadbandDeg, targetDeadband, 0.16f);
    }

    private static int GetPacketAgeMs(DateTimeOffset packetUtc)
    {
        return packetUtc == DateTimeOffset.MinValue
            ? int.MaxValue
            : (int)Math.Max(0.0, (DateTimeOffset.UtcNow - packetUtc).TotalMilliseconds);
    }

    private void ApplyNoInputWarningIfNeeded(int ifacialAgeMs, int webcamAgeMs)
    {
        if (!_diagnostics.IsActive)
        {
            return;
        }

        var hasIfacial = ifacialAgeMs != int.MaxValue;
        var hasWebcam = webcamAgeMs != int.MaxValue;
        var elapsedMs = _trackingStartedUtc == DateTimeOffset.MinValue
            ? 0
            : (int)Math.Max(0.0, (DateTimeOffset.UtcNow - _trackingStartedUtc).TotalMilliseconds);

        if ((hasIfacial || hasWebcam) && IsNoInputWarningCode(_diagnostics.LastErrorCode))
        {
            _diagnostics = _diagnostics with { LastErrorCode = string.Empty };
        }

        if (elapsedMs < NoActiveInputWarnDelayMs)
        {
            return;
        }

        var warningCode = string.Empty;
        switch (_options.SourceType)
        {
            case TrackingSourceType.OscIfacial:
                if (!hasIfacial)
                {
                    warningCode = "TRACKING_IFACIAL_NO_PACKET";
                }
                break;
            case TrackingSourceType.WebcamMediapipe:
                if (!hasWebcam)
                {
                    warningCode = _webcamRuntimeUnavailable
                        ? "TRACKING_WEBCAM_RUNTIME_UNAVAILABLE"
                        : "TRACKING_WEBCAM_NO_FRAME";
                }
                break;
            case TrackingSourceType.HybridAuto:
                if (!hasIfacial && !hasWebcam)
                {
                    warningCode = _webcamRuntimeUnavailable
                        ? "TRACKING_WEBCAM_RUNTIME_UNAVAILABLE"
                        : "TRACKING_NO_ACTIVE_INPUT_SOURCE";
                }
                break;
        }

        if (string.IsNullOrWhiteSpace(warningCode))
        {
            return;
        }

        if (!string.IsNullOrWhiteSpace(_diagnostics.LastErrorCode) &&
            !IsNoInputWarningCode(_diagnostics.LastErrorCode))
        {
            return;
        }

        _diagnostics = _diagnostics with { LastErrorCode = warningCode };
    }

    private static bool IsNoInputWarningCode(string code)
    {
        return string.Equals(code, "TRACKING_NO_ACTIVE_INPUT_SOURCE", StringComparison.Ordinal) ||
               string.Equals(code, "TRACKING_IFACIAL_NO_PACKET", StringComparison.Ordinal) ||
               string.Equals(code, "TRACKING_WEBCAM_NO_FRAME", StringComparison.Ordinal) ||
               string.Equals(code, "TRACKING_WEBCAM_RUNTIME_UNAVAILABLE", StringComparison.Ordinal);
    }

    private void ApplySourceSwitchWarningIfNeeded(DateTimeOffset now)
    {
        if (!_autoStabilityTuningEnabled || _options.SourceType != TrackingSourceType.HybridAuto)
        {
            if (string.Equals(_diagnostics.LastErrorCode, "TRACKING_SOURCE_SWITCH_THRASH", StringComparison.Ordinal))
            {
                _diagnostics = _diagnostics with { LastErrorCode = string.Empty };
            }
            return;
        }

        var switchCount = GetRecentSourceSwitchCount(now);
        var hasThrash = switchCount >= 6;
        if (hasThrash && string.IsNullOrWhiteSpace(_diagnostics.LastErrorCode))
        {
            _diagnostics = _diagnostics with { LastErrorCode = "TRACKING_SOURCE_SWITCH_THRASH" };
            return;
        }

        if (!hasThrash && string.Equals(_diagnostics.LastErrorCode, "TRACKING_SOURCE_SWITCH_THRASH", StringComparison.Ordinal))
        {
            _diagnostics = _diagnostics with { LastErrorCode = string.Empty };
        }
    }

    private static string ToActiveSourceLabel(TrackingRuntimeSource source)
    {
        return source switch
        {
            TrackingRuntimeSource.Ifacial => "ifacial",
            TrackingRuntimeSource.Webcam => "webcam",
            _ => "none",
        };
    }

    private TrackingRuntimeSource ResolvePreferredUpperBodySource()
    {
        return _options.SourceType switch
        {
            TrackingSourceType.OscIfacial => TrackingRuntimeSource.Ifacial,
            TrackingSourceType.WebcamMediapipe => TrackingRuntimeSource.Webcam,
            _ => _activeRuntimeSource,
        };
    }

    private string ResolveUpperBodyActiveSourceLabel()
    {
        if (!_upperBodyEnabled)
        {
            return "none";
        }

        if (!string.IsNullOrWhiteSpace(_upperBodyActiveSource))
        {
            return _upperBodyActiveSource;
        }

        var oscAge = GetPacketAgeMs(_lastUpperBodyOscPacketUtc);
        var webcamAge = GetPacketAgeMs(_lastUpperBodyWebcamPacketUtc);
        if (oscAge <= _options.StaleTimeoutMs && oscAge <= webcamAge)
        {
            return "osc";
        }
        if (webcamAge <= _options.StaleTimeoutMs)
        {
            return "webcam";
        }
        return "none";
    }

    private void ApplySmoothing()
    {
        if (!_hasSmoothedFrame)
        {
            _smoothedFrame = _rawFrame;
            _smoothedHeadYaw = _rawHeadYaw;
            _smoothedHeadPitch = _rawHeadPitch;
            _smoothedHeadRoll = _rawHeadRoll;
            _hasSmoothedFrame = true;
        }
        else
        {
            _smoothedFrame.HeadPosX = Ema(_smoothedFrame.HeadPosX, _rawFrame.HeadPosX, _poseAlpha);
            _smoothedFrame.HeadPosY = Ema(_smoothedFrame.HeadPosY, _rawFrame.HeadPosY, _poseAlpha);
            _smoothedFrame.HeadPosZ = Ema(_smoothedFrame.HeadPosZ, _rawFrame.HeadPosZ, _poseAlpha);
            _smoothedFrame.BlinkL = Ema(_smoothedFrame.BlinkL, Clamp01(_rawFrame.BlinkL), _expressionAlpha);
            _smoothedFrame.BlinkR = Ema(_smoothedFrame.BlinkR, Clamp01(_rawFrame.BlinkR), _expressionAlpha);
            _smoothedFrame.MouthOpen = Ema(_smoothedFrame.MouthOpen, Clamp01(_rawFrame.MouthOpen), _expressionAlpha);

            if (_hasHeadYpr)
            {
                _smoothedHeadYaw = SmoothPoseAxis(_smoothedHeadYaw, _rawHeadYaw, _poseAlpha);
                _smoothedHeadPitch = SmoothPoseAxis(_smoothedHeadPitch, _rawHeadPitch, _poseAlpha);
                _smoothedHeadRoll = SmoothPoseAxis(_smoothedHeadRoll, _rawHeadRoll, _poseAlpha);
            }
        }

        var yaw = _smoothedHeadYaw;
        var pitch = _smoothedHeadPitch;
        var roll = _smoothedHeadRoll;
        if (_hasRefYpr)
        {
            yaw -= _refYaw;
            pitch -= _refPitch;
            roll -= _refRoll;
        }

        _lastOutputFrame = _smoothedFrame;
        _lastOutputFrame.HeadPosX -= _headPosOffsetX;
        _lastOutputFrame.HeadPosY -= _headPosOffsetY;
        _lastOutputFrame.HeadPosZ -= _headPosOffsetZ;
        _lastOutputFrame.BlinkL = Clamp01(_lastOutputFrame.BlinkL);
        _lastOutputFrame.BlinkR = Clamp01(_lastOutputFrame.BlinkR);
        _lastOutputFrame.MouthOpen = Clamp01(_lastOutputFrame.MouthOpen);

        var q = ToQuaternion(pitch, yaw, roll);
        _lastOutputFrame.HeadRotX = q.x;
        _lastOutputFrame.HeadRotY = q.y;
        _lastOutputFrame.HeadRotZ = q.z;
        _lastOutputFrame.HeadRotW = q.w;
        if (_recenterStabilizeFramesRemaining > 0)
        {
            _recenterStabilizeFramesRemaining--;
        }
    }

    private void UpdateInputFps()
    {
        var now = DateTimeOffset.UtcNow;
        if (_lastFpsSampleUtc == DateTimeOffset.MinValue)
        {
            _lastFpsSampleUtc = now;
            _fpsSampleCount = 0;
        }

        _fpsSampleCount++;
        var elapsed = (now - _lastFpsSampleUtc).TotalSeconds;
        if (elapsed < 0.5)
        {
            return;
        }

        var instant = _fpsSampleCount / elapsed;
        _smoothedInputFps = _smoothedInputFps <= 0.001 ? instant : (_smoothedInputFps * 0.7) + (instant * 0.3);
        _fpsSampleCount = 0;
        _lastFpsSampleUtc = now;
    }

    private void UpdateCaptureFps(double observedCaptureFps)
    {
        lock (_sync)
        {
            if (observedCaptureFps > 0.0 && !double.IsNaN(observedCaptureFps) && !double.IsInfinity(observedCaptureFps))
            {
                _smoothedCaptureFps = _smoothedCaptureFps <= 0.001
                    ? observedCaptureFps
                    : (_smoothedCaptureFps * 0.7) + (observedCaptureFps * 0.3);
                return;
            }

            var now = DateTimeOffset.UtcNow;
            if (_lastCaptureSampleUtc == DateTimeOffset.MinValue)
            {
                _lastCaptureSampleUtc = now;
                _captureSampleCount = 0;
            }

            _captureSampleCount++;
            var elapsed = (now - _lastCaptureSampleUtc).TotalSeconds;
            if (elapsed < 0.5)
            {
                return;
            }

            var instant = _captureSampleCount / elapsed;
            _smoothedCaptureFps = _smoothedCaptureFps <= 0.001 ? instant : (_smoothedCaptureFps * 0.7) + (instant * 0.3);
            _captureSampleCount = 0;
            _lastCaptureSampleUtc = now;
        }
    }

    private void UpdateInferenceMs(double elapsedMs)
    {
        lock (_sync)
        {
            if (double.IsNaN(elapsedMs) || double.IsInfinity(elapsedMs) || elapsedMs < 0.0)
            {
                return;
            }

            _smoothedInferenceMs = _smoothedInferenceMs <= 0.001
                ? elapsedMs
                : (_smoothedInferenceMs * 0.7) + (elapsedMs * 0.3);
        }
    }

    private void ApplyLatencyProfileTuning(TrackingLatencyProfile profile)
    {
        switch (profile)
        {
            case TrackingLatencyProfile.LowLatency:
                _poseAlpha = 0.55f;
                _expressionAlpha = 0.78f;
                _ifacialFallbackAgeMs = 420;
                _ifacialRecoveryAgeMs = 130;
                _ifacialRecoveryStreakRequired = 4;
                break;
            case TrackingLatencyProfile.Stable:
                _poseAlpha = 0.25f;
                _expressionAlpha = 0.46f;
                _ifacialFallbackAgeMs = 850;
                _ifacialRecoveryAgeMs = 220;
                _ifacialRecoveryStreakRequired = 14;
                break;
            default:
                _poseAlpha = 0.35f;
                _expressionAlpha = 0.60f;
                _ifacialFallbackAgeMs = 650;
                _ifacialRecoveryAgeMs = 180;
                _ifacialRecoveryStreakRequired = 10;
                break;
        }
    }

    private void ApplyPoseFilterTuning(PoseFilterProfile profile, float deadbandDeg)
    {
        _poseDeadbandDeg = Math.Clamp(float.IsFinite(deadbandDeg) ? deadbandDeg : 0.9f, 0.0f, 3.0f);
        switch (profile)
        {
            case PoseFilterProfile.Reactive:
                _poseAdaptiveMinAlpha = 0.40f;
                _poseAdaptiveMaxAlpha = 0.82f;
                _poseAdaptiveGain = 0.045f;
                break;
            case PoseFilterProfile.Balanced:
                _poseAdaptiveMinAlpha = 0.28f;
                _poseAdaptiveMaxAlpha = 0.68f;
                _poseAdaptiveGain = 0.032f;
                break;
            default:
                _poseAdaptiveMinAlpha = 0.18f;
                _poseAdaptiveMaxAlpha = 0.52f;
                _poseAdaptiveGain = 0.024f;
                break;
        }
    }

    private void ApplyUpperBodySmoothingTuning(UpperBodySmoothingProfile profile)
    {
        _upperBodyAlpha = profile switch
        {
            UpperBodySmoothingProfile.Reactive => 0.58f,
            UpperBodySmoothingProfile.Stable => 0.24f,
            _ => 0.38f,
        };
    }

    private static bool IsUpperBodyChannel(string normalizedKey)
    {
        return normalizedKey is
            "leftshoulderpitch" or
            "rightshoulderpitch" or
            "leftupperarmpitch" or
            "rightupperarmpitch" or
            "lshoulderpitch" or
            "rshoulderpitch" or
            "lupperarmpitch" or
            "rupperarmpitch" or
            "lupparmpitch" or
            "rupparmpitch";
    }

    private bool HasUpperBodyResidual()
    {
        return Math.Abs(_smoothedLeftShoulderPitch) >= 0.15f ||
               Math.Abs(_smoothedRightShoulderPitch) >= 0.15f ||
               Math.Abs(_smoothedLeftUpperArmPitch) >= 0.15f ||
               Math.Abs(_smoothedRightUpperArmPitch) >= 0.15f;
    }

    private static float ClampShoulderPitch(float value)
    {
        return Math.Clamp(value, -55.0f, 55.0f);
    }

    private static float ClampUpperArmPitch(float value)
    {
        return Math.Clamp(value, -90.0f, 90.0f);
    }

    private float SmoothPoseAxis(float current, float incoming, float baseAlpha)
    {
        var delta = incoming - current;
        if (MathF.Abs(delta) < _poseDeadbandDeg)
        {
            return current;
        }

        var adaptive = Math.Clamp(
            MathF.Max(baseAlpha, _poseAdaptiveMinAlpha) + (MathF.Abs(delta) * _poseAdaptiveGain),
            _poseAdaptiveMinAlpha,
            _poseAdaptiveMaxAlpha);
        if (_recenterStabilizeFramesRemaining > 0)
        {
            adaptive = MathF.Min(adaptive, MathF.Max(0.10f, baseAlpha * 0.65f));
        }

        return current + (delta * adaptive);
    }

    private void UpdateCaptureStageMs(double captureStageMs)
    {
        if (double.IsNaN(captureStageMs) || double.IsInfinity(captureStageMs) || captureStageMs < 0.0)
        {
            return;
        }

        _smoothedCaptureStageMs = _smoothedCaptureStageMs <= 0.001
            ? captureStageMs
            : (_smoothedCaptureStageMs * 0.7) + (captureStageMs * 0.3);
    }

    private void UpdateParseStageMs(double parseStageMs)
    {
        if (double.IsNaN(parseStageMs) || double.IsInfinity(parseStageMs) || parseStageMs < 0.0)
        {
            return;
        }

        _smoothedParseStageMs = _smoothedParseStageMs <= 0.001
            ? parseStageMs
            : (_smoothedParseStageMs * 0.7) + (parseStageMs * 0.3);
    }

    private void UpdateSmoothStageMs(double smoothStageMs)
    {
        if (double.IsNaN(smoothStageMs) || double.IsInfinity(smoothStageMs) || smoothStageMs < 0.0)
        {
            return;
        }

        _smoothedSmoothStageMs = _smoothedSmoothStageMs <= 0.001
            ? smoothStageMs
            : (_smoothedSmoothStageMs * 0.7) + (smoothStageMs * 0.3);
    }

    private void UpdateSubmitStageMs(double submitStageMs)
    {
        if (double.IsNaN(submitStageMs) || double.IsInfinity(submitStageMs) || submitStageMs < 0.0)
        {
            return;
        }

        _smoothedSubmitStageMs = _smoothedSubmitStageMs <= 0.001
            ? submitStageMs
            : (_smoothedSubmitStageMs * 0.7) + (submitStageMs * 0.3);
    }

    private void RecordLatencySample(double latencyMs)
    {
        if (double.IsNaN(latencyMs) || double.IsInfinity(latencyMs))
        {
            return;
        }

        var bounded = Math.Clamp(latencyMs, 0.0, 5000.0);
        _latencySamples.Enqueue(bounded);
        var insertIndex = _latencySortedSamples.BinarySearch(bounded);
        if (insertIndex < 0)
        {
            insertIndex = ~insertIndex;
        }

        _latencySortedSamples.Insert(insertIndex, bounded);
        _latencySampleSum += bounded;
        while (_latencySamples.Count > LatencySampleWindow)
        {
            var removed = _latencySamples.Dequeue();
            _latencySampleSum -= removed;
            var removeIndex = _latencySortedSamples.BinarySearch(removed);
            if (removeIndex >= 0)
            {
                _latencySortedSamples.RemoveAt(removeIndex);
            }
        }

        if (_latencySortedSamples.Count == 0)
        {
            _latencyAvgMs = 0.0;
            _latencyP95Ms = 0.0;
            return;
        }

        _latencyAvgMs = _latencySampleSum / _latencySortedSamples.Count;
        var p95Index = Math.Clamp((int)Math.Ceiling((_latencySortedSamples.Count * 0.95) - 1.0), 0, _latencySortedSamples.Count - 1);
        _latencyP95Ms = _latencySortedSamples[p95Index];
    }

    private static (float x, float y, float z, float w) ToQuaternion(float pitchDeg, float yawDeg, float rollDeg)
    {
        var pitch = DegreesToRadians(pitchDeg) * 0.5f;
        var yaw = DegreesToRadians(yawDeg) * 0.5f;
        var roll = DegreesToRadians(rollDeg) * 0.5f;

        var cp = MathF.Cos(pitch);
        var sp = MathF.Sin(pitch);
        var cy = MathF.Cos(yaw);
        var sy = MathF.Sin(yaw);
        var cr = MathF.Cos(roll);
        var sr = MathF.Sin(roll);

        var w = (cr * cp * cy) + (sr * sp * sy);
        var x = (sr * cp * cy) - (cr * sp * sy);
        var y = (cr * sp * cy) + (sr * cp * sy);
        var z = (cr * cp * sy) - (sr * sp * cy);
        NormalizeQuaternion(ref x, ref y, ref z, ref w);
        return (x, y, z, w);
    }

    private static float DegreesToRadians(float degrees) => degrees * (MathF.PI / 180.0f);

    private static float Ema(float current, float incoming, float alpha)
    {
        var a = Math.Clamp(alpha, 0.0f, 1.0f);
        return current + ((incoming - current) * a);
    }

    private static float Clamp01(float value) => Math.Clamp(value, 0.0f, 1.0f);

    private static string ExtractAddressTail(string address)
    {
        if (string.IsNullOrWhiteSpace(address))
        {
            return string.Empty;
        }

        var trimmed = address.Trim();
        if (trimmed.EndsWith("/", StringComparison.Ordinal))
        {
            trimmed = trimmed[..^1];
        }
        var idx = trimmed.LastIndexOf('/');
        return idx >= 0 ? trimmed[(idx + 1)..] : trimmed;
    }

    private static string NormalizeKey(string raw)
    {
        return Arkit52Channels.NormalizeKey(raw);
    }

    private static void NormalizeQuaternion(ref float x, ref float y, ref float z, ref float w)
    {
        var len = MathF.Sqrt((x * x) + (y * y) + (z * z) + (w * w));
        if (len <= 1e-6f)
        {
            x = 0.0f;
            y = 0.0f;
            z = 0.0f;
            w = 1.0f;
            return;
        }

        var inv = 1.0f / len;
        x *= inv;
        y *= inv;
        z *= inv;
        w *= inv;
    }

    private static NcTrackingFrame BuildNeutralFrame()
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

    private sealed record MediapipeSidecarLaunchConfig(
        bool IsValid,
        string Executable,
        string Arguments,
        string ErrorMessage);

    private sealed record MediapipePythonCandidate(
        string Executable,
        string ArgumentPrefix,
        string DisplayName);

    private sealed record MediapipePythonLaunchConfig(
        bool IsValid,
        string Executable,
        string ArgumentPrefix,
        string ErrorMessage);

    private sealed record MediapipeFramePacket(
        int SchemaVersion,
        long FrameId,
        float HeadYawDeg,
        float HeadPitchDeg,
        float HeadRollDeg,
        float HeadPosX,
        float HeadPosY,
        float HeadPosZ,
        float BlinkLeft,
        float BlinkRight,
        float MouthOpen,
        float Smile,
        double CaptureFps,
        double InferenceMs,
        float SourceConfidence,
        long SourceTimestampUnixMs,
        double ParseMs,
        IReadOnlyDictionary<string, float> BlendshapeWeights,
        float LeftShoulderPitchDeg,
        float RightShoulderPitchDeg,
        float LeftUpperArmPitchDeg,
        float RightUpperArmPitchDeg,
        float UpperBodyConfidence);

    private readonly record struct ChannelCalibrationProfile(
        float WarmupAlpha,
        float SteadyAlpha,
        float MinDenominator);

    private static readonly Regex IfmDelimitedPairRegex = new(
        @"(?i)([A-Za-z][A-Za-z0-9_]{1,63})\s*[:=\-]\s*(-?\d+(?:\.\d+)?)",
        RegexOptions.Compiled | RegexOptions.CultureInvariant);
    private static readonly Regex IfmVersionRegex = new(
        @"(?i)\bversion\s*[:=\-]\s*(\d+)",
        RegexOptions.Compiled | RegexOptions.CultureInvariant);
    private static readonly IReadOnlySet<string> IfmLeftRightAliasStems = new HashSet<string>(
        new[]
        {
            "eyeblink",
            "mouthsmile",
            "browdown",
            "cheeksquint",
            "eyewide",
            "eyesquint",
            "eyelookin",
            "eyelookout",
            "eyelookup",
            "eyelookdown",
            "mouthfrown",
            "mouthdimple",
            "mouthstretch",
            "mouthpress",
            "mouthlowerdown",
            "mouthupperup",
            "browouterup",
            "nosesneer",
        },
        StringComparer.OrdinalIgnoreCase);

    private readonly record struct OscMessage(string Address, string TypeTag, IReadOnlyList<OscValue> Values);
    private readonly record struct OscValue(OscValueKind Kind, float FloatValue, string StringValue);
    private enum TrackingRuntimeSource
    {
        None = 0,
        Ifacial = 1,
        Webcam = 2,
    }

    private enum OscValueKind
    {
        Float = 0,
        String = 1,
    }

    private enum PacketParseFailure
    {
        Unknown = 0,
        EmptyPacket = 1,
        UnsupportedPacket = 2,
        UnsupportedTypeTag = 3,
        ProtocolMismatchVmc = 4,
        NoMappedChannels = 5,
        IfmMalformed = 6,
        IfmUnsupportedVersion = 7,
    }
}
