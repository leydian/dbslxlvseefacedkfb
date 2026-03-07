#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "animiq/core/result.h"

namespace animiq::vsf {

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

struct SerializedMeshObjectBlob {
    std::uint64_t path_id = 0;
    std::uint32_t byte_start = 0;
    std::uint32_t byte_size = 0;
    std::vector<unsigned char> bytes;
};

class SerializedFileReader {
  public:
    core::Result<SerializedFileSummary> ParseObjectSummary(const std::vector<unsigned char>& bytes) const;
    core::Result<std::vector<SerializedMeshObjectBlob>> ExtractMeshObjectBlobs(
        const std::vector<unsigned char>& bytes,
        std::size_t max_mesh_objects = 8U) const;
};

}  // namespace animiq::vsf
