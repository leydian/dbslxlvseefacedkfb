#include <iostream>
#include <string>

#include "vsfclone/avatar/avatar_loader_facade.h"
#include "vsfclone/avatar/avatar_package.h"

namespace {

const char* SourceTypeName(vsfclone::avatar::AvatarSourceType t) {
    using vsfclone::avatar::AvatarSourceType;
    switch (t) {
        case AvatarSourceType::Vrm:
            return "VRM";
        case AvatarSourceType::VxAvatar:
            return "VXAvatar";
        case AvatarSourceType::Vxa2:
            return "VXA2";
        case AvatarSourceType::Xav2:
            return "XAV2";
        case AvatarSourceType::VsfAvatar:
            return "VSFAvatar(UnityFS)";
        default:
            return "Unknown";
    }
}

void PrintUsage() {
    std::cout << "Usage:\n"
              << "  vsfclone_cli <path_to_avatar_file>\n"
              << "Examples:\n"
              << "  vsfclone_cli D:\\\\avatars\\\\sample.vsfavatar\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    const std::string path = argv[1];
    vsfclone::avatar::AvatarLoaderFacade loader;
    auto result = loader.Load(path);
    if (!result.ok) {
        std::cerr << "Load failed: " << result.error << "\n";
        return 2;
    }

    const auto& pkg = result.value;
    std::cout << "Load succeeded\n";
    std::cout << "  SourceType: " << SourceTypeName(pkg.source_type) << "\n";
    std::cout << "  DisplayName: " << pkg.display_name << "\n";
    std::cout << "  Meshes: " << pkg.meshes.size() << "\n";
    std::cout << "  Materials: " << pkg.materials.size() << "\n";
    if (!pkg.warnings.empty()) {
        std::cout << "  Warnings:\n";
        for (const auto& w : pkg.warnings) {
            std::cout << "    - " << w << "\n";
        }
    }
    return 0;
}
