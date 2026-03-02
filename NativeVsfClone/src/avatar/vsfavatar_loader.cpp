#include "vsfavatar_loader.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace vsfclone::avatar {

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static std::string EscapeCmdArg(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2U + 4U);
    out.push_back('"');
    for (const auto c : s) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

static core::Result<std::string> RunSidecar(const std::string& sidecar_path, const std::string& avatar_path) {
#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
        return core::Result<std::string>::Fail("CreatePipe failed");
    }
    if (!SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return core::Result<std::string>::Fail("SetHandleInformation failed");
    }

    STARTUPINFOA si {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi {};
    std::string cmd = EscapeCmdArg(sidecar_path) + " " + EscapeCmdArg(avatar_path);
    std::vector<char> cmd_mutable(cmd.begin(), cmd.end());
    cmd_mutable.push_back('\0');

    const BOOL created = CreateProcessA(
        nullptr,
        cmd_mutable.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi);

    CloseHandle(write_pipe);
    if (!created) {
        CloseHandle(read_pipe);
        return core::Result<std::string>::Fail("CreateProcess failed");
    }

    std::string output;
    char buffer[1024];
    DWORD read = 0;
    while (ReadFile(read_pipe, buffer, static_cast<DWORD>(sizeof(buffer)), &read, nullptr) && read > 0) {
        output.append(buffer, buffer + read);
    }
    CloseHandle(read_pipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (exit_code != 0) {
        return core::Result<std::string>::Fail(output.empty() ? "sidecar process failed" : output);
    }
    return core::Result<std::string>::Ok(output);
#else
    (void)sidecar_path;
    (void)avatar_path;
    return core::Result<std::string>::Fail("sidecar mode is only supported on Windows");
#endif
}

static std::string GetJsonString(const std::string& json, const std::string& key) {
    const auto needle = "\"" + key + "\"";
    const auto key_pos = json.find(needle);
    if (key_pos == std::string::npos) {
        return {};
    }
    const auto colon = json.find(':', key_pos + needle.size());
    if (colon == std::string::npos) {
        return {};
    }
    const auto quote_begin = json.find('"', colon + 1U);
    if (quote_begin == std::string::npos) {
        return {};
    }
    std::string out;
    for (std::size_t i = quote_begin + 1U; i < json.size(); ++i) {
        const auto c = json[i];
        if (c == '\\') {
            if (i + 1U < json.size()) {
                out.push_back(json[i + 1U]);
                ++i;
                continue;
            }
            break;
        }
        if (c == '"') {
            return out;
        }
        out.push_back(c);
    }
    return {};
}

static std::uint32_t GetJsonU32(const std::string& json, const std::string& key, std::uint32_t fallback = 0U) {
    const auto needle = "\"" + key + "\"";
    const auto key_pos = json.find(needle);
    if (key_pos == std::string::npos) {
        return fallback;
    }
    const auto colon = json.find(':', key_pos + needle.size());
    if (colon == std::string::npos) {
        return fallback;
    }
    std::size_t i = colon + 1U;
    while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i])) != 0) {
        ++i;
    }
    std::size_t end = i;
    while (end < json.size() && std::isdigit(static_cast<unsigned char>(json[end])) != 0) {
        ++end;
    }
    if (end == i) {
        return fallback;
    }
    return static_cast<std::uint32_t>(std::strtoul(json.substr(i, end - i).c_str(), nullptr, 10));
}

bool VsfAvatarLoader::CanLoadPath(const std::string& path) const {
    const auto ext = ToLower(fs::path(path).extension().string());
    return ext == ".vsfavatar";
}

core::Result<AvatarPackage> VsfAvatarLoader::Load(const std::string& path) const {
    std::string mode = "sidecar";
    if (const char* env = std::getenv("VSF_PARSER_MODE")) {
        mode = ToLower(env);
    }

    if (mode == "inhouse") {
        return LoadInHouse(path);
    }

    auto sidecar = LoadViaSidecar(path);
    if (sidecar.ok) {
        return sidecar;
    }

    if (mode == "sidecar-strict") {
        return sidecar;
    }

    auto fallback = LoadInHouse(path);
    if (!fallback.ok) {
        return fallback;
    }
    fallback.value.warnings.push_back("sidecar fallback: " + sidecar.error);
    fallback.value.warnings.push_back("parser mode=inhouse (fallback)");
    return fallback;
}

core::Result<AvatarPackage> VsfAvatarLoader::LoadViaSidecar(const std::string& path) const {
    std::string sidecar_path = ".\\build\\Release\\vsfavatar_sidecar.exe";
    if (const char* env = std::getenv("VSF_SIDECAR_PATH")) {
        sidecar_path = env;
    }

    const auto ran = RunSidecar(sidecar_path, path);
    if (!ran.ok) {
        return core::Result<AvatarPackage>::Fail("sidecar failed: " + ran.error);
    }
    const std::string output = ran.value;

    const auto status = GetJsonString(output, "status");
    if (status != "ok") {
        const auto err = GetJsonString(output, "error");
        return core::Result<AvatarPackage>::Fail(err.empty() ? "sidecar returned non-ok status" : err);
    }

    AvatarPackage pkg;
    pkg.source_type = AvatarSourceType::VsfAvatar;
    pkg.compat_level = AvatarCompatLevel::Partial;
    pkg.source_path = path;
    pkg.display_name = GetJsonString(output, "display_name");
    if (pkg.display_name.empty()) {
        pkg.display_name = fs::path(path).stem().string();
    }

    const auto mesh_count = GetJsonU32(output, "mesh_count");
    const auto material_count = GetJsonU32(output, "material_count");
    for (std::uint32_t i = 0; i < mesh_count; ++i) {
        pkg.meshes.push_back({"Mesh_" + std::to_string(i), 0, 0});
    }
    for (std::uint32_t i = 0; i < material_count; ++i) {
        pkg.materials.push_back({"Material_" + std::to_string(i), "UnityShader (sidecar)"}); 
    }
    if (pkg.materials.empty()) {
        pkg.materials.push_back({"Default", "MToon (placeholder)"});
    }

    const auto last_warning = GetJsonString(output, "last_warning");
    const auto last_missing = GetJsonString(output, "last_missing_feature");
    pkg.warnings.push_back("parser mode=sidecar");
    if (!last_warning.empty()) {
        pkg.warnings.push_back(last_warning);
    }
    if (!last_missing.empty()) {
        pkg.missing_features.push_back(last_missing);
    }
    if (mesh_count == 0U && material_count == 0U) {
        pkg.missing_features.push_back("mesh/material object discovery");
    }
    return core::Result<AvatarPackage>::Ok(pkg);
}

core::Result<AvatarPackage> VsfAvatarLoader::LoadInHouse(const std::string& path) const {
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
    pkg.warnings.push_back("parser mode=inhouse");

    return core::Result<AvatarPackage>::Ok(pkg);
}

}  // namespace vsfclone::avatar
