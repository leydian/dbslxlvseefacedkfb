#include "animiq/nativecore/api.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "animiq/avatar/avatar_loader_facade.h"
#include "animiq/avatar/avatar_package.h"
#include "animiq/osc/osc_endpoint.h"
#include "animiq/stream/spout_sender.h"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <dxgi.h>
#include <wincodec.h>
#endif

namespace animiq::nativecore {

namespace {

using animiq::avatar::AvatarCompatLevel;
using animiq::avatar::AvatarPackage;
using animiq::avatar::AvatarSourceType;
using animiq::avatar::ExpressionState;
using animiq::avatar::SkinningMatrixConvention;
using animiq::avatar::TransformConfidence;

#if defined(_WIN32)
struct WindowRenderState {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* device_context = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct GpuMeshResource {
    std::string mesh_name;
    ID3D11Buffer* vertex_buffer = nullptr;
    ID3D11Buffer* index_buffer = nullptr;
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
    std::int32_t material_index = -1;
    DirectX::XMFLOAT3 center = {0.0f, 0.0f, 0.0f};
    std::uint32_t vertex_stride = 12;
    DirectX::XMFLOAT3 bounds_min = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 bounds_max = {0.0f, 0.0f, 0.0f};
    std::vector<std::uint8_t> bind_pose_vertex_blob;
    std::vector<std::uint8_t> base_vertex_blob;
    std::vector<std::uint8_t> deformed_vertex_blob;
};

enum class RenderFamilyBackendKind : std::uint8_t {
    Common = 0,
    Liltoon = 1,
    Mtoon = 2,
    Poiyomi = 3,
    Standard = 4
};

const char* RenderFamilyBackendName(RenderFamilyBackendKind kind) {
    switch (kind) {
        case RenderFamilyBackendKind::Liltoon:
            return "liltoon";
        case RenderFamilyBackendKind::Mtoon:
            return "mtoon";
        case RenderFamilyBackendKind::Poiyomi:
            return "poiyomi";
        case RenderFamilyBackendKind::Standard:
            return "standard";
        case RenderFamilyBackendKind::Common:
        default:
            return "common";
    }
}

RenderFamilyBackendKind ResolveFamilyBackendRequest(const std::string& shader_family) {
    if (shader_family == "liltoon") {
        return RenderFamilyBackendKind::Liltoon;
    }
    if (shader_family == "mtoon") {
        return RenderFamilyBackendKind::Mtoon;
    }
    if (shader_family == "poiyomi") {
        return RenderFamilyBackendKind::Poiyomi;
    }
    if (shader_family == "standard") {
        return RenderFamilyBackendKind::Standard;
    }
    return RenderFamilyBackendKind::Common;
}

struct GpuMaterialResource {
    std::string shader_family = "legacy";
    std::string shader_variant = "default";
    std::string pass_flags = "base";
    RenderFamilyBackendKind backend_requested = RenderFamilyBackendKind::Common;
    RenderFamilyBackendKind backend_selected = RenderFamilyBackendKind::Common;
    bool backend_fallback_applied = false;
    std::string backend_fallback_reason = "none";
    std::string alpha_mode = "OPAQUE";
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
    bool enable_depth_pass = true;
    bool enable_shadow_pass = false;
    bool enable_base_pass = true;
    bool enable_outline_pass = false;
    bool enable_emission_pass = false;
    std::array<float, 4U> base_color = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4U> shade_color = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4U> emission_color = {0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 4U> rim_color = {0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 4U> matcap_color = {0.0f, 0.0f, 0.0f, 1.0f};
    float shade_mix = 0.0f;
    float emission_strength = 0.0f;
    float normal_strength = 0.0f;
    float rim_strength = 0.0f;
    float matcap_strength = 0.0f;
    float rim_power = 2.0f;
    float outline_width = 0.0f;
    float outline_lighting_mix = 0.0f;
    float uv_anim_scroll_x = 0.0f;
    float uv_anim_scroll_y = 0.0f;
    float uv_anim_rotation = 0.0f;
    bool uv_anim_enabled = false;
    ID3D11ShaderResourceView* base_color_srv = nullptr;
    ID3D11ShaderResourceView* normal_srv = nullptr;
    ID3D11ShaderResourceView* rim_srv = nullptr;
    ID3D11ShaderResourceView* emission_srv = nullptr;
    ID3D11ShaderResourceView* matcap_srv = nullptr;
    ID3D11ShaderResourceView* uv_anim_mask_srv = nullptr;
};

struct RendererResources {
    ID3D11Device* device = nullptr;
    ID3D11VertexShader* vertex_shader = nullptr;
    ID3D11PixelShader* pixel_shader_common = nullptr;
    ID3D11PixelShader* pixel_shader_liltoon = nullptr;
    ID3D11PixelShader* pixel_shader_mtoon = nullptr;
    ID3D11PixelShader* pixel_shader_poiyomi = nullptr;
    ID3D11PixelShader* pixel_shader_standard = nullptr;
    ID3D11InputLayout* input_layout = nullptr;
    ID3D11Buffer* constant_buffer = nullptr;
    ID3D11RasterizerState* raster_cull_back = nullptr;
    ID3D11RasterizerState* raster_cull_front = nullptr;
    ID3D11RasterizerState* raster_cull_none = nullptr;
    // CCW-front variants for glTF/VRM assets (glTF spec requires CCW winding
    // for front faces, which is opposite of DirectX's default CW convention).
    ID3D11RasterizerState* raster_cull_back_ccw = nullptr;
    ID3D11RasterizerState* raster_cull_front_ccw = nullptr;
    ID3D11DepthStencilState* depth_write = nullptr;
    ID3D11DepthStencilState* depth_read = nullptr;
    ID3D11BlendState* blend_opaque = nullptr;
    ID3D11BlendState* blend_alpha = nullptr;
    ID3D11BlendState* blend_additive = nullptr;
    ID3D11BlendState* blend_depth_only = nullptr;
    ID3D11SamplerState* linear_sampler = nullptr;
    ID3D11SamplerState* shadow_sampler = nullptr;
    ID3D11Texture2D* depth_texture = nullptr;
    ID3D11DepthStencilView* depth_dsv = nullptr;
    ID3D11Texture2D* shadow_texture = nullptr;
    ID3D11DepthStencilView* shadow_dsv = nullptr;
    ID3D11ShaderResourceView* shadow_srv = nullptr;
    std::uint32_t depth_width = 0;
    std::uint32_t depth_height = 0;
    std::uint32_t shadow_resolution = 0U;
    std::unordered_map<std::uint64_t, std::vector<GpuMeshResource>> avatar_meshes;
    std::unordered_map<std::uint64_t, std::vector<GpuMaterialResource>> avatar_materials;
};
#endif

struct SecondaryMotionChainRuntime {
    std::string name;
    bool is_vrc = false;
    std::vector<std::size_t> target_mesh_indices;
    float stiffness = 0.35f;
    float drag = 0.25f;
    float gravity_y = -0.15f;
    float radius = 0.02f;
    bool enabled = true;
    float velocity_x = 0.0f;
    float velocity_y = 0.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float rest_offset_x = 0.0f;
    float rest_offset_y = 0.0f;
    float fixed_dt_accumulator = 0.0f;
    std::uint32_t unsupported_collider_count = 0U;
    std::uint32_t last_substeps = 0U;
    std::uint32_t corrected_count = 0U;
};

struct AvatarSecondaryMotionState {
    bool initialized = false;
    bool warnings_emitted = false;
    std::vector<SecondaryMotionChainRuntime> chains;
    std::uint32_t active_chain_count = 0U;
    std::uint32_t corrected_chain_count = 0U;
    std::uint32_t disabled_chain_count = 0U;
    std::uint32_t unsupported_collider_chain_count = 0U;
    std::uint32_t constraint_hit_count = 0U;
    std::uint32_t damping_event_count = 0U;
    float avg_substeps = 0.0f;
    float avg_offset_magnitude = 0.0f;
    float peak_offset_magnitude = 0.0f;
};

struct AvatarArmPoseState {
    bool initialized = false;
    NcPoseBoneOffset left_upper_arm {};
    NcPoseBoneOffset right_upper_arm {};
    NcPoseBoneOffset left_shoulder {};
    NcPoseBoneOffset right_shoulder {};
    NcPoseBoneOffset left_lower_arm {};
    NcPoseBoneOffset right_lower_arm {};
    NcPoseBoneOffset left_hand {};
    NcPoseBoneOffset right_hand {};
};

struct AvatarPreviewOrientationMetrics {
    std::int32_t contract_preview_yaw_deg = 0;
    std::uint32_t transform_confidence_level = 0U;
    std::uint32_t is_vrm_origin_miq = 0U;
    std::uint32_t preview_bounds_excluded_mesh_count = 0U;
    std::uint32_t preview_hair_candidate_mesh_count = 0U;
    float preview_hair_head_alignment_score = -1.0f;
};

struct CoreState {
    bool initialized = false;
    std::uint64_t next_avatar_handle = 1;
    std::unordered_map<std::uint64_t, AvatarPackage> avatars;
    std::unordered_map<std::uint64_t, std::string> avatar_preview_debug;
    std::unordered_map<std::uint64_t, AvatarPreviewOrientationMetrics> avatar_preview_orientation_metrics;
    std::unordered_map<std::uint64_t, AvatarSecondaryMotionState> secondary_motion_states;
    std::unordered_map<std::uint64_t, AvatarArmPoseState> arm_pose_states;
    std::unordered_set<std::uint64_t> arm_pose_auto_rollback_handles;
    std::unordered_set<std::uint64_t> render_ready_avatars;
    avatar::AvatarLoaderFacade loader;
    stream::SpoutSender spout;
    osc::OscEndpoint osc;
    NcTrackingFrame latest_tracking {};
    bool tracking_weights_dirty = false;
    NcRenderQualityOptions render_quality {};
    NcLightingOptions lighting_options {};
    std::array<NcPoseBoneOffset, 15U> pose_offsets {};
    float last_frame_ms = 0.0f;
    float last_gpu_frame_ms = 0.0f;
    float last_cpu_frame_ms = 0.0f;
    float last_material_resolve_ms = 0.0f;
    std::uint32_t last_pass_count = 0U;
    std::uint32_t last_depth_pass_count = 0U;
    std::uint32_t last_shadow_pass_count = 0U;
    std::uint32_t last_base_pass_count = 0U;
    std::uint32_t last_outline_pass_count = 0U;
    std::uint32_t last_emission_pass_count = 0U;
    std::uint32_t last_blend_pass_count = 0U;
    float runtime_time_seconds = 0.0f;

#if defined(_WIN32)
    std::unordered_map<void*, WindowRenderState> window_targets;
    RendererResources renderer;
#endif

    NcResultCode last_error_code = NC_OK;
    std::string last_error_subsystem = "none";
    std::string last_error_message;
    bool last_error_recoverable = true;
};

NcRenderQualityOptions MakeDefaultRenderQualityOptions() {
    NcRenderQualityOptions options {};
    options.camera_mode = NC_CAMERA_MODE_AUTO_FIT_BUST;
    options.framing_target = 0.80f;
    options.headroom = 0.10f;
    options.yaw_deg = 0.0f;
    options.fov_deg = 40.0f;
    options.background_rgba[0] = 0.55f;
    options.background_rgba[1] = 0.55f;
    options.background_rgba[2] = 0.55f;
    options.background_rgba[3] = 1.0f;
    options.quality_profile = NC_RENDER_QUALITY_BALANCED;
    options.show_debug_overlay = 0U;
    return options;
}

NcLightingOptions MakeDefaultLightingOptions() {
    NcLightingOptions options {};
    options.light_position[0] = -0.72f;
    options.light_position[1] = 19.35f;
    options.light_position[2] = 3.7f;
    options.light_euler_deg[0] = 19.201f;
    options.light_euler_deg[1] = 175.0f;
    options.light_euler_deg[2] = 2.582f;
    options.intensity = 12.5f;
    options.range = 16.4f;
    options.spot_angle_deg = 16.6f;
    options.inner_spot_angle_deg = 0.0f;
    options.shadow_strength = 1.0f;
    options.shadow_bias = 0.1f;
    options.shadow_normal_bias = 0.0f;
    options.shadow_near_plane = 8.5f;
    options.shadow_resolution = 8192U;
    // Higher ambient so dark materials (hair, black clothing) retain detail
    // and don't "black out" or "crush" into a single flat shape.
    options.ambient_intensity = 0.55f;
    options.enable_sun_light = 0U;
    options.enable_shadow = 1U;
    return options;
}

NcRenderQualityOptions SanitizeRenderQualityOptions(const NcRenderQualityOptions& options) {
    NcRenderQualityOptions out = options;
    if (out.camera_mode < NC_CAMERA_MODE_AUTO_FIT_FULL || out.camera_mode > NC_CAMERA_MODE_MANUAL) {
        out.camera_mode = NC_CAMERA_MODE_AUTO_FIT_BUST;
    }
    out.framing_target = std::max(0.35f, std::min(0.95f, out.framing_target));
    out.headroom = std::max(0.0f, std::min(0.40f, out.headroom));
    out.yaw_deg = std::max(-180.0f, std::min(180.0f, out.yaw_deg));
    out.fov_deg = std::max(20.0f, std::min(80.0f, out.fov_deg));
    for (int i = 0; i < 4; ++i) {
        out.background_rgba[i] = std::max(0.0f, std::min(1.0f, out.background_rgba[i]));
    }
    if (out.quality_profile > NC_RENDER_QUALITY_FAST_FALLBACK) {
        out.quality_profile = NC_RENDER_QUALITY_DEFAULT;
    }
    out.show_debug_overlay = out.show_debug_overlay > 0U ? 1U : 0U;
    return out;
}

std::uint32_t NormalizeShadowResolution(std::uint32_t value) {
    if (value <= 256U) {
        return 256U;
    }
    if (value >= 8192U) {
        return 8192U;
    }
    std::uint32_t out = 256U;
    while (out < value && out < 8192U) {
        out <<= 1U;
    }
    return out;
}

NcLightingOptions SanitizeLightingOptions(const NcLightingOptions& options) {
    NcLightingOptions out = options;
    for (int i = 0; i < 3; ++i) {
        out.light_position[i] = std::isfinite(out.light_position[i]) ? out.light_position[i] : 0.0f;
        out.light_euler_deg[i] = std::isfinite(out.light_euler_deg[i]) ? out.light_euler_deg[i] : 0.0f;
    }
    out.intensity = std::max(0.0f, std::min(64.0f, out.intensity));
    out.range = std::max(0.25f, std::min(200.0f, out.range));
    out.spot_angle_deg = std::max(1.0f, std::min(179.0f, out.spot_angle_deg));
    out.inner_spot_angle_deg = std::max(0.0f, std::min(out.spot_angle_deg, out.inner_spot_angle_deg));
    out.shadow_strength = std::max(0.0f, std::min(1.0f, out.shadow_strength));
    out.shadow_bias = std::max(0.0f, std::min(8.0f, out.shadow_bias));
    out.shadow_normal_bias = std::max(0.0f, std::min(4.0f, out.shadow_normal_bias));
    out.shadow_near_plane = std::max(0.01f, std::min(100.0f, out.shadow_near_plane));
    out.shadow_resolution = NormalizeShadowResolution(out.shadow_resolution);
    out.ambient_intensity = std::max(0.0f, std::min(2.0f, out.ambient_intensity));
    out.enable_sun_light = out.enable_sun_light > 0U ? 1U : 0U;
    out.enable_shadow = out.enable_shadow > 0U ? 1U : 0U;
    return out;
}

CoreState g_state;
std::mutex g_mutex;

const char* RenderQualityModeName(std::uint32_t quality_profile);
std::vector<std::string> TokenizeLooseFlags(std::string text);
bool HasLooseToken(const std::vector<std::string>& tokens, const char* token);
std::string NormalizeShaderFamilyKey(const std::string& shader_family);

void CopyString(char* dst, std::size_t dst_size, const std::string& src) {
    if (dst == nullptr || dst_size == 0U) {
        return;
    }
    const std::size_t count = std::min(dst_size - 1U, src.size());
    std::memcpy(dst, src.data(), count);
    dst[count] = '\0';
}

std::string BuildMaterialDiagSummary(const avatar::MaterialDiagnosticsEntry& diag) {
    std::ostringstream ss;
    ss << diag.material_name
       << ", alphaMode=" << diag.alpha_mode
       << ", alphaSource=" << diag.alpha_source
       << ", alphaCutoff=" << diag.alpha_cutoff
       << ", doubleSided=" << (diag.double_sided ? "true" : "false")
       << ", mtoonBinding=" << (diag.has_mtoon_binding ? "true" : "false")
       << ", tex(base/normal/emission/rim)="
       << (diag.has_base_texture ? "1" : "0") << "/"
       << (diag.has_normal_texture ? "1" : "0") << "/"
       << (diag.has_emission_texture ? "1" : "0") << "/"
       << (diag.has_rim_texture ? "1" : "0")
       << ", typed(c/f/t)="
       << diag.typed_color_param_count << "/"
       << diag.typed_float_param_count << "/"
       << diag.typed_texture_param_count;
    return ss.str();
}

struct MaterialModeCounts {
    std::uint32_t opaque = 0U;
    std::uint32_t mask = 0U;
    std::uint32_t blend = 0U;
};

struct MtoonDiagnosticsCounts {
    std::uint32_t advanced = 0U;
    std::uint32_t fallback = 0U;
};

struct ParityDiagnosticsSummary {
    float score = 0.0f;
    std::string variant_id = "none";
    std::string fallback_reason = "none";
};

MaterialModeCounts CountMaterialModes(const AvatarPackage& pkg) {
    MaterialModeCounts counts {};
    auto bump = [&](std::string alpha_mode) {
        std::transform(alpha_mode.begin(), alpha_mode.end(), alpha_mode.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        if (alpha_mode == "MASK") {
            ++counts.mask;
            return;
        }
        if (alpha_mode == "BLEND") {
            ++counts.blend;
            return;
        }
        ++counts.opaque;
    };
    if (!pkg.material_diagnostics.empty()) {
        for (const auto& diag : pkg.material_diagnostics) {
            bump(diag.alpha_mode);
        }
        return counts;
    }
    for (const auto& payload : pkg.material_payloads) {
        bump(payload.alpha_mode);
    }
    return counts;
}

MtoonDiagnosticsCounts CountMtoonDiagnostics(const AvatarPackage& pkg) {
    MtoonDiagnosticsCounts counts {};
    for (const auto& diag : pkg.material_diagnostics) {
        if (diag.has_mtoon_binding) {
            const std::uint32_t typed_total =
                diag.typed_color_param_count + diag.typed_float_param_count + diag.typed_texture_param_count;
            if (typed_total >= 12U || diag.has_rim_texture || diag.has_emission_texture) {
                ++counts.advanced;
            }
        } else {
            ++counts.fallback;
        }
    }
    return counts;
}

ParityDiagnosticsSummary ComputeParityDiagnostics(
    const AvatarPackage& pkg,
    std::uint32_t quality_profile) {
    ParityDiagnosticsSummary out {};
    if (pkg.material_payloads.empty()) {
        out.fallback_reason = "no-material-payload";
        return out;
    }

    auto normalize = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    };
    auto is_parity_family = [&](const std::string& family) {
        const std::string key = normalize(family);
        return key == "liltoon" || key == "poiyomi" || key == "mtoon" || key == "standard";
    };

    std::uint32_t matched = 0U;
    std::vector<std::string> reasons;
    for (const auto& material : pkg.material_payloads) {
        const bool typed_v4 = normalize(material.material_param_encoding) == "typed-v4" &&
                              material.typed_schema_version >= 4U;
        const bool family_ok = is_parity_family(material.shader_family);
        const std::string alpha_mode = normalize(material.alpha_mode);
        const bool alpha_allows_depth = alpha_mode != "blend";
        const std::vector<std::string> pass_tokens = TokenizeLooseFlags(material.pass_flags + " " + material.keyword_set);
        const bool has_depth_token =
            HasLooseToken(pass_tokens, "depth") ||
            HasLooseToken(pass_tokens, "depthonly") ||
            HasLooseToken(pass_tokens, "zprepass");
        const bool has_shadow_token =
            HasLooseToken(pass_tokens, "shadow") ||
            HasLooseToken(pass_tokens, "shadowcaster") ||
            HasLooseToken(pass_tokens, "castshadow") ||
            HasLooseToken(pass_tokens, "forwardadd") ||
            HasLooseToken(pass_tokens, "forward_add");
        const bool has_base_color = std::any_of(
            material.typed_color_params.begin(),
            material.typed_color_params.end(),
            [](const avatar::MaterialRenderPayload::TypedColorParam& p) { return p.id == "_BaseColor"; });
        if (typed_v4 && family_ok && has_base_color) {
            ++matched;
        } else {
            if (!typed_v4) {
                reasons.push_back("non-v4");
            }
            if (!family_ok) {
                reasons.push_back("family");
            }
            if (!has_base_color) {
                reasons.push_back("base-color");
            }
        }
        if (alpha_allows_depth && !has_depth_token) {
            reasons.push_back("missing_depth_pass");
        }
        if (alpha_allows_depth && family_ok && !has_shadow_token) {
            reasons.push_back("missing_shadow_pass");
        }
    }

    out.score = static_cast<float>(matched) / static_cast<float>(pkg.material_payloads.size());
    const auto& first = pkg.material_payloads.front();
    out.variant_id = normalize(first.shader_family) + "." +
                     (first.shader_variant.empty() ? "default" : normalize(first.shader_variant)) + "." +
                     std::string(RenderQualityModeName(quality_profile));
    if (reasons.empty()) {
        out.fallback_reason = "none";
    } else {
        std::ostringstream ss;
        const std::size_t max_reasons = std::min<std::size_t>(reasons.size(), 6U);
        for (std::size_t i = 0U; i < max_reasons; ++i) {
            if (i > 0U) {
                ss << ",";
            }
            ss << reasons[i];
        }
        out.fallback_reason = ss.str();
    }
    return out;
}

std::uint32_t TransformConfidenceLevel(TransformConfidence confidence);

void FillAvatarRuntimeMetricsV2(const AvatarPackage& pkg, std::uint64_t handle, NcAvatarRuntimeMetricsV2* out_info) {
    if (out_info == nullptr) {
        return;
    }
    std::memset(out_info, 0, sizeof(*out_info));
    out_info->target_frame_ms = 1000.0f / 60.0f;
    out_info->last_frame_ms = g_state.last_frame_ms;
    CopyString(out_info->physics_solver, sizeof(out_info->physics_solver), "spring-v2-damped");
    CopyString(out_info->mtoon_runtime_mode, sizeof(out_info->mtoon_runtime_mode), "mtoon-advanced-runtime");
    out_info->contract_preview_yaw_deg = std::max(-180, std::min(180, static_cast<int>(pkg.recommended_preview_yaw_deg)));
    out_info->transform_confidence_level = TransformConfidenceLevel(pkg.transform_confidence);
    out_info->is_vrm_origin_miq = (pkg.source_type == AvatarSourceType::Miq && pkg.source_ext == ".vrm") ? 1U : 0U;
    out_info->preview_hair_head_alignment_score = -1.0f;

    const auto orientation_it = g_state.avatar_preview_orientation_metrics.find(handle);
    if (orientation_it != g_state.avatar_preview_orientation_metrics.end()) {
        out_info->contract_preview_yaw_deg = orientation_it->second.contract_preview_yaw_deg;
        out_info->transform_confidence_level = orientation_it->second.transform_confidence_level;
        out_info->is_vrm_origin_miq = orientation_it->second.is_vrm_origin_miq;
        out_info->preview_bounds_excluded_mesh_count = orientation_it->second.preview_bounds_excluded_mesh_count;
        out_info->preview_hair_candidate_mesh_count = orientation_it->second.preview_hair_candidate_mesh_count;
        out_info->preview_hair_head_alignment_score = orientation_it->second.preview_hair_head_alignment_score;
    }

    const auto state_it = g_state.secondary_motion_states.find(handle);
    if (state_it != g_state.secondary_motion_states.end()) {
        out_info->spring_active_chain_count = state_it->second.active_chain_count;
        out_info->spring_constraint_hit_count = state_it->second.constraint_hit_count;
        out_info->spring_damping_event_count = state_it->second.damping_event_count;
        out_info->spring_avg_offset_magnitude = state_it->second.avg_offset_magnitude;
        out_info->spring_peak_offset_magnitude = state_it->second.peak_offset_magnitude;
    }

    std::uint32_t blend_count = 0U;
    std::uint32_t mask_count = 0U;
#if defined(_WIN32)
    const auto material_it = g_state.renderer.avatar_materials.find(handle);
    if (material_it != g_state.renderer.avatar_materials.end()) {
        for (const auto& material : material_it->second) {
            if (material.outline_width > 0.0005f) {
                ++out_info->mtoon_outline_material_count;
            }
            if (material.uv_anim_enabled) {
                ++out_info->mtoon_uv_anim_material_count;
            }
            if (material.matcap_srv != nullptr || material.matcap_strength > 0.001f) {
                ++out_info->mtoon_matcap_material_count;
            }
            std::string alpha_mode = material.alpha_mode;
            std::transform(alpha_mode.begin(), alpha_mode.end(), alpha_mode.begin(), [](unsigned char c) {
                return static_cast<char>(std::toupper(c));
            });
            if (alpha_mode == "BLEND") {
                ++blend_count;
            } else if (alpha_mode == "MASK") {
                ++mask_count;
            }
        }
        out_info->mtoon_blend_material_count = blend_count;
        out_info->mtoon_mask_material_count = mask_count;
        return;
    }
#endif

    for (const auto& diag : pkg.material_diagnostics) {
        std::string alpha_mode = diag.alpha_mode;
        std::transform(alpha_mode.begin(), alpha_mode.end(), alpha_mode.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        if (alpha_mode == "BLEND") {
            ++blend_count;
        } else if (alpha_mode == "MASK") {
            ++mask_count;
        }
    }
    out_info->mtoon_blend_material_count = blend_count;
    out_info->mtoon_mask_material_count = mask_count;
    out_info->mtoon_outline_material_count = pkg.material_diagnostics.empty() ? 0U : 1U;
    out_info->mtoon_uv_anim_material_count = pkg.material_diagnostics.empty() ? 0U : 1U;
    out_info->mtoon_matcap_material_count = pkg.material_diagnostics.empty() ? 0U : 1U;
}

struct WarningMeta {
    const char* severity = "unknown";
    const char* category = "unknown";
    bool critical = false;
};

WarningMeta ClassifyWarningCode(std::string code) {
    std::transform(code.begin(), code.end(), code.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    static const std::unordered_set<std::string> kCriticalCodes = {
        "miq_skinning_static_disabled",
        "miq_skinning_fallback_skipped_no_skeleton",
        "miq_material_typed_texture_unresolved",
        "material_index_oob_skipped",
        "xav3_skeleton_payload_missing",
        "xav3_skeleton_mesh_bind_mismatch",
        "xav3_skinning_matrix_invalid",
        "miq_unknown_section_not_allowed",
    };

    WarningMeta meta {};
    if (code.rfind("e_", 0U) == 0U) {
        meta.severity = "error";
        meta.category = "schema";
        meta.critical = true;
        return meta;
    }
    if (code == "w_stage") {
        meta.severity = "info";
        meta.category = "stage";
        return meta;
    }
    if (code == "w_layout" || code == "w_offset" || code == "w_recon_summary") {
        meta.severity = "info";
        meta.category = "layout";
        return meta;
    }
    if (code.rfind("w_", 0U) == 0U) {
        meta.severity = "warn";
        meta.category = "payload";
        return meta;
    }
    if (code == "skinning_matrix_convention_applied" || code == "skinning_matrix_convention_selected") {
        meta.severity = "info";
        meta.category = "render";
        return meta;
    }
    if (code == "miq_skinning_convention_ambiguous") {
        meta.severity = "warn";
        meta.category = "render";
        return meta;
    }
    if (code == "skinning_static_disabled") {
        meta.severity = "warn";
        meta.category = "render";
        meta.critical = false;
        return meta;
    }
    if (code == "vrm_material_safe_fallback_applied" ||
        code == "vrm_mtoon_matcap_unresolved" ||
        code == "vrm_material_texture_unresolved") {
        meta.severity = "warn";
        meta.category = "render";
        meta.critical = false;
        return meta;
    }
    if (code.rfind("arm_pose_", 0U) == 0U ||
        code.rfind("shadow_disabled_", 0U) == 0U ||
        code == "shadow_pass_not_reported" ||
        code.rfind("tracking_", 0U) == 0U ||
        code == "expression_count_zero") {
        meta.severity = "warn";
        meta.category = "render";
        meta.critical = false;
        return meta;
    }
    if (code.rfind("nc_set_", 0U) == 0U) {
        meta.severity = "error";
        meta.category = "render";
        meta.critical = true;
        return meta;
    }
    if (code.rfind("miq_", 0U) == 0U || code.rfind("xav3_", 0U) == 0U || code.rfind("xav4_", 0U) == 0U) {
        meta.severity = "warn";
        meta.category = "render";
        meta.critical = kCriticalCodes.find(code) != kCriticalCodes.end();
        return meta;
    }
    if (code.rfind("vrm_", 0U) == 0U) {
        meta.severity = "warn";
        meta.category = "payload";
        return meta;
    }
    return meta;
}

bool IsPreferredWarningCodeForUi(const std::string& code) {
    std::string lowered = code;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lowered.find("material") != std::string::npos) {
        return true;
    }
    if (lowered.find("mtoon") != std::string::npos) {
        return true;
    }
    const auto meta = ClassifyWarningCode(lowered);
    return std::string(meta.category) == "render";
}

bool IsPreferredWarningMessageForUi(const std::string& warning) {
    std::string lowered = warning;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lowered.find("w_render:") != std::string::npos ||
           lowered.find("w_material:") != std::string::npos ||
           lowered.find("vrm_mtoon_matcap_unresolved") != std::string::npos ||
           lowered.find("vrm_texture_missing") != std::string::npos;
}

void SetError(NcResultCode code, const char* subsystem, const std::string& message, bool recoverable) {
    g_state.last_error_code = code;
    g_state.last_error_subsystem = subsystem != nullptr ? subsystem : "unknown";
    g_state.last_error_message = message;
    g_state.last_error_recoverable = recoverable;
}

void ClearError() {
    SetError(NC_OK, "none", "", true);
}

NcAvatarFormatHint ToFormatHint(AvatarSourceType source_type) {
    switch (source_type) {
        case AvatarSourceType::Vrm:
            return NC_AVATAR_FORMAT_VRM;
        case AvatarSourceType::VxAvatar:
            return NC_AVATAR_FORMAT_VXAVATAR;
        case AvatarSourceType::Vxa2:
            return NC_AVATAR_FORMAT_VXA2;
        case AvatarSourceType::Miq:
            return NC_AVATAR_FORMAT_MIQ;
        case AvatarSourceType::VsfAvatar:
            return NC_AVATAR_FORMAT_VSFAVATAR;
        default:
            return NC_AVATAR_FORMAT_AUTO;
    }
}

const char* AvatarSourceTypeName(AvatarSourceType source_type) {
    switch (source_type) {
        case AvatarSourceType::Vrm:
            return "vrm";
        case AvatarSourceType::VxAvatar:
            return "vxavatar";
        case AvatarSourceType::Vxa2:
            return "vxa2";
        case AvatarSourceType::Miq:
            return "miq";
        case AvatarSourceType::VsfAvatar:
            return "vsfavatar";
        default:
            return "unknown";
    }
}

const char* TransformConfidenceName(TransformConfidence confidence) {
    switch (confidence) {
        case TransformConfidence::Low:
            return "low";
        case TransformConfidence::Medium:
            return "medium";
        case TransformConfidence::High:
            return "high";
        default:
            return "unknown";
    }
}

std::uint32_t TransformConfidenceLevel(TransformConfidence confidence) {
    switch (confidence) {
        case TransformConfidence::Low:
            return 1U;
        case TransformConfidence::Medium:
            return 2U;
        case TransformConfidence::High:
            return 3U;
        default:
            return 0U;
    }
}

int PreviewYawDegreesForAvatarSource(AvatarSourceType source_type) {
    // Runtime preview yaw is the single source-of-truth for front-view alignment.
    if (source_type == AvatarSourceType::Miq) {
        return 0;
    }
    if (source_type == AvatarSourceType::Vrm) {
        return 180;
    }
    return 180;
}

float PreviewYawRadiansForAvatarSource(AvatarSourceType source_type) {
    constexpr float kPi = 3.14159265358979323846f;
    return static_cast<float>(PreviewYawDegreesForAvatarSource(source_type)) * (kPi / 180.0f);
}

int PreviewYawDegreesForAvatarPackage(const AvatarPackage& pkg, const char** reason_out) {
    if (reason_out != nullptr) {
        *reason_out = "default";
    }
    if (pkg.transform_confidence != TransformConfidence::Unknown &&
        pkg.recommended_preview_yaw_deg >= -180 &&
        pkg.recommended_preview_yaw_deg <= 180) {
        if (reason_out != nullptr) {
            *reason_out = "contract_recommended";
        }
        return std::max(-180, std::min(180, static_cast<int>(pkg.recommended_preview_yaw_deg)));
    }
    if (pkg.source_type == AvatarSourceType::Miq && pkg.source_ext == ".vrm") {
        if (reason_out != nullptr) {
            *reason_out = "legacy_miq_vrm_source_180";
        }
        return 180;
    }
    if (reason_out != nullptr) {
        *reason_out = "legacy_source_default";
    }
    return PreviewYawDegreesForAvatarSource(pkg.source_type);
}

float PreviewYawRadiansForAvatarPackage(const AvatarPackage& pkg, const char** reason_out) {
    constexpr float kPi = 3.14159265358979323846f;
    return static_cast<float>(PreviewYawDegreesForAvatarPackage(pkg, reason_out)) * (kPi / 180.0f);
}

NcCompatLevel ToCompatLevel(AvatarCompatLevel compat_level) {
    switch (compat_level) {
        case AvatarCompatLevel::Full:
            return NC_COMPAT_FULL;
        case AvatarCompatLevel::Partial:
            return NC_COMPAT_PARTIAL;
        case AvatarCompatLevel::Failed:
            return NC_COMPAT_FAILED;
        default:
            return NC_COMPAT_UNKNOWN;
    }
}

void FillAvatarInfo(const AvatarPackage& pkg, std::uint64_t handle, NcAvatarInfo* out_info) {
    if (out_info == nullptr) {
        return;
    }

    std::memset(out_info, 0, sizeof(*out_info));
    out_info->handle = handle;
    out_info->detected_format = ToFormatHint(pkg.source_type);
    out_info->compat_level = ToCompatLevel(pkg.compat_level);
    out_info->mesh_count = static_cast<std::uint32_t>(pkg.meshes.size());
    out_info->material_count = static_cast<std::uint32_t>(pkg.materials.size());
    out_info->mesh_payload_count = static_cast<std::uint32_t>(pkg.mesh_payloads.size());
    out_info->material_payload_count = static_cast<std::uint32_t>(pkg.material_payloads.size());
    out_info->texture_payload_count = static_cast<std::uint32_t>(pkg.texture_payloads.size());
    out_info->format_section_count = pkg.format_section_count;
    out_info->format_decoded_section_count = pkg.format_decoded_section_count;
    out_info->format_unknown_section_count = pkg.format_unknown_section_count;
    out_info->warning_count = static_cast<std::uint32_t>(pkg.warnings.size());
    out_info->warning_code_count = static_cast<std::uint32_t>(pkg.warning_codes.size());
    std::uint32_t critical_warning_count = 0U;
    for (const auto& code : pkg.warning_codes) {
        if (ClassifyWarningCode(code).critical) {
            ++critical_warning_count;
        }
    }
    out_info->critical_warning_count = critical_warning_count;
    out_info->material_diag_count = static_cast<std::uint32_t>(pkg.material_diagnostics.size());
    const auto mode_counts = CountMaterialModes(pkg);
    out_info->opaque_material_count = mode_counts.opaque;
    out_info->mask_material_count = mode_counts.mask;
    out_info->blend_material_count = mode_counts.blend;
    out_info->missing_feature_count = static_cast<std::uint32_t>(pkg.missing_features.size());
    out_info->expression_count = static_cast<std::uint32_t>(pkg.expressions.size());
    if (out_info->detected_format == NC_AVATAR_FORMAT_VRM && out_info->expression_count == 0U) {
        out_info->expression_count = 3U;
    }
    out_info->last_render_draw_calls = pkg.last_render_draw_calls;
    {
        const auto state_it = g_state.secondary_motion_states.find(handle);
        if (state_it != g_state.secondary_motion_states.end()) {
            out_info->spring_active_chain_count = state_it->second.active_chain_count;
            out_info->spring_corrected_chain_count = state_it->second.corrected_chain_count;
            out_info->spring_disabled_chain_count = state_it->second.disabled_chain_count;
            out_info->spring_unsupported_collider_chain_count = state_it->second.unsupported_collider_chain_count;
            out_info->spring_avg_substeps = state_it->second.avg_substeps;
        }
    }
    {
        const auto mtoon_counts = CountMtoonDiagnostics(pkg);
        out_info->mtoon_advanced_param_material_count = mtoon_counts.advanced;
        out_info->mtoon_fallback_material_count = mtoon_counts.fallback;
    }
    {
        const auto quality = SanitizeRenderQualityOptions(g_state.render_quality);
        const auto parity = ComputeParityDiagnostics(pkg, quality.quality_profile);
        out_info->parity_score = parity.score;
        CopyString(out_info->variant_id, sizeof(out_info->variant_id), parity.variant_id);
        CopyString(
            out_info->parity_fallback_reason,
            sizeof(out_info->parity_fallback_reason),
            parity.fallback_reason);
        CopyString(
            out_info->quality_mode,
            sizeof(out_info->quality_mode),
            RenderQualityModeName(quality.quality_profile));
    }
    CopyString(out_info->selected_family_backend, sizeof(out_info->selected_family_backend), "common");
    CopyString(out_info->active_passes, sizeof(out_info->active_passes), "none");
    CopyString(out_info->material_parity_last_mismatch, sizeof(out_info->material_parity_last_mismatch), "none");
    auto has_typed_slot = [](const avatar::MaterialRenderPayload& payload, const std::initializer_list<const char*> slots) {
        for (const auto& param : payload.typed_texture_params) {
            std::string normalized = param.slot;
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            for (const char* slot : slots) {
                if (slot == nullptr) {
                    continue;
                }
                std::string key(slot);
                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (normalized == key) {
                    return true;
                }
            }
        }
        return false;
    };
#if defined(_WIN32)
    {
        const auto material_it = g_state.renderer.avatar_materials.find(handle);
        if (material_it != g_state.renderer.avatar_materials.end() && !material_it->second.empty()) {
            std::array<std::uint32_t, 5U> backend_counts = {0U, 0U, 0U, 0U, 0U};
            std::uint32_t backend_fallback_count = 0U;
            bool has_depth = false;
            bool has_shadow = false;
            bool has_base = false;
            bool has_outline = false;
            bool has_emission = false;
            for (const auto& material : material_it->second) {
                const std::size_t idx =
                    material.backend_selected == RenderFamilyBackendKind::Liltoon ? 1U :
                    material.backend_selected == RenderFamilyBackendKind::Mtoon ? 2U :
                    material.backend_selected == RenderFamilyBackendKind::Poiyomi ? 3U :
                    material.backend_selected == RenderFamilyBackendKind::Standard ? 4U :
                    0U;
                backend_counts[idx] += 1U;
                if (material.backend_fallback_applied) {
                    ++backend_fallback_count;
                }
                has_depth = has_depth || material.enable_depth_pass;
                has_shadow = has_shadow || material.enable_shadow_pass;
                has_base = has_base || material.enable_base_pass;
                has_outline = has_outline || material.enable_outline_pass;
                has_emission = has_emission || material.enable_emission_pass;
            }
            std::size_t dominant_idx = 0U;
            for (std::size_t i = 1U; i < backend_counts.size(); ++i) {
                if (backend_counts[i] > backend_counts[dominant_idx]) {
                    dominant_idx = i;
                }
            }
            const RenderFamilyBackendKind dominant_backend =
                dominant_idx == 1U ? RenderFamilyBackendKind::Liltoon :
                dominant_idx == 2U ? RenderFamilyBackendKind::Mtoon :
                dominant_idx == 3U ? RenderFamilyBackendKind::Poiyomi :
                dominant_idx == 4U ? RenderFamilyBackendKind::Standard :
                                     RenderFamilyBackendKind::Common;
            out_info->family_backend_fallback_count = backend_fallback_count;
            CopyString(
                out_info->selected_family_backend,
                sizeof(out_info->selected_family_backend),
                RenderFamilyBackendName(dominant_backend));
            std::ostringstream pass_state;
            pass_state
                << (has_depth ? "depth" : "")
                << ((has_depth && has_shadow) ? "|" : "")
                << (has_shadow ? "shadow" : "")
                << (((has_depth || has_shadow) && has_base) ? "|" : "")
                << (has_base ? "base" : "")
                << (((has_depth || has_shadow || has_base) && has_outline) ? "|" : "")
                << (has_outline ? "outline" : "")
                << (((has_depth || has_shadow || has_base || has_outline) && has_emission) ? "|" : "")
                << (has_emission ? "emission" : "");
            const std::string pass_value = pass_state.str().empty() ? std::string("none") : pass_state.str();
            CopyString(out_info->active_passes, sizeof(out_info->active_passes), pass_value);
        }
    }
    {
        const auto material_it = g_state.renderer.avatar_materials.find(handle);
        if (material_it != g_state.renderer.avatar_materials.end()) {
            const auto& gpu_materials = material_it->second;
            const std::size_t compare_count = std::min(gpu_materials.size(), pkg.material_payloads.size());
            for (std::size_t i = 0U; i < compare_count; ++i) {
                const auto& payload = pkg.material_payloads[i];
                const auto& material = gpu_materials[i];
                auto upper = [](std::string s) {
                    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                        return static_cast<char>(std::toupper(c));
                    });
                    return s;
                };
                const bool expect_base =
                    !payload.base_color_texture_name.empty() ||
                    has_typed_slot(payload, {"base", "main", "_MainTex", "_BaseMap"});
                const bool expect_normal = has_typed_slot(payload, {"normal", "_BumpMap"});
                const bool expect_rim = has_typed_slot(payload, {"rim", "_RimTex"});
                const bool expect_emission = has_typed_slot(payload, {"emission", "_EmissionMap"});
                const bool expect_matcap = has_typed_slot(payload, {"matcap", "_MatCapTex", "_MatCapTexture"});
                const bool expect_uv_mask = has_typed_slot(payload, {"uvAnimationMask", "_UvAnimMaskTex"});
                const std::array<std::pair<const char*, std::pair<bool, bool>>, 6U> checks = {{
                    {"base", {expect_base, material.base_color_srv != nullptr}},
                    {"normal", {expect_normal, material.normal_srv != nullptr}},
                    {"rim", {expect_rim, material.rim_srv != nullptr}},
                    {"emission", {expect_emission, material.emission_srv != nullptr}},
                    {"matcap", {expect_matcap, material.matcap_srv != nullptr}},
                    {"uvAnimationMask", {expect_uv_mask, material.uv_anim_mask_srv != nullptr}},
                }};
                for (const auto& check : checks) {
                    const bool expected = check.second.first;
                    const bool bound = check.second.second;
                    if (expected == bound) {
                        continue;
                    }
                    ++out_info->material_parity_mismatch_count;
                    std::ostringstream mismatch;
                    mismatch << "material=" << payload.name
                             << ", slot=" << check.first
                             << ", expected=" << (expected ? "1" : "0")
                             << ", bound=" << (bound ? "1" : "0");
                    CopyString(
                        out_info->material_parity_last_mismatch,
                        sizeof(out_info->material_parity_last_mismatch),
                        mismatch.str());
                }
                const std::string payload_alpha = upper(payload.alpha_mode.empty() ? "OPAQUE" : payload.alpha_mode);
                const std::string gpu_alpha = upper(material.alpha_mode.empty() ? "OPAQUE" : material.alpha_mode);
                if (payload_alpha != gpu_alpha) {
                    ++out_info->material_parity_mismatch_count;
                    std::ostringstream mismatch;
                    mismatch << "material=" << payload.name
                             << ", field=alpha_mode, payload=" << payload_alpha
                             << ", gpu=" << gpu_alpha;
                    CopyString(
                        out_info->material_parity_last_mismatch,
                        sizeof(out_info->material_parity_last_mismatch),
                        mismatch.str());
                }
                const std::string payload_family = NormalizeShaderFamilyKey(payload.shader_family);
                const std::string gpu_family = NormalizeShaderFamilyKey(material.shader_family);
                if (payload_family != gpu_family) {
                    ++out_info->material_parity_mismatch_count;
                    std::ostringstream mismatch;
                    mismatch << "material=" << payload.name
                             << ", field=shader_family, payload=" << payload_family
                             << ", gpu=" << gpu_family;
                    CopyString(
                        out_info->material_parity_last_mismatch,
                        sizeof(out_info->material_parity_last_mismatch),
                        mismatch.str());
                }
                if (payload.double_sided != material.double_sided) {
                    ++out_info->material_parity_mismatch_count;
                    std::ostringstream mismatch;
                    mismatch << "material=" << payload.name
                             << ", field=double_sided, payload=" << (payload.double_sided ? "1" : "0")
                             << ", gpu=" << (material.double_sided ? "1" : "0");
                    CopyString(
                        out_info->material_parity_last_mismatch,
                        sizeof(out_info->material_parity_last_mismatch),
                        mismatch.str());
                }
            }
            if (gpu_materials.size() != pkg.material_payloads.size()) {
                ++out_info->material_parity_mismatch_count;
                std::ostringstream mismatch;
                mismatch << "material_count_mismatch: payload=" << pkg.material_payloads.size()
                         << ", gpu=" << gpu_materials.size();
                CopyString(
                    out_info->material_parity_last_mismatch,
                    sizeof(out_info->material_parity_last_mismatch),
                    mismatch.str());
            }
        }
    }
#endif
    for (const auto& warning_code : pkg.warning_codes) {
        std::string lowered = warning_code;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (lowered == "miq_material_texture_ambiguous" || lowered == "vrm_material_texture_ambiguous") {
            ++out_info->texture_resolve_ambiguous_count;
        }
    }
    CopyString(out_info->display_name, sizeof(out_info->display_name), pkg.display_name);
    CopyString(out_info->source_path, sizeof(out_info->source_path), pkg.source_path);
    CopyString(
        out_info->parser_stage,
        sizeof(out_info->parser_stage),
        pkg.parser_stage.empty() ? "unknown" : pkg.parser_stage);
    CopyString(
        out_info->primary_error_code,
        sizeof(out_info->primary_error_code),
        pkg.primary_error_code.empty() ? "NONE" : pkg.primary_error_code);
    if (!pkg.warning_codes.empty()) {
        const std::string* preferred_warning_code = nullptr;
        for (auto it = pkg.warning_codes.rbegin(); it != pkg.warning_codes.rend(); ++it) {
            if (IsPreferredWarningCodeForUi(*it)) {
                preferred_warning_code = &(*it);
                break;
            }
        }
        const std::string& selected_warning_code =
            preferred_warning_code != nullptr ? *preferred_warning_code : pkg.warning_codes.back();
        CopyString(out_info->last_warning_code, sizeof(out_info->last_warning_code), selected_warning_code);
        const auto last_meta = ClassifyWarningCode(selected_warning_code);
        CopyString(out_info->last_warning_severity, sizeof(out_info->last_warning_severity), last_meta.severity);
        CopyString(out_info->last_warning_category, sizeof(out_info->last_warning_category), last_meta.category);
    }
    const std::string expression_summary =
        (!pkg.last_expression_summary.empty())
            ? pkg.last_expression_summary
            : (out_info->detected_format == NC_AVATAR_FORMAT_VRM ? "blink=0.000000, aa=0.000000, joy=0.000000" : "");
    CopyString(
        out_info->last_expression_summary,
        sizeof(out_info->last_expression_summary),
        expression_summary);
    if (!pkg.warnings.empty()) {
        const std::string* preferred_warning = nullptr;
        for (auto it = pkg.warnings.rbegin(); it != pkg.warnings.rend(); ++it) {
            if (IsPreferredWarningMessageForUi(*it)) {
                preferred_warning = &(*it);
                break;
            }
        }
        const std::string& selected_warning = preferred_warning != nullptr ? *preferred_warning : pkg.warnings.back();
        CopyString(out_info->last_warning, sizeof(out_info->last_warning), selected_warning);
    }
    if (!pkg.material_diagnostics.empty()) {
        CopyString(
            out_info->last_material_diag,
            sizeof(out_info->last_material_diag),
            BuildMaterialDiagSummary(pkg.material_diagnostics.back()));
    }
    {
        const char* yaw_reason = "default";
        const int applied_preview_yaw = PreviewYawDegreesForAvatarPackage(pkg, &yaw_reason);
        std::ostringstream pass_summary;
        pass_summary << "format=" << AvatarSourceTypeName(pkg.source_type)
                     << ", applied_preview_yaw_deg=" << applied_preview_yaw
                     << ", preview_yaw_reason=" << yaw_reason
                     << ", contract_preview_yaw_deg=" << pkg.recommended_preview_yaw_deg
                     << ", transform_confidence=" << TransformConfidenceName(pkg.transform_confidence)
                     << ", quality_mode=" << out_info->quality_mode
                     << ", backend=" << out_info->selected_family_backend
                     << ", active_passes=" << out_info->active_passes
                     << ", backend_fallbacks=" << out_info->family_backend_fallback_count
                     << ", pass(depth/shadow/base/outline/emission/blend)="
                     << g_state.last_depth_pass_count << "/"
                     << g_state.last_shadow_pass_count << "/"
                     << g_state.last_base_pass_count << "/"
                     << g_state.last_outline_pass_count << "/"
                     << g_state.last_emission_pass_count << "/"
                     << g_state.last_blend_pass_count
                     << ", opaque=" << mode_counts.opaque
                     << ", mask=" << mode_counts.mask
                     << ", blend=" << mode_counts.blend;
        const auto preview_it = g_state.avatar_preview_debug.find(handle);
        if (preview_it != g_state.avatar_preview_debug.end() && !preview_it->second.empty()) {
            pass_summary << ", " << preview_it->second;
        }
        CopyString(
            out_info->last_render_pass_summary,
            sizeof(out_info->last_render_pass_summary),
            pass_summary.str());
    }
    if (!pkg.missing_features.empty()) {
        CopyString(out_info->last_missing_feature, sizeof(out_info->last_missing_feature), pkg.missing_features.back());
    }
}

bool EnsureInitialized() {
    if (g_state.initialized) {
        return true;
    }
    SetError(NC_ERROR_NOT_INITIALIZED, "runtime", "native core is not initialized", true);
    return false;
}

#if defined(_WIN32)
void ReleaseWindowState(WindowRenderState* state) {
    if (state == nullptr) {
        return;
    }
    if (state->rtv != nullptr) {
        state->rtv->Release();
        state->rtv = nullptr;
    }
    if (state->swap_chain != nullptr) {
        state->swap_chain->Release();
        state->swap_chain = nullptr;
    }
    if (state->device_context != nullptr) {
        state->device_context->Release();
        state->device_context = nullptr;
    }
    if (state->device != nullptr) {
        state->device->Release();
        state->device = nullptr;
    }
    state->width = 0;
    state->height = 0;
}

void ReleaseGpuMeshResource(GpuMeshResource* mesh) {
    if (mesh == nullptr) {
        return;
    }
    if (mesh->vertex_buffer != nullptr) {
        mesh->vertex_buffer->Release();
        mesh->vertex_buffer = nullptr;
    }
    if (mesh->index_buffer != nullptr) {
        mesh->index_buffer->Release();
        mesh->index_buffer = nullptr;
    }
    mesh->vertex_count = 0;
    mesh->index_count = 0;
    mesh->material_index = -1;
    mesh->center = {0.0f, 0.0f, 0.0f};
    mesh->bounds_min = {0.0f, 0.0f, 0.0f};
    mesh->bounds_max = {0.0f, 0.0f, 0.0f};
    mesh->mesh_name.clear();
    mesh->base_vertex_blob.clear();
    mesh->deformed_vertex_blob.clear();
}

void ReleaseGpuMaterialResource(GpuMaterialResource* material) {
    if (material == nullptr) {
        return;
    }
    if (material->base_color_srv != nullptr) {
        material->base_color_srv->Release();
        material->base_color_srv = nullptr;
    }
    if (material->normal_srv != nullptr) {
        material->normal_srv->Release();
        material->normal_srv = nullptr;
    }
    if (material->rim_srv != nullptr) {
        material->rim_srv->Release();
        material->rim_srv = nullptr;
    }
    if (material->emission_srv != nullptr) {
        material->emission_srv->Release();
        material->emission_srv = nullptr;
    }
    if (material->matcap_srv != nullptr) {
        material->matcap_srv->Release();
        material->matcap_srv = nullptr;
    }
    if (material->uv_anim_mask_srv != nullptr) {
        material->uv_anim_mask_srv->Release();
        material->uv_anim_mask_srv = nullptr;
    }
    material->alpha_mode = "OPAQUE";
    material->alpha_cutoff = 0.5f;
    material->double_sided = false;
    material->base_color = {1.0f, 1.0f, 1.0f, 1.0f};
    material->shade_color = {1.0f, 1.0f, 1.0f, 1.0f};
    material->emission_color = {0.0f, 0.0f, 0.0f, 1.0f};
    material->rim_color = {0.0f, 0.0f, 0.0f, 1.0f};
    material->matcap_color = {0.0f, 0.0f, 0.0f, 1.0f};
    material->shade_mix = 0.0f;
    material->emission_strength = 0.0f;
    material->normal_strength = 0.0f;
    material->rim_strength = 0.0f;
    material->matcap_strength = 0.0f;
    material->rim_power = 2.0f;
    material->outline_width = 0.0f;
    material->outline_lighting_mix = 0.0f;
    material->uv_anim_scroll_x = 0.0f;
    material->uv_anim_scroll_y = 0.0f;
    material->uv_anim_rotation = 0.0f;
    material->uv_anim_enabled = false;
    material->shader_family = "legacy";
    material->shader_variant = "default";
    material->pass_flags = "base";
    material->enable_depth_pass = true;
    material->enable_shadow_pass = false;
    material->enable_base_pass = true;
    material->enable_outline_pass = false;
    material->enable_emission_pass = false;
}

void ResetRendererResources(RendererResources* renderer) {
    if (renderer == nullptr) {
        return;
    }
    for (auto& [_, meshes] : renderer->avatar_meshes) {
        for (auto& mesh : meshes) {
            ReleaseGpuMeshResource(&mesh);
        }
    }
    renderer->avatar_meshes.clear();
    for (auto& [_, materials] : renderer->avatar_materials) {
        for (auto& material : materials) {
            ReleaseGpuMaterialResource(&material);
        }
    }
    renderer->avatar_materials.clear();
    if (renderer->depth_dsv != nullptr) {
        renderer->depth_dsv->Release();
        renderer->depth_dsv = nullptr;
    }
    if (renderer->depth_texture != nullptr) {
        renderer->depth_texture->Release();
        renderer->depth_texture = nullptr;
    }
    if (renderer->blend_alpha != nullptr) {
        renderer->blend_alpha->Release();
        renderer->blend_alpha = nullptr;
    }
    if (renderer->blend_additive != nullptr) {
        renderer->blend_additive->Release();
        renderer->blend_additive = nullptr;
    }
    if (renderer->blend_depth_only != nullptr) {
        renderer->blend_depth_only->Release();
        renderer->blend_depth_only = nullptr;
    }
    if (renderer->linear_sampler != nullptr) {
        renderer->linear_sampler->Release();
        renderer->linear_sampler = nullptr;
    }
    if (renderer->shadow_sampler != nullptr) {
        renderer->shadow_sampler->Release();
        renderer->shadow_sampler = nullptr;
    }
    if (renderer->shadow_srv != nullptr) {
        renderer->shadow_srv->Release();
        renderer->shadow_srv = nullptr;
    }
    if (renderer->shadow_dsv != nullptr) {
        renderer->shadow_dsv->Release();
        renderer->shadow_dsv = nullptr;
    }
    if (renderer->shadow_texture != nullptr) {
        renderer->shadow_texture->Release();
        renderer->shadow_texture = nullptr;
    }
    if (renderer->blend_opaque != nullptr) {
        renderer->blend_opaque->Release();
        renderer->blend_opaque = nullptr;
    }
    if (renderer->depth_read != nullptr) {
        renderer->depth_read->Release();
        renderer->depth_read = nullptr;
    }
    if (renderer->depth_write != nullptr) {
        renderer->depth_write->Release();
        renderer->depth_write = nullptr;
    }
    if (renderer->raster_cull_none != nullptr) {
        renderer->raster_cull_none->Release();
        renderer->raster_cull_none = nullptr;
    }
    if (renderer->raster_cull_front != nullptr) {
        renderer->raster_cull_front->Release();
        renderer->raster_cull_front = nullptr;
    }
    if (renderer->raster_cull_back != nullptr) {
        renderer->raster_cull_back->Release();
        renderer->raster_cull_back = nullptr;
    }
    if (renderer->raster_cull_back_ccw != nullptr) {
        renderer->raster_cull_back_ccw->Release();
        renderer->raster_cull_back_ccw = nullptr;
    }
    if (renderer->raster_cull_front_ccw != nullptr) {
        renderer->raster_cull_front_ccw->Release();
        renderer->raster_cull_front_ccw = nullptr;
    }
    if (renderer->constant_buffer != nullptr) {
        renderer->constant_buffer->Release();
        renderer->constant_buffer = nullptr;
    }
    if (renderer->input_layout != nullptr) {
        renderer->input_layout->Release();
        renderer->input_layout = nullptr;
    }
    if (renderer->pixel_shader_standard != nullptr) {
        renderer->pixel_shader_standard->Release();
        renderer->pixel_shader_standard = nullptr;
    }
    if (renderer->pixel_shader_poiyomi != nullptr) {
        renderer->pixel_shader_poiyomi->Release();
        renderer->pixel_shader_poiyomi = nullptr;
    }
    if (renderer->pixel_shader_mtoon != nullptr) {
        renderer->pixel_shader_mtoon->Release();
        renderer->pixel_shader_mtoon = nullptr;
    }
    if (renderer->pixel_shader_liltoon != nullptr) {
        renderer->pixel_shader_liltoon->Release();
        renderer->pixel_shader_liltoon = nullptr;
    }
    if (renderer->pixel_shader_common != nullptr) {
        renderer->pixel_shader_common->Release();
        renderer->pixel_shader_common = nullptr;
    }
    if (renderer->vertex_shader != nullptr) {
        renderer->vertex_shader->Release();
        renderer->vertex_shader = nullptr;
    }
    if (renderer->device != nullptr) {
        renderer->device->Release();
        renderer->device = nullptr;
    }
    renderer->depth_width = 0U;
    renderer->depth_height = 0U;
    renderer->shadow_resolution = 0U;
}

bool EnsureDepthResources(RendererResources* renderer, ID3D11Device* device, std::uint32_t width, std::uint32_t height) {
    if (renderer == nullptr || device == nullptr || width == 0U || height == 0U) {
        return false;
    }
    if (renderer->depth_texture != nullptr && renderer->depth_dsv != nullptr &&
        renderer->depth_width == width && renderer->depth_height == height) {
        return true;
    }
    if (renderer->depth_dsv != nullptr) {
        renderer->depth_dsv->Release();
        renderer->depth_dsv = nullptr;
    }
    if (renderer->depth_texture != nullptr) {
        renderer->depth_texture->Release();
        renderer->depth_texture = nullptr;
    }

    D3D11_TEXTURE2D_DESC depth_desc {};
    depth_desc.Width = width;
    depth_desc.Height = height;
    depth_desc.MipLevels = 1U;
    depth_desc.ArraySize = 1U;
    depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depth_desc.SampleDesc.Count = 1U;
    depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(device->CreateTexture2D(&depth_desc, nullptr, &renderer->depth_texture)) || renderer->depth_texture == nullptr) {
        return false;
    }
    if (FAILED(device->CreateDepthStencilView(renderer->depth_texture, nullptr, &renderer->depth_dsv)) || renderer->depth_dsv == nullptr) {
        return false;
    }
    renderer->depth_width = width;
    renderer->depth_height = height;
    return true;
}

bool EnsureShadowResources(RendererResources* renderer, ID3D11Device* device, std::uint32_t shadow_resolution) {
    if (renderer == nullptr || device == nullptr || shadow_resolution == 0U) {
        return false;
    }
    if (renderer->shadow_texture != nullptr &&
        renderer->shadow_dsv != nullptr &&
        renderer->shadow_srv != nullptr &&
        renderer->shadow_resolution == shadow_resolution) {
        return true;
    }
    if (renderer->shadow_srv != nullptr) {
        renderer->shadow_srv->Release();
        renderer->shadow_srv = nullptr;
    }
    if (renderer->shadow_dsv != nullptr) {
        renderer->shadow_dsv->Release();
        renderer->shadow_dsv = nullptr;
    }
    if (renderer->shadow_texture != nullptr) {
        renderer->shadow_texture->Release();
        renderer->shadow_texture = nullptr;
    }

    D3D11_TEXTURE2D_DESC shadow_desc {};
    shadow_desc.Width = shadow_resolution;
    shadow_desc.Height = shadow_resolution;
    shadow_desc.MipLevels = 1U;
    shadow_desc.ArraySize = 1U;
    shadow_desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    shadow_desc.SampleDesc.Count = 1U;
    shadow_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&shadow_desc, nullptr, &renderer->shadow_texture)) || renderer->shadow_texture == nullptr) {
        return false;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc {};
    dsv_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Texture2D.MipSlice = 0U;
    if (FAILED(device->CreateDepthStencilView(renderer->shadow_texture, &dsv_desc, &renderer->shadow_dsv)) || renderer->shadow_dsv == nullptr) {
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc {};
    srv_desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0U;
    srv_desc.Texture2D.MipLevels = 1U;
    if (FAILED(device->CreateShaderResourceView(renderer->shadow_texture, &srv_desc, &renderer->shadow_srv)) || renderer->shadow_srv == nullptr) {
        return false;
    }
    renderer->shadow_resolution = shadow_resolution;
    return true;
}

bool EnsurePipelineResources(RendererResources* renderer, ID3D11Device* device) {
    if (renderer == nullptr || device == nullptr) {
        return false;
    }
    if (renderer->device != nullptr && renderer->device != device) {
        ResetRendererResources(renderer);
    }
    if (renderer->device == nullptr) {
        renderer->device = device;
        renderer->device->AddRef();
    }
    if (renderer->vertex_shader != nullptr &&
        renderer->pixel_shader_common != nullptr &&
        renderer->pixel_shader_liltoon != nullptr &&
        renderer->pixel_shader_mtoon != nullptr &&
        renderer->pixel_shader_poiyomi != nullptr &&
        renderer->pixel_shader_standard != nullptr &&
        renderer->input_layout != nullptr && renderer->constant_buffer != nullptr &&
        renderer->raster_cull_back != nullptr && renderer->raster_cull_front != nullptr &&
        renderer->raster_cull_none != nullptr &&
        renderer->raster_cull_back_ccw != nullptr && renderer->raster_cull_front_ccw != nullptr &&
        renderer->depth_write != nullptr && renderer->depth_read != nullptr &&
        renderer->blend_opaque != nullptr && renderer->blend_alpha != nullptr &&
        renderer->blend_additive != nullptr && renderer->blend_depth_only != nullptr &&
        renderer->linear_sampler != nullptr && renderer->shadow_sampler != nullptr) {
        return true;
    }

    constexpr char kVertexShaderSrc[] =
        "cbuffer SceneCB : register(b0) {\n"
        "  float4x4 world_view_proj;\n"
        "  float4x4 world_matrix;\n"
        "  float4x4 light_view_proj;\n"
        "  float4 base_color;\n"
        "  float4 shade_color;\n"
        "  float4 emission_color;\n"
        "  float4 rim_color;\n"
        "  float4 matcap_color;\n"
        "  float4 lighting_params;\n"
        "  float4 shadow_params;\n"
        "  float4 liltoon_mix;\n"
        "  float4 liltoon_params;\n"
        "  float4 liltoon_aux;\n"
        "  float4 alpha_misc;\n"
        "  float4 outline_params;\n"
        "  float4 uv_anim_params;\n"
        "  float4 time_params;\n"
        "};\n"
        "struct VSIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
        "struct VSOut { float4 pos : SV_POSITION; float4 color : COLOR0; float3 nrm : NORMAL; float2 uv : TEXCOORD0; float3 world_pos : TEXCOORD1; };\n"
        "VSOut main(VSIn i) {\n"
        "  VSOut o;\n"
        "  float3 pos = i.pos;\n"
        "  if (outline_params.w > 0.5) {\n"
        "    pos += normalize(i.nrm) * outline_params.x;\n"
        "  }\n"
        "  o.pos = mul(float4(pos, 1.0), world_view_proj);\n"
        "  o.color = base_color;\n"
        "  o.nrm = normalize(i.nrm);\n"
        "  o.uv = i.uv;\n"
        "  o.world_pos = mul(float4(pos, 1.0), world_matrix).xyz;\n"
        "  return o;\n"
        "}\n";
    constexpr char kPixelShaderCommonSrc[] =
        "cbuffer SceneCB : register(b0) {\n"
        "  float4x4 world_view_proj;\n"
        "  float4x4 world_matrix;\n"
        "  float4x4 light_view_proj;\n"
        "  float4 base_color;\n"
        "  float4 shade_color;\n"
        "  float4 emission_color;\n"
        "  float4 rim_color;\n"
        "  float4 matcap_color;\n"
        "  float4 lighting_params;\n"
        "  float4 shadow_params;\n"
        "  float4 liltoon_mix;\n"
        "  float4 liltoon_params;\n"
        "  float4 liltoon_aux;\n"
        "  float4 alpha_misc;\n"
        "  float4 outline_params;\n"
        "  float4 uv_anim_params;\n"
        "  float4 time_params;\n"
        "};\n"
        "Texture2D tex0 : register(t0);\n"
        "Texture2D tex1 : register(t1);\n"
        "Texture2D tex2 : register(t2);\n"
        "Texture2D tex3 : register(t3);\n"
        "Texture2D tex4 : register(t4);\n"
        "Texture2D tex5 : register(t5);\n"
        "Texture2D tex6 : register(t6);\n"
        "SamplerState samp0 : register(s0);\n"
        "SamplerComparisonState samp1 : register(s1);\n"
        "float4 main(float4 pos : SV_POSITION, float4 color : COLOR0, float3 nrm : NORMAL, float2 uv : TEXCOORD0, float3 world_pos : TEXCOORD1) : SV_TARGET {\n"
        "  float is_outline_pass = outline_params.w;\n"
        "  float is_emission_pass = outline_params.z;\n"
        "  if (is_outline_pass > 0.5) {\n"
        "    float3 outline_tint = lerp(base_color.rgb, shade_color.rgb, saturate(outline_params.y));\n"
        "    return float4(outline_tint, base_color.a);\n"
        "  }\n"
        "  float alpha_cutoff = alpha_misc.x;\n"
        "  float alpha_mode_mask = alpha_misc.y;\n"
        "  float has_texture = alpha_misc.z;\n"
        "  float use_texture_alpha = alpha_misc.w;\n"
        "  float use_normal_tex = liltoon_params.y;\n"
        "  float use_rim_tex = liltoon_params.z;\n"
        "  float use_emission_tex = liltoon_params.w;\n"
        "  float use_matcap_tex = liltoon_aux.x;\n"
        "  float matcap_strength = saturate(liltoon_aux.y);\n"
        "  float has_uv_anim = uv_anim_params.w;\n"
        "  float has_uv_mask = liltoon_aux.z;\n"
        "  float t = time_params.x;\n"
        "  float2 sample_uv = uv;\n"
        "  if (has_uv_anim > 0.5) {\n"
        "    float2 centered = uv - float2(0.5, 0.5);\n"
        "    float rot = uv_anim_params.z * t;\n"
        "    float s = sin(rot);\n"
        "    float c = cos(rot);\n"
        "    float2 rotated = float2(centered.x * c - centered.y * s, centered.x * s + centered.y * c) + float2(0.5, 0.5);\n"
        "    float2 shifted = rotated + uv_anim_params.xy * t;\n"
        "    float mask = has_uv_mask > 0.5 ? saturate(tex5.Sample(samp0, uv).r) : 1.0;\n"
        "    sample_uv = lerp(uv, shifted, mask);\n"
        "  }\n"
        "  if (is_emission_pass > 0.5) {\n"
        "    float3 em = emission_color.rgb;\n"
        "    if (use_emission_tex > 0.5) {\n"
        "      em *= tex3.Sample(samp0, sample_uv).rgb;\n"
        "    }\n"
        "    return float4(em * saturate(liltoon_mix.y), 1.0);\n"
        "  }\n"
        "  float4 out_color = color;\n"
        "  float3 normal = normalize(nrm);\n"
        "  if (use_normal_tex > 0.5) {\n"
        "    float3 ntex = tex1.Sample(samp0, sample_uv).xyz * 2.0 - 1.0;\n"
        "    normal = normalize(float3(normal.xy + ntex.xy * saturate(liltoon_mix.z), max(0.15, normal.z * abs(ntex.z))));\n"
        "  }\n"
        "  float3 light_dir = normalize(lighting_params.xyz);\n"
        "  float ambient = saturate(lighting_params.w);\n"
        "  float direct_scale = max(0.0, shadow_params.w);\n"
        "  float ndotl = saturate(dot(normal, light_dir));\n"
        "#if defined(FAMILY_MTOON)\n"
        // Hard binary step matching MToon's toony shading: shadow side drops to
        // near-zero so that shade_color tinting becomes clearly visible.
        "  float lit = ndotl > 0.5 ? 1.0 : 0.05;\n"
        "#else\n"
        "  float lit = lerp(0.35, 1.0, ndotl);\n"
        "#endif\n"
        "  if (has_texture > 0.5) {\n"
        "    float4 texel = tex0.Sample(samp0, sample_uv);\n"
        "    out_color.rgb *= texel.rgb;\n"
        "    if (use_texture_alpha > 0.5) {\n"
        "      out_color.a *= texel.a;\n"
        "    }\n"
        "  }\n"
        "  out_color.rgb *= lerp(ambient, 1.0, saturate(lit * direct_scale));\n"
        "  float shadow_enabled = shadow_params.y;\n"
        "  float shadow_strength = shadow_params.x;\n"
        "  if (shadow_enabled > 0.5) {\n"
        "    float4 shadow_pos = mul(float4(world_pos, 1.0), light_view_proj);\n"
        "    float inv_w = rcp(max(0.0001, shadow_pos.w));\n"
        "    float2 suv = shadow_pos.xy * inv_w * float2(0.5, -0.5) + 0.5;\n"
        "    float depth = shadow_pos.z * inv_w;\n"
        "    float visibility = tex6.SampleCmpLevelZero(samp1, suv, depth - shadow_params.z);\n"
        "    float atten = lerp(1.0 - shadow_strength, 1.0, visibility);\n"
        "    out_color.rgb *= atten;\n"
        "  }\n"
        "#if defined(FAMILY_MTOON)\n"
        "  float shade_t = saturate((1.0 - lit) * saturate(liltoon_mix.x) * 1.35);\n"
        "#else\n"
        "  float shade_t = saturate((1.0 - ndotl) * saturate(liltoon_mix.x) * 1.2);\n"
        "#endif\n"
        "  out_color.rgb = lerp(out_color.rgb, out_color.rgb * shade_color.rgb, shade_t);\n"
        "  float3 emission_term = emission_color.rgb;\n"
        "  if (use_emission_tex > 0.5) {\n"
        "    emission_term *= tex3.Sample(samp0, sample_uv).rgb;\n"
        "  }\n"
        "  out_color.rgb += emission_term * saturate(liltoon_mix.y);\n"
        "  float rim = pow(saturate(1.0 - normal.z), max(0.1, liltoon_params.x)) * saturate(liltoon_mix.w);\n"
        "  if (use_rim_tex > 0.5) {\n"
        "    rim *= tex2.Sample(samp0, sample_uv).a;\n"
        "  }\n"
        "  out_color.rgb += rim_color.rgb * rim;\n"
        "  if (use_matcap_tex > 0.5 && matcap_strength > 0.001) {\n"
        "    float2 mc_uv = normal.xy * 0.5 + 0.5;\n"
        "    float3 matcap = tex4.Sample(samp0, mc_uv).rgb * matcap_color.rgb;\n"
        "    out_color.rgb += matcap * matcap_strength;\n"
        "  }\n"
        "  if (alpha_mode_mask > 0.5 && out_color.a < alpha_cutoff) {\n"
        "    clip(-1.0);\n"
        "  }\n"
        "  return out_color;\n"
        "}\n";
    const char* kPixelShaderLiltoonSrc = kPixelShaderCommonSrc;
    const char* kPixelShaderPoiyomiSrc = kPixelShaderCommonSrc;
    const char* kPixelShaderStandardSrc = kPixelShaderCommonSrc;

    ID3DBlob* vs_blob = nullptr;
    ID3DBlob* ps_common_blob = nullptr;
    ID3DBlob* ps_liltoon_blob = nullptr;
    ID3DBlob* ps_mtoon_blob = nullptr;
    ID3DBlob* ps_poiyomi_blob = nullptr;
    ID3DBlob* ps_standard_blob = nullptr;
    ID3DBlob* err_blob = nullptr;
    HRESULT hr = D3DCompile(
        kVertexShaderSrc,
        sizeof(kVertexShaderSrc) - 1U,
        nullptr,
        nullptr,
        nullptr,
        "main",
        "vs_5_0",
        0U,
        0U,
        &vs_blob,
        &err_blob);
    if (FAILED(hr) || vs_blob == nullptr) {
        if (err_blob != nullptr) {
            err_blob->Release();
        }
        return false;
    }
    if (err_blob != nullptr) {
        err_blob->Release();
        err_blob = nullptr;
    }
    auto release_compile_blobs = [&]() {
        if (vs_blob != nullptr) {
            vs_blob->Release();
            vs_blob = nullptr;
        }
        if (ps_common_blob != nullptr) {
            ps_common_blob->Release();
            ps_common_blob = nullptr;
        }
        if (ps_liltoon_blob != nullptr) {
            ps_liltoon_blob->Release();
            ps_liltoon_blob = nullptr;
        }
        if (ps_mtoon_blob != nullptr) {
            ps_mtoon_blob->Release();
            ps_mtoon_blob = nullptr;
        }
        if (ps_poiyomi_blob != nullptr) {
            ps_poiyomi_blob->Release();
            ps_poiyomi_blob = nullptr;
        }
        if (ps_standard_blob != nullptr) {
            ps_standard_blob->Release();
            ps_standard_blob = nullptr;
        }
        if (err_blob != nullptr) {
            err_blob->Release();
            err_blob = nullptr;
        }
    };
    auto compile_ps = [&](const char* src, std::size_t src_size, const D3D_SHADER_MACRO* macros, ID3DBlob** out_blob) -> bool {
        hr = D3DCompile(
            src,
            src_size,
            nullptr,
            macros,
            nullptr,
            "main",
            "ps_5_0",
            0U,
            0U,
            out_blob,
            &err_blob);
        if (FAILED(hr) || *out_blob == nullptr) {
            release_compile_blobs();
            return false;
        }
        if (err_blob != nullptr) {
            err_blob->Release();
            err_blob = nullptr;
        }
        return true;
    };
    const D3D_SHADER_MACRO kNoMacros[] = {{nullptr, nullptr}};
    const D3D_SHADER_MACRO kMtoonMacros[] = {
        {"FAMILY_MTOON", "1"},
        {nullptr, nullptr}};
    if (!compile_ps(kPixelShaderCommonSrc, sizeof(kPixelShaderCommonSrc) - 1U, kNoMacros, &ps_common_blob)) {
        return false;
    }
    if (!compile_ps(kPixelShaderLiltoonSrc, std::strlen(kPixelShaderLiltoonSrc), kNoMacros, &ps_liltoon_blob)) {
        return false;
    }
    if (!compile_ps(kPixelShaderCommonSrc, sizeof(kPixelShaderCommonSrc) - 1U, kMtoonMacros, &ps_mtoon_blob)) {
        return false;
    }
    if (!compile_ps(kPixelShaderPoiyomiSrc, std::strlen(kPixelShaderPoiyomiSrc), kNoMacros, &ps_poiyomi_blob)) {
        return false;
    }
    if (!compile_ps(kPixelShaderStandardSrc, std::strlen(kPixelShaderStandardSrc), kNoMacros, &ps_standard_blob)) {
        return false;
    }

    hr = device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &renderer->vertex_shader);
    if (FAILED(hr) || renderer->vertex_shader == nullptr) {
        release_compile_blobs();
        return false;
    }
    hr = device->CreatePixelShader(
        ps_common_blob->GetBufferPointer(),
        ps_common_blob->GetBufferSize(),
        nullptr,
        &renderer->pixel_shader_common);
    if (FAILED(hr) || renderer->pixel_shader_common == nullptr) {
        release_compile_blobs();
        return false;
    }
    hr = device->CreatePixelShader(
        ps_liltoon_blob->GetBufferPointer(),
        ps_liltoon_blob->GetBufferSize(),
        nullptr,
        &renderer->pixel_shader_liltoon);
    if (FAILED(hr) || renderer->pixel_shader_liltoon == nullptr) {
        release_compile_blobs();
        return false;
    }
    hr = device->CreatePixelShader(
        ps_mtoon_blob->GetBufferPointer(),
        ps_mtoon_blob->GetBufferSize(),
        nullptr,
        &renderer->pixel_shader_mtoon);
    if (FAILED(hr) || renderer->pixel_shader_mtoon == nullptr) {
        release_compile_blobs();
        return false;
    }
    hr = device->CreatePixelShader(
        ps_poiyomi_blob->GetBufferPointer(),
        ps_poiyomi_blob->GetBufferSize(),
        nullptr,
        &renderer->pixel_shader_poiyomi);
    if (FAILED(hr) || renderer->pixel_shader_poiyomi == nullptr) {
        release_compile_blobs();
        return false;
    }
    hr = device->CreatePixelShader(
        ps_standard_blob->GetBufferPointer(),
        ps_standard_blob->GetBufferSize(),
        nullptr,
        &renderer->pixel_shader_standard);
    if (FAILED(hr) || renderer->pixel_shader_standard == nullptr) {
        release_compile_blobs();
        return false;
    }

    const D3D11_INPUT_ELEMENT_DESC input_desc[] = {
        {"POSITION", 0U, DXGI_FORMAT_R32G32B32_FLOAT, 0U, 0U, D3D11_INPUT_PER_VERTEX_DATA, 0U},
        {"NORMAL", 0U, DXGI_FORMAT_R32G32B32_FLOAT, 0U, 12U, D3D11_INPUT_PER_VERTEX_DATA, 0U},
        {"TEXCOORD", 0U, DXGI_FORMAT_R32G32_FLOAT, 0U, 24U, D3D11_INPUT_PER_VERTEX_DATA, 0U},
    };
    hr = device->CreateInputLayout(
        input_desc,
        3U,
        vs_blob->GetBufferPointer(),
        vs_blob->GetBufferSize(),
        &renderer->input_layout);
    release_compile_blobs();
    if (FAILED(hr) || renderer->input_layout == nullptr) {
        return false;
    }

    struct alignas(16) SceneConstants {
        float world_view_proj[16];
        float world_matrix[16];
        float light_view_proj[16];
        float base_color[4];
        float shade_color[4];
        float emission_color[4];
        float rim_color[4];
        float matcap_color[4];
        float lighting_params[4];
        float shadow_params[4];
        float liltoon_mix[4];
        float liltoon_params[4];
        float liltoon_aux[4];
        float alpha_misc[4];
        float outline_params[4];
        float uv_anim_params[4];
        float time_params[4];
    };
    D3D11_BUFFER_DESC cb_desc {};
    cb_desc.ByteWidth = static_cast<UINT>(sizeof(SceneConstants));
    cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&cb_desc, nullptr, &renderer->constant_buffer);
    if (FAILED(hr) || renderer->constant_buffer == nullptr) {
        return false;
    }

    D3D11_RASTERIZER_DESC raster_desc {};
    raster_desc.FillMode = D3D11_FILL_SOLID;
    raster_desc.CullMode = D3D11_CULL_BACK;
    raster_desc.DepthClipEnable = TRUE;
    hr = device->CreateRasterizerState(&raster_desc, &renderer->raster_cull_back);
    if (FAILED(hr) || renderer->raster_cull_back == nullptr) {
        return false;
    }
    raster_desc.CullMode = D3D11_CULL_FRONT;
    hr = device->CreateRasterizerState(&raster_desc, &renderer->raster_cull_front);
    if (FAILED(hr) || renderer->raster_cull_front == nullptr) {
        return false;
    }
    raster_desc.CullMode = D3D11_CULL_NONE;
    hr = device->CreateRasterizerState(&raster_desc, &renderer->raster_cull_none);
    if (FAILED(hr) || renderer->raster_cull_none == nullptr) {
        return false;
    }
    // CCW-front variants: glTF/VRM spec mandates CCW front faces, which is
    // opposite of DirectX's default CW convention.
    raster_desc.FrontCounterClockwise = TRUE;
    raster_desc.CullMode = D3D11_CULL_BACK;
    hr = device->CreateRasterizerState(&raster_desc, &renderer->raster_cull_back_ccw);
    if (FAILED(hr) || renderer->raster_cull_back_ccw == nullptr) {
        return false;
    }
    raster_desc.CullMode = D3D11_CULL_FRONT;
    hr = device->CreateRasterizerState(&raster_desc, &renderer->raster_cull_front_ccw);
    if (FAILED(hr) || renderer->raster_cull_front_ccw == nullptr) {
        return false;
    }
    raster_desc.FrontCounterClockwise = FALSE;

    D3D11_DEPTH_STENCIL_DESC depth_desc {};
    depth_desc.DepthEnable = TRUE;
    depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth_desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    hr = device->CreateDepthStencilState(&depth_desc, &renderer->depth_write);
    if (FAILED(hr) || renderer->depth_write == nullptr) {
        return false;
    }
    depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    hr = device->CreateDepthStencilState(&depth_desc, &renderer->depth_read);
    if (FAILED(hr) || renderer->depth_read == nullptr) {
        return false;
    }

    D3D11_BLEND_DESC blend_desc {};
    blend_desc.RenderTarget[0].BlendEnable = FALSE;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = device->CreateBlendState(&blend_desc, &renderer->blend_opaque);
    if (FAILED(hr) || renderer->blend_opaque == nullptr) {
        return false;
    }

    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    hr = device->CreateBlendState(&blend_desc, &renderer->blend_alpha);
    if (FAILED(hr) || renderer->blend_alpha == nullptr) {
        return false;
    }

    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    hr = device->CreateBlendState(&blend_desc, &renderer->blend_additive);
    if (FAILED(hr) || renderer->blend_additive == nullptr) {
        return false;
    }

    blend_desc.RenderTarget[0].BlendEnable = FALSE;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = 0U;
    hr = device->CreateBlendState(&blend_desc, &renderer->blend_depth_only);
    if (FAILED(hr) || renderer->blend_depth_only == nullptr) {
        return false;
    }
    D3D11_SAMPLER_DESC sampler_desc {};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.MaxAnisotropy = 1U;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler_desc.MinLOD = 0.0f;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&sampler_desc, &renderer->linear_sampler);
    if (FAILED(hr) || renderer->linear_sampler == nullptr) {
        return false;
    }
    D3D11_SAMPLER_DESC shadow_sampler_desc {};
    shadow_sampler_desc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    shadow_sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    shadow_sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    shadow_sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    shadow_sampler_desc.MipLODBias = 0.0f;
    shadow_sampler_desc.MaxAnisotropy = 1U;
    shadow_sampler_desc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    shadow_sampler_desc.BorderColor[0] = 1.0f;
    shadow_sampler_desc.BorderColor[1] = 1.0f;
    shadow_sampler_desc.BorderColor[2] = 1.0f;
    shadow_sampler_desc.BorderColor[3] = 1.0f;
    shadow_sampler_desc.MinLOD = 0.0f;
    shadow_sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&shadow_sampler_desc, &renderer->shadow_sampler);
    if (FAILED(hr) || renderer->shadow_sampler == nullptr) {
        return false;
    }
    return true;
}

std::string NormalizeMeshKey(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        if (c == '\\') {
            return static_cast<char>('/');
        }
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string NormalizeRefKey(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        if (c == '\\') {
            return static_cast<char>('/');
        }
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

const char* SkinningMatrixConventionName(SkinningMatrixConvention convention) {
    switch (convention) {
        case SkinningMatrixConvention::DxRowMajor:
            return "dx_row_major";
        case SkinningMatrixConvention::GltfColumnMajor:
            return "gltf_column_major";
        case SkinningMatrixConvention::Unknown:
        default:
            return "unknown";
    }
}

std::string ExtractTerminalToken(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    const std::string key = NormalizeRefKey(path);
    const auto pos = key.find_last_of('/');
    if (pos == std::string::npos) {
        return key;
    }
    if (pos + 1U >= key.size()) {
        return {};
    }
    return key.substr(pos + 1U);
}

void PushAvatarWarningUnique(AvatarPackage* pkg, const std::string& message, const std::string& code) {
    if (pkg == nullptr) {
        return;
    }
    if (!message.empty()) {
        if (std::find(pkg->warnings.begin(), pkg->warnings.end(), message) == pkg->warnings.end()) {
            pkg->warnings.push_back(message);
        }
    }
    if (code.empty()) {
        return;
    }
    if (std::find(pkg->warning_codes.begin(), pkg->warning_codes.end(), code) == pkg->warning_codes.end()) {
        pkg->warning_codes.push_back(code);
    }
}

void RemoveAvatarWarningCode(AvatarPackage* pkg, const std::string& code) {
    if (pkg == nullptr || code.empty()) {
        return;
    }
    pkg->warning_codes.erase(
        std::remove(pkg->warning_codes.begin(), pkg->warning_codes.end(), code),
        pkg->warning_codes.end());
    pkg->warnings.erase(
        std::remove_if(pkg->warnings.begin(), pkg->warnings.end(), [&](const std::string& warning) {
            return warning.find(code) != std::string::npos;
        }),
        pkg->warnings.end());
}

void PushAvatarWarningExclusive(
    AvatarPackage* pkg,
    const std::string& message,
    const std::string& code,
    std::initializer_list<const char*> exclusive_codes) {
    if (pkg == nullptr) {
        return;
    }
    for (const char* exclusive_code : exclusive_codes) {
        if (exclusive_code == nullptr || *exclusive_code == '\0') {
            continue;
        }
        RemoveAvatarWarningCode(pkg, exclusive_code);
    }
    PushAvatarWarningUnique(pkg, message, code);
}

std::string ToLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::vector<std::string> TokenizeLooseFlags(std::string text) {
    for (char& ch : text) {
        const unsigned char uc = static_cast<unsigned char>(ch);
        if ((uc >= 'a' && uc <= 'z') ||
            (uc >= 'A' && uc <= 'Z') ||
            (uc >= '0' && uc <= '9') ||
            uc == '_' ||
            uc == '-') {
            ch = static_cast<char>(std::tolower(uc));
        } else {
            ch = ' ';
        }
    }
    std::vector<std::string> tokens;
    std::istringstream iss(text);
    std::string token;
    while (iss >> token) {
        tokens.push_back(std::move(token));
    }
    return tokens;
}

bool HasLooseToken(const std::vector<std::string>& tokens, const char* token) {
    if (token == nullptr || *token == '\0') {
        return false;
    }
    const std::string key = ToLowerAscii(token);
    return std::find(tokens.begin(), tokens.end(), key) != tokens.end();
}

ExpressionState* FindExpressionByNameCaseInsensitive(std::vector<ExpressionState>* expressions, const std::string& name) {
    if (expressions == nullptr) {
        return nullptr;
    }
    const std::string lowered = ToLowerAscii(name);
    for (auto& expr : *expressions) {
        if (ToLowerAscii(expr.name) == lowered) {
            return &expr;
        }
    }
    return nullptr;
}

const std::array<const char*, 52U> kArkit52Canonical = {
    "browDownLeft", "browDownRight", "browInnerUp", "browOuterUpLeft", "browOuterUpRight",
    "cheekPuff", "cheekSquintLeft", "cheekSquintRight", "eyeBlinkLeft", "eyeBlinkRight",
    "eyeLookDownLeft", "eyeLookDownRight", "eyeLookInLeft", "eyeLookInRight", "eyeLookOutLeft",
    "eyeLookOutRight", "eyeLookUpLeft", "eyeLookUpRight", "eyeSquintLeft", "eyeSquintRight",
    "eyeWideLeft", "eyeWideRight", "jawForward", "jawLeft", "jawOpen",
    "jawRight", "mouthClose", "mouthDimpleLeft", "mouthDimpleRight", "mouthFrownLeft",
    "mouthFrownRight", "mouthFunnel", "mouthLeft", "mouthLowerDownLeft", "mouthLowerDownRight",
    "mouthPressLeft", "mouthPressRight", "mouthPucker", "mouthRight", "mouthRollLower",
    "mouthRollUpper", "mouthShrugLower", "mouthShrugUpper", "mouthSmileLeft", "mouthSmileRight",
    "mouthStretchLeft", "mouthStretchRight", "mouthUpperUpLeft", "mouthUpperUpRight", "noseSneerLeft",
    "noseSneerRight", "tongueOut",
};

const std::unordered_map<std::string, std::vector<std::string>> kArkit52FallbackCandidates = {
    {"eyeblinkleft", {"blinkl", "blinkleft", "eyecloseleft"}},
    {"eyeblinkright", {"blinkr", "blinkright", "eyecloseright"}},
    {"jawopen", {"mouthopen", "visemeaa", "aa"}},
    {"mouthsmileleft", {"smileleft", "smilel", "smile"}},
    {"mouthsmileright", {"smileright", "smiler", "smile"}},
    {"browinnerup", {"browup", "browsup", "innerbrowup"}},
    {"mouthfunnel", {"funnel", "moutho", "visemeo"}},
    {"mouthpucker", {"pucker", "mouthu", "visemeu"}},
};

bool TryResolveArkitFallbackWeight(
    const std::string& canonical_channel,
    const std::unordered_map<std::string, float>& incoming,
    float* out_weight) {
    if (out_weight == nullptr) {
        return false;
    }
    const auto it = kArkit52FallbackCandidates.find(canonical_channel);
    if (it == kArkit52FallbackCandidates.end()) {
        return false;
    }
    for (const auto& candidate : it->second) {
        const auto w_it = incoming.find(candidate);
        if (w_it != incoming.end()) {
            *out_weight = std::max(0.0f, std::min(1.0f, w_it->second));
            return true;
        }
    }
    return false;
}

bool HasArkit52ExpressionBindings(const AvatarPackage& pkg) {
    std::size_t bound_channels = 0U;
    for (const auto* channel : kArkit52Canonical) {
        const auto* expr = static_cast<const ExpressionState*>(nullptr);
        const std::string lowered = ToLowerAscii(channel);
        for (const auto& item : pkg.expressions) {
            if (ToLowerAscii(item.name) == lowered) {
                expr = &item;
                break;
            }
        }
        if (expr != nullptr && !expr->binds.empty()) {
            ++bound_channels;
        }
    }
    return bound_channels > 0U;
}

void BuildArkit52ExpressionBindings(AvatarPackage* pkg) {
    if (pkg == nullptr || pkg->blendshape_payloads.empty()) {
        return;
    }

    std::unordered_map<std::string, std::string> canonical_by_lower;
    canonical_by_lower.reserve(kArkit52Canonical.size());
    for (const auto* channel : kArkit52Canonical) {
        canonical_by_lower[ToLowerAscii(channel)] = channel;
    }

    std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> channel_binds;
    channel_binds.reserve(kArkit52Canonical.size());
    for (const auto& blendshape : pkg->blendshape_payloads) {
        for (const auto& frame : blendshape.frames) {
            const auto it = canonical_by_lower.find(ToLowerAscii(frame.name));
            if (it == canonical_by_lower.end()) {
                continue;
            }
            channel_binds[it->second].push_back({blendshape.mesh_name, frame.name});
        }
    }

    std::vector<std::string> missing_channels;
    missing_channels.reserve(kArkit52Canonical.size());
    for (const auto* channel : kArkit52Canonical) {
        auto* expr = FindExpressionByNameCaseInsensitive(&pkg->expressions, channel);
        if (expr == nullptr) {
            ExpressionState created {};
            created.name = channel;
            created.mapping_kind = channel;
            created.default_weight = 0.0f;
            created.runtime_weight = 0.0f;
            pkg->expressions.push_back(std::move(created));
            expr = &pkg->expressions.back();
        } else {
            expr->mapping_kind = channel;
            expr->default_weight = 0.0f;
            expr->runtime_weight = 0.0f;
        }

        expr->binds.clear();
        std::unordered_set<std::string> bind_keys;
        const auto bind_it = channel_binds.find(channel);
        if (bind_it != channel_binds.end()) {
            bind_keys.reserve(bind_it->second.size());
            for (const auto& [mesh_name, frame_name] : bind_it->second) {
                const std::string key = NormalizeMeshKey(mesh_name) + "|" + ToLowerAscii(frame_name);
                if (!bind_keys.insert(key).second) {
                    continue;
                }
                ExpressionState::Bind bind {};
                bind.mesh_name = mesh_name;
                bind.frame_name = frame_name;
                bind.weight_scale = 1.0f;
                expr->binds.push_back(std::move(bind));
            }
        }
        if (expr->binds.empty()) {
            missing_channels.push_back(channel);
        }
    }

    if (!missing_channels.empty()) {
        std::ostringstream summary;
        summary << "W_EXPRESSION: ARKIT52 strict channel missing binds=" << missing_channels.size() << "/52";
        if (!missing_channels.empty()) {
            summary << " [";
            const std::size_t to_show = std::min<std::size_t>(8U, missing_channels.size());
            for (std::size_t i = 0U; i < to_show; ++i) {
                if (i > 0U) {
                    summary << ",";
                }
                summary << missing_channels[i];
            }
            if (missing_channels.size() > to_show) {
                summary << ",...";
            }
            summary << "]";
        }
        PushAvatarWarningUnique(pkg, summary.str(), "W_ARKIT52_MISSING_BIND");
    }
}

float ClampFinite(float value, float min_v, float max_v, float fallback) {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::max(min_v, std::min(max_v, value));
}

std::vector<std::size_t> ResolveChainTargetMeshes(
    const std::vector<std::string>& bone_paths,
    const std::string& root_bone_path,
    const AvatarPackage& avatar_pkg,
    const std::vector<GpuMeshResource>& meshes) {
    std::vector<std::string> tokens;
    for (const auto& path : bone_paths) {
        const auto token = ExtractTerminalToken(path);
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    const auto root_token = ExtractTerminalToken(root_bone_path);
    if (!root_token.empty()) {
        tokens.push_back(root_token);
    }
    if (tokens.empty()) {
        return {};
    }

    std::unordered_set<std::size_t> target_mesh_index_set;
    // Prefer explicit rig binding when available, then fall back to mesh-name token matching.
    for (const auto& rig : avatar_pkg.skeleton_rig_payloads) {
        const std::string rig_mesh_key = NormalizeMeshKey(rig.mesh_name);
        bool bone_hit = false;
        for (const auto& bone : rig.bones) {
            const std::string bone_key = NormalizeRefKey(bone.bone_name);
            for (const auto& token : tokens) {
                if (!token.empty() && bone_key.find(token) != std::string::npos) {
                    bone_hit = true;
                    break;
                }
            }
            if (bone_hit) {
                break;
            }
        }
        if (!bone_hit) {
            continue;
        }
        for (std::size_t i = 0U; i < meshes.size(); ++i) {
            if (NormalizeMeshKey(meshes[i].mesh_name) == rig_mesh_key) {
                target_mesh_index_set.insert(i);
            }
        }
    }
    for (std::size_t i = 0U; i < meshes.size(); ++i) {
        const auto mesh_key = NormalizeMeshKey(meshes[i].mesh_name);
        for (const auto& token : tokens) {
            if (token.empty()) {
                continue;
            }
            if (mesh_key.find(token) != std::string::npos) {
                target_mesh_index_set.insert(i);
                break;
            }
        }
    }
    std::vector<std::size_t> target_mesh_indices;
    target_mesh_indices.reserve(target_mesh_index_set.size());
    for (const auto idx : target_mesh_index_set) {
        target_mesh_indices.push_back(idx);
    }
    std::sort(target_mesh_indices.begin(), target_mesh_indices.end());
    return target_mesh_indices;
}

bool IsUnsupportedColliderShape(avatar::PhysicsColliderShape shape) {
    return shape == avatar::PhysicsColliderShape::Unknown;
}

const char* PhysicsAutoCorrectedCodeFor(const AvatarPackage& pkg) {
    return pkg.source_type == AvatarSourceType::Vrm ? "VRM_SPRING_AUTO_CORRECTED" : "MIQ_PHYSICS_AUTO_CORRECTED";
}

const char* PhysicsDisabledCodeFor(const AvatarPackage& pkg) {
    return pkg.source_type == AvatarSourceType::Vrm ? "VRM_SPRING_CHAIN_DISABLED" : "MIQ_PHYSICS_CHAIN_DISABLED";
}

const char* PhysicsUnsupportedColliderCodeFor(const AvatarPackage& pkg) {
    return pkg.source_type == AvatarSourceType::Vrm ? "VRM_SPRING_UNSUPPORTED_COLLIDER" : "MIQ_PHYSICS_UNSUPPORTED_COLLIDER";
}

void InitializeSecondaryMotionStateForAvatar(
    std::uint64_t handle,
    const AvatarPackage& avatar_pkg,
    const std::vector<GpuMeshResource>& meshes) {
    auto& state = g_state.secondary_motion_states[handle];
    if (state.initialized) {
        return;
    }
    state.chains.clear();
    state.active_chain_count = 0U;
    state.corrected_chain_count = 0U;
    state.disabled_chain_count = 0U;
    state.unsupported_collider_chain_count = 0U;
    state.constraint_hit_count = 0U;
    state.damping_event_count = 0U;
    state.avg_substeps = 0.0f;
    state.avg_offset_magnitude = 0.0f;
    state.peak_offset_magnitude = 0.0f;
    const bool has_vrc = !avatar_pkg.physbone_payloads.empty();

    std::unordered_set<std::string> collider_keys;
    std::unordered_set<std::string> unsupported_collider_keys;
    for (const auto& c : avatar_pkg.physics_colliders) {
        const auto key = NormalizeRefKey(c.name);
        collider_keys.insert(key);
        if (IsUnsupportedColliderShape(c.shape)) {
            unsupported_collider_keys.insert(key);
        }
    }

    for (const auto& phys : avatar_pkg.physbone_payloads) {
        SecondaryMotionChainRuntime chain {};
        chain.name = phys.name;
        chain.is_vrc = true;
        chain.enabled = phys.enabled;
        chain.radius = ClampFinite(phys.radius, 0.001f, 0.20f, 0.02f);
        chain.stiffness = ClampFinite(phys.spring, 0.05f, 1.0f, 0.35f);
        chain.drag = ClampFinite(phys.pull, 0.0f, 1.0f, 0.25f);
        chain.gravity_y = ClampFinite(phys.gravity[1], -1.0f, 1.0f, -0.15f);
        std::vector<std::string> corrected_bones = phys.bone_paths;
        if (corrected_bones.empty() && !phys.root_bone_path.empty()) {
            corrected_bones.push_back(phys.root_bone_path);
            ++chain.corrected_count;
        }
        if (chain.radius != phys.radius || chain.stiffness != phys.spring || chain.drag != phys.pull) {
            ++chain.corrected_count;
        }
        for (const auto& collider_ref : phys.collider_refs) {
            if (collider_keys.find(NormalizeRefKey(collider_ref)) == collider_keys.end()) {
                ++chain.corrected_count;
            }
        }
        chain.target_mesh_indices = ResolveChainTargetMeshes(corrected_bones, phys.root_bone_path, avatar_pkg, meshes);
        if (chain.target_mesh_indices.empty()) {
            chain.enabled = false;
            ++chain.corrected_count;
        }
        for (const auto& collider_ref : phys.collider_refs) {
            if (unsupported_collider_keys.find(NormalizeRefKey(collider_ref)) != unsupported_collider_keys.end()) {
                ++chain.unsupported_collider_count;
            }
        }
        state.chains.push_back(std::move(chain));
    }
    for (const auto& spring : avatar_pkg.springbone_payloads) {
        SecondaryMotionChainRuntime chain {};
        chain.name = spring.name;
        chain.is_vrc = false;
        chain.enabled = spring.enabled;
        chain.radius = ClampFinite(spring.radius, 0.001f, 0.20f, 0.02f);
        chain.stiffness = ClampFinite(spring.stiffness, 0.05f, 1.0f, 0.35f);
        chain.drag = ClampFinite(spring.drag, 0.0f, 1.0f, 0.25f);
        chain.gravity_y = ClampFinite(spring.gravity[1], -1.0f, 1.0f, -0.15f);
        std::vector<std::string> corrected_bones = spring.bone_paths;
        if (corrected_bones.empty() && !spring.root_bone_path.empty()) {
            corrected_bones.push_back(spring.root_bone_path);
            ++chain.corrected_count;
        }
        if (chain.radius != spring.radius || chain.stiffness != spring.stiffness || chain.drag != spring.drag) {
            ++chain.corrected_count;
        }
        for (const auto& collider_ref : spring.collider_refs) {
            if (collider_keys.find(NormalizeRefKey(collider_ref)) == collider_keys.end()) {
                ++chain.corrected_count;
            }
        }
        chain.target_mesh_indices = ResolveChainTargetMeshes(corrected_bones, spring.root_bone_path, avatar_pkg, meshes);
        if (chain.target_mesh_indices.empty()) {
            chain.enabled = false;
            ++chain.corrected_count;
        }
        for (const auto& collider_ref : spring.collider_refs) {
            if (unsupported_collider_keys.find(NormalizeRefKey(collider_ref)) != unsupported_collider_keys.end()) {
                ++chain.unsupported_collider_count;
            }
        }
        if (has_vrc) {
            chain.enabled = false;
        }
        state.chains.push_back(std::move(chain));
    }

    for (const auto& chain : state.chains) {
        if (chain.enabled) {
            ++state.active_chain_count;
        } else {
            ++state.disabled_chain_count;
        }
        if (chain.corrected_count > 0U) {
            ++state.corrected_chain_count;
        }
        if (chain.unsupported_collider_count > 0U) {
            ++state.unsupported_collider_chain_count;
        }
    }
    state.initialized = true;
}

bool ApplySecondaryMotionToAvatar(
    RendererResources* renderer,
    AvatarPackage* avatar_pkg,
    std::uint64_t handle,
    ID3D11DeviceContext* device_ctx,
    float delta_time_seconds) {
    if (renderer == nullptr || avatar_pkg == nullptr || device_ctx == nullptr) {
        return false;
    }
    if (avatar_pkg->physbone_payloads.empty() && avatar_pkg->springbone_payloads.empty()) {
        return true;
    }
    // Temporary safety gate: MIQ avatars still show unstable per-mesh deformation
    // in some exported rigs; keep base pose until parser/matrix paths are unified.
    if (avatar_pkg->source_type == AvatarSourceType::Miq) {
        return true;
    }

    auto mesh_it = renderer->avatar_meshes.find(handle);
    if (mesh_it == renderer->avatar_meshes.end()) {
        return true;
    }
    auto& meshes = mesh_it->second;
    if (meshes.empty()) {
        return true;
    }

    InitializeSecondaryMotionStateForAvatar(handle, *avatar_pkg, meshes);
    auto state_it = g_state.secondary_motion_states.find(handle);
    if (state_it == g_state.secondary_motion_states.end()) {
        return true;
    }
    auto& state = state_it->second;
    if (!state.warnings_emitted) {
        if (state.corrected_chain_count > 0U) {
            std::ostringstream oss;
            oss << "W_RENDER: " << PhysicsAutoCorrectedCodeFor(*avatar_pkg)
                << ": correctedChains=" << state.corrected_chain_count;
            PushAvatarWarningUnique(avatar_pkg, oss.str(), PhysicsAutoCorrectedCodeFor(*avatar_pkg));
        }
        if (state.disabled_chain_count > 0U) {
            std::ostringstream oss;
            oss << "W_RENDER: " << PhysicsDisabledCodeFor(*avatar_pkg)
                << ": disabledChains=" << state.disabled_chain_count;
            PushAvatarWarningUnique(avatar_pkg, oss.str(), PhysicsDisabledCodeFor(*avatar_pkg));
        }
        if (state.unsupported_collider_chain_count > 0U) {
            std::ostringstream oss;
            oss << "W_RENDER: " << PhysicsUnsupportedColliderCodeFor(*avatar_pkg)
                << ": affectedChains=" << state.unsupported_collider_chain_count;
            PushAvatarWarningUnique(avatar_pkg, oss.str(), PhysicsUnsupportedColliderCodeFor(*avatar_pkg));
        }
        state.warnings_emitted = true;
    }

    const float dt = ClampFinite(delta_time_seconds, 1.0f / 240.0f, 1.0f / 15.0f, 1.0f / 60.0f);
    constexpr float kFixedStep = 1.0f / 120.0f;
    constexpr std::uint32_t kMaxSubsteps = 8U;
    std::uint32_t accumulated_substeps = 0U;
    std::uint32_t stepped_chain_count = 0U;
    std::uint32_t frame_constraint_hits = 0U;
    std::uint32_t frame_damping_events = 0U;
    float frame_offset_mag_sum = 0.0f;
    float frame_offset_mag_peak = 0.0f;
    for (auto& chain : state.chains) {
        if (!chain.enabled || chain.target_mesh_indices.empty()) {
            continue;
        }
        chain.fixed_dt_accumulator = ClampFinite(chain.fixed_dt_accumulator + dt, 0.0f, 0.20f, 0.0f);
        std::uint32_t chain_substeps = 0U;
        while (chain.fixed_dt_accumulator >= kFixedStep && chain_substeps < kMaxSubsteps) {
            const float head_x = ClampFinite(g_state.latest_tracking.head_pos[0], -1.0f, 1.0f, 0.0f);
            const float head_y = ClampFinite(g_state.latest_tracking.head_pos[1], -1.0f, 1.0f, 0.0f);
            const float sway_gain = 0.0015f + chain.radius * 0.17f;
            const float target_x = chain.rest_offset_x + (head_x * sway_gain);
            const float target_y = chain.rest_offset_y + (chain.gravity_y * 0.0038f) + (head_y * 0.0012f);
            const float accel_x = (target_x - chain.offset_x) * (chain.stiffness * 22.0f);
            const float accel_y = (target_y - chain.offset_y) * (chain.stiffness * 24.0f);
            chain.velocity_x += accel_x * kFixedStep;
            chain.velocity_y += accel_y * kFixedStep;
            const float drag_decay = std::max(0.0f, 1.0f - chain.drag * (kFixedStep * 30.0f));
            chain.velocity_x *= drag_decay;
            chain.velocity_y *= drag_decay;
            chain.offset_x += chain.velocity_x * kFixedStep;
            chain.offset_y += chain.velocity_y * kFixedStep;
            const float length_limit = std::max(0.003f, chain.radius * 2.0f);
            const float len_sq = chain.offset_x * chain.offset_x + chain.offset_y * chain.offset_y;
            if (len_sq > (length_limit * length_limit)) {
                const float len = std::sqrt(std::max(len_sq, 1e-8f));
                const float scale = length_limit / len;
                chain.offset_x *= scale;
                chain.offset_y *= scale;
                chain.velocity_x *= 0.45f;
                chain.velocity_y *= 0.45f;
                ++frame_constraint_hits;
            }
            if (chain.unsupported_collider_count > 0U) {
                chain.velocity_x *= 0.9f;
                chain.velocity_y *= 0.9f;
                ++frame_damping_events;
            }
            chain.offset_x = ClampFinite(chain.offset_x, -0.05f, 0.05f, 0.0f);
            chain.offset_y = ClampFinite(chain.offset_y, -0.05f, 0.05f, 0.0f);
            chain.fixed_dt_accumulator -= kFixedStep;
            ++chain_substeps;
        }
        if (chain_substeps == kMaxSubsteps && chain.fixed_dt_accumulator >= kFixedStep) {
            chain.fixed_dt_accumulator = std::fmod(chain.fixed_dt_accumulator, kFixedStep);
        }
        chain.last_substeps = chain_substeps;
        accumulated_substeps += chain_substeps;
        ++stepped_chain_count;
        const float offset_mag = std::sqrt(std::max(0.0f, chain.offset_x * chain.offset_x + chain.offset_y * chain.offset_y));
        frame_offset_mag_sum += offset_mag;
        frame_offset_mag_peak = std::max(frame_offset_mag_peak, offset_mag);

        for (const auto mesh_index : chain.target_mesh_indices) {
            if (mesh_index >= meshes.size()) {
                continue;
            }
            auto& mesh = meshes[mesh_index];
            if (mesh.vertex_buffer == nullptr || mesh.vertex_stride < 12U) {
                continue;
            }

            auto& blob = mesh.deformed_vertex_blob;
            if (blob.empty()) {
                blob = mesh.base_vertex_blob;
            }
            const std::size_t stride = mesh.vertex_stride;
            const float min_y = mesh.bounds_min.y;
            const float max_y = mesh.bounds_max.y;
            const float inv_extent_y = 1.0f / std::max(0.0001f, max_y - min_y);
            for (std::uint32_t vi = 0U; vi < mesh.vertex_count; ++vi) {
                const std::size_t base = static_cast<std::size_t>(vi) * stride;
                if (base + 12U > blob.size()) {
                    break;
                }
                float px = 0.0f;
                float py = 0.0f;
                std::memcpy(&px, blob.data() + base, sizeof(float));
                std::memcpy(&py, blob.data() + base + 4U, sizeof(float));
                const float height_w = std::max(0.0f, std::min(1.0f, (py - min_y) * inv_extent_y));
                const float influence = 0.25f + (height_w * height_w) * 0.75f;
                px += chain.offset_x * influence;
                py += chain.offset_y * influence;
                std::memcpy(blob.data() + base, &px, sizeof(float));
                std::memcpy(blob.data() + base + 4U, &py, sizeof(float));
            }

            D3D11_MAPPED_SUBRESOURCE mapped {};
            if (SUCCEEDED(device_ctx->Map(mesh.vertex_buffer, 0U, D3D11_MAP_WRITE_DISCARD, 0U, &mapped))) {
                std::memcpy(mapped.pData, blob.data(), blob.size());
                device_ctx->Unmap(mesh.vertex_buffer, 0U);
            }
        }
    }
    state.avg_substeps = stepped_chain_count > 0U
        ? static_cast<float>(accumulated_substeps) / static_cast<float>(stepped_chain_count)
        : 0.0f;
    state.constraint_hit_count = frame_constraint_hits;
    state.damping_event_count = frame_damping_events;
    state.avg_offset_magnitude = stepped_chain_count > 0U
        ? frame_offset_mag_sum / static_cast<float>(stepped_chain_count)
        : 0.0f;
    state.peak_offset_magnitude = frame_offset_mag_peak;

    return true;
}

bool ShouldApplyExperimentalStaticSkinning() {
    enum class StaticSkinningEnvMode {
        Auto = 0,
        ForceOn,
        ForceOff,
    };
    static const StaticSkinningEnvMode mode = []() {
        const char* raw = std::getenv("ANIMIQ_MIQ_ENABLE_STATIC_SKINNING");
        if (raw == nullptr) {
            return StaticSkinningEnvMode::Auto;
        }
        std::string token(raw);
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (token == "0" || token == "false" || token == "no" || token == "off") {
            return StaticSkinningEnvMode::ForceOff;
        }
        if (token == "1" || token == "true" || token == "yes" || token == "on") {
            return StaticSkinningEnvMode::ForceOn;
        }
        if (token == "auto") {
            return StaticSkinningEnvMode::Auto;
        }
        return StaticSkinningEnvMode::Auto;
    }();
    return mode == StaticSkinningEnvMode::ForceOn;
}

bool ShouldApplyStaticSkinningForAvatarMeshes(const AvatarPackage& avatar_pkg) {
    enum class StaticSkinningEnvMode {
        Auto = 0,
        ForceOn,
        ForceOff,
    };
    static const StaticSkinningEnvMode mode = []() {
        const char* raw = std::getenv("ANIMIQ_MIQ_ENABLE_STATIC_SKINNING");
        if (raw == nullptr) {
            return StaticSkinningEnvMode::Auto;
        }
        std::string token(raw);
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (token == "0" || token == "false" || token == "no" || token == "off") {
            return StaticSkinningEnvMode::ForceOff;
        }
        if (token == "1" || token == "true" || token == "yes" || token == "on") {
            return StaticSkinningEnvMode::ForceOn;
        }
        return StaticSkinningEnvMode::Auto;
    }();
    if (mode == StaticSkinningEnvMode::ForceOn) {
        return true;
    }
    if (mode == StaticSkinningEnvMode::ForceOff) {
        return false;
    }
    // Auto mode: enable MIQ static skinning when payloads are present.
    // Per-mesh collapse guards will reject unsafe posed output.
    if (avatar_pkg.source_type == AvatarSourceType::Miq) {
        if (avatar_pkg.source_ext == ".vrm") {
            // Root fix: VRM-origin MIQ can produce mixed mesh-space outcomes
            // (face/hair/body desync) when runtime static skinning is applied.
            return false;
        }
        return !avatar_pkg.skin_payloads.empty() && !avatar_pkg.skeleton_payloads.empty();
    }
    return false;
}

bool ShouldApplyArmPoseForAvatar(const AvatarPackage& avatar_pkg) {
    enum class StaticSkinningEnvMode {
        Auto = 0,
        ForceOn,
        ForceOff,
    };
    static const StaticSkinningEnvMode mode = []() {
        const char* raw = std::getenv("ANIMIQ_MIQ_ENABLE_STATIC_SKINNING");
        if (raw == nullptr) {
            return StaticSkinningEnvMode::Auto;
        }
        std::string token(raw);
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (token == "0" || token == "false" || token == "no" || token == "off") {
            return StaticSkinningEnvMode::ForceOff;
        }
        if (token == "1" || token == "true" || token == "yes" || token == "on") {
            return StaticSkinningEnvMode::ForceOn;
        }
        return StaticSkinningEnvMode::Auto;
    }();
    if (mode == StaticSkinningEnvMode::ForceOn) {
        return true;
    }
    if (mode == StaticSkinningEnvMode::ForceOff) {
        return false;
    }
    // Auto mode: allow arm pose for MIQ when the runtime has complete pose payloads.
    // Keep mesh static-skinning policy separate to avoid regressions in mesh-space safety rules.
    return avatar_pkg.source_type == AvatarSourceType::Miq &&
        !avatar_pkg.skin_payloads.empty() &&
        !avatar_pkg.skeleton_payloads.empty() &&
        !avatar_pkg.skeleton_rig_payloads.empty();
}

bool ShouldUseConservativeMiqMaterialPath() {
    const char* raw = std::getenv("ANIMIQ_MIQ_CONSERVATIVE_MATERIAL");
    if (raw == nullptr) {
        return false;
    }
    std::string token(raw);
    std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (token == "0" || token == "false" || token == "no" || token == "off") {
        return false;
    }
    return token == "1" || token == "true" || token == "yes" || token == "on";
}

bool ShouldAllowMiqTextureAliasFallback() {
    const char* raw = std::getenv("ANIMIQ_MIQ_ALLOW_TEXTURE_ALIAS_FALLBACK");
    if (raw == nullptr) {
        return false;
    }
    std::string token(raw);
    std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return token == "1" || token == "true" || token == "yes" || token == "on";
}

bool ShouldAutoDetectMiqSkinningConvention() {
    const char* raw = std::getenv("ANIMIQ_MIQ_ENABLE_CONVENTION_AUTODETECT");
    if (raw == nullptr) {
        return false;
    }
    std::string token(raw);
    std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return token == "1" || token == "true" || token == "yes" || token == "on";
}

enum class MiqOutlierDrawPolicy {
    AutoFitOnly = 0,
    SkipDraw,
};

MiqOutlierDrawPolicy ResolveMiqOutlierDrawPolicy() {
    static const MiqOutlierDrawPolicy policy = []() {
        const char* raw = std::getenv("ANIMIQ_MIQ_OUTLIER_DRAW_POLICY");
        if (raw == nullptr) {
            return MiqOutlierDrawPolicy::AutoFitOnly;
        }
        std::string token(raw);
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (token == "skip_draw" || token == "skip-draw" ||
            token == "1" || token == "true" || token == "yes" || token == "on") {
            return MiqOutlierDrawPolicy::SkipDraw;
        }
        if (token == "autofit_only" || token == "autofit-only" ||
            token == "0" || token == "false" || token == "no" || token == "off") {
            return MiqOutlierDrawPolicy::AutoFitOnly;
        }
        return MiqOutlierDrawPolicy::AutoFitOnly;
    }();
    return policy;
}

void SanitizeTrackingFrame(NcTrackingFrame* frame) {
    if (frame == nullptr) {
        return;
    }
    auto clamp_finite = [](float value, float min_v, float max_v, float fallback) {
        if (!std::isfinite(value)) {
            return fallback;
        }
        return std::max(min_v, std::min(max_v, value));
    };
    frame->head_pos[0] = clamp_finite(frame->head_pos[0], -2.5f, 2.5f, 0.0f);
    frame->head_pos[1] = clamp_finite(frame->head_pos[1], -2.5f, 2.5f, 0.0f);
    frame->head_pos[2] = clamp_finite(frame->head_pos[2], -2.5f, 2.5f, 0.0f);

    float qx = frame->head_rot_quat[0];
    float qy = frame->head_rot_quat[1];
    float qz = frame->head_rot_quat[2];
    float qw = frame->head_rot_quat[3];
    if (!std::isfinite(qx) || !std::isfinite(qy) || !std::isfinite(qz) || !std::isfinite(qw)) {
        qx = 0.0f;
        qy = 0.0f;
        qz = 0.0f;
        qw = 1.0f;
    } else {
        const float qlen = std::sqrt((qx * qx) + (qy * qy) + (qz * qz) + (qw * qw));
        if (qlen <= 1e-6f || !std::isfinite(qlen)) {
            qx = 0.0f;
            qy = 0.0f;
            qz = 0.0f;
            qw = 1.0f;
        } else {
            const float inv = 1.0f / qlen;
            qx *= inv;
            qy *= inv;
            qz *= inv;
            qw *= inv;
        }
    }
    frame->head_rot_quat[0] = qx;
    frame->head_rot_quat[1] = qy;
    frame->head_rot_quat[2] = qz;
    frame->head_rot_quat[3] = qw;

    frame->eye_gaze_l[0] = clamp_finite(frame->eye_gaze_l[0], -1.0f, 1.0f, 0.0f);
    frame->eye_gaze_l[1] = clamp_finite(frame->eye_gaze_l[1], -1.0f, 1.0f, 0.0f);
    frame->eye_gaze_l[2] = clamp_finite(frame->eye_gaze_l[2], -1.0f, 1.0f, 0.0f);
    frame->eye_gaze_r[0] = clamp_finite(frame->eye_gaze_r[0], -1.0f, 1.0f, 0.0f);
    frame->eye_gaze_r[1] = clamp_finite(frame->eye_gaze_r[1], -1.0f, 1.0f, 0.0f);
    frame->eye_gaze_r[2] = clamp_finite(frame->eye_gaze_r[2], -1.0f, 1.0f, 0.0f);
    frame->blink_l = clamp_finite(frame->blink_l, 0.0f, 1.0f, 0.0f);
    frame->blink_r = clamp_finite(frame->blink_r, 0.0f, 1.0f, 0.0f);
    frame->mouth_open = clamp_finite(frame->mouth_open, 0.0f, 1.0f, 0.0f);
}

bool IsValidPoseBoneId(std::uint32_t bone_id) {
    return bone_id <= static_cast<std::uint32_t>(NC_POSE_BONE_RIGHT_HAND);
}

NcPoseBoneOffset SanitizePoseOffset(const NcPoseBoneOffset& src) {
    NcPoseBoneOffset out = src;
    if (!IsValidPoseBoneId(out.bone_id)) {
        out.bone_id = static_cast<std::uint32_t>(NC_POSE_BONE_UNKNOWN);
    }
    auto clamp_finite = [](float value, float min_v, float max_v) {
        if (!std::isfinite(value)) {
            return 0.0f;
        }
        return std::max(min_v, std::min(max_v, value));
    };
    float pitch_min = -180.0f;
    float pitch_max = 180.0f;
    out.pitch_deg = clamp_finite(out.pitch_deg, pitch_min, pitch_max);
    out.yaw_deg = clamp_finite(out.yaw_deg, -180.0f, 180.0f);
    out.roll_deg = clamp_finite(out.roll_deg, -180.0f, 180.0f);
    return out;
}

NcPoseBoneOffset GetPoseOffset(std::uint32_t bone_id) {
    if (!IsValidPoseBoneId(bone_id) || bone_id >= g_state.pose_offsets.size()) {
        NcPoseBoneOffset out {};
        out.bone_id = static_cast<std::uint32_t>(NC_POSE_BONE_UNKNOWN);
        return out;
    }
    return g_state.pose_offsets[bone_id];
}

struct SkinWeight4 {
    std::array<std::int32_t, 4U> bone_indices = {0, 0, 0, 0};
    std::array<float, 4U> weights = {0.0f, 0.0f, 0.0f, 0.0f};
};

constexpr std::size_t kPoseOffsetCapacity = 15U;

std::array<NcPoseBoneOffset, kPoseOffsetCapacity> MakeDefaultPoseOffsets() {
    std::array<NcPoseBoneOffset, kPoseOffsetCapacity> out {};
    for (std::size_t i = 0U; i < out.size(); ++i) {
        out[i].bone_id = static_cast<std::uint32_t>(i);
        out[i].pitch_deg = 0.0f;
        out[i].yaw_deg = 0.0f;
        out[i].roll_deg = 0.0f;
    }
    return out;
}

struct SkinQualityCheck {
    bool valid = false;
    std::string code;
    std::string detail;
};

bool DecodeSkinWeights(
    const std::vector<std::uint8_t>& skin_weight_blob,
    std::uint32_t vertex_count,
    std::vector<SkinWeight4>* out_weights) {
    if (out_weights == nullptr) {
        return false;
    }
    constexpr std::size_t kBytesPerVertex = 32U;  // 4*int32 + 4*float32
    const std::size_t expected = static_cast<std::size_t>(vertex_count) * kBytesPerVertex;
    if (skin_weight_blob.size() != expected) {
        return false;
    }
    out_weights->assign(static_cast<std::size_t>(vertex_count), SkinWeight4{});
    for (std::uint32_t vi = 0U; vi < vertex_count; ++vi) {
        const std::size_t base = static_cast<std::size_t>(vi) * kBytesPerVertex;
        auto& dst = (*out_weights)[vi];
        for (std::size_t i = 0U; i < 4U; ++i) {
            std::memcpy(&dst.bone_indices[i], skin_weight_blob.data() + base + i * 4U, sizeof(std::int32_t));
        }
        for (std::size_t i = 0U; i < 4U; ++i) {
            std::memcpy(&dst.weights[i], skin_weight_blob.data() + base + 16U + i * 4U, sizeof(float));
        }
    }
    return true;
}

bool IsValidSkeletonPosePayload(
    const avatar::SkinRenderPayload& skin_payload,
    const avatar::SkeletonRenderPayload& skeleton_payload) {
    const std::size_t bind_pose_count = skin_payload.bind_poses_16xn.size() / 16U;
    if (bind_pose_count == 0U) {
        return false;
    }
    for (const float v : skin_payload.bind_poses_16xn) {
        if (!std::isfinite(v)) {
            return false;
        }
    }
    if ((skeleton_payload.bone_matrices_16xn.size() % 16U) != 0U) {
        return false;
    }
    for (const float v : skeleton_payload.bone_matrices_16xn) {
        if (!std::isfinite(v)) {
            return false;
        }
    }
    const std::size_t skeleton_matrix_count = skeleton_payload.bone_matrices_16xn.size() / 16U;
    return skeleton_matrix_count >= bind_pose_count;
}

SkinQualityCheck ValidateSkinPayload(
    const avatar::MeshRenderPayload& mesh_payload,
    const avatar::SkinRenderPayload& skin_payload) {
    SkinQualityCheck out {};
    const std::uint32_t src_stride = mesh_payload.vertex_stride >= 12U ? mesh_payload.vertex_stride : 12U;
    if (mesh_payload.vertex_blob.empty() || (mesh_payload.vertex_blob.size() % src_stride) != 0U) {
        out.code = "MIQ_SKIN_MESH_VERTEX_LAYOUT_INVALID";
        out.detail = "mesh vertex blob/stride is invalid";
        return out;
    }
    if ((skin_payload.bind_poses_16xn.size() % 16U) != 0U || skin_payload.bind_poses_16xn.empty()) {
        out.code = "MIQ_SKIN_BINDPOSE_INVALID";
        out.detail = "bind pose array must be a non-empty multiple of 16";
        return out;
    }
    const std::uint32_t vertex_count = static_cast<std::uint32_t>(mesh_payload.vertex_blob.size() / src_stride);
    constexpr std::size_t kBytesPerVertex = 32U;
    if (skin_payload.skin_weight_blob.size() != static_cast<std::size_t>(vertex_count) * kBytesPerVertex) {
        out.code = "MIQ_SKIN_WEIGHT_BLOB_SIZE_MISMATCH";
        out.detail = "skin weight blob size does not match vertex count";
        return out;
    }
    std::vector<SkinWeight4> decoded_weights;
    if (!DecodeSkinWeights(skin_payload.skin_weight_blob, vertex_count, &decoded_weights)) {
        out.code = "MIQ_SKIN_WEIGHT_BLOB_DECODE_FAILED";
        out.detail = "skin weight blob decode failed";
        return out;
    }
    const std::size_t bind_pose_count = skin_payload.bind_poses_16xn.size() / 16U;
    for (std::uint32_t vi = 0U; vi < vertex_count; ++vi) {
        float sum = 0.0f;
        const auto& sw = decoded_weights[vi];
        for (std::size_t wi = 0U; wi < 4U; ++wi) {
            const auto bone_index = sw.bone_indices[wi];
            const float w = sw.weights[wi];
            if (w <= 0.0f) {
                continue;
            }
            if (bone_index < 0 || static_cast<std::size_t>(bone_index) >= bind_pose_count) {
                out.code = "MIQ_SKIN_BONE_INDEX_OOB";
                out.detail = "bone index is out of bind-pose range";
                return out;
            }
            sum += w;
        }
        if (sum > 0.0f && std::abs(sum - 1.0f) > 0.2f) {
            out.code = "MIQ_SKIN_WEIGHT_SUM_INVALID";
            out.detail = "per-vertex skin weight sum deviates from 1.0";
            return out;
        }
    }
    out.valid = true;
    return out;
}

bool ApplyStaticSkinningToVertexBlob(
    std::vector<std::uint8_t>* vertex_blob,
    std::uint32_t vertex_stride,
    const avatar::SkinRenderPayload& skin_payload,
    const avatar::SkeletonRenderPayload* skeleton_payload,
    SkinningMatrixConvention convention_hint,
    SkinningMatrixConvention* out_selected_convention,
    bool* out_ambiguous_selection) {
    if (out_selected_convention != nullptr) {
        *out_selected_convention = SkinningMatrixConvention::Unknown;
    }
    if (out_ambiguous_selection != nullptr) {
        *out_ambiguous_selection = false;
    }
    if (vertex_blob == nullptr || vertex_stride < 12U || vertex_blob->empty()) {
        return false;
    }
    if ((vertex_blob->size() % vertex_stride) != 0U) {
        return false;
    }
    if (skin_payload.bind_poses_16xn.empty() || skin_payload.skin_weight_blob.empty()) {
        return false;
    }
    if ((skin_payload.bind_poses_16xn.size() % 16U) != 0U) {
        return false;
    }
    const std::uint32_t vertex_count = static_cast<std::uint32_t>(vertex_blob->size() / vertex_stride);
    std::vector<SkinWeight4> decoded_weights;
    if (!DecodeSkinWeights(skin_payload.skin_weight_blob, vertex_count, &decoded_weights)) {
        return false;
    }

    const std::size_t bind_pose_count = skin_payload.bind_poses_16xn.size() / 16U;
    const bool has_skeleton_pose =
        skeleton_payload != nullptr && IsValidSkeletonPosePayload(skin_payload, *skeleton_payload);
    if (!has_skeleton_pose) {
        // inverseBind without matching skeleton pose is not a safe reconstruction path.
        // Keep original vertices to avoid catastrophic spikes/collapse.
        return true;
    }

    std::vector<DirectX::XMMATRIX> bind_matrices(bind_pose_count, DirectX::XMMatrixIdentity());
    std::vector<DirectX::XMMATRIX> bone_matrices(bind_pose_count, DirectX::XMMatrixIdentity());
    for (std::size_t i = 0U; i < bind_pose_count; ++i) {
        DirectX::XMFLOAT4X4 bind_pose {};
        for (std::size_t j = 0U; j < 16U; ++j) {
            reinterpret_cast<float*>(&bind_pose)[j] = skin_payload.bind_poses_16xn[i * 16U + j];
        }
        bind_matrices[i] = DirectX::XMLoadFloat4x4(&bind_pose);
        DirectX::XMFLOAT4X4 bone_m {};
        for (std::size_t j = 0U; j < 16U; ++j) {
            reinterpret_cast<float*>(&bone_m)[j] = skeleton_payload->bone_matrices_16xn[i * 16U + j];
        }
        bone_matrices[i] = DirectX::XMLoadFloat4x4(&bone_m);
    }

    auto build_skin_matrices = [&](SkinningMatrixConvention convention) {
        std::vector<DirectX::XMMATRIX> skin_matrices(bind_pose_count, DirectX::XMMatrixIdentity());
        for (std::size_t i = 0U; i < bind_pose_count; ++i) {
            if (convention == SkinningMatrixConvention::GltfColumnMajor) {
                // glTF column-major matrices loaded into XMMATRIX via XMLoadFloat4x4 
                // result in row-major matrices in DX layout (transposed).
                // To compute V * (IBM_row * Bone_row), we multiply IBM * Bone.
                // However, if the source was true column-major, and we want 
                // (Bone_col * IBM_col * V_col)^T = V_row * IBM_row * Bone_row.
                skin_matrices[i] = DirectX::XMMatrixMultiply(bind_matrices[i], bone_matrices[i]);
            } else {
                // Default to standard row-vector skinning order: V * Bind * Bone.
                skin_matrices[i] = DirectX::XMMatrixMultiply(bind_matrices[i], bone_matrices[i]);
            }
        }
        return skin_matrices;
    };

    struct PositionStats {
        bool finite = true;
        float max_abs = 0.0f;
        float extent_x = 0.0f;
        float extent_y = 0.0f;
        float extent_z = 0.0f;
        float extent_max = 0.0f;
        float extent_min = 0.0f;
    };
    auto compute_blob_stats = [&](const std::vector<std::uint8_t>& blob) {
        PositionStats stats {};
        if (blob.empty() || (blob.size() % vertex_stride) != 0U) {
            stats.finite = false;
            return stats;
        }
        float bmin_x = std::numeric_limits<float>::max();
        float bmin_y = std::numeric_limits<float>::max();
        float bmin_z = std::numeric_limits<float>::max();
        float bmax_x = -std::numeric_limits<float>::max();
        float bmax_y = -std::numeric_limits<float>::max();
        float bmax_z = -std::numeric_limits<float>::max();
        const std::size_t vtx_count = blob.size() / vertex_stride;
        for (std::size_t i = 0U; i < vtx_count; ++i) {
            const std::size_t base = i * vertex_stride;
            float px = 0.0f;
            float py = 0.0f;
            float pz = 0.0f;
            std::memcpy(&px, blob.data() + base, sizeof(float));
            std::memcpy(&py, blob.data() + base + 4U, sizeof(float));
            std::memcpy(&pz, blob.data() + base + 8U, sizeof(float));
            if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz)) {
                stats.finite = false;
                return stats;
            }
            stats.max_abs = std::max(stats.max_abs, std::max(std::abs(px), std::max(std::abs(py), std::abs(pz))));
            bmin_x = std::min(bmin_x, px);
            bmin_y = std::min(bmin_y, py);
            bmin_z = std::min(bmin_z, pz);
            bmax_x = std::max(bmax_x, px);
            bmax_y = std::max(bmax_y, py);
            bmax_z = std::max(bmax_z, pz);
        }
        stats.extent_x = std::max(0.0f, bmax_x - bmin_x);
        stats.extent_y = std::max(0.0f, bmax_y - bmin_y);
        stats.extent_z = std::max(0.0f, bmax_z - bmin_z);
        stats.extent_max = std::max(stats.extent_x, std::max(stats.extent_y, stats.extent_z));
        stats.extent_min = std::min(stats.extent_x, std::min(stats.extent_y, stats.extent_z));
        return stats;
    };
    auto score_candidate = [&](const PositionStats& pre, const PositionStats& post) {
        if (!post.finite) {
            return std::numeric_limits<float>::max();
        }
        const float pre_extent = std::max(0.0001f, pre.extent_max);
        const float post_extent = std::max(0.0001f, post.extent_max);
        const float extent_ratio = post_extent / pre_extent;
        const float pre_volume =
            std::max(0.0001f, pre.extent_x) *
            std::max(0.0001f, pre.extent_y) *
            std::max(0.0001f, pre.extent_z);
        const float post_volume =
            std::max(0.0001f, post.extent_x) *
            std::max(0.0001f, post.extent_y) *
            std::max(0.0001f, post.extent_z);
        const float volume_ratio = post_volume / pre_volume;
        const float axis_ratio = std::max(0.0f, post.extent_min) / std::max(0.0001f, post.extent_max);
        float score = std::abs(std::log(std::max(0.01f, std::min(100.0f, extent_ratio))));
        score += std::abs(std::log(std::max(0.01f, std::min(100.0f, volume_ratio))));
        if (axis_ratio < 0.04f) {
            score += 8.0f;
        }
        if (post_extent < (pre_extent * 0.12f) || post_extent > (pre_extent * 8.0f)) {
            score += 4.0f;
        }
        if (post.max_abs > std::max(200.0f, pre.max_abs * 20.0f)) {
            score += 8.0f;
        }
        return score;
    };

    SkinningMatrixConvention selected = convention_hint;
    if (selected == SkinningMatrixConvention::Unknown) {
        const auto pre_stats = compute_blob_stats(*vertex_blob);
        const auto dx_matrices = build_skin_matrices(SkinningMatrixConvention::DxRowMajor);
        const auto gltf_matrices = build_skin_matrices(SkinningMatrixConvention::GltfColumnMajor);

        auto apply_to_copy = [&](const std::vector<DirectX::XMMATRIX>& skin_matrices) {
            std::vector<std::uint8_t> copy = *vertex_blob;
            for (std::uint32_t vi = 0U; vi < vertex_count; ++vi) {
                const std::size_t base = static_cast<std::size_t>(vi) * vertex_stride;
                float px = 0.0f;
                float py = 0.0f;
                float pz = 0.0f;
                std::memcpy(&px, copy.data() + base, sizeof(float));
                std::memcpy(&py, copy.data() + base + 4U, sizeof(float));
                std::memcpy(&pz, copy.data() + base + 8U, sizeof(float));
                const auto p = DirectX::XMVectorSet(px, py, pz, 1.0f);
                const auto& sw = decoded_weights[vi];
                DirectX::XMVECTOR accum = DirectX::XMVectorZero();
                float total_weight = 0.0f;
                for (std::size_t wi = 0U; wi < 4U; ++wi) {
                    const float w = sw.weights[wi];
                    const auto bone_index = sw.bone_indices[wi];
                    if (w <= 0.000001f || bone_index < 0 ||
                        static_cast<std::size_t>(bone_index) >= skin_matrices.size()) {
                        continue;
                    }
                    const auto tp =
                        DirectX::XMVector3TransformCoord(p, skin_matrices[static_cast<std::size_t>(bone_index)]);
                    accum = DirectX::XMVectorAdd(accum, DirectX::XMVectorScale(tp, w));
                    total_weight += w;
                }
                if (total_weight <= 0.000001f) {
                    continue;
                }
                if (std::abs(total_weight - 1.0f) > 0.0001f) {
                    accum = DirectX::XMVectorScale(accum, 1.0f / total_weight);
                }
                DirectX::XMFLOAT3 out_pos {};
                DirectX::XMStoreFloat3(&out_pos, accum);
                std::memcpy(copy.data() + base, &out_pos.x, sizeof(float));
                std::memcpy(copy.data() + base + 4U, &out_pos.y, sizeof(float));
                std::memcpy(copy.data() + base + 8U, &out_pos.z, sizeof(float));
            }
            return copy;
        };

        const auto dx_stats = compute_blob_stats(apply_to_copy(dx_matrices));
        const auto gltf_stats = compute_blob_stats(apply_to_copy(gltf_matrices));
        const float dx_score = score_candidate(pre_stats, dx_stats);
        const float gltf_score = score_candidate(pre_stats, gltf_stats);
        if (std::abs(dx_score - gltf_score) < 0.35f) {
            if (out_ambiguous_selection != nullptr) {
                *out_ambiguous_selection = true;
            }
        }
        selected = (gltf_score < dx_score) ? SkinningMatrixConvention::GltfColumnMajor : SkinningMatrixConvention::DxRowMajor;
    }

    if (out_selected_convention != nullptr) {
        *out_selected_convention = selected;
    }
    const auto skin_matrices = build_skin_matrices(selected);
    for (std::uint32_t vi = 0U; vi < vertex_count; ++vi) {
        const std::size_t base = static_cast<std::size_t>(vi) * vertex_stride;
        float px = 0.0f;
        float py = 0.0f;
        float pz = 0.0f;
        std::memcpy(&px, vertex_blob->data() + base, sizeof(float));
        std::memcpy(&py, vertex_blob->data() + base + 4U, sizeof(float));
        std::memcpy(&pz, vertex_blob->data() + base + 8U, sizeof(float));
        const auto p = DirectX::XMVectorSet(px, py, pz, 1.0f);

        const auto& sw = decoded_weights[vi];
        DirectX::XMVECTOR accum = DirectX::XMVectorZero();
        float total_weight = 0.0f;
        for (std::size_t i = 0U; i < 4U; ++i) {
            const float w = sw.weights[i];
            const auto bone_index = sw.bone_indices[i];
            if (w <= 0.000001f || bone_index < 0 || static_cast<std::size_t>(bone_index) >= skin_matrices.size()) {
                continue;
            }
            const auto tp = DirectX::XMVector3TransformCoord(p, skin_matrices[static_cast<std::size_t>(bone_index)]);
            accum = DirectX::XMVectorAdd(accum, DirectX::XMVectorScale(tp, w));
            total_weight += w;
        }
        if (total_weight <= 0.000001f) {
            continue;
        }
        if (std::abs(total_weight - 1.0f) > 0.0001f) {
            accum = DirectX::XMVectorScale(accum, 1.0f / total_weight);
        }
        DirectX::XMFLOAT3 out_pos {};
        DirectX::XMStoreFloat3(&out_pos, accum);
        std::memcpy(vertex_blob->data() + base, &out_pos.x, sizeof(float));
        std::memcpy(vertex_blob->data() + base + 4U, &out_pos.y, sizeof(float));
        std::memcpy(vertex_blob->data() + base + 8U, &out_pos.z, sizeof(float));
    }
    return true;
}

void RecomputeMeshBoundsFromVertexBlob(GpuMeshResource* mesh) {
    if (mesh == nullptr || mesh->base_vertex_blob.empty() || mesh->vertex_stride < 12U || mesh->vertex_count == 0U) {
        return;
    }
    DirectX::XMFLOAT3 center = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 bmin = {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()};
    DirectX::XMFLOAT3 bmax = {
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max()};
    const auto* bytes = mesh->base_vertex_blob.data();
    for (std::uint32_t i = 0U; i < mesh->vertex_count; ++i) {
        const std::size_t base = static_cast<std::size_t>(i) * mesh->vertex_stride;
        float px = 0.0f;
        float py = 0.0f;
        float pz = 0.0f;
        std::memcpy(&px, bytes + base, sizeof(float));
        std::memcpy(&py, bytes + base + 4U, sizeof(float));
        std::memcpy(&pz, bytes + base + 8U, sizeof(float));
        center.x += px;
        center.y += py;
        center.z += pz;
        bmin.x = std::min(bmin.x, px);
        bmin.y = std::min(bmin.y, py);
        bmin.z = std::min(bmin.z, pz);
        bmax.x = std::max(bmax.x, px);
        bmax.y = std::max(bmax.y, py);
        bmax.z = std::max(bmax.z, pz);
    }
    const float inv_count = 1.0f / static_cast<float>(mesh->vertex_count);
    center.x *= inv_count;
    center.y *= inv_count;
    center.z *= inv_count;
    mesh->center = center;
    mesh->bounds_min = bmin;
    mesh->bounds_max = bmax;
}

bool UploadMeshVertexBlob(GpuMeshResource* mesh, ID3D11DeviceContext* device_ctx) {
    if (mesh == nullptr || device_ctx == nullptr || mesh->vertex_buffer == nullptr || mesh->base_vertex_blob.empty()) {
        return false;
    }
    D3D11_MAPPED_SUBRESOURCE mapped {};
    if (FAILED(device_ctx->Map(mesh->vertex_buffer, 0U, D3D11_MAP_WRITE_DISCARD, 0U, &mapped))) {
        return false;
    }
    std::memcpy(mapped.pData, mesh->base_vertex_blob.data(), mesh->base_vertex_blob.size());
    device_ctx->Unmap(mesh->vertex_buffer, 0U);
    return true;
}

void ResetAvatarMeshesToBindPose(
    RendererResources* renderer,
    std::uint64_t handle,
    ID3D11DeviceContext* device_ctx) {
    if (renderer == nullptr || device_ctx == nullptr) {
        return;
    }
    auto mesh_it = renderer->avatar_meshes.find(handle);
    if (mesh_it == renderer->avatar_meshes.end()) {
        return;
    }
    for (auto& mesh : mesh_it->second) {
        if (mesh.bind_pose_vertex_blob.empty()) {
            continue;
        }
        mesh.base_vertex_blob = mesh.bind_pose_vertex_blob;
        mesh.deformed_vertex_blob = mesh.base_vertex_blob;
        RecomputeMeshBoundsFromVertexBlob(&mesh);
        (void)UploadMeshVertexBlob(&mesh, device_ctx);
    }
    g_state.arm_pose_states.erase(handle);
}

bool BuildGpuMeshForPayload(
    const avatar::MeshRenderPayload& payload,
    const avatar::SkinRenderPayload* skin_payload,
    const avatar::SkeletonRenderPayload* skeleton_payload,
    SkinningMatrixConvention skinning_convention_hint,
    bool enable_static_skinning,
    bool force_static_skinning_fallback,
    SkinningMatrixConvention* out_selected_convention,
    bool* out_ambiguous_convention,
    bool* collapse_guard_triggered,
    bool* out_normals_generated,
    bool* out_normals_generation_failed,
    ID3D11Device* device,
    GpuMeshResource* out_mesh) {
    if (device == nullptr || out_mesh == nullptr || payload.vertex_blob.empty()) {
        std::ostringstream message;
        message << "gpu mesh build invalid input: mesh=" << payload.name
                << ", device=" << (device != nullptr ? "ok" : "null")
                << ", out_mesh=" << (out_mesh != nullptr ? "ok" : "null")
                << ", vertex_blob_bytes=" << payload.vertex_blob.size()
                << ", index_count=" << payload.indices.size();
        SetError(NC_ERROR_INTERNAL, "render", message.str(), true);
        return false;
    }
    if (out_normals_generated != nullptr) {
        *out_normals_generated = false;
    }
    if (out_normals_generation_failed != nullptr) {
        *out_normals_generation_failed = false;
    }
    const std::uint32_t src_stride = payload.vertex_stride >= 12U ? payload.vertex_stride : 12U;
    if ((payload.vertex_blob.size() % src_stride) != 0U) {
        std::ostringstream message;
        message << "gpu mesh stride mismatch: mesh=" << payload.name
                << ", vertex_blob_bytes=" << payload.vertex_blob.size()
                << ", src_stride=" << src_stride;
        SetError(NC_ERROR_INTERNAL, "render", message.str(), true);
        return false;
    }
    const std::uint32_t vertex_count = static_cast<std::uint32_t>(payload.vertex_blob.size() / src_stride);
    std::vector<std::uint32_t> generated_indices;
    const std::vector<std::uint32_t>* index_source = &payload.indices;
    if (payload.indices.size() < 3U) {
        std::string mesh_name_lower = payload.name;
        std::transform(mesh_name_lower.begin(), mesh_name_lower.end(), mesh_name_lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        const bool likely_non_render_proxy = mesh_name_lower.find("baked") != std::string::npos;
        if (likely_non_render_proxy) {
            std::ostringstream message;
            message << "gpu mesh skipped (insufficient indices): mesh=" << payload.name
                    << ", index_count=" << payload.indices.size();
            SetError(NC_ERROR_INTERNAL, "render", message.str(), true);
            return false;
        }
        const std::uint32_t tri_vertex_count = (vertex_count / 3U) * 3U;
        if (tri_vertex_count < 3U) {
            std::ostringstream message;
            message << "gpu mesh index fallback unavailable: mesh=" << payload.name
                    << ", vertex_count=" << vertex_count;
            SetError(NC_ERROR_INTERNAL, "render", message.str(), true);
            return false;
        }
        generated_indices.reserve(tri_vertex_count);
        for (std::uint32_t i = 0U; i < tri_vertex_count; ++i) {
            generated_indices.push_back(i);
        }
        index_source = &generated_indices;
    }
    const std::uint32_t index_count = static_cast<std::uint32_t>(index_source->size());

    std::vector<std::uint8_t> gpu_vertex_blob;
    gpu_vertex_blob.reserve(static_cast<std::size_t>(vertex_count) * 32U);
    const auto* src = payload.vertex_blob.data();
    // MIQ exporter layout is fixed: pos3(0) + normal3(12) + uv2(24) + tangent4(32).
    // Keep strict UV offset to avoid false-positive heuristic matches.
    const std::uint32_t uv_offset = (src_stride >= 32U) ? 24U : 12U;
    const bool src_has_normals = src_stride >= 24U;
    for (std::uint32_t i = 0U; i < vertex_count; ++i) {
        const std::size_t base = static_cast<std::size_t>(i) * src_stride;
        gpu_vertex_blob.insert(gpu_vertex_blob.end(), src + base, src + base + 12U);
        if (src_has_normals) {
            gpu_vertex_blob.insert(gpu_vertex_blob.end(), src + base + 12U, src + base + 24U);
        } else {
            const std::array<float, 3U> nrm_up = {0.0f, 0.0f, 0.0f};
            const auto* nrm_bytes = reinterpret_cast<const std::uint8_t*>(nrm_up.data());
            gpu_vertex_blob.insert(gpu_vertex_blob.end(), nrm_bytes, nrm_bytes + 12U);
        }
        if (src_stride >= (uv_offset + 8U)) {
            gpu_vertex_blob.insert(
                gpu_vertex_blob.end(),
                src + base + uv_offset,
                src + base + uv_offset + 8U);
        } else {
            const std::array<float, 2U> uv_zero = {0.0f, 0.0f};
            const auto* uv_bytes = reinterpret_cast<const std::uint8_t*>(uv_zero.data());
            gpu_vertex_blob.insert(gpu_vertex_blob.end(), uv_bytes, uv_bytes + 8U);
        }
    }
    if (!src_has_normals) {
        std::vector<float> normal_accum(static_cast<std::size_t>(vertex_count) * 3U, 0.0f);
        bool triangle_seen = false;
        for (std::size_t i = 0U; i + 2U < payload.indices.size(); i += 3U) {
            const std::uint32_t ia = payload.indices[i];
            const std::uint32_t ib = payload.indices[i + 1U];
            const std::uint32_t ic = payload.indices[i + 2U];
            if (ia >= vertex_count || ib >= vertex_count || ic >= vertex_count) {
                continue;
            }
            triangle_seen = true;
            auto read_pos = [&](std::uint32_t vi, float* x, float* y, float* z) {
                const std::size_t base = static_cast<std::size_t>(vi) * 32U;
                std::memcpy(x, gpu_vertex_blob.data() + base, sizeof(float));
                std::memcpy(y, gpu_vertex_blob.data() + base + 4U, sizeof(float));
                std::memcpy(z, gpu_vertex_blob.data() + base + 8U, sizeof(float));
            };
            float ax = 0.0f, ay = 0.0f, az = 0.0f;
            float bx = 0.0f, by = 0.0f, bz = 0.0f;
            float cx = 0.0f, cy = 0.0f, cz = 0.0f;
            read_pos(ia, &ax, &ay, &az);
            read_pos(ib, &bx, &by, &bz);
            read_pos(ic, &cx, &cy, &cz);
            const float ux = bx - ax;
            const float uy = by - ay;
            const float uz = bz - az;
            const float vx = cx - ax;
            const float vy = cy - ay;
            const float vz = cz - az;
            const float nx = uy * vz - uz * vy;
            const float ny = uz * vx - ux * vz;
            const float nz = ux * vy - uy * vx;
            auto accum = [&](std::uint32_t vi) {
                const std::size_t nbase = static_cast<std::size_t>(vi) * 3U;
                normal_accum[nbase] += nx;
                normal_accum[nbase + 1U] += ny;
                normal_accum[nbase + 2U] += nz;
            };
            accum(ia);
            accum(ib);
            accum(ic);
        }
        if (triangle_seen) {
            for (std::uint32_t i = 0U; i < vertex_count; ++i) {
                const std::size_t nbase = static_cast<std::size_t>(i) * 3U;
                float nx = normal_accum[nbase];
                float ny = normal_accum[nbase + 1U];
                float nz = normal_accum[nbase + 2U];
                const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len > 1e-6f) {
                    nx /= len;
                    ny /= len;
                    nz /= len;
                } else {
                    nx = 0.0f;
                    ny = 1.0f;
                    nz = 0.0f;
                }
                const std::size_t base = static_cast<std::size_t>(i) * 32U + 12U;
                std::memcpy(gpu_vertex_blob.data() + base, &nx, sizeof(float));
                std::memcpy(gpu_vertex_blob.data() + base + 4U, &ny, sizeof(float));
                std::memcpy(gpu_vertex_blob.data() + base + 8U, &nz, sizeof(float));
            }
            if (out_normals_generated != nullptr) {
                *out_normals_generated = true;
            }
        } else if (out_normals_generation_failed != nullptr) {
            *out_normals_generation_failed = true;
        }
    }

    const std::vector<std::uint8_t> bind_pose_blob = gpu_vertex_blob;
    auto compute_position_stats = [](const std::vector<std::uint8_t>& blob) {
        struct Stats {
            float max_abs = 0.0f;
            float extent_max = 0.0f;
            float extent_min = 0.0f;
            float extent_x = 0.0f;
            float extent_y = 0.0f;
            float extent_z = 0.0f;
            bool finite = true;
        };
        Stats s {};
        if (blob.empty()) {
            return s;
        }
        float bmin_x = std::numeric_limits<float>::max();
        float bmin_y = std::numeric_limits<float>::max();
        float bmin_z = std::numeric_limits<float>::max();
        float bmax_x = -std::numeric_limits<float>::max();
        float bmax_y = -std::numeric_limits<float>::max();
        float bmax_z = -std::numeric_limits<float>::max();
        const std::size_t vtx_count = blob.size() / 32U;
        for (std::size_t i = 0U; i < vtx_count; ++i) {
            const std::size_t base = i * 32U;
            float px = 0.0f;
            float py = 0.0f;
            float pz = 0.0f;
            std::memcpy(&px, blob.data() + base, sizeof(float));
            std::memcpy(&py, blob.data() + base + 4U, sizeof(float));
            std::memcpy(&pz, blob.data() + base + 8U, sizeof(float));
            if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz)) {
                s.finite = false;
                return s;
            }
            s.max_abs = std::max(s.max_abs, std::max(std::abs(px), std::max(std::abs(py), std::abs(pz))));
            bmin_x = std::min(bmin_x, px);
            bmin_y = std::min(bmin_y, py);
            bmin_z = std::min(bmin_z, pz);
            bmax_x = std::max(bmax_x, px);
            bmax_y = std::max(bmax_y, py);
            bmax_z = std::max(bmax_z, pz);
        }
        const float ex = std::max(0.0f, bmax_x - bmin_x);
        const float ey = std::max(0.0f, bmax_y - bmin_y);
        const float ez = std::max(0.0f, bmax_z - bmin_z);
        s.extent_x = ex;
        s.extent_y = ey;
        s.extent_z = ez;
        s.extent_max = std::max(ex, std::max(ey, ez));
        s.extent_min = std::min(ex, std::min(ey, ez));
        return s;
    };
    if (skin_payload != nullptr) {
        if (collapse_guard_triggered != nullptr) {
            *collapse_guard_triggered = false;
        }
        const auto pre_stats = compute_position_stats(gpu_vertex_blob);
        bool skinning_applied = false;
        const bool can_apply_with_skeleton =
            skeleton_payload != nullptr && IsValidSkeletonPosePayload(*skin_payload, *skeleton_payload);
        if (enable_static_skinning && can_apply_with_skeleton) {
            (void)ApplyStaticSkinningToVertexBlob(
                &gpu_vertex_blob,
                32U,
                *skin_payload,
                skeleton_payload,
                skinning_convention_hint,
                out_selected_convention,
                out_ambiguous_convention);
            skinning_applied = true;
        } else if (enable_static_skinning && force_static_skinning_fallback) {
            (void)ApplyStaticSkinningToVertexBlob(
                &gpu_vertex_blob,
                32U,
                *skin_payload,
                nullptr,
                skinning_convention_hint,
                out_selected_convention,
                out_ambiguous_convention);
            skinning_applied = true;
        }
        const auto post_stats = compute_position_stats(gpu_vertex_blob);
        const float pre_extent = std::max(0.0001f, pre_stats.extent_max);
        const float post_extent = post_stats.extent_max;
        const bool tiny_mesh = vertex_count < 96U || pre_extent < 0.06f;
        // Guard against plausible-but-wrong skinning matrices that stay finite
        // but inflate bounds massively (common when matrix convention mismatches).
        const bool exploded_extent = post_extent > (pre_extent * 20.0f);
        const bool exploded_abs = post_stats.max_abs > std::max(200.0f, pre_stats.max_abs * 20.0f);
        // Guard against collapsed pose output where vertices are squashed into
        // a thin tube or near-point cloud due to bad skeleton conventions.
        const bool collapsed_extent = skinning_applied && !tiny_mesh && post_extent < (pre_extent * 0.20f);
        const float pre_min_extent = std::max(0.0001f, pre_stats.extent_min);
        const float post_min_extent = std::max(0.0001f, post_stats.extent_min);
        const bool collapsed_axis = skinning_applied && !tiny_mesh && post_min_extent < (pre_min_extent * 0.15f);
        const float pre_aspect = pre_extent / pre_min_extent;
        const float post_aspect = post_extent / post_min_extent;
        const bool tube_aspect_spike = skinning_applied && !tiny_mesh && post_aspect > std::max(pre_aspect * 3.0f, 18.0f);
        const float post_axis_min = std::min(post_stats.extent_x, std::min(post_stats.extent_y, post_stats.extent_z));
        const float post_axis_max = std::max(0.0001f, post_stats.extent_max);
        const bool tube_axis_ratio = skinning_applied && !tiny_mesh && (post_axis_min / post_axis_max) < 0.06f;
        const float pre_volume =
            std::max(0.0001f, pre_stats.extent_x) *
            std::max(0.0001f, pre_stats.extent_y) *
            std::max(0.0001f, pre_stats.extent_z);
        const float post_volume =
            std::max(0.0001f, post_stats.extent_x) *
            std::max(0.0001f, post_stats.extent_y) *
            std::max(0.0001f, post_stats.extent_z);
        const float volume_ratio = post_volume / pre_volume;
        const bool collapsed_volume = skinning_applied && !tiny_mesh && volume_ratio < 0.08f;
        const bool exploded_volume = skinning_applied && !tiny_mesh && volume_ratio > 25.0f;
        // Keep MIQ meshes coherent: only reject truly catastrophic outputs.
        // Shape-collapse heuristics can desync per-mesh poses and create floating parts.
        if (!post_stats.finite || exploded_extent || exploded_abs || exploded_volume) {
            if (collapse_guard_triggered != nullptr) {
                *collapse_guard_triggered = true;
            }
            gpu_vertex_blob = bind_pose_blob;
        }
    }

    D3D11_BUFFER_DESC vb_desc {};
    vb_desc.ByteWidth = static_cast<UINT>(gpu_vertex_blob.size());
    vb_desc.Usage = D3D11_USAGE_DYNAMIC;
    vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    D3D11_SUBRESOURCE_DATA vb_data {};
    vb_data.pSysMem = gpu_vertex_blob.data();

    D3D11_BUFFER_DESC ib_desc {};
    ib_desc.ByteWidth = static_cast<UINT>(index_source->size() * sizeof(std::uint32_t));
    ib_desc.Usage = D3D11_USAGE_DEFAULT;
    ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ib_data {};
    ib_data.pSysMem = index_source->data();

    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    const HRESULT vb_hr = device->CreateBuffer(&vb_desc, &vb_data, &vb);
    if (FAILED(vb_hr) || vb == nullptr) {
        std::ostringstream message;
        message << "vertex buffer create failed: mesh=" << payload.name
                << ", vertex_count=" << vertex_count
                << ", vertex_stride=32"
                << ", vb_bytes=" << gpu_vertex_blob.size()
                << ", hr=0x" << std::hex << static_cast<std::uint32_t>(vb_hr);
        SetError(NC_ERROR_INTERNAL, "render", message.str(), true);
        return false;
    }
    const HRESULT ib_hr = device->CreateBuffer(&ib_desc, &ib_data, &ib);
    if (FAILED(ib_hr) || ib == nullptr) {
        std::ostringstream message;
        message << "index buffer create failed: mesh=" << payload.name
                << ", index_count=" << index_count
                << ", ib_bytes=" << (payload.indices.size() * sizeof(std::uint32_t))
                << ", hr=0x" << std::hex << static_cast<std::uint32_t>(ib_hr);
        SetError(NC_ERROR_INTERNAL, "render", message.str(), true);
        vb->Release();
        return false;
    }

    DirectX::XMFLOAT3 center = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 bmin = {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()};
    DirectX::XMFLOAT3 bmax = {
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max()};
    const auto* bytes = gpu_vertex_blob.data();
    for (std::uint32_t i = 0U; i < vertex_count; ++i) {
        const std::size_t base = static_cast<std::size_t>(i) * 32U;
        float px = 0.0f;
        float py = 0.0f;
        float pz = 0.0f;
        std::memcpy(&px, bytes + base, sizeof(float));
        std::memcpy(&py, bytes + base + 4U, sizeof(float));
        std::memcpy(&pz, bytes + base + 8U, sizeof(float));
        center.x += px;
        center.y += py;
        center.z += pz;
        bmin.x = std::min(bmin.x, px);
        bmin.y = std::min(bmin.y, py);
        bmin.z = std::min(bmin.z, pz);
        bmax.x = std::max(bmax.x, px);
        bmax.y = std::max(bmax.y, py);
        bmax.z = std::max(bmax.z, pz);
    }
    const float inv_count = vertex_count > 0U ? (1.0f / static_cast<float>(vertex_count)) : 1.0f;
    center.x *= inv_count;
    center.y *= inv_count;
    center.z *= inv_count;

    out_mesh->vertex_buffer = vb;
    out_mesh->index_buffer = ib;
    out_mesh->vertex_count = vertex_count;
    out_mesh->index_count = index_count;
    out_mesh->material_index = payload.material_index;
    out_mesh->center = center;
    out_mesh->vertex_stride = 32U;
    out_mesh->bounds_min = bmin;
    out_mesh->bounds_max = bmax;
    out_mesh->mesh_name = payload.name;
    out_mesh->bind_pose_vertex_blob = bind_pose_blob;
    out_mesh->base_vertex_blob = gpu_vertex_blob;
    out_mesh->deformed_vertex_blob = gpu_vertex_blob;
    return true;
}

bool EnsureAvatarGpuMeshes(RendererResources* renderer, const AvatarPackage& avatar_pkg, std::uint64_t handle, ID3D11Device* device) {
    if (renderer == nullptr || device == nullptr) {
        return false;
    }
    if (renderer->avatar_meshes.find(handle) != renderer->avatar_meshes.end()) {
        return true;
    }
    const bool static_skinning_enabled = ShouldApplyStaticSkinningForAvatarMeshes(avatar_pkg);
    if (!avatar_pkg.skin_payloads.empty() && !static_skinning_enabled) {
        auto avatar_it = g_state.avatars.find(handle);
        if (avatar_it != g_state.avatars.end()) {
            avatar_it->second.warnings.push_back(
                "W_RENDER: SKINNING_STATIC_DISABLED: skin payload detected; using original vertex positions.");
            avatar_it->second.warning_codes.push_back("SKINNING_STATIC_DISABLED");
        }
    }
    // Keep VRM on the same validated skinning path as other formats to avoid
    // mesh-space mismatch between body and attached parts on some avatars.
    const bool bypass_vrm_static_skinning = false;
    std::unordered_map<std::string, const avatar::SkinRenderPayload*> skin_by_mesh;
    skin_by_mesh.reserve(avatar_pkg.skin_payloads.size());
    for (const auto& skin : avatar_pkg.skin_payloads) {
        skin_by_mesh[NormalizeMeshKey(skin.mesh_name)] = &skin;
    }
    std::unordered_map<std::string, const avatar::SkeletonRenderPayload*> skeleton_by_mesh;
    skeleton_by_mesh.reserve(avatar_pkg.skeleton_payloads.size());
    for (const auto& skeleton : avatar_pkg.skeleton_payloads) {
        skeleton_by_mesh[NormalizeMeshKey(skeleton.mesh_name)] = &skeleton;
    }

    std::vector<GpuMeshResource> meshes;
    meshes.reserve(avatar_pkg.mesh_payloads.size());
    SkinningMatrixConvention avatar_skinning_convention = avatar_pkg.skinning_matrix_convention;
    bool avatar_skinning_convention_locked = avatar_skinning_convention != SkinningMatrixConvention::Unknown;
    const bool is_legacy_miq_without_basis =
        avatar_pkg.source_type == AvatarSourceType::Miq &&
        avatar_pkg.skinning_matrix_convention == SkinningMatrixConvention::Unknown &&
        NormalizeRefKey(avatar_pkg.skin_space_basis) != "mesh_local";
    const bool allow_miq_autodetect =
        avatar_pkg.source_type == AvatarSourceType::Miq &&
        (ShouldAutoDetectMiqSkinningConvention() || is_legacy_miq_without_basis);
    if (avatar_pkg.source_type == AvatarSourceType::Miq &&
        avatar_skinning_convention == SkinningMatrixConvention::Unknown &&
        !allow_miq_autodetect) {
        avatar_skinning_convention = SkinningMatrixConvention::DxRowMajor;
        avatar_skinning_convention_locked = true;
    }
    if (is_legacy_miq_without_basis && !ShouldAutoDetectMiqSkinningConvention()) {
        // Legacy files may omit basis metadata; prefer VRM-origin defaults.
        if (avatar_pkg.source_ext == ".vrm") {
            avatar_skinning_convention = SkinningMatrixConvention::DxRowMajor;
        } else {
            avatar_skinning_convention = SkinningMatrixConvention::GltfColumnMajor;
        }
        avatar_skinning_convention_locked = true;
    }
    bool avatar_skinning_convention_ambiguous = false;
    if (!avatar_skinning_convention_locked && static_skinning_enabled && allow_miq_autodetect) {
        struct ProbeTarget {
            const avatar::MeshRenderPayload* payload = nullptr;
            const avatar::SkinRenderPayload* skin = nullptr;
            const avatar::SkeletonRenderPayload* skeleton = nullptr;
            std::uint32_t vertex_count = 0U;
        };
        std::vector<ProbeTarget> probes;
        probes.reserve(8U);
        for (const auto& payload : avatar_pkg.mesh_payloads) {
            const auto skin_it = skin_by_mesh.find(NormalizeMeshKey(payload.name));
            if (skin_it == skin_by_mesh.end()) {
                continue;
            }
            const auto skeleton_it = skeleton_by_mesh.find(NormalizeMeshKey(payload.name));
            if (skeleton_it == skeleton_by_mesh.end()) {
                continue;
            }
            if (!IsValidSkeletonPosePayload(*skin_it->second, *skeleton_it->second)) {
                continue;
            }
            const std::uint32_t src_stride = payload.vertex_stride >= 12U ? payload.vertex_stride : 12U;
            if (payload.vertex_blob.empty() || (payload.vertex_blob.size() % src_stride) != 0U) {
                continue;
            }
            const std::uint32_t vertex_count = static_cast<std::uint32_t>(payload.vertex_blob.size() / src_stride);
            probes.push_back(ProbeTarget{&payload, skin_it->second, skeleton_it->second, vertex_count});
        }
        std::sort(probes.begin(), probes.end(), [](const ProbeTarget& a, const ProbeTarget& b) {
            return a.vertex_count > b.vertex_count;
        });
        if (probes.size() > 5U) {
            probes.resize(5U);
        }
        std::uint64_t vote_dx = 0U;
        std::uint64_t vote_gltf = 0U;
        bool any_ambiguous = false;
        for (const auto& probe : probes) {
            if (probe.payload == nullptr || probe.skin == nullptr || probe.skeleton == nullptr || probe.vertex_count == 0U) {
                continue;
            }
            std::vector<std::uint8_t> probe_gpu_vertex_blob;
            probe_gpu_vertex_blob.reserve(static_cast<std::size_t>(probe.vertex_count) * 32U);
            const std::uint32_t src_stride = probe.payload->vertex_stride >= 12U ? probe.payload->vertex_stride : 12U;
            const auto* src = probe.payload->vertex_blob.data();
            for (std::uint32_t i = 0U; i < probe.vertex_count; ++i) {
                const std::size_t base = static_cast<std::size_t>(i) * src_stride;
                probe_gpu_vertex_blob.insert(probe_gpu_vertex_blob.end(), src + base, src + base + 12U);
                if (src_stride >= 24U) {
                    probe_gpu_vertex_blob.insert(probe_gpu_vertex_blob.end(), src + base + 12U, src + base + 24U);
                } else {
                    constexpr std::array<std::uint8_t, 12U> nrm_bytes = {
                        0U, 0U, 0U, 0U, 0U, 0U, 128U, 63U, 0U, 0U, 0U, 0U};
                    probe_gpu_vertex_blob.insert(probe_gpu_vertex_blob.end(), nrm_bytes.begin(), nrm_bytes.end());
                }
                if (src_stride >= 32U) {
                    probe_gpu_vertex_blob.insert(
                        probe_gpu_vertex_blob.end(),
                        src + base + 24U,
                        src + base + 32U);
                } else {
                    constexpr std::array<std::uint8_t, 8U> uv_bytes = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
                    probe_gpu_vertex_blob.insert(probe_gpu_vertex_blob.end(), uv_bytes.begin(), uv_bytes.end());
                }
            }
            SkinningMatrixConvention selected = SkinningMatrixConvention::Unknown;
            bool ambiguous = false;
            if (ApplyStaticSkinningToVertexBlob(
                    &probe_gpu_vertex_blob,
                    32U,
                    *probe.skin,
                    probe.skeleton,
                    SkinningMatrixConvention::Unknown,
                    &selected,
                    &ambiguous) &&
                selected != SkinningMatrixConvention::Unknown) {
                if (selected == SkinningMatrixConvention::GltfColumnMajor) {
                    vote_gltf += static_cast<std::uint64_t>(probe.vertex_count);
                } else {
                    vote_dx += static_cast<std::uint64_t>(probe.vertex_count);
                }
                any_ambiguous = any_ambiguous || ambiguous;
            }
        }
        if (vote_dx > 0U || vote_gltf > 0U) {
            avatar_skinning_convention = vote_gltf > vote_dx
                ? SkinningMatrixConvention::GltfColumnMajor
                : SkinningMatrixConvention::DxRowMajor;
            avatar_skinning_convention_locked = true;
            avatar_skinning_convention_ambiguous = any_ambiguous || (vote_dx == vote_gltf);
        }
    }
    for (const auto& payload : avatar_pkg.mesh_payloads) {
        GpuMeshResource mesh {};
        const avatar::SkinRenderPayload* skin_payload = nullptr;
        const avatar::SkeletonRenderPayload* skeleton_payload = nullptr;
        bool force_static_skinning_fallback = false;
        SkinningMatrixConvention selected_skinning_convention = SkinningMatrixConvention::Unknown;
        bool ambiguous_skinning_convention = false;
        bool collapse_guard_triggered = false;
        bool normals_generated = false;
        bool normals_generation_failed = false;
        const auto skin_it = skin_by_mesh.find(NormalizeMeshKey(payload.name));
        if (static_skinning_enabled && !bypass_vrm_static_skinning && skin_it != skin_by_mesh.end()) {
            const auto check = ValidateSkinPayload(payload, *skin_it->second);
            if (check.valid) {
                skin_payload = skin_it->second;
                const auto skeleton_it = skeleton_by_mesh.find(NormalizeMeshKey(payload.name));
                if (skeleton_it != skeleton_by_mesh.end()) {
                    skeleton_payload = skeleton_it->second;
                    if (!IsValidSkeletonPosePayload(*skin_payload, *skeleton_payload)) {
                        auto avatar_it = g_state.avatars.find(handle);
                        if (avatar_it != g_state.avatars.end()) {
                            std::ostringstream warning;
                            warning << "W_RENDER: XAV3_SKINNING_MATRIX_INVALID: mesh=" << payload.name;
                            avatar_it->second.warnings.push_back(warning.str());
                            avatar_it->second.warning_codes.push_back("XAV3_SKINNING_MATRIX_INVALID");
                        }
                        skeleton_payload = nullptr;
                        force_static_skinning_fallback = true;
                    }
                } else if (!avatar_pkg.skeleton_payloads.empty()) {
                    auto avatar_it = g_state.avatars.find(handle);
                    if (avatar_it != g_state.avatars.end()) {
                        std::ostringstream warning;
                        warning << "W_RENDER: XAV3_SKELETON_PAYLOAD_MISSING: mesh=" << payload.name;
                        avatar_it->second.warnings.push_back(warning.str());
                        avatar_it->second.warning_codes.push_back("XAV3_SKELETON_PAYLOAD_MISSING");
                    }
                    force_static_skinning_fallback = true;
                }
            } else {
                auto avatar_it = g_state.avatars.find(handle);
                if (avatar_it != g_state.avatars.end()) {
                    std::ostringstream warning;
                    warning << "W_RENDER: " << check.code << ": mesh=" << payload.name << ", detail=" << check.detail;
                    avatar_it->second.warnings.push_back(warning.str());
                    avatar_it->second.warning_codes.push_back(check.code);
                }
            }
        }
        if (force_static_skinning_fallback && skin_payload != nullptr && skeleton_payload == nullptr) {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                PushAvatarWarningUnique(
                    &avatar_it->second,
                    "W_RENDER: MIQ_SKINNING_FALLBACK_SKIPPED_NO_SKELETON: preserve original vertices.",
                    "MIQ_SKINNING_FALLBACK_SKIPPED_NO_SKELETON");
            }
        }
        if (!BuildGpuMeshForPayload(
                payload,
                skin_payload,
                skeleton_payload,
                avatar_skinning_convention_locked ? avatar_skinning_convention : avatar_pkg.skinning_matrix_convention,
                static_skinning_enabled,
                force_static_skinning_fallback,
                &selected_skinning_convention,
                &ambiguous_skinning_convention,
                &collapse_guard_triggered,
                &normals_generated,
                &normals_generation_failed,
                device,
                &mesh)) {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                std::ostringstream warning;
                warning << "W_RENDER: GPU_MESH_UPLOAD_FAILED: mesh=" << payload.name << ", skip=true";
                avatar_it->second.warnings.push_back(warning.str());
                avatar_it->second.warning_codes.push_back("GPU_MESH_UPLOAD_FAILED");
            }
            ReleaseGpuMeshResource(&mesh);
            continue;
        }
        if (!avatar_skinning_convention_locked &&
            selected_skinning_convention != SkinningMatrixConvention::Unknown) {
            avatar_skinning_convention = selected_skinning_convention;
            avatar_skinning_convention_locked = true;
        }
        if (ambiguous_skinning_convention) {
            avatar_skinning_convention_ambiguous = allow_miq_autodetect;
        }
        if (static_skinning_enabled && skin_payload != nullptr && skeleton_payload != nullptr) {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                std::ostringstream warning;
                warning << "W_RENDER: SKINNING_MATRIX_CONVENTION_SELECTED: mesh=" << payload.name
                        << ", selected=" << SkinningMatrixConventionName(selected_skinning_convention)
                        << ", hint=" << SkinningMatrixConventionName(avatar_skinning_convention_locked
                                                                      ? avatar_skinning_convention
                                                                      : avatar_pkg.skinning_matrix_convention);
                PushAvatarWarningUnique(
                    &avatar_it->second,
                    warning.str(),
                    "SKINNING_MATRIX_CONVENTION_SELECTED");
            }
        }
        if (collapse_guard_triggered) {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                std::ostringstream warning;
                warning << "W_RENDER: MIQ_SKINNING_COLLAPSE_GUARD: mesh=" << payload.name
                        << ", posed mesh rejected; keep bind pose.";
                PushAvatarWarningUnique(
                    &avatar_it->second,
                    warning.str(),
                    "MIQ_SKINNING_COLLAPSE_GUARD");
            }
        }
        if (normals_generated) {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                std::ostringstream warning;
                warning << "W_RENDER: MIQ_NORMALS_GENERATED: mesh=" << payload.name;
                PushAvatarWarningUnique(
                    &avatar_it->second,
                    warning.str(),
                    "MIQ_NORMALS_GENERATED");
            }
        } else if (normals_generation_failed) {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                std::ostringstream warning;
                warning << "W_RENDER: MIQ_NORMALS_GENERATION_FAILED: mesh=" << payload.name;
                PushAvatarWarningUnique(
                    &avatar_it->second,
                    warning.str(),
                    "MIQ_NORMALS_GENERATION_FAILED");
            }
        }
        meshes.push_back(mesh);
    }
    if (meshes.empty()) {
        auto avatar_it = g_state.avatars.find(handle);
        if (avatar_it != g_state.avatars.end()) {
            PushAvatarWarningUnique(
                &avatar_it->second,
                "W_RENDER: GPU_MESH_UPLOAD_ALL_FAILED: no mesh could be uploaded for this avatar.",
                "GPU_MESH_UPLOAD_ALL_FAILED");
        }
        return false;
    }
    if (allow_miq_autodetect && avatar_skinning_convention_ambiguous) {
        auto avatar_it = g_state.avatars.find(handle);
        if (avatar_it != g_state.avatars.end()) {
            std::ostringstream warning;
            warning << "W_RENDER: MIQ_SKINNING_CONVENTION_AMBIGUOUS: selected="
                    << SkinningMatrixConventionName(avatar_skinning_convention)
                    << ", scope=avatar_locked";
            PushAvatarWarningUnique(
                &avatar_it->second,
                warning.str(),
                "MIQ_SKINNING_CONVENTION_AMBIGUOUS");
        }
    }
    renderer->avatar_meshes[handle] = std::move(meshes);
    return true;
}

bool ApplyArmPoseToAvatar(
    RendererResources* renderer,
    const AvatarPackage& avatar_pkg,
    std::uint64_t handle,
    ID3D11DeviceContext* device_ctx) {
    if (renderer == nullptr || device_ctx == nullptr) {
        return false;
    }
    const bool is_vrm_origin_miq =
        avatar_pkg.source_type == AvatarSourceType::Miq &&
        avatar_pkg.source_ext == ".vrm";
    if (is_vrm_origin_miq &&
        g_state.arm_pose_auto_rollback_handles.find(handle) != g_state.arm_pose_auto_rollback_handles.end()) {
        auto avatar_it = g_state.avatars.find(handle);
        if (avatar_it != g_state.avatars.end()) {
            PushAvatarWarningExclusive(
                &avatar_it->second,
                "W_RENDER: ARM_POSE_AUTO_ROLLBACK_VRM_ORIGIN: arm pose auto-disabled due to vrm-origin rollback guard.",
                "ARM_POSE_AUTO_ROLLBACK_VRM_ORIGIN",
                {"ARM_POSE_AUTO_ROLLBACK_VRM_ORIGIN", "ARM_POSE_FORMAT_UNSUPPORTED", "ARM_POSE_PAYLOAD_MISSING", "ARM_POSE_DISABLED_BY_STATIC_SKINNING_POLICY"});
        }
        return true;
    }
    const bool arm_pose_enabled = ShouldApplyArmPoseForAvatar(avatar_pkg);
    if (!arm_pose_enabled) {
        auto avatar_it = g_state.avatars.find(handle);
        if (avatar_it != g_state.avatars.end()) {
            const bool payload_complete =
                !avatar_pkg.skin_payloads.empty() &&
                !avatar_pkg.skeleton_payloads.empty() &&
                !avatar_pkg.skeleton_rig_payloads.empty();
            if (avatar_pkg.source_type != AvatarSourceType::Miq) {
                PushAvatarWarningExclusive(
                    &avatar_it->second,
                    "W_RENDER: ARM_POSE_FORMAT_UNSUPPORTED: arm pose is supported for MIQ payloads.",
                    "ARM_POSE_FORMAT_UNSUPPORTED",
                    {"ARM_POSE_AUTO_ROLLBACK_VRM_ORIGIN", "ARM_POSE_FORMAT_UNSUPPORTED", "ARM_POSE_PAYLOAD_MISSING", "ARM_POSE_DISABLED_BY_STATIC_SKINNING_POLICY"});
            } else if (!payload_complete) {
                PushAvatarWarningExclusive(
                    &avatar_it->second,
                    "W_RENDER: ARM_POSE_PAYLOAD_MISSING: arm pose skipped due to missing skin/skeleton/rig payload.",
                    "ARM_POSE_PAYLOAD_MISSING",
                    {"ARM_POSE_AUTO_ROLLBACK_VRM_ORIGIN", "ARM_POSE_FORMAT_UNSUPPORTED", "ARM_POSE_PAYLOAD_MISSING", "ARM_POSE_DISABLED_BY_STATIC_SKINNING_POLICY"});
            } else {
                PushAvatarWarningExclusive(
                    &avatar_it->second,
                    "W_RENDER: ARM_POSE_DISABLED_BY_STATIC_SKINNING_POLICY: arm pose skipped due to static skinning policy.",
                    "ARM_POSE_DISABLED_BY_STATIC_SKINNING_POLICY",
                    {"ARM_POSE_AUTO_ROLLBACK_VRM_ORIGIN", "ARM_POSE_FORMAT_UNSUPPORTED", "ARM_POSE_PAYLOAD_MISSING", "ARM_POSE_DISABLED_BY_STATIC_SKINNING_POLICY"});
            }
        }
        return true;
    }
    auto mesh_it = renderer->avatar_meshes.find(handle);
    if (mesh_it == renderer->avatar_meshes.end() || mesh_it->second.empty()) {
        return true;
    }
    if (avatar_pkg.skin_payloads.empty() || avatar_pkg.skeleton_payloads.empty() || avatar_pkg.skeleton_rig_payloads.empty()) {
        auto avatar_it = g_state.avatars.find(handle);
        if (avatar_it != g_state.avatars.end()) {
            PushAvatarWarningExclusive(
                &avatar_it->second,
                "W_RENDER: ARM_POSE_PAYLOAD_MISSING: arm pose skipped due to missing skin/skeleton/rig payload.",
                "ARM_POSE_PAYLOAD_MISSING",
                {"ARM_POSE_AUTO_ROLLBACK_VRM_ORIGIN", "ARM_POSE_FORMAT_UNSUPPORTED", "ARM_POSE_PAYLOAD_MISSING", "ARM_POSE_DISABLED_BY_STATIC_SKINNING_POLICY"});
        }
        return true;
    }
    SkinningMatrixConvention arm_pose_convention_hint = avatar_pkg.skinning_matrix_convention;
    const bool legacy_miq_unknown_basis =
        avatar_pkg.source_type == AvatarSourceType::Miq &&
        arm_pose_convention_hint == SkinningMatrixConvention::Unknown &&
        NormalizeRefKey(avatar_pkg.skin_space_basis) != "mesh_local";
    if (legacy_miq_unknown_basis && !ShouldAutoDetectMiqSkinningConvention()) {
        arm_pose_convention_hint =
            avatar_pkg.source_ext == ".vrm"
                ? SkinningMatrixConvention::DxRowMajor
                : SkinningMatrixConvention::GltfColumnMajor;
    }
    if (avatar_pkg.source_type == AvatarSourceType::Miq &&
        arm_pose_convention_hint == SkinningMatrixConvention::Unknown &&
        !ShouldAutoDetectMiqSkinningConvention()) {
        arm_pose_convention_hint = SkinningMatrixConvention::DxRowMajor;
    }

    std::unordered_map<std::string, const avatar::SkinRenderPayload*> skin_by_mesh;
    skin_by_mesh.reserve(avatar_pkg.skin_payloads.size());
    for (const auto& skin : avatar_pkg.skin_payloads) {
        skin_by_mesh[NormalizeMeshKey(skin.mesh_name)] = &skin;
    }
    std::unordered_map<std::string, const avatar::SkeletonRenderPayload*> skeleton_by_mesh;
    skeleton_by_mesh.reserve(avatar_pkg.skeleton_payloads.size());
    for (const auto& skeleton : avatar_pkg.skeleton_payloads) {
        skeleton_by_mesh[NormalizeMeshKey(skeleton.mesh_name)] = &skeleton;
    }
    std::unordered_map<std::string, const avatar::SkeletonRigPayload*> rig_by_mesh;
    rig_by_mesh.reserve(avatar_pkg.skeleton_rig_payloads.size());
    for (const auto& rig : avatar_pkg.skeleton_rig_payloads) {
        rig_by_mesh[NormalizeMeshKey(rig.mesh_name)] = &rig;
    }

    const auto left_upper_arm_pose = GetPoseOffset(static_cast<std::uint32_t>(NC_POSE_BONE_LEFT_UPPER_ARM));
    const auto right_upper_arm_pose = GetPoseOffset(static_cast<std::uint32_t>(NC_POSE_BONE_RIGHT_UPPER_ARM));
    const NcPoseBoneOffset left_shoulder_pose {};
    const NcPoseBoneOffset right_shoulder_pose {};
    const NcPoseBoneOffset left_lower_arm_pose {};
    const NcPoseBoneOffset right_lower_arm_pose {};
    const NcPoseBoneOffset left_hand_pose {};
    const NcPoseBoneOffset right_hand_pose {};
    auto& pose_state = g_state.arm_pose_states[handle];
    auto abs_max_delta = [](const NcPoseBoneOffset& a, const NcPoseBoneOffset& b) {
        return std::max(
            std::abs(a.pitch_deg - b.pitch_deg),
            std::max(std::abs(a.yaw_deg - b.yaw_deg), std::abs(a.roll_deg - b.roll_deg)));
    };
    if (pose_state.initialized &&
        abs_max_delta(pose_state.left_upper_arm, left_upper_arm_pose) < 0.2f &&
        abs_max_delta(pose_state.right_upper_arm, right_upper_arm_pose) < 0.2f &&
        abs_max_delta(pose_state.left_shoulder, left_shoulder_pose) < 0.2f &&
        abs_max_delta(pose_state.right_shoulder, right_shoulder_pose) < 0.2f &&
        abs_max_delta(pose_state.left_lower_arm, left_lower_arm_pose) < 0.2f &&
        abs_max_delta(pose_state.right_lower_arm, right_lower_arm_pose) < 0.2f &&
        abs_max_delta(pose_state.left_hand, left_hand_pose) < 0.2f &&
        abs_max_delta(pose_state.right_hand, right_hand_pose) < 0.2f) {
        return true;
    }
    bool any_mesh_updated = false;
    bool rollback_guard_triggered = false;
    std::string rollback_guard_reason;
    if (is_vrm_origin_miq) {
        const auto metrics_it = g_state.avatar_preview_orientation_metrics.find(handle);
        if (metrics_it != g_state.avatar_preview_orientation_metrics.end()) {
            const float hair_align_score = metrics_it->second.preview_hair_head_alignment_score;
            if (std::isfinite(hair_align_score) && hair_align_score >= 0.0f && hair_align_score < 0.35f) {
                rollback_guard_triggered = true;
                rollback_guard_reason = "hair_head_alignment_score_low";
            }
        }
    }
    auto& meshes = mesh_it->second;
    auto compute_position_stats = [](const std::vector<std::uint8_t>& blob, std::uint32_t stride) {
        struct Stats {
            float max_abs = 0.0f;
            float extent_max = 0.0f;
            float extent_min = 0.0f;
            float extent_x = 0.0f;
            float extent_y = 0.0f;
            float extent_z = 0.0f;
            bool finite = true;
        };
        Stats s {};
        if (blob.empty() || stride < 12U || (blob.size() % stride) != 0U) {
            return s;
        }
        float bmin_x = std::numeric_limits<float>::max();
        float bmin_y = std::numeric_limits<float>::max();
        float bmin_z = std::numeric_limits<float>::max();
        float bmax_x = -std::numeric_limits<float>::max();
        float bmax_y = -std::numeric_limits<float>::max();
        float bmax_z = -std::numeric_limits<float>::max();
        const std::size_t vtx_count = blob.size() / stride;
        for (std::size_t i = 0U; i < vtx_count; ++i) {
            const std::size_t base = i * stride;
            float px = 0.0f;
            float py = 0.0f;
            float pz = 0.0f;
            std::memcpy(&px, blob.data() + base, sizeof(float));
            std::memcpy(&py, blob.data() + base + 4U, sizeof(float));
            std::memcpy(&pz, blob.data() + base + 8U, sizeof(float));
            if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz)) {
                s.finite = false;
                return s;
            }
            s.max_abs = std::max(s.max_abs, std::max(std::abs(px), std::max(std::abs(py), std::abs(pz))));
            bmin_x = std::min(bmin_x, px);
            bmin_y = std::min(bmin_y, py);
            bmin_z = std::min(bmin_z, pz);
            bmax_x = std::max(bmax_x, px);
            bmax_y = std::max(bmax_y, py);
            bmax_z = std::max(bmax_z, pz);
        }
        const float ex = std::max(0.0f, bmax_x - bmin_x);
        const float ey = std::max(0.0f, bmax_y - bmin_y);
        const float ez = std::max(0.0f, bmax_z - bmin_z);
        s.extent_x = ex;
        s.extent_y = ey;
        s.extent_z = ez;
        s.extent_max = std::max(ex, std::max(ey, ez));
        s.extent_min = std::min(ex, std::min(ey, ez));
        return s;
    };
    for (auto& mesh : meshes) {
        if (mesh.bind_pose_vertex_blob.empty()) {
            continue;
        }
        const auto key = NormalizeMeshKey(mesh.mesh_name);
        const auto skin_it = skin_by_mesh.find(key);
        const auto skeleton_it = skeleton_by_mesh.find(key);
        const auto rig_it = rig_by_mesh.find(key);
        if (skin_it == skin_by_mesh.end() || skeleton_it == skeleton_by_mesh.end() || rig_it == rig_by_mesh.end()) {
            continue;
        }
        const auto* skin_payload = skin_it->second;
        const auto* skeleton_payload = skeleton_it->second;
        const auto* rig_payload = rig_it->second;
        if (skin_payload == nullptr || skeleton_payload == nullptr || rig_payload == nullptr) {
            continue;
        }
        if (!IsValidSkeletonPosePayload(*skin_payload, *skeleton_payload)) {
            continue;
        }

        std::vector<float> posed_bone_matrices = skeleton_payload->bone_matrices_16xn;
        auto apply_humanoid_pose = [&](avatar::HumanoidBoneId humanoid_id, const NcPoseBoneOffset& pose) {
            std::size_t bone_index = std::numeric_limits<std::size_t>::max();
            for (std::size_t i = 0U; i < rig_payload->bones.size(); ++i) {
                if (rig_payload->bones[i].humanoid_id == humanoid_id) {
                    bone_index = i;
                    break;
                }
            }
            if (bone_index == std::numeric_limits<std::size_t>::max()) {
                return;
            }
            const std::size_t base = bone_index * 16U;
            if (base + 16U > posed_bone_matrices.size()) {
                return;
            }
            DirectX::XMFLOAT4X4 bone_m {};
            for (std::size_t j = 0U; j < 16U; ++j) {
                reinterpret_cast<float*>(&bone_m)[j] = posed_bone_matrices[base + j];
            }
            const auto bone = DirectX::XMLoadFloat4x4(&bone_m);
            const auto arm_rot = DirectX::XMMatrixRotationRollPitchYaw(
                DirectX::XMConvertToRadians(pose.pitch_deg),
                DirectX::XMConvertToRadians(pose.yaw_deg),
                DirectX::XMConvertToRadians(pose.roll_deg));
            const auto posed = DirectX::XMMatrixMultiply(bone, arm_rot);
            DirectX::XMStoreFloat4x4(&bone_m, posed);
            for (std::size_t j = 0U; j < 16U; ++j) {
                posed_bone_matrices[base + j] = reinterpret_cast<float*>(&bone_m)[j];
            }
        };
        apply_humanoid_pose(avatar::HumanoidBoneId::LeftUpperArm, left_upper_arm_pose);
        apply_humanoid_pose(avatar::HumanoidBoneId::RightUpperArm, right_upper_arm_pose);

        avatar::SkeletonRenderPayload posed_skeleton;
        posed_skeleton.mesh_name = skeleton_payload->mesh_name;
        posed_skeleton.bone_matrices_16xn = std::move(posed_bone_matrices);

        const auto pre_stats = compute_position_stats(mesh.bind_pose_vertex_blob, mesh.vertex_stride);
        auto posed_vertices = mesh.bind_pose_vertex_blob;
        if (!ApplyStaticSkinningToVertexBlob(
                &posed_vertices,
                mesh.vertex_stride,
                *skin_payload,
                &posed_skeleton,
                arm_pose_convention_hint,
                nullptr,
                nullptr)) {
            continue;
        }
        const auto post_stats = compute_position_stats(posed_vertices, mesh.vertex_stride);
        const float pre_extent = std::max(0.0001f, pre_stats.extent_max);
        const bool exploded_extent = post_stats.extent_max > (pre_extent * 20.0f);
        const bool exploded_abs = post_stats.max_abs > std::max(200.0f, pre_stats.max_abs * 20.0f);
        const bool collapsed_extent = post_stats.extent_max < (pre_extent * 0.20f);
        const float pre_min_extent = std::max(0.0001f, pre_stats.extent_min);
        const float post_min_extent = std::max(0.0001f, post_stats.extent_min);
        const bool collapsed_axis = post_min_extent < (pre_min_extent * 0.15f);
        const float pre_aspect = pre_extent / pre_min_extent;
        const float post_aspect = std::max(0.0001f, post_stats.extent_max) / post_min_extent;
        const bool tube_aspect_spike = post_aspect > std::max(pre_aspect * 3.0f, 18.0f);
        const float post_axis_min = std::min(post_stats.extent_x, std::min(post_stats.extent_y, post_stats.extent_z));
        const float post_axis_max = std::max(0.0001f, post_stats.extent_max);
        const bool tube_axis_ratio = (post_axis_min / post_axis_max) < 0.06f;
        const float pre_volume =
            std::max(0.0001f, pre_stats.extent_x) *
            std::max(0.0001f, pre_stats.extent_y) *
            std::max(0.0001f, pre_stats.extent_z);
        const float post_volume =
            std::max(0.0001f, post_stats.extent_x) *
            std::max(0.0001f, post_stats.extent_y) *
            std::max(0.0001f, post_stats.extent_z);
        const float volume_ratio = post_volume / pre_volume;
        const bool collapsed_volume = volume_ratio < 0.08f;
        const bool exploded_volume = volume_ratio > 25.0f;
        if (!post_stats.finite || exploded_extent || exploded_abs || exploded_volume) {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                PushAvatarWarningUnique(
                    &avatar_it->second,
                    "W_RENDER: MIQ_SKINNING_EXTENT_GUARD: posed mesh rejected; keep bind pose.",
                    "MIQ_SKINNING_EXTENT_GUARD");
            }
            if (is_vrm_origin_miq) {
                rollback_guard_triggered = true;
                rollback_guard_reason = "miq_skinning_extent_guard";
            }
            posed_vertices = mesh.bind_pose_vertex_blob;
        }
        mesh.base_vertex_blob = std::move(posed_vertices);
        mesh.deformed_vertex_blob = mesh.base_vertex_blob;
        RecomputeMeshBoundsFromVertexBlob(&mesh);
        (void)UploadMeshVertexBlob(&mesh, device_ctx);
        any_mesh_updated = true;
    }
    if (is_vrm_origin_miq && rollback_guard_triggered) {
        g_state.arm_pose_auto_rollback_handles.insert(handle);
        ResetAvatarMeshesToBindPose(renderer, handle, device_ctx);
        auto avatar_it = g_state.avatars.find(handle);
        if (avatar_it != g_state.avatars.end()) {
            const std::string reason = rollback_guard_reason.empty()
                ? "unknown"
                : rollback_guard_reason;
            PushAvatarWarningExclusive(
                &avatar_it->second,
                std::string("W_RENDER: ARM_POSE_AUTO_ROLLBACK_VRM_ORIGIN: arm pose auto-disabled due to guard reason=") + reason,
                "ARM_POSE_AUTO_ROLLBACK_VRM_ORIGIN",
                {"ARM_POSE_AUTO_ROLLBACK_VRM_ORIGIN", "ARM_POSE_FORMAT_UNSUPPORTED", "ARM_POSE_PAYLOAD_MISSING", "ARM_POSE_DISABLED_BY_STATIC_SKINNING_POLICY"});
        }
        return true;
    }
    if (any_mesh_updated) {
        pose_state.initialized = true;
        pose_state.left_upper_arm = left_upper_arm_pose;
        pose_state.right_upper_arm = right_upper_arm_pose;
        pose_state.left_shoulder = left_shoulder_pose;
        pose_state.right_shoulder = right_shoulder_pose;
        pose_state.left_lower_arm = left_lower_arm_pose;
        pose_state.right_lower_arm = right_lower_arm_pose;
        pose_state.left_hand = left_hand_pose;
        pose_state.right_hand = right_hand_pose;
    }
    return true;
}

bool ApplyExpressionMorphToAvatar(
    RendererResources* renderer,
    const AvatarPackage& avatar_pkg,
    std::uint64_t handle,
    ID3D11DeviceContext* device_ctx) {
    if (renderer == nullptr || device_ctx == nullptr) {
        return false;
    }
    auto mesh_it = renderer->avatar_meshes.find(handle);
    if (mesh_it == renderer->avatar_meshes.end()) {
        return true;
    }
    if (avatar_pkg.expressions.empty() || avatar_pkg.blendshape_payloads.empty()) {
        return true;
    }
    auto& gpu_meshes = mesh_it->second;
    if (gpu_meshes.empty()) {
        return true;
    }

    std::unordered_map<std::string, std::size_t> gpu_mesh_index_by_name;
    for (std::size_t i = 0U; i < gpu_meshes.size(); ++i) {
        gpu_mesh_index_by_name[gpu_meshes[i].mesh_name] = i;
    }
    std::unordered_map<std::string, const avatar::BlendShapeRenderPayload*> blendshape_by_mesh;
    for (const auto& bs : avatar_pkg.blendshape_payloads) {
        blendshape_by_mesh[bs.mesh_name] = &bs;
    }

    std::vector<std::vector<float>> accum_deltas(gpu_meshes.size());
    std::vector<bool> has_delta(gpu_meshes.size(), false);
    for (const auto& expr : avatar_pkg.expressions) {
        const float expr_weight = std::max(0.0f, std::min(1.0f, expr.runtime_weight));
        if (expr_weight <= 0.0001f || expr.binds.empty()) {
            continue;
        }
        for (const auto& bind : expr.binds) {
            const auto mesh_idx_it = gpu_mesh_index_by_name.find(bind.mesh_name);
            if (mesh_idx_it == gpu_mesh_index_by_name.end()) {
                continue;
            }
            const auto bs_it = blendshape_by_mesh.find(bind.mesh_name);
            if (bs_it == blendshape_by_mesh.end() || bs_it->second == nullptr) {
                continue;
            }
            const auto mesh_index = mesh_idx_it->second;
            const auto& gpu_mesh = gpu_meshes[mesh_index];
            const auto* frame_ptr = static_cast<const avatar::BlendShapeFramePayload*>(nullptr);
            for (const auto& frame : bs_it->second->frames) {
                if (frame.name == bind.frame_name) {
                    frame_ptr = &frame;
                    break;
                }
            }
            if (frame_ptr == nullptr) {
                continue;
            }
            const auto& frame = *frame_ptr;
            if (frame.delta_vertices.size() < static_cast<std::size_t>(gpu_mesh.vertex_count) * 12U) {
                continue;
            }
            if (accum_deltas[mesh_index].empty()) {
                accum_deltas[mesh_index].assign(static_cast<std::size_t>(gpu_mesh.vertex_count) * 3U, 0.0f);
            }
            auto& accum = accum_deltas[mesh_index];
            const float scale = expr_weight * std::max(0.0f, bind.weight_scale);
            const auto* delta_bytes = frame.delta_vertices.data();
            for (std::uint32_t vi = 0U; vi < gpu_mesh.vertex_count; ++vi) {
                const std::size_t dbase = static_cast<std::size_t>(vi) * 12U;
                float dx = 0.0f;
                float dy = 0.0f;
                float dz = 0.0f;
                std::memcpy(&dx, delta_bytes + dbase, sizeof(float));
                std::memcpy(&dy, delta_bytes + dbase + 4U, sizeof(float));
                std::memcpy(&dz, delta_bytes + dbase + 8U, sizeof(float));
                const std::size_t a = static_cast<std::size_t>(vi) * 3U;
                accum[a] += dx * scale;
                accum[a + 1U] += dy * scale;
                accum[a + 2U] += dz * scale;
            }
            has_delta[mesh_index] = true;
        }
    }

    for (std::size_t mesh_index = 0U; mesh_index < gpu_meshes.size(); ++mesh_index) {
        auto& gpu_mesh = gpu_meshes[mesh_index];
        if (gpu_mesh.base_vertex_blob.empty() || gpu_mesh.vertex_buffer == nullptr) {
            continue;
        }
        gpu_mesh.deformed_vertex_blob = gpu_mesh.base_vertex_blob;
        if (has_delta[mesh_index] && !accum_deltas[mesh_index].empty()) {
            auto& deformed = gpu_mesh.deformed_vertex_blob;
            const auto& accum = accum_deltas[mesh_index];
            for (std::uint32_t vi = 0U; vi < gpu_mesh.vertex_count; ++vi) {
                const std::size_t vbase = static_cast<std::size_t>(vi) * gpu_mesh.vertex_stride;
                const std::size_t a = static_cast<std::size_t>(vi) * 3U;
                float px = 0.0f;
                float py = 0.0f;
                float pz = 0.0f;
                std::memcpy(&px, deformed.data() + vbase, sizeof(float));
                std::memcpy(&py, deformed.data() + vbase + 4U, sizeof(float));
                std::memcpy(&pz, deformed.data() + vbase + 8U, sizeof(float));
                px += accum[a];
                py += accum[a + 1U];
                pz += accum[a + 2U];
                std::memcpy(deformed.data() + vbase, &px, sizeof(float));
                std::memcpy(deformed.data() + vbase + 4U, &py, sizeof(float));
                std::memcpy(deformed.data() + vbase + 8U, &pz, sizeof(float));
            }
        }

        D3D11_MAPPED_SUBRESOURCE mapped {};
        if (SUCCEEDED(device_ctx->Map(gpu_mesh.vertex_buffer, 0U, D3D11_MAP_WRITE_DISCARD, 0U, &mapped))) {
            std::memcpy(mapped.pData, gpu_mesh.deformed_vertex_blob.data(), gpu_mesh.deformed_vertex_blob.size());
            device_ctx->Unmap(gpu_mesh.vertex_buffer, 0U);
        }
    }
    return true;
}

void ApplyTrackingDrivenExpressionWeights(AvatarPackage* avatar_pkg) {
    if (avatar_pkg == nullptr || avatar_pkg->expressions.empty()) {
        return;
    }
    const float blink_avg = std::max(
        0.0f,
        std::min(1.0f, (g_state.latest_tracking.blink_l + g_state.latest_tracking.blink_r) * 0.5f));
    const float mouth_open = std::max(0.0f, std::min(1.0f, g_state.latest_tracking.mouth_open));
    std::string summary;
    std::size_t shown = 0U;
    for (auto& expr : avatar_pkg->expressions) {
        float weight = std::max(0.0f, std::min(1.0f, expr.default_weight));
        if (expr.mapping_kind == "blink") {
            weight = blink_avg;
        } else if (expr.mapping_kind == "viseme_aa") {
            weight = mouth_open;
        } else if (expr.mapping_kind == "joy") {
            weight = std::max(0.0f, std::min(1.0f, mouth_open * 0.7f));
        }
        expr.runtime_weight = weight;
        if (shown < 3U) {
            if (!summary.empty()) {
                summary += ", ";
            }
            summary += expr.name + "=" + std::to_string(weight);
            ++shown;
        }
    }
    avatar_pkg->last_expression_summary = summary;
}

bool DecodeImageWithWic(
    const std::vector<std::uint8_t>& encoded,
    std::vector<std::uint8_t>* out_rgba,
    std::uint32_t* out_width,
    std::uint32_t* out_height) {
    if (encoded.empty() || out_rgba == nullptr || out_width == nullptr || out_height == nullptr) {
        return false;
    }
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_initialized = SUCCEEDED(hr);

    IWICImagingFactory* factory = nullptr;
    hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr) || factory == nullptr) {
        if (com_initialized) {
            CoUninitialize();
        }
        return false;
    }
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    bool ok = false;
    do {
        hr = factory->CreateStream(&stream);
        if (FAILED(hr) || stream == nullptr) {
            break;
        }
        hr = stream->InitializeFromMemory(const_cast<BYTE*>(encoded.data()), static_cast<DWORD>(encoded.size()));
        if (FAILED(hr)) {
            break;
        }
        hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
        if (FAILED(hr) || decoder == nullptr) {
            break;
        }
        hr = decoder->GetFrame(0U, &frame);
        if (FAILED(hr) || frame == nullptr) {
            break;
        }
        hr = frame->GetSize(reinterpret_cast<UINT*>(out_width), reinterpret_cast<UINT*>(out_height));
        if (FAILED(hr) || *out_width == 0U || *out_height == 0U) {
            break;
        }
        hr = factory->CreateFormatConverter(&converter);
        if (FAILED(hr) || converter == nullptr) {
            break;
        }
        hr = converter->Initialize(
            frame,
            GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) {
            break;
        }
        out_rgba->assign(static_cast<std::size_t>(*out_width) * static_cast<std::size_t>(*out_height) * 4U, 0U);
        hr = converter->CopyPixels(
            nullptr,
            (*out_width) * 4U,
            static_cast<UINT>(out_rgba->size()),
            out_rgba->data());
        if (FAILED(hr)) {
            out_rgba->clear();
            break;
        }
        ok = true;
    } while (false);

    if (converter != nullptr) {
        converter->Release();
    }
    if (frame != nullptr) {
        frame->Release();
    }
    if (decoder != nullptr) {
        decoder->Release();
    }
    if (stream != nullptr) {
        stream->Release();
    }
    if (factory != nullptr) {
        factory->Release();
    }
    if (com_initialized) {
        CoUninitialize();
    }
    return ok;
}

ID3D11ShaderResourceView* CreateTextureSrvFromPayload(ID3D11Device* device, const avatar::TextureRenderPayload* payload) {
    if (device == nullptr || payload == nullptr || payload->bytes.empty()) {
        return nullptr;
    }
    std::vector<std::uint8_t> bgra;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    if (!DecodeImageWithWic(payload->bytes, &bgra, &width, &height)) {
        return nullptr;
    }
    D3D11_TEXTURE2D_DESC tex_desc {};
    tex_desc.Width = width;
    tex_desc.Height = height;
    tex_desc.MipLevels = 1U;
    tex_desc.ArraySize = 1U;
    tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    tex_desc.SampleDesc.Count = 1U;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA tex_data {};
    tex_data.pSysMem = bgra.data();
    tex_data.SysMemPitch = width * 4U;
    ID3D11Texture2D* texture = nullptr;
    if (FAILED(device->CreateTexture2D(&tex_desc, &tex_data, &texture)) || texture == nullptr) {
        return nullptr;
    }
    ID3D11ShaderResourceView* srv = nullptr;
    const HRESULT hr = device->CreateShaderResourceView(texture, nullptr, &srv);
    texture->Release();
    if (FAILED(hr)) {
        return nullptr;
    }
    return srv;
}

std::string ToUpperAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

std::string CanonicalizeAlphaMode(std::string alpha_mode) {
    alpha_mode = ToUpperAscii(alpha_mode);
    if (alpha_mode == "MASK" || alpha_mode == "BLEND") {
        return alpha_mode;
    }
    return "OPAQUE";
}

std::string NormalizeShaderFamilyKey(const std::string& shader_family) {
    const std::string key = NormalizeRefKey(shader_family);
    if (key.empty()) {
        return "legacy";
    }
    if (key.find("mtoon") != std::string::npos) {
        return "mtoon";
    }
    if (key.find("liltoon") != std::string::npos) {
        return "liltoon";
    }
    if (key.find("poiyomi") != std::string::npos) {
        return "poiyomi";
    }
    if (key.find("potatoon") != std::string::npos) {
        return "potatoon";
    }
    if (key.find("realtoon") != std::string::npos) {
        return "realtoon";
    }
    return key;
}

bool IsSupportedShaderFamilyKey(const std::string& shader_family) {
    const std::string key = NormalizeShaderFamilyKey(shader_family);
    return key == "legacy" ||
           key == "standard" ||
           key == "mtoon" ||
           key == "liltoon" ||
           key == "poiyomi" ||
           key == "potatoon" ||
           key == "realtoon";
}

bool IsTypedEncoding(std::string encoding) {
    encoding = NormalizeRefKey(std::move(encoding));
    return encoding.rfind("typed-v", 0U) == 0U;
}

const char* RenderQualityModeName(std::uint32_t quality_profile) {
    if (quality_profile == NC_RENDER_QUALITY_ULTRA_PARITY) {
        return "ultra-parity";
    }
    if (quality_profile == NC_RENDER_QUALITY_FAST_FALLBACK) {
        return "fast-fallback";
    }
    if (quality_profile == NC_RENDER_QUALITY_BALANCED) {
        return "balanced";
    }
    return "default";
}

bool ParseFloatToken(const std::string& token, float* out_value) {
    if (out_value == nullptr) {
        return false;
    }
    char* end_ptr = nullptr;
    const float parsed = std::strtof(token.c_str(), &end_ptr);
    if (end_ptr == token.c_str()) {
        return false;
    }
    *out_value = parsed;
    return true;
}

bool TryExtractShaderParamFloat(const std::string& params_json, const std::string& key, float* out_value) {
    if (out_value == nullptr) {
        return false;
    }
    const std::string token = key + "=";
    const std::size_t pos = params_json.find(token);
    if (pos == std::string::npos) {
        return false;
    }
    std::size_t value_begin = pos + token.size();
    std::size_t value_end = value_begin;
    while (value_end < params_json.size()) {
        const char c = params_json[value_end];
        if (c == ',' || c == '"' || c == ']' || c == '}') {
            break;
        }
        ++value_end;
    }
    if (value_end <= value_begin) {
        return false;
    }
    return ParseFloatToken(params_json.substr(value_begin, value_end - value_begin), out_value);
}

bool TryExtractShaderParamColor(const std::string& params_json, const std::string& key, std::array<float, 4U>* out_color) {
    if (out_color == nullptr) {
        return false;
    }
    const std::string token = key + "=(";
    const std::size_t pos = params_json.find(token);
    if (pos == std::string::npos) {
        return false;
    }
    const std::size_t begin = pos + token.size();
    const std::size_t end = params_json.find(')', begin);
    if (end == std::string::npos || end <= begin) {
        return false;
    }
    std::array<float, 4U> parsed = {1.0f, 1.0f, 1.0f, 1.0f};
    std::size_t cursor = begin;
    for (std::size_t i = 0U; i < 4U; ++i) {
        std::size_t comma = params_json.find(',', cursor);
        const bool last = (i == 3U);
        const std::size_t token_end = last ? end : comma;
        if (token_end == std::string::npos || token_end <= cursor) {
            return false;
        }
        float value = 0.0f;
        if (!ParseFloatToken(params_json.substr(cursor, token_end - cursor), &value)) {
            return false;
        }
        parsed[i] = value;
        cursor = token_end + 1U;
    }
    *out_color = parsed;
    return true;
}

bool TryGetTypedFloatParam(const avatar::MaterialRenderPayload& payload, const std::string& id, float* out_value) {
    if (out_value == nullptr) {
        return false;
    }
    const auto it = std::find_if(
        payload.typed_float_params.begin(),
        payload.typed_float_params.end(),
        [&](const avatar::MaterialRenderPayload::TypedFloatParam& p) { return p.id == id; });
    if (it == payload.typed_float_params.end()) {
        return false;
    }
    *out_value = it->value;
    return true;
}

bool TryGetTypedColorParam(
    const avatar::MaterialRenderPayload& payload,
    const std::string& id,
    std::array<float, 4U>* out_color) {
    if (out_color == nullptr) {
        return false;
    }
    const auto it = std::find_if(
        payload.typed_color_params.begin(),
        payload.typed_color_params.end(),
        [&](const avatar::MaterialRenderPayload::TypedColorParam& p) { return p.id == id; });
    if (it == payload.typed_color_params.end()) {
        return false;
    }
    *out_color = {it->rgba[0], it->rgba[1], it->rgba[2], it->rgba[3]};
    return true;
}

bool TryGetTypedTextureRef(
    const avatar::MaterialRenderPayload& payload,
    const std::string& slot,
    std::string* out_texture_ref) {
    if (out_texture_ref == nullptr) {
        return false;
    }
    const auto it = std::find_if(
        payload.typed_texture_params.begin(),
        payload.typed_texture_params.end(),
        [&](const avatar::MaterialRenderPayload::TypedTextureParam& p) {
            return NormalizeRefKey(p.slot) == NormalizeRefKey(slot);
        });
    if (it == payload.typed_texture_params.end()) {
        return false;
    }
    *out_texture_ref = it->texture_ref;
    return !out_texture_ref->empty();
}

std::string ResolveAlphaMode(const avatar::MaterialRenderPayload& payload) {
    std::string alpha_mode = CanonicalizeAlphaMode(payload.alpha_mode);
    const std::string typed_encoding = NormalizeRefKey(payload.material_param_encoding);
    if (typed_encoding == "typed-v2" || typed_encoding == "typed-v3" || typed_encoding == "typed-v4") {
        if (payload.feature_flags & (1U << 1)) {
            return "BLEND";
        }
        if (payload.feature_flags & (1U << 0)) {
            return "MASK";
        }
    }
    const std::string upper_json = ToUpperAscii(payload.shader_params_json);
    if (upper_json.find("_ALPHATEST_ON") != std::string::npos) {
        return "MASK";
    }
    if (upper_json.find("_ALPHABLEND_ON") != std::string::npos ||
        upper_json.find("_ALPHAPREMULTIPLY_ON") != std::string::npos) {
        return "BLEND";
    }
    float value = 0.0f;
    if (TryExtractShaderParamFloat(payload.shader_params_json, "_AlphaClip", &value) && value > 0.5f) {
        return "MASK";
    }
    if (TryExtractShaderParamFloat(payload.shader_params_json, "_UseAlphaClipping", &value) && value > 0.5f) {
        return "MASK";
    }
    if (TryExtractShaderParamFloat(payload.shader_params_json, "_Cutoff", &value) &&
        value > 0.001f &&
        alpha_mode != "OPAQUE") {
        return "MASK";
    }
    if (TryExtractShaderParamFloat(payload.shader_params_json, "_Surface", &value) && value >= 1.0f) {
        return "BLEND";
    }
    if (TryExtractShaderParamFloat(payload.shader_params_json, "_Mode", &value) && value >= 2.0f) {
        return "BLEND";
    }
    return CanonicalizeAlphaMode(alpha_mode);
}

bool EnsureAvatarGpuMaterials(RendererResources* renderer, const AvatarPackage& avatar_pkg, std::uint64_t handle, ID3D11Device* device) {
    if (renderer == nullptr || device == nullptr) {
        return false;
    }
    if (renderer->avatar_materials.find(handle) != renderer->avatar_materials.end()) {
        return true;
    }
    std::vector<GpuMaterialResource> materials;
    materials.reserve(std::max<std::size_t>(avatar_pkg.material_payloads.size(), 1U));
    std::unordered_map<std::string, const avatar::TextureRenderPayload*> textures_by_key;
    textures_by_key.reserve(avatar_pkg.texture_payloads.size());
    std::unordered_map<std::string, const avatar::TextureRenderPayload*> textures_by_normalized_alias;
    textures_by_normalized_alias.reserve(avatar_pkg.texture_payloads.size() * 2U);
    std::unordered_map<std::string, const avatar::TextureRenderPayload*> textures_by_basename;
    textures_by_basename.reserve(avatar_pkg.texture_payloads.size());
    const auto insert_unique_or_ambiguous = [](
        std::unordered_map<std::string, const avatar::TextureRenderPayload*>* map,
        const std::string& key,
        const avatar::TextureRenderPayload* payload) {
        if (map == nullptr || key.empty() || payload == nullptr) {
            return;
        }
        const auto it = map->find(key);
        if (it == map->end()) {
            map->emplace(key, payload);
            return;
        }
        if (it->second != payload) {
            it->second = nullptr;
        }
    };
    const auto trim_texture_prefix = [](std::string key) {
        for (;;) {
            if (key.rfind("./", 0U) == 0U) {
                key.erase(0U, 2U);
                continue;
            }
            if (key.rfind("/", 0U) == 0U) {
                key.erase(0U, 1U);
                continue;
            }
            if (key.rfind("assets/", 0U) == 0U) {
                key.erase(0U, 7U);
                continue;
            }
            if (key.rfind("textures/", 0U) == 0U) {
                key.erase(0U, 9U);
                continue;
            }
            if (key.rfind("texture/", 0U) == 0U) {
                key.erase(0U, 8U);
                continue;
            }
            if (key.rfind("resources/", 0U) == 0U) {
                key.erase(0U, 10U);
                continue;
            }
            break;
        }
        return key;
    };
    const auto drop_extension = [](std::string key) {
        const auto slash = key.find_last_of('/');
        const auto dot = key.find_last_of('.');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
            key.erase(dot);
        }
        return key;
    };
    for (const auto& tex : avatar_pkg.texture_payloads) {
        const std::string key = NormalizeRefKey(tex.name);
        textures_by_key[key] = &tex;
        const std::string trimmed = trim_texture_prefix(key);
        const std::string key_noext = drop_extension(key);
        const std::string trimmed_noext = drop_extension(trimmed);
        insert_unique_or_ambiguous(&textures_by_normalized_alias, trimmed, &tex);
        insert_unique_or_ambiguous(&textures_by_normalized_alias, key_noext, &tex);
        insert_unique_or_ambiguous(&textures_by_normalized_alias, trimmed_noext, &tex);
        const std::string basename = drop_extension(ExtractTerminalToken(key));
        insert_unique_or_ambiguous(&textures_by_basename, basename, &tex);
    }
    enum class TextureMatchStage {
        Exact,
        Normalized,
        Basename,
        Missing,
        Ambiguous,
    };
    const auto texture_match_stage_name = [](TextureMatchStage stage) {
        switch (stage) {
        case TextureMatchStage::Exact:
            return "exact";
        case TextureMatchStage::Normalized:
            return "normalized";
        case TextureMatchStage::Basename:
            return "basename";
        case TextureMatchStage::Ambiguous:
            return "ambiguous";
        default:
            return "miss";
        }
    };
    struct TextureResolveResult {
        const avatar::TextureRenderPayload* payload = nullptr;
        TextureMatchStage stage = TextureMatchStage::Missing;
    };
    const bool allow_miq_texture_alias_fallback = ShouldAllowMiqTextureAliasFallback();
    auto resolve_texture_payload = [&](const std::string& texture_ref) -> TextureResolveResult {
        TextureResolveResult out {};
        if (texture_ref.empty()) {
            return out;
        }
        const std::string key = NormalizeRefKey(texture_ref);
        const auto exact_it = textures_by_key.find(key);
        if (exact_it != textures_by_key.end()) {
            out.payload = exact_it->second;
            out.stage = TextureMatchStage::Exact;
            return out;
        }
        if (avatar_pkg.source_type == AvatarSourceType::Miq) {
            const std::string trimmed = trim_texture_prefix(key);
            const std::string key_noext = drop_extension(key);
            const std::string trimmed_noext = drop_extension(trimmed);
            const std::array<std::string, 3U> normalized_candidates = {trimmed, key_noext, trimmed_noext};
            for (const auto& candidate : normalized_candidates) {
                if (candidate.empty()) {
                    continue;
                }
                const auto it = textures_by_normalized_alias.find(candidate);
                if (it == textures_by_normalized_alias.end()) {
                    continue;
                }
                if (it->second == nullptr) {
                    out.stage = TextureMatchStage::Ambiguous;
                    return out;
                }
                out.payload = it->second;
                out.stage = TextureMatchStage::Normalized;
                return out;
            }
            if (allow_miq_texture_alias_fallback) {
                const std::string basename = drop_extension(ExtractTerminalToken(key));
                if (!basename.empty()) {
                    const auto it = textures_by_basename.find(basename);
                    if (it != textures_by_basename.end()) {
                        if (it->second == nullptr) {
                            out.stage = TextureMatchStage::Ambiguous;
                            return out;
                        }
                        out.payload = it->second;
                        out.stage = TextureMatchStage::Basename;
                        return out;
                    }
                }
            }
        }
        return out;
    };
    const bool use_conservative_miq_material_path = ShouldUseConservativeMiqMaterialPath();
    for (const auto& payload : avatar_pkg.material_payloads) {
        GpuMaterialResource material {};
        const bool conservative_miq_material =
            avatar_pkg.source_type == AvatarSourceType::Miq && use_conservative_miq_material_path;
        const bool is_vrm_source = avatar_pkg.source_type == AvatarSourceType::Vrm;
        const char* unresolved_texture_code = is_vrm_source
            ? "VRM_MATERIAL_TEXTURE_UNRESOLVED"
            : "MIQ_MATERIAL_TYPED_TEXTURE_UNRESOLVED";
        const char* ambiguous_texture_code = is_vrm_source
            ? "VRM_MATERIAL_TEXTURE_AMBIGUOUS"
            : "MIQ_MATERIAL_TEXTURE_AMBIGUOUS";
        std::vector<std::string> fallback_reasons;
        const std::string shader_family = NormalizeShaderFamilyKey(payload.shader_family);
        material.shader_family = shader_family;
        material.shader_variant = payload.shader_variant.empty() ? "default" : payload.shader_variant;
        material.pass_flags = payload.pass_flags.empty() ? "base" : payload.pass_flags;
        const std::vector<std::string> pass_tokens = TokenizeLooseFlags(material.pass_flags + " " + payload.keyword_set);
        const bool pass_declared = !payload.pass_flags.empty();
        material.enable_base_pass =
            !pass_declared ||
            HasLooseToken(pass_tokens, "base") ||
            HasLooseToken(pass_tokens, "main") ||
            HasLooseToken(pass_tokens, "forward");
        material.enable_outline_pass =
            HasLooseToken(pass_tokens, "outline") ||
            HasLooseToken(pass_tokens, "outline_on") ||
            HasLooseToken(pass_tokens, "_outline_on");
        material.enable_emission_pass =
            HasLooseToken(pass_tokens, "emission") ||
            HasLooseToken(pass_tokens, "emission_on") ||
            HasLooseToken(pass_tokens, "_emission") ||
            HasLooseToken(pass_tokens, "_emission_on");
        const bool has_depth_token =
            HasLooseToken(pass_tokens, "depth") ||
            HasLooseToken(pass_tokens, "depthonly") ||
            HasLooseToken(pass_tokens, "zprepass");
        const bool has_shadow_token =
            HasLooseToken(pass_tokens, "shadow") ||
            HasLooseToken(pass_tokens, "shadowcaster") ||
            HasLooseToken(pass_tokens, "castshadow") ||
            HasLooseToken(pass_tokens, "forwardadd") ||
            HasLooseToken(pass_tokens, "forward_add");
        const bool base_only_pass_declared =
            pass_declared &&
            (HasLooseToken(pass_tokens, "base") ||
             HasLooseToken(pass_tokens, "main") ||
             HasLooseToken(pass_tokens, "forward")) &&
            !has_depth_token &&
            !has_shadow_token &&
            !material.enable_outline_pass &&
            !material.enable_emission_pass;
        material.enable_depth_pass = !pass_declared || has_depth_token;
        material.enable_shadow_pass = has_shadow_token;
        // Some MIQ exports carry non-canonical pass strings. If every pass is
        // disabled, force a base pass to avoid a "drawcalls=0 / active_passes=none" frame.
        if (avatar_pkg.source_type == AvatarSourceType::Miq &&
            !material.enable_base_pass &&
            !material.enable_depth_pass &&
            !material.enable_shadow_pass &&
            !material.enable_outline_pass &&
            !material.enable_emission_pass) {
            material.enable_base_pass = true;
            fallback_reasons.push_back("miq_pass_flags_defaulted_to_base");
        }
        material.backend_requested = ResolveFamilyBackendRequest(shader_family);
        material.backend_selected = material.backend_requested;
        material.backend_fallback_applied = false;
        material.backend_fallback_reason = "none";
        if (material.backend_selected != RenderFamilyBackendKind::Liltoon &&
            material.backend_selected != RenderFamilyBackendKind::Mtoon &&
            material.backend_selected != RenderFamilyBackendKind::Poiyomi &&
            material.backend_selected != RenderFamilyBackendKind::Standard) {
            material.backend_selected = RenderFamilyBackendKind::Common;
        }
        const bool family_supported = IsSupportedShaderFamilyKey(shader_family);
        const bool has_typed_payload =
            IsTypedEncoding(payload.material_param_encoding) ||
            !payload.typed_float_params.empty() ||
            !payload.typed_color_params.empty() ||
            !payload.typed_texture_params.empty();
        if (has_typed_payload && !family_supported) {
            fallback_reasons.push_back("unsupported_shader_family");
        }
        if (!payload.alpha_mode.empty() && CanonicalizeAlphaMode(payload.alpha_mode) != ToUpperAscii(payload.alpha_mode)) {
            fallback_reasons.push_back("alpha_mode_defaulted");
        }
        if (avatar_pkg.source_type == AvatarSourceType::Miq && !payload.alpha_mode.empty()) {
            // MIQ payload alpha_mode is already normalized by loader-side parity logic.
            // Trust payload to avoid runtime heuristic drift (e.g. OPAQUE -> MASK).
            material.alpha_mode = CanonicalizeAlphaMode(payload.alpha_mode);
        } else {
            material.alpha_mode = ResolveAlphaMode(payload);
        }
        const std::string resolved_alpha_mode = ToUpperAscii(material.alpha_mode);
        const bool alpha_allows_depth = resolved_alpha_mode != "BLEND";
        const bool parity_family =
            shader_family == "liltoon" ||
            shader_family == "poiyomi" ||
            shader_family == "mtoon" ||
            shader_family == "standard";
        if (!alpha_allows_depth) {
            material.enable_depth_pass = false;
            material.enable_shadow_pass = false;
        } else if (!pass_declared && parity_family) {
            material.enable_shadow_pass = true;
        } else if (avatar_pkg.source_type == AvatarSourceType::Miq &&
                   base_only_pass_declared &&
                   parity_family) {
            // Legacy MIQ exports may declare only "base" pass flags even when
            // Unity material has a valid shadow caster path. Infer shadow pass.
            material.enable_shadow_pass = true;
            fallback_reasons.push_back("miq_pass_flags_base_only_shadow_inferred");
        }
        material.alpha_cutoff = payload.alpha_cutoff;
        material.double_sided = payload.double_sided;
        float typed_cutoff = 0.0f;
        if (TryGetTypedFloatParam(payload, "_Cutoff", &typed_cutoff)) {
            material.alpha_cutoff = std::max(0.0f, std::min(1.0f, typed_cutoff));
        }
        std::array<float, 4U> base_color = {1.0f, 1.0f, 1.0f, 1.0f};
        const bool has_typed_base_color = TryGetTypedColorParam(payload, "_BaseColor", &base_color);
        bool has_base_color = has_typed_base_color;
        if (!has_base_color) {
            has_base_color = TryExtractShaderParamColor(payload.shader_params_json, "_BaseColor", &base_color);
        }
        if (!has_base_color) {
            has_base_color = TryExtractShaderParamColor(payload.shader_params_json, "_Color", &base_color);
        }
        if (!has_base_color) {
            fallback_reasons.push_back("base_color_defaulted");
        }
        for (float& c : base_color) {
            c = std::max(0.0f, std::min(1.0f, c));
        }
        material.base_color = base_color;
        std::array<float, 4U> shade_color = {1.0f, 1.0f, 1.0f, 1.0f};
        if (TryGetTypedColorParam(payload, "_ShadeColor", &shade_color) ||
            TryExtractShaderParamColor(payload.shader_params_json, "_ShadeColor", &shade_color)) {
            for (float& c : shade_color) {
                c = std::max(0.0f, std::min(1.0f, c));
            }
            material.shade_mix = 0.80f;
        }
        material.shade_color = shade_color;
        std::array<float, 4U> emission_color = {0.0f, 0.0f, 0.0f, 1.0f};
        if (TryGetTypedColorParam(payload, "_EmissionColor", &emission_color) ||
            TryExtractShaderParamColor(payload.shader_params_json, "_EmissionColor", &emission_color)) {
            for (float& c : emission_color) {
                c = std::max(0.0f, std::min(1.0f, c));
            }
            material.emission_strength = std::max(emission_color[0], std::max(emission_color[1], emission_color[2])) > 0.001f
                ? 1.0f
                : 0.0f;
        }
        material.emission_color = emission_color;

        std::array<float, 4U> rim_color = {0.0f, 0.0f, 0.0f, 1.0f};
        if (TryGetTypedColorParam(payload, "_RimColor", &rim_color) ||
            TryExtractShaderParamColor(payload.shader_params_json, "_RimColor", &rim_color)) {
            for (float& c : rim_color) {
                c = std::max(0.0f, std::min(1.0f, c));
            }
            material.rim_strength = std::max(rim_color[0], std::max(rim_color[1], rim_color[2])) > 0.001f ? 1.0f : 0.0f;
        }
        material.rim_color = rim_color;

        float rim_power = 2.0f;
        if (TryGetTypedFloatParam(payload, "_RimFresnelPower", &rim_power) ||
            TryExtractShaderParamFloat(payload.shader_params_json, "_RimFresnelPower", &rim_power)) {
            material.rim_power = std::max(0.1f, std::min(12.0f, rim_power));
        }

        float rim_mix = 0.0f;
        if (TryGetTypedFloatParam(payload, "_RimLightingMix", &rim_mix) ||
            TryExtractShaderParamFloat(payload.shader_params_json, "_RimLightingMix", &rim_mix)) {
            material.rim_strength = std::max(material.rim_strength, std::max(0.0f, std::min(1.0f, rim_mix)));
        }

        float bump_scale = 0.0f;
        if (TryGetTypedFloatParam(payload, "_BumpScale", &bump_scale) ||
            TryExtractShaderParamFloat(payload.shader_params_json, "_BumpScale", &bump_scale)) {
            material.normal_strength = std::max(0.0f, std::min(1.0f, bump_scale));
        }
        float emission_strength = 0.0f;
        if (TryGetTypedFloatParam(payload, "_EmissionStrength", &emission_strength) ||
            TryExtractShaderParamFloat(payload.shader_params_json, "_EmissionMapStrength", &emission_strength) ||
            TryExtractShaderParamFloat(payload.shader_params_json, "_EmissionStrength", &emission_strength)) {
            material.emission_strength = std::max(material.emission_strength, std::max(0.0f, std::min(3.0f, emission_strength)));
        }
        float matcap_strength = 0.0f;
        if (TryGetTypedFloatParam(payload, "_MatCapBlend", &matcap_strength) ||
            TryExtractShaderParamFloat(payload.shader_params_json, "_MatCapBlend", &matcap_strength) ||
            TryExtractShaderParamFloat(payload.shader_params_json, "_MatCapBlendUV1", &matcap_strength) ||
            TryExtractShaderParamFloat(payload.shader_params_json, "_MatCapStrength", &matcap_strength)) {
            material.matcap_strength = std::max(0.0f, std::min(1.0f, matcap_strength));
        }
        float outline_width = 0.0f;
        if (TryGetTypedFloatParam(payload, "_OutlineWidth", &outline_width) ||
            TryExtractShaderParamFloat(payload.shader_params_json, "_OutlineWidth", &outline_width)) {
            material.outline_width = std::max(0.0f, std::min(1.0f, outline_width));
        }
        float outline_mix = 0.0f;
        if (TryGetTypedFloatParam(payload, "_OutlineLightingMix", &outline_mix) ||
            TryExtractShaderParamFloat(payload.shader_params_json, "_OutlineLightingMix", &outline_mix)) {
            material.outline_lighting_mix = std::max(0.0f, std::min(1.0f, outline_mix));
        }
        float uv_scroll_x = 0.0f;
        if (TryGetTypedFloatParam(payload, "_UvAnimScrollX", &uv_scroll_x) ||
            TryExtractShaderParamFloat(payload.shader_params_json, "_UvAnimScrollX", &uv_scroll_x)) {
            material.uv_anim_scroll_x = std::max(-2.0f, std::min(2.0f, uv_scroll_x));
        }
        float uv_scroll_y = 0.0f;
        if (TryGetTypedFloatParam(payload, "_UvAnimScrollY", &uv_scroll_y) ||
            TryExtractShaderParamFloat(payload.shader_params_json, "_UvAnimScrollY", &uv_scroll_y)) {
            material.uv_anim_scroll_y = std::max(-2.0f, std::min(2.0f, uv_scroll_y));
        }
        float uv_rot = 0.0f;
        if (TryGetTypedFloatParam(payload, "_UvAnimRotation", &uv_rot) ||
            TryExtractShaderParamFloat(payload.shader_params_json, "_UvAnimRotation", &uv_rot)) {
            material.uv_anim_rotation = std::max(-6.0f, std::min(6.0f, uv_rot));
        }
        material.uv_anim_enabled =
            std::abs(material.uv_anim_scroll_x) > 0.0001f ||
            std::abs(material.uv_anim_scroll_y) > 0.0001f ||
            std::abs(material.uv_anim_rotation) > 0.0001f;
        std::array<float, 4U> matcap_color = {0.0f, 0.0f, 0.0f, 1.0f};
        if (TryGetTypedColorParam(payload, "_MatCapColor", &matcap_color) ||
            TryExtractShaderParamColor(payload.shader_params_json, "_MatCapColor", &matcap_color) ||
            TryExtractShaderParamColor(payload.shader_params_json, "_MatCapTexColor", &matcap_color)) {
            for (float& c : matcap_color) {
                c = std::max(0.0f, std::min(1.0f, c));
            }
            material.matcap_strength = std::max(material.matcap_strength, 0.35f);
        }
        material.matcap_color = matcap_color;

        std::string base_texture_ref = payload.base_color_texture_name;
        bool unresolved_base_texture = false;
        bool unresolved_normal_texture = false;
        bool unresolved_rim_texture = false;
        bool unresolved_emission_texture = false;
        bool unresolved_matcap_texture = false;
        bool unresolved_uv_mask_texture = false;
        const bool has_typed_base_ref =
            TryGetTypedTextureRef(payload, "base", &base_texture_ref) ||
            TryGetTypedTextureRef(payload, "main", &base_texture_ref) ||
            TryGetTypedTextureRef(payload, "_MainTex", &base_texture_ref) ||
            TryGetTypedTextureRef(payload, "_BaseMap", &base_texture_ref);
        if (!base_texture_ref.empty()) {
            const auto tex_res = resolve_texture_payload(base_texture_ref);
            if (tex_res.payload != nullptr) {
                material.base_color_srv = CreateTextureSrvFromPayload(device, tex_res.payload);
            } else if (has_typed_base_ref) {
                unresolved_base_texture = true;
                std::ostringstream warning;
                const char* warning_code = tex_res.stage == TextureMatchStage::Ambiguous ? ambiguous_texture_code : unresolved_texture_code;
                warning << "W_RENDER: " << warning_code << ": material=" << payload.name
                        << ", slot=base, ref=" << base_texture_ref
                        << ", match_stage=" << texture_match_stage_name(tex_res.stage);
                auto avatar_it = g_state.avatars.find(handle);
                if (avatar_it != g_state.avatars.end()) {
                    avatar_it->second.warnings.push_back(warning.str());
                    avatar_it->second.warning_codes.push_back(warning_code);
                }
            } else {
                unresolved_base_texture = true;
            }
        }

        std::string normal_texture_ref;
        const bool has_typed_normal_ref =
            TryGetTypedTextureRef(payload, "normal", &normal_texture_ref) ||
            TryGetTypedTextureRef(payload, "_BumpMap", &normal_texture_ref);
        if (!conservative_miq_material && !normal_texture_ref.empty()) {
            const auto tex_res = resolve_texture_payload(normal_texture_ref);
            if (tex_res.payload != nullptr) {
                material.normal_srv = CreateTextureSrvFromPayload(device, tex_res.payload);
                if (material.normal_strength < 0.01f) {
                    material.normal_strength = 0.35f;
                }
            } else if (has_typed_normal_ref) {
                unresolved_normal_texture = true;
                std::ostringstream warning;
                const char* warning_code = tex_res.stage == TextureMatchStage::Ambiguous ? ambiguous_texture_code : unresolved_texture_code;
                warning << "W_RENDER: " << warning_code << ": material=" << payload.name
                        << ", slot=normal, ref=" << normal_texture_ref
                        << ", match_stage=" << texture_match_stage_name(tex_res.stage);
                auto avatar_it = g_state.avatars.find(handle);
                if (avatar_it != g_state.avatars.end()) {
                    avatar_it->second.warnings.push_back(warning.str());
                    avatar_it->second.warning_codes.push_back(warning_code);
                }
            } else {
                unresolved_normal_texture = true;
            }
        }

        std::string rim_texture_ref;
        const bool has_typed_rim_ref =
            TryGetTypedTextureRef(payload, "rim", &rim_texture_ref) ||
            TryGetTypedTextureRef(payload, "_RimTex", &rim_texture_ref);
        if (!conservative_miq_material && !rim_texture_ref.empty()) {
            const auto tex_res = resolve_texture_payload(rim_texture_ref);
            if (tex_res.payload != nullptr) {
                material.rim_srv = CreateTextureSrvFromPayload(device, tex_res.payload);
                material.rim_strength = std::max(0.35f, material.rim_strength);
            } else if (has_typed_rim_ref) {
                unresolved_rim_texture = true;
                std::ostringstream warning;
                const char* warning_code = tex_res.stage == TextureMatchStage::Ambiguous ? ambiguous_texture_code : unresolved_texture_code;
                warning << "W_RENDER: " << warning_code << ": material=" << payload.name
                        << ", slot=rim, ref=" << rim_texture_ref
                        << ", match_stage=" << texture_match_stage_name(tex_res.stage);
                auto avatar_it = g_state.avatars.find(handle);
                if (avatar_it != g_state.avatars.end()) {
                    avatar_it->second.warnings.push_back(warning.str());
                    avatar_it->second.warning_codes.push_back(warning_code);
                }
            } else {
                unresolved_rim_texture = true;
            }
        }
        std::string emission_texture_ref;
        const bool has_typed_emission_ref =
            TryGetTypedTextureRef(payload, "emission", &emission_texture_ref) ||
            TryGetTypedTextureRef(payload, "_EmissionMap", &emission_texture_ref);
        if (!conservative_miq_material && !emission_texture_ref.empty()) {
            const auto tex_res = resolve_texture_payload(emission_texture_ref);
            if (tex_res.payload != nullptr) {
                material.emission_srv = CreateTextureSrvFromPayload(device, tex_res.payload);
                material.emission_strength = std::max(material.emission_strength, 0.75f);
            } else if (has_typed_emission_ref) {
                unresolved_emission_texture = true;
                std::ostringstream warning;
                const char* warning_code = tex_res.stage == TextureMatchStage::Ambiguous ? ambiguous_texture_code : unresolved_texture_code;
                warning << "W_RENDER: " << warning_code << ": material=" << payload.name
                        << ", slot=emission, ref=" << emission_texture_ref
                        << ", match_stage=" << texture_match_stage_name(tex_res.stage);
                auto avatar_it = g_state.avatars.find(handle);
                if (avatar_it != g_state.avatars.end()) {
                    avatar_it->second.warnings.push_back(warning.str());
                    avatar_it->second.warning_codes.push_back(warning_code);
                }
            } else {
                unresolved_emission_texture = true;
            }
        }

        std::string matcap_texture_ref;
        const bool has_typed_matcap_ref =
            TryGetTypedTextureRef(payload, "matcap", &matcap_texture_ref) ||
            TryGetTypedTextureRef(payload, "_MatCapTex", &matcap_texture_ref) ||
            TryGetTypedTextureRef(payload, "_MatCapTexture", &matcap_texture_ref);
        if (!conservative_miq_material && !matcap_texture_ref.empty()) {
            const auto tex_res = resolve_texture_payload(matcap_texture_ref);
            if (tex_res.payload != nullptr) {
                material.matcap_srv = CreateTextureSrvFromPayload(device, tex_res.payload);
                material.matcap_strength = std::max(material.matcap_strength, 0.35f);
            } else if (has_typed_matcap_ref) {
                unresolved_matcap_texture = true;
                std::ostringstream warning;
                const char* warning_code = tex_res.stage == TextureMatchStage::Ambiguous ? ambiguous_texture_code : unresolved_texture_code;
                warning << "W_RENDER: " << warning_code << ": material=" << payload.name
                        << ", slot=matcap, ref=" << matcap_texture_ref
                        << ", match_stage=" << texture_match_stage_name(tex_res.stage);
                auto avatar_it = g_state.avatars.find(handle);
                if (avatar_it != g_state.avatars.end()) {
                    avatar_it->second.warnings.push_back(warning.str());
                    avatar_it->second.warning_codes.push_back(warning_code);
                }
            } else {
                unresolved_matcap_texture = true;
            }
        }
        std::string uv_anim_mask_ref;
        const bool has_typed_uv_mask_ref =
            TryGetTypedTextureRef(payload, "uvAnimationMask", &uv_anim_mask_ref) ||
            TryGetTypedTextureRef(payload, "_UvAnimMaskTex", &uv_anim_mask_ref);
        if (!conservative_miq_material && !uv_anim_mask_ref.empty()) {
            const auto tex_res = resolve_texture_payload(uv_anim_mask_ref);
            if (tex_res.payload != nullptr) {
                material.uv_anim_mask_srv = CreateTextureSrvFromPayload(device, tex_res.payload);
                material.uv_anim_enabled = true;
            } else if (has_typed_uv_mask_ref) {
                unresolved_uv_mask_texture = true;
                std::ostringstream warning;
                const char* warning_code = tex_res.stage == TextureMatchStage::Ambiguous ? ambiguous_texture_code : unresolved_texture_code;
                warning << "W_RENDER: " << warning_code << ": material=" << payload.name
                        << ", slot=uvAnimationMask, ref=" << uv_anim_mask_ref
                        << ", match_stage=" << texture_match_stage_name(tex_res.stage);
                auto avatar_it = g_state.avatars.find(handle);
                if (avatar_it != g_state.avatars.end()) {
                    avatar_it->second.warnings.push_back(warning.str());
                    avatar_it->second.warning_codes.push_back(warning_code);
                }
            } else {
                unresolved_uv_mask_texture = true;
            }
        }
        if (avatar_pkg.source_type == AvatarSourceType::Vrm) {
            if (unresolved_base_texture) {
                fallback_reasons.push_back("vrm_base_texture_unresolved");
                material.base_color_srv = nullptr;
                material.base_color[0] = std::max(0.65f, material.base_color[0]);
                material.base_color[1] = std::max(0.65f, material.base_color[1]);
                material.base_color[2] = std::max(0.65f, material.base_color[2]);
                material.base_color[3] = std::max(0.85f, material.base_color[3]);
            }
            if (unresolved_normal_texture) {
                fallback_reasons.push_back("vrm_normal_texture_unresolved");
                material.normal_srv = nullptr;
                material.normal_strength = 0.0f;
            }
            if (unresolved_rim_texture) {
                fallback_reasons.push_back("vrm_rim_texture_unresolved");
                material.rim_srv = nullptr;
                material.rim_strength = 0.0f;
            }
            if (unresolved_emission_texture) {
                fallback_reasons.push_back("vrm_emission_texture_unresolved");
                material.emission_srv = nullptr;
                material.emission_strength = 0.0f;
            }
            if (unresolved_matcap_texture) {
                fallback_reasons.push_back("vrm_matcap_texture_unresolved");
                material.matcap_srv = nullptr;
                material.matcap_strength = 0.0f;
            }
            if (unresolved_uv_mask_texture) {
                fallback_reasons.push_back("vrm_uv_mask_texture_unresolved");
                material.uv_anim_mask_srv = nullptr;
                material.uv_anim_enabled = false;
            }
        }
        if (conservative_miq_material) {
            if (material.backend_selected != RenderFamilyBackendKind::Common) {
                material.backend_fallback_applied = true;
                material.backend_fallback_reason = "conservative_miq_material";
                material.backend_selected = RenderFamilyBackendKind::Common;
            }
            material.normal_srv = nullptr;
            material.rim_srv = nullptr;
            material.emission_srv = nullptr;
            material.matcap_srv = nullptr;
            material.uv_anim_mask_srv = nullptr;
            material.normal_strength = 0.0f;
            material.rim_strength = 0.0f;
            material.emission_strength = 0.0f;
            material.matcap_strength = 0.0f;
            material.uv_anim_enabled = false;
            material.shade_mix = 0.0f;
            material.alpha_mode = "OPAQUE";
            material.alpha_cutoff = 0.0f;
            material.double_sided = false;
            material.enable_depth_pass = true;
            material.enable_shadow_pass = false;
            material.enable_outline_pass = false;
            material.enable_emission_pass = false;
        }
        if (material.backend_selected == RenderFamilyBackendKind::Common &&
            material.backend_requested != RenderFamilyBackendKind::Common &&
            !material.backend_fallback_applied) {
            material.backend_fallback_applied = true;
            material.backend_fallback_reason = "backend_not_implemented";
        }
        if (material.backend_fallback_applied) {
            fallback_reasons.push_back(std::string("family_backend_fallback:") + material.backend_fallback_reason);
            std::ostringstream warning;
            const char* fallback_code =
                avatar_pkg.source_type == AvatarSourceType::Vrm
                    ? "VRM_FAMILY_BACKEND_FALLBACK"
                    : "MIQ_FAMILY_BACKEND_FALLBACK";
            warning << "W_RENDER: " << fallback_code
                    << ": material=" << payload.name
                    << ", requested=" << RenderFamilyBackendName(material.backend_requested)
                    << ", selected=" << RenderFamilyBackendName(material.backend_selected)
                    << ", reason=" << material.backend_fallback_reason;
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                PushAvatarWarningUnique(&avatar_it->second, warning.str(), fallback_code);
            }
        }
        if (!fallback_reasons.empty()) {
            std::ostringstream reasons;
            for (std::size_t i = 0U; i < fallback_reasons.size(); ++i) {
                if (i > 0U) {
                    reasons << '|';
                }
                reasons << fallback_reasons[i];
            }
            std::ostringstream warning;
            const bool is_vrm_safe_fallback = avatar_pkg.source_type == AvatarSourceType::Vrm;
            const char* fallback_code = is_vrm_safe_fallback
                ? "VRM_MATERIAL_SAFE_FALLBACK_APPLIED"
                : "MIQ_MATERIAL_FALLBACK_APPLIED";
            warning << "W_RENDER: " << fallback_code << ": material=" << payload.name
                    << ", family=" << shader_family
                    << ", reason=" << reasons.str();
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                avatar_it->second.warnings.push_back(warning.str());
                avatar_it->second.warning_codes.push_back(fallback_code);
            }
        }
        materials.push_back(std::move(material));
    }
    if (materials.empty()) {
        materials.push_back(GpuMaterialResource{});
    }
    renderer->avatar_materials[handle] = std::move(materials);
    return true;
}

DirectX::XMMATRIX ComputeViewMatrix(
    float camera_distance,
    float look_at_y,
    float yaw_deg) {
    using namespace DirectX;
    const float yaw_rad = XMConvertToRadians(yaw_deg);
    const float eye_x = std::sin(yaw_rad) * camera_distance;
    const float eye_z = std::cos(yaw_rad) * camera_distance;
    const XMVECTOR eye = XMVectorSet(eye_x, look_at_y + 0.08f, eye_z, 1.0f);
    const XMVECTOR at = XMVectorSet(0.0f, look_at_y, 0.0f, 1.0f);
    const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    return XMMatrixLookAtRH(eye, at, up);
}

DirectX::XMMATRIX ComputeProjectionMatrix(std::uint32_t width, std::uint32_t height, float fov_deg) {
    using namespace DirectX;
    const float aspect = static_cast<float>(width) / static_cast<float>(std::max<std::uint32_t>(height, 1U));
    return XMMatrixPerspectiveFovRH(XMConvertToRadians(fov_deg), aspect, 0.01f, 100.0f);
}

DirectX::XMMATRIX PoseOffsetToMatrix(const NcPoseBoneOffset& pose) {
    using namespace DirectX;
    return XMMatrixRotationRollPitchYaw(
        XMConvertToRadians(pose.pitch_deg),
        XMConvertToRadians(pose.yaw_deg),
        XMConvertToRadians(pose.roll_deg));
}

NcPoseBoneOffset ScalePoseOffset(const NcPoseBoneOffset& src, float weight) {
    NcPoseBoneOffset out = src;
    const float w = std::max(0.0f, std::min(1.0f, weight));
    out.pitch_deg *= w;
    out.yaw_deg *= w;
    out.roll_deg *= w;
    return out;
}

DirectX::XMMATRIX ComputeUpperBodyPoseOffsetMatrix() {
    const auto hips = GetPoseOffset(static_cast<std::uint32_t>(NC_POSE_BONE_HIPS));
    const auto spine = GetPoseOffset(static_cast<std::uint32_t>(NC_POSE_BONE_SPINE));
    const auto chest = GetPoseOffset(static_cast<std::uint32_t>(NC_POSE_BONE_CHEST));
    const auto upper_chest = GetPoseOffset(static_cast<std::uint32_t>(NC_POSE_BONE_UPPER_CHEST));
    const auto neck = GetPoseOffset(static_cast<std::uint32_t>(NC_POSE_BONE_NECK));
    const auto head = GetPoseOffset(static_cast<std::uint32_t>(NC_POSE_BONE_HEAD));
    return PoseOffsetToMatrix(ScalePoseOffset(hips, 0.35f)) *
           PoseOffsetToMatrix(ScalePoseOffset(spine, 0.55f)) *
           PoseOffsetToMatrix(ScalePoseOffset(chest, 0.75f)) *
           PoseOffsetToMatrix(upper_chest) *
           PoseOffsetToMatrix(ScalePoseOffset(neck, 0.85f)) *
           PoseOffsetToMatrix(head);
}

bool CaptureRtvBgra(
    ID3D11Device* device,
    ID3D11DeviceContext* device_ctx,
    ID3D11RenderTargetView* rtv,
    std::uint32_t width,
    std::uint32_t height,
    std::vector<std::uint8_t>* out_pixels) {
    if (device == nullptr || device_ctx == nullptr || rtv == nullptr || out_pixels == nullptr || width == 0U || height == 0U) {
        return false;
    }

    ID3D11Resource* rtv_resource = nullptr;
    rtv->GetResource(&rtv_resource);
    if (rtv_resource == nullptr) {
        return false;
    }

    ID3D11Texture2D* src_texture = nullptr;
    const HRESULT src_hr = rtv_resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&src_texture));
    rtv_resource->Release();
    if (FAILED(src_hr) || src_texture == nullptr) {
        return false;
    }

    D3D11_TEXTURE2D_DESC src_desc {};
    src_texture->GetDesc(&src_desc);
    if (src_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && src_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
        src_texture->Release();
        return false;
    }

    D3D11_TEXTURE2D_DESC staging_desc = src_desc;
    staging_desc.BindFlags = 0U;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.MiscFlags = 0U;
    staging_desc.SampleDesc.Count = 1U;
    staging_desc.SampleDesc.Quality = 0U;

    ID3D11Texture2D* staging_texture = nullptr;
    const HRESULT create_hr = device->CreateTexture2D(&staging_desc, nullptr, &staging_texture);
    if (FAILED(create_hr) || staging_texture == nullptr) {
        src_texture->Release();
        return false;
    }

    device_ctx->CopyResource(staging_texture, src_texture);

    D3D11_MAPPED_SUBRESOURCE mapped {};
    const HRESULT map_hr = device_ctx->Map(staging_texture, 0U, D3D11_MAP_READ, 0U, &mapped);
    if (FAILED(map_hr)) {
        staging_texture->Release();
        src_texture->Release();
        return false;
    }

    out_pixels->assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U, 0U);
    auto* dst = out_pixels->data();
    const auto* src = static_cast<const std::uint8_t*>(mapped.pData);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* row_src = src + static_cast<std::size_t>(mapped.RowPitch) * y;
        std::uint8_t* row_dst = dst + static_cast<std::size_t>(width) * 4U * y;
        std::memcpy(row_dst, row_src, static_cast<std::size_t>(width) * 4U);
    }

    device_ctx->Unmap(staging_texture, 0U);
    staging_texture->Release();
    src_texture->Release();
    return true;
}

bool TryAnsiToWide(const char* text, std::wstring* out) {
    if (text == nullptr || out == nullptr) {
        return false;
    }
    const int needed = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (needed <= 1) {
        return false;
    }
    out->assign(static_cast<std::size_t>(needed - 1), L'\0');
    const int written = MultiByteToWideChar(
        CP_ACP,
        0,
        text,
        -1,
        out->data(),
        needed);
    return written > 1;
}

bool EncodeBgraToPngFile(
    const std::vector<std::uint8_t>& pixels,
    std::uint32_t width,
    std::uint32_t height,
    const char* output_path) {
    if (pixels.empty() || width == 0U || height == 0U || output_path == nullptr || output_path[0] == '\0') {
        return false;
    }
    if (pixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U) {
        return false;
    }

    std::wstring wide_path;
    if (!TryAnsiToWide(output_path, &wide_path)) {
        return false;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_initialized = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        return false;
    }

    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* props = nullptr;
    bool ok = false;

    do {
        hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory));
        if (FAILED(hr) || factory == nullptr) {
            break;
        }

        hr = factory->CreateStream(&stream);
        if (FAILED(hr) || stream == nullptr) {
            break;
        }

        hr = stream->InitializeFromFilename(wide_path.c_str(), GENERIC_WRITE);
        if (FAILED(hr)) {
            break;
        }

        hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
        if (FAILED(hr) || encoder == nullptr) {
            break;
        }

        hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
        if (FAILED(hr)) {
            break;
        }

        hr = encoder->CreateNewFrame(&frame, &props);
        if (FAILED(hr) || frame == nullptr) {
            break;
        }

        hr = frame->Initialize(props);
        if (FAILED(hr)) {
            break;
        }

        hr = frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));
        if (FAILED(hr)) {
            break;
        }

        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        hr = frame->SetPixelFormat(&format);
        if (FAILED(hr) || format != GUID_WICPixelFormat32bppBGRA) {
            break;
        }

        const UINT stride = static_cast<UINT>(width * 4U);
        const UINT image_size = static_cast<UINT>(pixels.size());
        hr = frame->WritePixels(static_cast<UINT>(height), stride, image_size, const_cast<BYTE*>(pixels.data()));
        if (FAILED(hr)) {
            break;
        }

        hr = frame->Commit();
        if (FAILED(hr)) {
            break;
        }

        hr = encoder->Commit();
        if (FAILED(hr)) {
            break;
        }

        ok = true;
    } while (false);

    if (props != nullptr) {
        props->Release();
    }
    if (frame != nullptr) {
        frame->Release();
    }
    if (encoder != nullptr) {
        encoder->Release();
    }
    if (stream != nullptr) {
        stream->Release();
    }
    if (factory != nullptr) {
        factory->Release();
    }
    if (com_initialized) {
        CoUninitialize();
    }
    return ok;
}
#endif

void PublishTrackingFrame() {
    if (!g_state.osc.IsBound()) {
        return;
    }
    g_state.osc.Publish("/Animiq/Tracking/BlinkL", g_state.latest_tracking.blink_l);
    g_state.osc.Publish("/Animiq/Tracking/BlinkR", g_state.latest_tracking.blink_r);
    g_state.osc.Publish("/Animiq/Tracking/MouthOpen", g_state.latest_tracking.mouth_open);
    g_state.osc.Publish("/Animiq/Tracking/HeadPosX", g_state.latest_tracking.head_pos[0]);
    g_state.osc.Publish("/Animiq/Tracking/HeadPosY", g_state.latest_tracking.head_pos[1]);
    g_state.osc.Publish("/Animiq/Tracking/HeadPosZ", g_state.latest_tracking.head_pos[2]);
}

NcResultCode RenderFrameLocked(const NcRenderContext* ctx) {
    if (!EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (ctx == nullptr || ctx->width == 0U || ctx->height == 0U) {
        SetError(NC_ERROR_INVALID_ARGUMENT, "render", "render context/size is invalid", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (ctx->d3d11_device == nullptr || ctx->d3d11_device_context == nullptr || ctx->d3d11_rtv == nullptr) {
        SetError(
            NC_ERROR_INVALID_ARGUMENT,
            "render",
            "d3d11_device/d3d11_device_context/d3d11_rtv must be non-null",
            true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (g_state.render_ready_avatars.empty()) {
        SetError(NC_ERROR_UNSUPPORTED, "render", "no avatar has render resources", true);
        return NC_ERROR_UNSUPPORTED;
    }
    const auto frame_begin = std::chrono::steady_clock::now();
    double material_resolve_ms = 0.0;

#if defined(_WIN32)
    auto* device = reinterpret_cast<ID3D11Device*>(ctx->d3d11_device);
    auto* device_ctx = reinterpret_cast<ID3D11DeviceContext*>(ctx->d3d11_device_context);
    auto* rtv = reinterpret_cast<ID3D11RenderTargetView*>(ctx->d3d11_rtv);
    if (device == nullptr || device_ctx == nullptr || rtv == nullptr) {
        SetError(
            NC_ERROR_INVALID_ARGUMENT,
            "render",
            "invalid d3d11 context pointers",
            true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    auto& renderer = g_state.renderer;
    if (!EnsurePipelineResources(&renderer, device)) {
        SetError(NC_ERROR_INTERNAL, "render", "failed to initialize D3D11 pipeline resources", true);
        return NC_ERROR_INTERNAL;
    }
    if (!EnsureDepthResources(&renderer, device, ctx->width, ctx->height)) {
        SetError(NC_ERROR_INTERNAL, "render", "failed to initialize depth-stencil resources", true);
        return NC_ERROR_INTERNAL;
    }

    const auto quality = SanitizeRenderQualityOptions(g_state.render_quality);
    const auto lighting = SanitizeLightingOptions(g_state.lighting_options);
    const bool fast_fallback = (quality.quality_profile == NC_RENDER_QUALITY_FAST_FALLBACK);
    const bool shadow_runtime_enabled = !fast_fallback && lighting.enable_shadow > 0U;
    if (shadow_runtime_enabled && !EnsureShadowResources(&renderer, device, lighting.shadow_resolution)) {
        SetError(NC_ERROR_INTERNAL, "render", "failed to initialize shadow-map resources", true);
        return NC_ERROR_INTERNAL;
    }
    const float frame_dt = std::max(1.0f / 240.0f, std::min(1.0f / 15.0f, ctx->delta_time_seconds));
    g_state.runtime_time_seconds =
        std::fmod(std::max(0.0f, g_state.runtime_time_seconds + frame_dt), 3600.0f);
    if (g_state.tracking_weights_dirty && !g_state.avatars.empty()) {
        for (auto& [handle, avatar] : g_state.avatars) {
            (void)handle;
            ApplyTrackingDrivenExpressionWeights(&avatar);
        }
        g_state.tracking_weights_dirty = false;
    }
    const bool spout_active = g_state.spout.IsActive();
    const float clear_alpha = spout_active ? 0.0f : quality.background_rgba[3];
    const float clear_color[4] = {
        quality.background_rgba[0],
        quality.background_rgba[1],
        quality.background_rgba[2],
        clear_alpha};
    device_ctx->OMSetRenderTargets(1, &rtv, renderer.depth_dsv);
    device_ctx->ClearRenderTargetView(rtv, clear_color);
    device_ctx->ClearDepthStencilView(renderer.depth_dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0U);

    D3D11_VIEWPORT viewport {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(ctx->width);
    viewport.Height = static_cast<float>(ctx->height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    device_ctx->RSSetViewports(1U, &viewport);

    device_ctx->IASetInputLayout(renderer.input_layout);
    device_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    device_ctx->VSSetShader(renderer.vertex_shader, nullptr, 0U);
    device_ctx->PSSetShader(renderer.pixel_shader_common, nullptr, 0U);
    device_ctx->VSSetConstantBuffers(0U, 1U, &renderer.constant_buffer);
    device_ctx->PSSetConstantBuffers(0U, 1U, &renderer.constant_buffer);

    struct DrawItem {
        std::uint64_t handle = 0U;
        std::size_t mesh_index = 0U;
        const AvatarPackage* pkg = nullptr;
        GpuMeshResource* mesh = nullptr;
        GpuMaterialResource* material = nullptr;
        RenderFamilyBackendKind backend = RenderFamilyBackendKind::Common;
        DirectX::XMMATRIX world = DirectX::XMMatrixIdentity();
        float view_z = 0.0f;
        bool is_blend = false;
        bool is_mask = false;
    };
    struct FamilyDrawQueues {
        std::vector<DrawItem> opaque_draws;
        std::vector<DrawItem> mask_draws;
        std::vector<DrawItem> blend_draws;
        std::vector<DrawItem> depth_draws;
        std::vector<DrawItem> shadow_draws;
        std::vector<DrawItem> outline_draws;
        std::vector<DrawItem> emission_draws;
    };
    auto backend_index = [](RenderFamilyBackendKind kind) -> std::size_t {
        switch (kind) {
            case RenderFamilyBackendKind::Liltoon:
                return 1U;
            case RenderFamilyBackendKind::Mtoon:
                return 2U;
            case RenderFamilyBackendKind::Poiyomi:
                return 3U;
            case RenderFamilyBackendKind::Standard:
                return 4U;
            case RenderFamilyBackendKind::Common:
            default:
                return 0U;
        }
    };
    std::array<FamilyDrawQueues, 5U> family_draws;
    std::uint32_t frame_draw_calls = 0U;
    const float fov_deg = quality.fov_deg;
    const float tan_half_fov = std::tan(DirectX::XMConvertToRadians(fov_deg) * 0.5f);
    const float camera_distance =
        (quality.camera_mode == NC_CAMERA_MODE_AUTO_FIT_BUST) ? 2.7f : 3.2f;
    const float look_at_y = quality.headroom * 0.6f;
    const auto view = ComputeViewMatrix(camera_distance, look_at_y, quality.yaw_deg);
    const auto proj = ComputeProjectionMatrix(ctx->width, ctx->height, fov_deg);
    const DirectX::XMVECTOR light_position = DirectX::XMVectorSet(
        lighting.light_position[0],
        lighting.light_position[1],
        lighting.light_position[2],
        1.0f);
    const auto light_rot = DirectX::XMMatrixRotationRollPitchYaw(
        DirectX::XMConvertToRadians(lighting.light_euler_deg[0]),
        DirectX::XMConvertToRadians(lighting.light_euler_deg[1]),
        DirectX::XMConvertToRadians(lighting.light_euler_deg[2]));
    const DirectX::XMVECTOR light_forward = DirectX::XMVector3Normalize(
        DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), light_rot));
    const DirectX::XMVECTOR shading_light_dir = DirectX::XMVectorNegate(light_forward);
    DirectX::XMFLOAT3 shading_light_dir3 {};
    DirectX::XMStoreFloat3(&shading_light_dir3, shading_light_dir);
    const DirectX::XMVECTOR light_up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const auto light_view = DirectX::XMMatrixLookToLH(light_position, light_forward, light_up);
    const float light_near = std::max(0.01f, std::min(lighting.shadow_near_plane, lighting.range - 0.05f));
    const auto light_proj = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XMConvertToRadians(lighting.spot_angle_deg),
        1.0f,
        light_near,
        std::max(light_near + 0.05f, lighting.range));
    const auto light_view_proj = light_view * light_proj;
    const float light_intensity_scale = std::max(0.0f, std::min(4.0f, lighting.intensity / 12.5f));
    std::uint32_t avatar_slot = 0U;
    for (const auto handle : g_state.render_ready_avatars) {
        auto it = g_state.avatars.find(handle);
        if (it == g_state.avatars.end()) {
            continue;
        }
        const auto material_resolve_begin = std::chrono::steady_clock::now();
        if (!EnsureAvatarGpuMeshes(&renderer, it->second, handle, device)) {
            if (g_state.last_error_code == NC_OK || g_state.last_error_message.empty()) {
                SetError(NC_ERROR_INTERNAL, "render", "failed to upload mesh payloads to GPU", true);
            }
            return NC_ERROR_INTERNAL;
        }
        if (!EnsureAvatarGpuMaterials(&renderer, it->second, handle, device)) {
            SetError(NC_ERROR_INTERNAL, "render", "failed to create material GPU resources", true);
            return NC_ERROR_INTERNAL;
        }
        if (!ApplyArmPoseToAvatar(&renderer, it->second, handle, device_ctx)) {
            SetError(NC_ERROR_INTERNAL, "render", "failed to apply arm pose offsets", true);
            return NC_ERROR_INTERNAL;
        }
        const auto material_resolve_end = std::chrono::steady_clock::now();
        material_resolve_ms += static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(material_resolve_end - material_resolve_begin).count()) /
            1000.0;
        if (!ApplyExpressionMorphToAvatar(&renderer, it->second, handle, device_ctx)) {
            SetError(NC_ERROR_INTERNAL, "render", "failed to apply expression morphs", true);
            return NC_ERROR_INTERNAL;
        }
        if (!ApplySecondaryMotionToAvatar(&renderer, &it->second, handle, device_ctx, ctx->delta_time_seconds)) {
            SetError(NC_ERROR_INTERNAL, "render", "failed to apply secondary motion", true);
            return NC_ERROR_INTERNAL;
        }
        auto mesh_it = renderer.avatar_meshes.find(handle);
        if (mesh_it == renderer.avatar_meshes.end()) {
            continue;
        }
        auto material_it = renderer.avatar_materials.find(handle);
        if (material_it == renderer.avatar_materials.end()) {
            continue;
        }
        struct MeshExtentSample {
            std::size_t index = 0U;
            float extent = 0.0f;
        };
        std::vector<MeshExtentSample> extent_samples;
        extent_samples.reserve(mesh_it->second.size());
        for (std::size_t mesh_index = 0U; mesh_index < mesh_it->second.size(); ++mesh_index) {
            const auto& m = mesh_it->second[mesh_index];
            const float ex = std::max(m.bounds_max.x - m.bounds_min.x, 0.0f);
            const float ey = std::max(m.bounds_max.y - m.bounds_min.y, 0.0f);
            const float ez = std::max(m.bounds_max.z - m.bounds_min.z, 0.0f);
            const float emax = std::max(ex, std::max(ey, ez));
            if (std::isfinite(emax) && emax > 0.0f) {
                extent_samples.push_back({mesh_index, emax});
            }
        }
        std::vector<float> sorted_extents;
        sorted_extents.reserve(extent_samples.size());
        for (const auto& sample : extent_samples) {
            sorted_extents.push_back(sample.extent);
        }
        std::sort(sorted_extents.begin(), sorted_extents.end());
        const float median_extent = sorted_extents.empty()
            ? 1.0f
            : sorted_extents[sorted_extents.size() / 2U];
        const float extent_threshold = std::max(0.5f, median_extent * 20.0f);
        const float draw_extent_threshold = std::max(2.5f, median_extent * 6.0f);
        const float bounds_cluster_distance_threshold = std::max(2.2f, median_extent * 2.8f);
        const float cluster_bounds_extent_cap = std::max(0.75f, median_extent * 1.4f);
        constexpr std::size_t kMinClusterSamplesForBoundsFilter = 6U;
        std::vector<std::uint8_t> preview_bounds_excluded(mesh_it->second.size(), 0U);
        if (it->second.source_type == AvatarSourceType::Miq) {
            for (const auto& sample : extent_samples) {
                const auto& m = mesh_it->second[sample.index];
                if (!std::isfinite(m.center.x) || !std::isfinite(m.center.y) || !std::isfinite(m.center.z)) {
                    preview_bounds_excluded[sample.index] = 1U;
                    continue;
                }
                if (std::abs(m.center.x) > 1000.0f || std::abs(m.center.y) > 1000.0f || std::abs(m.center.z) > 1000.0f) {
                    preview_bounds_excluded[sample.index] = 1U;
                }
            }
        }
        std::vector<float> center_x_samples;
        std::vector<float> center_y_samples;
        std::vector<float> center_z_samples;
        center_x_samples.reserve(extent_samples.size());
        center_y_samples.reserve(extent_samples.size());
        center_z_samples.reserve(extent_samples.size());
        for (const auto& sample : extent_samples) {
            if (sample.extent > extent_threshold) {
                continue;
            }
            if (preview_bounds_excluded[sample.index] != 0U) {
                continue;
            }
            const auto& m = mesh_it->second[sample.index];
            if (!std::isfinite(m.center.x) || !std::isfinite(m.center.y) || !std::isfinite(m.center.z)) {
                continue;
            }
            if (std::abs(m.center.x) > 1000.0f || std::abs(m.center.y) > 1000.0f || std::abs(m.center.z) > 1000.0f) {
                continue;
            }
            center_x_samples.push_back(m.center.x);
            center_y_samples.push_back(m.center.y);
            center_z_samples.push_back(m.center.z);
        }
        auto pick_median = [](std::vector<float>* values) -> float {
            if (values == nullptr || values->empty()) {
                return 0.0f;
            }
            std::sort(values->begin(), values->end());
            return (*values)[values->size() / 2U];
        };
        const float cluster_cx = pick_median(&center_x_samples);
        const float cluster_cy = pick_median(&center_y_samples);
        const float cluster_cz = pick_median(&center_z_samples);
        const bool allow_cluster_bounds_filter =
            it->second.source_type == AvatarSourceType::Miq &&
            center_x_samples.size() >= kMinClusterSamplesForBoundsFilter;
        if (allow_cluster_bounds_filter) {
            for (const auto& sample : extent_samples) {
                if (preview_bounds_excluded[sample.index] != 0U) {
                    continue;
                }
                if (sample.extent > extent_threshold) {
                    continue;
                }
                if (sample.extent > cluster_bounds_extent_cap) {
                    continue;
                }
                const auto& m = mesh_it->second[sample.index];
                const float ccx = m.center.x - cluster_cx;
                const float ccy = m.center.y - cluster_cy;
                const float ccz = m.center.z - cluster_cz;
                const float cluster_dist = std::sqrt((ccx * ccx) + (ccy * ccy) + (ccz * ccz));
                if (std::isfinite(cluster_dist) && cluster_dist > bounds_cluster_distance_threshold) {
                    preview_bounds_excluded[sample.index] = 1U;
                }
            }
        }
        std::vector<std::string> excluded_mesh_names;
        excluded_mesh_names.reserve(4U);
        std::size_t excluded_bounds_mesh_count = 0U;
        for (std::size_t i = 0U; i < preview_bounds_excluded.size(); ++i) {
            if (preview_bounds_excluded[i] == 0U) {
                continue;
            }
            ++excluded_bounds_mesh_count;
            if (excluded_mesh_names.size() < 4U) {
                excluded_mesh_names.push_back(mesh_it->second[i].mesh_name);
            }
        }

        DirectX::XMFLOAT3 avatar_bmin = {
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
        DirectX::XMFLOAT3 avatar_bmax = {
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max()};
        std::size_t included_bounds_mesh_count = 0U;
        for (const auto& sample : extent_samples) {
            if (sample.extent > extent_threshold) {
                continue;
            }
            if (preview_bounds_excluded[sample.index] != 0U) {
                continue;
            }
            const auto& m = mesh_it->second[sample.index];
            avatar_bmin.x = std::min(avatar_bmin.x, m.bounds_min.x);
            avatar_bmin.y = std::min(avatar_bmin.y, m.bounds_min.y);
            avatar_bmin.z = std::min(avatar_bmin.z, m.bounds_min.z);
            avatar_bmax.x = std::max(avatar_bmax.x, m.bounds_max.x);
            avatar_bmax.y = std::max(avatar_bmax.y, m.bounds_max.y);
            avatar_bmax.z = std::max(avatar_bmax.z, m.bounds_max.z);
            ++included_bounds_mesh_count;
        }
        if (included_bounds_mesh_count == 0U) {
            for (std::size_t mesh_index = 0U; mesh_index < mesh_it->second.size(); ++mesh_index) {
                if (preview_bounds_excluded[mesh_index] != 0U) {
                    continue;
                }
                const auto& m = mesh_it->second[mesh_index];
                avatar_bmin.x = std::min(avatar_bmin.x, m.bounds_min.x);
                avatar_bmin.y = std::min(avatar_bmin.y, m.bounds_min.y);
                avatar_bmin.z = std::min(avatar_bmin.z, m.bounds_min.z);
                avatar_bmax.x = std::max(avatar_bmax.x, m.bounds_max.x);
                avatar_bmax.y = std::max(avatar_bmax.y, m.bounds_max.y);
                avatar_bmax.z = std::max(avatar_bmax.z, m.bounds_max.z);
                ++included_bounds_mesh_count;
            }
        }
        if (included_bounds_mesh_count == 0U) {
            for (const auto& m : mesh_it->second) {
                avatar_bmin.x = std::min(avatar_bmin.x, m.bounds_min.x);
                avatar_bmin.y = std::min(avatar_bmin.y, m.bounds_min.y);
                avatar_bmin.z = std::min(avatar_bmin.z, m.bounds_min.z);
                avatar_bmax.x = std::max(avatar_bmax.x, m.bounds_max.x);
                avatar_bmax.y = std::max(avatar_bmax.y, m.bounds_max.y);
                avatar_bmax.z = std::max(avatar_bmax.z, m.bounds_max.z);
            }
            included_bounds_mesh_count = mesh_it->second.size();
        }
        bool near_origin_bounds_used = false;
        if (it->second.source_type == AvatarSourceType::Miq) {
            DirectX::XMFLOAT3 near_bmin = {
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()};
            DirectX::XMFLOAT3 near_bmax = {
                -std::numeric_limits<float>::max(),
                -std::numeric_limits<float>::max(),
                -std::numeric_limits<float>::max()};
            std::size_t near_count = 0U;
            for (const auto& sample : extent_samples) {
                if (sample.extent > extent_threshold) {
                    continue;
                }
                if (preview_bounds_excluded[sample.index] != 0U) {
                    continue;
                }
                const auto& m = mesh_it->second[sample.index];
                const bool near_origin =
                    std::abs(m.center.x) <= 1000.0f &&
                    std::abs(m.center.y) <= 1000.0f &&
                    std::abs(m.center.z) <= 1000.0f;
                if (!near_origin) {
                    continue;
                }
                near_bmin.x = std::min(near_bmin.x, m.bounds_min.x);
                near_bmin.y = std::min(near_bmin.y, m.bounds_min.y);
                near_bmin.z = std::min(near_bmin.z, m.bounds_min.z);
                near_bmax.x = std::max(near_bmax.x, m.bounds_max.x);
                near_bmax.y = std::max(near_bmax.y, m.bounds_max.y);
                near_bmax.z = std::max(near_bmax.z, m.bounds_max.z);
                ++near_count;
            }
            if (near_count >= 3U) {
                avatar_bmin = near_bmin;
                avatar_bmax = near_bmax;
                included_bounds_mesh_count = near_count;
                near_origin_bounds_used = true;
            }
        }
        const float extent_x = std::max(avatar_bmax.x - avatar_bmin.x, 0.0001f);
        const float extent_y = std::max(avatar_bmax.y - avatar_bmin.y, 0.0001f);
        const float extent_z = std::max(avatar_bmax.z - avatar_bmin.z, 0.0001f);
        const float min_extent = std::max(0.0001f, std::min(extent_x, std::min(extent_y, extent_z)));
        const float aspect_ratio = std::max(extent_x, std::max(extent_y, extent_z)) / min_extent;
        bool autofit_degenerate = false;
        if (it->second.source_type == AvatarSourceType::Miq) {
            autofit_degenerate = aspect_ratio > 30.0f || extent_y < 0.05f;
        }
        if (autofit_degenerate) {
            DirectX::XMFLOAT3 all_bmin = {
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()};
            DirectX::XMFLOAT3 all_bmax = {
                -std::numeric_limits<float>::max(),
                -std::numeric_limits<float>::max(),
                -std::numeric_limits<float>::max()};
            std::size_t all_count = 0U;
            for (const auto& m : mesh_it->second) {
                all_bmin.x = std::min(all_bmin.x, m.bounds_min.x);
                all_bmin.y = std::min(all_bmin.y, m.bounds_min.y);
                all_bmin.z = std::min(all_bmin.z, m.bounds_min.z);
                all_bmax.x = std::max(all_bmax.x, m.bounds_max.x);
                all_bmax.y = std::max(all_bmax.y, m.bounds_max.y);
                all_bmax.z = std::max(all_bmax.z, m.bounds_max.z);
                ++all_count;
            }
            if (all_count > 0U) {
                avatar_bmin = all_bmin;
                avatar_bmax = all_bmax;
                included_bounds_mesh_count = all_count;
                near_origin_bounds_used = false;
            }
        }
        const float safe_extent_x = std::max(avatar_bmax.x - avatar_bmin.x, 0.0001f);
        const float safe_extent_y = std::max(avatar_bmax.y - avatar_bmin.y, 0.0001f);
        const float safe_extent_z = std::max(avatar_bmax.z - avatar_bmin.z, 0.0001f);
        bool has_vsf_placeholder_payload = false;
        if (it->second.source_type == AvatarSourceType::VsfAvatar) {
            for (const auto& warning_code : it->second.warning_codes) {
                if (warning_code == "VSF_OBJECT_STUB_RENDER_PAYLOAD" ||
                    warning_code == "VSF_PLACEHOLDER_RENDER_PAYLOAD") {
                    has_vsf_placeholder_payload = true;
                    break;
                }
            }
        }
        const bool use_vsf_proxy_full_fit =
            has_vsf_placeholder_payload &&
            quality.camera_mode == NC_CAMERA_MODE_AUTO_FIT_BUST;
        const float max_extent = std::max(safe_extent_x, std::max(safe_extent_y, safe_extent_z));
        float fit_scale = 1.4f / max_extent;
        if (quality.camera_mode == NC_CAMERA_MODE_AUTO_FIT_FULL || quality.camera_mode == NC_CAMERA_MODE_AUTO_FIT_BUST) {
            const float fit_basis_height =
                (quality.camera_mode == NC_CAMERA_MODE_AUTO_FIT_BUST && !use_vsf_proxy_full_fit)
                    ? std::max(safe_extent_y * 0.58f, 0.0001f)
                    : safe_extent_y;
            const float desired = use_vsf_proxy_full_fit
                ? std::min(quality.framing_target, 0.68f)
                : quality.framing_target;
            fit_scale = (desired * 2.0f * camera_distance * std::max(0.01f, tan_half_fov)) / fit_basis_height;
        }
        // Some imported assets can carry very large coordinate ranges.
        // Keep tiny fit scales instead of forcing a 0.05 floor, otherwise
        // those avatars remain outside the camera frustum.
        fit_scale = std::max(1.0e-7f, std::min(50.0f, fit_scale));
        const float cx = (avatar_bmin.x + avatar_bmax.x) * 0.5f;
        const float cy = (avatar_bmin.y + avatar_bmax.y) * 0.5f;
        const float cz = (avatar_bmin.z + avatar_bmax.z) * 0.5f;
        std::vector<float> robust_center_x_samples;
        std::vector<float> robust_center_y_samples;
        std::vector<float> robust_center_z_samples;
        robust_center_x_samples.reserve(mesh_it->second.size());
        robust_center_y_samples.reserve(mesh_it->second.size());
        robust_center_z_samples.reserve(mesh_it->second.size());
        for (const auto& mesh : mesh_it->second) {
            if (std::isfinite(mesh.center.x) && std::isfinite(mesh.center.y) && std::isfinite(mesh.center.z)) {
                robust_center_x_samples.push_back(mesh.center.x);
                robust_center_y_samples.push_back(mesh.center.y);
                robust_center_z_samples.push_back(mesh.center.z);
            }
        }
        auto median_of = [](std::vector<float>& values, float fallback) {
            if (values.empty()) {
                return fallback;
            }
            const std::size_t mid = values.size() / 2U;
            std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid), values.end());
            return values[mid];
        };
        const float robust_cx = median_of(robust_center_x_samples, cx);
        const float robust_cy = median_of(robust_center_y_samples, cy);
        const float robust_cz = median_of(robust_center_z_samples, cz);
        std::vector<float> center_dist_samples;
        center_dist_samples.reserve(mesh_it->second.size());
        for (const auto& mesh : mesh_it->second) {
            const float dx = mesh.center.x - robust_cx;
            const float dy = mesh.center.y - robust_cy;
            const float dz = mesh.center.z - robust_cz;
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (std::isfinite(dist)) {
                center_dist_samples.push_back(dist);
            }
        }
        const float median_center_dist = median_of(center_dist_samples, 0.0f);
        float bust_anchor = 0.68f + quality.headroom * 0.2f;
        if (it->second.source_type == AvatarSourceType::Miq && excluded_bounds_mesh_count > 0U) {
            bust_anchor = std::min(bust_anchor, 0.60f + quality.headroom * 0.15f);
        }
        float focus_y = cy;
        if (quality.camera_mode == NC_CAMERA_MODE_AUTO_FIT_BUST) {
            const float focus_from_bounds = avatar_bmin.y + safe_extent_y * bust_anchor;
            if (use_vsf_proxy_full_fit) {
                // Proxy payloads are coarse full-body stubs; center focus prevents over-zoom framing.
                focus_y = avatar_bmin.y + safe_extent_y * 0.50f;
            } else {
            // Robust center keeps framing stable when outlier meshes are excluded.
            const float focus_from_cluster = robust_cy - safe_extent_y * 0.03f;
            if (it->second.source_type == AvatarSourceType::Miq) {
                const bool miq_outlier_filtered = excluded_bounds_mesh_count > 0U;
                const float blend = miq_outlier_filtered ? 0.78f : 0.52f;
                focus_y = focus_from_bounds * (1.0f - blend) + focus_from_cluster * blend;
                const float focus_min = avatar_bmin.y + safe_extent_y * 0.42f;
                const float focus_max = avatar_bmin.y + safe_extent_y * 0.82f;
                focus_y = std::max(focus_min, std::min(focus_max, focus_y));
                if (it->second.source_ext == ".vrm") {
                    // VRM-origin MIQ tends to look "sunk" when bust focus is too high.
                    // Bias slightly downward and clamp to a lower window.
                const float vrm_focus_from_cluster = robust_cy - safe_extent_y * 0.14f;
                focus_y = focus_y * 0.30f + vrm_focus_from_cluster * 0.70f;
                const float vrm_focus_min = avatar_bmin.y + safe_extent_y * 0.34f;
                const float vrm_focus_max = avatar_bmin.y + safe_extent_y * 0.64f;
                focus_y = std::max(vrm_focus_min, std::min(vrm_focus_max, focus_y));
            }
        } else {
                focus_y = focus_from_bounds;
            }
            }
        }
        // Keep preview centered even if multiple handles are present.
        // Host UI currently operates in single-avatar mode, and slot offsets
        // can push the visible avatar out of frame after reload/recovery paths.
        const float x_offset = 0.0f;
        const float preview_yaw_override = PreviewYawRadiansForAvatarPackage(it->second, nullptr);
        {
            std::ostringstream preview_debug;
            preview_debug << "extent=(" << safe_extent_x << "/" << safe_extent_y << "/" << safe_extent_z
                          << "), fit_scale=" << fit_scale
                          << ", center=(" << cx << "/" << cy << "/" << cz << ")"
                          << ", bounds_meshes=" << included_bounds_mesh_count
                          << "/" << mesh_it->second.size()
                          << ", bounds_excluded=" << excluded_bounds_mesh_count
                          << ", bounds_cluster_threshold=" << bounds_cluster_distance_threshold
                          << ", bounds_cluster_extent_cap=" << cluster_bounds_extent_cap
                          << ", autofit_degenerate=" << (autofit_degenerate ? "1" : "0")
                          << ", near_origin=" << (near_origin_bounds_used ? "1" : "0")
                          << ", vsf_proxy_full_fit=" << (use_vsf_proxy_full_fit ? "1" : "0")
                          << ", preview_yaw_deg=" << PreviewYawDegreesForAvatarPackage(it->second, nullptr);
            if (!excluded_mesh_names.empty()) {
                preview_debug << ", excluded_names=";
                for (std::size_t i = 0U; i < excluded_mesh_names.size(); ++i) {
                    if (i > 0U) {
                        preview_debug << "|";
                    }
                    preview_debug << excluded_mesh_names[i];
                }
            }
            g_state.avatar_preview_debug[handle] = preview_debug.str();
        }
        const auto raw_head_rot = DirectX::XMVectorSet(
            g_state.latest_tracking.head_rot_quat[0],
            g_state.latest_tracking.head_rot_quat[1],
            g_state.latest_tracking.head_rot_quat[2],
            g_state.latest_tracking.head_rot_quat[3]);
        const float quat_len_sq =
            (g_state.latest_tracking.head_rot_quat[0] * g_state.latest_tracking.head_rot_quat[0]) +
            (g_state.latest_tracking.head_rot_quat[1] * g_state.latest_tracking.head_rot_quat[1]) +
            (g_state.latest_tracking.head_rot_quat[2] * g_state.latest_tracking.head_rot_quat[2]) +
            (g_state.latest_tracking.head_rot_quat[3] * g_state.latest_tracking.head_rot_quat[3]);
        const auto head_rot = (quat_len_sq > 1e-6f)
            ? DirectX::XMMatrixRotationQuaternion(DirectX::XMQuaternionNormalize(raw_head_rot))
            : DirectX::XMMatrixIdentity();
        const auto user_pose_rot = ComputeUpperBodyPoseOffsetMatrix();
        const auto composed_head_rot = user_pose_rot * head_rot;
        constexpr float kHeadPosScale = 0.20f;
        const auto head_pos = DirectX::XMMatrixTranslation(
            g_state.latest_tracking.head_pos[0] * kHeadPosScale,
            g_state.latest_tracking.head_pos[1] * kHeadPosScale,
            g_state.latest_tracking.head_pos[2] * kHeadPosScale);
        float avatar_y_lift = 0.0f;
        if (it->second.source_type == AvatarSourceType::Miq && it->second.source_ext == ".vrm") {
            // VRM-origin MIQ can appear visually sunk in bust mode; lift slightly.
            avatar_y_lift = safe_extent_y * 0.12f;
        }
        const auto world =
            DirectX::XMMatrixTranslation(-cx, -focus_y, -cz) *
            DirectX::XMMatrixRotationY(preview_yaw_override) *
            composed_head_rot *
            DirectX::XMMatrixScaling(fit_scale, fit_scale, fit_scale) *
            head_pos *
            DirectX::XMMatrixTranslation(x_offset, -look_at_y + avatar_y_lift, 0.0f);
        ++avatar_slot;
        std::uint32_t material_index_oob_count = 0U;
        std::uint32_t mesh_extent_outlier_skipped_count = 0U;
        std::uint32_t mesh_detached_outlier_skipped_count = 0U;
        std::uint32_t mesh_detached_cluster_skipped_count = 0U;
        std::uint32_t mesh_extreme_detached_cluster_skipped_count = 0U;
        std::uint32_t mesh_vrm_origin_detached_cluster_recentered_count = 0U;
        std::uint32_t mesh_vrm_origin_hair_head_realigned_count = 0U;
        std::uint32_t mesh_bounds_outlier_draw_skipped_count = 0U;
        std::uint32_t bounds_outlier_excluded_count = static_cast<std::uint32_t>(excluded_bounds_mesh_count);
        std::uint32_t preview_hair_candidate_mesh_count = 0U;
        std::uint32_t preview_hair_aligned_mesh_count = 0U;
        std::vector<std::string> detached_mesh_names;
        bool avatar_has_shadow_capable_material = false;
        bool avatar_shadow_draw_enqueued = false;
        const bool is_miq_avatar = it->second.source_type == AvatarSourceType::Miq;
        const bool is_vrm_origin_miq = is_miq_avatar && it->second.source_ext == ".vrm";
        const bool enable_vrm_mesh_recentering = false;
        const bool skip_miq_outlier_draws =
            is_miq_avatar &&
            ResolveMiqOutlierDrawPolicy() == MiqOutlierDrawPolicy::SkipDraw;
        bool head_ref_valid = false;
        float head_ref_x = robust_cx;
        float head_ref_y = robust_cy;
        float head_ref_z = robust_cz;
        if (is_vrm_origin_miq) {
            float acc_x = 0.0f;
            float acc_y = 0.0f;
            float acc_z = 0.0f;
            std::uint32_t acc_n = 0U;
            for (const auto& m : mesh_it->second) {
                std::string name_key = m.mesh_name;
                std::transform(name_key.begin(), name_key.end(), name_key.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                const bool is_head_ref =
                    name_key.find("face") != std::string::npos ||
                    name_key.find("head") != std::string::npos;
                if (!is_head_ref) {
                    continue;
                }
                if (!std::isfinite(m.center.x) || !std::isfinite(m.center.y) || !std::isfinite(m.center.z)) {
                    continue;
                }
                acc_x += m.center.x;
                acc_y += m.center.y;
                acc_z += m.center.z;
                ++acc_n;
            }
            if (acc_n > 0U) {
                const float inv_n = 1.0f / static_cast<float>(acc_n);
                head_ref_x = acc_x * inv_n;
                head_ref_y = acc_y * inv_n;
                head_ref_z = acc_z * inv_n;
                head_ref_valid = true;
            }
        }
        for (std::size_t mesh_index = 0U; mesh_index < mesh_it->second.size(); ++mesh_index) {
            auto& mesh = mesh_it->second[mesh_index];
            if (skip_miq_outlier_draws &&
                mesh_index < preview_bounds_excluded.size() &&
                preview_bounds_excluded[mesh_index] != 0U) {
                ++mesh_bounds_outlier_draw_skipped_count;
                continue;
            }
            const float ex = std::max(mesh.bounds_max.x - mesh.bounds_min.x, 0.0f);
            const float ey = std::max(mesh.bounds_max.y - mesh.bounds_min.y, 0.0f);
            const float ez = std::max(mesh.bounds_max.z - mesh.bounds_min.z, 0.0f);
            const float emax = std::max(ex, std::max(ey, ez));
            const float dcx = mesh.center.x - robust_cx;
            const float dcy = mesh.center.y - robust_cy;
            const float dcz = mesh.center.z - robust_cz;
            const float robust_dist = std::sqrt(dcx * dcx + dcy * dcy + dcz * dcz);
            const float detached_cluster_threshold =
                std::max(0.8f, median_center_dist * 3.0f);
            const float detached_cluster_size_cap =
                std::max(0.5f, median_extent * 2.0f);
            if (is_vrm_origin_miq && head_ref_valid) {
                std::string hair_name_key = mesh.mesh_name;
                std::transform(hair_name_key.begin(), hair_name_key.end(), hair_name_key.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                const bool is_hair_candidate =
                    hair_name_key.find("hair") != std::string::npos ||
                    hair_name_key.find("hairpin") != std::string::npos ||
                    hair_name_key == "front.baked.baked" ||
                    hair_name_key == "back.baked.baked" ||
                    hair_name_key == "side.baked.baked";
                if (is_hair_candidate) {
                    ++preview_hair_candidate_mesh_count;
                    const float hx = mesh.center.x - head_ref_x;
                    const float hy = mesh.center.y - head_ref_y;
                    const float hz = mesh.center.z - head_ref_z;
                    const float head_dist = std::sqrt(hx * hx + hy * hy + hz * hz);
                    const bool hair_aligned =
                        std::isfinite(head_dist) &&
                        std::abs(hy) <= std::max(0.12f, safe_extent_y * 0.16f) &&
                        head_dist <= std::max(0.35f, median_center_dist * 1.6f);
                    if (hair_aligned) {
                        ++preview_hair_aligned_mesh_count;
                    }
                }
            }
            const float extreme_detached_cluster_threshold =
                std::max(2.4f, median_center_dist * 5.5f);
            const float extreme_detached_cluster_size_cap =
                std::max(0.35f, median_extent * 1.1f);
            const bool extreme_detached_cluster =
                is_miq_avatar &&
                !is_vrm_origin_miq &&
                std::isfinite(robust_dist) &&
                robust_dist > extreme_detached_cluster_threshold &&
                std::isfinite(emax) &&
                emax <= extreme_detached_cluster_size_cap;
            if (extreme_detached_cluster) {
                ++mesh_extreme_detached_cluster_skipped_count;
                if (detached_mesh_names.size() < 6U) {
                    detached_mesh_names.push_back(mesh.mesh_name.empty() ? std::to_string(mesh_index) : mesh.mesh_name);
                }
                continue;
            }
            const float vertical_detached_threshold = std::max(0.55f, safe_extent_y * 0.38f);
            const float dy_from_robust = mesh.center.y - robust_cy;
            const bool likely_footwear =
                mesh.mesh_name.find("Boot") != std::string::npos ||
                mesh.mesh_name.find("boot") != std::string::npos ||
                mesh.mesh_name.find("Shoe") != std::string::npos ||
                mesh.mesh_name.find("shoe") != std::string::npos;
            const bool vrm_origin_vertical_detached =
                is_vrm_origin_miq &&
                enable_vrm_mesh_recentering &&
                !likely_footwear &&
                std::isfinite(dy_from_robust) &&
                dy_from_robust > vertical_detached_threshold &&
                std::isfinite(emax) &&
                emax <= std::max(0.35f, median_extent * 1.3f);
            float vrm_detached_recenter_x = 0.0f;
            float vrm_detached_recenter_y = 0.0f;
            float vrm_detached_recenter_z = 0.0f;
            float vrm_hair_align_x = 0.0f;
            float vrm_hair_align_y = 0.0f;
            float vrm_hair_align_z = 0.0f;
            if (vrm_origin_vertical_detached) {
                // Recenter detached upper clusters toward the robust body/head cluster
                // instead of dropping them, to preserve footwear and accessory continuity.
                vrm_detached_recenter_x = -dcx * 0.42f;
                vrm_detached_recenter_y = -(dy_from_robust - vertical_detached_threshold * 0.45f);
                vrm_detached_recenter_z = -dcz * 0.42f;
                const float clamp_xz = std::max(0.05f, safe_extent_x * 0.35f);
                const float clamp_y = std::max(0.08f, safe_extent_y * 0.55f);
                vrm_detached_recenter_x = std::max(-clamp_xz, std::min(clamp_xz, vrm_detached_recenter_x));
                vrm_detached_recenter_y = std::max(-clamp_y, std::min(clamp_y, vrm_detached_recenter_y));
                vrm_detached_recenter_z = std::max(-clamp_xz, std::min(clamp_xz, vrm_detached_recenter_z));
                ++mesh_vrm_origin_detached_cluster_recentered_count;
                if (detached_mesh_names.size() < 6U) {
                    detached_mesh_names.push_back(mesh.mesh_name.empty() ? std::to_string(mesh_index) : mesh.mesh_name);
                }
            }
            if (is_vrm_origin_miq && enable_vrm_mesh_recentering && head_ref_valid) {
                std::string name_key = mesh.mesh_name;
                std::transform(name_key.begin(), name_key.end(), name_key.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                const bool is_hair_candidate =
                    name_key.find("hair") != std::string::npos ||
                    name_key.find("hairpin") != std::string::npos ||
                    name_key == "front.baked.baked" ||
                    name_key == "back.baked.baked" ||
                    name_key == "side.baked.baked";
                if (is_hair_candidate) {
                    const float hx = mesh.center.x - head_ref_x;
                    const float hy = mesh.center.y - head_ref_y;
                    const float hz = mesh.center.z - head_ref_z;
                    const float head_dist = std::sqrt(hx * hx + hy * hy + hz * hz);
                    const bool hair_misaligned =
                        std::isfinite(head_dist) &&
                        (std::abs(hy) > std::max(0.06f, safe_extent_y * 0.08f) ||
                         head_dist > std::max(0.12f, median_center_dist * 0.70f)) &&
                        std::isfinite(emax) &&
                        emax <= std::max(0.9f, median_extent * 2.2f);
                    if (hair_misaligned) {
                        vrm_hair_align_x = -hx * 0.85f;
                        vrm_hair_align_y = -hy * 0.85f;
                        vrm_hair_align_z = -hz * 0.85f;
                        const float clamp_xz = std::max(0.08f, safe_extent_x * 0.40f);
                        const float clamp_y = std::max(0.10f, safe_extent_y * 0.45f);
                        vrm_hair_align_x = std::max(-clamp_xz, std::min(clamp_xz, vrm_hair_align_x));
                        vrm_hair_align_y = std::max(-clamp_y, std::min(clamp_y, vrm_hair_align_y));
                        vrm_hair_align_z = std::max(-clamp_xz, std::min(clamp_xz, vrm_hair_align_z));
                        ++mesh_vrm_origin_hair_head_realigned_count;
                    }
                }
            }
            if (skip_miq_outlier_draws) {
                if (std::isfinite(emax) && emax > draw_extent_threshold) {
                    ++mesh_extent_outlier_skipped_count;
                    continue;
                }
                const float avatar_extent = std::max(safe_extent_x, std::max(safe_extent_y, safe_extent_z));
                if (std::isfinite(robust_dist) &&
                    robust_dist > detached_cluster_threshold &&
                    emax <= detached_cluster_size_cap) {
                    ++mesh_detached_cluster_skipped_count;
                    if (detached_mesh_names.size() < 6U) {
                        detached_mesh_names.push_back(mesh.mesh_name.empty() ? std::to_string(mesh_index) : mesh.mesh_name);
                    }
                    continue;
                }
                const float dx = mesh.center.x - cx;
                const float dy = mesh.center.y - cy;
                const float dz = mesh.center.z - cz;
                const float center_dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                const bool detached_small_piece =
                    std::isfinite(center_dist) &&
                    std::isfinite(avatar_extent) &&
                    avatar_extent > 0.05f &&
                    center_dist > (avatar_extent * 5.5f) &&
                    emax < (avatar_extent * 0.45f);
                if (detached_small_piece) {
                    ++mesh_detached_outlier_skipped_count;
                    if (detached_mesh_names.size() < 6U) {
                        detached_mesh_names.push_back(mesh.mesh_name.empty() ? std::to_string(mesh_index) : mesh.mesh_name);
                    }
                    continue;
                }
            }
            std::size_t material_index = std::numeric_limits<std::size_t>::max();
            if (mesh.material_index >= 0 &&
                static_cast<std::size_t>(mesh.material_index) < material_it->second.size()) {
                material_index = static_cast<std::size_t>(mesh.material_index);
            } else if (mesh_index < material_it->second.size()) {
                material_index = mesh_index;
            } else if (material_it->second.size() == 1U) {
                material_index = 0U;
            }
            if (material_index >= material_it->second.size()) {
                ++material_index_oob_count;
                continue;
            }
            auto* material = &material_it->second[material_index];
            std::string alpha_mode = "OPAQUE";
            if (!material->alpha_mode.empty()) {
                alpha_mode = material->alpha_mode;
            }
            std::transform(alpha_mode.begin(), alpha_mode.end(), alpha_mode.begin(), [](unsigned char c) {
                return static_cast<char>(std::toupper(c));
            });
            DrawItem item;
            item.handle = handle;
            item.mesh_index = mesh_index;
            item.pkg = &it->second;
            item.mesh = &mesh;
            item.material = material;
            item.backend = material != nullptr ? material->backend_selected : RenderFamilyBackendKind::Common;
            auto mesh_world = world;
            if (vrm_origin_vertical_detached) {
                mesh_world =
                    DirectX::XMMatrixTranslation(vrm_detached_recenter_x, vrm_detached_recenter_y, vrm_detached_recenter_z) *
                    mesh_world;
            }
            if (vrm_hair_align_x != 0.0f || vrm_hair_align_y != 0.0f || vrm_hair_align_z != 0.0f) {
                mesh_world =
                    DirectX::XMMatrixTranslation(vrm_hair_align_x, vrm_hair_align_y, vrm_hair_align_z) *
                    mesh_world;
            }
            item.world = mesh_world;
            item.is_mask = (alpha_mode == "MASK");
            item.is_blend = (alpha_mode == "BLEND");
            const auto center = DirectX::XMVectorSet(mesh.center.x, mesh.center.y, mesh.center.z, 1.0f);
            const auto center_view = DirectX::XMVector3TransformCoord(DirectX::XMVector3TransformCoord(center, mesh_world), view);
            item.view_z = DirectX::XMVectorGetZ(center_view);
            FamilyDrawQueues& q = family_draws[backend_index(item.backend)];
            if (!fast_fallback && material != nullptr && material->enable_depth_pass) {
                q.depth_draws.push_back(item);
            }
            if (material != nullptr && material->enable_shadow_pass) {
                avatar_has_shadow_capable_material = true;
            }
            if (!fast_fallback && material != nullptr && material->enable_shadow_pass) {
                q.shadow_draws.push_back(item);
                avatar_shadow_draw_enqueued = true;
            }
            if (material != nullptr && material->enable_base_pass) {
                if (item.is_blend) {
                    q.blend_draws.push_back(item);
                } else if (item.is_mask) {
                    q.mask_draws.push_back(item);
                } else {
                    q.opaque_draws.push_back(item);
                }
            }
            if (!fast_fallback &&
                material != nullptr &&
                material->enable_outline_pass &&
                material->outline_width > 0.0005f) {
                q.outline_draws.push_back(item);
            }
            if (!fast_fallback &&
                material != nullptr &&
                material->enable_emission_pass &&
                material->emission_strength > 0.0001f) {
                q.emission_draws.push_back(item);
            }
        }
        if (material_index_oob_count > 0U) {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                std::ostringstream warning;
                warning << "W_RENDER: MATERIAL_INDEX_OOB_SKIPPED: meshes=" << material_index_oob_count;
                PushAvatarWarningUnique(&avatar_it->second, warning.str(), "MATERIAL_INDEX_OOB_SKIPPED");
            }
        }
        {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                if (lighting.enable_shadow == 0U) {
                    PushAvatarWarningExclusive(
                        &avatar_it->second,
                        "W_RENDER: SHADOW_DISABLED_TOGGLE_OFF: lighting.enable_shadow=0.",
                        "SHADOW_DISABLED_TOGGLE_OFF",
                        {"SHADOW_DISABLED_TOGGLE_OFF", "SHADOW_DISABLED_FAST_FALLBACK", "SHADOW_DISABLED_NO_SHADOW_PASS_MATERIAL", "SHADOW_DISABLED_SHADOW_DRAW_EMPTY"});
                } else if (fast_fallback) {
                    PushAvatarWarningExclusive(
                        &avatar_it->second,
                        "W_RENDER: SHADOW_DISABLED_FAST_FALLBACK: quality profile disables realtime shadow pass.",
                        "SHADOW_DISABLED_FAST_FALLBACK",
                        {"SHADOW_DISABLED_TOGGLE_OFF", "SHADOW_DISABLED_FAST_FALLBACK", "SHADOW_DISABLED_NO_SHADOW_PASS_MATERIAL", "SHADOW_DISABLED_SHADOW_DRAW_EMPTY"});
                } else if (!avatar_has_shadow_capable_material) {
                    PushAvatarWarningExclusive(
                        &avatar_it->second,
                        "W_RENDER: SHADOW_DISABLED_NO_SHADOW_PASS_MATERIAL: no material advertises shadow pass.",
                        "SHADOW_DISABLED_NO_SHADOW_PASS_MATERIAL",
                        {"SHADOW_DISABLED_TOGGLE_OFF", "SHADOW_DISABLED_FAST_FALLBACK", "SHADOW_DISABLED_NO_SHADOW_PASS_MATERIAL", "SHADOW_DISABLED_SHADOW_DRAW_EMPTY"});
                } else if (!avatar_shadow_draw_enqueued) {
                    PushAvatarWarningExclusive(
                        &avatar_it->second,
                        "W_RENDER: SHADOW_DISABLED_SHADOW_DRAW_EMPTY: shadow-capable materials exist but no shadow draw was enqueued.",
                        "SHADOW_DISABLED_SHADOW_DRAW_EMPTY",
                        {"SHADOW_DISABLED_TOGGLE_OFF", "SHADOW_DISABLED_FAST_FALLBACK", "SHADOW_DISABLED_NO_SHADOW_PASS_MATERIAL", "SHADOW_DISABLED_SHADOW_DRAW_EMPTY"});
                }
            }
        }
        if (mesh_extent_outlier_skipped_count > 0U) {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                std::ostringstream warning;
                warning << "W_RENDER: MIQ_MESH_EXTENT_OUTLIER_SKIPPED: meshes=" << mesh_extent_outlier_skipped_count;
                PushAvatarWarningUnique(&avatar_it->second, warning.str(), "MIQ_MESH_EXTENT_OUTLIER_SKIPPED");
            }
        }
        if (mesh_detached_outlier_skipped_count > 0U) {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                std::ostringstream warning;
                warning << "W_RENDER: MIQ_DETACHED_MESH_OUTLIER_SKIPPED: meshes=" << mesh_detached_outlier_skipped_count;
                if (!detached_mesh_names.empty()) {
                    warning << ", names=";
                    for (std::size_t i = 0U; i < detached_mesh_names.size(); ++i) {
                        if (i > 0U) {
                            warning << "|";
                        }
                        warning << detached_mesh_names[i];
                    }
                }
                PushAvatarWarningUnique(&avatar_it->second, warning.str(), "MIQ_DETACHED_MESH_OUTLIER_SKIPPED");
            }
        }
        if (mesh_extreme_detached_cluster_skipped_count > 0U) {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                std::ostringstream warning;
                warning << "W_RENDER: MIQ_EXTREME_DETACHED_CLUSTER_SKIPPED: meshes=" << mesh_extreme_detached_cluster_skipped_count;
                if (!detached_mesh_names.empty()) {
                    warning << ", names=";
                    for (std::size_t i = 0U; i < detached_mesh_names.size(); ++i) {
                        if (i > 0U) {
                            warning << "|";
                        }
                        warning << detached_mesh_names[i];
                    }
                }
                PushAvatarWarningUnique(&avatar_it->second, warning.str(), "MIQ_EXTREME_DETACHED_CLUSTER_SKIPPED");
            }
        }
        if (mesh_vrm_origin_detached_cluster_recentered_count > 0U) {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                std::ostringstream warning;
                warning << "W_RENDER: MIQ_VRM_ORIGIN_DETACHED_CLUSTER_RECENTERED: mode=vertical_only, meshes="
                        << mesh_vrm_origin_detached_cluster_recentered_count;
                if (!detached_mesh_names.empty()) {
                    warning << ", names=";
                    for (std::size_t i = 0U; i < detached_mesh_names.size(); ++i) {
                        if (i > 0U) {
                            warning << "|";
                        }
                        warning << detached_mesh_names[i];
                    }
                }
                PushAvatarWarningUnique(&avatar_it->second, warning.str(), "MIQ_VRM_ORIGIN_DETACHED_CLUSTER_RECENTERED");
            }
        }
        if (mesh_vrm_origin_hair_head_realigned_count > 0U) {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                std::ostringstream warning;
                warning << "W_RENDER: MIQ_VRM_HAIR_HEAD_REALIGNED: meshes=" << mesh_vrm_origin_hair_head_realigned_count;
                PushAvatarWarningUnique(&avatar_it->second, warning.str(), "MIQ_VRM_HAIR_HEAD_REALIGNED");
            }
        }
        if (mesh_detached_cluster_skipped_count > 0U) {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                std::ostringstream warning;
                warning << "W_RENDER: MIQ_DETACHED_CLUSTER_SKIPPED: meshes=" << mesh_detached_cluster_skipped_count;
                if (!detached_mesh_names.empty()) {
                    warning << ", names=";
                    for (std::size_t i = 0U; i < detached_mesh_names.size(); ++i) {
                        if (i > 0U) {
                            warning << "|";
                        }
                        warning << detached_mesh_names[i];
                    }
                }
                PushAvatarWarningUnique(&avatar_it->second, warning.str(), "MIQ_DETACHED_CLUSTER_SKIPPED");
            }
        }
        if (bounds_outlier_excluded_count > 0U) {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                std::ostringstream warning;
                warning << "W_RENDER: MIQ_BOUNDS_OUTLIER_EXCLUDED: autofit_meshes=" << bounds_outlier_excluded_count;
                if (!excluded_mesh_names.empty()) {
                    warning << ", names=";
                    for (std::size_t i = 0U; i < excluded_mesh_names.size(); ++i) {
                        if (i > 0U) {
                            warning << "|";
                        }
                        warning << excluded_mesh_names[i];
                    }
                }
                PushAvatarWarningUnique(&avatar_it->second, warning.str(), "MIQ_BOUNDS_OUTLIER_EXCLUDED");
            }
        }
        if (mesh_bounds_outlier_draw_skipped_count > 0U) {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                std::ostringstream warning;
                warning << "W_RENDER: MIQ_BOUNDS_OUTLIER_DRAW_SKIPPED: meshes=" << mesh_bounds_outlier_draw_skipped_count;
                PushAvatarWarningUnique(&avatar_it->second, warning.str(), "MIQ_BOUNDS_OUTLIER_DRAW_SKIPPED");
            }
        }
        if (mesh_extreme_detached_cluster_skipped_count > 0U) {
            auto preview_it = g_state.avatar_preview_debug.find(handle);
            if (preview_it != g_state.avatar_preview_debug.end() && !preview_it->second.empty()) {
                preview_it->second += ", extreme_detached_skipped=" + std::to_string(mesh_extreme_detached_cluster_skipped_count);
            }
        }
        if (mesh_vrm_origin_detached_cluster_recentered_count > 0U) {
            auto preview_it = g_state.avatar_preview_debug.find(handle);
            if (preview_it != g_state.avatar_preview_debug.end() && !preview_it->second.empty()) {
                preview_it->second += ", vrm_origin_detached_recentered=" + std::to_string(mesh_vrm_origin_detached_cluster_recentered_count);
            }
        }
        if (mesh_vrm_origin_hair_head_realigned_count > 0U) {
            auto preview_it = g_state.avatar_preview_debug.find(handle);
            if (preview_it != g_state.avatar_preview_debug.end() && !preview_it->second.empty()) {
                preview_it->second += ", vrm_hair_head_realigned=" + std::to_string(mesh_vrm_origin_hair_head_realigned_count);
            }
        }
        float hair_alignment_score = -1.0f;
        if (preview_hair_candidate_mesh_count > 0U) {
            hair_alignment_score = static_cast<float>(preview_hair_aligned_mesh_count) /
                static_cast<float>(preview_hair_candidate_mesh_count);
        }
        g_state.avatar_preview_orientation_metrics[handle] = AvatarPreviewOrientationMetrics {
            std::max(-180, std::min(180, static_cast<int>(it->second.recommended_preview_yaw_deg))),
            TransformConfidenceLevel(it->second.transform_confidence),
            is_vrm_origin_miq ? 1U : 0U,
            bounds_outlier_excluded_count,
            preview_hair_candidate_mesh_count,
            hair_alignment_score,
        };
    }
    for (auto& family_q : family_draws) {
        std::sort(family_q.blend_draws.begin(), family_q.blend_draws.end(), [](const DrawItem& a, const DrawItem& b) {
            const float dz = std::abs(a.view_z - b.view_z);
            if (dz > 1e-4f) {
                return a.view_z > b.view_z;
            }
            return a.mesh_index < b.mesh_index;
        });
    }

    struct alignas(16) SceneConstants {
        float world_view_proj[16];
        float world_matrix[16];
        float light_view_proj[16];
        float base_color[4];
        float shade_color[4];
        float emission_color[4];
        float rim_color[4];
        float matcap_color[4];
        float lighting_params[4];
        float shadow_params[4];
        float liltoon_mix[4];
        float liltoon_params[4];
        float liltoon_aux[4];
        float alpha_misc[4];
        float outline_params[4];
        float uv_anim_params[4];
        float time_params[4];
    };
    enum class RenderPassKind {
        DepthOnly,
        ShadowCaster,
        Base,
        Outline,
        Emission
    };
    bool rendering_shadow_map = false;
    auto draw_pass = [&](const DrawItem& item, RenderPassKind pass_kind, RenderFamilyBackendKind backend_kind) {
        if (item.mesh == nullptr || item.mesh->vertex_buffer == nullptr || item.mesh->index_buffer == nullptr || item.pkg == nullptr) {
            return;
        }
        std::string alpha_mode = "OPAQUE";
        float alpha_cutoff = 0.5f;
        bool double_sided = false;
        ID3D11ShaderResourceView* base_srv = nullptr;
        ID3D11ShaderResourceView* normal_srv = nullptr;
        ID3D11ShaderResourceView* rim_srv = nullptr;
        ID3D11ShaderResourceView* emission_srv = nullptr;
        ID3D11ShaderResourceView* matcap_srv = nullptr;
        ID3D11ShaderResourceView* uv_mask_srv = nullptr;
        if (item.material != nullptr) {
            if (!item.material->alpha_mode.empty()) {
                alpha_mode = item.material->alpha_mode;
            }
            alpha_cutoff = item.material->alpha_cutoff;
            double_sided = item.material->double_sided;
            base_srv = item.material->base_color_srv;
            normal_srv = item.material->normal_srv;
            rim_srv = item.material->rim_srv;
            emission_srv = item.material->emission_srv;
            matcap_srv = item.material->matcap_srv;
            uv_mask_srv = item.material->uv_anim_mask_srv;
        }
        std::transform(alpha_mode.begin(), alpha_mode.end(), alpha_mode.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        const bool depth_only_pass = pass_kind == RenderPassKind::DepthOnly;
        const bool shadow_pass = pass_kind == RenderPassKind::ShadowCaster;
        const bool outline_pass = pass_kind == RenderPassKind::Outline;
        const bool emission_pass = pass_kind == RenderPassKind::Emission;
        const bool is_mask = (alpha_mode == "MASK");
        const bool is_blend = (alpha_mode == "BLEND");
        // VRM/MIQ avatars use CULL_NONE because winding conventions are not
        // uniformly CCW across all mesh components in practice: some VRM
        // exporters emit CW-wound face/head meshes while clothing is CCW.
        // Per-material double_sided flag is respected on top of this.
        const bool force_no_cull_for_avatar =
            (item.pkg != nullptr &&
                (item.pkg->source_type == AvatarSourceType::Miq ||
                 item.pkg->source_type == AvatarSourceType::Vrm));
        ID3D11PixelShader* active_ps = renderer.pixel_shader_common;
        if (backend_kind == RenderFamilyBackendKind::Liltoon && renderer.pixel_shader_liltoon != nullptr) {
            active_ps = renderer.pixel_shader_liltoon;
        } else if (backend_kind == RenderFamilyBackendKind::Mtoon && renderer.pixel_shader_mtoon != nullptr) {
            active_ps = renderer.pixel_shader_mtoon;
        } else if (backend_kind == RenderFamilyBackendKind::Poiyomi && renderer.pixel_shader_poiyomi != nullptr) {
            active_ps = renderer.pixel_shader_poiyomi;
        } else if (backend_kind == RenderFamilyBackendKind::Standard && renderer.pixel_shader_standard != nullptr) {
            active_ps = renderer.pixel_shader_standard;
        }
        device_ctx->PSSetShader(active_ps, nullptr, 0U);
        const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        if (depth_only_pass || shadow_pass) {
            device_ctx->OMSetBlendState(renderer.blend_depth_only, blend_factor, 0xFFFFFFFFU);
            device_ctx->OMSetDepthStencilState(renderer.depth_write, 0U);
        } else if (emission_pass) {
            device_ctx->OMSetBlendState(renderer.blend_additive, blend_factor, 0xFFFFFFFFU);
            device_ctx->OMSetDepthStencilState(renderer.depth_read, 0U);
        } else if (is_blend) {
            device_ctx->OMSetBlendState(renderer.blend_alpha, blend_factor, 0xFFFFFFFFU);
            device_ctx->OMSetDepthStencilState(renderer.depth_read, 0U);
        } else if (is_mask) {
            device_ctx->OMSetBlendState(renderer.blend_opaque, blend_factor, 0xFFFFFFFFU);
            device_ctx->OMSetDepthStencilState(renderer.depth_write, 0U);
        } else {
            device_ctx->OMSetBlendState(renderer.blend_opaque, blend_factor, 0xFFFFFFFFU);
            device_ctx->OMSetDepthStencilState(renderer.depth_write, 0U);
        }
        if (outline_pass) {
            device_ctx->OMSetBlendState(renderer.blend_opaque, blend_factor, 0xFFFFFFFFU);
            device_ctx->OMSetDepthStencilState(renderer.depth_read, 0U);
            if (double_sided) {
                device_ctx->RSSetState(renderer.raster_cull_none);
            } else if (force_no_cull_for_avatar) {
                device_ctx->RSSetState(renderer.raster_cull_front_ccw);
            } else {
                device_ctx->RSSetState(renderer.raster_cull_front);
            }
        } else if (shadow_pass) {
            if (double_sided) {
                device_ctx->RSSetState(renderer.raster_cull_none);
            } else if (force_no_cull_for_avatar) {
                device_ctx->RSSetState(renderer.raster_cull_back_ccw);
            } else {
                device_ctx->RSSetState(renderer.raster_cull_back);
            }
        } else {
            if (double_sided) {
                device_ctx->RSSetState(renderer.raster_cull_none);
            } else if (force_no_cull_for_avatar) {
                device_ctx->RSSetState(renderer.raster_cull_back_ccw);
            } else {
                device_ctx->RSSetState(renderer.raster_cull_back);
            }
        }

        const UINT stride = item.mesh->vertex_stride;
        const UINT offset = 0U;
        device_ctx->IASetVertexBuffers(0U, 1U, &item.mesh->vertex_buffer, &stride, &offset);
        device_ctx->IASetIndexBuffer(item.mesh->index_buffer, DXGI_FORMAT_R32_UINT, 0U);
        ID3D11SamplerState* samplers[2] = {renderer.linear_sampler, renderer.shadow_sampler};
        device_ctx->PSSetSamplers(0U, 2U, samplers);
        ID3D11ShaderResourceView* shadow_srv = rendering_shadow_map ? nullptr : renderer.shadow_srv;
        ID3D11ShaderResourceView* srvs[7] = {base_srv, normal_srv, rim_srv, emission_srv, matcap_srv, uv_mask_srv, shadow_srv};
        device_ctx->PSSetShaderResources(0U, 7U, srvs);

        const auto world_view_proj = item.world * ((rendering_shadow_map && shadow_pass) ? light_view_proj : (view * proj));
        const auto world_view_proj_t = DirectX::XMMatrixTranspose(world_view_proj);
        const auto world_matrix_t = DirectX::XMMatrixTranspose(item.world);
        const auto light_view_proj_t = DirectX::XMMatrixTranspose(light_view_proj);
        SceneConstants cb {};
        DirectX::XMFLOAT4X4 wvp_store {};
        DirectX::XMFLOAT4X4 world_store {};
        DirectX::XMFLOAT4X4 light_wvp_store {};
        DirectX::XMStoreFloat4x4(&wvp_store, world_view_proj_t);
        DirectX::XMStoreFloat4x4(&world_store, world_matrix_t);
        DirectX::XMStoreFloat4x4(&light_wvp_store, light_view_proj_t);
        std::memcpy(cb.world_view_proj, &wvp_store, sizeof(cb.world_view_proj));
        std::memcpy(cb.world_matrix, &world_store, sizeof(cb.world_matrix));
        std::memcpy(cb.light_view_proj, &light_wvp_store, sizeof(cb.light_view_proj));
        cb.lighting_params[0] = shading_light_dir3.x;
        cb.lighting_params[1] = shading_light_dir3.y;
        cb.lighting_params[2] = shading_light_dir3.z;
        cb.lighting_params[3] = std::max(0.0f, std::min(1.0f, lighting.ambient_intensity));
        cb.shadow_params[0] = lighting.shadow_strength;
        cb.shadow_params[1] = (shadow_runtime_enabled && !depth_only_pass && !shadow_pass && !outline_pass && !emission_pass) ? 1.0f : 0.0f;
        cb.shadow_params[2] = lighting.shadow_bias * 0.001f;
        cb.shadow_params[3] = light_intensity_scale;
        if (item.material != nullptr) {
            cb.base_color[0] = item.material->base_color[0];
            cb.base_color[1] = item.material->base_color[1];
            cb.base_color[2] = item.material->base_color[2];
            cb.base_color[3] = item.material->base_color[3];
            cb.shade_color[0] = item.material->shade_color[0];
            cb.shade_color[1] = item.material->shade_color[1];
            cb.shade_color[2] = item.material->shade_color[2];
            cb.shade_color[3] = item.material->shade_color[3];
            cb.emission_color[0] = item.material->emission_color[0];
            cb.emission_color[1] = item.material->emission_color[1];
            cb.emission_color[2] = item.material->emission_color[2];
            cb.emission_color[3] = item.material->emission_color[3];
            cb.rim_color[0] = item.material->rim_color[0];
            cb.rim_color[1] = item.material->rim_color[1];
            cb.rim_color[2] = item.material->rim_color[2];
            cb.rim_color[3] = item.material->rim_color[3];
            cb.matcap_color[0] = item.material->matcap_color[0];
            cb.matcap_color[1] = item.material->matcap_color[1];
            cb.matcap_color[2] = item.material->matcap_color[2];
            cb.matcap_color[3] = item.material->matcap_color[3];
            cb.liltoon_mix[0] = item.material->shade_mix;
            cb.liltoon_mix[1] = item.material->emission_strength;
            cb.liltoon_mix[2] = item.material->normal_strength;
            cb.liltoon_mix[3] = item.material->rim_strength;
            cb.liltoon_params[0] = item.material->rim_power;
            cb.liltoon_params[1] = normal_srv != nullptr ? 1.0f : 0.0f;
            cb.liltoon_params[2] = rim_srv != nullptr ? 1.0f : 0.0f;
            cb.liltoon_params[3] = emission_srv != nullptr ? 1.0f : 0.0f;
            cb.liltoon_aux[0] = matcap_srv != nullptr ? 1.0f : 0.0f;
            cb.liltoon_aux[1] = item.material->matcap_strength;
            cb.liltoon_aux[2] = uv_mask_srv != nullptr ? 1.0f : 0.0f;
            cb.liltoon_aux[3] = 0.0f;
            cb.outline_params[0] = item.material->outline_width * 0.02f;
            cb.outline_params[1] = item.material->outline_lighting_mix;
            cb.outline_params[2] = emission_pass ? 1.0f : 0.0f;
            cb.outline_params[3] = outline_pass ? 1.0f : 0.0f;
            cb.uv_anim_params[0] = item.material->uv_anim_scroll_x;
            cb.uv_anim_params[1] = item.material->uv_anim_scroll_y;
            cb.uv_anim_params[2] = item.material->uv_anim_rotation;
            cb.uv_anim_params[3] = item.material->uv_anim_enabled ? 1.0f : 0.0f;
            cb.time_params[0] = g_state.runtime_time_seconds;
            cb.time_params[1] = frame_dt;
            cb.time_params[2] = 0.0f;
            cb.time_params[3] = 0.0f;
            if (fast_fallback) {
                cb.liltoon_mix[0] = std::min(cb.liltoon_mix[0], 0.12f);
                cb.liltoon_mix[1] = 0.0f;
                cb.liltoon_mix[2] = 0.0f;
                cb.liltoon_mix[3] = 0.0f;
                cb.liltoon_params[1] = 0.0f;
                cb.liltoon_params[2] = 0.0f;
                cb.liltoon_params[3] = 0.0f;
                cb.liltoon_aux[0] = 0.0f;
                cb.liltoon_aux[1] = 0.0f;
                cb.liltoon_aux[2] = 0.0f;
                cb.outline_params[0] = 0.0f;
                cb.outline_params[1] = 0.0f;
                cb.outline_params[3] = 0.0f;
                cb.uv_anim_params[0] = 0.0f;
                cb.uv_anim_params[1] = 0.0f;
                cb.uv_anim_params[2] = 0.0f;
                cb.uv_anim_params[3] = 0.0f;
            }
        } else {
            cb.base_color[0] = 1.0f;
            cb.base_color[1] = 1.0f;
            cb.base_color[2] = 1.0f;
            cb.base_color[3] = 1.0f;
            cb.shade_color[0] = 1.0f;
            cb.shade_color[1] = 1.0f;
            cb.shade_color[2] = 1.0f;
            cb.shade_color[3] = 1.0f;
            cb.emission_color[0] = 0.0f;
            cb.emission_color[1] = 0.0f;
            cb.emission_color[2] = 0.0f;
            cb.emission_color[3] = 1.0f;
            cb.rim_color[0] = 0.0f;
            cb.rim_color[1] = 0.0f;
            cb.rim_color[2] = 0.0f;
            cb.rim_color[3] = 1.0f;
            cb.matcap_color[0] = 0.0f;
            cb.matcap_color[1] = 0.0f;
            cb.matcap_color[2] = 0.0f;
            cb.matcap_color[3] = 1.0f;
            cb.liltoon_mix[0] = 0.0f;
            cb.liltoon_mix[1] = 0.0f;
            cb.liltoon_mix[2] = 0.0f;
            cb.liltoon_mix[3] = 0.0f;
            cb.liltoon_params[0] = 2.0f;
            cb.liltoon_params[1] = 0.0f;
            cb.liltoon_params[2] = 0.0f;
            cb.liltoon_params[3] = 0.0f;
            cb.liltoon_aux[0] = 0.0f;
            cb.liltoon_aux[1] = 0.0f;
            cb.liltoon_aux[2] = 0.0f;
            cb.liltoon_aux[3] = 0.0f;
            cb.outline_params[0] = 0.0f;
            cb.outline_params[1] = 0.0f;
            cb.outline_params[2] = emission_pass ? 1.0f : 0.0f;
            cb.outline_params[3] = 0.0f;
            cb.uv_anim_params[0] = 0.0f;
            cb.uv_anim_params[1] = 0.0f;
            cb.uv_anim_params[2] = 0.0f;
            cb.uv_anim_params[3] = 0.0f;
            cb.time_params[0] = g_state.runtime_time_seconds;
            cb.time_params[1] = frame_dt;
            cb.time_params[2] = 0.0f;
            cb.time_params[3] = 0.0f;
        }
        cb.alpha_misc[0] = (is_mask && !emission_pass) ? alpha_cutoff : 0.0f;
        cb.alpha_misc[1] = (is_mask && !emission_pass) ? 1.0f : 0.0f;
        cb.alpha_misc[2] = base_srv != nullptr ? 1.0f : 0.0f;
        cb.alpha_misc[3] = ((is_mask || is_blend) && !emission_pass) ? 1.0f : 0.0f;
        if (rendering_shadow_map && shadow_pass) {
            cb.shadow_params[1] = 0.0f;
        }

        D3D11_MAPPED_SUBRESOURCE mapped {};
        if (SUCCEEDED(device_ctx->Map(renderer.constant_buffer, 0U, D3D11_MAP_WRITE_DISCARD, 0U, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            device_ctx->Unmap(renderer.constant_buffer, 0U);
        }
        device_ctx->DrawIndexed(item.mesh->index_count, 0U, 0);
        ID3D11ShaderResourceView* null_srvs[7] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
        device_ctx->PSSetShaderResources(0U, 7U, null_srvs);
        ++frame_draw_calls;
    };
    std::uint32_t frame_pass_count = 0U;
    std::uint32_t depth_pass_count = 0U;
    std::uint32_t shadow_pass_count = 0U;
    std::uint32_t base_pass_count = 0U;
    std::uint32_t outline_pass_count = 0U;
    std::uint32_t emission_pass_count = 0U;
    std::uint32_t blend_pass_count = 0U;
    for (const auto& family_q : family_draws) {
        depth_pass_count += family_q.depth_draws.empty() ? 0U : 1U;
        shadow_pass_count += family_q.shadow_draws.empty() ? 0U : 1U;
        base_pass_count += (!family_q.opaque_draws.empty() || !family_q.mask_draws.empty()) ? 1U : 0U;
        outline_pass_count += family_q.outline_draws.empty() ? 0U : 1U;
        emission_pass_count += family_q.emission_draws.empty() ? 0U : 1U;
        blend_pass_count += family_q.blend_draws.empty() ? 0U : 1U;
    }
    frame_pass_count =
        depth_pass_count +
        shadow_pass_count +
        base_pass_count +
        outline_pass_count +
        emission_pass_count +
        blend_pass_count;
    const std::array<RenderFamilyBackendKind, 5U> backend_order = {
        RenderFamilyBackendKind::Common,
        RenderFamilyBackendKind::Liltoon,
        RenderFamilyBackendKind::Mtoon,
        RenderFamilyBackendKind::Poiyomi,
        RenderFamilyBackendKind::Standard};
    if (shadow_runtime_enabled && renderer.shadow_dsv != nullptr && renderer.shadow_resolution > 0U) {
        D3D11_VIEWPORT shadow_viewport {};
        shadow_viewport.TopLeftX = 0.0f;
        shadow_viewport.TopLeftY = 0.0f;
        shadow_viewport.Width = static_cast<float>(renderer.shadow_resolution);
        shadow_viewport.Height = static_cast<float>(renderer.shadow_resolution);
        shadow_viewport.MinDepth = 0.0f;
        shadow_viewport.MaxDepth = 1.0f;
        ID3D11RenderTargetView* null_rtv = nullptr;
        device_ctx->OMSetRenderTargets(0U, &null_rtv, renderer.shadow_dsv);
        device_ctx->ClearDepthStencilView(renderer.shadow_dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0U);
        device_ctx->RSSetViewports(1U, &shadow_viewport);
        rendering_shadow_map = true;
        for (const auto backend_kind : backend_order) {
            const auto& q = family_draws[backend_index(backend_kind)];
            for (const auto& item : q.shadow_draws) {
                draw_pass(item, RenderPassKind::ShadowCaster, backend_kind);
            }
        }
        rendering_shadow_map = false;
        device_ctx->OMSetRenderTargets(1U, &rtv, renderer.depth_dsv);
        device_ctx->RSSetViewports(1U, &viewport);
    }
    for (const auto backend_kind : backend_order) {
        const auto& q = family_draws[backend_index(backend_kind)];
        for (const auto& item : q.depth_draws) {
            draw_pass(item, RenderPassKind::DepthOnly, backend_kind);
        }
        if (!shadow_runtime_enabled) {
            for (const auto& item : q.shadow_draws) {
                draw_pass(item, RenderPassKind::ShadowCaster, backend_kind);
            }
        }
        for (const auto& item : q.opaque_draws) {
            draw_pass(item, RenderPassKind::Base, backend_kind);
        }
        for (const auto& item : q.mask_draws) {
            draw_pass(item, RenderPassKind::Base, backend_kind);
        }
        for (const auto& item : q.outline_draws) {
            draw_pass(item, RenderPassKind::Outline, backend_kind);
        }
        for (const auto& item : q.emission_draws) {
            draw_pass(item, RenderPassKind::Emission, backend_kind);
        }
        for (const auto& item : q.blend_draws) {
            draw_pass(item, RenderPassKind::Base, backend_kind);
        }
    }

    g_state.last_depth_pass_count = depth_pass_count;
    g_state.last_shadow_pass_count = shadow_pass_count;
    g_state.last_base_pass_count = base_pass_count;
    g_state.last_outline_pass_count = outline_pass_count;
    g_state.last_emission_pass_count = emission_pass_count;
    g_state.last_blend_pass_count = blend_pass_count;

    if (g_state.spout.IsActive()) {
        bool submitted_on_gpu = false;
        if (g_state.spout.WantsGpuTextureSubmit()) {
            ID3D11Resource* resource = nullptr;
            rtv->GetResource(&resource);
            if (resource != nullptr) {
                submitted_on_gpu = g_state.spout.SubmitFrameTexture(device, resource);
                resource->Release();
            }
        }

        if (!submitted_on_gpu) {
            if (g_state.spout.IsStrictMode()) {
                SetError(
                    NC_ERROR_INTERNAL,
                    "spout",
                    "strict spout mode enabled but gpu submit failed (" + g_state.spout.LastErrorCode() + ")",
                    true);
                return NC_ERROR_INTERNAL;
            }
            std::vector<std::uint8_t> pixels;
            if (CaptureRtvBgra(device, device_ctx, rtv, ctx->width, ctx->height, &pixels)) {
                g_state.spout.SubmitFrame(pixels.data(), static_cast<std::uint32_t>(pixels.size()));
            }
        }
    }
#endif

    PublishTrackingFrame();
    for (const auto handle : g_state.render_ready_avatars) {
        auto it = g_state.avatars.find(handle);
        if (it == g_state.avatars.end()) {
            continue;
        }
        it->second.last_render_draw_calls = frame_draw_calls;
    }
    if (frame_draw_calls == 0U) {
        SetError(NC_ERROR_UNSUPPORTED, "render", "render-ready avatars produced zero draw calls", true);
        return NC_ERROR_UNSUPPORTED;
    }

    const auto frame_end = std::chrono::steady_clock::now();
    const float frame_ms = static_cast<float>(
        std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_begin).count()) /
        1000.0f;
    g_state.last_frame_ms = frame_ms;
    g_state.last_cpu_frame_ms = frame_ms;
    g_state.last_gpu_frame_ms = frame_ms;
    g_state.last_material_resolve_ms = static_cast<float>(material_resolve_ms);
    g_state.last_pass_count = frame_pass_count;

    ClearError();
    return NC_OK;
}

}  // namespace

}  // namespace animiq::nativecore

NcResultCode nc_initialize(const NcInitOptions* options) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);

    if (options == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "runtime", "options must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    animiq::nativecore::g_state.initialized = true;
    animiq::nativecore::g_state.next_avatar_handle = 1;
    animiq::nativecore::g_state.avatars.clear();
    animiq::nativecore::g_state.secondary_motion_states.clear();
    animiq::nativecore::g_state.arm_pose_states.clear();
    animiq::nativecore::g_state.arm_pose_auto_rollback_handles.clear();
    animiq::nativecore::g_state.render_ready_avatars.clear();
    animiq::nativecore::g_state.render_quality = animiq::nativecore::MakeDefaultRenderQualityOptions();
    animiq::nativecore::g_state.lighting_options = animiq::nativecore::MakeDefaultLightingOptions();
    animiq::nativecore::g_state.pose_offsets = animiq::nativecore::MakeDefaultPoseOffsets();
    animiq::nativecore::g_state.tracking_weights_dirty = false;
    animiq::nativecore::g_state.last_frame_ms = 0.0f;
    animiq::nativecore::g_state.last_gpu_frame_ms = 0.0f;
    animiq::nativecore::g_state.last_cpu_frame_ms = 0.0f;
    animiq::nativecore::g_state.last_material_resolve_ms = 0.0f;
    animiq::nativecore::g_state.last_pass_count = 0U;
    animiq::nativecore::g_state.last_depth_pass_count = 0U;
    animiq::nativecore::g_state.last_shadow_pass_count = 0U;
    animiq::nativecore::g_state.last_base_pass_count = 0U;
    animiq::nativecore::g_state.last_outline_pass_count = 0U;
    animiq::nativecore::g_state.last_emission_pass_count = 0U;
    animiq::nativecore::g_state.last_blend_pass_count = 0U;
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_shutdown(void) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);

    if (!animiq::nativecore::g_state.initialized) {
        return NC_OK;
    }

    animiq::nativecore::g_state.spout.Stop();
    animiq::nativecore::g_state.osc.Close();
#if defined(_WIN32)
    for (auto& [_, state] : animiq::nativecore::g_state.window_targets) {
        animiq::nativecore::ReleaseWindowState(&state);
    }
    animiq::nativecore::g_state.window_targets.clear();
    animiq::nativecore::ResetRendererResources(&animiq::nativecore::g_state.renderer);
#endif
    animiq::nativecore::g_state.avatars.clear();
    animiq::nativecore::g_state.secondary_motion_states.clear();
    animiq::nativecore::g_state.arm_pose_states.clear();
    animiq::nativecore::g_state.arm_pose_auto_rollback_handles.clear();
    animiq::nativecore::g_state.render_ready_avatars.clear();
    animiq::nativecore::g_state.render_quality = animiq::nativecore::MakeDefaultRenderQualityOptions();
    animiq::nativecore::g_state.lighting_options = animiq::nativecore::MakeDefaultLightingOptions();
    animiq::nativecore::g_state.pose_offsets = animiq::nativecore::MakeDefaultPoseOffsets();
    animiq::nativecore::g_state.tracking_weights_dirty = false;
    animiq::nativecore::g_state.last_depth_pass_count = 0U;
    animiq::nativecore::g_state.last_shadow_pass_count = 0U;
    animiq::nativecore::g_state.last_base_pass_count = 0U;
    animiq::nativecore::g_state.last_outline_pass_count = 0U;
    animiq::nativecore::g_state.last_emission_pass_count = 0U;
    animiq::nativecore::g_state.last_blend_pass_count = 0U;
    animiq::nativecore::g_state.initialized = false;
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_load_avatar(const NcAvatarLoadRequest* request, NcAvatarHandle* out_handle, NcAvatarInfo* out_info) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (request == nullptr || out_handle == nullptr || request->path == nullptr || request->path[0] == '\0') {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "request/path/out_handle must be valid", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    animiq::avatar::AvatarLoadOptions options;
    if (request->format_hint == NC_AVATAR_FORMAT_VRM) options.forced_source_type = animiq::avatar::AvatarSourceType::Vrm;
    else if (request->format_hint == NC_AVATAR_FORMAT_MIQ) options.forced_source_type = animiq::avatar::AvatarSourceType::Miq;
    else if (request->format_hint == NC_AVATAR_FORMAT_VSFAVATAR) options.forced_source_type = animiq::avatar::AvatarSourceType::VsfAvatar;

    if (request->format_hint == NC_AVATAR_FORMAT_MIQ) {
        options.miq_unknown_section_policy = static_cast<animiq::avatar::MiqUnknownSectionPolicy>(request->fallback_policy);
    }

    auto loaded = animiq::nativecore::g_state.loader.Load(request->path, options);
    if (!loaded.ok) {
        animiq::nativecore::SetError(NC_ERROR_IO, "avatar", loaded.error, true);
        return NC_ERROR_IO;
    }
    animiq::nativecore::BuildArkit52ExpressionBindings(&loaded.value);
    const bool needs_expression_fallback = loaded.value.expressions.empty() &&
        (loaded.value.source_type == animiq::avatar::AvatarSourceType::Vrm ||
         loaded.value.source_type == animiq::avatar::AvatarSourceType::Miq);
    if (needs_expression_fallback) {
        loaded.value.expressions.push_back({"blink", "blink", 0.0f, 0.0f});
        loaded.value.expressions.push_back({"aa", "viseme_aa", 0.0f, 0.0f});
        loaded.value.expressions.push_back({"joy", "joy", 0.0f, 0.0f});
        if (loaded.value.source_type == animiq::avatar::AvatarSourceType::Vrm) {
            loaded.value.warnings.push_back("W_VRM_EXPRESSION_FALLBACK: runtime injected blink/aa/joy expression defaults");
            loaded.value.warning_codes.push_back("VRM_EXPRESSION_FALLBACK_APPLIED");
        } else {
            loaded.value.warnings.push_back("W_MIQ_EXPRESSION_FALLBACK: runtime injected blink/aa/joy expression defaults");
            loaded.value.warning_codes.push_back("MIQ_EXPRESSION_FALLBACK_APPLIED");
        }
    }

    const std::uint64_t handle = animiq::nativecore::g_state.next_avatar_handle++;
    animiq::nativecore::g_state.avatars[handle] = loaded.value;
    animiq::nativecore::g_state.avatar_preview_debug.erase(handle);
    animiq::nativecore::g_state.avatar_preview_orientation_metrics.erase(handle);
    animiq::nativecore::g_state.secondary_motion_states.erase(handle);
    animiq::nativecore::g_state.arm_pose_states.erase(handle);
    animiq::nativecore::g_state.arm_pose_auto_rollback_handles.erase(handle);
    *out_handle = handle;
    animiq::nativecore::FillAvatarInfo(loaded.value, handle, out_info);
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_unload_avatar(NcAvatarHandle handle) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }

    auto it = animiq::nativecore::g_state.avatars.find(handle);
    if (it == animiq::nativecore::g_state.avatars.end()) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    animiq::nativecore::g_state.render_ready_avatars.erase(handle);
    animiq::nativecore::g_state.avatar_preview_debug.erase(handle);
    animiq::nativecore::g_state.avatar_preview_orientation_metrics.erase(handle);
    animiq::nativecore::g_state.secondary_motion_states.erase(handle);
    animiq::nativecore::g_state.arm_pose_states.erase(handle);
    animiq::nativecore::g_state.arm_pose_auto_rollback_handles.erase(handle);
#if defined(_WIN32)
    auto mesh_it = animiq::nativecore::g_state.renderer.avatar_meshes.find(handle);
    if (mesh_it != animiq::nativecore::g_state.renderer.avatar_meshes.end()) {
        for (auto& mesh : mesh_it->second) {
            animiq::nativecore::ReleaseGpuMeshResource(&mesh);
        }
        animiq::nativecore::g_state.renderer.avatar_meshes.erase(mesh_it);
    }
    auto material_it = animiq::nativecore::g_state.renderer.avatar_materials.find(handle);
    if (material_it != animiq::nativecore::g_state.renderer.avatar_materials.end()) {
        for (auto& material : material_it->second) {
            animiq::nativecore::ReleaseGpuMaterialResource(&material);
        }
        animiq::nativecore::g_state.renderer.avatar_materials.erase(material_it);
    }
#endif
    animiq::nativecore::g_state.avatars.erase(it);
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_get_avatar_info(NcAvatarHandle handle, NcAvatarInfo* out_info) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (out_info == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "out_info must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    auto it = animiq::nativecore::g_state.avatars.find(handle);
    if (it == animiq::nativecore::g_state.avatars.end()) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    animiq::nativecore::FillAvatarInfo(it->second, handle, out_info);
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_get_expression_count(NcAvatarHandle handle, uint32_t* out_count) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (out_count == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "out_count must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    auto it = animiq::nativecore::g_state.avatars.find(handle);
    if (it == animiq::nativecore::g_state.avatars.end()) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    *out_count = static_cast<std::uint32_t>(it->second.expressions.size());
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_get_expression_infos(
    NcAvatarHandle handle,
    NcExpressionInfo* out_infos,
    uint32_t capacity,
    uint32_t* out_written) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (out_written == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "out_written must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    auto it = animiq::nativecore::g_state.avatars.find(handle);
    if (it == animiq::nativecore::g_state.avatars.end()) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    *out_written = 0U;
    const auto& expressions = it->second.expressions;
    const auto to_write = static_cast<std::uint32_t>(std::min<std::size_t>(capacity, expressions.size()));
    if (to_write > 0U && out_infos == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "out_infos must not be null when capacity > 0", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    for (std::uint32_t i = 0U; i < to_write; ++i) {
        std::memset(&out_infos[i], 0, sizeof(NcExpressionInfo));
        const auto& expr = expressions[i];
        animiq::nativecore::CopyString(out_infos[i].name, sizeof(out_infos[i].name), expr.name);
        animiq::nativecore::CopyString(out_infos[i].mapping_kind, sizeof(out_infos[i].mapping_kind), expr.mapping_kind);
        out_infos[i].default_weight = expr.default_weight;
        out_infos[i].runtime_weight = expr.runtime_weight;
        out_infos[i].bind_count = static_cast<std::uint32_t>(expr.binds.size());
    }
    *out_written = to_write;
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_get_springbone_info(NcAvatarHandle handle, NcSpringBoneInfo* out_info) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (out_info == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "out_info must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    auto it = animiq::nativecore::g_state.avatars.find(handle);
    if (it == animiq::nativecore::g_state.avatars.end()) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    std::memset(out_info, 0, sizeof(*out_info));
    out_info->present = it->second.springbone_summary.present ? 1U : 0U;
    out_info->spring_count = it->second.springbone_summary.spring_count;
    out_info->joint_count = it->second.springbone_summary.joint_count;
    out_info->collider_count = it->second.springbone_summary.collider_count;
    out_info->collider_group_count = it->second.springbone_summary.collider_group_count;
    const auto state_it = animiq::nativecore::g_state.secondary_motion_states.find(handle);
    if (state_it != animiq::nativecore::g_state.secondary_motion_states.end()) {
        out_info->active_chain_count = state_it->second.active_chain_count;
        out_info->corrected_chain_count = state_it->second.corrected_chain_count;
        out_info->disabled_chain_count = state_it->second.disabled_chain_count;
        out_info->unsupported_collider_chain_count = state_it->second.unsupported_collider_chain_count;
        out_info->avg_substeps = state_it->second.avg_substeps;
    }
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_get_avatar_runtime_metrics_v2(NcAvatarHandle handle, NcAvatarRuntimeMetricsV2* out_info) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (out_info == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "out_info must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    auto it = animiq::nativecore::g_state.avatars.find(handle);
    if (it == animiq::nativecore::g_state.avatars.end()) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    animiq::nativecore::FillAvatarRuntimeMetricsV2(it->second, handle, out_info);
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_set_tracking_frame(const NcTrackingFrame* frame) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (frame == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "tracking", "frame must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    NcTrackingFrame sanitized = *frame;
    animiq::nativecore::SanitizeTrackingFrame(&sanitized);
    animiq::nativecore::g_state.latest_tracking = sanitized;
    animiq::nativecore::g_state.tracking_weights_dirty = true;
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_set_expression_weights(const NcExpressionWeight* weights, uint32_t count) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if ((count > 0U) && weights == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "tracking", "weights must not be null when count > 0", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    auto normalize_key = [](const std::string& raw) -> std::string {
        std::string out;
        out.reserve(raw.size());
        for (const unsigned char ch : raw) {
            if (std::isalnum(ch) != 0) {
                out.push_back(static_cast<char>(std::tolower(ch)));
            }
        }
        return out;
    };

    std::unordered_map<std::string, float> incoming;
    incoming.reserve(static_cast<std::size_t>(count));
    for (uint32_t i = 0U; i < count; ++i) {
        std::string key(weights[i].name);
        key = normalize_key(key);
        if (key.empty()) {
            continue;
        }
        const float clamped = std::max(0.0f, std::min(1.0f, weights[i].weight));
        incoming[key] = clamped;
    }

    for (auto& [handle, pkg] : animiq::nativecore::g_state.avatars) {
        (void)handle;
        if (pkg.expressions.empty()) {
            continue;
        }
        const bool arkit52_mode = animiq::nativecore::HasArkit52ExpressionBindings(pkg);
        std::size_t fallback_applied_count = 0U;

        std::string summary;
        std::size_t shown = 0U;
        for (auto& expr : pkg.expressions) {
            const std::string expr_name = normalize_key(expr.name);
            const std::string mapping = normalize_key(expr.mapping_kind);

            float weight = std::max(0.0f, std::min(1.0f, expr.default_weight));
            bool matched = false;
            auto it = incoming.find(expr_name);
            if (it != incoming.end()) {
                weight = it->second;
                matched = true;
            }
            if (!matched) {
                it = incoming.find(mapping);
                if (it != incoming.end()) {
                    weight = it->second;
                    matched = true;
                }
            }
            if (!matched && arkit52_mode) {
                float fallback_weight = 0.0f;
                if (animiq::nativecore::TryResolveArkitFallbackWeight(expr_name, incoming, &fallback_weight)) {
                    weight = fallback_weight;
                    matched = true;
                    ++fallback_applied_count;
                }
            }
            if (!matched && !arkit52_mode && mapping == "blink") {
                const auto left_it = incoming.find("eyeblinkleft");
                const auto right_it = incoming.find("eyeblinkright");
                if (left_it != incoming.end() || right_it != incoming.end()) {
                    const float left = (left_it == incoming.end()) ? 0.0f : left_it->second;
                    const float right = (right_it == incoming.end()) ? 0.0f : right_it->second;
                    weight = (left + right) * 0.5f;
                    matched = true;
                }
            }
            if (!matched && !arkit52_mode && mapping == "visemeaa") {
                const auto jaw_it = incoming.find("jawopen");
                if (jaw_it != incoming.end()) {
                    weight = jaw_it->second;
                    matched = true;
                }
            }
            if (!matched && !arkit52_mode && mapping == "joy") {
                const auto smile_it = incoming.find("mouthsmileleft");
                if (smile_it != incoming.end()) {
                    weight = smile_it->second;
                    matched = true;
                }
            }

            expr.runtime_weight = std::max(0.0f, std::min(1.0f, weight));
            if (shown < 3U) {
                if (!summary.empty()) {
                    summary += ", ";
                }
                summary += expr.name + "=" + std::to_string(expr.runtime_weight);
                ++shown;
            }
        }
        pkg.last_expression_summary = summary;
        if (arkit52_mode && fallback_applied_count > 0U) {
            std::ostringstream message;
            message << "W_EXPRESSION: ARKIT52 fallback aliases applied count=" << fallback_applied_count;
            animiq::nativecore::PushAvatarWarningUnique(&pkg, message.str(), "W_ARKIT52_FALLBACK_APPLIED");
        }
    }

    animiq::nativecore::g_state.tracking_weights_dirty = false;
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_create_render_resources(NcAvatarHandle handle) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }

    auto it = animiq::nativecore::g_state.avatars.find(handle);
    if (it == animiq::nativecore::g_state.avatars.end()) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const char* parser_mode_raw = std::getenv("VSF_PARSER_MODE");
    const char* parser_mode = (parser_mode_raw != nullptr && *parser_mode_raw != '\0') ? parser_mode_raw : "sidecar";
    std::string mesh_extract_stage;
    for (const auto& warning : it->second.warnings) {
        constexpr const char* kMeshStagePrefix = "W_MESH_EXTRACT_STAGE:";
        const std::string prefix(kMeshStagePrefix);
        if (warning.rfind(prefix, 0U) == 0U) {
            mesh_extract_stage = warning.substr(prefix.size());
            while (!mesh_extract_stage.empty() &&
                   std::isspace(static_cast<unsigned char>(mesh_extract_stage.front())) != 0) {
                mesh_extract_stage.erase(mesh_extract_stage.begin());
            }
            while (!mesh_extract_stage.empty() &&
                   std::isspace(static_cast<unsigned char>(mesh_extract_stage.back())) != 0) {
                mesh_extract_stage.pop_back();
            }
            break;
        }
    }
    const char* allow_placeholder_raw = std::getenv("VSF_ALLOW_VSF_PLACEHOLDER_RENDER");
    bool allow_vsf_placeholder_render = false;
    if (allow_placeholder_raw != nullptr) {
        std::string value(allow_placeholder_raw);
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        allow_vsf_placeholder_render = value == "1" || value == "true" || value == "yes" || value == "on";
    }
    bool placeholder_payload_only = false;
    if (it->second.source_type == animiq::avatar::AvatarSourceType::VsfAvatar &&
        !it->second.mesh_payloads.empty()) {
        bool all_placeholder = true;
        for (const auto& mesh : it->second.mesh_payloads) {
            if (mesh.name != "VSF_PLACEHOLDER_QUAD") {
                all_placeholder = false;
                break;
            }
        }
        bool has_placeholder_warning_code = false;
        for (const auto& code : it->second.warning_codes) {
            std::string lowered = code;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (lowered == "vsf_placeholder_render_payload") {
                has_placeholder_warning_code = true;
                break;
            }
        }
        placeholder_payload_only = all_placeholder || has_placeholder_warning_code;
    }
    if (placeholder_payload_only && !allow_vsf_placeholder_render) {
        std::ostringstream detail;
        detail << "vsfavatar placeholder payload is preview-only; output render blocked"
               << " (format=vsfavatar"
               << ", parser_mode=" << parser_mode
               << ", parser_stage=" << (it->second.parser_stage.empty() ? "unknown" : it->second.parser_stage)
               << ", primary_error=" << (it->second.primary_error_code.empty() ? "NONE" : it->second.primary_error_code)
               << ", mesh_extract_stage=" << (mesh_extract_stage.empty() ? "unknown" : mesh_extract_stage)
               << ", mesh_payload_count=" << it->second.mesh_payloads.size()
               << ")";
        animiq::nativecore::SetError(
            NC_ERROR_UNSUPPORTED,
            "render",
            detail.str(),
            true);
        return NC_ERROR_UNSUPPORTED;
    }
    if (it->second.mesh_payloads.empty()) {
        const char* format_name = animiq::nativecore::AvatarSourceTypeName(it->second.source_type);
        const bool contract_managed_format =
            it->second.source_type == animiq::avatar::AvatarSourceType::Vrm ||
            it->second.source_type == animiq::avatar::AvatarSourceType::Miq;
        std::ostringstream detail;
        detail << "render-ready contract failed: no renderable mesh payloads"
               << " (contract=" << (contract_managed_format ? "avatar_render_ready_v1" : "legacy")
               << ", format=" << format_name
               << ", parser_mode=" << parser_mode
               << ", parser_stage=" << (it->second.parser_stage.empty() ? "unknown" : it->second.parser_stage)
               << ", primary_error=" << (it->second.primary_error_code.empty() ? "NONE" : it->second.primary_error_code)
               << ", mesh_extract_stage=" << (mesh_extract_stage.empty() ? "unknown" : mesh_extract_stage)
               << ", mesh_count=" << it->second.meshes.size()
               << ", mesh_payload_count=" << it->second.mesh_payloads.size()
               << ", material_count=" << it->second.materials.size()
               << ", material_payload_count=" << it->second.material_payloads.size()
               << ")";
        animiq::nativecore::SetError(
            NC_ERROR_UNSUPPORTED,
            "render",
            detail.str(),
            true);
        return NC_ERROR_UNSUPPORTED;
    }

    animiq::nativecore::g_state.render_ready_avatars.insert(handle);
    animiq::nativecore::g_state.avatar_preview_debug.erase(handle);
    animiq::nativecore::g_state.avatar_preview_orientation_metrics.erase(handle);
#if defined(_WIN32)
    auto mesh_it = animiq::nativecore::g_state.renderer.avatar_meshes.find(handle);
    if (mesh_it != animiq::nativecore::g_state.renderer.avatar_meshes.end()) {
        for (auto& mesh : mesh_it->second) {
            animiq::nativecore::ReleaseGpuMeshResource(&mesh);
        }
        animiq::nativecore::g_state.renderer.avatar_meshes.erase(mesh_it);
    }
    auto material_it = animiq::nativecore::g_state.renderer.avatar_materials.find(handle);
    if (material_it != animiq::nativecore::g_state.renderer.avatar_materials.end()) {
        for (auto& material : material_it->second) {
            animiq::nativecore::ReleaseGpuMaterialResource(&material);
        }
        animiq::nativecore::g_state.renderer.avatar_materials.erase(material_it);
    }
#endif
    animiq::nativecore::g_state.secondary_motion_states.erase(handle);
    animiq::nativecore::g_state.arm_pose_states.erase(handle);
    animiq::nativecore::g_state.arm_pose_auto_rollback_handles.erase(handle);
    it->second.last_render_draw_calls = 0U;
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_destroy_render_resources(NcAvatarHandle handle) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }

    auto it = animiq::nativecore::g_state.avatars.find(handle);
    if (it == animiq::nativecore::g_state.avatars.end()) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    animiq::nativecore::g_state.render_ready_avatars.erase(handle);
    animiq::nativecore::g_state.avatar_preview_debug.erase(handle);
    animiq::nativecore::g_state.avatar_preview_orientation_metrics.erase(handle);
#if defined(_WIN32)
    auto mesh_it = animiq::nativecore::g_state.renderer.avatar_meshes.find(handle);
    if (mesh_it != animiq::nativecore::g_state.renderer.avatar_meshes.end()) {
        for (auto& mesh : mesh_it->second) {
            animiq::nativecore::ReleaseGpuMeshResource(&mesh);
        }
        animiq::nativecore::g_state.renderer.avatar_meshes.erase(mesh_it);
    }
    auto material_it = animiq::nativecore::g_state.renderer.avatar_materials.find(handle);
    if (material_it != animiq::nativecore::g_state.renderer.avatar_materials.end()) {
        for (auto& material : material_it->second) {
            animiq::nativecore::ReleaseGpuMaterialResource(&material);
        }
        animiq::nativecore::g_state.renderer.avatar_materials.erase(material_it);
    }
#endif
    animiq::nativecore::g_state.secondary_motion_states.erase(handle);
    animiq::nativecore::g_state.arm_pose_states.erase(handle);
    animiq::nativecore::g_state.arm_pose_auto_rollback_handles.erase(handle);
    it->second.last_render_draw_calls = 0U;
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_render_frame(const NcRenderContext* ctx) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    return animiq::nativecore::RenderFrameLocked(ctx);
}

NcResultCode nc_create_window_render_target(const NcWindowRenderTarget* target) {
#if !defined(_WIN32)
    (void)target;
    return NC_ERROR_UNSUPPORTED;
#else
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (target == nullptr || target->hwnd == nullptr || target->width == 0U || target->height == 0U) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "window target is invalid", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    auto& state = animiq::nativecore::g_state.window_targets[target->hwnd];
    animiq::nativecore::ReleaseWindowState(&state);

    DXGI_SWAP_CHAIN_DESC swap_desc {};
    swap_desc.BufferDesc.Width = target->width;
    swap_desc.BufferDesc.Height = target->height;
    swap_desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swap_desc.SampleDesc.Count = 1U;
    swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.BufferCount = 2U;
    swap_desc.OutputWindow = static_cast<HWND>(target->hwnd);
    swap_desc.Windowed = TRUE;
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL feature_levels[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL selected_level = D3D_FEATURE_LEVEL_11_0;

    const UINT device_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        device_flags,
        feature_levels,
        static_cast<UINT>(std::size(feature_levels)),
        D3D11_SDK_VERSION,
        &swap_desc,
        &state.swap_chain,
        &state.device,
        &selected_level,
        &state.device_context);
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            device_flags,
            feature_levels,
            static_cast<UINT>(std::size(feature_levels)),
            D3D11_SDK_VERSION,
            &swap_desc,
            &state.swap_chain,
            &state.device,
            &selected_level,
            &state.device_context);
    }
    if (FAILED(hr)) {
        animiq::nativecore::g_state.window_targets.erase(target->hwnd);
        animiq::nativecore::SetError(NC_ERROR_INTERNAL, "render", "failed to create d3d11 device/swapchain", true);
        return NC_ERROR_INTERNAL;
    }

    ID3D11Texture2D* backbuffer = nullptr;
    HRESULT bb_hr = state.swap_chain->GetBuffer(0U, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuffer));
    if (FAILED(bb_hr) || backbuffer == nullptr) {
        animiq::nativecore::ReleaseWindowState(&state);
        animiq::nativecore::g_state.window_targets.erase(target->hwnd);
        animiq::nativecore::SetError(NC_ERROR_INTERNAL, "render", "failed to fetch swapchain backbuffer", true);
        return NC_ERROR_INTERNAL;
    }

    HRESULT rtv_hr = state.device->CreateRenderTargetView(backbuffer, nullptr, &state.rtv);
    backbuffer->Release();
    if (FAILED(rtv_hr) || state.rtv == nullptr) {
        animiq::nativecore::ReleaseWindowState(&state);
        animiq::nativecore::g_state.window_targets.erase(target->hwnd);
        animiq::nativecore::SetError(NC_ERROR_INTERNAL, "render", "failed to create render target view", true);
        return NC_ERROR_INTERNAL;
    }

    state.width = target->width;
    state.height = target->height;

    animiq::nativecore::ClearError();
    return NC_OK;
#endif
}

NcResultCode nc_resize_window_render_target(const NcWindowRenderTarget* target) {
#if !defined(_WIN32)
    (void)target;
    return NC_ERROR_UNSUPPORTED;
#else
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (target == nullptr || target->hwnd == nullptr || target->width == 0U || target->height == 0U) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "window target is invalid", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    auto it = animiq::nativecore::g_state.window_targets.find(target->hwnd);
    if (it == animiq::nativecore::g_state.window_targets.end()) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "unknown window render target", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    auto& state = it->second;
    if (state.swap_chain == nullptr || state.device == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INTERNAL, "render", "window render target is not initialized", true);
        return NC_ERROR_INTERNAL;
    }

    if (state.device_context != nullptr) {
        // Ensure no backbuffer-dependent bindings remain before ResizeBuffers.
        ID3D11RenderTargetView* null_rtvs[1] = {nullptr};
        state.device_context->OMSetRenderTargets(1U, null_rtvs, nullptr);
        state.device_context->ClearState();
        state.device_context->Flush();
    }

    if (state.rtv != nullptr) {
        state.rtv->Release();
        state.rtv = nullptr;
    }

    HRESULT resize_hr = state.swap_chain->ResizeBuffers(0U, target->width, target->height, DXGI_FORMAT_UNKNOWN, 0U);
    if (FAILED(resize_hr)) {
        animiq::nativecore::SetError(NC_ERROR_INTERNAL, "render", "swapchain resize failed", true);
        return NC_ERROR_INTERNAL;
    }

    ID3D11Texture2D* backbuffer = nullptr;
    HRESULT bb_hr = state.swap_chain->GetBuffer(0U, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuffer));
    if (FAILED(bb_hr) || backbuffer == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INTERNAL, "render", "failed to fetch resized backbuffer", true);
        return NC_ERROR_INTERNAL;
    }

    HRESULT rtv_hr = state.device->CreateRenderTargetView(backbuffer, nullptr, &state.rtv);
    backbuffer->Release();
    if (FAILED(rtv_hr) || state.rtv == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INTERNAL, "render", "failed to create resized render target view", true);
        return NC_ERROR_INTERNAL;
    }

    state.width = target->width;
    state.height = target->height;
    animiq::nativecore::ClearError();
    return NC_OK;
#endif
}

NcResultCode nc_destroy_window_render_target(void* hwnd) {
#if !defined(_WIN32)
    (void)hwnd;
    return NC_ERROR_UNSUPPORTED;
#else
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (hwnd == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "hwnd must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    auto it = animiq::nativecore::g_state.window_targets.find(hwnd);
    if (it == animiq::nativecore::g_state.window_targets.end()) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "unknown window render target", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    animiq::nativecore::ReleaseWindowState(&it->second);
    animiq::nativecore::g_state.window_targets.erase(it);
    animiq::nativecore::ClearError();
    return NC_OK;
#endif
}

NcResultCode nc_render_frame_to_window(void* hwnd, float delta_time_seconds) {
#if !defined(_WIN32)
    (void)hwnd;
    (void)delta_time_seconds;
    return NC_ERROR_UNSUPPORTED;
#else
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (hwnd == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "hwnd must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    auto it = animiq::nativecore::g_state.window_targets.find(hwnd);
    if (it == animiq::nativecore::g_state.window_targets.end()) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "unknown window render target", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    auto& state = it->second;
    if (state.device == nullptr || state.device_context == nullptr || state.rtv == nullptr || state.swap_chain == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INTERNAL, "render", "window render target is incomplete", true);
        return NC_ERROR_INTERNAL;
    }

    NcRenderContext ctx {};
    ctx.hwnd = hwnd;
    ctx.d3d11_device = state.device;
    ctx.d3d11_device_context = state.device_context;
    ctx.d3d11_rtv = state.rtv;
    ctx.width = state.width;
    ctx.height = state.height;
    ctx.delta_time_seconds = delta_time_seconds;

    NcResultCode rc = animiq::nativecore::RenderFrameLocked(&ctx);
    if (rc != NC_OK) {
        return rc;
    }

    const HRESULT present_hr = state.swap_chain->Present(1U, 0U);
    if (FAILED(present_hr)) {
        animiq::nativecore::SetError(NC_ERROR_INTERNAL, "render", "swapchain present failed", true);
        return NC_ERROR_INTERNAL;
    }

    animiq::nativecore::ClearError();
    return NC_OK;
#endif
}

NcResultCode nc_render_avatar_thumbnail_png(const NcThumbnailRequest* request) {
#if !defined(_WIN32)
    (void)request;
    return NC_ERROR_UNSUPPORTED;
#else
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (request == nullptr ||
        request->handle == 0U ||
        request->output_path == nullptr ||
        request->output_path[0] == '\0' ||
        request->width == 0U ||
        request->height == 0U) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "thumbnail", "thumbnail request is invalid", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (request->width > 4096U || request->height > 4096U) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "thumbnail", "thumbnail size exceeds max 4096x4096", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (animiq::nativecore::g_state.avatars.find(request->handle) == animiq::nativecore::g_state.avatars.end()) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "thumbnail", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (animiq::nativecore::g_state.render_ready_avatars.find(request->handle) == animiq::nativecore::g_state.render_ready_avatars.end()) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "thumbnail", "avatar render resources are not ready", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    D3D_FEATURE_LEVEL feature_levels[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL selected_level = D3D_FEATURE_LEVEL_11_0;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* device_ctx = nullptr;
    HRESULT device_hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        feature_levels,
        static_cast<UINT>(std::size(feature_levels)),
        D3D11_SDK_VERSION,
        &device,
        &selected_level,
        &device_ctx);
    if (FAILED(device_hr) || device == nullptr || device_ctx == nullptr) {
        if (device_ctx != nullptr) {
            device_ctx->Release();
            device_ctx = nullptr;
        }
        if (device != nullptr) {
            device->Release();
            device = nullptr;
        }
        device_hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            feature_levels,
            static_cast<UINT>(std::size(feature_levels)),
            D3D11_SDK_VERSION,
            &device,
            &selected_level,
            &device_ctx);
    }
    if (FAILED(device_hr) || device == nullptr || device_ctx == nullptr) {
        if (device_ctx != nullptr) {
            device_ctx->Release();
        }
        if (device != nullptr) {
            device->Release();
        }
        animiq::nativecore::SetError(NC_ERROR_INTERNAL, "thumbnail", "failed to create thumbnail d3d11 device", true);
        return NC_ERROR_INTERNAL;
    }

    D3D11_TEXTURE2D_DESC tex_desc {};
    tex_desc.Width = request->width;
    tex_desc.Height = request->height;
    tex_desc.MipLevels = 1U;
    tex_desc.ArraySize = 1U;
    tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    tex_desc.SampleDesc.Count = 1U;
    tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    ID3D11Texture2D* target_texture = nullptr;
    const HRESULT tex_hr = device->CreateTexture2D(&tex_desc, nullptr, &target_texture);
    if (FAILED(tex_hr) || target_texture == nullptr) {
        device_ctx->Release();
        device->Release();
        animiq::nativecore::SetError(NC_ERROR_INTERNAL, "thumbnail", "failed to create thumbnail render target texture", true);
        return NC_ERROR_INTERNAL;
    }

    ID3D11RenderTargetView* rtv = nullptr;
    const HRESULT rtv_hr = device->CreateRenderTargetView(target_texture, nullptr, &rtv);
    if (FAILED(rtv_hr) || rtv == nullptr) {
        target_texture->Release();
        device_ctx->Release();
        device->Release();
        animiq::nativecore::SetError(NC_ERROR_INTERNAL, "thumbnail", "failed to create thumbnail render target view", true);
        return NC_ERROR_INTERNAL;
    }

    const float delta = request->delta_time_seconds > 0.0f ? request->delta_time_seconds : (1.0f / 60.0f);
    NcRenderContext ctx {};
    ctx.hwnd = nullptr;
    ctx.d3d11_device = device;
    ctx.d3d11_device_context = device_ctx;
    ctx.d3d11_rtv = rtv;
    ctx.width = request->width;
    ctx.height = request->height;
    ctx.delta_time_seconds = delta;

    const auto previous_ready = animiq::nativecore::g_state.render_ready_avatars;
    animiq::nativecore::g_state.render_ready_avatars.clear();
    animiq::nativecore::g_state.render_ready_avatars.insert(request->handle);
    const NcResultCode render_rc = animiq::nativecore::RenderFrameLocked(&ctx);
    animiq::nativecore::g_state.render_ready_avatars = previous_ready;
    if (render_rc != NC_OK) {
        rtv->Release();
        target_texture->Release();
        device_ctx->Release();
        device->Release();
        return render_rc;
    }

    std::vector<std::uint8_t> pixels;
    if (!animiq::nativecore::CaptureRtvBgra(device, device_ctx, rtv, request->width, request->height, &pixels)) {
        rtv->Release();
        target_texture->Release();
        device_ctx->Release();
        device->Release();
        animiq::nativecore::SetError(NC_ERROR_INTERNAL, "thumbnail", "failed to capture thumbnail pixels", true);
        return NC_ERROR_INTERNAL;
    }
    if (!animiq::nativecore::EncodeBgraToPngFile(pixels, request->width, request->height, request->output_path)) {
        rtv->Release();
        target_texture->Release();
        device_ctx->Release();
        device->Release();
        animiq::nativecore::SetError(NC_ERROR_IO, "thumbnail", "failed to encode thumbnail png", true);
        return NC_ERROR_IO;
    }

    rtv->Release();
    target_texture->Release();
    device_ctx->Release();
    device->Release();
    animiq::nativecore::ClearError();
    return NC_OK;
#endif
}

NcResultCode nc_set_render_quality_options(const NcRenderQualityOptions* options) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (options == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "options must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    animiq::nativecore::g_state.render_quality = animiq::nativecore::SanitizeRenderQualityOptions(*options);
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_get_render_quality_options(NcRenderQualityOptions* out_options) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (out_options == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "out_options must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    *out_options = animiq::nativecore::SanitizeRenderQualityOptions(animiq::nativecore::g_state.render_quality);
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_set_lighting_options(const NcLightingOptions* options) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (options == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "lighting options must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    animiq::nativecore::g_state.lighting_options = animiq::nativecore::SanitizeLightingOptions(*options);
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_get_lighting_options(NcLightingOptions* out_options) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (out_options == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "out lighting options must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    *out_options = animiq::nativecore::SanitizeLightingOptions(animiq::nativecore::g_state.lighting_options);
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_set_pose_offsets(const NcPoseBoneOffset* offsets, uint32_t count) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if ((count > 0U) && offsets == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "pose", "offsets must not be null when count > 0", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    auto next = animiq::nativecore::MakeDefaultPoseOffsets();
    for (uint32_t i = 0U; i < count; ++i) {
        const auto sanitized = animiq::nativecore::SanitizePoseOffset(offsets[i]);
        if (!animiq::nativecore::IsValidPoseBoneId(sanitized.bone_id)) {
            continue;
        }
        next[sanitized.bone_id] = sanitized;
    }
    animiq::nativecore::g_state.pose_offsets = next;
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_clear_pose_offsets(void) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    animiq::nativecore::g_state.pose_offsets = animiq::nativecore::MakeDefaultPoseOffsets();
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_start_spout(const NcSpoutOptions* options) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (options == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "spout", "options must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    animiq::stream::StreamConfig cfg;
    cfg.width = options->width > 0U ? options->width : 1920U;
    cfg.height = options->height > 0U ? options->height : 1080U;
    cfg.fps = options->fps > 0U ? options->fps : 60U;
    cfg.channel_name = (options->channel_name != nullptr && options->channel_name[0] != '\0') ? options->channel_name : "Animiq";

    if (!animiq::nativecore::g_state.spout.Start(cfg)) {
        animiq::nativecore::SetError(NC_ERROR_INTERNAL, "spout", "failed to start spout output", true);
        return NC_ERROR_INTERNAL;
    }
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_stop_spout(void) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    animiq::nativecore::g_state.spout.Stop();
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_start_osc(const NcOscOptions* options) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (options == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "osc", "options must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    const std::uint16_t port = options->bind_port > 0U ? options->bind_port : 39539U;
    if (!animiq::nativecore::g_state.osc.SetDestination(
            (options->publish_address != nullptr && options->publish_address[0] != '\0')
                ? options->publish_address
                : "127.0.0.1:39539")) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "osc", "invalid publish_address format", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (!animiq::nativecore::g_state.osc.Bind(port)) {
        animiq::nativecore::SetError(NC_ERROR_INTERNAL, "osc", "failed to bind osc endpoint", true);
        return NC_ERROR_INTERNAL;
    }
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_stop_osc(void) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    animiq::nativecore::g_state.osc.Close();
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_get_last_error(NcErrorInfo* out_error) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (out_error == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }

    std::memset(out_error, 0, sizeof(*out_error));
    out_error->code = animiq::nativecore::g_state.last_error_code;
    animiq::nativecore::CopyString(out_error->subsystem, sizeof(out_error->subsystem), animiq::nativecore::g_state.last_error_subsystem);
    animiq::nativecore::CopyString(out_error->message, sizeof(out_error->message), animiq::nativecore::g_state.last_error_message);
    out_error->recoverable = animiq::nativecore::g_state.last_error_recoverable ? 1U : 0U;
    return NC_OK;
}

NcResultCode nc_get_runtime_stats(NcRuntimeStats* out_stats) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (out_stats == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "runtime", "out_stats must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    std::memset(out_stats, 0, sizeof(*out_stats));
    out_stats->render_ready_avatar_count = static_cast<std::uint32_t>(animiq::nativecore::g_state.render_ready_avatars.size());
    out_stats->spout_active = animiq::nativecore::g_state.spout.IsActive() ? 1U : 0U;
    out_stats->osc_active = animiq::nativecore::g_state.osc.IsBound() ? 1U : 0U;
    out_stats->last_frame_ms = animiq::nativecore::g_state.last_frame_ms;
    out_stats->gpu_frame_ms = animiq::nativecore::g_state.last_gpu_frame_ms;
    out_stats->cpu_frame_ms = animiq::nativecore::g_state.last_cpu_frame_ms;
    out_stats->material_resolve_ms = animiq::nativecore::g_state.last_material_resolve_ms;
    out_stats->pass_count = animiq::nativecore::g_state.last_pass_count;
    animiq::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_get_spout_diagnostics(NcSpoutDiagnostics* out_diag) {
    std::lock_guard<std::mutex> lock(animiq::nativecore::g_mutex);
    if (!animiq::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (out_diag == nullptr) {
        animiq::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "spout", "out_diag must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    std::memset(out_diag, 0, sizeof(*out_diag));
    out_diag->strict_mode = animiq::nativecore::g_state.spout.IsStrictMode() ? 1U : 0U;
    out_diag->fallback_count = animiq::nativecore::g_state.spout.FallbackCount();
    const auto backend = animiq::nativecore::g_state.spout.ActiveBackendKind();
    switch (backend) {
        case animiq::stream::SpoutSender::BackendKind::Spout2Gpu:
            out_diag->backend_kind = NC_SPOUT_BACKEND_SPOUT2_GPU;
            break;
        case animiq::stream::SpoutSender::BackendKind::LegacySharedMemory:
            out_diag->backend_kind = NC_SPOUT_BACKEND_LEGACY_SHARED_MEMORY;
            break;
        default:
            out_diag->backend_kind = NC_SPOUT_BACKEND_INACTIVE;
            break;
    }
    animiq::nativecore::CopyString(out_diag->last_error_code, sizeof(out_diag->last_error_code), animiq::nativecore::g_state.spout.LastErrorCode());
    animiq::nativecore::ClearError();
    return NC_OK;
}
