#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "vsfclone/vsf/unityfs_reader.h"

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
              << "\"schema_version\":3,"
              << "\"extractor_version\":\"inhouse-sidecar-v3\","
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
    if (argc < 2) {
        PrintErrorJson("usage: vsfavatar_sidecar <path_to_vsfavatar>");
        return 1;
    }

    const std::string path = argv[1];
    if (!fs::exists(path)) {
        PrintErrorJson("file not found");
        return 2;
    }

    vsfclone::vsf::UnityFsReader reader;
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
              << ", expected=" << p.failed_block_expected_size
              << ", code=" << p.failed_block_error_code;
        warnings.push_back("W_RECON: " + block.str());
    }
    if (!p.selected_block0_hypothesis.empty()) {
        warnings.push_back("W_BLOCK0: hypothesis=" + p.selected_block0_hypothesis +
                           ", attempts=" + std::to_string(p.block0_attempt_count));
    }
    if (!p.object_table_parsed || mesh_count == 0U) {
        missing_features.push_back("mesh/material object discovery");
    } else {
        missing_features.push_back("mesh/material payload extraction");
    }

    std::string compat_level = "partial";
    if (p.object_table_parsed && mesh_count > 0U) {
        compat_level = "full";
    }
    if (!p.metadata_parsed) {
        compat_level = "failed";
    }
    const std::string primary_error_code = p.probe_primary_error.empty() ? "NONE" : p.probe_primary_error;

    std::cout << "{"
              << "\"status\":\"ok\","
              << "\"schema_version\":3,"
              << "\"extractor_version\":\"inhouse-sidecar-v3\","
              << "\"display_name\":\"" << EscapeJson(fs::path(path).stem().string()) << "\","
              << "\"compat_level\":\"" << compat_level << "\","
              << "\"probe_stage\":\"" << EscapeJson(p.probe_stage) << "\","
              << "\"primary_error_code\":\"" << EscapeJson(primary_error_code) << "\","
              << "\"object_table_parsed\":" << (p.object_table_parsed ? "true" : "false") << ","
              << "\"mesh_count\":" << mesh_count << ","
              << "\"material_count\":" << material_count << ","
              << "\"selected_block_layout\":\"" << EscapeJson(p.selected_block_layout) << "\","
              << "\"selected_block0_hypothesis\":\"" << EscapeJson(p.selected_block0_hypothesis) << "\","
              << "\"block0_attempt_count\":" << p.block0_attempt_count << ","
              << "\"selected_offset_family\":\"" << EscapeJson(p.selected_offset_family) << "\","
              << "\"reconstruction_summary\":\"" << EscapeJson(p.reconstruction_failure_summary_code) << "\","
              << "\"warnings\":" << JoinJsonStringArray(warnings) << ","
              << "\"missing_features\":" << JoinJsonStringArray(missing_features)
              << "}\n";
    return 0;
}
