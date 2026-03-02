#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

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
              << "\"error\":\"" << EscapeJson(error) << "\""
              << "}\n";
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
    const auto missing = (!p.object_table_parsed || mesh_count == 0U)
                             ? "mesh/material object discovery"
                             : "mesh/material payload extraction";

    std::ostringstream warning;
    if (!p.metadata_error.empty()) {
        warning << p.metadata_error;
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

    std::cout << "{"
              << "\"status\":\"ok\","
              << "\"schema_version\":1,"
              << "\"extractor_version\":\"inhouse-sidecar-v1\","
              << "\"display_name\":\"" << EscapeJson(fs::path(path).stem().string()) << "\","
              << "\"object_table_parsed\":" << (p.object_table_parsed ? "true" : "false") << ","
              << "\"mesh_count\":" << mesh_count << ","
              << "\"material_count\":" << material_count << ","
              << "\"warning_count\":1,"
              << "\"missing_feature_count\":1,"
              << "\"last_warning\":\"" << EscapeJson(warning.str()) << "\","
              << "\"last_missing_feature\":\"" << EscapeJson(missing) << "\""
              << "}\n";
    return 0;
}

