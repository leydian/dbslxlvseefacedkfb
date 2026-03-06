using System;

namespace HostCore;

public static class TrackingErrorHintCatalog
{
    public static string BuildHint(string? lastErrorCode)
    {
        if (string.IsNullOrWhiteSpace(lastErrorCode))
        {
            return string.Empty;
        }

        return lastErrorCode switch
        {
            "TRACKING_PARSE_THRESHOLD_EXCEEDED" => " hint=parse errors exceeded threshold",
            "TRACKING_PARSE_FAILED" => " hint=packet parse failed; verify iFacialMocap send mode and port",
            "TRACKING_PROTOCOL_MISMATCH_VMC" => " hint=received VMC packets; switch sender to iFacialMocap OSC mode",
            "TRACKING_OSC_TYPE_UNSUPPORTED" => " hint=OSC type tag unsupported; disable non-iFacial stream or VMC extensions",
            "TRACKING_DROP_THRESHOLD_EXCEEDED" => " hint=dropped packets exceeded threshold",
            "TRACKING_NO_MAPPED_CHANNELS" => " hint=source packet had no mapped channels",
            "TRACKING_SOURCE_SWITCH_THRASH" => " hint=source switching too frequently; stabilize network/light or lock source temporarily",
            "TRACKING_MEDIAPIPE_CONFIG_INVALID" => " hint=webcam runtime config invalid (missing mediapipe_webcam_sidecar.py; set VSFCLONE_MEDIAPIPE_SIDECAR_SCRIPT)",
            "TRACKING_MEDIAPIPE_START_FAILED" => " hint=webcam sidecar start failed; verify VSFCLONE_MEDIAPIPE_PYTHON or run setup_tracking_python_venv.ps1",
            "TRACKING_MEDIAPIPE_NO_FRAME" => " hint=webcam sidecar produced no frames",
            "TRACKING_NO_ACTIVE_INPUT_SOURCE" => " hint=no active input source; start iFacial send or enable webcam runtime",
            "TRACKING_IFACIAL_NO_PACKET" => " hint=no iFacial packet received",
            "TRACKING_WEBCAM_RUNTIME_UNAVAILABLE" => " hint=webcam runtime unavailable (python/mediapipe not ready)",
            "TRACKING_WEBCAM_NO_FRAME" => " hint=webcam runtime started but no frame",
            _ when lastErrorCode.StartsWith("NC_SET_TRACKING_FRAME_", StringComparison.Ordinal) => " hint=native tracking submit failed",
            _ when lastErrorCode.StartsWith("NC_SET_EXPRESSION_WEIGHTS_", StringComparison.Ordinal) => " hint=native expression submit failed",
            _ => string.Empty,
        };
    }
}
