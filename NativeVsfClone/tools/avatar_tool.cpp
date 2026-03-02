#include <iostream>
#include <string>

#include "vsfclone/nativecore/api.h"

namespace {

const char* ToFormatName(NcAvatarFormatHint fmt) {
    switch (fmt) {
        case NC_AVATAR_FORMAT_VRM:
            return "VRM";
        case NC_AVATAR_FORMAT_VXAVATAR:
            return "VXAvatar";
        case NC_AVATAR_FORMAT_VSFAVATAR:
            return "VSFAvatar";
        case NC_AVATAR_FORMAT_VXA2:
            return "VXA2";
        default:
            return "Unknown";
    }
}

const char* ToCompatName(NcCompatLevel compat) {
    switch (compat) {
        case NC_COMPAT_FULL:
            return "full";
        case NC_COMPAT_PARTIAL:
            return "partial";
        case NC_COMPAT_FAILED:
            return "failed";
        default:
            return "unknown";
    }
}

void PrintUsage() {
    std::cout << "Usage:\n"
              << "  avatar_tool <path_to_avatar_file>\n";
}

void PrintLastError() {
    NcErrorInfo err {};
    if (nc_get_last_error(&err) == NC_OK) {
        std::cerr << "error subsystem=" << err.subsystem << ", code=" << static_cast<int>(err.code)
                  << ", message=" << err.message << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    NcInitOptions init {};
    init.api_version = 1;
    if (nc_initialize(&init) != NC_OK) {
        PrintLastError();
        return 2;
    }

    NcAvatarLoadRequest req {};
    req.path = argv[1];
    req.format_hint = NC_AVATAR_FORMAT_AUTO;

    NcAvatarHandle handle = 0;
    NcAvatarInfo info {};
    const auto rc = nc_load_avatar(&req, &handle, &info);
    if (rc != NC_OK) {
        PrintLastError();
        nc_shutdown();
        return 3;
    }

    std::cout << "Load succeeded\n";
    std::cout << "  Handle: " << handle << "\n";
    std::cout << "  DisplayName: " << info.display_name << "\n";
    std::cout << "  SourcePath: " << info.source_path << "\n";
    std::cout << "  Format: " << ToFormatName(info.detected_format) << "\n";
    std::cout << "  Compat: " << ToCompatName(info.compat_level) << "\n";
    std::cout << "  ParserStage: " << info.parser_stage << "\n";
    std::cout << "  PrimaryError: " << info.primary_error_code << "\n";
    std::cout << "  Meshes: " << info.mesh_count << "\n";
    std::cout << "  Materials: " << info.material_count << "\n";
    std::cout << "  MeshPayloads: " << info.mesh_payload_count << "\n";
    std::cout << "  MaterialPayloads: " << info.material_payload_count << "\n";
    std::cout << "  TexturePayloads: " << info.texture_payload_count << "\n";
    std::cout << "  FormatSections: " << info.format_section_count << "\n";
    std::cout << "  FormatDecodedSections: " << info.format_decoded_section_count << "\n";
    std::cout << "  FormatUnknownSections: " << info.format_unknown_section_count << "\n";
    std::cout << "  Warnings: " << info.warning_count << "\n";
    if (info.last_warning[0] != '\0') {
        std::cout << "  LastWarning: " << info.last_warning << "\n";
    }
    std::cout << "  MissingFeatures: " << info.missing_feature_count << "\n";
    if (info.last_missing_feature[0] != '\0') {
        std::cout << "  LastMissingFeature: " << info.last_missing_feature << "\n";
    }

    nc_unload_avatar(handle);
    nc_shutdown();
    return 0;
}
