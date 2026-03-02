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
    std::string probe_stage = "header";
    std::string probe_primary_error;
    bool metadata_parsed = false;
    std::uint32_t block_count = 0;
    std::uint32_t node_count = 0;
    std::string first_node_path;
    std::uint32_t serialized_candidate_count = 0;
    bool object_table_parsed = false;
    std::uint32_t object_count = 0;
    std::uint32_t mesh_object_count = 0;
    std::uint32_t material_object_count = 0;
    std::uint32_t texture_object_count = 0;
    std::uint32_t game_object_count = 0;
    std::uint32_t skinned_mesh_renderer_count = 0;
    std::string major_types_found;
    std::uint32_t reconstruction_attempts = 0;
    std::uint64_t reconstruction_success_offset = 0;
    std::string serialized_parse_error_code;
    std::uint64_t metadata_offset = 0;
    std::string metadata_decode_strategy;
    std::uint32_t metadata_decode_mode = 0;
    std::string metadata_decode_error_code;
    std::string selected_block_layout;
    std::string selected_reconstruction_layout;
    std::string selected_block0_hypothesis;
    std::uint32_t block0_attempt_count = 0;
    std::uint64_t block0_selected_offset = 0;
    std::string block0_selected_mode_source;
    std::string selected_offset_family;
    std::string reconstruction_failure_summary_code;
    std::uint32_t reconstruction_candidate_count = 0;
    std::int32_t best_candidate_score = 0;
    std::uint64_t total_block_compressed_size = 0;
    std::uint64_t total_block_uncompressed_size = 0;
    std::uint32_t reconstruction_best_partial_blocks = 0;
    std::uint32_t failed_block_index = 0;
    std::uint32_t failed_block_mode = 0;
    std::uint32_t failed_block_expected_size = 0;
    std::uint64_t failed_block_read_offset = 0;
    std::uint32_t failed_block_compressed_size = 0;
    std::uint32_t failed_block_uncompressed_size = 0;
    std::string failed_block_error_code;
    std::string metadata_error;
};

class UnityFsReader {
  public:
    core::Result<UnityFsProbe> Probe(const std::string& path) const;
};

}  // namespace vsfclone::vsf
