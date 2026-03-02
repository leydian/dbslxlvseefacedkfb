#include "vsfavatar_loader.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace vsfclone::avatar {

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool VsfAvatarLoader::CanLoadPath(const std::string& path) const {
    const auto ext = ToLower(fs::path(path).extension().string());
    return ext == ".vsfavatar";
}

core::Result<AvatarPackage> VsfAvatarLoader::Load(const std::string& path) const {
    auto probe = reader_.Probe(path);
    if (!probe.ok) {
        return core::Result<AvatarPackage>::Fail(probe.error);
    }

    AvatarPackage pkg;
    pkg.source_type = AvatarSourceType::VsfAvatar;
    pkg.compat_level = AvatarCompatLevel::Partial;
    pkg.source_path = path;
    pkg.display_name = fs::path(path).stem().string();
    pkg.materials.push_back({"Default", "MToon (placeholder)"});

    std::ostringstream warn;
    warn << "UnityFS " << probe.value.header.engine_version
         << ", compression mode=" << static_cast<int>(probe.value.header.compression_mode)
         << ", VRM token hits=" << probe.value.vrm_token_hits;
    pkg.warnings.push_back(warn.str());
    if (!probe.value.has_cab_token) {
        pkg.warnings.push_back("CAB token not found in first probe window.");
    }
    pkg.missing_features.push_back("UnityFS metadata decompression");
    pkg.missing_features.push_back("SerializedFile object table decode");
    pkg.missing_features.push_back("mesh/material extraction");

    return core::Result<AvatarPackage>::Ok(pkg);
}

}  // namespace vsfclone::avatar
