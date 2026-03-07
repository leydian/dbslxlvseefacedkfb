#include <filesystem>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

#include "animiq/vsf/unityfs_reader.h"

namespace fs = std::filesystem;

namespace {

std::string EscapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8U);
    for (const auto c : s) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

void PrintErrorJson(const std::string& error) {
    std::cout << "{"
              << "\"status\":\"error\","
              << "\"schema_version\":6,"
              << "\"extractor_version\":\"inhouse-sidecar-v6\","
              << "\"error_code\":\"SIDECAR_RUNTIME_ERROR\","
              << "\"primary_error_code\":\"SIDECAR_RUNTIME_ERROR\","
              << "\"error_message\":\"" << EscapeJson(error) << "\","
              << "\"error\":\"" << EscapeJson(error) << "\""
              << "}\n";
}

std::string JoinJsonStringArray(const std::vector<std::string>& items) {
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0U) {
            out << ",";
        }
        out << "\"" << EscapeJson(items[i]) << "\"";
    }
    out << "]";
    return out.str();
}

std::string TruncateForSummary(const std::string& text, std::size_t max_chars) {
    if (text.size() <= max_chars) {
        return text;
    }
    return text.substr(0, max_chars) + "...";
}

}  // namespace

int main(int argc, char** argv) {
    const auto started_at = std::chrono::steady_clock::now();
    if (argc < 2) {
        PrintErrorJson("usage: vsfavatar_sidecar <path_to_vsfavatar>");
        return 1;
    }

    const std::string path = argv[1];
    if (!fs::exists(path)) {
        PrintErrorJson("file not found");
        return 2;
    }

    animiq::vsf::UnityFsReader reader;
    const auto probe = reader.Probe(path);
    if (!probe.ok) {
        PrintErrorJson(probe.error);
        return 3;
    }

    const auto& p = probe.value;
    const auto mesh_count = p.mesh_object_count;
    const auto material_count = p.material_object_count > 0U ? p.material_object_count : 1U;
    std::vector<std::string> warnings;
    std::vector<std::string> missing_features;

    std::ostringstream warning;
    if (!p.metadata_error.empty()) {
        warning << TruncateForSummary(p.metadata_error, 180U);
    } else {
        warning << "metadata parsed";
    }
    if (!p.reconstruction_failure_summary_code.empty()) {
        warning << ", recon summary code=" << p.reconstruction_failure_summary_code;
    }
    if (!p.selected_block_layout.empty()) {
        warning << ", block layout=" << p.selected_block_layout;
    }
    if (!p.selected_reconstruction_layout.empty()) {
        warning << ", recon layout=" << p.selected_reconstruction_layout;
    }
    warnings.push_back("W_META: " + warning.str());
    if (!p.failed_block_error_code.empty()) {
        std::ostringstream block;
        block << "block=" << p.failed_block_index
              << ", mode=" << p.failed_block_mode
              << ", read_offset=" << p.failed_block_read_offset
              << ", csize=" << p.failed_block_compressed_size
              << ", usize=" << p.failed_block_uncompressed_size
              << ", expected=" << p.failed_block_expected_size
              << ", code=" << p.failed_block_error_code;
        warnings.push_back("W_RECON: " + block.str());
    }
    warnings.push_back("W_RECON_META: candidates=" + std::to_string(p.reconstruction_candidate_count) +
                       ", best-score=" + std::to_string(p.best_candidate_score));
    warnings.push_back("W_SERIALIZED_META: candidates=" + std::to_string(p.serialized_candidate_count) +
                       ", attempts=" + std::to_string(p.serialized_attempt_count) +
                       ", best-score=" + std::to_string(p.serialized_best_candidate_score));
    if (!p.serialized_detail_error_code.empty() ||
        p.serialized_last_failure_offset > 0U ||
        p.serialized_last_failure_window_size > 0U ||
        !p.serialized_last_failure_code.empty()) {
        warnings.push_back("W_SERIALIZED_DETAIL: code=" + p.serialized_detail_error_code +
                           ", last-offset=" + std::to_string(p.serialized_last_failure_offset) +
                           ", window=" + std::to_string(p.serialized_last_failure_window_size) +
                           ", last-code=" + p.serialized_last_failure_code);
    }
    if (!p.serialized_best_candidate_path.empty()) {
        warnings.push_back("W_SERIALIZED_PATH: " + p.serialized_best_candidate_path);
    }
    if (!p.serialized_parse_path.empty()) {
        warnings.push_back("W_SERIALIZED_PARSE_PATH: " + p.serialized_parse_path);
    }
    if (!p.major_types_found.empty()) {
        warnings.push_back("W_SERIALIZED_TYPES: " + p.major_types_found);
    }
    if (!p.selected_block0_hypothesis.empty()) {
        warnings.push_back("W_BLOCK0: hypothesis=" + p.selected_block0_hypothesis +
                           ", attempts=" + std::to_string(p.block0_attempt_count));
    }
    if (p.block0_selected_offset > 0U || !p.block0_selected_mode_source.empty()) {
        warnings.push_back("W_BLOCK0_META: offset=" + std::to_string(p.block0_selected_offset) +
                           ", mode-source=" + p.block0_selected_mode_source);
    }
    if (p.lzma_decode_attempted || !p.lzma_decode_variant.empty()) {
        warnings.push_back("W_LZMA: attempted=" + std::string(p.lzma_decode_attempted ? "true" : "false") +
                           ", variant=" + p.lzma_decode_variant);
    }
    if (!p.recon_failure_detail_code.empty()) {
        warnings.push_back("W_RECON_DETAIL: " + p.recon_failure_detail_code);
    }
    if (!p.object_table_parsed) {
        missing_features.push_back("mesh/material object discovery");
    } else if (mesh_count == 0U) {
        missing_features.push_back("mesh object discovery");
    } else {
        missing_features.push_back("mesh/material payload extraction");
    }

    std::string compat_level = "partial";
    if (p.probe_stage == "complete" && p.object_table_parsed) {
        compat_level = "full";
    }
    if (!p.metadata_parsed) {
        compat_level = "failed";
    }
    std::string primary_error_code = p.probe_primary_error.empty() ? "NONE" : p.probe_primary_error;
    if (p.probe_stage == "complete" && p.object_table_parsed) {
        primary_error_code = "NONE";
    }
    std::string mesh_extract_stage = "mesh-extract-not-attempted";
    if (!p.object_table_parsed) {
        mesh_extract_stage = "object-table-unavailable";
    } else if (mesh_count == 0U) {
        mesh_extract_stage = "object-table-ready-no-mesh-stub-payload";
    } else {
        mesh_extract_stage = "mesh-objects-discovered-stub-payload";
    }

    const std::string recovery_attempt_profile =
        p.serialized_attempt_count > 0U || p.reconstruction_candidate_count > 1U
            ? "serialized-candidate-scan-v1"
            : "metadata-recon-baseline-v1";

    if (primary_error_code == "NONE") {
        if (p.probe_stage == "complete" && p.object_table_parsed) {
            primary_error_code = "NONE";
        } else if (p.probe_stage == "failed-serialized") {
            primary_error_code = "VSF_SERIALIZED_TABLE_INCOMPLETE";
        } else if (mesh_extract_stage == "object-table-ready-no-mesh-stub-payload") {
            primary_error_code = "VSF_MESH_PAYLOAD_MISSING";
        }
    }

    const bool authored_table_ready =
        p.probe_stage == "complete" &&
        p.object_table_parsed &&
        p.mesh_object_count > 0U;
    const bool can_emit_authored_payload =
        authored_table_ready &&
        p.skinned_mesh_renderer_count > 0U;
    const bool can_emit_object_stub_payload = p.probe_stage == "complete" && p.object_table_parsed;
    const bool can_emit_placeholder_payload = false;
    std::string render_payload_mode = "none";
    std::uint32_t mesh_payload_count = 0U;
    std::uint32_t material_payload_count = 0U;
    std::uint32_t payload_quality_score = 0U;
    std::vector<std::string> topology_flags;
    float skin_binding_coverage = 0.0f;
    std::string payload_route_reason_code = "VSF_ROUTE_NONE";
    if (can_emit_authored_payload) {
        render_payload_mode = "authored_mesh_v1";
        mesh_payload_count = std::min<std::uint32_t>(std::max<std::uint32_t>(1U, mesh_count), 128U);
        material_payload_count = 1U;
        payload_quality_score = 78U;
        topology_flags.push_back("mesh_object_table_ready");
        topology_flags.push_back("skinned_renderer_linked");
        skin_binding_coverage = 1.0f;
        payload_route_reason_code = "VSF_ROUTE_AUTHORED_OBJECT_TABLE_READY";
    } else if (can_emit_object_stub_payload) {
        render_payload_mode = "object_stub_v1";
        std::uint32_t proxy_count = std::max<std::uint32_t>(1U, mesh_count);
        if (proxy_count == 1U && p.mesh_object_count == 0U && p.object_count >= 128U) {
            proxy_count = 7U;
        }
        mesh_payload_count = std::min<std::uint32_t>(proxy_count, 28U);
        material_payload_count = 1U;
        payload_quality_score = authored_table_ready ? 42U : 22U;
        topology_flags.push_back(authored_table_ready ? "mesh_objects_present_payload_pending" : "mesh_objects_missing");
        skin_binding_coverage = (p.skinned_mesh_renderer_count > 0U) ? 0.35f : 0.0f;
        payload_route_reason_code = authored_table_ready
            ? "VSF_ROUTE_OBJECT_STUB_AUTHORED_PENDING"
            : "VSF_ROUTE_OBJECT_STUB_ONLY";
    } else if (can_emit_placeholder_payload) {
        render_payload_mode = "placeholder_quad_v1";
        mesh_payload_count = 1U;
        material_payload_count = 1U;
        payload_quality_score = 8U;
        topology_flags.push_back("placeholder_only");
        skin_binding_coverage = 0.0f;
        payload_route_reason_code = "VSF_ROUTE_PLACEHOLDER_ONLY";
    }
    const std::string serialized_best_candidate_path =
        p.serialized_best_candidate_path.empty() ? "NONE" : p.serialized_best_candidate_path;
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at).count();
    if (can_emit_authored_payload) {
        warnings.push_back("W_RENDER_PAYLOAD: authored mesh contract emitted (loader extraction required).");
        warnings.push_back("W_ROUTE: code=" + payload_route_reason_code);
    } else if (can_emit_object_stub_payload) {
        warnings.push_back("W_RENDER_PAYLOAD: object stub payload emitted (authored extraction pending).");
        warnings.push_back("W_ROUTE: code=" + payload_route_reason_code);
        missing_features.push_back("authored mesh payload extraction");
    }

    std::cout << "{"
              << "\"status\":\"ok\","
              << "\"schema_version\":6,"
              << "\"extractor_version\":\"inhouse-sidecar-v6\","
              << "\"display_name\":\"" << EscapeJson(fs::path(path).stem().string()) << "\","
              << "\"compat_level\":\"" << compat_level << "\","
              << "\"probe_stage\":\"" << EscapeJson(p.probe_stage) << "\","
              << "\"primary_error_code\":\"" << EscapeJson(primary_error_code) << "\","
              << "\"recovery_attempt_profile\":\"" << EscapeJson(recovery_attempt_profile) << "\","
              << "\"mesh_extract_stage\":\"" << EscapeJson(mesh_extract_stage) << "\","
              << "\"timing_ms\":" << elapsed_ms << ","
              << "\"object_table_parsed\":" << (p.object_table_parsed ? "true" : "false") << ","
              << "\"mesh_count\":" << mesh_count << ","
              << "\"material_count\":" << material_count << ","
              << "\"render_payload_mode\":\"" << render_payload_mode << "\","
              << "\"mesh_payload_count\":" << mesh_payload_count << ","
              << "\"material_payload_count\":" << material_payload_count << ","
              << "\"payload_quality_score\":" << payload_quality_score << ","
              << "\"skin_binding_coverage\":" << skin_binding_coverage << ","
              << "\"payload_route_reason_code\":\"" << EscapeJson(payload_route_reason_code) << "\","
              << "\"topology_flags\":" << JoinJsonStringArray(topology_flags) << ","
              << "\"selected_block_layout\":\"" << EscapeJson(p.selected_block_layout) << "\","
              << "\"selected_block0_hypothesis\":\"" << EscapeJson(p.selected_block0_hypothesis) << "\","
              << "\"block0_attempt_count\":" << p.block0_attempt_count << ","
              << "\"block0_mode_rank\":" << p.block0_mode_rank << ","
              << "\"block0_selected_offset\":" << p.block0_selected_offset << ","
              << "\"block0_selected_mode_source\":\"" << EscapeJson(p.block0_selected_mode_source) << "\","
              << "\"lzma_decode_attempted\":" << (p.lzma_decode_attempted ? "true" : "false") << ","
              << "\"lzma_decode_variant\":\"" << EscapeJson(p.lzma_decode_variant) << "\","
              << "\"selected_offset_family\":\"" << EscapeJson(p.selected_offset_family) << "\","
              << "\"reconstruction_summary\":\"" << EscapeJson(p.reconstruction_failure_summary_code) << "\","
              << "\"recon_failure_detail_code\":\"" << EscapeJson(p.recon_failure_detail_code) << "\","
              << "\"reconstruction_candidate_count\":" << p.reconstruction_candidate_count << ","
              << "\"best_candidate_score\":" << p.best_candidate_score << ","
              << "\"serialized_candidate_count\":" << p.serialized_candidate_count << ","
              << "\"serialized_attempt_count\":" << p.serialized_attempt_count << ","
              << "\"serialized_best_candidate_path\":\"" << EscapeJson(serialized_best_candidate_path) << "\","
              << "\"serialized_parse_path\":\"" << EscapeJson(p.serialized_parse_path) << "\","
              << "\"serialized_best_candidate_score\":" << p.serialized_best_candidate_score << ","
              << "\"major_types_found\":\"" << EscapeJson(p.major_types_found) << "\","
              << "\"skinned_mesh_renderer_count\":" << p.skinned_mesh_renderer_count << ","
              << "\"serialized_detail_error_code\":\"" << EscapeJson(p.serialized_detail_error_code) << "\","
              << "\"serialized_last_failure_offset\":" << p.serialized_last_failure_offset << ","
              << "\"serialized_last_failure_window_size\":" << p.serialized_last_failure_window_size << ","
              << "\"serialized_last_failure_code\":\"" << EscapeJson(p.serialized_last_failure_code) << "\","
              << "\"failed_block_read_offset\":" << p.failed_block_read_offset << ","
              << "\"failed_block_compressed_size\":" << p.failed_block_compressed_size << ","
              << "\"failed_block_uncompressed_size\":" << p.failed_block_uncompressed_size << ","
              << "\"warnings\":" << JoinJsonStringArray(warnings) << ","
              << "\"missing_features\":" << JoinJsonStringArray(missing_features)
              << "}\n";
    return 0;
}
