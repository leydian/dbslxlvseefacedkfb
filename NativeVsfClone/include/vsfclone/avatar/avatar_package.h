#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vsfclone::avatar {

enum class AvatarSourceType {
    Unknown = 0,
    Vrm,
    VxAvatar,
    VsfAvatar,
};

enum class AvatarCompatLevel {
    Unknown = 0,
    Full,
    Partial,
    Failed,
};

struct MeshAssetSummary {
    std::string name;
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
};

struct MaterialAssetSummary {
    std::string name;
    std::string shader_name;
};

struct AvatarPackage {
    AvatarSourceType source_type = AvatarSourceType::Unknown;
    AvatarCompatLevel compat_level = AvatarCompatLevel::Unknown;
    std::string source_path;
    std::string display_name;
    std::vector<MeshAssetSummary> meshes;
    std::vector<MaterialAssetSummary> materials;
    std::vector<std::string> warnings;
    std::vector<std::string> missing_features;
};

}  // namespace vsfclone::avatar
