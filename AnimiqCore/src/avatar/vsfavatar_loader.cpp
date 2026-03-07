#include "vsfavatar_loader.h"
#include "animiq/vsf/serialized_file_reader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <sstream>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
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
    cmd_mutable.push_back('\0');
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
    payload.indices = {
        // front
        0U, 1U, 2U, 0U, 2U, 3U,
        // back
        4U, 6U, 5U, 4U, 7U, 6U,
        // left
        0U, 3U, 7U, 0U, 7U, 4U,
        // right
        1U, 5U, 6U, 1U, 6U, 2U,
        // top
        3U, 2U, 6U, 3U, 6U, 7U,
        // bottom
        0U, 4U, 5U, 0U, 5U, 1U
    };
    payload.vertex_blob.reserve(8U * 12U);

    float center_x = 0.0f;
    float center_y = 0.0f;
    float half_w = 0.18f;
    float half_h = 0.35f;
    float half_d = 0.10f;

    // Build a simple humanoid proxy from multiple cuboids.
    switch (index % 7U) {
        case 0U: // torso
            center_x = 0.0f;
            center_y = 0.05f;
            half_w = 0.18f;
            half_h = 0.32f;
            half_d = 0.10f;
            break;
        case 1U: // head
            center_x = 0.0f;
            center_y = 0.52f;
            half_w = 0.12f;
            half_h = 0.12f;
            half_d = 0.11f;
            break;
        case 2U: // left arm
            center_x = -0.28f;
            center_y = 0.08f;
            half_w = 0.06f;
            half_h = 0.26f;
            half_d = 0.07f;
            break;
        case 3U: // right arm
            center_x = 0.28f;
            center_y = 0.08f;
            half_w = 0.06f;
            half_h = 0.26f;
            half_d = 0.07f;
            break;
        case 4U: // left leg
            center_x = -0.10f;
            center_y = -0.52f;
            half_w = 0.07f;
            half_h = 0.30f;
            half_d = 0.08f;
            break;
        case 5U: // right leg
            center_x = 0.10f;
            center_y = -0.52f;
            half_w = 0.07f;
            half_h = 0.30f;
            half_d = 0.08f;
            break;
        default: // hip connector
            center_x = 0.0f;
            center_y = -0.20f;
            half_w = 0.16f;
            half_h = 0.10f;
            half_d = 0.09f;
            break;
    }

    const std::uint32_t cluster = index / 7U;
    if (cluster > 0U) {
        center_x += static_cast<float>(cluster) * 0.30f;
    }

    // Keep proxy compact so default framing does not over-zoom.
    constexpr float kProxyScale = 0.32f;
    center_x *= kProxyScale;
    center_y *= kProxyScale;
    half_w *= kProxyScale;
    half_h *= kProxyScale;
    half_d *= kProxyScale;
    center_y -= 0.12f;

    const float x_min = center_x - half_w;
    const float x_max = center_x + half_w;
    const float y_min = center_y - half_h;
    const float y_max = center_y + half_h;
    const float z_min = -half_d;
    const float z_max = half_d;

    // front quad
    AppendFloat(&payload.vertex_blob, x_min); AppendFloat(&payload.vertex_blob, y_min); AppendFloat(&payload.vertex_blob, z_max); // 0
    AppendFloat(&payload.vertex_blob, x_max); AppendFloat(&payload.vertex_blob, y_min); AppendFloat(&payload.vertex_blob, z_max); // 1
    AppendFloat(&payload.vertex_blob, x_max); AppendFloat(&payload.vertex_blob, y_max); AppendFloat(&payload.vertex_blob, z_max); // 2
    AppendFloat(&payload.vertex_blob, x_min); AppendFloat(&payload.vertex_blob, y_max); AppendFloat(&payload.vertex_blob, z_max); // 3
    // back quad
    AppendFloat(&payload.vertex_blob, x_min); AppendFloat(&payload.vertex_blob, y_min); AppendFloat(&payload.vertex_blob, z_min); // 4
    AppendFloat(&payload.vertex_blob, x_max); AppendFloat(&payload.vertex_blob, y_min); AppendFloat(&payload.vertex_blob, z_min); // 5
    AppendFloat(&payload.vertex_blob, x_max); AppendFloat(&payload.vertex_blob, y_max); AppendFloat(&payload.vertex_blob, z_min); // 6
    AppendFloat(&payload.vertex_blob, x_min); AppendFloat(&payload.vertex_blob, y_max); AppendFloat(&payload.vertex_blob, z_min); // 7
    return payload;
}

static float ReadF32LE(const std::vector<unsigned char>& bytes, std::size_t at) {
    std::uint32_t raw = static_cast<std::uint32_t>(bytes[at]) |
                        (static_cast<std::uint32_t>(bytes[at + 1U]) << 8U) |
                        (static_cast<std::uint32_t>(bytes[at + 2U]) << 16U) |
                        (static_cast<std::uint32_t>(bytes[at + 3U]) << 24U);
    float out = 0.0f;
    std::memcpy(&out, &raw, sizeof(float));
    return out;
}

static std::uint16_t ReadU16LE(const std::vector<unsigned char>& bytes, std::size_t at) {
    return static_cast<std::uint16_t>(bytes[at]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[at + 1U]) << 8U);
}

static std::uint32_t ReadU32LE(const std::vector<unsigned char>& bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at]) |
           (static_cast<std::uint32_t>(bytes[at + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[at + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[at + 3U]) << 24U);
}

static std::uint32_t ReadU32BE(const std::vector<unsigned char>& bytes, std::size_t at) {
    return (static_cast<std::uint32_t>(bytes[at]) << 24U) |
           (static_cast<std::uint32_t>(bytes[at + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[at + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[at + 3U]);
}

struct ByteArraySegment {
    std::size_t length_pos = 0U;
    std::size_t data_pos = 0U;
    std::size_t length = 0U;
};

struct IndexCandidate {
    std::size_t segment_index = 0U;
    std::uint32_t index_format_bytes = 2U;
    std::size_t index_count = 0U;
    std::uint32_t max_index = 0U;
    float degenerate_ratio = 1.0f;
    float score = 0.0f;
};

struct VertexCandidate {
    std::size_t segment_index = 0U;
    std::uint32_t stride = 12U;
    std::size_t vertex_count = 0U;
    float finite_ratio = 0.0f;
    float span = 0.0f;
    float score = 0.0f;
};

static bool TryBuildIndexedMeshPayloadFromBlob(
    const vsf::SerializedMeshObjectBlob& blob,
    std::uint32_t mesh_index,
    MeshRenderPayload* out_payload) {
    if (out_payload == nullptr) {
        return false;
    }
    if (blob.bytes.size() < 1024U) {
        return false;
    }

    std::vector<ByteArraySegment> segments;
    segments.reserve(64U);
    const std::size_t blob_size = blob.bytes.size();
    const std::size_t max_seg_len = std::min<std::size_t>(blob_size, 8U * 1024U * 1024U);
    std::unordered_set<std::uint64_t> seen_segments;
    seen_segments.reserve(512U);
    for (std::size_t p = 0U; p + 8U <= blob_size; ++p) {
        const std::array<std::uint32_t, 2U> lens = {ReadU32LE(blob.bytes, p), ReadU32BE(blob.bytes, p)};
        for (const auto len : lens) {
            if (len < 96U || len > max_seg_len) {
                continue;
            }
            const std::size_t data_pos = p + 4U;
            if (data_pos + static_cast<std::size_t>(len) > blob_size) {
                continue;
            }
            const std::uint64_t key =
                (static_cast<std::uint64_t>(data_pos) << 32U) ^ static_cast<std::uint64_t>(len);
            if (!seen_segments.insert(key).second) {
                continue;
            }
            segments.push_back({p, data_pos, static_cast<std::size_t>(len)});
            if (segments.size() >= 512U) {
                break;
            }
        }
        if (segments.size() >= 512U) {
            break;
        }
    }
    if (segments.empty()) {
        return false;
    }

    std::vector<IndexCandidate> index_candidates;
    index_candidates.reserve(64U);
    for (std::size_t s = 0U; s < segments.size(); ++s) {
        const auto& seg = segments[s];
        const std::array<std::uint32_t, 2U> fmts = {2U, 4U};
        for (const auto fmt : fmts) {
            if ((seg.length % fmt) != 0U) {
                continue;
            }
            const std::size_t index_count = seg.length / fmt;
            if (index_count < 96U || index_count > 1800000U) {
                continue;
            }
            const std::size_t sample_count = std::min<std::size_t>(index_count, 24000U);
            std::uint32_t max_index = 0U;
            std::size_t tri_count = 0U;
            std::size_t degenerate = 0U;
            for (std::size_t i = 0U; i < sample_count; ++i) {
                const std::size_t at = seg.data_pos + i * fmt;
                const std::uint32_t v = (fmt == 2U)
                    ? static_cast<std::uint32_t>(ReadU16LE(blob.bytes, at))
                    : ReadU32LE(blob.bytes, at);
                max_index = std::max(max_index, v);
            }
            for (std::size_t i = 0U; i + 2U < sample_count; i += 3U) {
                const std::size_t at0 = seg.data_pos + i * fmt;
                const std::size_t at1 = seg.data_pos + (i + 1U) * fmt;
                const std::size_t at2 = seg.data_pos + (i + 2U) * fmt;
                const std::uint32_t a = (fmt == 2U)
                    ? static_cast<std::uint32_t>(ReadU16LE(blob.bytes, at0))
                    : ReadU32LE(blob.bytes, at0);
                const std::uint32_t b = (fmt == 2U)
                    ? static_cast<std::uint32_t>(ReadU16LE(blob.bytes, at1))
                    : ReadU32LE(blob.bytes, at1);
                const std::uint32_t c = (fmt == 2U)
                    ? static_cast<std::uint32_t>(ReadU16LE(blob.bytes, at2))
                    : ReadU32LE(blob.bytes, at2);
                ++tri_count;
                if (a == b || b == c || c == a) {
                    ++degenerate;
                }
            }
            if (tri_count < 24U || max_index < 16U) {
                continue;
            }
            const float deg_ratio = static_cast<float>(degenerate) / static_cast<float>(tri_count);
            if (deg_ratio > 0.85f) {
                continue;
            }
            float score = static_cast<float>(tri_count);
            score -= deg_ratio * static_cast<float>(tri_count) * 1.4f;
            score -= static_cast<float>(max_index) * 0.0015f;
            if (fmt == 2U) {
                score += 24.0f;
            }
            index_candidates.push_back({s, fmt, index_count, max_index, deg_ratio, score});
        }
    }
    if (index_candidates.empty()) {
        return false;
    }
    std::sort(index_candidates.begin(), index_candidates.end(), [](const IndexCandidate& a, const IndexCandidate& b) {
        return a.score > b.score;
    });
    if (index_candidates.size() > 16U) {
        index_candidates.resize(16U);
    }

    std::vector<VertexCandidate> vertex_candidates;
    vertex_candidates.reserve(96U);
    const std::array<std::uint32_t, 14U> strides = {12U, 16U, 20U, 24U, 28U, 32U, 36U, 40U, 44U, 48U, 52U, 56U, 60U, 64U};
    for (std::size_t s = 0U; s < segments.size(); ++s) {
        const auto& seg = segments[s];
        for (const auto stride : strides) {
            if ((seg.length % stride) != 0U) {
                continue;
            }
            const std::size_t vertex_count = seg.length / stride;
            if (vertex_count < 32U || vertex_count > 500000U) {
                continue;
            }
            const std::size_t sample_count = std::min<std::size_t>(vertex_count, 4000U);
            std::size_t finite_count = 0U;
            float min_x = std::numeric_limits<float>::max();
            float min_y = std::numeric_limits<float>::max();
            float min_z = std::numeric_limits<float>::max();
            float max_x = -std::numeric_limits<float>::max();
            float max_y = -std::numeric_limits<float>::max();
            float max_z = -std::numeric_limits<float>::max();
            for (std::size_t i = 0U; i < sample_count; ++i) {
                const std::size_t at = seg.data_pos + i * stride;
                const float x = ReadF32LE(blob.bytes, at);
                const float y = ReadF32LE(blob.bytes, at + 4U);
                const float z = ReadF32LE(blob.bytes, at + 8U);
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
                    continue;
                }
                if (std::abs(x) > 10000.0f || std::abs(y) > 10000.0f || std::abs(z) > 10000.0f) {
                    continue;
                }
                ++finite_count;
                min_x = std::min(min_x, x);
                min_y = std::min(min_y, y);
                min_z = std::min(min_z, z);
                max_x = std::max(max_x, x);
                max_y = std::max(max_y, y);
                max_z = std::max(max_z, z);
            }
            const float finite_ratio = sample_count > 0U
                ? static_cast<float>(finite_count) / static_cast<float>(sample_count)
                : 0.0f;
            if (finite_ratio < 0.90f || finite_count < 32U) {
                continue;
            }
            const float sx = std::max(0.0f, max_x - min_x);
            const float sy = std::max(0.0f, max_y - min_y);
            const float sz = std::max(0.0f, max_z - min_z);
            const float span = std::max(sx, std::max(sy, sz));
            if (!std::isfinite(span) || span < 0.005f || span > 3000.0f) {
                continue;
            }
            float score = finite_ratio * 1000.0f;
            score += std::min<float>(static_cast<float>(vertex_count), 120000.0f) * 0.01f;
            score += std::min(span, 4.0f) * 10.0f;
            if (stride == 12U || stride == 16U || stride == 32U) {
                score += 8.0f;
            }
            vertex_candidates.push_back({s, stride, vertex_count, finite_ratio, span, score});
        }
    }
    if (vertex_candidates.empty()) {
        return false;
    }
    std::sort(vertex_candidates.begin(), vertex_candidates.end(), [](const VertexCandidate& a, const VertexCandidate& b) {
        return a.score > b.score;
    });
    if (vertex_candidates.size() > 24U) {
        vertex_candidates.resize(24U);
    }

    struct PairChoice {
        std::size_t idx_i = 0U;
        std::size_t vtx_i = 0U;
        float score = -1.0f;
    };
    PairChoice best_pair {};
    for (std::size_t i = 0U; i < index_candidates.size(); ++i) {
        const auto& idx = index_candidates[i];
        for (std::size_t j = 0U; j < vertex_candidates.size(); ++j) {
            const auto& vtx = vertex_candidates[j];
            if (vtx.vertex_count <= static_cast<std::size_t>(idx.max_index)) {
                continue;
            }
            float score = idx.score + vtx.score;
            if (idx.segment_index == vtx.segment_index) {
                score -= 120.0f;
            }
            const auto& idx_seg = segments[idx.segment_index];
            const auto& vtx_seg = segments[vtx.segment_index];
            if (vtx_seg.data_pos > idx_seg.data_pos) {
                score += 24.0f;
            }
            if (score > best_pair.score) {
                best_pair = {i, j, score};
            }
        }
    }
    if (best_pair.score < 0.0f) {
        return false;
    }

    const auto& idx = index_candidates[best_pair.idx_i];
    const auto& vtx = vertex_candidates[best_pair.vtx_i];
    const auto& idx_seg = segments[idx.segment_index];
    const auto& vtx_seg = segments[vtx.segment_index];

    MeshRenderPayload payload;
    payload.name = "VSF_STRUCT_MESH_" + std::to_string(mesh_index);
    payload.vertex_stride = 12U;
    payload.material_index = 0;
    payload.vertex_blob.reserve(vtx.vertex_count * 12U);
    for (std::size_t i = 0U; i < vtx.vertex_count; ++i) {
        const std::size_t at = vtx_seg.data_pos + i * vtx.stride;
        AppendFloat(&payload.vertex_blob, ReadF32LE(blob.bytes, at));
        AppendFloat(&payload.vertex_blob, ReadF32LE(blob.bytes, at + 4U));
        AppendFloat(&payload.vertex_blob, ReadF32LE(blob.bytes, at + 8U));
    }

    payload.indices.reserve(idx.index_count);
    std::size_t invalid_index = 0U;
    const std::size_t usable_indices = idx.index_count - (idx.index_count % 3U);
    for (std::size_t i = 0U; i < usable_indices; ++i) {
        const std::size_t at = idx_seg.data_pos + i * idx.index_format_bytes;
        const std::uint32_t v = (idx.index_format_bytes == 2U)
            ? static_cast<std::uint32_t>(ReadU16LE(blob.bytes, at))
            : ReadU32LE(blob.bytes, at);
        if (v >= vtx.vertex_count) {
            ++invalid_index;
            continue;
        }
        payload.indices.push_back(v);
    }

    if (payload.indices.size() < 600U) {
        return false;
    }
    const float invalid_ratio = usable_indices > 0U
        ? static_cast<float>(invalid_index) / static_cast<float>(usable_indices)
        : 1.0f;
    if (invalid_ratio > 0.15f) {
        return false;
    }
    // Reject candidate meshes that still contain too many stretched triangles.
    float bmin_x = std::numeric_limits<float>::max();
    float bmin_y = std::numeric_limits<float>::max();
    float bmin_z = std::numeric_limits<float>::max();
    float bmax_x = -std::numeric_limits<float>::max();
    float bmax_y = -std::numeric_limits<float>::max();
    float bmax_z = -std::numeric_limits<float>::max();
    for (std::size_t i = 0U; i < vtx.vertex_count; ++i) {
        const std::size_t at = i * 12U;
        const float x = ReadF32LE(payload.vertex_blob, at);
        const float y = ReadF32LE(payload.vertex_blob, at + 4U);
        const float z = ReadF32LE(payload.vertex_blob, at + 8U);
        bmin_x = std::min(bmin_x, x);
        bmin_y = std::min(bmin_y, y);
        bmin_z = std::min(bmin_z, z);
        bmax_x = std::max(bmax_x, x);
        bmax_y = std::max(bmax_y, y);
        bmax_z = std::max(bmax_z, z);
    }
    const float span_x = std::max(1.0e-5f, bmax_x - bmin_x);
    const float span_y = std::max(1.0e-5f, bmax_y - bmin_y);
    const float span_z = std::max(1.0e-5f, bmax_z - bmin_z);
    const float span = std::max(span_x, std::max(span_y, span_z));
    const float max_edge = std::max(0.01f, std::min(span * 0.18f, 0.80f));
    const float max_edge_sq = max_edge * max_edge;
    auto sq_dist = [&](std::uint32_t ia, std::uint32_t ib) -> float {
        const std::size_t a = static_cast<std::size_t>(ia) * 12U;
        const std::size_t b = static_cast<std::size_t>(ib) * 12U;
        const float ax = ReadF32LE(payload.vertex_blob, a);
        const float ay = ReadF32LE(payload.vertex_blob, a + 4U);
        const float az = ReadF32LE(payload.vertex_blob, a + 8U);
        const float bx = ReadF32LE(payload.vertex_blob, b);
        const float by = ReadF32LE(payload.vertex_blob, b + 4U);
        const float bz = ReadF32LE(payload.vertex_blob, b + 8U);
        const float dx = ax - bx;
        const float dy = ay - by;
        const float dz = az - bz;
        return dx * dx + dy * dy + dz * dz;
    };
    const std::size_t tri_total = payload.indices.size() / 3U;
    const std::size_t tri_sample = std::min<std::size_t>(tri_total, 20000U);
    std::size_t long_edge_tri = 0U;
    for (std::size_t t = 0U; t < tri_sample; ++t) {
        const std::uint32_t i0 = payload.indices[t * 3U + 0U];
        const std::uint32_t i1 = payload.indices[t * 3U + 1U];
        const std::uint32_t i2 = payload.indices[t * 3U + 2U];
        const float e01 = sq_dist(i0, i1);
        const float e12 = sq_dist(i1, i2);
        const float e20 = sq_dist(i2, i0);
        if (!std::isfinite(e01) || !std::isfinite(e12) || !std::isfinite(e20) ||
            e01 > max_edge_sq || e12 > max_edge_sq || e20 > max_edge_sq) {
            ++long_edge_tri;
        }
    }
    if (tri_sample >= 64U) {
        const float long_ratio = static_cast<float>(long_edge_tri) / static_cast<float>(tri_sample);
        if (long_ratio > 0.28f) {
            return false;
        }
    }
    *out_payload = std::move(payload);
    return true;
}

static bool TryBuildHeuristicMeshPayloadFromBlob(
    const vsf::SerializedMeshObjectBlob& blob,
    std::uint32_t mesh_index,
    MeshRenderPayload* out_payload) {
    if (out_payload == nullptr) {
        return false;
    }
    if (blob.bytes.size() < 256U) {
        return false;
    }

    struct Candidate {
        std::size_t start = 0U;
        std::size_t stride = 12U;
        std::size_t count = 0U;
        float span = 0.0f;
    };
    Candidate best {};
    const std::array<std::size_t, 10U> strides = {12U, 16U, 20U, 24U, 28U, 32U, 36U, 40U, 44U, 48U};
    const std::size_t scan_limit = std::min<std::size_t>(blob.bytes.size(), 8192U);
    constexpr std::size_t kMaxVertexCount = 6000U;
    for (std::size_t start = 0U; start + 12U <= scan_limit; start += 4U) {
        for (const auto stride : strides) {
            if (start + stride > blob.bytes.size() || stride < 12U || (stride % 4U) != 0U) {
                continue;
            }
            float min_x = std::numeric_limits<float>::max();
            float min_y = std::numeric_limits<float>::max();
            float min_z = std::numeric_limits<float>::max();
            float max_x = -std::numeric_limits<float>::max();
            float max_y = -std::numeric_limits<float>::max();
            float max_z = -std::numeric_limits<float>::max();
            std::size_t count = 0U;
            for (std::size_t at = start; at + 12U <= blob.bytes.size() && count < kMaxVertexCount; at += stride) {
                const float x = ReadF32LE(blob.bytes, at);
                const float y = ReadF32LE(blob.bytes, at + 4U);
                const float z = ReadF32LE(blob.bytes, at + 8U);
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
                    break;
                }
                if (std::abs(x) > 10000.0f || std::abs(y) > 10000.0f || std::abs(z) > 10000.0f) {
                    break;
                }
                min_x = std::min(min_x, x);
                min_y = std::min(min_y, y);
                min_z = std::min(min_z, z);
                max_x = std::max(max_x, x);
                max_y = std::max(max_y, y);
                max_z = std::max(max_z, z);
                ++count;
            }
            if (count < 48U) {
                continue;
            }
            const float span_x = std::max(0.0f, max_x - min_x);
            const float span_y = std::max(0.0f, max_y - min_y);
            const float span_z = std::max(0.0f, max_z - min_z);
            const float span = std::max(span_x, std::max(span_y, span_z));
            if (!std::isfinite(span) || span < 0.01f || span > 1000.0f) {
                continue;
            }
            if (count > best.count || (count == best.count && span > best.span)) {
                best = {start, stride, count, span};
            }
        }
    }
    if (best.count < 48U) {
        return false;
    }

    MeshRenderPayload payload;
    payload.name = "VSF_HEURISTIC_MESH_" + std::to_string(mesh_index);
    payload.vertex_stride = 12U;
    payload.material_index = 0;
    std::vector<float> positions;
    positions.reserve(best.count * 3U);
    payload.vertex_blob.reserve(best.count * 12U);
    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float min_z = std::numeric_limits<float>::max();
    float max_x = -std::numeric_limits<float>::max();
    float max_y = -std::numeric_limits<float>::max();
    float max_z = -std::numeric_limits<float>::max();
    for (std::size_t i = 0U; i < best.count; ++i) {
        const std::size_t at = best.start + i * best.stride;
        if (at + 12U > blob.bytes.size()) {
            break;
        }
        const float x = ReadF32LE(blob.bytes, at);
        const float y = ReadF32LE(blob.bytes, at + 4U);
        const float z = ReadF32LE(blob.bytes, at + 8U);
        AppendFloat(&payload.vertex_blob, x);
        AppendFloat(&payload.vertex_blob, y);
        AppendFloat(&payload.vertex_blob, z);
        positions.push_back(x);
        positions.push_back(y);
        positions.push_back(z);
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        min_z = std::min(min_z, z);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
        max_z = std::max(max_z, z);
    }
    const std::size_t vertex_count = payload.vertex_blob.size() / 12U;
    if (vertex_count < 48U) {
        return false;
    }

    const float span_x = std::max(0.0f, max_x - min_x);
    const float span_y = std::max(0.0f, max_y - min_y);
    const float span_z = std::max(0.0f, max_z - min_z);
    const float span = std::max(span_x, std::max(span_y, span_z));
    if (!std::isfinite(span) || span < 0.01f || span > 1000.0f) {
        return false;
    }

    auto median_of = [](std::vector<float> values) -> float {
        if (values.empty()) {
            return 0.0f;
        }
        const std::size_t mid = values.size() / 2U;
        std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid), values.end());
        return values[mid];
    };
    const float cell_size = std::max(0.01f, std::min(span * 0.03f, 0.25f));
    struct CellAccum {
        std::uint32_t count = 0U;
        float sx = 0.0f;
        float sy = 0.0f;
        float sz = 0.0f;
    };
    std::unordered_map<std::uint64_t, CellAccum> cells;
    cells.reserve(vertex_count / 4U + 32U);
    auto pack_key = [](int gx, int gy, int gz) -> std::uint64_t {
        const std::uint64_t ux = static_cast<std::uint64_t>(static_cast<std::uint32_t>(gx + 32768));
        const std::uint64_t uy = static_cast<std::uint64_t>(static_cast<std::uint32_t>(gy + 32768));
        const std::uint64_t uz = static_cast<std::uint64_t>(static_cast<std::uint32_t>(gz + 32768));
        return (ux << 32U) ^ (uy << 16U) ^ uz;
    };
    for (std::size_t i = 0U; i < vertex_count; ++i) {
        const float x = positions[i * 3U + 0U];
        const float y = positions[i * 3U + 1U];
        const float z = positions[i * 3U + 2U];
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            continue;
        }
        const int gx = static_cast<int>(std::floor(x / cell_size));
        const int gy = static_cast<int>(std::floor(y / cell_size));
        const int gz = static_cast<int>(std::floor(z / cell_size));
        auto& cell = cells[pack_key(gx, gy, gz)];
        cell.count += 1U;
        cell.sx += x;
        cell.sy += y;
        cell.sz += z;
    }
    if (cells.empty()) {
        return false;
    }
    std::uint64_t best_key = 0U;
    std::uint32_t best_count = 0U;
    for (const auto& kv : cells) {
        if (kv.second.count > best_count) {
            best_count = kv.second.count;
            best_key = kv.first;
        }
    }
    const auto best_it = cells.find(best_key);
    if (best_it == cells.end() || best_it->second.count < 8U) {
        return false;
    }
    const float cluster_cx = best_it->second.sx / static_cast<float>(best_it->second.count);
    const float cluster_cy = best_it->second.sy / static_cast<float>(best_it->second.count);
    const float cluster_cz = best_it->second.sz / static_cast<float>(best_it->second.count);
    const float keep_radius = std::max(cell_size * 3.5f, std::min(span * 0.22f, 0.9f));

    std::vector<std::size_t> kept_points;
    kept_points.reserve(vertex_count / 2U + 32U);
    for (std::size_t i = 0U; i < vertex_count; ++i) {
        const float dx = positions[i * 3U + 0U] - cluster_cx;
        const float dy = positions[i * 3U + 1U] - cluster_cy;
        const float dz = positions[i * 3U + 2U] - cluster_cz;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!std::isfinite(dist) || dist > keep_radius) {
            continue;
        }
        kept_points.push_back(i);
    }
    if (kept_points.size() < 120U) {
        return false;
    }

    // Second-pass local density filter: remove sparse axis-like outliers.
    const float local_cell = std::max(0.004f, std::min(keep_radius * 0.08f, 0.06f));
    std::unordered_map<std::uint64_t, std::uint32_t> local_counts;
    local_counts.reserve(kept_points.size() / 2U + 32U);
    for (const auto i : kept_points) {
        const float x = positions[i * 3U + 0U];
        const float y = positions[i * 3U + 1U];
        const float z = positions[i * 3U + 2U];
        const int gx = static_cast<int>(std::floor((x - cluster_cx) / local_cell));
        const int gy = static_cast<int>(std::floor((y - cluster_cy) / local_cell));
        const int gz = static_cast<int>(std::floor((z - cluster_cz) / local_cell));
        local_counts[pack_key(gx, gy, gz)] += 1U;
    }
    std::vector<std::size_t> dense_points;
    dense_points.reserve(kept_points.size());
    for (const auto i : kept_points) {
        const float x = positions[i * 3U + 0U];
        const float y = positions[i * 3U + 1U];
        const float z = positions[i * 3U + 2U];
        const int gx = static_cast<int>(std::floor((x - cluster_cx) / local_cell));
        const int gy = static_cast<int>(std::floor((y - cluster_cy) / local_cell));
        const int gz = static_cast<int>(std::floor((z - cluster_cz) / local_cell));
        const auto it = local_counts.find(pack_key(gx, gy, gz));
        if (it == local_counts.end() || it->second < 3U) {
            continue;
        }
        dense_points.push_back(i);
    }
    if (dense_points.size() >= 120U) {
        kept_points.swap(dense_points);
    }
    // Reject line-like or plane-like clusters; they come from mis-parsed float runs.
    float kmin_x = std::numeric_limits<float>::max();
    float kmin_y = std::numeric_limits<float>::max();
    float kmin_z = std::numeric_limits<float>::max();
    float kmax_x = -std::numeric_limits<float>::max();
    float kmax_y = -std::numeric_limits<float>::max();
    float kmax_z = -std::numeric_limits<float>::max();
    for (const auto i : kept_points) {
        const float x = positions[i * 3U + 0U];
        const float y = positions[i * 3U + 1U];
        const float z = positions[i * 3U + 2U];
        kmin_x = std::min(kmin_x, x);
        kmin_y = std::min(kmin_y, y);
        kmin_z = std::min(kmin_z, z);
        kmax_x = std::max(kmax_x, x);
        kmax_y = std::max(kmax_y, y);
        kmax_z = std::max(kmax_z, z);
    }
    const float kex = std::max(1.0e-5f, kmax_x - kmin_x);
    const float key = std::max(1.0e-5f, kmax_y - kmin_y);
    const float kez = std::max(1.0e-5f, kmax_z - kmin_z);
    const float kmajor = std::max(kex, std::max(key, kez));
    const float kmid = std::max(std::min(std::max(kex, key), std::max(std::min(kex, key), kez)), 1.0e-5f);
    const float kminor = std::max(std::min(kex, std::min(key, kez)), 1.0e-5f);
    const bool line_like = (kmajor / kmid) > 45.0f;
    const bool ultra_thin = (kmajor / kminor) > 180.0f;
    if (line_like || ultra_thin) {
        return false;
    }

    const std::size_t target_points = 2200U;
    const std::size_t kept_count = kept_points.size();
    const std::size_t step = std::max<std::size_t>(1U, kept_count / target_points);
    const float point_size = std::max(0.0008f, std::min(span * 0.0022f, 0.018f));
    std::vector<std::uint8_t> splat_vertices;
    std::vector<std::uint32_t> splat_indices;
    splat_vertices.reserve((kept_count / step + 1U) * 3U * 12U);
    splat_indices.reserve((kept_count / step + 1U) * 3U);
    std::uint32_t base_vertex = 0U;
    std::size_t splat_count = 0U;
    for (std::size_t k = 0U; k < kept_count; k += step) {
        const std::size_t i = kept_points[k];
        const float x = positions[i * 3U + 0U];
        const float y = positions[i * 3U + 1U];
        const float z = positions[i * 3U + 2U];
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            continue;
        }
        // Tiny local triangle per sampled point. This avoids giant stretched polygons
        // while preserving the extracted vertex-space silhouette.
        AppendFloat(&splat_vertices, x - point_size);
        AppendFloat(&splat_vertices, y);
        AppendFloat(&splat_vertices, z);
        AppendFloat(&splat_vertices, x + point_size);
        AppendFloat(&splat_vertices, y);
        AppendFloat(&splat_vertices, z);
        AppendFloat(&splat_vertices, x);
        AppendFloat(&splat_vertices, y + point_size);
        AppendFloat(&splat_vertices, z);
        splat_indices.push_back(base_vertex + 0U);
        splat_indices.push_back(base_vertex + 1U);
        splat_indices.push_back(base_vertex + 2U);
        base_vertex += 3U;
        ++splat_count;
    }
    if (splat_count < 100U || splat_indices.size() < 300U) {
        return false;
    }
    payload.vertex_blob = std::move(splat_vertices);
    payload.indices = std::move(splat_indices);
    *out_payload = std::move(payload);
    return true;
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
    std::uint32_t timeout_ms = 120000U;
    if (env_timeout > 0U) {
        timeout_ms = env_timeout;
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
    bool used_heuristic_mesh_payload = false;
    bool used_structured_mesh_payload = false;
    // Extraction is enabled by default; explicit disable flags are opt-out only.
    const bool enable_structured_mesh = !EnvFlagEnabled("VSF_DISABLE_STRUCTURED_MESH");
    const bool enable_heuristic_mesh = !EnvFlagEnabled("VSF_DISABLE_HEURISTIC_MESH");
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
        std::uint32_t heuristic_payload_count = 0U;
        std::uint32_t heuristic_index_count_total = 0U;
        std::uint32_t structured_payload_count = 0U;
        std::uint32_t structured_index_count_total = 0U;
        if (enable_structured_mesh || enable_heuristic_mesh) {
            auto probe = reader_.Probe(path);
            if (probe.ok && !probe.value.serialized_file_bytes.empty()) {
                vsf::SerializedFileReader serialized_reader;
                const std::uint32_t blob_scan_limit = std::max<std::uint32_t>(
                    6U,
                    std::min<std::uint32_t>(96U, sidecar_mesh_payload_count > 0U ? sidecar_mesh_payload_count * 2U : 24U));
                auto blobs = serialized_reader.ExtractMeshObjectBlobs(probe.value.serialized_file_bytes, blob_scan_limit);
                if (blobs.ok) {
                    if (enable_structured_mesh) {
                        for (std::size_t i = 0U; i < blobs.value.size(); ++i) {
                            MeshRenderPayload payload;
                            if (!TryBuildIndexedMeshPayloadFromBlob(
                                    blobs.value[i],
                                    static_cast<std::uint32_t>(i),
                                    &payload)) {
                                continue;
                            }
                            pkg.mesh_payloads.push_back(std::move(payload));
                            ++structured_payload_count;
                            structured_index_count_total += static_cast<std::uint32_t>(pkg.mesh_payloads.back().indices.size());
                        }
                    }
                    if (pkg.mesh_payloads.empty() && enable_heuristic_mesh) {
                        for (std::size_t i = 0U; i < blobs.value.size(); ++i) {
                            MeshRenderPayload payload;
                            if (!TryBuildHeuristicMeshPayloadFromBlob(
                                    blobs.value[i],
                                    static_cast<std::uint32_t>(i),
                                    &payload)) {
                                continue;
                            }
                            pkg.mesh_payloads.push_back(std::move(payload));
                            ++heuristic_payload_count;
                            heuristic_index_count_total += static_cast<std::uint32_t>(pkg.mesh_payloads.back().indices.size());
                        }
                    }
                }
            }
        }
        if (structured_payload_count > 0U) {
            used_structured_mesh_payload = true;
        }
        const bool heuristic_quality_ok =
            heuristic_payload_count >= 1U &&
            heuristic_index_count_total >= 90U;
        const bool structured_quality_ok =
            structured_payload_count >= 1U &&
            structured_index_count_total >= 120U;
        if (!structured_quality_ok && !heuristic_quality_ok) {
            pkg.mesh_payloads.clear();
            for (std::uint32_t i = 0; i < sidecar_mesh_payload_count; ++i) {
                pkg.mesh_payloads.push_back(BuildObjectStubPayload(i));
            }
            pkg.warnings.push_back("W_RENDER_PAYLOAD: object stub payload applied from sidecar contract.");
            pkg.warning_codes.push_back("VSF_OBJECT_STUB_RENDER_PAYLOAD");
            pkg.warnings.push_back(
                "W_HEURISTIC_MESH: quality-low, fallback=object_stub, structured-payloads=" +
                std::to_string(structured_payload_count) +
                ", structured-indices=" + std::to_string(structured_index_count_total) +
                ", heuristic-payloads=" +
                std::to_string(heuristic_payload_count) +
                ", heuristic-indices=" + std::to_string(heuristic_index_count_total) +
                ", structured-enabled=" + std::string(enable_structured_mesh ? "true" : "false") +
                ", heuristic-enabled=" + std::string(enable_heuristic_mesh ? "true" : "false"));
            used_structured_mesh_payload = false;
        } else {
            if (structured_quality_ok) {
                pkg.warnings.push_back("W_RENDER_PAYLOAD: structured indexed mesh payload extracted.");
                pkg.warning_codes.push_back("VSF_SERIALIZED_STRUCTURED_MESH_PAYLOAD");
                pkg.warnings.push_back(
                    "W_STRUCTURED_MESH: payloads=" + std::to_string(structured_payload_count) +
                    ", indices=" + std::to_string(structured_index_count_total));
                used_heuristic_mesh_payload = false;
            } else {
                used_heuristic_mesh_payload = true;
                pkg.warnings.push_back("W_RENDER_PAYLOAD: heuristic mesh payload extracted from serialized mesh objects.");
                pkg.warning_codes.push_back("VSF_SERIALIZED_HEURISTIC_MESH_PAYLOAD");
                pkg.warnings.push_back(
                    "W_HEURISTIC_MESH: payloads=" + std::to_string(heuristic_payload_count) +
                    ", indices=" + std::to_string(heuristic_index_count_total));
            }
        }
        pkg.warnings.push_back(
            "W_STRUCTURED_TRY: payloads=" + std::to_string(structured_payload_count) +
            ", indices=" + std::to_string(structured_index_count_total) +
            ", enabled=" + std::string(enable_structured_mesh ? "true" : "false"));
        if (sidecar_material_payload_count > 0U) {
            MaterialRenderPayload mat {};
            mat.name = "VSF_OBJECT_STUB_MAT";
            mat.shader_name = "Unlit/Color";
            mat.shader_variant = "stub";
            mat.alpha_mode = "OPAQUE";
            mat.double_sided = true;
            pkg.material_payloads.push_back(mat);
        }
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
    const auto serialized_parse_path = GetJsonString(output, "serialized_parse_path");
    if (!serialized_parse_path.empty()) {
        pkg.warnings.push_back("W_SERIALIZED_PARSE_PATH: " + serialized_parse_path);
    }
    const auto major_types_found = GetJsonString(output, "major_types_found");
    if (!major_types_found.empty()) {
        pkg.warnings.push_back("W_SERIALIZED_TYPES: " + major_types_found);
    }
    const auto skinned_mesh_renderer_count = GetJsonU32(output, "skinned_mesh_renderer_count");
    if (HasJsonKey(output, "skinned_mesh_renderer_count")) {
        pkg.warnings.push_back("W_SKINNED_RENDERER_COUNT: " + std::to_string(skinned_mesh_renderer_count));
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
    if (used_heuristic_mesh_payload) {
        pkg.missing_features.erase(
            std::remove_if(
                pkg.missing_features.begin(),
                pkg.missing_features.end(),
                [](const std::string& value) {
                    return value.find("mesh payload extraction") != std::string::npos;
                }),
            pkg.missing_features.end());
    }
    if (used_structured_mesh_payload) {
        pkg.missing_features.erase(
            std::remove_if(
                pkg.missing_features.begin(),
                pkg.missing_features.end(),
                [](const std::string& value) {
                    return value.find("mesh payload extraction") != std::string::npos;
                }),
            pkg.missing_features.end());
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
