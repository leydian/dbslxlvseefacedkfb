#include "vxa2_loader.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace vsfclone::avatar {

namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool ReadFileBytes(const std::string& path, std::vector<std::uint8_t>* out) {
    if (out == nullptr) {
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        return false;
    }
    in.seekg(0, std::ios::beg);
    out->assign(static_cast<std::size_t>(size), 0U);
    if (!out->empty()) {
        in.read(reinterpret_cast<char*>(out->data()), static_cast<std::streamsize>(out->size()));
    }
    return in.good() || in.eof();
}

std::optional<std::uint16_t> ReadU16Le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + 2U > bytes.size()) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

std::optional<std::uint32_t> ReadU32Le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + 4U > bytes.size()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(
        bytes[offset] | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) | (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U));
}

bool HasJsonKey(const std::string& json, const std::string& key) {
    return json.find("\"" + key + "\"") != std::string::npos;
}

std::optional<std::string> ExtractStringField(const std::string& json, const std::string& key) {
    const std::string token = "\"" + key + "\"";
    const std::size_t key_pos = json.find(token);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t colon = json.find(':', key_pos + token.size());
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    std::size_t q0 = json.find('"', colon + 1U);
    if (q0 == std::string::npos) {
        return std::nullopt;
    }
    ++q0;
    std::string out;
    bool escaped = false;
    for (std::size_t i = q0; i < json.size(); ++i) {
        const char c = json[i];
        if (escaped) {
            out.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            return out;
        }
        out.push_back(c);
    }
    return std::nullopt;
}

bool ParseStringArray(const std::string& json, const std::string& key, std::vector<std::string>* out) {
    const std::string token = "\"" + key + "\"";
    const std::size_t key_pos = json.find(token);
    if (key_pos == std::string::npos) {
        return false;
    }
    const std::size_t colon = json.find(':', key_pos + token.size());
    if (colon == std::string::npos) {
        return false;
    }
    const std::size_t arr0 = json.find('[', colon + 1U);
    if (arr0 == std::string::npos) {
        return false;
    }
    out->clear();
    std::size_t i = arr0 + 1U;
    while (i < json.size()) {
        while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) {
            ++i;
        }
        if (i >= json.size()) {
            return false;
        }
        if (json[i] == ']') {
            return true;
        }
        if (json[i] != '"') {
            return false;
        }
        ++i;
        std::string value;
        bool escaped = false;
        while (i < json.size()) {
            const char c = json[i++];
            if (escaped) {
                value.push_back(c);
                escaped = false;
                continue;
            }
            if (c == '\\') {
                escaped = true;
                continue;
            }
            if (c == '"') {
                break;
            }
            value.push_back(c);
        }
        if (value.empty()) {
            return false;
        }
        out->push_back(std::move(value));
        while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) {
            ++i;
        }
        if (i < json.size() && json[i] == ',') {
            ++i;
            continue;
        }
        if (i < json.size() && json[i] == ']') {
            return true;
        }
        return false;
    }
    return false;
}

}  // namespace

bool Vxa2Loader::CanLoadPath(const std::string& path) const {
    const auto ext = ToLower(fs::path(path).extension().string());
    return ext == ".vxa2";
}

core::Result<AvatarPackage> Vxa2Loader::Load(const std::string& path) const {
    std::vector<std::uint8_t> bytes;
    if (!ReadFileBytes(path, &bytes)) {
        return core::Result<AvatarPackage>::Fail("could not open vxa2 file");
    }
    if (bytes.size() < 10U) {
        return core::Result<AvatarPackage>::Fail("vxa2 file is too small");
    }

    AvatarPackage pkg;
    pkg.source_type = AvatarSourceType::Vxa2;
    pkg.compat_level = AvatarCompatLevel::Failed;
    pkg.parser_stage = "parse";
    pkg.primary_error_code = "NONE";
    pkg.source_path = path;
    pkg.display_name = fs::path(path).stem().string();

    if (!(bytes[0] == 'V' && bytes[1] == 'X' && bytes[2] == 'A' && bytes[3] == '2')) {
        pkg.primary_error_code = "VXA2_SCHEMA_INVALID";
        pkg.warnings.push_back("E_PARSE: VXA2_SCHEMA_INVALID: magic header mismatch.");
        return core::Result<AvatarPackage>::Ok(pkg);
    }
    const auto version = ReadU16Le(bytes, 4U);
    const auto manifest_size = ReadU32Le(bytes, 6U);
    if (!version || !manifest_size || *version != 1U) {
        pkg.primary_error_code = "VXA2_SCHEMA_INVALID";
        pkg.warnings.push_back("E_PARSE: VXA2_SCHEMA_INVALID: unsupported version.");
        return core::Result<AvatarPackage>::Ok(pkg);
    }
    const std::size_t manifest_offset = 10U;
    const std::size_t manifest_end = manifest_offset + static_cast<std::size_t>(*manifest_size);
    if (manifest_end > bytes.size()) {
        pkg.primary_error_code = "VXA2_SCHEMA_INVALID";
        pkg.warnings.push_back("E_PARSE: VXA2_SCHEMA_INVALID: manifest section out of range.");
        return core::Result<AvatarPackage>::Ok(pkg);
    }

    const std::string manifest(reinterpret_cast<const char*>(bytes.data() + static_cast<std::ptrdiff_t>(manifest_offset)), *manifest_size);
    if (!HasJsonKey(manifest, "avatarId") || !HasJsonKey(manifest, "meshRefs") || !HasJsonKey(manifest, "materialRefs") ||
        !HasJsonKey(manifest, "textureRefs")) {
        pkg.primary_error_code = "VXA2_SCHEMA_INVALID";
        pkg.warnings.push_back(
            "E_PARSE: VXA2_SCHEMA_INVALID: required keys avatarId/meshRefs/materialRefs/textureRefs are missing.");
        return core::Result<AvatarPackage>::Ok(pkg);
    }
    if (const auto display_name = ExtractStringField(manifest, "displayName"); display_name && !display_name->empty()) {
        pkg.display_name = *display_name;
    }
    pkg.warnings.push_back("W_STAGE: parse");

    pkg.parser_stage = "resolve";
    std::vector<std::string> mesh_refs;
    std::vector<std::string> material_refs;
    std::vector<std::string> texture_refs;
    if (!ParseStringArray(manifest, "meshRefs", &mesh_refs) || !ParseStringArray(manifest, "materialRefs", &material_refs) ||
        !ParseStringArray(manifest, "textureRefs", &texture_refs)) {
        pkg.primary_error_code = "VXA2_SCHEMA_INVALID";
        pkg.warnings.push_back("E_RESOLVE: VXA2_SCHEMA_INVALID: invalid reference array values.");
        return core::Result<AvatarPackage>::Ok(pkg);
    }
    pkg.warnings.push_back("W_STAGE: resolve");

    pkg.parser_stage = "payload";
    for (const std::string& mesh_ref : mesh_refs) {
        pkg.meshes.push_back({mesh_ref, 0U, 0U});
        pkg.mesh_payloads.push_back({mesh_ref, {}, {}});
    }
    for (const std::string& mat_ref : material_refs) {
        pkg.materials.push_back({mat_ref, "mtoon"});
        MaterialRenderPayload payload;
        payload.name = mat_ref;
        payload.shader_name = "mtoon";
        if (!texture_refs.empty()) {
            payload.base_color_texture_name = texture_refs.front();
        }
        pkg.material_payloads.push_back(std::move(payload));
    }
    for (const std::string& tex_ref : texture_refs) {
        TextureRenderPayload payload;
        payload.name = tex_ref;
        payload.format = "external";
        pkg.texture_payloads.push_back(std::move(payload));
    }
    pkg.warnings.push_back("W_STAGE: payload");

    pkg.parser_stage = "runtime-ready";
    pkg.warnings.push_back("W_STAGE: runtime-ready");
    pkg.compat_level = AvatarCompatLevel::Partial;
    pkg.primary_error_code = "NONE";
    pkg.missing_features.push_back("VXA2 binary asset section decode");
    return core::Result<AvatarPackage>::Ok(pkg);
}

}  // namespace vsfclone::avatar
