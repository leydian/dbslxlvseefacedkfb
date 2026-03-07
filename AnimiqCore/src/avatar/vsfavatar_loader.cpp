#include "vsfavatar_loader.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace animiq::avatar {

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

static std::uint32_t GetEnvU32(const char* name, std::uint32_t fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    const auto parsed = std::strtoul(value, nullptr, 10);
    if (parsed == 0UL) {
        return fallback;
    }
    return static_cast<std::uint32_t>(parsed);
}

static bool EnvFlagEnabled(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return false;
    }
    const std::string lowered = ToLower(value);
    return lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on";
}

static core::Result<std::string> RunSidecar(
    const std::string& sidecar_path,
    const std::string& avatar_path,
    std::uint32_t timeout_ms) {
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
    ZeroMemory(&pi, sizeof(pi));

    // Create a Job Object to ensure the sidecar process is terminated if the host process exits unexpectedly.
    HANDLE hJob = CreateJobObject(nullptr, nullptr);
    if (hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {0};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }

    const BOOL created = CreateProcessA(
        nullptr,
        cmd_mutable.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED, // Create suspended to assign to job object
        nullptr,
        nullptr,
        &si,
        &pi);

    CloseHandle(write_pipe);
    if (!created) {
        if (hJob) CloseHandle(hJob);
        CloseHandle(read_pipe);
        return core::Result<std::string>::Fail("CreateProcess failed");
    }

    if (hJob) {
        AssignProcessToJobObject(hJob, pi.hProcess);
    }
    ResumeThread(pi.hThread);

    std::string output;
    char buffer[1024];
    const DWORD start_tick = GetTickCount();
    bool process_exited = false;


    for (;;) {
        DWORD available = 0;
        if (PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &available, nullptr) != 0 && available > 0) {
            while (available > 0) {
                DWORD read = 0;
                const DWORD to_read = (available > sizeof(buffer)) ? static_cast<DWORD>(sizeof(buffer)) : available;
                if (ReadFile(read_pipe, buffer, to_read, &read, nullptr) == 0 || read == 0) {
                    break;
                }
                output.append(buffer, buffer + read);
                available -= read;
            }
        }

        const DWORD wait_result = WaitForSingleObject(pi.hProcess, 30);
        if (wait_result == WAIT_OBJECT_0) {
            process_exited = true;
            break;
        }
        if (wait_result != WAIT_TIMEOUT) {
            CloseHandle(read_pipe);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return core::Result<std::string>::Fail("SIDECAR_EXEC_FAILED: wait failed");
        }
        if (GetTickCount() - start_tick > timeout_ms) {
            TerminateProcess(pi.hProcess, 124);
            CloseHandle(read_pipe);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return core::Result<std::string>::Fail("SIDECAR_TIMEOUT: process timed out");
        }
    }

    if (process_exited) {
        // Read any remaining data after process exit
        for (;;) {
            DWORD available = 0;
            if (PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &available, nullptr) == 0 || available == 0) {
                break;
            }
            DWORD read = 0;
            if (ReadFile(read_pipe, buffer, (available > sizeof(buffer)) ? sizeof(buffer) : available, &read, nullptr) == 0 || read == 0) {
                break;
            }
            output.append(buffer, buffer + read);
        }
    }
    CloseHandle(read_pipe);

    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (exit_code != 0) {
        if (!output.empty()) {
            return core::Result<std::string>::Fail("SIDECAR_RUNTIME_ERROR: " + output);
        }
        return core::Result<std::string>::Fail("SIDECAR_RUNTIME_ERROR: sidecar process failed");
    }
    return core::Result<std::string>::Ok(output);
#else
    (void)sidecar_path;
    (void)avatar_path;
    (void)timeout_ms;
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

static std::uint64_t GetJsonU64(const std::string& json, const std::string& key, std::uint64_t fallback = 0ULL) {
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
    return static_cast<std::uint64_t>(std::strtoull(json.substr(i, end - i).c_str(), nullptr, 10));
}

static bool HasJsonKey(const std::string& json, const std::string& key) {
    return json.find("\"" + key + "\"") != std::string::npos;
}

static bool TryGetJsonBool(const std::string& json, const std::string& key, bool* out_value) {
    if (out_value == nullptr) {
        return false;
    }
    const auto needle = "\"" + key + "\"";
    const auto key_pos = json.find(needle);
    if (key_pos == std::string::npos) {
        return false;
    }
    const auto colon = json.find(':', key_pos + needle.size());
    if (colon == std::string::npos) {
        return false;
    }
    std::size_t i = colon + 1U;
    while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i])) != 0) {
        ++i;
    }
    if (json.compare(i, 4U, "true") == 0) {
        *out_value = true;
        return true;
    }
    if (json.compare(i, 5U, "false") == 0) {
        *out_value = false;
        return true;
    }
    return false;
}

static std::vector<std::string> GetJsonStringArray(const std::string& json, const std::string& key) {
    std::vector<std::string> values;
    const auto needle = "\"" + key + "\"";
    const auto key_pos = json.find(needle);
    if (key_pos == std::string::npos) {
        return values;
    }
    const auto colon = json.find(':', key_pos + needle.size());
    if (colon == std::string::npos) {
        return values;
    }
    const auto begin = json.find('[', colon + 1U);
    if (begin == std::string::npos) {
        return values;
    }
    const auto end = json.find(']', begin + 1U);
    if (end == std::string::npos) {
        return values;
    }

    std::size_t i = begin + 1U;
    while (i < end) {
        while (i < end && (std::isspace(static_cast<unsigned char>(json[i])) != 0 || json[i] == ',')) {
            ++i;
        }
        if (i >= end || json[i] != '"') {
            ++i;
            continue;
        }
        std::string item;
        ++i;
        while (i < end) {
            const auto c = json[i];
            if (c == '\\') {
                if (i + 1U < end) {
                    item.push_back(json[i + 1U]);
                    i += 2U;
                    continue;
                }
                break;
            }
            if (c == '"') {
                ++i;
                break;
            }
            item.push_back(c);
            ++i;
        }
        if (!item.empty()) {
            values.push_back(item);
        }
    }
    return values;
}

static core::Result<bool> ValidateSidecarSchema(const std::string& output) {
    const auto schema_version = GetJsonU32(output, "schema_version");
    if (schema_version != 2U && schema_version != 3U && schema_version != 4U && schema_version != 5U) {
        return core::Result<bool>::Fail("SCHEMA_INVALID: schema_version must be 2, 3, 4, or 5");
    }
    const auto status = GetJsonString(output, "status");
    if (status.empty()) {
        return core::Result<bool>::Fail("SCHEMA_INVALID: missing status");
    }
    if (status == "ok" && GetJsonString(output, "display_name").empty()) {
        return core::Result<bool>::Fail("SCHEMA_INVALID: missing display_name");
    }
    if (status == "ok" && GetJsonString(output, "extractor_version").empty()) {
        return core::Result<bool>::Fail("SCHEMA_INVALID: missing extractor_version");
    }
    if (status == "ok" && GetJsonString(output, "compat_level").empty()) {
        return core::Result<bool>::Fail("SCHEMA_INVALID: missing compat_level");
    }
    bool object_table_parsed = false;
    if (status == "ok" && !TryGetJsonBool(output, "object_table_parsed", &object_table_parsed)) {
        return core::Result<bool>::Fail("SCHEMA_INVALID: missing object_table_parsed");
    }
    if (status == "ok" && GetJsonString(output, "primary_error_code").empty()) {
        return core::Result<bool>::Fail("SCHEMA_INVALID: missing primary_error_code");
    }
    if (status == "ok" && !HasJsonKey(output, "warnings")) {
        return core::Result<bool>::Fail("SCHEMA_INVALID: missing warnings");
    }
    if (status == "ok" && !HasJsonKey(output, "missing_features")) {
        return core::Result<bool>::Fail("SCHEMA_INVALID: missing missing_features");
    }
    if (status == "ok" && GetJsonString(output, "render_payload_mode").empty()) {
        return core::Result<bool>::Fail("SCHEMA_INVALID: missing render_payload_mode");
    }
    if (status == "ok" && !HasJsonKey(output, "mesh_payload_count")) {
        return core::Result<bool>::Fail("SCHEMA_INVALID: missing mesh_payload_count");
    }
    if (status == "ok" && !HasJsonKey(output, "material_payload_count")) {
        return core::Result<bool>::Fail("SCHEMA_INVALID: missing material_payload_count");
    }
    if (status == "ok" && schema_version >= 5U) {
        if (GetJsonString(output, "recovery_attempt_profile").empty()) {
            return core::Result<bool>::Fail("SCHEMA_INVALID: missing recovery_attempt_profile");
        }
        if (GetJsonString(output, "mesh_extract_stage").empty()) {
            return core::Result<bool>::Fail("SCHEMA_INVALID: missing mesh_extract_stage");
        }
        if (!HasJsonKey(output, "timing_ms")) {
            return core::Result<bool>::Fail("SCHEMA_INVALID: missing timing_ms");
        }
    }
    return core::Result<bool>::Ok(true);
}

static AvatarCompatLevel ParseCompatLevel(const std::string& value) {
    const auto lowered = ToLower(value);
    if (lowered == "full") {
        return AvatarCompatLevel::Full;
    }
    if (lowered == "failed") {
        return AvatarCompatLevel::Failed;
    }
    if (lowered == "partial") {
        return AvatarCompatLevel::Partial;
    }
    return AvatarCompatLevel::Unknown;
}

static void AppendFloat(std::vector<std::uint8_t>* out, float value) {
    if (out == nullptr) {
        return;
    }
    std::uint8_t bytes[sizeof(float)];
    std::memcpy(bytes, &value, sizeof(float));
    out->insert(out->end(), bytes, bytes + sizeof(float));
}

static MeshRenderPayload BuildPlaceholderQuadPayload() {
    MeshRenderPayload payload;
    payload.name = "VSF_PLACEHOLDER_QUAD";
    payload.vertex_stride = 12U;
    payload.material_index = 0;
    payload.indices = {0U, 1U, 2U, 2U, 3U, 0U};
    payload.vertex_blob.reserve(4U * 12U);

    AppendFloat(&payload.vertex_blob, -0.5f);
    AppendFloat(&payload.vertex_blob, -0.8f);
    AppendFloat(&payload.vertex_blob, 0.0f);

    AppendFloat(&payload.vertex_blob, 0.5f);
    AppendFloat(&payload.vertex_blob, -0.8f);
    AppendFloat(&payload.vertex_blob, 0.0f);

    AppendFloat(&payload.vertex_blob, 0.5f);
    AppendFloat(&payload.vertex_blob, 0.8f);
    AppendFloat(&payload.vertex_blob, 0.0f);

    AppendFloat(&payload.vertex_blob, -0.5f);
    AppendFloat(&payload.vertex_blob, 0.8f);
    AppendFloat(&payload.vertex_blob, 0.0f);
    return payload;
}

static MeshRenderPayload BuildObjectStubPayload(std::uint32_t index) {
    MeshRenderPayload payload;
    payload.name = "VSF_OBJECT_STUB_" + std::to_string(index);
    payload.vertex_stride = 12U;
    payload.material_index = 0;
    payload.indices = {0U, 1U, 2U};
    payload.vertex_blob.reserve(3U * 12U);
    const float x_offset = static_cast<float>(index) * 0.08f;

    AppendFloat(&payload.vertex_blob, -0.2f + x_offset);
    AppendFloat(&payload.vertex_blob, -0.2f);
    AppendFloat(&payload.vertex_blob, 0.0f);

    AppendFloat(&payload.vertex_blob, 0.2f + x_offset);
    AppendFloat(&payload.vertex_blob, -0.2f);
    AppendFloat(&payload.vertex_blob, 0.0f);

    AppendFloat(&payload.vertex_blob, 0.0f + x_offset);
    AppendFloat(&payload.vertex_blob, 0.25f);
    AppendFloat(&payload.vertex_blob, 0.0f);
    return payload;
}

bool VsfAvatarLoader::CanLoadPath(const std::string& path) const {
    const auto ext = ToLower(fs::path(path).extension().string());
    return ext == ".vsfavatar";
}

bool VsfAvatarLoader::CanLoadBytes(const std::vector<std::uint8_t>& head) const {
    return head.size() >= 7U && head[0] == 'U' && head[1] == 'n' && head[2] == 'i' && head[3] == 't' &&
           head[4] == 'y' && head[5] == 'F' && head[6] == 'S';
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
    fallback.value.warnings.push_back("W_FALLBACK: sidecar fallback: " + sidecar.error);
    fallback.value.warnings.push_back("W_MODE: parser mode=inhouse (fallback)");
    return fallback;
}

core::Result<AvatarPackage> VsfAvatarLoader::LoadViaSidecar(const std::string& path) const {
    std::string sidecar_path = "vsfavatar_sidecar.exe";
#if defined(_WIN32)
    char module_path[MAX_PATH];
    if (GetModuleFileNameA(GetModuleHandleA("nativecore.dll"), module_path, MAX_PATH)) {
        fs::path p(module_path);
        std::string local_sidecar = (p.parent_path() / "vsfavatar_sidecar.exe").string();
        if (fs::exists(local_sidecar)) {
            sidecar_path = local_sidecar;
        }
    }
#endif
    if (const char* env = std::getenv("VSF_SIDECAR_PATH")) {
        sidecar_path = env;
    }
    const auto env_timeout = GetEnvU32("VSF_SIDECAR_TIMEOUT_MS", 0U);
    std::uint32_t timeout_ms = 15000U;
    if (env_timeout > 0U) {
        timeout_ms = env_timeout;
    } else {
        // Dynamic timeout based on file size: 15s base + 500ms per MB, capped at 60s.
        try {
            const auto size = fs::file_size(path);
            const auto extra_ms = static_cast<std::uint32_t>((size / (1024ULL * 1024ULL)) * 500ULL);
            const std::uint32_t candidate_timeout = 15000U + extra_ms;
            timeout_ms = (candidate_timeout > 60000U) ? 60000U : candidate_timeout;
        } catch (...) {
            // Fallback to default 15s if file_size fails
        }
    }

    const auto ran = RunSidecar(sidecar_path, path, timeout_ms);
    if (!ran.ok) {
        return core::Result<AvatarPackage>::Fail(ran.error);
    }
    const std::string output = ran.value;
    const auto schema = ValidateSidecarSchema(output);
    if (!schema.ok) {
        return core::Result<AvatarPackage>::Fail(schema.error);
    }

    const auto status = GetJsonString(output, "status");
    if (status != "ok") {
        const auto err_code = GetJsonString(output, "error_code");
        const auto err = GetJsonString(output, "error");
        const auto err_message = GetJsonString(output, "error_message");
        if (!err_code.empty() || !err_message.empty()) {
            return core::Result<AvatarPackage>::Fail(
                "SIDECAR_RUNTIME_ERROR: " + err_code + " " + err_message);
        }
        return core::Result<AvatarPackage>::Fail(
            err.empty() ? "SIDECAR_RUNTIME_ERROR: sidecar returned non-ok status" : err);
    }

    AvatarPackage pkg;
    pkg.source_type = AvatarSourceType::VsfAvatar;
    pkg.compat_level = ParseCompatLevel(GetJsonString(output, "compat_level"));
    if (pkg.compat_level == AvatarCompatLevel::Unknown) {
        pkg.compat_level = AvatarCompatLevel::Partial;
    }
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

    const auto sidecar_mesh_payload_count = GetJsonU32(output, "mesh_payload_count");
    const auto sidecar_material_payload_count = GetJsonU32(output, "material_payload_count");
    const auto render_payload_mode = GetJsonString(output, "render_payload_mode");
    const bool allow_placeholder_render = EnvFlagEnabled("VSF_ALLOW_VSF_PLACEHOLDER_RENDER");
    if (render_payload_mode != "none" && sidecar_mesh_payload_count == 0U) {
        return core::Result<AvatarPackage>::Fail("SCHEMA_INVALID: render_payload_mode requires mesh_payload_count > 0");
    }
    if (render_payload_mode == "placeholder_quad_v1" && sidecar_mesh_payload_count > 0U) {
        if (allow_placeholder_render) {
            pkg.mesh_payloads.push_back(BuildPlaceholderQuadPayload());
            MaterialRenderPayload mat {};
            mat.name = "VSF_PLACEHOLDER_MAT";
            mat.shader_name = "Unlit/Color";
            mat.shader_variant = "placeholder";
            mat.alpha_mode = "OPAQUE";
            mat.double_sided = true;
            if (sidecar_material_payload_count > 0U) {
                pkg.material_payloads.push_back(mat);
            }
            pkg.warnings.push_back("W_RENDER_PAYLOAD: placeholder quad payload applied from sidecar contract.");
            pkg.warning_codes.push_back("VSF_PLACEHOLDER_RENDER_PAYLOAD");
        } else {
            pkg.compat_level = AvatarCompatLevel::Failed;
            pkg.primary_error_code = "VSF_PLACEHOLDER_OUTPUT_BLOCKED";
            pkg.warnings.push_back("E_RENDER_PAYLOAD: placeholder payload blocked for output path.");
            pkg.warning_codes.push_back("VSF_PLACEHOLDER_OUTPUT_BLOCKED");
            pkg.missing_features.push_back("authored mesh payload extraction");
        }
    } else if (render_payload_mode == "object_stub_v1" && sidecar_mesh_payload_count > 0U) {
        for (std::uint32_t i = 0; i < sidecar_mesh_payload_count; ++i) {
            pkg.mesh_payloads.push_back(BuildObjectStubPayload(i));
        }
        if (sidecar_material_payload_count > 0U) {
            MaterialRenderPayload mat {};
            mat.name = "VSF_OBJECT_STUB_MAT";
            mat.shader_name = "Unlit/Color";
            mat.shader_variant = "stub";
            mat.alpha_mode = "OPAQUE";
            mat.double_sided = true;
            pkg.material_payloads.push_back(mat);
        }
        pkg.warnings.push_back("W_RENDER_PAYLOAD: object stub payload applied from sidecar contract.");
        pkg.warning_codes.push_back("VSF_OBJECT_STUB_RENDER_PAYLOAD");
    }

    pkg.warnings.push_back("W_MODE: parser mode=sidecar");
    const auto probe_stage = GetJsonString(output, "probe_stage");
    pkg.parser_stage = probe_stage.empty() ? "unknown" : probe_stage;
    if (!probe_stage.empty()) {
        pkg.warnings.push_back("W_STAGE: " + probe_stage);
    }
    const auto primary_error_code = GetJsonString(output, "primary_error_code");
    if (pkg.primary_error_code.empty() || pkg.primary_error_code == "NONE") {
        pkg.primary_error_code = primary_error_code.empty() ? "NONE" : primary_error_code;
    }
    if (!primary_error_code.empty() && primary_error_code != "NONE") {
        pkg.warnings.push_back("W_PRIMARY: " + primary_error_code);
    }
    const auto block_layout = GetJsonString(output, "selected_block_layout");
    if (!block_layout.empty()) {
        pkg.warnings.push_back("W_LAYOUT: " + block_layout);
    }
    const auto offset_family = GetJsonString(output, "selected_offset_family");
    if (!offset_family.empty()) {
        pkg.warnings.push_back("W_OFFSET: " + offset_family);
    }
    const auto recon_summary = GetJsonString(output, "reconstruction_summary");
    if (!recon_summary.empty()) {
        pkg.warnings.push_back("W_RECON_SUMMARY: " + recon_summary);
    }
    const auto recon_candidate_count = GetJsonU32(output, "reconstruction_candidate_count");
    const auto best_candidate_score = GetJsonU32(output, "best_candidate_score");
    if (recon_candidate_count > 0U) {
        pkg.warnings.push_back("W_RECON_META: candidates=" + std::to_string(recon_candidate_count) +
                               ", best-score=" + std::to_string(best_candidate_score));
    }
    const auto serialized_candidate_count = GetJsonU32(output, "serialized_candidate_count");
    const auto serialized_attempt_count = GetJsonU32(output, "serialized_attempt_count");
    const auto serialized_best_candidate_score = GetJsonU32(output, "serialized_best_candidate_score");
    if (serialized_candidate_count > 0U || serialized_attempt_count > 0U) {
        pkg.warnings.push_back("W_SERIALIZED_META: candidates=" + std::to_string(serialized_candidate_count) +
                               ", attempts=" + std::to_string(serialized_attempt_count) +
                               ", best-score=" + std::to_string(serialized_best_candidate_score));
    }
    const auto serialized_best_candidate_path = GetJsonString(output, "serialized_best_candidate_path");
    if (!serialized_best_candidate_path.empty()) {
        pkg.warnings.push_back("W_SERIALIZED_PATH: " + serialized_best_candidate_path);
    }
    const auto recovery_attempt_profile = GetJsonString(output, "recovery_attempt_profile");
    if (!recovery_attempt_profile.empty()) {
        pkg.warnings.push_back("W_RECOVERY_PROFILE: " + recovery_attempt_profile);
    }
    const auto mesh_extract_stage = GetJsonString(output, "mesh_extract_stage");
    if (!mesh_extract_stage.empty()) {
        pkg.warnings.push_back("W_MESH_EXTRACT_STAGE: " + mesh_extract_stage);
    }
    const auto timing_ms = GetJsonU64(output, "timing_ms", 0ULL);
    if (HasJsonKey(output, "timing_ms")) {
        pkg.warnings.push_back("W_TIMING_MS: " + std::to_string(timing_ms));
    }
    const auto serialized_detail_error_code = GetJsonString(output, "serialized_detail_error_code");
    const auto serialized_last_failure_offset = GetJsonU64(output, "serialized_last_failure_offset");
    const auto serialized_last_failure_window_size = GetJsonU64(output, "serialized_last_failure_window_size");
    const auto serialized_last_failure_code = GetJsonString(output, "serialized_last_failure_code");
    if (!serialized_detail_error_code.empty() ||
        serialized_last_failure_offset > 0U ||
        serialized_last_failure_window_size > 0U ||
        !serialized_last_failure_code.empty()) {
        pkg.warnings.push_back("W_SERIALIZED_DETAIL: code=" + serialized_detail_error_code +
                               ", last-offset=" + std::to_string(serialized_last_failure_offset) +
                               ", window=" + std::to_string(serialized_last_failure_window_size) +
                               ", last-code=" + serialized_last_failure_code);
    }
    const auto failed_read_offset = GetJsonU64(output, "failed_block_read_offset");
    const auto failed_csize = GetJsonU32(output, "failed_block_compressed_size");
    const auto failed_usize = GetJsonU32(output, "failed_block_uncompressed_size");
    if (failed_read_offset > 0U || failed_csize > 0U || failed_usize > 0U) {
        pkg.warnings.push_back("W_RECON_FAIL_META: read-offset=" + std::to_string(failed_read_offset) +
                               ", csize=" + std::to_string(failed_csize) +
                               ", usize=" + std::to_string(failed_usize));
    }
    const auto block0_hypothesis = GetJsonString(output, "selected_block0_hypothesis");
    if (!block0_hypothesis.empty()) {
        const auto block0_attempt_count = GetJsonU32(output, "block0_attempt_count");
        pkg.warnings.push_back("W_BLOCK0: hypothesis=" + block0_hypothesis +
                               ", attempts=" + std::to_string(block0_attempt_count));
    }
    const auto block0_selected_offset = GetJsonU64(output, "block0_selected_offset");
    const auto block0_mode_source = GetJsonString(output, "block0_selected_mode_source");
    if (block0_selected_offset > 0U || !block0_mode_source.empty()) {
        pkg.warnings.push_back("W_BLOCK0_META: offset=" + std::to_string(block0_selected_offset) +
                               ", mode-source=" + block0_mode_source);
    }
    const auto block0_mode_rank = GetJsonU32(output, "block0_mode_rank");
    if (block0_mode_rank > 0U) {
        pkg.warnings.push_back("W_BLOCK0_RANK: " + std::to_string(block0_mode_rank));
    }
    bool lzma_decode_attempted = false;
    if (TryGetJsonBool(output, "lzma_decode_attempted", &lzma_decode_attempted) && lzma_decode_attempted) {
        const auto lzma_decode_variant = GetJsonString(output, "lzma_decode_variant");
        pkg.warnings.push_back("W_LZMA: attempted=true, variant=" + lzma_decode_variant);
    }
    const auto recon_failure_detail_code = GetJsonString(output, "recon_failure_detail_code");
    if (!recon_failure_detail_code.empty()) {
        pkg.warnings.push_back("W_RECON_DETAIL: " + recon_failure_detail_code);
    }
    const auto warning_items = GetJsonStringArray(output, "warnings");
    for (const auto& w : warning_items) {
        pkg.warnings.push_back(w);
    }
    const auto missing_items = GetJsonStringArray(output, "missing_features");
    for (const auto& m : missing_items) {
        pkg.missing_features.push_back(m);
    }
    if (pkg.missing_features.empty() && mesh_count == 0U && material_count == 0U) {
        pkg.missing_features.push_back("mesh/material object discovery");
    }
    if (pkg.mesh_payloads.empty() && pkg.compat_level != AvatarCompatLevel::Failed) {
        if (pkg.primary_error_code.empty() || pkg.primary_error_code == "NONE") {
            if (mesh_extract_stage == "mesh-objects-discovered-payload-pending") {
                pkg.primary_error_code = "VSF_MESH_EXTRACT_FAILED";
            } else if (pkg.parser_stage == "failed-serialized") {
                pkg.primary_error_code = "VSF_SERIALIZED_TABLE_INCOMPLETE";
            } else {
                pkg.primary_error_code = "VSF_MESH_PAYLOAD_MISSING";
            }
        }
        pkg.warnings.push_back("W_PRIMARY: " + pkg.primary_error_code);
        if (std::find(pkg.missing_features.begin(), pkg.missing_features.end(), "authored mesh payload extraction") ==
            pkg.missing_features.end()) {
            pkg.missing_features.push_back("authored mesh payload extraction");
        }
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
    pkg.parser_stage = probe.value.probe_stage.empty() ? "unknown" : probe.value.probe_stage;
    pkg.primary_error_code = probe.value.probe_primary_error.empty() ? "NONE" : probe.value.probe_primary_error;
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
    pkg.warnings.push_back("W_HEADER: " + warn.str());
    if (probe.value.metadata_parsed) {
        std::ostringstream meta;
        meta << "metadata parsed: blocks=" << probe.value.block_count
             << ", nodes=" << probe.value.node_count
             << ", block compressed total=" << probe.value.total_block_compressed_size
             << ", block uncompressed total=" << probe.value.total_block_uncompressed_size
             << ", reconstruct attempts=" << probe.value.reconstruction_attempts
             << ", candidate count=" << probe.value.reconstruction_candidate_count
             << ", best score=" << probe.value.best_candidate_score
             << ", serialized candidates=" << probe.value.serialized_candidate_count
             << ", serialized attempts=" << probe.value.serialized_attempt_count
             << ", serialized best score=" << probe.value.serialized_best_candidate_score;
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
        if (!probe.value.selected_block0_hypothesis.empty()) {
            meta << ", block0 hypothesis=" << probe.value.selected_block0_hypothesis
                 << ", block0 attempts=" << probe.value.block0_attempt_count;
            if (probe.value.block0_mode_rank > 0U) {
                meta << ", block0 mode rank=" << probe.value.block0_mode_rank;
            }
        }
        if (probe.value.block0_selected_offset > 0U || !probe.value.block0_selected_mode_source.empty()) {
            meta << ", block0 offset=" << probe.value.block0_selected_offset
                 << ", block0 mode-source=" << probe.value.block0_selected_mode_source;
        }
        if (probe.value.lzma_decode_attempted || !probe.value.lzma_decode_variant.empty()) {
            meta << ", lzma attempted=" << (probe.value.lzma_decode_attempted ? "true" : "false")
                 << ", lzma variant=" << probe.value.lzma_decode_variant;
        }
        if (!probe.value.reconstruction_failure_summary_code.empty()) {
            meta << ", recon summary code=" << probe.value.reconstruction_failure_summary_code;
            if (!probe.value.recon_failure_detail_code.empty()) {
                meta << ", recon detail code=" << probe.value.recon_failure_detail_code;
            }
        }
        if (probe.value.metadata_offset > 0U) {
            meta << ", metadata offset=" << probe.value.metadata_offset;
        }
        if (!probe.value.selected_offset_family.empty()) {
            meta << ", offset family=" << probe.value.selected_offset_family;
        }
        if (probe.value.reconstruction_success_offset > 0U) {
            meta << ", reconstruct success offset=" << probe.value.reconstruction_success_offset;
        }
        if (!probe.value.first_node_path.empty()) {
            meta << ", first node=" << probe.value.first_node_path;
        }
        if (!probe.value.serialized_best_candidate_path.empty()) {
            meta << ", serialized best path=" << probe.value.serialized_best_candidate_path;
        }
        pkg.warnings.push_back("W_META: " + meta.str());
    }
    if (!probe.value.metadata_error.empty()) {
        if (probe.value.metadata_parsed) {
            pkg.warnings.push_back("E_META_SERIALIZED: " + probe.value.metadata_error);
        } else {
            pkg.warnings.push_back("E_META_PARSE: " + probe.value.metadata_error);
        }
        if (!probe.value.metadata_decode_error_code.empty()) {
            pkg.warnings.push_back("E_META_CODE: " + probe.value.metadata_decode_error_code);
        }
        if (!probe.value.failed_block_error_code.empty()) {
            std::ostringstream block;
            block << "data block diagnostic: index=" << probe.value.failed_block_index
                  << ", mode=" << probe.value.failed_block_mode
                  << ", read-offset=" << probe.value.failed_block_read_offset
                  << ", csize=" << probe.value.failed_block_compressed_size
                  << ", usize=" << probe.value.failed_block_uncompressed_size
                  << ", expected=" << probe.value.failed_block_expected_size
                  << ", code=" << probe.value.failed_block_error_code;
            pkg.warnings.push_back("E_RECON_BLOCK: " + block.str());
        }
    }
    if (!probe.value.probe_stage.empty()) {
        pkg.warnings.push_back("W_STAGE: " + probe.value.probe_stage);
    }
    if (!probe.value.probe_primary_error.empty()) {
        pkg.warnings.push_back("E_PRIMARY: " + probe.value.probe_primary_error);
    }
    if (probe.value.object_table_parsed) {
        std::ostringstream obj;
        obj << "object table parsed: objects=" << probe.value.object_count
            << ", meshes=" << probe.value.mesh_object_count
            << ", materials=" << probe.value.material_object_count;
        if (!probe.value.major_types_found.empty()) {
            obj << ", types={" << probe.value.major_types_found << "}";
        }
        pkg.warnings.push_back("W_OBJECT_TABLE: " + obj.str());
        pkg.warnings.push_back("W_PAYLOAD_PENDING: mesh vertex/index and material parameter extraction.");
    } else if (!probe.value.serialized_parse_error_code.empty()) {
        pkg.warnings.push_back("E_SERIALIZED_CODE: " + probe.value.serialized_parse_error_code);
    }
    if (!probe.value.has_cab_token) {
        pkg.warnings.push_back("W_CAB_TOKEN: not found in first probe window.");
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
    pkg.warnings.push_back("W_MODE: parser mode=inhouse");

    return core::Result<AvatarPackage>::Ok(pkg);
}

}  // namespace animiq::avatar
