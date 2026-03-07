using System;

namespace HostCore;

public sealed record FeatureGateState(
    bool Enabled,
    string ReasonCode,
    string ReasonText);

public sealed record FeatureGateSnapshot(
    string CommonClass,
    string CommonReasonCode,
    FeatureGateState ArmPose,
    FeatureGateState RealtimeShadow,
    FeatureGateState Expression);

public static class HostFeatureGateResolver
{
    public static FeatureGateSnapshot Evaluate(
        DiagnosticsModel runtime,
        NcAvatarInfo? avatarInfo,
        TrackingDiagnostics tracking)
    {
        if (!runtime.RuntimePathMatch || runtime.RuntimeModuleStaleVsBuildOutput)
        {
            var runtimeReason = !runtime.RuntimePathMatch
                ? NormalizeCode(runtime.RuntimePathWarningCode, "HOST_RUNTIME_MISMATCH_DIST_EXPECTED")
                : NormalizeCode(runtime.RuntimeTimestampWarningCode, "HOST_RUNTIME_DIST_OLDER_THAN_BUILD_OUTPUT");
            var gated = Disabled(runtimeReason);
            return new FeatureGateSnapshot(
                CommonClass: "runtime_binary_mismatch",
                CommonReasonCode: runtimeReason,
                ArmPose: gated,
                RealtimeShadow: gated,
                Expression: gated);
        }

        var hasNativeSubmitFailure =
            !string.IsNullOrWhiteSpace(tracking.LastErrorCode) &&
            tracking.LastErrorCode.StartsWith("NC_SET_", StringComparison.Ordinal);
        if (!avatarInfo.HasValue)
        {
            var noAvatar = Disabled("NO_AVATAR_LOADED");
            return new FeatureGateSnapshot(
                CommonClass: hasNativeSubmitFailure ? "native_submit_failure" : "none_detected",
                CommonReasonCode: hasNativeSubmitFailure ? tracking.LastErrorCode : "NO_AVATAR_LOADED",
                ArmPose: noAvatar,
                RealtimeShadow: noAvatar,
                Expression: noAvatar);
        }

        var info = avatarInfo.Value;
        var armSignal = ResolveSignalCode(info, "ARM_POSE_");
        var shadowSignal = ResolveSignalCode(info, "SHADOW_DISABLED_");

        FeatureGateState armGate;
        if (!string.IsNullOrWhiteSpace(armSignal))
        {
            armGate = Disabled(armSignal);
        }
        else if (info.DetectedFormat != NcAvatarFormatHint.Miq)
        {
            armGate = Disabled("ARM_POSE_FORMAT_UNSUPPORTED");
        }
        else
        {
            armGate = Enabled();
        }

        FeatureGateState shadowGate;
        if (!string.IsNullOrWhiteSpace(shadowSignal))
        {
            shadowGate = Disabled(shadowSignal);
        }
        else if (string.IsNullOrWhiteSpace(info.ActivePasses) ||
                 !info.ActivePasses.Contains("shadow", StringComparison.OrdinalIgnoreCase))
        {
            shadowGate = Disabled("SHADOW_PASS_NOT_REPORTED");
        }
        else
        {
            shadowGate = Enabled();
        }

        FeatureGateState expressionGate;
        if (hasNativeSubmitFailure)
        {
            expressionGate = Disabled(tracking.LastErrorCode);
        }
        else if (info.ExpressionCount == 0U)
        {
            expressionGate = Disabled("EXPRESSION_COUNT_ZERO");
        }
        else if (!tracking.IsActive)
        {
            expressionGate = Disabled("TRACKING_INACTIVE");
        }
        else if (tracking.IsStale)
        {
            expressionGate = Disabled("TRACKING_STALE");
        }
        else
        {
            expressionGate = Enabled();
        }

        var commonClass = "none_detected";
        var commonReason = "none";
        if (hasNativeSubmitFailure)
        {
            commonClass = "native_submit_failure";
            commonReason = tracking.LastErrorCode;
        }
        else if (info.ExpressionCount == 0U)
        {
            commonClass = "payload_policy_gate";
            commonReason = "EXPRESSION_COUNT_ZERO";
        }
        else if (!string.IsNullOrWhiteSpace(armSignal) || !string.IsNullOrWhiteSpace(shadowSignal))
        {
            commonClass = "payload_policy_gate";
            commonReason = !string.IsNullOrWhiteSpace(armSignal) ? armSignal : shadowSignal;
        }
        else if (!expressionGate.Enabled)
        {
            commonClass = "tracking_input_inactive";
            commonReason = expressionGate.ReasonCode;
        }

        return new FeatureGateSnapshot(
            CommonClass: commonClass,
            CommonReasonCode: commonReason,
            ArmPose: armGate,
            RealtimeShadow: shadowGate,
            Expression: expressionGate);
    }

    private static FeatureGateState Enabled()
    {
        return new FeatureGateState(true, "none", "ready");
    }

    private static FeatureGateState Disabled(string reasonCode)
    {
        var normalized = NormalizeCode(reasonCode, "unknown");
        return new FeatureGateState(false, normalized, ResolveReasonText(normalized));
    }

    private static string ResolveSignalCode(NcAvatarInfo info, string marker)
    {
        var fromCode = ExtractSignalCode(info.LastWarningCode, marker);
        if (!string.IsNullOrWhiteSpace(fromCode))
        {
            return fromCode;
        }

        return ExtractSignalCode(info.LastWarning, marker);
    }

    private static string ExtractSignalCode(string text, string marker)
    {
        if (string.IsNullOrWhiteSpace(text) || string.IsNullOrWhiteSpace(marker))
        {
            return string.Empty;
        }

        var idx = text.IndexOf(marker, StringComparison.OrdinalIgnoreCase);
        if (idx < 0)
        {
            return string.Empty;
        }

        var end = idx;
        while (end < text.Length)
        {
            var ch = text[end];
            if (!(char.IsLetterOrDigit(ch) || ch is '_' or '-'))
            {
                break;
            }

            end++;
        }

        return end > idx ? text[idx..end] : string.Empty;
    }

    private static string NormalizeCode(string code, string fallback)
    {
        return string.IsNullOrWhiteSpace(code) ? fallback : code.Trim();
    }

    private static string ResolveReasonText(string reasonCode)
    {
        return reasonCode switch
        {
            "HOST_RUNTIME_PATH_UNKNOWN" => "runtime path is unknown",
            "HOST_RUNTIME_MISMATCH_DIST_EXPECTED" => "runtime nativecore path mismatch",
            "HOST_RUNTIME_DIST_OLDER_THAN_BUILD_OUTPUT" => "runtime nativecore is older than build output",
            "NO_AVATAR_LOADED" => "no avatar loaded",
            "ARM_POSE_FORMAT_UNSUPPORTED" => "arm pose is supported for MIQ payloads",
            "ARM_POSE_DISABLED_BY_STATIC_SKINNING_POLICY" => "arm pose blocked by static skinning policy",
            "ARM_POSE_PAYLOAD_MISSING" => "arm pose payload is missing",
            "ARM_POSE_AUTO_ROLLBACK_VRM_ORIGIN" => "arm pose auto-disabled after vrm-origin rollback guard trigger",
            "SHADOW_DISABLED_TOGGLE_OFF" => "realtime shadow toggle is off",
            "SHADOW_DISABLED_FAST_FALLBACK" => "realtime shadow is disabled in fast fallback profile",
            "SHADOW_DISABLED_NO_SHADOW_PASS_MATERIAL" => "no material advertises shadow pass",
            "SHADOW_DISABLED_SHADOW_DRAW_EMPTY" => "shadow draw queue is empty",
            "SHADOW_PASS_NOT_REPORTED" => "active passes do not report shadow",
            "EXPRESSION_COUNT_ZERO" => "avatar has no expression payload",
            "TRACKING_INACTIVE" => "tracking is not active",
            "TRACKING_STALE" => "tracking input is stale",
            _ when reasonCode.StartsWith("NC_SET_", StringComparison.Ordinal) => "native submit failed",
            _ => "feature gated by runtime policy",
        };
    }

    public static string ResolveOperatorActionHint(string commonClass, string commonReasonCode)
    {
        var normalizedClass = NormalizeCode(commonClass, "none_detected");
        var normalizedReason = NormalizeCode(commonReasonCode, "none");

        return normalizedClass switch
        {
            "runtime_binary_mismatch" =>
                "Runtime/build mismatch detected. Republish dist/wpf and relaunch WpfHost.exe.",
            "native_submit_failure" =>
                "Native submit failed. Check tracking diagnostics and last NC_SET_* error code.",
            "payload_policy_gate" => normalizedReason switch
            {
                "EXPRESSION_COUNT_ZERO" =>
                    "Avatar expression payload is empty. Re-export avatar with expression catalog/blendshape bindings.",
                "ARM_POSE_DISABLED_BY_STATIC_SKINNING_POLICY" =>
                    "Arm pose is blocked by static skinning policy. Use payloads that include valid rig/skinning data.",
                "ARM_POSE_PAYLOAD_MISSING" =>
                    "Arm pose payload is missing. Re-export avatar with skeleton/skin/rig payloads.",
                "ARM_POSE_AUTO_ROLLBACK_VRM_ORIGIN" =>
                    "VRM-origin rollback guard disabled arm pose for safety. Keep current session or adjust pose policy after validating mesh-space stability.",
                "SHADOW_DISABLED_TOGGLE_OFF" =>
                    "Realtime shadow is disabled by toggle. Turn on shadow and retest.",
                "SHADOW_DISABLED_FAST_FALLBACK" =>
                    "Realtime shadow is disabled in fast fallback profile. Increase quality profile and retest.",
                "SHADOW_DISABLED_NO_SHADOW_PASS_MATERIAL" =>
                    "Avatar materials do not advertise a shadow pass. Compare with another avatar or re-export materials with shadow-pass support.",
                "SHADOW_DISABLED_SHADOW_DRAW_EMPTY" =>
                    "Shadow queue is empty. Validate material pass setup and render pass authoring.",
                _ =>
                    "Payload/policy gate detected. Check FeatureGate reason codes in Runtime and Avatar diagnostics.",
            },
            "tracking_input_inactive" => normalizedReason switch
            {
                "TRACKING_INACTIVE" =>
                    "Tracking is inactive. Start tracking and verify source input health.",
                "TRACKING_STALE" =>
                    "Tracking input is stale. Check camera/source connectivity and stale timeout settings.",
                _ =>
                    "Tracking input is not healthy. Verify source status and diagnostics.",
            },
            _ => "No blocking common cause detected.",
        };
    }
}
