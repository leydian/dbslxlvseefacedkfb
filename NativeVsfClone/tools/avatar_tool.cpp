#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>

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
              << "  avatar_tool <path_to_avatar_file> [--xav2-unknown-section-policy=warn|ignore|fail]\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    std::string path;
    vsfclone::avatar::AvatarLoadOptions load_options {};
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        constexpr const char* kPolicyArg = "--xav2-unknown-section-policy=";
        if (arg.rfind(kPolicyArg, 0) == 0) {
            const std::string raw = arg.substr(std::char_traits<char>::length(kPolicyArg));
            if (!TryParsePolicy(raw, &load_options.xav2_unknown_section_policy)) {
                std::cerr << "invalid policy: " << raw << " (expected warn|ignore|fail)\n";
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
    std::cout << "  LastRenderDrawCalls: " << info.last_render_draw_calls << "\n";
    std::cout << "  FormatSections: " << info.format_section_count << "\n";
    std::cout << "  FormatDecodedSections: " << info.format_decoded_section_count << "\n";
    std::cout << "  FormatUnknownSections: " << info.format_unknown_section_count << "\n";
    std::cout << "  Warnings: " << info.warnings.size() << "\n";
    std::cout << "  WarningCodes: " << info.warning_codes.size() << "\n";
    for (std::size_t i = 0; i < info.warning_codes.size(); ++i) {
        std::cout << "  WarningCode[" << i << "]: " << info.warning_codes[i] << "\n";
    }
    if (!info.warning_codes.empty()) {
        std::cout << "  LastWarningCode: " << info.warning_codes.back() << "\n";
    }
    if (!info.warnings.empty()) {
        std::cout << "  LastWarning: " << info.warnings.back() << "\n";
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
