#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vsfclone::avatar {

enum class AvatarSourceType {
    Unknown = 0,
    Vrm,
    VxAvatar,
    Vxa2,
    Xav2,
    VsfAvatar,
};

enum class AvatarCompatLevel {
    Unknown = 0,
    Full,
    Partial,
    Failed,
};

struct MeshAssetSummary {
    std::string name;
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
};

struct MaterialAssetSummary {
    std::string name;
    std::string shader_name;
};

struct MeshRenderPayload {
    std::string name;
    std::vector<std::uint8_t> vertex_blob;
    std::vector<std::uint32_t> indices;
    std::uint32_t vertex_stride = 12;
    std::int32_t material_index = -1;
};

struct SkinRenderPayload {
    std::string mesh_name;
    std::vector<std::int32_t> bone_indices;
    std::vector<float> bind_poses_16xn;
    std::vector<std::uint8_t> skin_weight_blob;
};

struct BlendShapeFramePayload {
    std::string name;
    float weight = 0.0f;
    std::vector<std::uint8_t> delta_vertices;
    std::vector<std::uint8_t> delta_normals;
    std::vector<std::uint8_t> delta_tangents;
};

struct BlendShapeRenderPayload {
    std::string mesh_name;
    std::vector<BlendShapeFramePayload> frames;
};

struct MaterialRenderPayload {
    std::string name;
    std::string shader_name;
    std::string shader_variant = "default";
    std::string base_color_texture_name;
    std::string shader_params_json = "{}";
    std::string alpha_mode = "OPAQUE";
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
};

struct TextureRenderPayload {
    std::string name;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string format;
    std::vector<std::uint8_t> bytes;
};

struct ExpressionState {
    std::string name;
    std::string mapping_kind;
    float default_weight = 0.0f;
    float runtime_weight = 0.0f;
};

struct AvatarPackage {
    AvatarSourceType source_type = AvatarSourceType::Unknown;
    AvatarCompatLevel compat_level = AvatarCompatLevel::Unknown;
    std::string parser_stage;
    std::string primary_error_code;
    std::string source_path;
    std::string display_name;
    std::vector<MeshAssetSummary> meshes;
    std::vector<MaterialAssetSummary> materials;
    std::vector<MeshRenderPayload> mesh_payloads;
    std::vector<SkinRenderPayload> skin_payloads;
    std::vector<BlendShapeRenderPayload> blendshape_payloads;
    std::vector<MaterialRenderPayload> material_payloads;
    std::vector<TextureRenderPayload> texture_payloads;
    std::vector<ExpressionState> expressions;
    std::string last_expression_summary;
    std::uint32_t last_render_draw_calls = 0;
    std::uint32_t format_section_count = 0;
    std::uint32_t format_decoded_section_count = 0;
    std::uint32_t format_unknown_section_count = 0;
    std::vector<std::string> warnings;
    std::vector<std::string> missing_features;
};

}  // namespace vsfclone::avatar
