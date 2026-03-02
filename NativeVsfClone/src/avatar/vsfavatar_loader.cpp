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
    for (std::uint32_t i = 0; i < probe.value.mesh_object_count; ++i) {
        pkg.meshes.push_back({"Mesh_" + std::to_string(i), 0, 0});
    }
    for (std::uint32_t i = 0; i < probe.value.material_object_count; ++i) {
        pkg.materials.push_back({"Material_" + std::to_string(i), "UnityShader (placeholder)"}); 
    }
    if (pkg.materials.empty()) {
        pkg.materials.push_back({"Default", "MToon (placeholder)"});
    }

    std::ostringstream warn;
    warn << "UnityFS " << probe.value.header.engine_version
         << ", compression mode=" << static_cast<int>(probe.value.header.compression_mode)
         << ", VRM token hits=" << probe.value.vrm_token_hits;
    pkg.warnings.push_back(warn.str());
    if (probe.value.metadata_parsed) {
        std::ostringstream meta;
        meta << "metadata parsed: blocks=" << probe.value.block_count
             << ", nodes=" << probe.value.node_count
             << ", block compressed total=" << probe.value.total_block_compressed_size
             << ", block uncompressed total=" << probe.value.total_block_uncompressed_size
             << ", reconstruct attempts=" << probe.value.reconstruction_attempts;
        if (probe.value.reconstruction_best_partial_blocks > 0U) {
            meta << ", best partial blocks=" << probe.value.reconstruction_best_partial_blocks;
        }
        if (!probe.value.metadata_decode_strategy.empty()) {
            meta << ", decode strategy=" << probe.value.metadata_decode_strategy
                 << ", decode mode=" << probe.value.metadata_decode_mode;
        }
        if (!probe.value.selected_block_layout.empty()) {
            meta << ", block layout=" << probe.value.selected_block_layout;
        }
        if (!probe.value.selected_reconstruction_layout.empty()) {
            meta << ", recon layout=" << probe.value.selected_reconstruction_layout;
        }
        if (!probe.value.reconstruction_failure_summary_code.empty()) {
            meta << ", recon summary code=" << probe.value.reconstruction_failure_summary_code;
        }
        if (probe.value.metadata_offset > 0U) {
            meta << ", metadata offset=" << probe.value.metadata_offset;
        }
        if (probe.value.reconstruction_success_offset > 0U) {
            meta << ", reconstruct success offset=" << probe.value.reconstruction_success_offset;
        }
        if (!probe.value.first_node_path.empty()) {
            meta << ", first node=" << probe.value.first_node_path;
        }
        pkg.warnings.push_back(meta.str());
    }
    if (!probe.value.metadata_error.empty()) {
        if (probe.value.metadata_parsed) {
            pkg.warnings.push_back("metadata/serialized diagnostic: " + probe.value.metadata_error);
        } else {
            pkg.warnings.push_back("metadata parse failed: " + probe.value.metadata_error);
        }
        if (!probe.value.metadata_decode_error_code.empty()) {
            pkg.warnings.push_back("metadata decode code: " + probe.value.metadata_decode_error_code);
        }
        if (!probe.value.failed_block_error_code.empty()) {
            std::ostringstream block;
            block << "data block diagnostic: index=" << probe.value.failed_block_index
                  << ", mode=" << probe.value.failed_block_mode
                  << ", expected=" << probe.value.failed_block_expected_size
                  << ", code=" << probe.value.failed_block_error_code;
            pkg.warnings.push_back(block.str());
        }
    }
    if (probe.value.object_table_parsed) {
        std::ostringstream obj;
        obj << "object table parsed: objects=" << probe.value.object_count
            << ", meshes=" << probe.value.mesh_object_count
            << ", materials=" << probe.value.material_object_count;
        if (!probe.value.major_types_found.empty()) {
            obj << ", types={" << probe.value.major_types_found << "}";
        }
        pkg.warnings.push_back(obj.str());
        pkg.warnings.push_back("payload decode pending: mesh vertex/index and material parameter extraction.");
    } else if (!probe.value.serialized_parse_error_code.empty()) {
        pkg.warnings.push_back("serialized parse code: " + probe.value.serialized_parse_error_code);
    }
    if (!probe.value.has_cab_token) {
        pkg.warnings.push_back("CAB token not found in first probe window.");
    }
    if (!probe.value.metadata_parsed) {
        pkg.missing_features.push_back("UnityFS metadata decompression");
    }
    if (!probe.value.object_table_parsed) {
        pkg.missing_features.push_back("SerializedFile object table decode");
    }
    if (probe.value.mesh_object_count == 0 && probe.value.material_object_count == 0) {
        pkg.missing_features.push_back("mesh/material object discovery");
    } else {
        pkg.missing_features.push_back("mesh/material payload extraction");
    }

    return core::Result<AvatarPackage>::Ok(pkg);
}

}  // namespace vsfclone::avatar
