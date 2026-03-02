#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vsfclone/core/result.h"

namespace vsfclone::vsf {

enum class UnityFsCompressionMode {
    None = 0,
    Lzma = 1,
    Lz4 = 2,
    Lz4Hc = 3,
    Unknown = 255,
};

struct UnityFsHeader {
    std::string signature;
    std::uint32_t format_version = 0;
    std::string minimum_player_version;
    std::string engine_version;
    std::uint64_t bundle_file_size = 0;
    std::uint32_t compressed_metadata_size = 0;
    std::uint32_t uncompressed_metadata_size = 0;
    std::uint32_t flags = 0;
    UnityFsCompressionMode compression_mode = UnityFsCompressionMode::Unknown;
};

struct UnityFsProbe {
    UnityFsHeader header;
    bool has_cab_token = false;
    std::uint32_t vrm_token_hits = 0;
};

class UnityFsReader {
  public:
    core::Result<UnityFsProbe> Probe(const std::string& path) const;
};

}  // namespace vsfclone::vsf

