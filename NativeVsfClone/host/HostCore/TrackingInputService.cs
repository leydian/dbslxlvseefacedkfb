using System.Diagnostics;
using System.Globalization;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Diagnostics.CodeAnalysis;

namespace HostCore;

public sealed class TrackingInputService : ITrackingInputService
{
    private const ushort DefaultListenPort = 49983;
    private const int DefaultStaleTimeoutMs = 500;
    private const int IfacialFallbackAgeMs = 650;
    private const int IfacialRecoveryAgeMs = 180;
    private const int IfacialRecoveryStreakRequired = 10;
    private const int CalibrationWarmupFrames = 90;

    private readonly object _sync = new();
    private readonly Dictionary<string, float> _expressionCache = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, float> _expressionSnapshot = new(StringComparer.OrdinalIgnoreCase);

    private UdpClient? _udpClient;
    private CancellationTokenSource? _cts;
    private Task? _receiveTask;
    private TrackingStartOptions _options = new(DefaultListenPort, DefaultStaleTimeoutMs, TrackingSourceType.OscIfacial, string.Empty, 30, 10, 10);
    private TrackingDiagnostics _diagnostics = new(false, "unknown", 0.0, 0.0, 0.0, int.MaxValue, true, 0, 0, 0, "stopped", TrackingSourceType.OscIfacial, "idle");

    private DateTimeOffset _lastPacketUtc = DateTimeOffset.MinValue;
    private DateTimeOffset _lastFpsSampleUtc = DateTimeOffset.MinValue;
    private DateTimeOffset _lastCaptureSampleUtc = DateTimeOffset.MinValue;
    private int _fpsSampleCount;
    private int _captureSampleCount;
    private double _smoothedInputFps;
    private double _smoothedCaptureFps;
    private double _smoothedInferenceMs;

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
    private int _calibrationFrames;
    private readonly Dictionary<string, float> _calibrationBaseline = new(StringComparer.OrdinalIgnoreCase);
    private string _calibrationState = "idle";

    private static readonly string[] ArkitBlendshapeOrder =
    {
        "browDownLeft", "browDownRight", "browInnerUp", "browOuterUpLeft", "browOuterUpRight",
        "cheekPuff", "cheekSquintLeft", "cheekSquintRight", "eyeBlinkLeft", "eyeBlinkRight",
        "eyeLookDownLeft", "eyeLookDownRight", "eyeLookInLeft", "eyeLookInRight", "eyeLookOutLeft",
        "eyeLookOutRight", "eyeLookUpLeft", "eyeLookUpRight", "eyeSquintLeft", "eyeSquintRight",
        "eyeWideLeft", "eyeWideRight", "jawForward", "jawLeft", "jawOpen",
        "jawRight", "mouthClose", "mouthDimpleLeft", "mouthDimpleRight", "mouthFrownLeft",
        "mouthFrownRight", "mouthFunnel", "mouthLeft", "mouthLowerDownLeft", "mouthLowerDownRight",
        "mouthPressLeft", "mouthPressRight", "mouthPucker", "mouthRight", "mouthRollLower",
        "mouthRollUpper", "mouthShrugLower", "mouthShrugUpper", "mouthSmileLeft", "mouthSmileRight",
        "mouthStretchLeft", "mouthStretchRight", "mouthUpperUpLeft", "mouthUpperUpRight", "noseSneerLeft",
        "noseSneerRight", "tongueOut",
    };

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
            };
            ResetRuntimeState();

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
                _udpClient = new UdpClient(_options.ListenPort);
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
            _activeRuntimeSource = TrackingRuntimeSource.Ifacial;
            _diagnostics = _diagnostics with
            {
                IsActive = true,
                SourceType = TrackingSourceType.OscIfacial,
                SourceStatus = $"udp-listening:{_options.ListenPort}",
                StatusMessage = $"listening:{_options.ListenPort}",
                ActiveSource = "ifacial",
                ConfidenceSummary = BuildConfidenceSummary(),
            };

            // Start webcam sidecar as fallback path, but do not fail OSC start when unavailable.
            var fallbackRc = InitializeWebcamRuntime();
            if (fallbackRc == NcResultCode.Ok)
            {
                _ = Task.Run(() => WebcamLoopAsync(_cts.Token));
                _diagnostics = _diagnostics with
                {
                    SourceStatus = "ifacial-active:webcam-fallback-ready",
                    StatusMessage = $"listening:{_options.ListenPort}; fallback=webcam-ready",
                };
            }
            else
            {
                _diagnostics = _diagnostics with
                {
                    SourceStatus = "ifacial-active:webcam-fallback-unavailable",
                    StatusMessage = $"listening:{_options.ListenPort}; fallback=webcam-unavailable",
                };
            }
            return NcResultCode.Ok;
        }
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
            DisposeWebcamRuntime();
            _diagnostics = _diagnostics with
            {
                IsActive = false,
                LastPacketAgeMs = int.MaxValue,
                IsStale = true,
                SourceStatus = "stopped",
                StatusMessage = "stopped",
                ActiveSource = "none",
                ConfidenceSummary = BuildConfidenceSummary(),
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
            return NcResultCode.Ok;
        }
    }

    public bool TryGetLatestFrame(out NcTrackingFrame frame)
    {
        lock (_sync)
        {
            if (!_hasFrame)
            {
                frame = BuildNeutralFrame();
                return false;
            }

            var ageMs = _lastPacketUtc == DateTimeOffset.MinValue
                ? int.MaxValue
                : (int)Math.Max(0.0, (DateTimeOffset.UtcNow - _lastPacketUtc).TotalMilliseconds);
            var stale = ageMs > _options.StaleTimeoutMs;
            EvaluateSourceArbitration(ageMs);
            _diagnostics = _diagnostics with
            {
                LastPacketAgeMs = ageMs,
                IsStale = stale,
                InputFps = _smoothedInputFps,
                CaptureFps = _smoothedCaptureFps,
                InferenceMsAvg = _smoothedInferenceMs,
                SourceStatus = stale ? "stale-reset-to-neutral" : _diagnostics.SourceStatus,
                StatusMessage = stale ? "stale (reset to neutral)" : _diagnostics.StatusMessage,
                ActiveSource = ToActiveSourceLabel(_activeRuntimeSource),
                FallbackCount = _fallbackCount,
                CalibrationState = _calibrationState,
                ConfidenceSummary = BuildConfidenceSummary(),
            };

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

    public TrackingDiagnostics GetDiagnostics()
    {
        lock (_sync)
        {
            var ageMs = _lastPacketUtc == DateTimeOffset.MinValue
                ? int.MaxValue
                : (int)Math.Max(0.0, (DateTimeOffset.UtcNow - _lastPacketUtc).TotalMilliseconds);
            var stale = ageMs > _options.StaleTimeoutMs;
            EvaluateSourceArbitration(ageMs);
            _diagnostics = _diagnostics with
            {
                LastPacketAgeMs = ageMs,
                IsStale = stale,
                InputFps = _smoothedInputFps,
                CaptureFps = _smoothedCaptureFps,
                InferenceMsAvg = _smoothedInferenceMs,
                SourceType = _options.SourceType,
                ActiveSource = ToActiveSourceLabel(_activeRuntimeSource),
                FallbackCount = _fallbackCount,
                CalibrationState = _calibrationState,
                ConfidenceSummary = BuildConfidenceSummary(),
            };
            return _diagnostics;
        }
    }

    private void ResetRuntimeState()
    {
        _expressionCache.Clear();
        _expressionSnapshot.Clear();
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
        _lastIfacialPacketUtc = DateTimeOffset.MinValue;
        _lastWebcamPacketUtc = DateTimeOffset.MinValue;
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
            "ifacial=0.00,webcam=0.00");
    }

    private async Task ReceiveLoopAsync(CancellationToken token)
    {
        while (!token.IsCancellationRequested)
        {
            UdpReceiveResult result;
            try
            {
                if (_udpClient is null)
                {
                    break;
                }
                result = await _udpClient.ReceiveAsync(token);
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
                await Task.Delay(10, CancellationToken.None);
                continue;
            }

            lock (_sync)
            {
                var receivedPackets = _diagnostics.ReceivedPackets + 1;
                _diagnostics = _diagnostics with { ReceivedPackets = receivedPackets };

                if (!TryParsePacket(result.Buffer, out var updates, out var formatName))
                {
                _diagnostics = _diagnostics with
                {
                    ParseErrors = _diagnostics.ParseErrors + 1,
                    DroppedPackets = _diagnostics.DroppedPackets + 1,
                    SourceStatus = (_diagnostics.ParseErrors + 1 >= (ulong)_options.ParseErrorWarnThreshold)
                        ? "udp-parse-threshold-exceeded"
                        : "udp-parse-failed",
                    StatusMessage = "packet parse failed",
                    LastErrorCode = (_diagnostics.ParseErrors + 1 >= (ulong)_options.ParseErrorWarnThreshold)
                        ? "TRACKING_PARSE_THRESHOLD_EXCEEDED"
                        : "TRACKING_PARSE_FAILED",
                };
                _ifacialConsecutiveFailures++;
                continue;
            }

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
                    ApplySmoothing();
                    AdvanceCalibration();
                }
                _diagnostics = _diagnostics with
                {
                    DetectedFormat = formatName,
                    SourceStatus = _activeRuntimeSource == TrackingRuntimeSource.Ifacial ? "ifacial-active" : "ifacial-recovering",
                    StatusMessage = $"receiving:{formatName}",
                    LastErrorCode = string.Empty,
                    ActiveSource = ToActiveSourceLabel(_activeRuntimeSource),
                    FallbackCount = _fallbackCount,
                    CalibrationState = _calibrationState,
                    ConfidenceSummary = BuildConfidenceSummary(),
                };
            }
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
                    _diagnostics = _diagnostics with
                    {
                        IsActive = false,
                        SourceStatus = BuildWebcamSourceStatus("sidecar-no-frames"),
                        StatusMessage = string.IsNullOrWhiteSpace(_latestMediapipeError)
                            ? "mediapipe sidecar started but no frame received"
                            : $"mediapipe sidecar no frame: {_latestMediapipeError}",
                        ModelSchemaOk = false,
                        LastErrorCode = "TRACKING_MEDIAPIPE_NO_FRAME",
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
            ApplySmoothing();
            AdvanceCalibration();
            _diagnostics = _diagnostics with
            {
                DetectedFormat = "webcam-mediapipe",
                InputFps = _smoothedInputFps,
                CaptureFps = _smoothedCaptureFps,
                InferenceMsAvg = _smoothedInferenceMs,
                SourceStatus = _options.SourceType == TrackingSourceType.OscIfacial
                    ? BuildWebcamSourceStatus("fallback-active")
                    : BuildWebcamSourceStatus("receiving"),
                StatusMessage = _options.SourceType == TrackingSourceType.OscIfacial
                    ? "receiving:webcam-mediapipe (fallback)"
                    : "receiving:webcam-mediapipe",
                ModelSchemaOk = true,
                LastErrorCode = string.Empty,
                ActiveSource = ToActiveSourceLabel(_activeRuntimeSource),
                FallbackCount = _fallbackCount,
                CalibrationState = _calibrationState,
                ConfidenceSummary = BuildConfidenceSummary(),
            };
        }
    }

    private void ApplyMediapipeBlendshapeResult(MediapipeFramePacket packet)
    {
        for (var i = 0; i < ArkitBlendshapeOrder.Length; i++)
        {
            var key = NormalizeKey(ArkitBlendshapeOrder[i]);
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
        var pythonExe = Environment.GetEnvironmentVariable("VSFCLONE_MEDIAPIPE_PYTHON");
        if (string.IsNullOrWhiteSpace(pythonExe))
        {
            pythonExe = "python";
        }

        var scriptPath = Environment.GetEnvironmentVariable("VSFCLONE_MEDIAPIPE_SIDECAR_SCRIPT");
        if (string.IsNullOrWhiteSpace(scriptPath))
        {
            var cwdCandidate = Path.Combine(Environment.CurrentDirectory, "tools", "mediapipe_webcam_sidecar.py");
            if (File.Exists(cwdCandidate))
            {
                scriptPath = cwdCandidate;
            }
            else
            {
                var baseDirCandidate = Path.Combine(AppContext.BaseDirectory, "tools", "mediapipe_webcam_sidecar.py");
                scriptPath = baseDirCandidate;
            }
        }

        if (string.IsNullOrWhiteSpace(scriptPath) || !File.Exists(scriptPath))
        {
            return new MediapipeSidecarLaunchConfig(
                false,
                string.Empty,
                string.Empty,
                "mediapipe sidecar script not found. set VSFCLONE_MEDIAPIPE_SIDECAR_SCRIPT to tools/mediapipe_webcam_sidecar.py");
        }

        var cameraArg = string.IsNullOrWhiteSpace(_options.CameraDeviceKey) ? "0" : _options.CameraDeviceKey.Trim();
        var args = string.Create(
            CultureInfo.InvariantCulture,
            $"\"{scriptPath}\" --camera \"{cameraArg}\" --fps {_options.InferenceFpsCap}");
        return new MediapipeSidecarLaunchConfig(true, pythonExe, args, string.Empty);
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
        try
        {
            using var doc = JsonDocument.Parse(jsonLine);
            var root = doc.RootElement;

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
            _ = TryReadNumber(root, "capture_fps", out captureFps);
            _ = TryReadNumber(root, "inference_ms", out inferenceMs);
            _ = TryReadNumber(root, "confidence", out sourceConfidence);

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
            packet = new MediapipeFramePacket(
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
                blendshapes);
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

    private bool TryParsePacket(byte[] packet, out List<KeyValuePair<string, float>> updates, out string formatName)
    {
        updates = new List<KeyValuePair<string, float>>();
        formatName = "unknown";

        if (packet.Length == 0)
        {
            return false;
        }

        if (IsOscBundle(packet))
        {
            formatName = "format-b";
            return TryParseBundle(packet, updates);
        }

        if (!TryParseOscMessage(packet, out var message))
        {
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

    private bool TryParseBundle(byte[] packet, List<KeyValuePair<string, float>> updates)
    {
        // bundle = "#bundle\0" + timetag(8) + element[size+payload]*
        if (packet.Length < 16)
        {
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
                return false;
            }

            var slice = new byte[size];
            Buffer.BlockCopy(packet, index, slice, 0, size);
            index += size;

            if (IsOscBundle(slice))
            {
                if (!TryParseBundle(slice, updates))
                {
                    return false;
                }
                any = true;
                continue;
            }

            if (!TryParseOscMessage(slice, out var message))
            {
                continue;
            }
            any |= TryExtractFormatA(message, updates) || TryExtractFormatB(message, updates);
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

    private static bool TryParseOscMessage(byte[] packet, out OscMessage message)
    {
        message = new OscMessage(string.Empty, string.Empty, Array.Empty<OscValue>());
        if (packet.Length < 8)
        {
            return false;
        }

        var index = 0;
        if (!TryReadOscString(packet, ref index, out var address))
        {
            return false;
        }
        if (!TryReadOscString(packet, ref index, out var typeTag))
        {
            return false;
        }
        if (string.IsNullOrWhiteSpace(typeTag) || typeTag[0] != ',')
        {
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
                        return false;
                    }
                    values.Add(new OscValue(OscValueKind.Float, f, string.Empty));
                    break;
                case 'i':
                    if (index + 4 > packet.Length)
                    {
                        return false;
                    }
                    var i32 = ReadInt32BigEndian(packet, index);
                    index += 4;
                    values.Add(new OscValue(OscValueKind.Float, i32, string.Empty));
                    break;
                case 's':
                    if (!TryReadOscString(packet, ref index, out var s))
                    {
                        return false;
                    }
                    values.Add(new OscValue(OscValueKind.String, 0.0f, s));
                    break;
                default:
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

    private float ApplyAdaptiveCalibration(string key, float value)
    {
        var normalized = NormalizeKey(key);
        if (string.IsNullOrWhiteSpace(normalized))
        {
            return Clamp01(value);
        }

        if (!_calibrationBaseline.TryGetValue(normalized, out var baseline))
        {
            baseline = value;
            _calibrationBaseline[normalized] = baseline;
        }
        else
        {
            var alpha = _calibrationFrames < CalibrationWarmupFrames ? 0.06f : 0.01f;
            baseline = Ema(baseline, value, alpha);
            _calibrationBaseline[normalized] = baseline;
        }

        if (_calibrationFrames < CalibrationWarmupFrames)
        {
            _calibrationState = "calibrating";
            return Clamp01(value);
        }

        var denom = MathF.Max(0.18f, 1.0f - baseline);
        var normalizedValue = (value - baseline) / denom;
        _calibrationState = "stable";
        return Clamp01(normalizedValue);
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
        if (_options.SourceType == TrackingSourceType.WebcamMediapipe)
        {
            _activeRuntimeSource = TrackingRuntimeSource.Webcam;
            return;
        }

        var now = DateTimeOffset.UtcNow;
        var ifacialAge = _lastIfacialPacketUtc == DateTimeOffset.MinValue
            ? int.MaxValue
            : (int)Math.Max(0.0, (now - _lastIfacialPacketUtc).TotalMilliseconds);
        var webcamAge = _lastWebcamPacketUtc == DateTimeOffset.MinValue
            ? int.MaxValue
            : (int)Math.Max(0.0, (now - _lastWebcamPacketUtc).TotalMilliseconds);
        var ageMs = currentAgeMs > 0 ? currentAgeMs : ifacialAge;

        var shouldFallback = (ageMs >= IfacialFallbackAgeMs || _ifacialConsecutiveFailures >= 4) &&
                             webcamAge < (_options.StaleTimeoutMs * 2);
        if (shouldFallback && _activeRuntimeSource != TrackingRuntimeSource.Webcam)
        {
            _activeRuntimeSource = TrackingRuntimeSource.Webcam;
            _fallbackCount++;
            _ifacialRecoveryStreak = 0;
        }

        if (_activeRuntimeSource == TrackingRuntimeSource.Webcam)
        {
            var ifacialRecovered = ifacialAge <= IfacialRecoveryAgeMs && _ifacialConsecutiveFailures == 0;
            if (ifacialRecovered)
            {
                _ifacialRecoveryStreak++;
                if (_ifacialRecoveryStreak >= IfacialRecoveryStreakRequired)
                {
                    _activeRuntimeSource = TrackingRuntimeSource.Ifacial;
                    _ifacialRecoveryStreak = 0;
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
        }
    }

    private bool ShouldConsumeIfacialFrame()
    {
        if (_options.SourceType == TrackingSourceType.WebcamMediapipe)
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

        var ifacialConfidence = ifacialAge == int.MaxValue
            ? 0.0
            : Math.Clamp(1.0 - (ifacialAge / 900.0), 0.0, 1.0);
        if (_ifacialConsecutiveFailures > 0)
        {
            ifacialConfidence *= Math.Clamp(1.0 - (_ifacialConsecutiveFailures * 0.15), 0.0, 1.0);
        }

        var webcamConfidence = (_latestMediapipePacket is null)
            ? (webcamAge == int.MaxValue ? 0.0 : Math.Clamp(1.0 - (webcamAge / 1200.0), 0.0, 1.0))
            : Math.Clamp(_latestMediapipePacket.SourceConfidence, 0.0, 1.0);

        return string.Create(
            CultureInfo.InvariantCulture,
            $"ifacial={ifacialConfidence:F2},webcam={webcamConfidence:F2}");
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

    private void ApplySmoothing()
    {
        const float poseAlpha = 0.35f;
        const float expressionAlpha = 0.60f;

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
            _smoothedFrame.HeadPosX = Ema(_smoothedFrame.HeadPosX, _rawFrame.HeadPosX, poseAlpha);
            _smoothedFrame.HeadPosY = Ema(_smoothedFrame.HeadPosY, _rawFrame.HeadPosY, poseAlpha);
            _smoothedFrame.HeadPosZ = Ema(_smoothedFrame.HeadPosZ, _rawFrame.HeadPosZ, poseAlpha);
            _smoothedFrame.BlinkL = Ema(_smoothedFrame.BlinkL, Clamp01(_rawFrame.BlinkL), expressionAlpha);
            _smoothedFrame.BlinkR = Ema(_smoothedFrame.BlinkR, Clamp01(_rawFrame.BlinkR), expressionAlpha);
            _smoothedFrame.MouthOpen = Ema(_smoothedFrame.MouthOpen, Clamp01(_rawFrame.MouthOpen), expressionAlpha);

            if (_hasHeadYpr)
            {
                _smoothedHeadYaw = Ema(_smoothedHeadYaw, _rawHeadYaw, poseAlpha);
                _smoothedHeadPitch = Ema(_smoothedHeadPitch, _rawHeadPitch, poseAlpha);
                _smoothedHeadRoll = Ema(_smoothedHeadRoll, _rawHeadRoll, poseAlpha);
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
        if (string.IsNullOrWhiteSpace(raw))
        {
            return string.Empty;
        }

        var sb = new StringBuilder(raw.Length);
        foreach (var ch in raw)
        {
            if (char.IsLetterOrDigit(ch))
            {
                sb.Append(char.ToLowerInvariant(ch));
            }
        }
        return sb.ToString();
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

    private sealed record MediapipeFramePacket(
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
        IReadOnlyDictionary<string, float> BlendshapeWeights);

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
}
