
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "vsfclone/avatar/avatar_loader_facade.h"
#include "vsfclone/avatar/avatar_package.h"

namespace fs = std::filesystem;

namespace {

constexpr std::uint16_t kXav2Version = 5U;
constexpr std::uint16_t kSectionTextureBlob = 0x0002U;
constexpr std::uint16_t kSectionMaterialOverride = 0x0003U;
constexpr std::uint16_t kSectionMeshRenderPayload = 0x0011U;
constexpr std::uint16_t kSectionMaterialShaderParams = 0x0012U;
constexpr std::uint16_t kSectionSkinPayload = 0x0013U;
constexpr std::uint16_t kSectionBlendShapePayload = 0x0014U;
constexpr std::uint16_t kSectionMaterialTypedParams = 0x0015U;
constexpr std::uint16_t kSectionSkeletonPosePayload = 0x0016U;
constexpr std::uint16_t kSectionSkeletonRigPayload = 0x0017U;
constexpr std::uint16_t kSectionSpringBonePayload = 0x0018U;
constexpr std::uint16_t kSectionPhysBonePayload = 0x0019U;
constexpr std::uint16_t kSectionPhysicsColliderPayload = 0x001AU;
constexpr std::uint16_t kSectionFlagPayloadCompressedLz4 = 0x0001U;

struct CliOptions {
    bool strict = false;
    bool enable_compression = true;
    std::string diag_json_path;
    std::string strict_allowlist_path;
    std::string perf_metrics_json_path;
    std::string input_path;
    std::string output_path;
};

struct ValidationIssue {
    std::string code;
    std::string message;
    bool error = false;
};

struct WriteStats {
    std::uint32_t section_count = 0U;
    std::uint32_t compressed_section_count = 0U;
    std::uint64_t raw_total_bytes = 0U;
    std::uint64_t written_total_bytes = 0U;
    std::uint64_t max_payload_buffer_bytes = 0U;
    std::unordered_map<std::uint16_t, std::uint64_t> section_raw_bytes;
    std::unordered_map<std::uint16_t, std::uint64_t> section_written_bytes;
    std::unordered_map<std::uint16_t, std::uint32_t> section_counts;
};

struct StrictDecision {
    bool compat_ok = true;
    std::vector<std::string> accepted_source_warning_codes;
    std::vector<std::string> rejected_source_warning_codes;
    std::vector<std::string> accepted_validation_issue_codes;
    std::vector<std::string> rejected_validation_issue_codes;
};

struct PerfMetrics {
    std::uint64_t load_ms = 0U;
    std::uint64_t validate_ms = 0U;
    std::uint64_t write_ms = 0U;
    std::uint64_t total_ms = 0U;
};

void WriteU16Le(std::ofstream* out, std::uint16_t value) {
    const std::array<std::uint8_t, 2> bytes = {
        static_cast<std::uint8_t>(value & 0xFFU),
        static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
    };
    out->write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void WriteU32Le(std::ofstream* out, std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes = {
        static_cast<std::uint8_t>(value & 0xFFU),
        static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((value >> 24U) & 0xFFU),
    };
    out->write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void AppendU16Le(std::vector<std::uint8_t>* out, std::uint16_t value) {
    out->push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out->push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void AppendU32Le(std::vector<std::uint8_t>* out, std::uint32_t value) {
    out->push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out->push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out->push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    out->push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

void AppendI32Le(std::vector<std::uint8_t>* out, std::int32_t value) {
    AppendU32Le(out, static_cast<std::uint32_t>(value));
}

void AppendF32Le(std::vector<std::uint8_t>* out, float value) {
    std::uint32_t bits = 0U;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    AppendU32Le(out, bits);
}

bool AppendSizedString(std::vector<std::uint8_t>* out, const std::string& value) {
    if (value.size() > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }
    AppendU16Le(out, static_cast<std::uint16_t>(value.size()));
    out->insert(out->end(), value.begin(), value.end());
    return true;
}

bool AppendStringList(std::vector<std::uint8_t>* out, const std::vector<std::string>& values) {
    if (values.size() > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }
    AppendU16Le(out, static_cast<std::uint16_t>(values.size()));
    for (const auto& value : values) {
        if (!AppendSizedString(out, value)) {
            return false;
        }
    }
    return true;
}

std::string EscapeJson(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 8U);
    for (char c : input) {
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

std::string BoolJson(bool value) {
    return value ? "true" : "false";
}

std::string TrimAscii(std::string value) {
    auto is_space = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string ToUpperAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        if (c >= 'a' && c <= 'z') {
            return static_cast<char>(c - 'a' + 'A');
        }
        return static_cast<char>(c);
    });
    return value;
}

std::string NormalizeCode(std::string value) {
    return ToUpperAscii(TrimAscii(std::move(value)));
}

std::uint64_t MillisecondsSince(const std::chrono::steady_clock::time_point& start) {
    const auto dur = std::chrono::steady_clock::now() - start;
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(dur).count());
}

std::string ResolvePhysicsSource(const vsfclone::avatar::AvatarPackage& pkg) {
    const bool has_spring = !pkg.springbone_payloads.empty();
    const bool has_phys = !pkg.physbone_payloads.empty();
    if (has_spring && has_phys) {
        return "mixed";
    }
    if (has_phys) {
        return "vrc";
    }
    if (has_spring) {
        return "vrm";
    }
    return "none";
}

std::string ResolveMaterialParamEncoding(const vsfclone::avatar::AvatarPackage& pkg) {
    for (const auto& mat : pkg.material_payloads) {
        if (!mat.typed_float_params.empty() || !mat.typed_color_params.empty() || !mat.typed_texture_params.empty()) {
            return "typed-v3";
        }
        if (mat.material_param_encoding.rfind("typed-v", 0U) == 0U) {
            return mat.material_param_encoding;
        }
    }
    return "legacy-json";
}

void AppendStringArrayJson(std::string* out, const std::vector<std::string>& values) {
    *out += "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0U) {
            *out += ",";
        }
        *out += "\"";
        *out += EscapeJson(values[i]);
        *out += "\"";
    }
    *out += "]";
}

std::string BuildManifest(const vsfclone::avatar::AvatarPackage& pkg) {
    std::vector<std::string> mesh_refs;
    mesh_refs.reserve(pkg.mesh_payloads.size());
    for (const auto& mesh : pkg.mesh_payloads) {
        mesh_refs.push_back(mesh.name);
    }
    std::vector<std::string> material_refs;
    material_refs.reserve(pkg.material_payloads.size());
    for (const auto& mat : pkg.material_payloads) {
        material_refs.push_back(mat.name);
    }
    std::vector<std::string> texture_refs;
    texture_refs.reserve(pkg.texture_payloads.size());
    for (const auto& tex : pkg.texture_payloads) {
        texture_refs.push_back(tex.name);
    }

    std::string out = "{";
    out += "\"schemaVersion\":1,";
    out += "\"exporterVersion\":\"0.4.0\",";
    out += "\"avatarId\":\"" + EscapeJson(fs::path(pkg.source_path).stem().string()) + "\",";
    out += "\"displayName\":\"" + EscapeJson(pkg.display_name.empty() ? fs::path(pkg.source_path).stem().string() : pkg.display_name) + "\",";
    out += "\"sourceExt\":\".vrm\",";
    out += "\"materialParamEncoding\":\"" + EscapeJson(ResolveMaterialParamEncoding(pkg)) + "\",";
    out += "\"meshRefs\":";
    AppendStringArrayJson(&out, mesh_refs);
    out += ",\"materialRefs\":";
    AppendStringArrayJson(&out, material_refs);
    out += ",\"textureRefs\":";
    AppendStringArrayJson(&out, texture_refs);
    out += ",\"strictShaderSet\":[\"lilToon\",\"Poiyomi\",\"potatoon\",\"realtoon\"],";
    out += "\"hasSkinning\":" + BoolJson(!pkg.skin_payloads.empty()) + ",";
    out += "\"hasBlendShapes\":" + BoolJson(!pkg.blendshape_payloads.empty()) + ",";
    out += "\"hasSpringBones\":" + BoolJson(!pkg.springbone_payloads.empty()) + ",";
    out += "\"hasPhysBones\":" + BoolJson(!pkg.physbone_payloads.empty()) + ",";
    out += "\"physicsSchemaVersion\":1,";
    out += "\"physicsSource\":\"" + ResolvePhysicsSource(pkg) + "\"";
    out += "}";
    return out;
}

bool BuildMeshRenderSection(const vsfclone::avatar::MeshRenderPayload& mesh, std::vector<std::uint8_t>* out) {
    if (out == nullptr || mesh.indices.size() > std::numeric_limits<std::uint32_t>::max() ||
        mesh.vertex_blob.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    out->clear();
    if (!AppendSizedString(out, mesh.name)) {
        return false;
    }
    AppendU32Le(out, mesh.vertex_stride);
    AppendI32Le(out, mesh.material_index);
    AppendU32Le(out, static_cast<std::uint32_t>(mesh.vertex_blob.size()));
    out->insert(out->end(), mesh.vertex_blob.begin(), mesh.vertex_blob.end());
    AppendU32Le(out, static_cast<std::uint32_t>(mesh.indices.size()));
    for (std::uint32_t index : mesh.indices) {
        AppendU32Le(out, index);
    }
    return true;
}

bool BuildTextureSection(const vsfclone::avatar::TextureRenderPayload& tex, std::vector<std::uint8_t>* out) {
    if (out == nullptr || tex.bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    out->clear();
    if (!AppendSizedString(out, tex.name)) {
        return false;
    }
    AppendU32Le(out, static_cast<std::uint32_t>(tex.bytes.size()));
    out->insert(out->end(), tex.bytes.begin(), tex.bytes.end());
    return true;
}

bool BuildMaterialSection(const vsfclone::avatar::MaterialRenderPayload& mat, std::vector<std::uint8_t>* out) {
    if (out == nullptr) {
        return false;
    }
    out->clear();
    if (!AppendSizedString(out, mat.name) ||
        !AppendSizedString(out, mat.shader_name) ||
        !AppendSizedString(out, mat.shader_variant.empty() ? "default" : mat.shader_variant) ||
        !AppendSizedString(out, mat.base_color_texture_name) ||
        !AppendSizedString(out, mat.alpha_mode.empty() ? "OPAQUE" : mat.alpha_mode)) {
        return false;
    }
    AppendF32Le(out, mat.alpha_cutoff);
    out->push_back(mat.double_sided ? 1U : 0U);
    return true;
}

bool BuildMaterialParamsSection(const vsfclone::avatar::MaterialRenderPayload& mat, std::vector<std::uint8_t>* out) {
    if (out == nullptr) {
        return false;
    }
    out->clear();
    return AppendSizedString(out, mat.name) &&
           AppendSizedString(out, mat.shader_params_json.empty() ? "{}" : mat.shader_params_json);
}

bool BuildMaterialTypedParamsSection(const vsfclone::avatar::MaterialRenderPayload& mat, std::vector<std::uint8_t>* out) {
    if (out == nullptr ||
        mat.typed_float_params.size() > std::numeric_limits<std::uint16_t>::max() ||
        mat.typed_color_params.size() > std::numeric_limits<std::uint16_t>::max() ||
        mat.typed_texture_params.size() > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }
    out->clear();
    if (!AppendSizedString(out, mat.name) ||
        !AppendSizedString(out, mat.shader_family.empty() ? "legacy" : mat.shader_family)) {
        return false;
    }
    AppendU32Le(out, mat.feature_flags);
    const std::uint16_t schema_version = mat.typed_schema_version == 0U ? 3U : mat.typed_schema_version;
    if (schema_version >= 3U) {
        AppendU16Le(out, schema_version);
    }
    AppendU16Le(out, static_cast<std::uint16_t>(mat.typed_float_params.size()));
    for (const auto& p : mat.typed_float_params) {
        if (!AppendSizedString(out, p.id)) {
            return false;
        }
        AppendF32Le(out, p.value);
    }
    AppendU16Le(out, static_cast<std::uint16_t>(mat.typed_color_params.size()));
    for (const auto& p : mat.typed_color_params) {
        if (!AppendSizedString(out, p.id)) {
            return false;
        }
        for (float c : p.rgba) {
            AppendF32Le(out, c);
        }
    }
    AppendU16Le(out, static_cast<std::uint16_t>(mat.typed_texture_params.size()));
    for (const auto& p : mat.typed_texture_params) {
        if (!AppendSizedString(out, p.slot) || !AppendSizedString(out, p.texture_ref)) {
            return false;
        }
    }
    return true;
}

bool BuildSkinSection(const vsfclone::avatar::SkinRenderPayload& skin, std::vector<std::uint8_t>* out) {
    if (out == nullptr || skin.bone_indices.size() > std::numeric_limits<std::uint32_t>::max() ||
        skin.bind_poses_16xn.size() > std::numeric_limits<std::uint32_t>::max() ||
        skin.skin_weight_blob.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    out->clear();
    if (!AppendSizedString(out, skin.mesh_name)) {
        return false;
    }
    AppendU32Le(out, static_cast<std::uint32_t>(skin.bone_indices.size()));
    for (std::int32_t idx : skin.bone_indices) {
        AppendI32Le(out, idx);
    }
    AppendU32Le(out, static_cast<std::uint32_t>(skin.bind_poses_16xn.size()));
    for (float v : skin.bind_poses_16xn) {
        AppendF32Le(out, v);
    }
    AppendU32Le(out, static_cast<std::uint32_t>(skin.skin_weight_blob.size()));
    out->insert(out->end(), skin.skin_weight_blob.begin(), skin.skin_weight_blob.end());
    return true;
}

bool BuildSkeletonPoseSection(const vsfclone::avatar::SkeletonRenderPayload& skeleton, std::vector<std::uint8_t>* out) {
    if (out == nullptr || skeleton.bone_matrices_16xn.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    out->clear();
    if (!AppendSizedString(out, skeleton.mesh_name)) {
        return false;
    }
    AppendU32Le(out, static_cast<std::uint32_t>(skeleton.bone_matrices_16xn.size()));
    for (float v : skeleton.bone_matrices_16xn) {
        AppendF32Le(out, v);
    }
    return true;
}

bool BuildSkeletonRigSection(const vsfclone::avatar::SkeletonRigPayload& rig, std::vector<std::uint8_t>* out) {
    if (out == nullptr || rig.bones.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    out->clear();
    if (!AppendSizedString(out, rig.mesh_name)) {
        return false;
    }
    AppendU32Le(out, static_cast<std::uint32_t>(rig.bones.size()));
    for (const auto& bone : rig.bones) {
        if (bone.local_matrix_16.size() != 16U ||
            !AppendSizedString(out, bone.bone_name)) {
            return false;
        }
        AppendI32Le(out, bone.parent_index);
        AppendU32Le(out, static_cast<std::uint32_t>(bone.local_matrix_16.size()));
        for (float v : bone.local_matrix_16) {
            AppendF32Le(out, v);
        }
    }
    return true;
}

bool BuildBlendShapeSection(const vsfclone::avatar::BlendShapeRenderPayload& blendshape, std::vector<std::uint8_t>* out) {
    if (out == nullptr || blendshape.frames.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    out->clear();
    if (!AppendSizedString(out, blendshape.mesh_name)) {
        return false;
    }
    AppendU32Le(out, static_cast<std::uint32_t>(blendshape.frames.size()));
    for (const auto& frame : blendshape.frames) {
        if (frame.delta_vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
            frame.delta_normals.size() > std::numeric_limits<std::uint32_t>::max() ||
            frame.delta_tangents.size() > std::numeric_limits<std::uint32_t>::max() ||
            !AppendSizedString(out, frame.name)) {
            return false;
        }
        AppendF32Le(out, frame.weight);
        AppendU32Le(out, static_cast<std::uint32_t>(frame.delta_vertices.size()));
        out->insert(out->end(), frame.delta_vertices.begin(), frame.delta_vertices.end());
        AppendU32Le(out, static_cast<std::uint32_t>(frame.delta_normals.size()));
        out->insert(out->end(), frame.delta_normals.begin(), frame.delta_normals.end());
        AppendU32Le(out, static_cast<std::uint32_t>(frame.delta_tangents.size()));
        out->insert(out->end(), frame.delta_tangents.begin(), frame.delta_tangents.end());
    }
    return true;
}

bool BuildPhysicsColliderSection(const vsfclone::avatar::PhysicsColliderPayload& collider, std::vector<std::uint8_t>* out) {
    if (out == nullptr) {
        return false;
    }
    out->clear();
    if (!AppendSizedString(out, collider.name) ||
        !AppendSizedString(out, collider.bone_path)) {
        return false;
    }
    out->push_back(static_cast<std::uint8_t>(collider.shape));
    AppendF32Le(out, collider.radius);
    AppendF32Le(out, collider.height);
    AppendF32Le(out, collider.local_position[0]);
    AppendF32Le(out, collider.local_position[1]);
    AppendF32Le(out, collider.local_position[2]);
    AppendF32Le(out, collider.local_direction[0]);
    AppendF32Le(out, collider.local_direction[1]);
    AppendF32Le(out, collider.local_direction[2]);
    return true;
}

bool BuildSpringBoneSection(const vsfclone::avatar::SpringBonePayload& springbone, std::vector<std::uint8_t>* out) {
    if (out == nullptr) {
        return false;
    }
    out->clear();
    if (!AppendSizedString(out, springbone.name) ||
        !AppendSizedString(out, springbone.root_bone_path) ||
        !AppendStringList(out, springbone.bone_paths)) {
        return false;
    }
    AppendF32Le(out, springbone.stiffness);
    AppendF32Le(out, springbone.drag);
    AppendF32Le(out, springbone.radius);
    AppendF32Le(out, springbone.gravity[0]);
    AppendF32Le(out, springbone.gravity[1]);
    AppendF32Le(out, springbone.gravity[2]);
    if (!AppendStringList(out, springbone.collider_refs)) {
        return false;
    }
    out->push_back(springbone.enabled ? 1U : 0U);
    return true;
}

bool BuildPhysBoneSection(const vsfclone::avatar::PhysBonePayload& physbone, std::vector<std::uint8_t>* out) {
    if (out == nullptr) {
        return false;
    }
    out->clear();
    if (!AppendSizedString(out, physbone.name) ||
        !AppendSizedString(out, physbone.root_bone_path) ||
        !AppendStringList(out, physbone.bone_paths)) {
        return false;
    }
    AppendF32Le(out, physbone.pull);
    AppendF32Le(out, physbone.spring);
    AppendF32Le(out, physbone.immobile);
    AppendF32Le(out, physbone.radius);
    AppendF32Le(out, physbone.gravity[0]);
    AppendF32Le(out, physbone.gravity[1]);
    AppendF32Le(out, physbone.gravity[2]);
    if (!AppendStringList(out, physbone.collider_refs)) {
        return false;
    }
    out->push_back(physbone.enabled ? 1U : 0U);
    return true;
}

bool ShouldCompressSection(std::uint16_t section_type, std::size_t payload_size, bool enabled) {
    if (!enabled || payload_size < 256U) {
        return false;
    }
    return section_type == kSectionMeshRenderPayload ||
           section_type == kSectionTextureBlob ||
           section_type == kSectionSkinPayload ||
           section_type == kSectionBlendShapePayload;
}

bool IsAlreadyCompressedTexture(const std::vector<std::uint8_t>& payload) {
    if (payload.size() < 16U) {
        return false;
    }
    // texture payload: [name_len(2)][name][blob_size(4)][blob]
    const std::size_t name_len = static_cast<std::size_t>(payload[0]) | (static_cast<std::size_t>(payload[1]) << 8U);
    if (2U + name_len + 4U >= payload.size()) {
        return false;
    }
    const std::size_t blob_offset = 2U + name_len + 4U;
    if (blob_offset + 4U > payload.size()) {
        return false;
    }
    const auto* p = payload.data() + static_cast<std::ptrdiff_t>(blob_offset);
    // png
    if (p[0] == 0x89U && p[1] == 0x50U && p[2] == 0x4EU && p[3] == 0x47U) {
        return true;
    }
    // jpg
    if (p[0] == 0xFFU && p[1] == 0xD8U) {
        return true;
    }
    // dds
    if (p[0] == 0x44U && p[1] == 0x44U && p[2] == 0x53U && p[3] == 0x20U) {
        return true;
    }
    // ktx
    if (p[0] == 0xABU && p[1] == 0x4BU && p[2] == 0x54U && p[3] == 0x58U) {
        return true;
    }
    return false;
}

bool ShouldAttemptCompression(std::uint16_t section_type, const std::vector<std::uint8_t>& payload, bool enabled) {
    if (!ShouldCompressSection(section_type, payload.size(), enabled)) {
        return false;
    }
    if (section_type == kSectionTextureBlob && IsAlreadyCompressedTexture(payload)) {
        return false;
    }
    if (payload.size() < 1024U) {
        return false;
    }
    return true;
}

inline std::uint32_t ReadU32Le(const std::vector<std::uint8_t>& data, std::size_t pos) {
    return static_cast<std::uint32_t>(data[pos]) |
           (static_cast<std::uint32_t>(data[pos + 1U]) << 8U) |
           (static_cast<std::uint32_t>(data[pos + 2U]) << 16U) |
           (static_cast<std::uint32_t>(data[pos + 3U]) << 24U);
}

bool TryLz4CompressRaw(const std::vector<std::uint8_t>& src, std::vector<std::uint8_t>* out) {
    if (out == nullptr) {
        return false;
    }
    out->clear();
    if (src.empty()) {
        return true;
    }

    constexpr std::size_t kHashBits = 16U;
    constexpr std::size_t kHashSize = 1U << kHashBits;
    std::array<int, kHashSize> hash_table {};
    hash_table.fill(-1);

    const auto emit_length = [&](std::size_t len) {
        while (len >= 255U) {
            out->push_back(255U);
            len -= 255U;
        }
        out->push_back(static_cast<std::uint8_t>(len));
    };

    std::size_t anchor = 0U;
    std::size_t ip = 0U;
    const std::size_t n = src.size();
    const std::size_t mflimit = (n >= 12U) ? (n - 12U) : 0U;

    while (ip <= mflimit) {
        const std::uint32_t seq = ReadU32Le(src, ip);
        const std::size_t hash = ((seq * 2654435761U) >> (32U - kHashBits)) & (kHashSize - 1U);
        const int ref = hash_table[hash];
        hash_table[hash] = static_cast<int>(ip);

        if (ref < 0) {
            ++ip;
            continue;
        }
        const std::size_t ref_pos = static_cast<std::size_t>(ref);
        if (ip <= ref_pos || (ip - ref_pos) > 65535U) {
            ++ip;
            continue;
        }
        if (ref_pos + 4U > n || src[ref_pos] != src[ip] || src[ref_pos + 1U] != src[ip + 1U] ||
            src[ref_pos + 2U] != src[ip + 2U] || src[ref_pos + 3U] != src[ip + 3U]) {
            ++ip;
            continue;
        }

        std::size_t match_len = 4U;
        while ((ip + match_len) < n && src[ref_pos + match_len] == src[ip + match_len]) {
            ++match_len;
        }

        const std::size_t literal_len = ip - anchor;
        const std::size_t token_pos = out->size();
        out->push_back(0U);
        std::uint8_t token = static_cast<std::uint8_t>((literal_len < 15U ? literal_len : 15U) << 4U);
        if (literal_len >= 15U) {
            emit_length(literal_len - 15U);
        }
        out->insert(out->end(), src.begin() + static_cast<std::ptrdiff_t>(anchor), src.begin() + static_cast<std::ptrdiff_t>(ip));
        const std::size_t offset = ip - ref_pos;
        out->push_back(static_cast<std::uint8_t>(offset & 0xFFU));
        out->push_back(static_cast<std::uint8_t>((offset >> 8U) & 0xFFU));

        const std::size_t match_token = match_len - 4U;
        token |= static_cast<std::uint8_t>(match_token < 15U ? match_token : 15U);
        if (match_token >= 15U) {
            emit_length(match_token - 15U);
        }
        out->at(token_pos) = token;

        ip += match_len;
        anchor = ip;
        if (ip > mflimit) {
            break;
        }
    }

    const std::size_t literal_len = n - anchor;
    const std::size_t token_pos = out->size();
    out->push_back(0U);
    std::uint8_t token = static_cast<std::uint8_t>((literal_len < 15U ? literal_len : 15U) << 4U);
    if (literal_len >= 15U) {
        std::size_t rem = literal_len - 15U;
        while (rem >= 255U) {
            out->push_back(255U);
            rem -= 255U;
        }
        out->push_back(static_cast<std::uint8_t>(rem));
    }
    out->insert(out->end(), src.begin() + static_cast<std::ptrdiff_t>(anchor), src.end());
    out->at(token_pos) = token;
    return true;
}

std::vector<std::uint8_t> BuildCompressedEnvelope(std::size_t uncompressed_size, const std::vector<std::uint8_t>& compressed) {
    std::vector<std::uint8_t> out;
    out.reserve(4U + compressed.size());
    AppendU32Le(&out, static_cast<std::uint32_t>(uncompressed_size));
    out.insert(out.end(), compressed.begin(), compressed.end());
    return out;
}

bool WriteSection(
    std::ofstream* out,
    std::uint16_t type,
    const std::vector<std::uint8_t>& payload,
    bool allow_compression,
    WriteStats* stats) {
    if (out == nullptr || payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    std::uint16_t flags = 0U;
    std::vector<std::uint8_t> serialized_payload = payload;
    if (ShouldAttemptCompression(type, payload, allow_compression)) {
        std::vector<std::uint8_t> compressed;
        if (TryLz4CompressRaw(payload, &compressed) && (compressed.size() + 4U) < payload.size()) {
            flags |= kSectionFlagPayloadCompressedLz4;
            serialized_payload = BuildCompressedEnvelope(payload.size(), compressed);
        }
    }

    WriteU16Le(out, type);
    WriteU16Le(out, flags);
    WriteU32Le(out, static_cast<std::uint32_t>(serialized_payload.size()));
    if (!serialized_payload.empty()) {
        out->write(reinterpret_cast<const char*>(serialized_payload.data()), static_cast<std::streamsize>(serialized_payload.size()));
    }
    if (!out->good()) {
        return false;
    }

    if (stats != nullptr) {
        ++stats->section_count;
        stats->raw_total_bytes += static_cast<std::uint64_t>(payload.size());
        stats->written_total_bytes += static_cast<std::uint64_t>(serialized_payload.size());
        stats->section_raw_bytes[type] += static_cast<std::uint64_t>(payload.size());
        stats->section_written_bytes[type] += static_cast<std::uint64_t>(serialized_payload.size());
        stats->section_counts[type] += 1U;
        if ((flags & kSectionFlagPayloadCompressedLz4) != 0U) {
            ++stats->compressed_section_count;
        }
    }
    return true;
}

void PushIssue(std::vector<ValidationIssue>* out, std::string code, std::string message, bool error) {
    if (out == nullptr) {
        return;
    }
    out->push_back({std::move(code), std::move(message), error});
}

std::vector<ValidationIssue> ValidatePackageForExport(const vsfclone::avatar::AvatarPackage& pkg) {
    std::vector<ValidationIssue> issues;
    std::unordered_set<std::string> texture_names;
    texture_names.reserve(pkg.texture_payloads.size());
    for (const auto& tex : pkg.texture_payloads) {
        texture_names.insert(tex.name);
    }

    std::unordered_set<std::string> collider_names;
    collider_names.reserve(pkg.physics_colliders.size());
    for (const auto& collider : pkg.physics_colliders) {
        if (!collider_names.insert(collider.name).second) {
            PushIssue(&issues, "XAV2_PHYSICS_COLLIDER_DUPLICATE", "duplicate collider name: " + collider.name, false);
        }
    }

    for (const auto& mat : pkg.material_payloads) {
        for (const auto& tp : mat.typed_texture_params) {
            if (!tp.texture_ref.empty() && texture_names.find(tp.texture_ref) == texture_names.end()) {
                PushIssue(&issues, "XAV2_MATERIAL_TYPED_TEXTURE_UNRESOLVED",
                          "material=" + mat.name + ", textureRef=" + tp.texture_ref, false);
            }
        }
    }

    auto check_collider_refs = [&](const std::string& name, const std::vector<std::string>& refs, const char* kind) {
        for (const auto& ref : refs) {
            if (!ref.empty() && collider_names.find(ref) == collider_names.end()) {
                PushIssue(&issues, "XAV2_PHYSICS_REF_MISSING",
                          std::string(kind) + "=" + name + ", collider=" + ref, false);
            }
        }
    };
    for (const auto& spring : pkg.springbone_payloads) {
        check_collider_refs(spring.name, spring.collider_refs, "springBone");
    }
    for (const auto& phys : pkg.physbone_payloads) {
        check_collider_refs(phys.name, phys.collider_refs, "physBone");
    }

    for (const auto& rig : pkg.skeleton_rig_payloads) {
        for (const auto& bone : rig.bones) {
            if (bone.local_matrix_16.size() != 16U) {
                PushIssue(&issues, "XAV4_RIG_MATRIX_INVALID",
                          "mesh=" + rig.mesh_name + ", bone=" + bone.bone_name + ", matrixCount=" +
                              std::to_string(bone.local_matrix_16.size()),
                          true);
            }
        }
    }
    return issues;
}

std::unordered_set<std::string> LoadAllowlist(const std::string& path, std::string* out_error) {
    std::unordered_set<std::string> allowlist;
    if (path.empty()) {
        return allowlist;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        *out_error = "could not open strict allowlist file: " + path;
        return {};
    }
    std::string line;
    while (std::getline(in, line)) {
        std::size_t at = 0U;
        while (at < line.size()) {
            std::size_t comma = line.find(',', at);
            std::string token = (comma == std::string::npos) ? line.substr(at) : line.substr(at, comma - at);
            token = NormalizeCode(std::move(token));
            if (!token.empty() && token[0] != '#') {
                allowlist.insert(token);
            }
            if (comma == std::string::npos) {
                break;
            }
            at = comma + 1U;
        }
    }
    return allowlist;
}

StrictDecision EvaluateStrictDecision(
    const CliOptions& options,
    const vsfclone::avatar::AvatarPackage& pkg,
    const std::vector<ValidationIssue>& issues,
    const std::unordered_set<std::string>& allowlist) {
    StrictDecision decision;
    decision.compat_ok = (pkg.compat_level == vsfclone::avatar::AvatarCompatLevel::Full);
    if (!options.strict) {
        return decision;
    }

    auto classify = [&](const std::string& raw_code, std::vector<std::string>* accepted, std::vector<std::string>* rejected) {
        const std::string code = NormalizeCode(raw_code);
        if (code.empty()) {
            return;
        }
        if (!allowlist.empty() && allowlist.find(code) != allowlist.end()) {
            accepted->push_back(code);
        } else {
            rejected->push_back(code);
        }
    };

    for (const auto& code : pkg.warning_codes) {
        classify(code, &decision.accepted_source_warning_codes, &decision.rejected_source_warning_codes);
    }
    for (const auto& issue : issues) {
        classify(issue.code, &decision.accepted_validation_issue_codes, &decision.rejected_validation_issue_codes);
    }
    return decision;
}

bool WriteDiagJson(
    const std::string& path,
    const CliOptions& options,
    const vsfclone::avatar::AvatarPackage& pkg,
    const WriteStats& stats,
    const std::vector<ValidationIssue>& issues,
    const StrictDecision& strict_decision,
    const PerfMetrics& perf,
    const std::string& output_path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << "{";
    out << "\"input\":\"" << EscapeJson(options.input_path) << "\",";
    out << "\"output\":\"" << EscapeJson(output_path) << "\",";
    out << "\"strict\":" << BoolJson(options.strict) << ",";
    out << "\"strictAllowlistPath\":\"" << EscapeJson(options.strict_allowlist_path) << "\",";
    out << "\"compressionEnabled\":" << BoolJson(options.enable_compression) << ",";
    out << "\"xav2Version\":" << kXav2Version << ",";
    out << "\"sectionCount\":" << stats.section_count << ",";
    out << "\"compressedSectionCount\":" << stats.compressed_section_count << ",";
    out << "\"rawTotalBytes\":" << stats.raw_total_bytes << ",";
    out << "\"writtenTotalBytes\":" << stats.written_total_bytes << ",";
    out << "\"maxPayloadBufferBytes\":" << stats.max_payload_buffer_bytes << ",";
    out << "\"payloadCounts\":{";
    out << "\"mesh\":" << pkg.mesh_payloads.size() << ",";
    out << "\"material\":" << pkg.material_payloads.size() << ",";
    out << "\"texture\":" << pkg.texture_payloads.size() << ",";
    out << "\"skin\":" << pkg.skin_payloads.size() << ",";
    out << "\"skeleton\":" << pkg.skeleton_payloads.size() << ",";
    out << "\"rig\":" << pkg.skeleton_rig_payloads.size() << ",";
    out << "\"blendShape\":" << pkg.blendshape_payloads.size() << ",";
    out << "\"springBone\":" << pkg.springbone_payloads.size() << ",";
    out << "\"physBone\":" << pkg.physbone_payloads.size() << ",";
    out << "\"collider\":" << pkg.physics_colliders.size();
    out << "},";
    out << "\"warningCodes\":[";
    for (std::size_t i = 0; i < pkg.warning_codes.size(); ++i) {
        if (i > 0U) {
            out << ",";
        }
        out << "\"" << EscapeJson(pkg.warning_codes[i]) << "\"";
    }
    out << "],";
    out << "\"strictPolicy\":{";
    out << "\"compatOk\":" << BoolJson(strict_decision.compat_ok) << ",";
    out << "\"acceptedSourceWarningCodes\":[";
    for (std::size_t i = 0; i < strict_decision.accepted_source_warning_codes.size(); ++i) {
        if (i > 0U) {
            out << ",";
        }
        out << "\"" << EscapeJson(strict_decision.accepted_source_warning_codes[i]) << "\"";
    }
    out << "],";
    out << "\"rejectedSourceWarningCodes\":[";
    for (std::size_t i = 0; i < strict_decision.rejected_source_warning_codes.size(); ++i) {
        if (i > 0U) {
            out << ",";
        }
        out << "\"" << EscapeJson(strict_decision.rejected_source_warning_codes[i]) << "\"";
    }
    out << "],";
    out << "\"acceptedValidationIssueCodes\":[";
    for (std::size_t i = 0; i < strict_decision.accepted_validation_issue_codes.size(); ++i) {
        if (i > 0U) {
            out << ",";
        }
        out << "\"" << EscapeJson(strict_decision.accepted_validation_issue_codes[i]) << "\"";
    }
    out << "],";
    out << "\"rejectedValidationIssueCodes\":[";
    for (std::size_t i = 0; i < strict_decision.rejected_validation_issue_codes.size(); ++i) {
        if (i > 0U) {
            out << ",";
        }
        out << "\"" << EscapeJson(strict_decision.rejected_validation_issue_codes[i]) << "\"";
    }
    out << "]";
    out << "},";
    out << "\"perf\":{";
    out << "\"loadMs\":" << perf.load_ms << ",";
    out << "\"validateMs\":" << perf.validate_ms << ",";
    out << "\"writeMs\":" << perf.write_ms << ",";
    out << "\"totalMs\":" << perf.total_ms;
    out << "},";
    out << "\"sectionMetrics\":[";
    bool first_metric = true;
    for (const auto& [section_type, count] : stats.section_counts) {
        if (!first_metric) {
            out << ",";
        }
        first_metric = false;
        const auto raw_it = stats.section_raw_bytes.find(section_type);
        const auto written_it = stats.section_written_bytes.find(section_type);
        const std::uint64_t raw_bytes = (raw_it != stats.section_raw_bytes.end()) ? raw_it->second : 0U;
        const std::uint64_t written_bytes = (written_it != stats.section_written_bytes.end()) ? written_it->second : 0U;
        out << "{";
        out << "\"type\":" << section_type << ",";
        out << "\"count\":" << count << ",";
        out << "\"rawBytes\":" << raw_bytes << ",";
        out << "\"writtenBytes\":" << written_bytes;
        out << "}";
    }
    out << "],";
    out << "\"issues\":[";
    for (std::size_t i = 0; i < issues.size(); ++i) {
        if (i > 0U) {
            out << ",";
        }
        out << "{";
        out << "\"code\":\"" << EscapeJson(issues[i].code) << "\",";
        out << "\"error\":" << BoolJson(issues[i].error) << ",";
        out << "\"message\":\"" << EscapeJson(issues[i].message) << "\"";
        out << "}";
    }
    out << "]";
    out << "}";
    return out.good();
}

bool WritePerfMetricsJson(
    const std::string& path,
    const CliOptions& options,
    const WriteStats& stats,
    const PerfMetrics& perf) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << "{";
    out << "\"input\":\"" << EscapeJson(options.input_path) << "\",";
    out << "\"output\":\"" << EscapeJson(options.output_path) << "\",";
    out << "\"compressionEnabled\":" << BoolJson(options.enable_compression) << ",";
    out << "\"sections\":" << stats.section_count << ",";
    out << "\"compressedSections\":" << stats.compressed_section_count << ",";
    out << "\"rawTotalBytes\":" << stats.raw_total_bytes << ",";
    out << "\"writtenTotalBytes\":" << stats.written_total_bytes << ",";
    out << "\"maxPayloadBufferBytes\":" << stats.max_payload_buffer_bytes << ",";
    out << "\"timingMs\":{";
    out << "\"load\":" << perf.load_ms << ",";
    out << "\"validate\":" << perf.validate_ms << ",";
    out << "\"write\":" << perf.write_ms << ",";
    out << "\"total\":" << perf.total_ms;
    out << "}";
    out << "}";
    return out.good();
}

std::optional<CliOptions> ParseArgs(int argc, char** argv, std::string* out_error) {
    CliOptions options;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--strict") {
            options.strict = true;
            continue;
        }
        if (arg == "--no-compress") {
            options.enable_compression = false;
            continue;
        }
        if (arg == "--diag-json") {
            if (i + 1 >= argc) {
                *out_error = "--diag-json requires a path";
                return std::nullopt;
            }
            options.diag_json_path = argv[++i];
            continue;
        }
        if (arg == "--strict-allowlist") {
            if (i + 1 >= argc) {
                *out_error = "--strict-allowlist requires a path";
                return std::nullopt;
            }
            options.strict_allowlist_path = argv[++i];
            continue;
        }
        if (arg == "--perf-metrics-json") {
            if (i + 1 >= argc) {
                *out_error = "--perf-metrics-json requires a path";
                return std::nullopt;
            }
            options.perf_metrics_json_path = argv[++i];
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            *out_error = "unknown option: " + arg;
            return std::nullopt;
        }
        positional.push_back(arg);
    }

    if (positional.size() != 2U) {
        *out_error =
            "Usage: vrm_to_xav2 [--strict] [--strict-allowlist <path>] [--no-compress] [--diag-json <path>] [--perf-metrics-json <path>] <input.vrm> <output.xav2>";
        return std::nullopt;
    }
    options.input_path = positional[0];
    options.output_path = positional[1];
    return options;
}

bool IsStrictFailure(
    const CliOptions& options,
    const StrictDecision& strict_decision,
    std::string* out_reason) {
    if (!options.strict) {
        return false;
    }
    if (!strict_decision.compat_ok) {
        *out_reason = "strict mode requires compat=Full";
        return true;
    }
    if (!strict_decision.rejected_source_warning_codes.empty()) {
        *out_reason =
            "strict mode rejected source warning code: " + strict_decision.rejected_source_warning_codes.front();
        return true;
    }
    if (!strict_decision.rejected_validation_issue_codes.empty()) {
        *out_reason =
            "strict mode rejected validation issue code: " + strict_decision.rejected_validation_issue_codes.front();
        return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    const auto t0 = std::chrono::steady_clock::now();
    std::string arg_error;
    const auto options_opt = ParseArgs(argc, argv, &arg_error);
    if (!options_opt) {
        std::cerr << arg_error << "\n";
        return 1;
    }
    const CliOptions options = *options_opt;

    PerfMetrics perf;
    vsfclone::avatar::AvatarLoaderFacade facade;
    const auto t_load = std::chrono::steady_clock::now();
    auto loaded = facade.Load(options.input_path);
    perf.load_ms = MillisecondsSince(t_load);
    if (!loaded.ok) {
        std::cerr << "Load failed: " << loaded.error << "\n";
        return 1;
    }
    const auto& pkg = loaded.value;
    if (pkg.source_type != vsfclone::avatar::AvatarSourceType::Vrm) {
        std::cerr << "Input must be a .vrm file.\n";
        return 1;
    }
    if (pkg.compat_level == vsfclone::avatar::AvatarCompatLevel::Failed) {
        std::cerr << "VRM parse failed with code: " << pkg.primary_error_code << "\n";
        return 1;
    }

    const auto t_validate = std::chrono::steady_clock::now();
    const std::vector<ValidationIssue> issues = ValidatePackageForExport(pkg);
    std::string allowlist_error;
    const auto strict_allowlist = LoadAllowlist(options.strict_allowlist_path, &allowlist_error);
    if (!allowlist_error.empty()) {
        std::cerr << allowlist_error << "\n";
        return 1;
    }
    const StrictDecision strict_decision = EvaluateStrictDecision(options, pkg, issues, strict_allowlist);
    perf.validate_ms = MillisecondsSince(t_validate);
    std::string strict_reason;
    if (IsStrictFailure(options, strict_decision, &strict_reason)) {
        std::cerr << "Strict validation failed: " << strict_reason << "\n";
        return 1;
    }

    std::ofstream out(options.output_path, std::ios::binary);
    if (!out) {
        std::cerr << "Could not open output file: " << options.output_path << "\n";
        return 1;
    }

    const std::string manifest = BuildManifest(pkg);
    out.write("XAV2", 4);
    WriteU16Le(&out, kXav2Version);
    WriteU32Le(&out, static_cast<std::uint32_t>(manifest.size()));
    out.write(manifest.data(), static_cast<std::streamsize>(manifest.size()));

    WriteStats stats;
    std::vector<std::uint8_t> payload;
    stats.max_payload_buffer_bytes = std::max<std::uint64_t>(stats.max_payload_buffer_bytes, payload.capacity());
    const auto t_write = std::chrono::steady_clock::now();

    auto write_payload = [&](std::uint16_t type, const std::string& name) -> bool {
        stats.max_payload_buffer_bytes = std::max<std::uint64_t>(stats.max_payload_buffer_bytes, payload.capacity());
        if (!WriteSection(&out, type, payload, options.enable_compression, &stats)) {
            std::cerr << "Failed to write " << name << " section.\n";
            return false;
        }
        return true;
    };

    for (const auto& mesh : pkg.mesh_payloads) {
        if (!BuildMeshRenderSection(mesh, &payload) || !write_payload(kSectionMeshRenderPayload, "mesh")) {
            return 1;
        }
    }
    for (const auto& tex : pkg.texture_payloads) {
        if (!BuildTextureSection(tex, &payload) || !write_payload(kSectionTextureBlob, "texture")) {
            return 1;
        }
    }
    for (const auto& mat : pkg.material_payloads) {
        if (!BuildMaterialSection(mat, &payload) || !write_payload(kSectionMaterialOverride, "material")) {
            return 1;
        }
        if (!BuildMaterialParamsSection(mat, &payload) || !write_payload(kSectionMaterialShaderParams, "material-params")) {
            return 1;
        }
        if (!mat.typed_float_params.empty() || !mat.typed_color_params.empty() || !mat.typed_texture_params.empty()) {
            if (!BuildMaterialTypedParamsSection(mat, &payload) || !write_payload(kSectionMaterialTypedParams, "material-typed")) {
                return 1;
            }
        }
    }
    for (const auto& skin : pkg.skin_payloads) {
        if (!BuildSkinSection(skin, &payload) || !write_payload(kSectionSkinPayload, "skin")) {
            return 1;
        }
    }
    for (const auto& skeleton : pkg.skeleton_payloads) {
        if (!BuildSkeletonPoseSection(skeleton, &payload) || !write_payload(kSectionSkeletonPosePayload, "skeleton-pose")) {
            return 1;
        }
    }
    for (const auto& rig : pkg.skeleton_rig_payloads) {
        if (!BuildSkeletonRigSection(rig, &payload) || !write_payload(kSectionSkeletonRigPayload, "skeleton-rig")) {
            return 1;
        }
    }
    for (const auto& blendshape : pkg.blendshape_payloads) {
        if (!BuildBlendShapeSection(blendshape, &payload) || !write_payload(kSectionBlendShapePayload, "blendshape")) {
            return 1;
        }
    }
    for (const auto& collider : pkg.physics_colliders) {
        if (!BuildPhysicsColliderSection(collider, &payload) || !write_payload(kSectionPhysicsColliderPayload, "physics-collider")) {
            return 1;
        }
    }
    for (const auto& spring : pkg.springbone_payloads) {
        if (!BuildSpringBoneSection(spring, &payload) || !write_payload(kSectionSpringBonePayload, "springbone")) {
            return 1;
        }
    }
    for (const auto& phys : pkg.physbone_payloads) {
        if (!BuildPhysBoneSection(phys, &payload) || !write_payload(kSectionPhysBonePayload, "physbone")) {
            return 1;
        }
    }

    if (!out.good()) {
        std::cerr << "Write failed.\n";
        return 1;
    }
    perf.write_ms = MillisecondsSince(t_write);
    perf.total_ms = MillisecondsSince(t0);

    if (!options.diag_json_path.empty()) {
        if (!WriteDiagJson(options.diag_json_path, options, pkg, stats, issues, strict_decision, perf, options.output_path)) {
            std::cerr << "Failed to write diagnostics json: " << options.diag_json_path << "\n";
            return 1;
        }
    }
    if (!options.perf_metrics_json_path.empty()) {
        if (!WritePerfMetricsJson(options.perf_metrics_json_path, options, stats, perf)) {
            std::cerr << "Failed to write perf metrics json: " << options.perf_metrics_json_path << "\n";
            return 1;
        }
    }

    std::cout << "Wrote XAV2(v" << kXav2Version << "): " << options.output_path << "\n";
    std::cout << "Sections=" << stats.section_count
              << ", CompressedSections=" << stats.compressed_section_count
              << ", Meshes=" << pkg.mesh_payloads.size()
              << ", Materials=" << pkg.material_payloads.size()
              << ", Textures=" << pkg.texture_payloads.size()
              << ", BlendShapes=" << pkg.blendshape_payloads.size()
              << ", SpringBones=" << pkg.springbone_payloads.size()
              << ", PhysBones=" << pkg.physbone_payloads.size()
              << "\n";
    std::cout << "TimingMs: load=" << perf.load_ms
              << ", validate=" << perf.validate_ms
              << ", write=" << perf.write_ms
              << ", total=" << perf.total_ms
              << "\n";
    if (options.strict) {
        std::cout << "StrictPolicy: sourceAccepted=" << strict_decision.accepted_source_warning_codes.size()
                  << ", sourceRejected=" << strict_decision.rejected_source_warning_codes.size()
                  << ", issuesAccepted=" << strict_decision.accepted_validation_issue_codes.size()
                  << ", issuesRejected=" << strict_decision.rejected_validation_issue_codes.size()
                  << "\n";
    }
    if (!issues.empty()) {
        std::cout << "Warnings=" << issues.size() << " (use --strict to fail on validation warnings)\n";
    }
    return 0;
}
