#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_set>

#include "vsfclone/avatar/avatar_loader_facade.h"
#include "vsfclone/avatar/avatar_package.h"

namespace {

const char* ToFormatName(vsfclone::avatar::AvatarSourceType fmt) {
    using vsfclone::avatar::AvatarSourceType;
    switch (fmt) {
        case AvatarSourceType::Vrm:
            return "VRM";
        case AvatarSourceType::VxAvatar:
            return "VXAvatar";
        case AvatarSourceType::VsfAvatar:
            return "VSFAvatar";
        case AvatarSourceType::Vxa2:
            return "VXA2";
        case AvatarSourceType::Xav2:
            return "XAV2";
        default:
            return "Unknown";
    }
}

const char* ToCompatName(vsfclone::avatar::AvatarCompatLevel compat) {
    using vsfclone::avatar::AvatarCompatLevel;
    switch (compat) {
        case AvatarCompatLevel::Full:
            return "full";
        case AvatarCompatLevel::Partial:
            return "partial";
        case AvatarCompatLevel::Failed:
            return "failed";
        default:
            return "unknown";
    }
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

struct WarningMeta {
    const char* severity = "unknown";
    const char* category = "unknown";
    bool critical = false;
};

WarningMeta ClassifyWarningCode(const std::string& raw_code) {
    const std::string code = ToLower(raw_code);
    static const std::unordered_set<std::string> kCriticalCodes = {
        "xav2_skinning_static_disabled",
        "xav2_skinning_fallback_skipped_no_skeleton",
        "material_index_oob_skipped",
        "xav2_material_typed_texture_unresolved",
        "xav3_skeleton_payload_missing",
        "xav3_skeleton_mesh_bind_mismatch",
        "xav3_skinning_matrix_invalid",
        "xav2_unknown_section_not_allowed",
    };

    WarningMeta meta {};
    if (code.rfind("e_", 0U) == 0U) {
        meta.severity = "error";
        meta.category = "schema";
        meta.critical = true;
        return meta;
    }
    if (code == "w_stage") {
        meta.severity = "info";
        meta.category = "stage";
        return meta;
    }
    if (code == "w_layout" || code == "w_offset" || code == "w_recon_summary") {
        meta.severity = "info";
        meta.category = "layout";
        return meta;
    }
    if (code.rfind("w_", 0U) == 0U) {
        meta.severity = "warn";
        meta.category = "payload";
        return meta;
    }
    if (code == "skinning_matrix_convention_applied" || code == "skinning_matrix_convention_selected") {
        meta.severity = "info";
        meta.category = "render";
        return meta;
    }
    if (code == "xav2_skinning_convention_ambiguous") {
        meta.severity = "warn";
        meta.category = "render";
        return meta;
    }
    if (code == "skinning_static_disabled") {
        meta.severity = "warn";
        meta.category = "render";
        meta.critical = false;
        return meta;
    }
    if (code == "vrm_material_safe_fallback_applied" ||
        code == "vrm_mtoon_matcap_unresolved" ||
        code == "vrm_material_texture_unresolved") {
        meta.severity = "warn";
        meta.category = "render";
        meta.critical = false;
        return meta;
    }
    if (code.rfind("xav2_", 0U) == 0U || code.rfind("xav3_", 0U) == 0U || code.rfind("xav4_", 0U) == 0U) {
        meta.severity = "warn";
        meta.category = "render";
        meta.critical = kCriticalCodes.find(code) != kCriticalCodes.end();
        return meta;
    }
    if (code.rfind("vrm_", 0U) == 0U) {
        meta.severity = "warn";
        meta.category = "payload";
        return meta;
    }
    return meta;
}

bool TryParsePolicy(
    const std::string& raw,
    vsfclone::avatar::Xav2UnknownSectionPolicy* out_policy) {
    if (out_policy == nullptr) {
        return false;
    }
    const std::string policy = ToLower(raw);
    if (policy == "warn") {
        *out_policy = vsfclone::avatar::Xav2UnknownSectionPolicy::Warn;
        return true;
    }
    if (policy == "ignore") {
        *out_policy = vsfclone::avatar::Xav2UnknownSectionPolicy::Ignore;
        return true;
    }
    if (policy == "fail") {
        *out_policy = vsfclone::avatar::Xav2UnknownSectionPolicy::Fail;
        return true;
    }
    return false;
}

void PrintUsage() {
    std::cout << "Usage:\n"
              << "  avatar_tool <path_to_avatar_file> [--xav2-unknown-section-policy=warn|ignore|fail]\n"
              << "             [--dump-warnings | --dump-warnings-limit=<N>]\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    std::string path;
    vsfclone::avatar::AvatarLoadOptions load_options {};
    std::size_t warning_dump_limit = 0U;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        constexpr const char* kPolicyArg = "--xav2-unknown-section-policy=";
        constexpr const char* kDumpWarningsLimitArg = "--dump-warnings-limit=";
        if (arg.rfind(kPolicyArg, 0) == 0) {
            const std::string raw = arg.substr(std::char_traits<char>::length(kPolicyArg));
            if (!TryParsePolicy(raw, &load_options.xav2_unknown_section_policy)) {
                std::cerr << "invalid policy: " << raw << " (expected warn|ignore|fail)\n";
                return 1;
            }
            continue;
        }
        if (arg == "--dump-warnings") {
            warning_dump_limit = std::numeric_limits<std::size_t>::max();
            continue;
        }
        if (arg.rfind(kDumpWarningsLimitArg, 0) == 0) {
            const std::string raw = arg.substr(std::char_traits<char>::length(kDumpWarningsLimitArg));
            if (raw.empty()) {
                std::cerr << "invalid warning dump limit: empty value\n";
                return 1;
            }
            try {
                warning_dump_limit = static_cast<std::size_t>(std::stoull(raw));
            } catch (...) {
                std::cerr << "invalid warning dump limit: " << raw << "\n";
                return 1;
            }
            continue;
        }
        if (path.empty()) {
            path = arg;
            continue;
        }
        std::cerr << "unexpected argument: " << arg << "\n";
        PrintUsage();
        return 1;
    }
    if (path.empty()) {
        PrintUsage();
        return 1;
    }

    vsfclone::avatar::AvatarLoaderFacade loader;
    const auto loaded = loader.Load(path, load_options);
    if (!loaded.ok) {
        std::cerr << "Load failed: " << loaded.error << "\n";
        return 3;
    }
    const auto& info = loaded.value;

    std::cout << "Load succeeded\n";
    std::cout << "  DisplayName: " << info.display_name << "\n";
    std::cout << "  SourcePath: " << info.source_path << "\n";
    std::cout << "  Format: " << ToFormatName(info.source_type) << "\n";
    std::cout << "  Compat: " << ToCompatName(info.compat_level) << "\n";
    std::cout << "  ParserStage: " << info.parser_stage << "\n";
    std::cout << "  PrimaryError: " << info.primary_error_code << "\n";
    std::cout << "  Meshes: " << info.meshes.size() << "\n";
    std::cout << "  Materials: " << info.materials.size() << "\n";
    std::cout << "  MeshPayloads: " << info.mesh_payloads.size() << "\n";
    std::cout << "  SkinPayloads: " << info.skin_payloads.size() << "\n";
    std::cout << "  SkeletonPayloads: " << info.skeleton_payloads.size() << "\n";
    std::cout << "  MaterialPayloads: " << info.material_payloads.size() << "\n";
    std::cout << "  TexturePayloads: " << info.texture_payloads.size() << "\n";
    std::cout << "  ExpressionCount: " << info.expressions.size() << "\n";
    std::size_t expression_bind_total = 0U;
    for (const auto& expr : info.expressions) {
        expression_bind_total += expr.binds.size();
    }
    std::cout << "  ExpressionBindTotal: " << expression_bind_total << "\n";
    std::cout << "  SpringPayloads: " << info.springbone_payloads.size() << "\n";
    std::cout << "  PhysicsColliders: " << info.physics_colliders.size() << "\n";
    std::cout << "  LastRenderDrawCalls: " << info.last_render_draw_calls << "\n";
    std::cout << "  FormatSections: " << info.format_section_count << "\n";
    std::cout << "  FormatDecodedSections: " << info.format_decoded_section_count << "\n";
    std::cout << "  FormatUnknownSections: " << info.format_unknown_section_count << "\n";
    std::cout << "  Warnings: " << info.warnings.size() << "\n";
    std::cout << "  WarningCodes: " << info.warning_codes.size() << "\n";
    std::cout << "  MaterialDiagnostics: " << info.material_diagnostics.size() << "\n";
    std::size_t mtoon_advanced_material_count = 0U;
    std::size_t mtoon_fallback_material_count = 0U;
    std::size_t mtoon_outline_material_count = 0U;
    std::size_t mtoon_uv_anim_material_count = 0U;
    std::size_t mtoon_matcap_material_count = 0U;
    std::size_t vrm_safe_fallback_warning_count = 0U;
    std::size_t vrm_matcap_unresolved_warning_count = 0U;
    std::size_t vrm_texture_unresolved_warning_count = 0U;
    std::size_t opaque_material_count = 0U;
    std::size_t mask_material_count = 0U;
    std::size_t blend_material_count = 0U;
    for (const auto& diag : info.material_diagnostics) {
        const auto alpha_mode = ToLower(diag.alpha_mode);
        if (alpha_mode == "mask") {
            ++mask_material_count;
        } else if (alpha_mode == "blend") {
            ++blend_material_count;
        } else {
            ++opaque_material_count;
        }
        const std::size_t typed_total =
            static_cast<std::size_t>(diag.typed_color_param_count) +
            static_cast<std::size_t>(diag.typed_float_param_count) +
            static_cast<std::size_t>(diag.typed_texture_param_count);
        if (diag.has_mtoon_binding && (typed_total >= 12U || diag.has_rim_texture || diag.has_emission_texture)) {
            ++mtoon_advanced_material_count;
        }
        if (!diag.has_mtoon_binding) {
            ++mtoon_fallback_material_count;
        }
    }
    for (const auto& payload : info.material_payloads) {
        bool has_outline = false;
        bool has_uv_anim = false;
        bool has_matcap = false;
        for (const auto& p : payload.typed_float_params) {
            const auto key = ToLower(p.id);
            if ((key == "_outlinewidth" || key == "_outlinelightingmix") && std::abs(p.value) > 0.0001f) {
                has_outline = true;
            }
            if ((key == "_uvanimscrollx" || key == "_uvanimscrolly" || key == "_uvanimrotation") && std::abs(p.value) > 0.0001f) {
                has_uv_anim = true;
            }
            if (key == "_matcapblend" && p.value > 0.0001f) {
                has_matcap = true;
            }
        }
        for (const auto& t : payload.typed_texture_params) {
            const auto slot = ToLower(t.slot);
            if (slot == "matcap" || slot == "_matcaptex" || slot == "_matcaptexture") {
                has_matcap = true;
            }
            if (slot == "uvanimationmask" || slot == "_uvanimmasktex") {
                has_uv_anim = true;
            }
        }
        if (has_outline) {
            ++mtoon_outline_material_count;
        }
        if (has_uv_anim) {
            ++mtoon_uv_anim_material_count;
        }
        if (has_matcap) {
            ++mtoon_matcap_material_count;
        }
    }
    for (const auto& code_raw : info.warning_codes) {
        const auto code = ToLower(code_raw);
        if (code == "vrm_material_safe_fallback_applied") {
            ++vrm_safe_fallback_warning_count;
        } else if (code == "vrm_mtoon_matcap_unresolved") {
            ++vrm_matcap_unresolved_warning_count;
        } else if (code == "vrm_material_texture_unresolved") {
            ++vrm_texture_unresolved_warning_count;
        }
    }
    std::cout << "  OpaqueMaterials: " << opaque_material_count << "\n";
    std::cout << "  MaskMaterials: " << mask_material_count << "\n";
    std::cout << "  BlendMaterials: " << blend_material_count << "\n";
    std::cout << "  MtoonAdvancedMaterials: " << mtoon_advanced_material_count << "\n";
    std::cout << "  MtoonFallbackMaterials: " << mtoon_fallback_material_count << "\n";
    std::cout << "  MtoonOutlineMaterials: " << mtoon_outline_material_count << "\n";
    std::cout << "  MtoonUvAnimMaterials: " << mtoon_uv_anim_material_count << "\n";
    std::cout << "  MtoonMatcapMaterials: " << mtoon_matcap_material_count << "\n";
    std::cout << "  VrmSafeFallbackWarnings: " << vrm_safe_fallback_warning_count << "\n";
    std::cout << "  VrmMatcapUnresolvedWarnings: " << vrm_matcap_unresolved_warning_count << "\n";
    std::cout << "  VrmTextureUnresolvedWarnings: " << vrm_texture_unresolved_warning_count << "\n";
    std::size_t warning_info_count = 0U;
    std::size_t warning_warn_count = 0U;
    std::size_t warning_error_count = 0U;
    std::size_t critical_warning_count = 0U;
    for (std::size_t i = 0; i < info.warning_codes.size(); ++i) {
        const auto meta = ClassifyWarningCode(info.warning_codes[i]);
        if (std::string(meta.severity) == "info") {
            ++warning_info_count;
        } else if (std::string(meta.severity) == "warn") {
            ++warning_warn_count;
        } else if (std::string(meta.severity) == "error") {
            ++warning_error_count;
        }
        if (meta.critical) {
            ++critical_warning_count;
        }
        std::cout << "  WarningCode[" << i << "]: " << info.warning_codes[i] << "\n";
        std::cout << "  WarningCodeMeta[" << i << "]: severity=" << meta.severity
                  << ", category=" << meta.category
                  << ", critical=" << (meta.critical ? "true" : "false") << "\n";
    }
    std::cout << "  WarningInfoCount: " << warning_info_count << "\n";
    std::cout << "  WarningWarnCount: " << warning_warn_count << "\n";
    std::cout << "  WarningErrorCount: " << warning_error_count << "\n";
    std::cout << "  CriticalWarningCount: " << critical_warning_count << "\n";
    if (!info.warning_codes.empty()) {
        std::cout << "  LastWarningCode: " << info.warning_codes.back() << "\n";
        const auto last_meta = ClassifyWarningCode(info.warning_codes.back());
        std::cout << "  LastWarningSeverity: " << last_meta.severity << "\n";
        std::cout << "  LastWarningCategory: " << last_meta.category << "\n";
        std::cout << "  LastWarningCritical: " << (last_meta.critical ? "true" : "false") << "\n";
    }
    if (!info.warnings.empty()) {
        std::cout << "  LastWarning: " << info.warnings.back() << "\n";
    }
    if (warning_dump_limit > 0U && !info.warnings.empty()) {
        const std::size_t available = info.warnings.size();
        const std::size_t dump_count =
            warning_dump_limit == std::numeric_limits<std::size_t>::max()
                ? available
                : std::min(available, warning_dump_limit);
        std::cout << "  WarningDumpCount: " << dump_count << "\n";
        for (std::size_t i = 0U; i < dump_count; ++i) {
            std::cout << "  Warning[" << i << "]: " << info.warnings[i] << "\n";
        }
        if (dump_count < available) {
            std::cout << "  WarningDumpTruncated: true\n";
        }
    }
    if (!info.material_diagnostics.empty()) {
        const auto& diag = info.material_diagnostics.back();
        std::cout << "  LastMaterialDiag: " << diag.material_name
                  << ", alphaMode=" << diag.alpha_mode
                  << ", alphaSource=" << diag.alpha_source
                  << ", alphaCutoff=" << diag.alpha_cutoff
                  << ", doubleSided=" << (diag.double_sided ? "true" : "false")
                  << ", mtoonBinding=" << (diag.has_mtoon_binding ? "true" : "false")
                  << "\n";
    }
    if (!info.last_expression_summary.empty()) {
        std::cout << "  LastExpressionSummary: " << info.last_expression_summary << "\n";
    }
    std::cout << "  SpringBonePresent: " << (info.springbone_summary.present ? "true" : "false") << "\n";
    if (info.springbone_summary.present) {
        std::cout << "  SpringBoneSprings: " << info.springbone_summary.spring_count << "\n";
        std::cout << "  SpringBoneJoints: " << info.springbone_summary.joint_count << "\n";
        std::cout << "  SpringBoneColliders: " << info.springbone_summary.collider_count << "\n";
        std::cout << "  SpringBoneColliderGroups: " << info.springbone_summary.collider_group_count << "\n";
    }
    std::cout << "  MissingFeatures: " << info.missing_features.size() << "\n";
    if (!info.missing_features.empty()) {
        std::cout << "  LastMissingFeature: " << info.missing_features.back() << "\n";
    }

    return 0;
}
