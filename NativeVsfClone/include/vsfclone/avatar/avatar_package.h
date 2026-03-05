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

enum class Xav2UnknownSectionPolicy {
    Warn = 0,
    Ignore,
    Fail,
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

struct SkeletonRenderPayload {
    std::string mesh_name;
    std::vector<float> bone_matrices_16xn;
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
    struct TypedFloatParam {
        std::string id;
        float value = 0.0f;
    };
    struct TypedColorParam {
        std::string id;
        float rgba[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    };
    struct TypedTextureParam {
        std::string slot;
        std::string texture_ref;
    };

    std::string name;
    std::string shader_name;
    std::string shader_variant = "default";
    std::string shader_family = "legacy";
    std::string material_param_encoding = "legacy-json";
    std::uint16_t typed_schema_version = 0;
    std::uint32_t feature_flags = 0;
    std::string base_color_texture_name;
    std::string shader_params_json = "{}";
    std::string alpha_mode = "OPAQUE";
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
    std::vector<TypedFloatParam> typed_float_params;
    std::vector<TypedColorParam> typed_color_params;
    std::vector<TypedTextureParam> typed_texture_params;
};

struct MaterialDiagnosticsEntry {
    std::string material_name;
    std::string alpha_mode = "OPAQUE";
    std::string alpha_source = "unknown";
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
    bool has_mtoon_binding = false;
    bool has_base_texture = false;
    bool has_normal_texture = false;
    bool has_emission_texture = false;
    bool has_rim_texture = false;
    std::uint32_t typed_color_param_count = 0;
    std::uint32_t typed_float_param_count = 0;
    std::uint32_t typed_texture_param_count = 0;
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
    struct Bind {
        std::string mesh_name;
        std::string frame_name;
        float weight_scale = 1.0f;
    };
    std::vector<Bind> binds;
};

struct SpringBoneSummary {
    bool present = false;
    std::uint32_t spring_count = 0;
    std::uint32_t joint_count = 0;
    std::uint32_t collider_count = 0;
    std::uint32_t collider_group_count = 0;
};

enum class PhysicsColliderShape : std::uint8_t {
    Sphere = 0,
    Capsule = 1,
    Plane = 2,
    Unknown = 255,
};

struct PhysicsColliderPayload {
    std::string name;
    std::string bone_path;
    PhysicsColliderShape shape = PhysicsColliderShape::Unknown;
    float radius = 0.0f;
    float height = 0.0f;
    float local_position[3] = {0.0f, 0.0f, 0.0f};
    float local_direction[3] = {0.0f, 0.0f, 1.0f};
};

struct SpringBonePayload {
    std::string name;
    std::string root_bone_path;
    std::vector<std::string> bone_paths;
    float stiffness = 0.0f;
    float drag = 0.0f;
    float radius = 0.0f;
    float gravity[3] = {0.0f, 0.0f, 0.0f};
    std::vector<std::string> collider_refs;
    bool enabled = true;
};

struct PhysBonePayload {
    std::string name;
    std::string root_bone_path;
    std::vector<std::string> bone_paths;
    float pull = 0.0f;
    float spring = 0.0f;
    float immobile = 0.0f;
    float radius = 0.0f;
    float gravity[3] = {0.0f, 0.0f, 0.0f};
    std::vector<std::string> collider_refs;
    bool enabled = true;
};

enum class HumanoidBoneId : std::uint32_t {
    Unknown = 0,
    Hips = 1,
    Spine = 2,
    Chest = 3,
    UpperChest = 4,
    Neck = 5,
    Head = 6,
    LeftUpperArm = 7,
    RightUpperArm = 8,
};

struct SkeletonRigBonePayload {
    std::string bone_name;
    std::int32_t parent_index = -1;
    std::vector<float> local_matrix_16;
    HumanoidBoneId humanoid_id = HumanoidBoneId::Unknown;
};

struct SkeletonRigPayload {
    std::string mesh_name;
    std::vector<SkeletonRigBonePayload> bones;
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
    std::vector<SkeletonRenderPayload> skeleton_payloads;
    std::vector<SkeletonRigPayload> skeleton_rig_payloads;
    std::vector<BlendShapeRenderPayload> blendshape_payloads;
    std::vector<MaterialRenderPayload> material_payloads;
    std::vector<TextureRenderPayload> texture_payloads;
    std::vector<ExpressionState> expressions;
    SpringBoneSummary springbone_summary;
    std::vector<PhysicsColliderPayload> physics_colliders;
    std::vector<SpringBonePayload> springbone_payloads;
    std::vector<PhysBonePayload> physbone_payloads;
    std::string last_expression_summary;
    std::uint32_t last_render_draw_calls = 0;
    std::uint32_t format_section_count = 0;
    std::uint32_t format_decoded_section_count = 0;
    std::uint32_t format_unknown_section_count = 0;
    std::vector<std::string> warnings;
    std::vector<std::string> warning_codes;
    std::vector<std::string> missing_features;
    std::vector<MaterialDiagnosticsEntry> material_diagnostics;
};

}  // namespace vsfclone::avatar
