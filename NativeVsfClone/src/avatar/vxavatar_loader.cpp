#include "vxavatar_loader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace vsfclone::avatar {

namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

}  // namespace

bool VxAvatarLoader::CanLoadPath(const std::string& path) const {
    const auto ext = ToLower(fs::path(path).extension().string());
    return ext == ".vxavatar";
}

core::Result<AvatarPackage> VxAvatarLoader::Load(const std::string& path) const {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return core::Result<AvatarPackage>::Fail("could not open vxavatar file");
    }

    std::array<unsigned char, 4> magic {};
    in.read(reinterpret_cast<char*>(magic.data()), static_cast<std::streamsize>(magic.size()));
    if (in.gcount() != static_cast<std::streamsize>(magic.size())) {
        return core::Result<AvatarPackage>::Fail("vxavatar file is too small");
    }

    AvatarPackage pkg;
    pkg.source_type = AvatarSourceType::VxAvatar;
    pkg.compat_level = AvatarCompatLevel::Partial;
    pkg.source_path = path;
    pkg.display_name = fs::path(path).stem().string();
    pkg.materials.push_back({"Default", "ShaderPolicy (placeholder)"});

    const bool is_zip = (magic[0] == 0x50U && magic[1] == 0x4BU);
    if (!is_zip) {
        pkg.compat_level = AvatarCompatLevel::Failed;
        pkg.missing_features.push_back("ZIP container header");
        pkg.warnings.push_back("vxavatar must be a ZIP container with manifest.json.");
    } else {
        pkg.warnings.push_back("vxavatar parsing is scaffold-only. manifest/material override decode is pending.");
        pkg.missing_features.push_back("manifest.json parser");
        pkg.missing_features.push_back("material override application");
    }

    return core::Result<AvatarPackage>::Ok(pkg);
}

}  // namespace vsfclone::avatar
