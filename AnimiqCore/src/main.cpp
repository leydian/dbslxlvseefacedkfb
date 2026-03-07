#include <iostream>
#include <string>

#include "animiq/avatar/avatar_loader_facade.h"
#include "animiq/avatar/avatar_package.h"

namespace {

const char* SourceTypeName(animiq::avatar::AvatarSourceType t) {
    using animiq::avatar::AvatarSourceType;
    switch (t) {
        case AvatarSourceType::Vrm:
            return "VRM";
        case AvatarSourceType::VxAvatar:
            return "VXAvatar";
        case AvatarSourceType::Vxa2:
            return "VXA2";
        case AvatarSourceType::Miq:
            return "MIQ";
        case AvatarSourceType::VsfAvatar:
            return "VSFAvatar(UnityFS)";
        default:
            return "Unknown";
    }
}

void PrintUsage() {
    std::cout << "Usage:\n"
              << "  animiq_cli <path_to_avatar_file> [--format=vrm|miq|vsfavatar]\n"
              << "Examples:\n"
              << "  animiq_cli sample.txt --format=vrm\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    std::string path;
    animiq::avatar::AvatarLoadOptions options;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.find("--format=") == 0) {
            std::string fmt = arg.substr(9);
            if (fmt == "vrm") options.forced_source_type = animiq::avatar::AvatarSourceType::Vrm;
            else if (fmt == "miq") options.forced_source_type = animiq::avatar::AvatarSourceType::Miq;
            else if (fmt == "vsfavatar") options.forced_source_type = animiq::avatar::AvatarSourceType::VsfAvatar;
        } else if (path.empty()) {
            path = arg;
        }
    }

    if (path.empty()) {
        PrintUsage();
        return 1;
    }

    animiq::avatar::AvatarLoaderFacade loader;
    auto result = loader.Load(path, options);
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
