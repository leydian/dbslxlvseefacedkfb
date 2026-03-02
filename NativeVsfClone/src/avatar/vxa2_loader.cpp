#include "vxa2_loader.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <unordered_map>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace vsfclone::avatar {

namespace {

constexpr std::uint16_t kSectionMeshBlob = 0x0001U;
constexpr std::uint16_t kSectionTextureBlob = 0x0002U;
constexpr std::uint16_t kSectionMaterialOverride = 0x0003U;

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string NormalizeRefKey(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        out.push_back(c == '\\' ? '/' : c);
    }
    return ToLower(out);
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

bool ReadSizedString(
    const std::vector<std::uint8_t>& bytes,
    std::size_t* inout_offset,
    std::size_t end,
    std::string* out_value) {
    if (inout_offset == nullptr || out_value == nullptr) {
        return false;
    }
    const auto len = ReadU16Le(bytes, *inout_offset);
    if (!len) {
        return false;
    }
    *inout_offset += 2U;
    const std::size_t name_len = static_cast<std::size_t>(*len);
    if (*inout_offset + name_len > end) {
        return false;
    }
    out_value->assign(reinterpret_cast<const char*>(bytes.data() + static_cast<std::ptrdiff_t>(*inout_offset)), name_len);
    *inout_offset += name_len;
    return true;
}

bool ParseBinaryPayloadSection(
    const std::vector<std::uint8_t>& bytes,
    std::size_t payload_offset,
    std::size_t payload_size,
    std::string* out_name,
    std::vector<std::uint8_t>* out_blob) {
    if (out_name == nullptr || out_blob == nullptr) {
        return false;
    }
    const std::size_t end = payload_offset + payload_size;
    std::size_t cursor = payload_offset;
    if (!ReadSizedString(bytes, &cursor, end, out_name)) {
        return false;
    }
    if (out_name->empty()) {
        return false;
    }
    const auto blob_size = ReadU32Le(bytes, cursor);
    if (!blob_size) {
        return false;
    }
    cursor += 4U;
    const std::size_t data_size = static_cast<std::size_t>(*blob_size);
    if (cursor + data_size != end) {
        return false;
    }
    out_blob->assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
        bytes.begin() + static_cast<std::ptrdiff_t>(end));
    return true;
}

bool ParseMaterialOverrideSection(
    const std::vector<std::uint8_t>& bytes,
    std::size_t payload_offset,
    std::size_t payload_size,
    MaterialRenderPayload* out_override) {
    if (out_override == nullptr) {
        return false;
    }
    const std::size_t end = payload_offset + payload_size;
    std::size_t cursor = payload_offset;
    if (!ReadSizedString(bytes, &cursor, end, &out_override->name)) {
        return false;
    }
    if (out_override->name.empty()) {
        return false;
    }
    if (!ReadSizedString(bytes, &cursor, end, &out_override->shader_name)) {
        return false;
    }
    if (out_override->shader_name.empty()) {
        return false;
    }
    if (!ReadSizedString(bytes, &cursor, end, &out_override->base_color_texture_name)) {
        return false;
    }
    return cursor == end;
}

}  // namespace

bool Vxa2Loader::CanLoadPath(const std::string& path) const {
    const auto ext = ToLower(fs::path(path).extension().string());
    return ext == ".vxa2";
}

bool Vxa2Loader::CanLoadBytes(const std::vector<std::uint8_t>& head) const {
    return head.size() >= 4U && head[0] == 'V' && head[1] == 'X' && head[2] == 'A' && head[3] == '2';
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

    for (const std::string& mesh_ref : mesh_refs) {
        pkg.meshes.push_back({mesh_ref, 0U, 0U});
    }
    for (const std::string& mat_ref : material_refs) {
        pkg.materials.push_back({mat_ref, "mtoon"});
    }

    std::unordered_map<std::string, std::vector<std::uint8_t>> mesh_sections;
    std::unordered_map<std::string, std::vector<std::uint8_t>> texture_sections;
    std::unordered_map<std::string, MaterialRenderPayload> material_sections;

    std::size_t cursor = manifest_end;
    while (cursor < bytes.size()) {
        const auto type = ReadU16Le(bytes, cursor);
        const auto flags = ReadU16Le(bytes, cursor + 2U);
        const auto sec_size = ReadU32Le(bytes, cursor + 4U);
        if (!type || !flags || !sec_size) {
            pkg.primary_error_code = "VXA2_SECTION_TRUNCATED";
            pkg.warnings.push_back("E_PARSE: VXA2_SECTION_TRUNCATED: section header is truncated.");
            return core::Result<AvatarPackage>::Ok(pkg);
        }

        const std::size_t payload_offset = cursor + 8U;
        const std::size_t payload_size = static_cast<std::size_t>(*sec_size);
        const std::size_t section_end = payload_offset + payload_size;
        if (payload_offset > bytes.size() || section_end > bytes.size()) {
            pkg.primary_error_code = "VXA2_SECTION_TRUNCATED";
            pkg.warnings.push_back("E_PARSE: VXA2_SECTION_TRUNCATED: section payload range out of file.");
            return core::Result<AvatarPackage>::Ok(pkg);
        }

        ++pkg.format_section_count;
        if (*flags != 0U) {
            pkg.warnings.push_back("W_PARSE: VXA2_SECTION_FLAGS_NONZERO: type=" + std::to_string(*type));
        }

        if (*type == kSectionMeshBlob || *type == kSectionTextureBlob) {
            std::string name;
            std::vector<std::uint8_t> blob;
            if (!ParseBinaryPayloadSection(bytes, payload_offset, payload_size, &name, &blob)) {
                pkg.primary_error_code = "VXA2_SCHEMA_INVALID";
                pkg.warnings.push_back("E_PARSE: VXA2_SCHEMA_INVALID: invalid binary payload section.");
                return core::Result<AvatarPackage>::Ok(pkg);
            }
            if (*type == kSectionMeshBlob) {
                mesh_sections[NormalizeRefKey(name)] = std::move(blob);
            } else {
                texture_sections[NormalizeRefKey(name)] = std::move(blob);
            }
            ++pkg.format_decoded_section_count;
        } else if (*type == kSectionMaterialOverride) {
            MaterialRenderPayload material_payload;
            if (!ParseMaterialOverrideSection(bytes, payload_offset, payload_size, &material_payload)) {
                pkg.primary_error_code = "VXA2_SCHEMA_INVALID";
                pkg.warnings.push_back("E_PARSE: VXA2_SCHEMA_INVALID: invalid material override section.");
                return core::Result<AvatarPackage>::Ok(pkg);
            }
            material_sections[NormalizeRefKey(material_payload.name)] = std::move(material_payload);
            ++pkg.format_decoded_section_count;
        } else {
            ++pkg.format_unknown_section_count;
            pkg.warnings.push_back("W_PARSE: VXA2_UNKNOWN_SECTION: type=" + std::to_string(*type));
        }

        cursor = section_end;
    }

    pkg.parser_stage = "payload";
    bool has_payload_gap = false;
    std::size_t matched_mesh_payloads = 0U;
    std::size_t matched_texture_payloads = 0U;
    for (const std::string& mesh_ref : mesh_refs) {
        MeshRenderPayload payload;
        payload.name = mesh_ref;
        const auto it = mesh_sections.find(NormalizeRefKey(mesh_ref));
        if (it != mesh_sections.end()) {
            payload.vertex_blob = it->second;
            ++matched_mesh_payloads;
        } else {
            has_payload_gap = true;
        }
        pkg.mesh_payloads.push_back(std::move(payload));
    }
    for (const std::string& mat_ref : material_refs) {
        MaterialRenderPayload payload;
        payload.name = mat_ref;
        payload.shader_name = "mtoon";
        const auto it = material_sections.find(NormalizeRefKey(mat_ref));
        if (it != material_sections.end()) {
            if (!it->second.shader_name.empty()) {
                payload.shader_name = it->second.shader_name;
            }
            if (!it->second.base_color_texture_name.empty()) {
                payload.base_color_texture_name = it->second.base_color_texture_name;
            }
        }
        if (!texture_refs.empty()) {
            if (payload.base_color_texture_name.empty()) {
                payload.base_color_texture_name = texture_refs.front();
            }
        }
        pkg.material_payloads.push_back(std::move(payload));
    }
    for (const std::string& tex_ref : texture_refs) {
        TextureRenderPayload payload;
        payload.name = tex_ref;
        payload.format = "binary";
        const auto it = texture_sections.find(NormalizeRefKey(tex_ref));
        if (it != texture_sections.end()) {
            payload.bytes = it->second;
            ++matched_texture_payloads;
        } else {
            has_payload_gap = true;
        }
        pkg.texture_payloads.push_back(std::move(payload));
    }
    if (pkg.format_section_count == 0U) {
        pkg.warnings.push_back("W_PAYLOAD: VXA2 has no section table entries.");
    }
    if (mesh_refs.size() != matched_mesh_payloads || texture_refs.size() != matched_texture_payloads) {
        pkg.warnings.push_back(
            "E_PAYLOAD: VXA2_ASSET_MISSING: ref/payload mismatch (mesh=" + std::to_string(matched_mesh_payloads) + "/" +
            std::to_string(mesh_refs.size()) + ", texture=" + std::to_string(matched_texture_payloads) + "/" +
            std::to_string(texture_refs.size()) + ").");
    }
    if (pkg.format_unknown_section_count > 0U) {
        pkg.missing_features.push_back("VXA2 unknown section passthrough");
    }
    if (matched_mesh_payloads != mesh_refs.size()) {
        pkg.missing_features.push_back("VXA2 mesh section payload decode coverage");
    }
    if (matched_texture_payloads != texture_refs.size()) {
        pkg.missing_features.push_back("VXA2 texture section payload decode coverage");
    }
    pkg.warnings.push_back("W_STAGE: payload");

    pkg.parser_stage = "runtime-ready";
    pkg.warnings.push_back("W_STAGE: runtime-ready");
    const bool fully_matched =
        mesh_refs.size() == matched_mesh_payloads && texture_refs.size() == matched_texture_payloads;
    pkg.compat_level = fully_matched ? AvatarCompatLevel::Full : AvatarCompatLevel::Partial;
    pkg.primary_error_code = has_payload_gap ? "VXA2_ASSET_MISSING" : "NONE";
    return core::Result<AvatarPackage>::Ok(pkg);
}

}  // namespace vsfclone::avatar
