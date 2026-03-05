using System.Diagnostics;
using System.Net.Sockets;
using System.Text;
using Microsoft.ML.OnnxRuntime;
using Microsoft.ML.OnnxRuntime.Tensors;
using OpenCvSharp;

namespace HostCore;

public sealed class TrackingInputService : ITrackingInputService
{
    private const ushort DefaultListenPort = 49983;
    private const int DefaultStaleTimeoutMs = 500;

    private readonly object _sync = new();
    private readonly Dictionary<string, float> _expressionCache = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, float> _expressionSnapshot = new(StringComparer.OrdinalIgnoreCase);

    private UdpClient? _udpClient;
    private CancellationTokenSource? _cts;
    private Task? _receiveTask;
    private TrackingStartOptions _options = new(DefaultListenPort, DefaultStaleTimeoutMs, TrackingSourceType.OscIfacial, string.Empty, string.Empty, 30);
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
    private VideoCapture? _webcamCapture;
    private InferenceSession? _onnxSession;
    private WebcamOnnxSchema? _webcamSchema;

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
                WebcamDeviceId = options.WebcamDeviceId?.Trim() ?? string.Empty,
                OnnxModelPath = options.OnnxModelPath?.Trim() ?? string.Empty,
                InferenceFpsCap = Math.Clamp(options.InferenceFpsCap <= 0 ? 30 : options.InferenceFpsCap, 5, 120),
            };
            ResetRuntimeState();

            if (_options.SourceType == TrackingSourceType.WebcamOnnx)
            {
                var initRc = InitializeWebcamRuntime();
                if (initRc != NcResultCode.Ok)
                {
                    return initRc;
                }

                _cts = new CancellationTokenSource();
                _receiveTask = Task.Run(() => WebcamLoopAsync(_cts.Token));
                _diagnostics = _diagnostics with
                {
                    IsActive = true,
                    DetectedFormat = "webcam-onnx",
                    SourceStatus = BuildWebcamSourceStatus("starting"),
                    StatusMessage = "starting:webcam-onnx",
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
            _diagnostics = _diagnostics with
            {
                IsActive = true,
                SourceType = TrackingSourceType.OscIfacial,
                SourceStatus = $"udp-listening:{_options.ListenPort}",
                StatusMessage = $"listening:{_options.ListenPort}",
            };
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
            _diagnostics = _diagnostics with
            {
                LastPacketAgeMs = ageMs,
                IsStale = stale,
                InputFps = _smoothedInputFps,
                CaptureFps = _smoothedCaptureFps,
                InferenceMsAvg = _smoothedInferenceMs,
                StatusMessage = stale ? "stale (holding last frame)" : _diagnostics.StatusMessage,
            };

            frame = _lastOutputFrame;
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
            _diagnostics = _diagnostics with
            {
                LastPacketAgeMs = ageMs,
                IsStale = stale,
                InputFps = _smoothedInputFps,
                CaptureFps = _smoothedCaptureFps,
                InferenceMsAvg = _smoothedInferenceMs,
                SourceType = _options.SourceType,
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
        _diagnostics = new TrackingDiagnostics(true, "unknown", 0.0, 0.0, 0.0, int.MaxValue, true, 0, 0, 0, "listening", _options.SourceType, "initializing");
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
                    SourceStatus = "udp-parse-failed",
                    StatusMessage = "packet parse failed",
                };
                continue;
            }

                if (!ApplyUpdates(updates))
                {
                _diagnostics = _diagnostics with
                {
                    DroppedPackets = _diagnostics.DroppedPackets + 1,
                    SourceStatus = "udp-no-mapped-channels",
                    StatusMessage = "packet dropped (no mapped channels)",
                };
                continue;
            }

                _lastPacketUtc = DateTimeOffset.UtcNow;
                _hasFrame = true;
                UpdateInputFps();
                ApplySmoothing();
                _diagnostics = _diagnostics with
                {
                    DetectedFormat = formatName,
                    SourceStatus = "udp-receiving",
                    StatusMessage = $"receiving:{formatName}",
                };
            }
        }
    }

    private async Task WebcamLoopAsync(CancellationToken token)
    {
        var fpsCap = Math.Clamp(_options.InferenceFpsCap, 5, 120);
        var targetFrameBudget = TimeSpan.FromMilliseconds(1000.0 / fpsCap);
        using var frame = new Mat();
        using var resized = new Mat();
        using var rgb = new Mat();

        while (!token.IsCancellationRequested)
        {
            var cycleStart = Stopwatch.GetTimestamp();
            NcResultCode loopRc = NcResultCode.Ok;
            string? loopError = null;
            try
            {
                if (_webcamCapture is null || _onnxSession is null || _webcamSchema is null)
                {
                    loopRc = NcResultCode.NotInitialized;
                    loopError = "runtime not initialized";
                }
                else if (!_webcamCapture.Read(frame) || frame.Empty())
                {
                    loopRc = NcResultCode.Io;
                    loopError = "camera frame read failed";
                }
                else
                {
                    UpdateCaptureFps();
                    Cv2.Resize(frame, resized, new OpenCvSharp.Size(256, 256), interpolation: InterpolationFlags.Linear);
                    Cv2.CvtColor(resized, rgb, ColorConversionCodes.BGR2RGB);
                    var inputData = ConvertMatToInput(rgb);
                    var inferWatch = Stopwatch.StartNew();
                    var result = RunInference(_onnxSession, _webcamSchema, inputData);
                    inferWatch.Stop();
                    UpdateInferenceMs(inferWatch.Elapsed.TotalMilliseconds);

                    lock (_sync)
                    {
                        _rawFrame = BuildNeutralFrame();
                        _rawHeadYaw = result.HeadYawDeg;
                        _rawHeadPitch = result.HeadPitchDeg;
                        _rawHeadRoll = result.HeadRollDeg;
                        _hasHeadYpr = true;
                        _rawFrame.HeadPosX = result.HeadPosX;
                        _rawFrame.HeadPosY = result.HeadPosY;
                        _rawFrame.HeadPosZ = result.HeadPosZ;
                        ApplyBlendshapeResult(result.BlendshapeWeights);

                        _lastPacketUtc = DateTimeOffset.UtcNow;
                        _hasFrame = true;
                        UpdateInputFps();
                        ApplySmoothing();
                        _diagnostics = _diagnostics with
                        {
                            DetectedFormat = "webcam-onnx",
                            InputFps = _smoothedInputFps,
                            CaptureFps = _smoothedCaptureFps,
                            InferenceMsAvg = _smoothedInferenceMs,
                            SourceStatus = BuildWebcamSourceStatus("receiving"),
                            StatusMessage = "receiving:webcam-onnx",
                            ModelSchemaOk = true,
                            LastErrorCode = string.Empty,
                        };
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
                        SourceStatus = BuildWebcamSourceStatus("error"),
                        StatusMessage = $"webcam error: {loopError}",
                        LastErrorCode = loopRc.ToString(),
                    };
                }
            }

            var elapsed = Stopwatch.GetElapsedTime(cycleStart);
            var remaining = targetFrameBudget - elapsed;
            if (remaining < TimeSpan.Zero)
            {
                continue;
            }

            try
            {
                await Task.Delay(remaining, token);
            }
            catch (OperationCanceledException)
            {
                break;
            }
        }
    }

    private string BuildWebcamSourceStatus(string stage)
    {
        var device = string.IsNullOrWhiteSpace(_options.WebcamDeviceId) ? "default-device" : _options.WebcamDeviceId;
        var model = string.IsNullOrWhiteSpace(_options.OnnxModelPath) ? "no-model" : "model-configured";
        return $"webcam-onnx:{stage}:{device}:{model}:fps_cap={_options.InferenceFpsCap}";
    }

    private NcResultCode InitializeWebcamRuntime()
    {
        DisposeWebcamRuntime();
        if (string.IsNullOrWhiteSpace(_options.OnnxModelPath))
        {
            _diagnostics = _diagnostics with
            {
                IsActive = false,
                SourceStatus = BuildWebcamSourceStatus("model-missing"),
                StatusMessage = "webcam onnx model path is required",
                ModelSchemaOk = false,
                LastErrorCode = NcResultCode.InvalidArgument.ToString(),
            };
            return NcResultCode.InvalidArgument;
        }

        if (!File.Exists(_options.OnnxModelPath))
        {
            _diagnostics = _diagnostics with
            {
                IsActive = false,
                SourceStatus = BuildWebcamSourceStatus("model-not-found"),
                StatusMessage = $"onnx model not found: {_options.OnnxModelPath}",
                ModelSchemaOk = false,
                LastErrorCode = NcResultCode.InvalidArgument.ToString(),
            };
            return NcResultCode.InvalidArgument;
        }

        try
        {
            _webcamCapture = OpenCapture(_options.WebcamDeviceId);
            if (_webcamCapture is null || !_webcamCapture.IsOpened())
            {
                _diagnostics = _diagnostics with
                {
                    IsActive = false,
                    SourceStatus = BuildWebcamSourceStatus("camera-open-failed"),
                    StatusMessage = "webcam open failed",
                    ModelSchemaOk = false,
                    LastErrorCode = NcResultCode.Io.ToString(),
                };
                return NcResultCode.Io;
            }

            _onnxSession = new InferenceSession(_options.OnnxModelPath);
            var schema = WebcamOnnxSchema.TryCreate(_onnxSession, out var schemaError);
            if (schema is null)
            {
                _diagnostics = _diagnostics with
                {
                    IsActive = false,
                    SourceStatus = BuildWebcamSourceStatus("schema-mismatch"),
                    StatusMessage = schemaError,
                    ModelSchemaOk = false,
                    LastErrorCode = NcResultCode.InvalidArgument.ToString(),
                };
                return NcResultCode.InvalidArgument;
            }

            _webcamSchema = schema;
            _diagnostics = _diagnostics with
            {
                ModelSchemaOk = true,
                LastErrorCode = string.Empty,
                SourceStatus = BuildWebcamSourceStatus("initialized"),
                StatusMessage = "webcam runtime initialized",
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
        _webcamCapture?.Release();
        _webcamCapture?.Dispose();
        _webcamCapture = null;
        _onnxSession?.Dispose();
        _onnxSession = null;
        _webcamSchema = null;
    }

    private static VideoCapture OpenCapture(string webcamDeviceId)
    {
        if (string.IsNullOrWhiteSpace(webcamDeviceId))
        {
            return new VideoCapture(0);
        }

        if (int.TryParse(webcamDeviceId, out var index))
        {
            return new VideoCapture(index);
        }

        return new VideoCapture(webcamDeviceId);
    }

    private static float[] ConvertMatToInput(Mat rgb)
    {
        const int width = 256;
        const int height = 256;
        var channelSize = width * height;
        var data = new float[channelSize * 3];
        var indexer = rgb.GetGenericIndexer<Vec3b>();
        for (var y = 0; y < height; y++)
        {
            for (var x = 0; x < width; x++)
            {
                var pixel = indexer[y, x];
                var idx = (y * width) + x;
                data[idx] = pixel.Item0 / 255.0f;
                data[channelSize + idx] = pixel.Item1 / 255.0f;
                data[(channelSize * 2) + idx] = pixel.Item2 / 255.0f;
            }
        }

        return data;
    }

    private static WebcamInferenceResult RunInference(
        InferenceSession session,
        WebcamOnnxSchema schema,
        float[] inputData)
    {
        var inputTensor = new DenseTensor<float>(inputData, schema.InputShape);
        var inputs = new List<NamedOnnxValue> { NamedOnnxValue.CreateFromTensor(schema.InputName, inputTensor) };
        using IDisposableReadOnlyCollection<DisposableNamedOnnxValue> outputs = session.Run(inputs);

        var blendOutput = outputs.FirstOrDefault(x => string.Equals(x.Name, schema.BlendshapeOutputName, StringComparison.Ordinal));
        var poseOutput = outputs.FirstOrDefault(x => string.Equals(x.Name, schema.HeadPoseOutputName, StringComparison.Ordinal));
        if (blendOutput is null || poseOutput is null)
        {
            throw new InvalidOperationException("missing required onnx output(s): blendshape/head pose");
        }

        var blendTensor = blendOutput.AsTensor<float>();
        var blendArray = blendTensor.ToArray();
        if (blendArray.Length != ArkitBlendshapeOrder.Length)
        {
            throw new InvalidOperationException($"blendshape output size mismatch: expected {ArkitBlendshapeOrder.Length}, got {blendArray.Length}");
        }

        var poseTensor = poseOutput.AsTensor<float>();
        var poseArray = poseTensor.ToArray();
        if (poseArray.Length < 3)
        {
            throw new InvalidOperationException("head pose output size mismatch: expected at least 3 values");
        }

        float headPosX = 0.0f;
        float headPosY = 0.0f;
        float headPosZ = 0.0f;
        if (schema.HeadPosOutputName is not null)
        {
            var posOutput = outputs.FirstOrDefault(x => string.Equals(x.Name, schema.HeadPosOutputName, StringComparison.Ordinal));
            if (posOutput is null)
            {
                throw new InvalidOperationException("head position output missing while schema requires it");
            }
            var posTensor = posOutput.AsTensor<float>();
            var posArray = posTensor.ToArray();
            if (posArray.Length < 3)
            {
                throw new InvalidOperationException("head position output size mismatch: expected at least 3 values");
            }
            headPosX = posArray[0];
            headPosY = posArray[1];
            headPosZ = posArray[2];
        }

        return new WebcamInferenceResult(
            blendArray,
            poseArray[0],
            poseArray[1],
            poseArray[2],
            headPosX,
            headPosY,
            headPosZ);
    }

    private void ApplyBlendshapeResult(IReadOnlyList<float> blendshapeWeights)
    {
        for (var i = 0; i < ArkitBlendshapeOrder.Length; i++)
        {
            var key = NormalizeKey(ArkitBlendshapeOrder[i]);
            var value = Clamp01(blendshapeWeights[i]);
            _expressionCache[key] = value;
        }

        _rawFrame.BlinkL = _expressionCache["eyeblinkleft"];
        _rawFrame.BlinkR = _expressionCache["eyeblinkright"];
        _rawFrame.MouthOpen = _expressionCache["jawopen"];
        SnapshotExpressionWeights();
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
                _rawFrame.BlinkL = Clamp01(value);
                return true;
            case "eyeblinkright":
                _rawFrame.BlinkR = Clamp01(value);
                return true;
            case "jawopen":
                _rawFrame.MouthOpen = Clamp01(value);
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
                _expressionCache[normalized] = Clamp01(value);
                return true;
        }
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

    private void UpdateCaptureFps()
    {
        lock (_sync)
        {
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

    private sealed record WebcamInferenceResult(
        float[] BlendshapeWeights,
        float HeadYawDeg,
        float HeadPitchDeg,
        float HeadRollDeg,
        float HeadPosX,
        float HeadPosY,
        float HeadPosZ);

    private sealed record WebcamOnnxSchema(
        string InputName,
        int[] InputShape,
        string BlendshapeOutputName,
        string HeadPoseOutputName,
        string? HeadPosOutputName)
    {
        private static readonly int[] ExpectedInput = { 1, 3, 256, 256 };
        private static readonly int[] ExpectedBlend = { 1, 52 };
        private static readonly int[] ExpectedPose = { 1, 3 };

        public static WebcamOnnxSchema? TryCreate(InferenceSession session, out string error)
        {
            const string inputName = "input";
            const string blendName = "blendshape";
            const string poseName = "head_pose";
            const string posName = "head_pos";
            error = string.Empty;

            if (!session.InputMetadata.TryGetValue(inputName, out var inputMeta))
            {
                error = "onnx schema mismatch: missing input 'input' [1,3,256,256]";
                return null;
            }

            if (!ValidateShape(inputMeta.Dimensions, ExpectedInput))
            {
                error = "onnx schema mismatch: input 'input' must be [1,3,256,256]";
                return null;
            }

            if (!session.OutputMetadata.TryGetValue(blendName, out var blendMeta) ||
                !ValidateShape(blendMeta.Dimensions, ExpectedBlend))
            {
                error = "onnx schema mismatch: output 'blendshape' must be [1,52]";
                return null;
            }

            if (!session.OutputMetadata.TryGetValue(poseName, out var poseMeta) ||
                !ValidateShape(poseMeta.Dimensions, ExpectedPose))
            {
                error = "onnx schema mismatch: output 'head_pose' must be [1,3]";
                return null;
            }

            string? optionalPosOutput = null;
            if (session.OutputMetadata.TryGetValue(posName, out var posMeta))
            {
                if (!ValidateShape(posMeta.Dimensions, ExpectedPose))
                {
                    error = "onnx schema mismatch: output 'head_pos' must be [1,3] when present";
                    return null;
                }
                optionalPosOutput = posName;
            }

            return new WebcamOnnxSchema(inputName, ExpectedInput, blendName, poseName, optionalPosOutput);
        }

        private static bool ValidateShape(IReadOnlyList<int> actual, IReadOnlyList<int> expected)
        {
            if (actual.Count != expected.Count)
            {
                return false;
            }

            for (var i = 0; i < expected.Count; i++)
            {
                var expectedDim = expected[i];
                var actualDim = actual[i];
                if (i == 0)
                {
                    // Allow dynamic batch for user-supplied models.
                    if (actualDim == -1 || actualDim == expectedDim)
                    {
                        continue;
                    }
                    return false;
                }

                if (actualDim != expectedDim)
                {
                    return false;
                }
            }

            return true;
        }
    }

    private readonly record struct OscMessage(string Address, string TypeTag, IReadOnlyList<OscValue> Values);
    private readonly record struct OscValue(OscValueKind Kind, float FloatValue, string StringValue);
    private enum OscValueKind
    {
        Float = 0,
        String = 1,
    }
}
