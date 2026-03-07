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

    public static string ResolveReasonText(string reasonCode)
    {
        return reasonCode switch
        {
            "HOST_RUNTIME_PATH_UNKNOWN" => "런타임 경로를 알 수 없습니다",
            "HOST_RUNTIME_MISMATCH_DIST_EXPECTED" => "런타임 nativecore 경로가 일치하지 않습니다",
            "HOST_RUNTIME_DIST_OLDER_THAN_BUILD_OUTPUT" => "런타임 nativecore가 빌드 결과물보다 오래되었습니다",
            "NO_AVATAR_LOADED" => "아바타가 로드되지 않았습니다",
            "ARM_POSE_FORMAT_UNSUPPORTED" => "팔 포즈는 MIQ 형식 아바타에서만 지원됩니다",
            "ARM_POSE_DISABLED_BY_STATIC_SKINNING_POLICY" => "정적 스키닝 정책으로 팔 포즈가 비활성화되었습니다",
            "ARM_POSE_PAYLOAD_MISSING" => "팔 포즈 페이로드가 없습니다",
            "ARM_POSE_AUTO_ROLLBACK_VRM_ORIGIN" => "VRM 원점 롤백 감지로 팔 포즈가 자동 비활성화되었습니다",
            "SHADOW_DISABLED_TOGGLE_OFF" => "실시간 그림자 토글이 꺼져 있습니다",
            "SHADOW_DISABLED_FAST_FALLBACK" => "빠른 폴백 프로파일에서 실시간 그림자가 비활성화됩니다",
            "SHADOW_DISABLED_NO_SHADOW_PASS_MATERIAL" => "그림자 패스를 지원하는 머티리얼이 없습니다",
            "SHADOW_DISABLED_SHADOW_DRAW_EMPTY" => "그림자 드로우 큐가 비어 있습니다",
            "SHADOW_PASS_NOT_REPORTED" => "이 아바타 셰이더가 그림자 패스를 지원하지 않습니다",
            "EXPRESSION_COUNT_ZERO" => "아바타에 표정 페이로드가 없습니다",
            "TRACKING_INACTIVE" => "트래킹이 시작되지 않았습니다",
            "TRACKING_STALE" => "트래킹 입력이 오래되었습니다 (연결 확인)",
            _ when reasonCode.StartsWith("NC_SET_", StringComparison.Ordinal) => "네이티브 제출에 실패했습니다",
            _ => "런타임 정책에 의해 기능이 제한되었습니다",
        };
    }

    public static string ResolveOperatorActionHint(string commonClass, string commonReasonCode)
    {
        var normalizedClass = NormalizeCode(commonClass, "none_detected");
        var normalizedReason = NormalizeCode(commonReasonCode, "none");

        return normalizedClass switch
        {
            "runtime_binary_mismatch" =>
                "런타임/빌드 불일치가 감지되었습니다. dist/wpf를 다시 배포하고 WpfHost.exe를 재시작하세요.",
            "native_submit_failure" =>
                "네이티브 제출에 실패했습니다. 트래킹 진단 및 마지막 NC_SET_* 오류 코드를 확인하세요.",
            "payload_policy_gate" => normalizedReason switch
            {
                "EXPRESSION_COUNT_ZERO" =>
                    "아바타 표정 페이로드가 비어 있습니다. 표정 카탈로그/블렌드셰이프 바인딩을 포함하여 아바타를 다시 내보내세요.",
                "ARM_POSE_DISABLED_BY_STATIC_SKINNING_POLICY" =>
                    "정적 스키닝 정책으로 팔 포즈가 차단되었습니다. 유효한 리그/스키닝 데이터가 포함된 페이로드를 사용하세요.",
                "ARM_POSE_PAYLOAD_MISSING" =>
                    "팔 포즈 페이로드가 없습니다. 스켈레톤/스킨/리그 페이로드를 포함하여 아바타를 다시 내보내세요.",
                "ARM_POSE_AUTO_ROLLBACK_VRM_ORIGIN" =>
                    "VRM 원점 롤백 가드가 안전을 위해 팔 포즈를 비활성화했습니다. 현재 세션을 유지하거나 메시 공간 안정성 검증 후 포즈 정책을 조정하세요.",
                "SHADOW_DISABLED_TOGGLE_OFF" =>
                    "그림자 토글이 꺼져 있습니다. 그림자를 켜고 다시 테스트하세요.",
                "SHADOW_DISABLED_FAST_FALLBACK" =>
                    "빠른 폴백 프로파일에서 실시간 그림자가 비활성화됩니다. 품질 프로파일을 높이고 다시 테스트하세요.",
                "SHADOW_DISABLED_NO_SHADOW_PASS_MATERIAL" =>
                    "아바타 머티리얼이 그림자 패스를 지원하지 않습니다. 다른 아바타와 비교하거나 그림자 패스 지원 머티리얼로 다시 내보내세요.",
                "SHADOW_DISABLED_SHADOW_DRAW_EMPTY" =>
                    "그림자 큐가 비어 있습니다. 머티리얼 패스 설정과 렌더 패스 작성을 확인하세요.",
                _ =>
                    "페이로드/정책 게이트가 감지되었습니다. 런타임 및 아바타 진단에서 FeatureGate 이유 코드를 확인하세요.",
            },
            "tracking_input_inactive" => normalizedReason switch
            {
                "TRACKING_INACTIVE" =>
                    "트래킹이 비활성 상태입니다. 트래킹을 시작하고 입력 소스 상태를 확인하세요.",
                "TRACKING_STALE" =>
                    "트래킹 입력이 오래되었습니다. 카메라/소스 연결 상태와 지연 시간 설정을 확인하세요.",
                _ =>
                    "트래킹 입력이 정상적이지 않습니다. 소스 상태와 진단 정보를 확인하세요.",
            },
            _ => "차단 공통 원인이 감지되지 않았습니다.",
        };
    }
}
