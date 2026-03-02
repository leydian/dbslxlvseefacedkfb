#include "vrm_loader.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace vsfclone::avatar {

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool VrmLoader::CanLoadPath(const std::string& path) const {
    const auto ext = ToLower(fs::path(path).extension().string());
    return ext == ".vrm";
}

core::Result<AvatarPackage> VrmLoader::Load(const std::string& path) const {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return core::Result<AvatarPackage>::Fail("could not open vrm file");
    }

    AvatarPackage pkg;
    pkg.source_type = AvatarSourceType::Vrm;
    pkg.compat_level = AvatarCompatLevel::Partial;
    pkg.source_path = path;
    pkg.display_name = fs::path(path).stem().string();
    pkg.warnings.push_back("VRM parser is scaffold only. glTF/VRM decode not wired yet.");
    pkg.missing_features.push_back("glTF/VRM scene decode");
    pkg.missing_features.push_back("MToon parameter binding");
    return core::Result<AvatarPackage>::Ok(pkg);
}

}  // namespace vsfclone::avatar
