#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vsfclone/core/result.h"

namespace vsfclone::vsf {

struct SerializedFileSummary {
    std::uint32_t object_count = 0;
    std::uint32_t mesh_object_count = 0;
    std::uint32_t material_object_count = 0;
    std::uint32_t texture_object_count = 0;
    std::uint32_t game_object_count = 0;
    std::uint32_t skinned_mesh_renderer_count = 0;
    std::string major_types_found;
    std::string parse_path;
    std::string error_code;
};

class SerializedFileReader {
  public:
    core::Result<SerializedFileSummary> ParseObjectSummary(const std::vector<unsigned char>& bytes) const;
};

}  // namespace vsfclone::vsf
