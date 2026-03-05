#include "xav2_loader.h"

#include <algorithm>
#include <cstring>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace vsfclone::avatar {

namespace {

constexpr std::uint16_t kSectionMeshBlobLegacy = 0x0001U;
constexpr std::uint16_t kSectionTextureBlob = 0x0002U;
constexpr std::uint16_t kSectionMaterialOverride = 0x0003U;
constexpr std::uint16_t kSectionMeshRenderPayload = 0x0011U;
constexpr std::uint16_t kSectionMaterialShaderParams = 0x0012U;
constexpr std::uint16_t kSectionSkinPayload = 0x0013U;
constexpr std::uint16_t kSectionBlendShapePayload = 0x0014U;
constexpr std::uint16_t kSectionMaterialTypedParams = 0x0015U;
constexpr std::uint16_t kSectionSkeletonPosePayload = 0x0016U;
constexpr std::uint16_t kSectionSkeletonRigPayload = 0x0017U;
constexpr std::uint16_t kSectionFlagPayloadCompressedLz4 = 0x0001U;
constexpr std::uint16_t kSectionFlagKnownMask = kSectionFlagPayloadCompressedLz4;

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

std::string NormalizeShaderFamily(const std::string& raw) {
    const std::string key = NormalizeRefKey(raw);
    return key.empty() ? "legacy" : key;
}

bool IsSupportedShaderFamily(const std::string& raw) {
    const std::string key = NormalizeShaderFamily(raw);
    return key == "legacy" ||
           key == "liltoon" ||
           key == "poiyomi" ||
           key == "potatoon" ||
           key == "realtoon";
}

HumanoidBoneId ToHumanoidBoneId(const std::string& bone_name_raw) {
    std::string key;
    key.reserve(bone_name_raw.size());
    for (const unsigned char c : bone_name_raw) {
        if (std::isalnum(c) != 0) {
            key.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    if (key == "hips") {
        return HumanoidBoneId::Hips;
    }
    if (key == "spine") {
        return HumanoidBoneId::Spine;
    }
    if (key == "chest") {
        return HumanoidBoneId::Chest;
    }
    if (key == "upperchest") {
        return HumanoidBoneId::UpperChest;
    }
    if (key == "neck") {
        return HumanoidBoneId::Neck;
    }
    if (key == "head") {
        return HumanoidBoneId::Head;
    }
    if (key == "leftupperarm" || key == "leftarm") {
        return HumanoidBoneId::LeftUpperArm;
    }
    if (key == "rightupperarm" || key == "rightarm") {
        return HumanoidBoneId::RightUpperArm;
    }
    return HumanoidBoneId::Unknown;
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

std::optional<std::int32_t> ReadI32Le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    const auto u = ReadU32Le(bytes, offset);
    if (!u) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(*u);
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
    const std::size_t value_len = static_cast<std::size_t>(*len);
    if (*inout_offset + value_len > end) {
        return false;
    }
    out_value->assign(reinterpret_cast<const char*>(bytes.data() + static_cast<std::ptrdiff_t>(*inout_offset)), value_len);
    *inout_offset += value_len;
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
    if (!ReadSizedString(bytes, &cursor, end, &out_override->name) || out_override->name.empty()) {
        return false;
    }
    if (!ReadSizedString(bytes, &cursor, end, &out_override->shader_name) || out_override->shader_name.empty()) {
        return false;
    }
    const std::size_t cursor_after_shader = cursor;

    auto parse_tail = [&](bool with_variant) -> bool {
        std::size_t c = cursor_after_shader;
        if (with_variant) {
            if (!ReadSizedString(bytes, &c, end, &out_override->shader_variant) || out_override->shader_variant.empty()) {
                return false;
            }
        } else {
            out_override->shader_variant = "default";
        }
        if (!ReadSizedString(bytes, &c, end, &out_override->base_color_texture_name)) {
            return false;
        }
        if (!ReadSizedString(bytes, &c, end, &out_override->alpha_mode) || out_override->alpha_mode.empty()) {
            return false;
        }
        const auto alpha_cutoff_bits = ReadU32Le(bytes, c);
        if (!alpha_cutoff_bits) {
            return false;
        }
        c += 4U;
        float alpha_cutoff = 0.5f;
        static_assert(sizeof(float) == sizeof(std::uint32_t));
        std::memcpy(&alpha_cutoff, &(*alpha_cutoff_bits), sizeof(float));
        out_override->alpha_cutoff = alpha_cutoff;
        if (c >= end) {
            return false;
        }
        out_override->double_sided = bytes[c] != 0U;
        c += 1U;
        if (c != end) {
            return false;
        }
        cursor = c;
        return true;
    };

    if (parse_tail(true)) {
        return true;
    }
    return parse_tail(false);
}

bool ParseSkinPayloadSection(
    const std::vector<std::uint8_t>& bytes,
    std::size_t payload_offset,
    std::size_t payload_size,
    SkinRenderPayload* out_payload) {
    if (out_payload == nullptr) {
        return false;
    }
    const std::size_t end = payload_offset + payload_size;
    std::size_t cursor = payload_offset;
    if (!ReadSizedString(bytes, &cursor, end, &out_payload->mesh_name) || out_payload->mesh_name.empty()) {
        return false;
    }
    const auto bone_count = ReadU32Le(bytes, cursor);
    if (!bone_count) {
        return false;
    }
    cursor += 4U;
    const std::size_t bone_count_sz = static_cast<std::size_t>(*bone_count);
    out_payload->bone_indices.clear();
    out_payload->bone_indices.reserve(bone_count_sz);
    for (std::size_t i = 0; i < bone_count_sz; ++i) {
        const auto bone_index = ReadI32Le(bytes, cursor + i * 4U);
        if (!bone_index) {
            return false;
        }
        out_payload->bone_indices.push_back(*bone_index);
    }
    cursor += bone_count_sz * 4U;
    const auto bindpose_f32_count = ReadU32Le(bytes, cursor);
    if (!bindpose_f32_count) {
        return false;
    }
    cursor += 4U;
    const std::size_t bindpose_count_sz = static_cast<std::size_t>(*bindpose_f32_count);
    out_payload->bind_poses_16xn.clear();
    out_payload->bind_poses_16xn.reserve(bindpose_count_sz);
    for (std::size_t i = 0; i < bindpose_count_sz; ++i) {
        const auto f32_bits = ReadU32Le(bytes, cursor + i * 4U);
        if (!f32_bits) {
            return false;
        }
        float value = 0.0f;
        std::memcpy(&value, &(*f32_bits), sizeof(float));
        out_payload->bind_poses_16xn.push_back(value);
    }
    cursor += bindpose_count_sz * 4U;
    const auto weight_blob_size = ReadU32Le(bytes, cursor);
    if (!weight_blob_size) {
        return false;
    }
    cursor += 4U;
    const std::size_t weight_blob_size_sz = static_cast<std::size_t>(*weight_blob_size);
    if (cursor + weight_blob_size_sz != end) {
        return false;
    }
    out_payload->skin_weight_blob.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
        bytes.begin() + static_cast<std::ptrdiff_t>(end));
    return true;
}

bool ParseBlendShapePayloadSection(
    const std::vector<std::uint8_t>& bytes,
    std::size_t payload_offset,
    std::size_t payload_size,
    BlendShapeRenderPayload* out_payload) {
    if (out_payload == nullptr) {
        return false;
    }
    const std::size_t end = payload_offset + payload_size;
    std::size_t cursor = payload_offset;
    if (!ReadSizedString(bytes, &cursor, end, &out_payload->mesh_name) || out_payload->mesh_name.empty()) {
        return false;
    }
    const auto frame_count = ReadU32Le(bytes, cursor);
    if (!frame_count) {
        return false;
    }
    cursor += 4U;
    out_payload->frames.clear();
    out_payload->frames.reserve(*frame_count);
    for (std::size_t i = 0; i < *frame_count; ++i) {
        BlendShapeFramePayload frame;
        if (!ReadSizedString(bytes, &cursor, end, &frame.name) || frame.name.empty()) {
            return false;
        }
        const auto weight_bits = ReadU32Le(bytes, cursor);
        if (!weight_bits) {
            return false;
        }
        cursor += 4U;
        std::memcpy(&frame.weight, &(*weight_bits), sizeof(float));
        const auto dv_size = ReadU32Le(bytes, cursor);
        if (!dv_size) {
            return false;
        }
        cursor += 4U;
        const std::size_t dv_size_sz = static_cast<std::size_t>(*dv_size);
        if (cursor + dv_size_sz > end) {
            return false;
        }
        frame.delta_vertices.assign(
            bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
            bytes.begin() + static_cast<std::ptrdiff_t>(cursor + dv_size_sz));
        cursor += dv_size_sz;
        const auto dn_size = ReadU32Le(bytes, cursor);
        if (!dn_size) {
            return false;
        }
        cursor += 4U;
        const std::size_t dn_size_sz = static_cast<std::size_t>(*dn_size);
        if (cursor + dn_size_sz > end) {
            return false;
        }
        frame.delta_normals.assign(
            bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
            bytes.begin() + static_cast<std::ptrdiff_t>(cursor + dn_size_sz));
        cursor += dn_size_sz;
        const auto dt_size = ReadU32Le(bytes, cursor);
        if (!dt_size) {
            return false;
        }
        cursor += 4U;
        const std::size_t dt_size_sz = static_cast<std::size_t>(*dt_size);
        if (cursor + dt_size_sz > end) {
            return false;
        }
        frame.delta_tangents.assign(
            bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
            bytes.begin() + static_cast<std::ptrdiff_t>(cursor + dt_size_sz));
        cursor += dt_size_sz;
        out_payload->frames.push_back(std::move(frame));
    }
    return cursor == end;
}

bool ParseMeshRenderPayloadSection(
    const std::vector<std::uint8_t>& bytes,
    std::size_t payload_offset,
    std::size_t payload_size,
    MeshRenderPayload* out_payload) {
    if (out_payload == nullptr) {
        return false;
    }
    const std::size_t end = payload_offset + payload_size;
    std::size_t cursor = payload_offset;
    if (!ReadSizedString(bytes, &cursor, end, &out_payload->name) || out_payload->name.empty()) {
        return false;
    }
    const auto vertex_stride = ReadU32Le(bytes, cursor);
    if (!vertex_stride) {
        return false;
    }
    cursor += 4U;
    const auto material_index = ReadI32Le(bytes, cursor);
    if (!material_index) {
        return false;
    }
    cursor += 4U;
    const auto vb_size = ReadU32Le(bytes, cursor);
    if (!vb_size) {
        return false;
    }
    cursor += 4U;
    const std::size_t vertex_blob_size = static_cast<std::size_t>(*vb_size);
    if (cursor + vertex_blob_size > end) {
        return false;
    }
    out_payload->vertex_blob.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
        bytes.begin() + static_cast<std::ptrdiff_t>(cursor + vertex_blob_size));
    cursor += vertex_blob_size;
    const auto index_count = ReadU32Le(bytes, cursor);
    if (!index_count) {
        return false;
    }
    cursor += 4U;
    const std::size_t index_bytes = static_cast<std::size_t>(*index_count) * sizeof(std::uint32_t);
    if (cursor + index_bytes != end) {
        return false;
    }
    out_payload->indices.clear();
    out_payload->indices.reserve(*index_count);
    for (std::size_t i = 0; i < *index_count; ++i) {
        const auto index = ReadU32Le(bytes, cursor + i * sizeof(std::uint32_t));
        if (!index) {
            return false;
        }
        out_payload->indices.push_back(*index);
    }
    out_payload->vertex_stride = *vertex_stride;
    out_payload->material_index = *material_index;
    return true;
}

bool ParseMaterialShaderParamsSection(
    const std::vector<std::uint8_t>& bytes,
    std::size_t payload_offset,
    std::size_t payload_size,
    std::string* out_name,
    std::string* out_json) {
    if (out_name == nullptr || out_json == nullptr) {
        return false;
    }
    const std::size_t end = payload_offset + payload_size;
    std::size_t cursor = payload_offset;
    if (!ReadSizedString(bytes, &cursor, end, out_name) || out_name->empty()) {
        return false;
    }
    if (!ReadSizedString(bytes, &cursor, end, out_json) || out_json->empty()) {
        return false;
    }
    return cursor == end;
}

std::optional<bool> ExtractBoolField(const std::string& json, const std::string& key) {
    const std::string token = "\"" + key + "\"";
    const std::size_t key_pos = json.find(token);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t colon = json.find(':', key_pos + token.size());
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    std::size_t i = colon + 1U;
    while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i])) != 0) {
        ++i;
    }
    if (i + 4U <= json.size() && json.compare(i, 4U, "true") == 0) {
        return true;
    }
    if (i + 5U <= json.size() && json.compare(i, 5U, "false") == 0) {
        return false;
    }
    return std::nullopt;
}

bool ParseSkeletonPosePayloadSection(
    const std::vector<std::uint8_t>& bytes,
    std::size_t payload_offset,
    std::size_t payload_size,
    SkeletonRenderPayload* out_payload) {
    if (out_payload == nullptr) {
        return false;
    }
    const std::size_t end = payload_offset + payload_size;
    std::size_t cursor = payload_offset;
    if (!ReadSizedString(bytes, &cursor, end, &out_payload->mesh_name) || out_payload->mesh_name.empty()) {
        return false;
    }
    const auto matrix_f32_count = ReadU32Le(bytes, cursor);
    if (!matrix_f32_count) {
        return false;
    }
    cursor += 4U;
    const std::size_t value_count = static_cast<std::size_t>(*matrix_f32_count);
    out_payload->bone_matrices_16xn.clear();
    out_payload->bone_matrices_16xn.reserve(value_count);
    for (std::size_t i = 0U; i < value_count; ++i) {
        const auto f32_bits = ReadU32Le(bytes, cursor + i * 4U);
        if (!f32_bits) {
            return false;
        }
        float value = 0.0f;
        std::memcpy(&value, &(*f32_bits), sizeof(float));
        out_payload->bone_matrices_16xn.push_back(value);
    }
    cursor += value_count * 4U;
    return cursor == end;
}

bool ParseSkeletonRigPayloadSection(
    const std::vector<std::uint8_t>& bytes,
    std::size_t payload_offset,
    std::size_t payload_size,
    SkeletonRigPayload* out_payload) {
    if (out_payload == nullptr) {
        return false;
    }
    const std::size_t end = payload_offset + payload_size;
    std::size_t cursor = payload_offset;
    if (!ReadSizedString(bytes, &cursor, end, &out_payload->mesh_name) || out_payload->mesh_name.empty()) {
        return false;
    }
    const auto bone_count = ReadU32Le(bytes, cursor);
    if (!bone_count) {
        return false;
    }
    cursor += 4U;
    out_payload->bones.clear();
    out_payload->bones.reserve(*bone_count);
    for (std::size_t i = 0U; i < *bone_count; ++i) {
        SkeletonRigBonePayload bone;
        if (!ReadSizedString(bytes, &cursor, end, &bone.bone_name) || bone.bone_name.empty()) {
            return false;
        }
        const auto parent_index = ReadI32Le(bytes, cursor);
        if (!parent_index) {
            return false;
        }
        cursor += 4U;
        const auto matrix_f32_count = ReadU32Le(bytes, cursor);
        if (!matrix_f32_count) {
            return false;
        }
        cursor += 4U;
        const std::size_t value_count = static_cast<std::size_t>(*matrix_f32_count);
        if (value_count != 16U) {
            return false;
        }
        bone.parent_index = *parent_index;
        bone.local_matrix_16.clear();
        bone.local_matrix_16.reserve(value_count);
        for (std::size_t mi = 0U; mi < value_count; ++mi) {
            const auto f32_bits = ReadU32Le(bytes, cursor + mi * 4U);
            if (!f32_bits) {
                return false;
            }
            float value = 0.0f;
            std::memcpy(&value, &(*f32_bits), sizeof(float));
            bone.local_matrix_16.push_back(value);
        }
        cursor += value_count * 4U;
        bone.humanoid_id = ToHumanoidBoneId(bone.bone_name);
        out_payload->bones.push_back(std::move(bone));
    }
    return cursor == end;
}

bool ParseMaterialTypedParamsSection(
    const std::vector<std::uint8_t>& bytes,
    std::size_t payload_offset,
    std::size_t payload_size,
    bool prefer_typed_v3,
    MaterialRenderPayload* out_payload) {
    if (out_payload == nullptr) {
        return false;
    }
    const std::size_t end = payload_offset + payload_size;
    std::size_t cursor = payload_offset;
    if (!ReadSizedString(bytes, &cursor, end, &out_payload->name) || out_payload->name.empty()) {
        return false;
    }
    if (!ReadSizedString(bytes, &cursor, end, &out_payload->shader_family) || out_payload->shader_family.empty()) {
        return false;
    }
    out_payload->shader_family = NormalizeShaderFamily(out_payload->shader_family);
    const auto feature_flags = ReadU32Le(bytes, cursor);
    if (!feature_flags) {
        return false;
    }
    cursor += 4U;
    out_payload->feature_flags = *feature_flags;
    out_payload->typed_schema_version = 2U;
    out_payload->material_param_encoding = "typed-v2";
    out_payload->typed_float_params.clear();
    out_payload->typed_color_params.clear();
    out_payload->typed_texture_params.clear();
    const auto parse_typed_body = [&](std::size_t body_cursor, std::uint16_t float_count, MaterialRenderPayload* dst) -> bool {
        dst->typed_float_params.clear();
        dst->typed_color_params.clear();
        dst->typed_texture_params.clear();

        dst->typed_float_params.reserve(float_count);
        for (std::size_t i = 0U; i < float_count; ++i) {
            MaterialRenderPayload::TypedFloatParam p;
            if (!ReadSizedString(bytes, &body_cursor, end, &p.id) || p.id.empty()) {
                return false;
            }
            const auto value_bits = ReadU32Le(bytes, body_cursor);
            if (!value_bits) {
                return false;
            }
            body_cursor += 4U;
            std::memcpy(&p.value, &(*value_bits), sizeof(float));
            dst->typed_float_params.push_back(std::move(p));
        }

        const auto color_count = ReadU16Le(bytes, body_cursor);
        if (!color_count) {
            return false;
        }
        body_cursor += 2U;
        dst->typed_color_params.reserve(*color_count);
        for (std::size_t i = 0U; i < *color_count; ++i) {
            MaterialRenderPayload::TypedColorParam p;
            if (!ReadSizedString(bytes, &body_cursor, end, &p.id) || p.id.empty()) {
                return false;
            }
            for (std::size_t c = 0U; c < 4U; ++c) {
                const auto f32_bits = ReadU32Le(bytes, body_cursor);
                if (!f32_bits) {
                    return false;
                }
                body_cursor += 4U;
                std::memcpy(&p.rgba[c], &(*f32_bits), sizeof(float));
            }
            dst->typed_color_params.push_back(std::move(p));
        }

        const auto texture_count = ReadU16Le(bytes, body_cursor);
        if (!texture_count) {
            return false;
        }
        body_cursor += 2U;
        dst->typed_texture_params.reserve(*texture_count);
        for (std::size_t i = 0U; i < *texture_count; ++i) {
            MaterialRenderPayload::TypedTextureParam p;
            if (!ReadSizedString(bytes, &body_cursor, end, &p.slot) || p.slot.empty()) {
                return false;
            }
            if (!ReadSizedString(bytes, &body_cursor, end, &p.texture_ref) || p.texture_ref.empty()) {
                return false;
            }
            dst->typed_texture_params.push_back(std::move(p));
        }
        return body_cursor == end;
    };

    const auto first_u16 = ReadU16Le(bytes, cursor);
    if (!first_u16) {
        return false;
    }
    cursor += 2U;

    MaterialRenderPayload parsed_payload = *out_payload;
    bool parsed = false;

    if (prefer_typed_v3 && *first_u16 >= 3U) {
        const std::uint16_t schema_version = *first_u16;
        std::size_t schema_cursor = cursor;
        const auto float_count = ReadU16Le(bytes, schema_cursor);
        if (float_count) {
            schema_cursor += 2U;
            parsed_payload.typed_schema_version = schema_version;
            parsed_payload.material_param_encoding = "typed-v" + std::to_string(schema_version);
            parsed = parse_typed_body(schema_cursor, *float_count, &parsed_payload);
        }
    }

    if (!parsed) {
        parsed_payload.typed_schema_version = 2U;
        parsed_payload.material_param_encoding = "typed-v2";
        parsed = parse_typed_body(cursor, *first_u16, &parsed_payload);
    }
    if (!parsed && (!prefer_typed_v3) && *first_u16 >= 3U) {
        const std::uint16_t schema_version = *first_u16;
        std::size_t schema_cursor = cursor;
        const auto float_count = ReadU16Le(bytes, schema_cursor);
        if (float_count) {
            schema_cursor += 2U;
            parsed_payload.typed_schema_version = schema_version;
            parsed_payload.material_param_encoding = "typed-v" + std::to_string(schema_version);
            parsed = parse_typed_body(schema_cursor, *float_count, &parsed_payload);
        }
    }
    if (!parsed) {
        return false;
    }

    *out_payload = std::move(parsed_payload);
    return true;
}

bool Lz4DecompressRawBounded(
    const std::vector<std::uint8_t>& src,
    std::size_t max_output_size,
    std::vector<std::uint8_t>* dst) {
    if (dst == nullptr) {
        return false;
    }
    dst->clear();
    dst->reserve(max_output_size);

    std::size_t ip = 0U;
    while (ip < src.size()) {
        const std::uint8_t token = src[ip++];
        std::size_t literal_len = static_cast<std::size_t>(token >> 4U);
        if (literal_len == 15U) {
            while (ip < src.size() && src[ip] == 255U) {
                literal_len += 255U;
                ++ip;
            }
            if (ip >= src.size()) {
                return false;
            }
            literal_len += src[ip++];
        }
        if (ip + literal_len > src.size() || dst->size() + literal_len > max_output_size) {
            return false;
        }
        dst->insert(
            dst->end(),
            src.begin() + static_cast<std::ptrdiff_t>(ip),
            src.begin() + static_cast<std::ptrdiff_t>(ip + literal_len));
        ip += literal_len;

        if (ip >= src.size()) {
            break;
        }
        if (ip + 2U > src.size()) {
            return false;
        }
        const std::size_t offset = static_cast<std::size_t>(src[ip]) | (static_cast<std::size_t>(src[ip + 1U]) << 8U);
        ip += 2U;
        if (offset == 0U || offset > dst->size()) {
            return false;
        }

        std::size_t match_len = static_cast<std::size_t>(token & 0x0FU) + 4U;
        if ((token & 0x0FU) == 15U) {
            while (ip < src.size() && src[ip] == 255U) {
                match_len += 255U;
                ++ip;
            }
            if (ip >= src.size()) {
                return false;
            }
            match_len += src[ip++];
        }
        if (dst->size() + match_len > max_output_size) {
            return false;
        }

        const std::size_t copy_from = dst->size() - offset;
        for (std::size_t i = 0; i < match_len; ++i) {
            dst->push_back((*dst)[copy_from + i]);
        }
    }

    return dst->size() == max_output_size;
}

bool DecodeCompressedSectionPayload(
    const std::vector<std::uint8_t>& bytes,
    std::size_t payload_offset,
    std::size_t payload_size,
    std::vector<std::uint8_t>* out_decoded) {
    if (out_decoded == nullptr || payload_size < 4U) {
        return false;
    }
    const auto expected_size = ReadU32Le(bytes, payload_offset);
    if (!expected_size) {
        return false;
    }
    const std::size_t compressed_offset = payload_offset + 4U;
    const std::size_t compressed_size = payload_size - 4U;
    std::vector<std::uint8_t> compressed(
        bytes.begin() + static_cast<std::ptrdiff_t>(compressed_offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(compressed_offset + compressed_size));
    return Lz4DecompressRawBounded(compressed, static_cast<std::size_t>(*expected_size), out_decoded);
}

void AppendWarningCode(AvatarPackage* pkg, const std::string& warning) {
    if (pkg == nullptr) {
        return;
    }
    const std::size_t sep = warning.find(':');
    if (sep == std::string::npos || sep == 0U) {
        return;
    }
    const auto trim = [](std::string s) {
        const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
        while (!s.empty() && is_space(static_cast<unsigned char>(s.front()))) {
            s.erase(s.begin());
        }
        while (!s.empty() && is_space(static_cast<unsigned char>(s.back()))) {
            s.pop_back();
        }
        return s;
    };
    const std::string prefix = trim(warning.substr(0U, sep));
    if (prefix.empty()) {
        return;
    }
    // Stage/event markers are useful for diagnostics text, but should not be treated as quality warning codes.
    if (prefix == "W_STAGE") {
        return;
    }

    std::string resolved_code = prefix;
    if ((prefix.rfind("W_", 0U) == 0U) || (prefix.rfind("E_", 0U) == 0U)) {
        const std::size_t payload_start = sep + 1U;
        const std::size_t sep2 = warning.find(':', payload_start);
        const std::string candidate =
            trim(warning.substr(payload_start, sep2 == std::string::npos ? std::string::npos : (sep2 - payload_start)));
        static const std::regex kCodePattern("^[A-Z0-9_]+$");
        if (!candidate.empty() && std::regex_match(candidate, kCodePattern)) {
            resolved_code = candidate;
        } else if (prefix == "W_PAYLOAD") {
            // Generic payload note without stable code: keep text warning only.
            return;
        }
    }
    if (!resolved_code.empty()) {
        if (resolved_code.size() > 8U && resolved_code.rfind("_PARTIAL") == (resolved_code.size() - 8U)) {
            return;
        }
        pkg->warning_codes.push_back(resolved_code);
    }
}

void PushWarning(AvatarPackage* pkg, const std::string& warning) {
    if (pkg == nullptr) {
        return;
    }
    pkg->warnings.push_back(warning);
    AppendWarningCode(pkg, warning);
}

}  // namespace

bool Xav2Loader::CanLoadPath(const std::string& path) const {
    const auto ext = ToLower(fs::path(path).extension().string());
    return ext == ".xav2";
}

bool Xav2Loader::CanLoadBytes(const std::vector<std::uint8_t>& head) const {
    return head.size() >= 4U && head[0] == 'X' && head[1] == 'A' && head[2] == 'V' && head[3] == '2';
}

core::Result<AvatarPackage> Xav2Loader::Load(const std::string& path) const {
    return Load(path, Xav2UnknownSectionPolicy::Warn);
}

core::Result<AvatarPackage> Xav2Loader::Load(
    const std::string& path,
    Xav2UnknownSectionPolicy unknown_section_policy) const {
    std::vector<std::uint8_t> bytes;
    if (!ReadFileBytes(path, &bytes)) {
        return core::Result<AvatarPackage>::Fail("could not open xav2 file");
    }
    if (bytes.size() < 10U) {
        return core::Result<AvatarPackage>::Fail("xav2 file is too small");
    }

    AvatarPackage pkg;
    pkg.source_type = AvatarSourceType::Xav2;
    pkg.compat_level = AvatarCompatLevel::Failed;
    pkg.parser_stage = "parse";
    pkg.primary_error_code = "NONE";
    pkg.source_path = path;
    pkg.display_name = fs::path(path).stem().string();

    if (!(bytes[0] == 'X' && bytes[1] == 'A' && bytes[2] == 'V' && bytes[3] == '2')) {
        pkg.primary_error_code = "XAV2_SCHEMA_INVALID";
        PushWarning(&pkg, "E_PARSE: XAV2_SCHEMA_INVALID: magic header mismatch.");
        return core::Result<AvatarPackage>::Ok(pkg);
    }
    const auto version = ReadU16Le(bytes, 4U);
    const auto manifest_size = ReadU32Le(bytes, 6U);
    if (!version || !manifest_size || (*version != 1U && *version != 2U && *version != 3U && *version != 4U && *version != 5U)) {
        pkg.primary_error_code = "XAV2_SCHEMA_INVALID";
        PushWarning(&pkg, "E_PARSE: XAV2_SCHEMA_INVALID: unsupported version.");
        return core::Result<AvatarPackage>::Ok(pkg);
    }
    const std::uint16_t format_version = *version;
    const std::size_t manifest_offset = 10U;
    const std::size_t manifest_end = manifest_offset + static_cast<std::size_t>(*manifest_size);
    if (manifest_end > bytes.size()) {
        pkg.primary_error_code = "XAV2_SCHEMA_INVALID";
        PushWarning(&pkg, "E_PARSE: XAV2_SCHEMA_INVALID: manifest section out of range.");
        return core::Result<AvatarPackage>::Ok(pkg);
    }

    const std::string manifest(
        reinterpret_cast<const char*>(bytes.data() + static_cast<std::ptrdiff_t>(manifest_offset)),
        *manifest_size);
    if (!HasJsonKey(manifest, "avatarId") || !HasJsonKey(manifest, "meshRefs") || !HasJsonKey(manifest, "materialRefs") ||
        !HasJsonKey(manifest, "textureRefs")) {
        pkg.primary_error_code = "XAV2_SCHEMA_INVALID";
        PushWarning(
            &pkg,
            "E_PARSE: XAV2_SCHEMA_INVALID: required keys avatarId/meshRefs/materialRefs/textureRefs are missing.");
        return core::Result<AvatarPackage>::Ok(pkg);
    }
    if (const auto display_name = ExtractStringField(manifest, "displayName"); display_name && !display_name->empty()) {
        pkg.display_name = *display_name;
    }
    PushWarning(&pkg, "W_STAGE: parse");

    pkg.parser_stage = "resolve";
    std::vector<std::string> mesh_refs;
    std::vector<std::string> material_refs;
    std::vector<std::string> texture_refs;
    if (!ParseStringArray(manifest, "meshRefs", &mesh_refs) || !ParseStringArray(manifest, "materialRefs", &material_refs) ||
        !ParseStringArray(manifest, "textureRefs", &texture_refs)) {
        pkg.primary_error_code = "XAV2_SCHEMA_INVALID";
        PushWarning(&pkg, "E_RESOLVE: XAV2_SCHEMA_INVALID: invalid reference array values.");
        return core::Result<AvatarPackage>::Ok(pkg);
    }
    PushWarning(&pkg, "W_STAGE: resolve");
    std::string manifest_material_param_encoding = "legacy-json";
    if (const auto encoding = ExtractStringField(manifest, "materialParamEncoding"); encoding && !encoding->empty()) {
        manifest_material_param_encoding = ToLower(*encoding);
    }
    const bool expects_blendshapes = ExtractBoolField(manifest, "hasBlendShapes").value_or(false);

    for (const std::string& mesh_ref : mesh_refs) {
        pkg.meshes.push_back({mesh_ref, 0U, 0U});
    }
    for (const std::string& mat_ref : material_refs) {
        pkg.materials.push_back({mat_ref, "unknown"});
    }

    std::unordered_map<std::string, MeshRenderPayload> mesh_render_sections;
    std::unordered_map<std::string, std::vector<std::uint8_t>> mesh_blob_legacy_sections;
    std::unordered_map<std::string, std::vector<std::uint8_t>> texture_sections;
    std::unordered_map<std::string, MaterialRenderPayload> material_sections;
    std::unordered_map<std::string, std::string> material_params_sections;
    std::unordered_map<std::string, MaterialRenderPayload> material_typed_sections;
    std::unordered_map<std::string, SkinRenderPayload> skin_sections;
    std::unordered_map<std::string, SkeletonRenderPayload> skeleton_pose_sections;
    std::unordered_map<std::string, SkeletonRigPayload> skeleton_rig_sections;
    std::unordered_map<std::string, BlendShapeRenderPayload> blendshape_sections;

    std::size_t cursor = manifest_end;
    while (cursor < bytes.size()) {
        const auto type = ReadU16Le(bytes, cursor);
        const auto flags = ReadU16Le(bytes, cursor + 2U);
        const auto sec_size = ReadU32Le(bytes, cursor + 4U);
        if (!type || !flags || !sec_size) {
            pkg.primary_error_code = "XAV2_SECTION_TRUNCATED";
            PushWarning(&pkg, "E_PARSE: XAV2_SECTION_TRUNCATED: section header is truncated.");
            return core::Result<AvatarPackage>::Ok(pkg);
        }

        const std::size_t payload_offset = cursor + 8U;
        const std::size_t payload_size = static_cast<std::size_t>(*sec_size);
        const std::size_t section_end = payload_offset + payload_size;
        if (payload_offset > bytes.size() || section_end > bytes.size()) {
            pkg.primary_error_code = "XAV2_SECTION_TRUNCATED";
            PushWarning(&pkg, "E_PARSE: XAV2_SECTION_TRUNCATED: section payload range out of file.");
            return core::Result<AvatarPackage>::Ok(pkg);
        }

        ++pkg.format_section_count;
        std::vector<std::uint8_t> section_payload;
        if ((*flags & kSectionFlagPayloadCompressedLz4) != 0U) {
            if (format_version < 5U) {
                pkg.primary_error_code = "XAV2_COMPRESSION_UNSUPPORTED_VERSION";
                PushWarning(&pkg, "E_PARSE: XAV2_COMPRESSION_UNSUPPORTED_VERSION: compressed section requires version >= 5.");
                return core::Result<AvatarPackage>::Ok(pkg);
            }
            if (!DecodeCompressedSectionPayload(bytes, payload_offset, payload_size, &section_payload)) {
                pkg.primary_error_code = "XAV2_COMPRESSION_DECODE_FAILED";
                PushWarning(&pkg, "E_PARSE: XAV2_COMPRESSION_DECODE_FAILED: LZ4 section decode failed.");
                return core::Result<AvatarPackage>::Ok(pkg);
            }
        } else {
            section_payload.assign(
                bytes.begin() + static_cast<std::ptrdiff_t>(payload_offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(section_end));
        }

        const auto unknown_flags = static_cast<std::uint16_t>(*flags & static_cast<std::uint16_t>(~kSectionFlagKnownMask));
        if (unknown_flags != 0U) {
            PushWarning(&pkg, "W_PARSE: XAV2_SECTION_FLAGS_NONZERO: type=" + std::to_string(*type));
        }

        if (*type == kSectionMeshRenderPayload) {
            MeshRenderPayload mesh_payload;
            if (!ParseMeshRenderPayloadSection(section_payload, 0U, section_payload.size(), &mesh_payload)) {
                pkg.primary_error_code = "XAV2_SCHEMA_INVALID";
                PushWarning(&pkg, "E_PARSE: XAV2_SCHEMA_INVALID: invalid mesh render payload section.");
                return core::Result<AvatarPackage>::Ok(pkg);
            }
            mesh_render_sections[NormalizeRefKey(mesh_payload.name)] = std::move(mesh_payload);
            ++pkg.format_decoded_section_count;
        } else if (*type == kSectionMeshBlobLegacy || *type == kSectionTextureBlob) {
            std::string name;
            std::vector<std::uint8_t> blob;
            if (!ParseBinaryPayloadSection(section_payload, 0U, section_payload.size(), &name, &blob)) {
                pkg.primary_error_code = "XAV2_SCHEMA_INVALID";
                PushWarning(&pkg, "E_PARSE: XAV2_SCHEMA_INVALID: invalid binary payload section.");
                return core::Result<AvatarPackage>::Ok(pkg);
            }
            if (*type == kSectionMeshBlobLegacy) {
                mesh_blob_legacy_sections[NormalizeRefKey(name)] = std::move(blob);
            } else {
                texture_sections[NormalizeRefKey(name)] = std::move(blob);
            }
            ++pkg.format_decoded_section_count;
        } else if (*type == kSectionMaterialOverride) {
            MaterialRenderPayload material_payload;
            if (!ParseMaterialOverrideSection(section_payload, 0U, section_payload.size(), &material_payload)) {
                pkg.primary_error_code = "XAV2_SCHEMA_INVALID";
                PushWarning(&pkg, "E_PARSE: XAV2_SCHEMA_INVALID: invalid material section.");
                return core::Result<AvatarPackage>::Ok(pkg);
            }
            material_sections[NormalizeRefKey(material_payload.name)] = std::move(material_payload);
            ++pkg.format_decoded_section_count;
        } else if (*type == kSectionMaterialShaderParams) {
            std::string material_name;
            std::string params_json;
            if (!ParseMaterialShaderParamsSection(section_payload, 0U, section_payload.size(), &material_name, &params_json)) {
                pkg.primary_error_code = "XAV2_SCHEMA_INVALID";
                PushWarning(&pkg, "E_PARSE: XAV2_SCHEMA_INVALID: invalid material shader params section.");
                return core::Result<AvatarPackage>::Ok(pkg);
            }
            material_params_sections[NormalizeRefKey(material_name)] = std::move(params_json);
            ++pkg.format_decoded_section_count;
        } else if (*type == kSectionMaterialTypedParams) {
            MaterialRenderPayload typed_payload;
            if (!ParseMaterialTypedParamsSection(
                    section_payload,
                    0U,
                    section_payload.size(),
                    manifest_material_param_encoding == "typed-v3",
                    &typed_payload)) {
                pkg.primary_error_code = "XAV2_MATERIAL_TYPED_SCHEMA_INVALID";
                PushWarning(&pkg, "E_PARSE: XAV2_MATERIAL_TYPED_SCHEMA_INVALID: invalid material typed params section.");
                return core::Result<AvatarPackage>::Ok(pkg);
            }
            if (!IsSupportedShaderFamily(typed_payload.shader_family)) {
                PushWarning(
                    &pkg,
                    "W_PARSE: XAV2_MATERIAL_TYPED_UNSUPPORTED_SHADER_FAMILY: material=" + typed_payload.name +
                        ", family=" + typed_payload.shader_family);
            }
            material_typed_sections[NormalizeRefKey(typed_payload.name)] = std::move(typed_payload);
            ++pkg.format_decoded_section_count;
        } else if (*type == kSectionSkinPayload) {
            SkinRenderPayload skin_payload;
            if (!ParseSkinPayloadSection(section_payload, 0U, section_payload.size(), &skin_payload)) {
                pkg.primary_error_code = "XAV2_SKIN_SCHEMA_INVALID";
                PushWarning(&pkg, "E_PARSE: XAV2_SKIN_SCHEMA_INVALID: invalid skin payload section.");
                return core::Result<AvatarPackage>::Ok(pkg);
            }
            skin_sections[NormalizeRefKey(skin_payload.mesh_name)] = std::move(skin_payload);
            ++pkg.format_decoded_section_count;
        } else if (*type == kSectionSkeletonPosePayload) {
            SkeletonRenderPayload skeleton_payload;
            if (!ParseSkeletonPosePayloadSection(section_payload, 0U, section_payload.size(), &skeleton_payload)) {
                pkg.primary_error_code = "XAV2_SKELETON_SCHEMA_INVALID";
                PushWarning(&pkg, "E_PARSE: XAV2_SKELETON_SCHEMA_INVALID: invalid skeleton pose payload section.");
                return core::Result<AvatarPackage>::Ok(pkg);
            }
            if ((skeleton_payload.bone_matrices_16xn.size() % 16U) != 0U) {
                PushWarning(
                    &pkg,
                    "W_PAYLOAD: XAV3_SKINNING_MATRIX_INVALID: mesh=" + skeleton_payload.mesh_name +
                        ", matrix_f32_count=" + std::to_string(skeleton_payload.bone_matrices_16xn.size()));
            }
            skeleton_pose_sections[NormalizeRefKey(skeleton_payload.mesh_name)] = std::move(skeleton_payload);
            ++pkg.format_decoded_section_count;
        } else if (*type == kSectionSkeletonRigPayload) {
            SkeletonRigPayload rig_payload;
            if (!ParseSkeletonRigPayloadSection(section_payload, 0U, section_payload.size(), &rig_payload)) {
                pkg.primary_error_code = "XAV4_RIG_SCHEMA_INVALID";
                PushWarning(&pkg, "E_PARSE: XAV4_RIG_SCHEMA_INVALID: invalid skeleton rig payload section.");
                return core::Result<AvatarPackage>::Ok(pkg);
            }
            skeleton_rig_sections[NormalizeRefKey(rig_payload.mesh_name)] = std::move(rig_payload);
            ++pkg.format_decoded_section_count;
        } else if (*type == kSectionBlendShapePayload) {
            BlendShapeRenderPayload blendshape_payload;
            if (!ParseBlendShapePayloadSection(section_payload, 0U, section_payload.size(), &blendshape_payload)) {
                pkg.primary_error_code = "XAV2_BLENDSHAPE_SCHEMA_INVALID";
                PushWarning(&pkg, "E_PARSE: XAV2_BLENDSHAPE_SCHEMA_INVALID: invalid blendshape payload section.");
                return core::Result<AvatarPackage>::Ok(pkg);
            }
            blendshape_sections[NormalizeRefKey(blendshape_payload.mesh_name)] = std::move(blendshape_payload);
            ++pkg.format_decoded_section_count;
        } else {
            ++pkg.format_unknown_section_count;
            if (unknown_section_policy == Xav2UnknownSectionPolicy::Warn) {
                PushWarning(&pkg, "W_PARSE: XAV2_UNKNOWN_SECTION: type=" + std::to_string(*type));
            } else if (unknown_section_policy == Xav2UnknownSectionPolicy::Fail) {
                pkg.primary_error_code = "XAV2_UNKNOWN_SECTION_NOT_ALLOWED";
                PushWarning(&pkg, "E_PARSE: XAV2_UNKNOWN_SECTION_NOT_ALLOWED: type=" + std::to_string(*type));
                return core::Result<AvatarPackage>::Ok(pkg);
            }
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
        const std::string key = NormalizeRefKey(mesh_ref);
        const auto it = mesh_render_sections.find(key);
        if (it != mesh_render_sections.end()) {
            payload = it->second;
            ++matched_mesh_payloads;
        } else {
            const auto legacy_it = mesh_blob_legacy_sections.find(key);
            if (legacy_it != mesh_blob_legacy_sections.end()) {
                payload.vertex_blob = legacy_it->second;
            } else {
                has_payload_gap = true;
            }
        }
        pkg.mesh_payloads.push_back(std::move(payload));

        const auto skin_it = skin_sections.find(key);
        if (skin_it != skin_sections.end()) {
            pkg.skin_payloads.push_back(skin_it->second);
        }
        const auto bs_it = blendshape_sections.find(key);
        if (bs_it != blendshape_sections.end()) {
            pkg.blendshape_payloads.push_back(bs_it->second);
        }
        const auto skel_it = skeleton_pose_sections.find(key);
        if (skel_it != skeleton_pose_sections.end()) {
            pkg.skeleton_payloads.push_back(skel_it->second);
        }
        const auto rig_it = skeleton_rig_sections.find(key);
        if (rig_it != skeleton_rig_sections.end()) {
            pkg.skeleton_rig_payloads.push_back(rig_it->second);
        }
    }

    for (const std::string& mat_ref : material_refs) {
        MaterialRenderPayload payload;
        payload.name = mat_ref;
        payload.shader_name = "MToon (minimal)";
        const std::string key = NormalizeRefKey(mat_ref);
        const auto it = material_sections.find(key);
        if (it != material_sections.end()) {
            payload = it->second;
        }
        const auto params_it = material_params_sections.find(key);
        if (params_it != material_params_sections.end()) {
            payload.shader_params_json = params_it->second;
        }
        const auto typed_it = material_typed_sections.find(key);
        if (typed_it != material_typed_sections.end()) {
            payload.shader_family = typed_it->second.shader_family;
            payload.material_param_encoding = typed_it->second.material_param_encoding;
            payload.typed_schema_version = typed_it->second.typed_schema_version;
            payload.feature_flags = typed_it->second.feature_flags;
            payload.typed_float_params = typed_it->second.typed_float_params;
            payload.typed_color_params = typed_it->second.typed_color_params;
            payload.typed_texture_params = typed_it->second.typed_texture_params;
            if (payload.shader_family != "legacy") {
                const auto has_base_color = std::any_of(
                    payload.typed_color_params.begin(),
                    payload.typed_color_params.end(),
                    [](const MaterialRenderPayload::TypedColorParam& p) { return p.id == "_BaseColor"; });
                if (!has_base_color) {
                    PushWarning(
                        &pkg,
                        "W_PAYLOAD: XAV2_MATERIAL_TYPED_MISSING_REQUIRED_PARAM: material=" + payload.name +
                            ", id=_BaseColor");
                }
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

    std::unordered_set<std::string> texture_ref_keys;
    texture_ref_keys.reserve(texture_refs.size());
    for (const auto& tex_ref : texture_refs) {
        texture_ref_keys.insert(NormalizeRefKey(tex_ref));
    }
    for (const auto& material : pkg.material_payloads) {
        for (const auto& typed_texture : material.typed_texture_params) {
            const std::string key = NormalizeRefKey(typed_texture.texture_ref);
            if (key.empty() || texture_ref_keys.find(key) != texture_ref_keys.end()) {
                continue;
            }
            PushWarning(
                &pkg,
                "W_PAYLOAD: XAV2_MATERIAL_TYPED_TEXTURE_UNRESOLVED: material=" + material.name +
                    ", slot=" + typed_texture.slot + ", ref=" + typed_texture.texture_ref);
        }
    }

    if (pkg.format_section_count == 0U) {
        PushWarning(&pkg, "W_PAYLOAD: XAV2 has no section table entries.");
    }
    if (mesh_refs.size() != matched_mesh_payloads || texture_refs.size() != matched_texture_payloads) {
        PushWarning(
            &pkg,
            "E_PAYLOAD: XAV2_ASSET_MISSING: ref/payload mismatch (mesh=" + std::to_string(matched_mesh_payloads) + "/" +
            std::to_string(mesh_refs.size()) + ", texture=" + std::to_string(matched_texture_payloads) + "/" +
            std::to_string(texture_refs.size()) + ").");
    }
    if (pkg.format_unknown_section_count > 0U) {
        pkg.missing_features.push_back("XAV2 unknown section passthrough");
    }
    if (matched_mesh_payloads != mesh_refs.size()) {
        pkg.missing_features.push_back("XAV2 mesh section payload decode coverage");
    }
    if (matched_texture_payloads != texture_refs.size()) {
        pkg.missing_features.push_back("XAV2 texture section payload decode coverage");
    }
    if (!skin_sections.empty() && pkg.skin_payloads.size() != mesh_refs.size()) {
        PushWarning(
            &pkg,
            "W_PAYLOAD: XAV2_SKIN_PARTIAL: skin sections=" + std::to_string(skin_sections.size()) +
            ", mesh refs=" + std::to_string(mesh_refs.size()));
    }
    if (format_version >= 3U && !skin_sections.empty()) {
        for (const auto& skin : pkg.skin_payloads) {
            const auto skel_it = skeleton_pose_sections.find(NormalizeRefKey(skin.mesh_name));
            if (skel_it == skeleton_pose_sections.end()) {
                PushWarning(&pkg, "W_PAYLOAD: XAV3_SKELETON_PAYLOAD_MISSING: mesh=" + skin.mesh_name);
            }
        }
        for (const auto& [skeleton_mesh_key, _] : skeleton_pose_sections) {
            if (skin_sections.find(skeleton_mesh_key) == skin_sections.end()) {
                PushWarning(&pkg, "W_PAYLOAD: XAV3_SKELETON_MESH_BIND_MISMATCH: mesh=" + skeleton_mesh_key);
            }
        }
    }
    if (format_version >= 4U && !skin_sections.empty()) {
        for (const auto& skin : pkg.skin_payloads) {
            const auto rig_it = skeleton_rig_sections.find(NormalizeRefKey(skin.mesh_name));
            if (rig_it == skeleton_rig_sections.end()) {
                PushWarning(&pkg, "W_PAYLOAD: XAV4_RIG_MISSING: mesh=" + skin.mesh_name);
            }
        }
    }
    if (expects_blendshapes && !blendshape_sections.empty() && pkg.blendshape_payloads.size() != mesh_refs.size()) {
        PushWarning(
            &pkg,
            "W_PAYLOAD: XAV2_BLENDSHAPE_PARTIAL: blendshape sections=" + std::to_string(blendshape_sections.size()) +
            ", mesh refs=" + std::to_string(mesh_refs.size()));
    }
    PushWarning(&pkg, "W_STAGE: payload");

    pkg.parser_stage = "runtime-ready";
    PushWarning(&pkg, "W_STAGE: runtime-ready");
    const bool fully_matched =
        mesh_refs.size() == matched_mesh_payloads && texture_refs.size() == matched_texture_payloads;
    pkg.compat_level = fully_matched ? AvatarCompatLevel::Full : AvatarCompatLevel::Partial;
    pkg.primary_error_code = has_payload_gap ? "XAV2_ASSET_MISSING" : "NONE";
    return core::Result<AvatarPackage>::Ok(pkg);
}

}  // namespace vsfclone::avatar
