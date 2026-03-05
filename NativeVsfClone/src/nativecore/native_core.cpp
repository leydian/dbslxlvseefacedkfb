#include "vsfclone/nativecore/api.h"

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

#include "vsfclone/avatar/avatar_loader_facade.h"
#include "vsfclone/avatar/avatar_package.h"
#include "vsfclone/osc/osc_endpoint.h"
#include "vsfclone/stream/spout_sender.h"
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

namespace vsfclone::nativecore {

namespace {

using vsfclone::avatar::AvatarCompatLevel;
using vsfclone::avatar::AvatarPackage;
using vsfclone::avatar::AvatarSourceType;
using vsfclone::avatar::ExpressionState;

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

struct GpuMaterialResource {
    std::string alpha_mode = "OPAQUE";
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
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
    ID3D11PixelShader* pixel_shader = nullptr;
    ID3D11InputLayout* input_layout = nullptr;
    ID3D11Buffer* constant_buffer = nullptr;
    ID3D11RasterizerState* raster_cull_back = nullptr;
    ID3D11RasterizerState* raster_cull_front = nullptr;
    ID3D11RasterizerState* raster_cull_none = nullptr;
    ID3D11DepthStencilState* depth_write = nullptr;
    ID3D11DepthStencilState* depth_read = nullptr;
    ID3D11BlendState* blend_opaque = nullptr;
    ID3D11BlendState* blend_alpha = nullptr;
    ID3D11SamplerState* linear_sampler = nullptr;
    ID3D11Texture2D* depth_texture = nullptr;
    ID3D11DepthStencilView* depth_dsv = nullptr;
    std::uint32_t depth_width = 0;
    std::uint32_t depth_height = 0;
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

struct CoreState {
    bool initialized = false;
    std::uint64_t next_avatar_handle = 1;
    std::unordered_map<std::uint64_t, AvatarPackage> avatars;
    std::unordered_map<std::uint64_t, std::string> avatar_preview_debug;
    std::unordered_map<std::uint64_t, AvatarSecondaryMotionState> secondary_motion_states;
    std::unordered_map<std::uint64_t, AvatarArmPoseState> arm_pose_states;
    std::unordered_set<std::uint64_t> render_ready_avatars;
    avatar::AvatarLoaderFacade loader;
    stream::SpoutSender spout;
    osc::OscEndpoint osc;
    NcTrackingFrame latest_tracking {};
    NcRenderQualityOptions render_quality {};
    std::array<NcPoseBoneOffset, 15U> pose_offsets {};
    float last_frame_ms = 0.0f;
    float last_gpu_frame_ms = 0.0f;
    float last_cpu_frame_ms = 0.0f;
    float last_material_resolve_ms = 0.0f;
    std::uint32_t last_pass_count = 0U;
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
    options.framing_target = 0.72f;
    options.headroom = 0.12f;
    options.yaw_deg = 0.0f;
    options.fov_deg = 45.0f;
    options.background_rgba[0] = 0.08f;
    options.background_rgba[1] = 0.12f;
    options.background_rgba[2] = 0.18f;
    options.background_rgba[3] = 1.0f;
    options.quality_profile = NC_RENDER_QUALITY_DEFAULT;
    options.show_debug_overlay = 0U;
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
    if (out.quality_profile > NC_RENDER_QUALITY_ULTRA_PARITY) {
        out.quality_profile = NC_RENDER_QUALITY_DEFAULT;
    }
    out.show_debug_overlay = out.show_debug_overlay > 0U ? 1U : 0U;
    return out;
}

CoreState g_state;
std::mutex g_mutex;

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

void FillAvatarRuntimeMetricsV2(const AvatarPackage& pkg, std::uint64_t handle, NcAvatarRuntimeMetricsV2* out_info) {
    if (out_info == nullptr) {
        return;
    }
    std::memset(out_info, 0, sizeof(*out_info));
    out_info->target_frame_ms = 1000.0f / 60.0f;
    out_info->last_frame_ms = g_state.last_frame_ms;
    CopyString(out_info->physics_solver, sizeof(out_info->physics_solver), "spring-v2-damped");
    CopyString(out_info->mtoon_runtime_mode, sizeof(out_info->mtoon_runtime_mode), "mtoon-advanced-runtime");

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
        "xav2_skinning_static_disabled",
        "xav2_skinning_fallback_skipped_no_skeleton",
        "xav2_material_typed_texture_unresolved",
        "material_index_oob_skipped",
        "xav3_skeleton_payload_missing",
        "xav3_skeleton_mesh_bind_mismatch",
        "xav3_skinning_matrix_invalid",
        "xav2_unknown_section_not_allowed",
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
    if (code == "skinning_matrix_convention_applied") {
        meta.severity = "info";
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
    if (code.rfind("xav2_", 0U) == 0U || code.rfind("xav3_", 0U) == 0U || code.rfind("xav4_", 0U) == 0U) {
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
        case AvatarSourceType::Xav2:
            return NC_AVATAR_FORMAT_XAV2;
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
        case AvatarSourceType::Xav2:
            return "xav2";
        case AvatarSourceType::VsfAvatar:
            return "vsfavatar";
        default:
            return "unknown";
    }
}

int PreviewYawDegreesForAvatarSource(AvatarSourceType source_type) {
    // Runtime preview yaw is the single source-of-truth for front-view alignment.
    if (source_type == AvatarSourceType::Xav2) {
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

bool HasWarningCode(const AvatarPackage& pkg, const char* code) {
    if (code == nullptr || *code == '\0') {
        return false;
    }
    std::string needle(code);
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    for (const auto& item : pkg.warning_codes) {
        std::string lowered = item;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (lowered == needle) {
            return true;
        }
    }
    return false;
}

int PreviewYawDegreesForAvatarPackage(const AvatarPackage& pkg, const char** reason_out) {
    if (reason_out != nullptr) {
        *reason_out = "default";
    }
    const int base = PreviewYawDegreesForAvatarSource(pkg.source_type);
    if (pkg.source_type != AvatarSourceType::Vrm) {
        return base;
    }
    if (HasWarningCode(pkg, "VRM_NODE_TRANSFORM_SKIN_FALLBACK") ||
        HasWarningCode(pkg, "VRM_NODE_TRANSFORM_SKIN_BYPASS") ||
        HasWarningCode(pkg, "VRM_NODE_TRANSFORM_CONFLICT") ||
        HasWarningCode(pkg, "VRM_MESH_MULTI_NODE_REF")) {
        if (reason_out != nullptr) {
            *reason_out = "vrm_auto_fallback_180";
        }
        return 180;
    }
    if (HasWarningCode(pkg, "VRM_NODE_TRANSFORM_APPLIED") && !pkg.skin_payloads.empty()) {
        if (reason_out != nullptr) {
            *reason_out = "vrm_node_transform_applied_180";
        }
        return 180;
    }
    return base;
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
    if (renderer->linear_sampler != nullptr) {
        renderer->linear_sampler->Release();
        renderer->linear_sampler = nullptr;
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
    if (renderer->constant_buffer != nullptr) {
        renderer->constant_buffer->Release();
        renderer->constant_buffer = nullptr;
    }
    if (renderer->input_layout != nullptr) {
        renderer->input_layout->Release();
        renderer->input_layout = nullptr;
    }
    if (renderer->pixel_shader != nullptr) {
        renderer->pixel_shader->Release();
        renderer->pixel_shader = nullptr;
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
    if (renderer->vertex_shader != nullptr && renderer->pixel_shader != nullptr &&
        renderer->input_layout != nullptr && renderer->constant_buffer != nullptr &&
        renderer->raster_cull_back != nullptr && renderer->raster_cull_front != nullptr &&
        renderer->raster_cull_none != nullptr &&
        renderer->depth_write != nullptr && renderer->depth_read != nullptr &&
        renderer->blend_opaque != nullptr && renderer->blend_alpha != nullptr &&
        renderer->linear_sampler != nullptr) {
        return true;
    }

    constexpr char kVertexShaderSrc[] =
        "cbuffer SceneCB : register(b0) {\n"
        "  float4x4 world_view_proj;\n"
        "  float4 base_color;\n"
        "  float4 shade_color;\n"
        "  float4 emission_color;\n"
        "  float4 rim_color;\n"
        "  float4 matcap_color;\n"
        "  float4 liltoon_mix;\n"
        "  float4 liltoon_params;\n"
        "  float4 liltoon_aux;\n"
        "  float4 alpha_misc;\n"
        "  float4 outline_params;\n"
        "  float4 uv_anim_params;\n"
        "  float4 time_params;\n"
        "};\n"
        "struct VSIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
        "struct VSOut { float4 pos : SV_POSITION; float4 color : COLOR0; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
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
        "  return o;\n"
        "}\n";
    constexpr char kPixelShaderSrc[] =
        "cbuffer SceneCB : register(b0) {\n"
        "  float4x4 world_view_proj;\n"
        "  float4 base_color;\n"
        "  float4 shade_color;\n"
        "  float4 emission_color;\n"
        "  float4 rim_color;\n"
        "  float4 matcap_color;\n"
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
        "SamplerState samp0 : register(s0);\n"
        "float4 main(float4 pos : SV_POSITION, float4 color : COLOR0, float3 nrm : NORMAL, float2 uv : TEXCOORD0) : SV_TARGET {\n"
        "  float is_outline_pass = outline_params.w;\n"
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
        "  float4 out_color = color;\n"
        "  float3 normal = normalize(nrm);\n"
        "  if (use_normal_tex > 0.5) {\n"
        "    float3 ntex = tex1.Sample(samp0, sample_uv).xyz * 2.0 - 1.0;\n"
        "    normal = normalize(float3(normal.xy + ntex.xy * saturate(liltoon_mix.z), max(0.15, normal.z * abs(ntex.z))));\n"
        "  }\n"
        "  float3 light_dir = normalize(float3(0.35, 0.45, 0.82));\n"
        "  float ndotl = saturate(dot(normal, light_dir));\n"
        "  float lit = lerp(0.55, 1.0, ndotl);\n"
        "  if (has_texture > 0.5) {\n"
        "    float4 texel = tex0.Sample(samp0, sample_uv);\n"
        "    out_color.rgb *= texel.rgb;\n"
        "    if (use_texture_alpha > 0.5) {\n"
        "      out_color.a *= texel.a;\n"
        "    }\n"
        "  }\n"
        "  out_color.rgb *= lit;\n"
        "  float shade_t = saturate((1.0 - ndotl) * saturate(liltoon_mix.x) * 1.2);\n"
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

    ID3DBlob* vs_blob = nullptr;
    ID3DBlob* ps_blob = nullptr;
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
    hr = D3DCompile(
        kPixelShaderSrc,
        sizeof(kPixelShaderSrc) - 1U,
        nullptr,
        nullptr,
        nullptr,
        "main",
        "ps_5_0",
        0U,
        0U,
        &ps_blob,
        &err_blob);
    if (FAILED(hr) || ps_blob == nullptr) {
        if (vs_blob != nullptr) {
            vs_blob->Release();
        }
        if (err_blob != nullptr) {
            err_blob->Release();
        }
        return false;
    }
    if (err_blob != nullptr) {
        err_blob->Release();
        err_blob = nullptr;
    }

    hr = device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &renderer->vertex_shader);
    if (FAILED(hr) || renderer->vertex_shader == nullptr) {
        vs_blob->Release();
        ps_blob->Release();
        return false;
    }
    hr = device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &renderer->pixel_shader);
    if (FAILED(hr) || renderer->pixel_shader == nullptr) {
        vs_blob->Release();
        ps_blob->Release();
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
    vs_blob->Release();
    ps_blob->Release();
    if (FAILED(hr) || renderer->input_layout == nullptr) {
        return false;
    }

    struct alignas(16) SceneConstants {
        float world_view_proj[16];
        float base_color[4];
        float shade_color[4];
        float emission_color[4];
        float rim_color[4];
        float matcap_color[4];
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

std::string ToLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
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
    return pkg.source_type == AvatarSourceType::Vrm ? "VRM_SPRING_AUTO_CORRECTED" : "XAV2_PHYSICS_AUTO_CORRECTED";
}

const char* PhysicsDisabledCodeFor(const AvatarPackage& pkg) {
    return pkg.source_type == AvatarSourceType::Vrm ? "VRM_SPRING_CHAIN_DISABLED" : "XAV2_PHYSICS_CHAIN_DISABLED";
}

const char* PhysicsUnsupportedColliderCodeFor(const AvatarPackage& pkg) {
    return pkg.source_type == AvatarSourceType::Vrm ? "VRM_SPRING_UNSUPPORTED_COLLIDER" : "XAV2_PHYSICS_UNSUPPORTED_COLLIDER";
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
    // Temporary safety gate: XAV2 avatars still show unstable per-mesh deformation
    // in some exported rigs; keep base pose until parser/matrix paths are unified.
    if (avatar_pkg->source_type == AvatarSourceType::Xav2) {
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
    static const bool enabled = []() {
        const char* raw = std::getenv("VSFCLONE_XAV2_ENABLE_STATIC_SKINNING");
        if (raw == nullptr) {
            return true;
        }
        std::string token(raw);
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (token == "0" || token == "false" || token == "no" || token == "off") {
            return false;
        }
        return token == "1" || token == "true" || token == "yes" || token == "on";
    }();
    return enabled;
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
        out.code = "XAV2_SKIN_MESH_VERTEX_LAYOUT_INVALID";
        out.detail = "mesh vertex blob/stride is invalid";
        return out;
    }
    if ((skin_payload.bind_poses_16xn.size() % 16U) != 0U || skin_payload.bind_poses_16xn.empty()) {
        out.code = "XAV2_SKIN_BINDPOSE_INVALID";
        out.detail = "bind pose array must be a non-empty multiple of 16";
        return out;
    }
    const std::uint32_t vertex_count = static_cast<std::uint32_t>(mesh_payload.vertex_blob.size() / src_stride);
    constexpr std::size_t kBytesPerVertex = 32U;
    if (skin_payload.skin_weight_blob.size() != static_cast<std::size_t>(vertex_count) * kBytesPerVertex) {
        out.code = "XAV2_SKIN_WEIGHT_BLOB_SIZE_MISMATCH";
        out.detail = "skin weight blob size does not match vertex count";
        return out;
    }
    std::vector<SkinWeight4> decoded_weights;
    if (!DecodeSkinWeights(skin_payload.skin_weight_blob, vertex_count, &decoded_weights)) {
        out.code = "XAV2_SKIN_WEIGHT_BLOB_DECODE_FAILED";
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
                out.code = "XAV2_SKIN_BONE_INDEX_OOB";
                out.detail = "bone index is out of bind-pose range";
                return out;
            }
            sum += w;
        }
        if (sum > 0.0f && std::abs(sum - 1.0f) > 0.2f) {
            out.code = "XAV2_SKIN_WEIGHT_SUM_INVALID";
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
    const avatar::SkeletonRenderPayload* skeleton_payload) {
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

    std::vector<DirectX::XMMATRIX> skin_matrices(bind_pose_count, DirectX::XMMatrixIdentity());
    for (std::size_t i = 0U; i < bind_pose_count; ++i) {
        DirectX::XMFLOAT4X4 bind_pose {};
        for (std::size_t j = 0U; j < 16U; ++j) {
            reinterpret_cast<float*>(&bind_pose)[j] = skin_payload.bind_poses_16xn[i * 16U + j];
        }
        const auto bind_m = DirectX::XMLoadFloat4x4(&bind_pose);
        DirectX::XMFLOAT4X4 bone_m {};
        for (std::size_t j = 0U; j < 16U; ++j) {
            reinterpret_cast<float*>(&bone_m)[j] = skeleton_payload->bone_matrices_16xn[i * 16U + j];
        }
        // Convention: skin matrix = current joint pose (mesh-space) * inverse-bind.
        skin_matrices[i] = DirectX::XMMatrixMultiply(DirectX::XMLoadFloat4x4(&bone_m), bind_m);
    }

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

bool BuildGpuMeshForPayload(
    const avatar::MeshRenderPayload& payload,
    const avatar::SkinRenderPayload* skin_payload,
    const avatar::SkeletonRenderPayload* skeleton_payload,
    bool force_static_skinning_fallback,
    ID3D11Device* device,
    GpuMeshResource* out_mesh) {
    if (device == nullptr || out_mesh == nullptr || payload.vertex_blob.empty() || payload.indices.empty()) {
        return false;
    }
    const std::uint32_t src_stride = payload.vertex_stride >= 12U ? payload.vertex_stride : 12U;
    if ((payload.vertex_blob.size() % src_stride) != 0U) {
        return false;
    }
    const std::uint32_t vertex_count = static_cast<std::uint32_t>(payload.vertex_blob.size() / src_stride);
    const std::uint32_t index_count = static_cast<std::uint32_t>(payload.indices.size());

    std::vector<std::uint8_t> gpu_vertex_blob;
    gpu_vertex_blob.reserve(static_cast<std::size_t>(vertex_count) * 32U);
    const auto* src = payload.vertex_blob.data();
    // XAV2 exporter layout is fixed: pos3(0) + normal3(12) + uv2(24) + tangent4(32).
    // Keep strict UV offset to avoid false-positive heuristic matches.
    const std::uint32_t uv_offset = (src_stride >= 32U) ? 24U : 12U;
    for (std::uint32_t i = 0U; i < vertex_count; ++i) {
        const std::size_t base = static_cast<std::size_t>(i) * src_stride;
        gpu_vertex_blob.insert(gpu_vertex_blob.end(), src + base, src + base + 12U);
        if (src_stride >= 24U) {
            gpu_vertex_blob.insert(gpu_vertex_blob.end(), src + base + 12U, src + base + 24U);
        } else {
            const std::array<float, 3U> nrm_up = {0.0f, 1.0f, 0.0f};
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

    const std::vector<std::uint8_t> bind_pose_blob = gpu_vertex_blob;
    auto compute_position_stats = [](const std::vector<std::uint8_t>& blob) {
        struct Stats {
            float max_abs = 0.0f;
            float extent_max = 0.0f;
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
        s.extent_max = std::max(ex, std::max(ey, ez));
        return s;
    };
    if (skin_payload != nullptr) {
        const bool enable_static_skinning = ShouldApplyExperimentalStaticSkinning();
        const auto pre_stats = compute_position_stats(gpu_vertex_blob);
        const bool can_apply_with_skeleton =
            skeleton_payload != nullptr && IsValidSkeletonPosePayload(*skin_payload, *skeleton_payload);
        if (enable_static_skinning && can_apply_with_skeleton) {
            (void)ApplyStaticSkinningToVertexBlob(&gpu_vertex_blob, 32U, *skin_payload, skeleton_payload);
        } else if (enable_static_skinning && (force_static_skinning_fallback || ShouldApplyExperimentalStaticSkinning())) {
            (void)ApplyStaticSkinningToVertexBlob(&gpu_vertex_blob, 32U, *skin_payload, nullptr);
        }
        const auto post_stats = compute_position_stats(gpu_vertex_blob);
        const float pre_extent = std::max(0.0001f, pre_stats.extent_max);
        const float post_extent = post_stats.extent_max;
        // Guard against plausible-but-wrong skinning matrices that stay finite
        // but inflate bounds massively (common when matrix convention mismatches).
        const bool exploded_extent = post_extent > (pre_extent * 20.0f);
        const bool exploded_abs = post_stats.max_abs > std::max(200.0f, pre_stats.max_abs * 20.0f);
        if (!post_stats.finite || exploded_extent || exploded_abs) {
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
    ib_desc.ByteWidth = static_cast<UINT>(payload.indices.size() * sizeof(std::uint32_t));
    ib_desc.Usage = D3D11_USAGE_DEFAULT;
    ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ib_data {};
    ib_data.pSysMem = payload.indices.data();

    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    if (FAILED(device->CreateBuffer(&vb_desc, &vb_data, &vb)) || vb == nullptr) {
        return false;
    }
    if (FAILED(device->CreateBuffer(&ib_desc, &ib_data, &ib)) || ib == nullptr) {
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
    const bool static_skinning_enabled = ShouldApplyExperimentalStaticSkinning();
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
    for (const auto& payload : avatar_pkg.mesh_payloads) {
        GpuMeshResource mesh {};
        const avatar::SkinRenderPayload* skin_payload = nullptr;
        const avatar::SkeletonRenderPayload* skeleton_payload = nullptr;
        bool force_static_skinning_fallback = false;
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
                    "W_RENDER: XAV2_SKINNING_FALLBACK_SKIPPED_NO_SKELETON: preserve original vertices.",
                    "XAV2_SKINNING_FALLBACK_SKIPPED_NO_SKELETON");
            }
        }
        if (static_skinning_enabled && skin_payload != nullptr && skeleton_payload != nullptr) {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                PushAvatarWarningUnique(
                    &avatar_it->second,
                    "W_RENDER: SKINNING_MATRIX_CONVENTION_APPLIED: skin=jointPose*inverseBind.",
                    "SKINNING_MATRIX_CONVENTION_APPLIED");
            }
        }
        if (!BuildGpuMeshForPayload(
                payload,
                skin_payload,
                skeleton_payload,
                force_static_skinning_fallback,
                device,
                &mesh)) {
            for (auto& created : meshes) {
                ReleaseGpuMeshResource(&created);
            }
            return false;
        }
        meshes.push_back(mesh);
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
    if (!ShouldApplyExperimentalStaticSkinning()) {
        return true;
    }
    // Temporary safety gate for XAV2 until arm-pose skeleton convention is finalized.
    if (avatar_pkg.source_type == AvatarSourceType::Xav2) {
        return true;
    }
    auto mesh_it = renderer->avatar_meshes.find(handle);
    if (mesh_it == renderer->avatar_meshes.end() || mesh_it->second.empty()) {
        return true;
    }
    if (avatar_pkg.skin_payloads.empty() || avatar_pkg.skeleton_payloads.empty() || avatar_pkg.skeleton_rig_payloads.empty()) {
        return true;
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
    const auto left_shoulder_pose = GetPoseOffset(static_cast<std::uint32_t>(NC_POSE_BONE_LEFT_SHOULDER));
    const auto right_shoulder_pose = GetPoseOffset(static_cast<std::uint32_t>(NC_POSE_BONE_RIGHT_SHOULDER));
    const auto left_lower_arm_pose = GetPoseOffset(static_cast<std::uint32_t>(NC_POSE_BONE_LEFT_LOWER_ARM));
    const auto right_lower_arm_pose = GetPoseOffset(static_cast<std::uint32_t>(NC_POSE_BONE_RIGHT_LOWER_ARM));
    const auto left_hand_pose = GetPoseOffset(static_cast<std::uint32_t>(NC_POSE_BONE_LEFT_HAND));
    const auto right_hand_pose = GetPoseOffset(static_cast<std::uint32_t>(NC_POSE_BONE_RIGHT_HAND));
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
    auto& meshes = mesh_it->second;
    auto compute_position_stats = [](const std::vector<std::uint8_t>& blob, std::uint32_t stride) {
        struct Stats {
            float max_abs = 0.0f;
            float extent_max = 0.0f;
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
        s.extent_max = std::max(ex, std::max(ey, ez));
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
        apply_humanoid_pose(avatar::HumanoidBoneId::LeftShoulder, left_shoulder_pose);
        apply_humanoid_pose(avatar::HumanoidBoneId::RightShoulder, right_shoulder_pose);
        apply_humanoid_pose(avatar::HumanoidBoneId::LeftLowerArm, left_lower_arm_pose);
        apply_humanoid_pose(avatar::HumanoidBoneId::RightLowerArm, right_lower_arm_pose);
        apply_humanoid_pose(avatar::HumanoidBoneId::LeftHand, left_hand_pose);
        apply_humanoid_pose(avatar::HumanoidBoneId::RightHand, right_hand_pose);

        avatar::SkeletonRenderPayload posed_skeleton;
        posed_skeleton.mesh_name = skeleton_payload->mesh_name;
        posed_skeleton.bone_matrices_16xn = std::move(posed_bone_matrices);

        const auto pre_stats = compute_position_stats(mesh.bind_pose_vertex_blob, mesh.vertex_stride);
        auto posed_vertices = mesh.bind_pose_vertex_blob;
        if (!ApplyStaticSkinningToVertexBlob(&posed_vertices, mesh.vertex_stride, *skin_payload, &posed_skeleton)) {
            continue;
        }
        const auto post_stats = compute_position_stats(posed_vertices, mesh.vertex_stride);
        const float pre_extent = std::max(0.0001f, pre_stats.extent_max);
        const bool exploded_extent = post_stats.extent_max > (pre_extent * 20.0f);
        const bool exploded_abs = post_stats.max_abs > std::max(200.0f, pre_stats.max_abs * 20.0f);
        if (!post_stats.finite || exploded_extent || exploded_abs) {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                PushAvatarWarningUnique(
                    &avatar_it->second,
                    "W_RENDER: XAV2_SKINNING_EXTENT_GUARD: posed mesh rejected; keep bind pose.",
                    "XAV2_SKINNING_EXTENT_GUARD");
            }
            posed_vertices = mesh.bind_pose_vertex_blob;
        }
        mesh.base_vertex_blob = std::move(posed_vertices);
        mesh.deformed_vertex_blob = mesh.base_vertex_blob;
        RecomputeMeshBoundsFromVertexBlob(&mesh);
        (void)UploadMeshVertexBlob(&mesh, device_ctx);
        any_mesh_updated = true;
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
    if (typed_encoding == "typed-v2" || typed_encoding == "typed-v3") {
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
    for (const auto& tex : avatar_pkg.texture_payloads) {
        textures_by_key[NormalizeRefKey(tex.name)] = &tex;
    };
    auto resolve_texture_payload = [&](const std::string& texture_ref) -> const avatar::TextureRenderPayload* {
        if (texture_ref.empty()) {
            return nullptr;
        }
        const std::string key = NormalizeRefKey(texture_ref);
        const auto exact_it = textures_by_key.find(key);
        if (exact_it != textures_by_key.end()) {
            return exact_it->second;
        }
        return nullptr;
    };
    for (const auto& payload : avatar_pkg.material_payloads) {
        GpuMaterialResource material {};
        const bool conservative_xav2_material = (avatar_pkg.source_type == AvatarSourceType::Xav2);
        const bool is_vrm_source = avatar_pkg.source_type == AvatarSourceType::Vrm;
        const char* unresolved_texture_code = is_vrm_source
            ? "VRM_MATERIAL_TEXTURE_UNRESOLVED"
            : "XAV2_MATERIAL_TYPED_TEXTURE_UNRESOLVED";
        std::vector<std::string> fallback_reasons;
        const std::string shader_family = NormalizeShaderFamilyKey(payload.shader_family);
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
        material.alpha_mode = ResolveAlphaMode(payload);
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
            material.shade_mix = 0.28f;
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
            const auto* tex_payload = resolve_texture_payload(base_texture_ref);
            if (tex_payload != nullptr) {
                material.base_color_srv = CreateTextureSrvFromPayload(device, tex_payload);
            } else if (has_typed_base_ref) {
                unresolved_base_texture = true;
                std::ostringstream warning;
                warning << "W_RENDER: " << unresolved_texture_code << ": material=" << payload.name
                        << ", slot=base, ref=" << base_texture_ref;
                auto avatar_it = g_state.avatars.find(handle);
                if (avatar_it != g_state.avatars.end()) {
                    avatar_it->second.warnings.push_back(warning.str());
                    avatar_it->second.warning_codes.push_back(unresolved_texture_code);
                }
            } else {
                unresolved_base_texture = true;
            }
        }

        std::string normal_texture_ref;
        const bool has_typed_normal_ref =
            TryGetTypedTextureRef(payload, "normal", &normal_texture_ref) ||
            TryGetTypedTextureRef(payload, "_BumpMap", &normal_texture_ref);
        if (!conservative_xav2_material && !normal_texture_ref.empty()) {
            const auto* tex_payload = resolve_texture_payload(normal_texture_ref);
            if (tex_payload != nullptr) {
                material.normal_srv = CreateTextureSrvFromPayload(device, tex_payload);
                if (material.normal_strength < 0.01f) {
                    material.normal_strength = 0.35f;
                }
            } else if (has_typed_normal_ref) {
                unresolved_normal_texture = true;
                std::ostringstream warning;
                warning << "W_RENDER: " << unresolved_texture_code << ": material=" << payload.name
                        << ", slot=normal, ref=" << normal_texture_ref;
                auto avatar_it = g_state.avatars.find(handle);
                if (avatar_it != g_state.avatars.end()) {
                    avatar_it->second.warnings.push_back(warning.str());
                    avatar_it->second.warning_codes.push_back(unresolved_texture_code);
                }
            } else {
                unresolved_normal_texture = true;
            }
        }

        std::string rim_texture_ref;
        const bool has_typed_rim_ref =
            TryGetTypedTextureRef(payload, "rim", &rim_texture_ref) ||
            TryGetTypedTextureRef(payload, "_RimTex", &rim_texture_ref);
        if (!conservative_xav2_material && !rim_texture_ref.empty()) {
            const auto* tex_payload = resolve_texture_payload(rim_texture_ref);
            if (tex_payload != nullptr) {
                material.rim_srv = CreateTextureSrvFromPayload(device, tex_payload);
                material.rim_strength = std::max(0.35f, material.rim_strength);
            } else if (has_typed_rim_ref) {
                unresolved_rim_texture = true;
                std::ostringstream warning;
                warning << "W_RENDER: " << unresolved_texture_code << ": material=" << payload.name
                        << ", slot=rim, ref=" << rim_texture_ref;
                auto avatar_it = g_state.avatars.find(handle);
                if (avatar_it != g_state.avatars.end()) {
                    avatar_it->second.warnings.push_back(warning.str());
                    avatar_it->second.warning_codes.push_back(unresolved_texture_code);
                }
            } else {
                unresolved_rim_texture = true;
            }
        }
        std::string emission_texture_ref;
        const bool has_typed_emission_ref =
            TryGetTypedTextureRef(payload, "emission", &emission_texture_ref) ||
            TryGetTypedTextureRef(payload, "_EmissionMap", &emission_texture_ref);
        if (!conservative_xav2_material && !emission_texture_ref.empty()) {
            const auto* tex_payload = resolve_texture_payload(emission_texture_ref);
            if (tex_payload != nullptr) {
                material.emission_srv = CreateTextureSrvFromPayload(device, tex_payload);
                material.emission_strength = std::max(material.emission_strength, 0.75f);
            } else if (has_typed_emission_ref) {
                unresolved_emission_texture = true;
                std::ostringstream warning;
                warning << "W_RENDER: " << unresolved_texture_code << ": material=" << payload.name
                        << ", slot=emission, ref=" << emission_texture_ref;
                auto avatar_it = g_state.avatars.find(handle);
                if (avatar_it != g_state.avatars.end()) {
                    avatar_it->second.warnings.push_back(warning.str());
                    avatar_it->second.warning_codes.push_back(unresolved_texture_code);
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
        if (!conservative_xav2_material && !matcap_texture_ref.empty()) {
            const auto* tex_payload = resolve_texture_payload(matcap_texture_ref);
            if (tex_payload != nullptr) {
                material.matcap_srv = CreateTextureSrvFromPayload(device, tex_payload);
                material.matcap_strength = std::max(material.matcap_strength, 0.35f);
            } else if (has_typed_matcap_ref) {
                unresolved_matcap_texture = true;
                std::ostringstream warning;
                warning << "W_RENDER: " << unresolved_texture_code << ": material=" << payload.name
                        << ", slot=matcap, ref=" << matcap_texture_ref;
                auto avatar_it = g_state.avatars.find(handle);
                if (avatar_it != g_state.avatars.end()) {
                    avatar_it->second.warnings.push_back(warning.str());
                    avatar_it->second.warning_codes.push_back(unresolved_texture_code);
                }
            } else {
                unresolved_matcap_texture = true;
            }
        }
        std::string uv_anim_mask_ref;
        const bool has_typed_uv_mask_ref =
            TryGetTypedTextureRef(payload, "uvAnimationMask", &uv_anim_mask_ref) ||
            TryGetTypedTextureRef(payload, "_UvAnimMaskTex", &uv_anim_mask_ref);
        if (!conservative_xav2_material && !uv_anim_mask_ref.empty()) {
            const auto* tex_payload = resolve_texture_payload(uv_anim_mask_ref);
            if (tex_payload != nullptr) {
                material.uv_anim_mask_srv = CreateTextureSrvFromPayload(device, tex_payload);
                material.uv_anim_enabled = true;
            } else if (has_typed_uv_mask_ref) {
                unresolved_uv_mask_texture = true;
                std::ostringstream warning;
                warning << "W_RENDER: " << unresolved_texture_code << ": material=" << payload.name
                        << ", slot=uvAnimationMask, ref=" << uv_anim_mask_ref;
                auto avatar_it = g_state.avatars.find(handle);
                if (avatar_it != g_state.avatars.end()) {
                    avatar_it->second.warnings.push_back(warning.str());
                    avatar_it->second.warning_codes.push_back(unresolved_texture_code);
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
        if (conservative_xav2_material) {
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
                : "XAV2_MATERIAL_FALLBACK_APPLIED";
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
    g_state.osc.Publish("/VsfClone/Tracking/BlinkL", g_state.latest_tracking.blink_l);
    g_state.osc.Publish("/VsfClone/Tracking/BlinkR", g_state.latest_tracking.blink_r);
    g_state.osc.Publish("/VsfClone/Tracking/MouthOpen", g_state.latest_tracking.mouth_open);
    g_state.osc.Publish("/VsfClone/Tracking/HeadPosX", g_state.latest_tracking.head_pos[0]);
    g_state.osc.Publish("/VsfClone/Tracking/HeadPosY", g_state.latest_tracking.head_pos[1]);
    g_state.osc.Publish("/VsfClone/Tracking/HeadPosZ", g_state.latest_tracking.head_pos[2]);
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
    const float frame_dt = std::max(1.0f / 240.0f, std::min(1.0f / 15.0f, ctx->delta_time_seconds));
    g_state.runtime_time_seconds =
        std::fmod(std::max(0.0f, g_state.runtime_time_seconds + frame_dt), 3600.0f);
    const float clear_color[4] = {
        quality.background_rgba[0],
        quality.background_rgba[1],
        quality.background_rgba[2],
        quality.background_rgba[3]};
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
    device_ctx->PSSetShader(renderer.pixel_shader, nullptr, 0U);
    device_ctx->VSSetConstantBuffers(0U, 1U, &renderer.constant_buffer);
    device_ctx->PSSetConstantBuffers(0U, 1U, &renderer.constant_buffer);

    struct DrawItem {
        std::uint64_t handle = 0U;
        std::size_t mesh_index = 0U;
        const AvatarPackage* pkg = nullptr;
        GpuMeshResource* mesh = nullptr;
        GpuMaterialResource* material = nullptr;
        DirectX::XMMATRIX world = DirectX::XMMatrixIdentity();
        float view_z = 0.0f;
        bool is_blend = false;
        bool is_mask = false;
    };
    std::vector<DrawItem> opaque_draws;
    std::vector<DrawItem> mask_draws;
    std::vector<DrawItem> blend_draws;
    std::vector<DrawItem> outline_draws;
    std::uint32_t frame_draw_calls = 0U;
    const float fov_deg = quality.fov_deg;
    const float tan_half_fov = std::tan(DirectX::XMConvertToRadians(fov_deg) * 0.5f);
    const float camera_distance =
        (quality.camera_mode == NC_CAMERA_MODE_AUTO_FIT_BUST) ? 2.7f : 3.2f;
    const float look_at_y = quality.headroom * 0.6f;
    const auto view = ComputeViewMatrix(camera_distance, look_at_y, quality.yaw_deg);
    const auto proj = ComputeProjectionMatrix(ctx->width, ctx->height, fov_deg);
    std::uint32_t avatar_slot = 0U;
    for (const auto handle : g_state.render_ready_avatars) {
        auto it = g_state.avatars.find(handle);
        if (it == g_state.avatars.end()) {
            continue;
        }
        const auto material_resolve_begin = std::chrono::steady_clock::now();
        if (!EnsureAvatarGpuMeshes(&renderer, it->second, handle, device)) {
            SetError(NC_ERROR_INTERNAL, "render", "failed to upload mesh payloads to GPU", true);
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
        const float bounds_cluster_distance_threshold = std::max(2.2f, median_extent * 3.0f);
        std::vector<std::uint8_t> preview_bounds_excluded(mesh_it->second.size(), 0U);
        if (it->second.source_type == AvatarSourceType::Xav2) {
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
        if (it->second.source_type == AvatarSourceType::Xav2 && !center_x_samples.empty()) {
            for (const auto& sample : extent_samples) {
                if (preview_bounds_excluded[sample.index] != 0U) {
                    continue;
                }
                if (sample.extent > extent_threshold) {
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
        if (it->second.source_type == AvatarSourceType::Xav2) {
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
        const float max_extent = std::max(extent_x, std::max(extent_y, extent_z));
        float fit_scale = 1.4f / max_extent;
        if (quality.camera_mode == NC_CAMERA_MODE_AUTO_FIT_FULL || quality.camera_mode == NC_CAMERA_MODE_AUTO_FIT_BUST) {
            const float fit_basis_height =
                (quality.camera_mode == NC_CAMERA_MODE_AUTO_FIT_BUST)
                    ? std::max(extent_y * 0.58f, 0.0001f)
                    : extent_y;
            const float desired = quality.framing_target;
            fit_scale = (desired * 2.0f * camera_distance * std::max(0.01f, tan_half_fov)) / fit_basis_height;
        }
        // Some imported assets can carry very large coordinate ranges.
        // Keep tiny fit scales instead of forcing a 0.05 floor, otherwise
        // those avatars remain outside the camera frustum.
        fit_scale = std::max(1.0e-7f, std::min(50.0f, fit_scale));
        const float cx = (avatar_bmin.x + avatar_bmax.x) * 0.5f;
        const float cy = (avatar_bmin.y + avatar_bmax.y) * 0.5f;
        const float cz = (avatar_bmin.z + avatar_bmax.z) * 0.5f;
        const float focus_y =
            (quality.camera_mode == NC_CAMERA_MODE_AUTO_FIT_BUST)
                ? (avatar_bmin.y + extent_y * (0.68f + quality.headroom * 0.2f))
                : cy;
        // Keep preview centered even if multiple handles are present.
        // Host UI currently operates in single-avatar mode, and slot offsets
        // can push the visible avatar out of frame after reload/recovery paths.
        const float x_offset = 0.0f;
        const float preview_yaw_override = PreviewYawRadiansForAvatarPackage(it->second, nullptr);
        {
            std::ostringstream preview_debug;
            preview_debug << "extent=(" << extent_x << "/" << extent_y << "/" << extent_z
                          << "), fit_scale=" << fit_scale
                          << ", center=(" << cx << "/" << cy << "/" << cz << ")"
                          << ", bounds_meshes=" << included_bounds_mesh_count
                          << "/" << mesh_it->second.size()
                          << ", bounds_excluded=" << excluded_bounds_mesh_count
                          << ", near_origin=" << (near_origin_bounds_used ? "1" : "0")
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
        const auto world =
            DirectX::XMMatrixTranslation(-cx, -focus_y, -cz) *
            DirectX::XMMatrixRotationY(preview_yaw_override) *
            composed_head_rot *
            DirectX::XMMatrixScaling(fit_scale, fit_scale, fit_scale) *
            head_pos *
            DirectX::XMMatrixTranslation(x_offset, -look_at_y, 0.0f);
        ++avatar_slot;
        std::uint32_t material_index_oob_count = 0U;
        std::uint32_t mesh_extent_outlier_skipped_count = 0U;
        std::uint32_t bounds_outlier_excluded_count = 0U;
        for (std::size_t mesh_index = 0U; mesh_index < mesh_it->second.size(); ++mesh_index) {
            auto& mesh = mesh_it->second[mesh_index];
            if (it->second.source_type == AvatarSourceType::Xav2) {
                if (mesh_index < preview_bounds_excluded.size() && preview_bounds_excluded[mesh_index] != 0U) {
                    ++bounds_outlier_excluded_count;
                    continue;
                }
                const float ex = std::max(mesh.bounds_max.x - mesh.bounds_min.x, 0.0f);
                const float ey = std::max(mesh.bounds_max.y - mesh.bounds_min.y, 0.0f);
                const float ez = std::max(mesh.bounds_max.z - mesh.bounds_min.z, 0.0f);
                const float emax = std::max(ex, std::max(ey, ez));
                if (std::isfinite(emax) && emax > draw_extent_threshold) {
                    ++mesh_extent_outlier_skipped_count;
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
            item.world = world;
            item.is_mask = (alpha_mode == "MASK");
            item.is_blend = (alpha_mode == "BLEND");
            const auto center = DirectX::XMVectorSet(mesh.center.x, mesh.center.y, mesh.center.z, 1.0f);
            const auto center_view = DirectX::XMVector3TransformCoord(DirectX::XMVector3TransformCoord(center, world), view);
            item.view_z = DirectX::XMVectorGetZ(center_view);
            if (item.is_blend) {
                blend_draws.push_back(item);
            } else if (item.is_mask) {
                mask_draws.push_back(item);
            } else {
                opaque_draws.push_back(item);
            }
            if (material != nullptr && material->outline_width > 0.0005f) {
                outline_draws.push_back(item);
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
        if (mesh_extent_outlier_skipped_count > 0U) {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                std::ostringstream warning;
                warning << "W_RENDER: XAV2_MESH_EXTENT_OUTLIER_SKIPPED: meshes=" << mesh_extent_outlier_skipped_count;
                PushAvatarWarningUnique(&avatar_it->second, warning.str(), "XAV2_MESH_EXTENT_OUTLIER_SKIPPED");
            }
        }
        if (bounds_outlier_excluded_count > 0U) {
            auto avatar_it = g_state.avatars.find(handle);
            if (avatar_it != g_state.avatars.end()) {
                std::ostringstream warning;
                warning << "W_RENDER: XAV2_BOUNDS_OUTLIER_EXCLUDED: meshes=" << bounds_outlier_excluded_count;
                if (!excluded_mesh_names.empty()) {
                    warning << ", names=";
                    for (std::size_t i = 0U; i < excluded_mesh_names.size(); ++i) {
                        if (i > 0U) {
                            warning << "|";
                        }
                        warning << excluded_mesh_names[i];
                    }
                }
                PushAvatarWarningUnique(&avatar_it->second, warning.str(), "XAV2_BOUNDS_OUTLIER_EXCLUDED");
            }
        }
    }
    std::sort(blend_draws.begin(), blend_draws.end(), [](const DrawItem& a, const DrawItem& b) {
        const float dz = std::abs(a.view_z - b.view_z);
        if (dz > 1e-4f) {
            return a.view_z > b.view_z;
        }
        return a.mesh_index < b.mesh_index;
    });

    struct alignas(16) SceneConstants {
        float world_view_proj[16];
        float base_color[4];
        float shade_color[4];
        float emission_color[4];
        float rim_color[4];
        float matcap_color[4];
        float liltoon_mix[4];
        float liltoon_params[4];
        float liltoon_aux[4];
        float alpha_misc[4];
        float outline_params[4];
        float uv_anim_params[4];
        float time_params[4];
    };
    auto draw_pass = [&](const DrawItem& item, bool outline_pass) {
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
        const bool is_mask = (alpha_mode == "MASK");
        const bool is_blend = (alpha_mode == "BLEND");
        const bool force_no_cull_for_avatar =
            (item.pkg != nullptr &&
                (item.pkg->source_type == AvatarSourceType::Xav2 ||
                 item.pkg->source_type == AvatarSourceType::Vrm));
        const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        if (is_blend) {
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
            if (double_sided || force_no_cull_for_avatar) {
                device_ctx->RSSetState(renderer.raster_cull_none);
            } else {
                device_ctx->RSSetState(renderer.raster_cull_front);
            }
        } else {
            device_ctx->RSSetState((double_sided || force_no_cull_for_avatar) ? renderer.raster_cull_none : renderer.raster_cull_back);
        }

        const UINT stride = item.mesh->vertex_stride;
        const UINT offset = 0U;
        device_ctx->IASetVertexBuffers(0U, 1U, &item.mesh->vertex_buffer, &stride, &offset);
        device_ctx->IASetIndexBuffer(item.mesh->index_buffer, DXGI_FORMAT_R32_UINT, 0U);
        device_ctx->PSSetSamplers(0U, 1U, &renderer.linear_sampler);
        ID3D11ShaderResourceView* srvs[6] = {base_srv, normal_srv, rim_srv, emission_srv, matcap_srv, uv_mask_srv};
        device_ctx->PSSetShaderResources(0U, 6U, srvs);

        const auto world_view_proj = item.world * view * proj;
        const auto world_view_proj_t = DirectX::XMMatrixTranspose(world_view_proj);
        SceneConstants cb {};
        DirectX::XMFLOAT4X4 wvp_store {};
        DirectX::XMStoreFloat4x4(&wvp_store, world_view_proj_t);
        std::memcpy(cb.world_view_proj, &wvp_store, sizeof(cb.world_view_proj));
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
            cb.outline_params[2] = 0.0f;
            cb.outline_params[3] = outline_pass ? 1.0f : 0.0f;
            cb.uv_anim_params[0] = item.material->uv_anim_scroll_x;
            cb.uv_anim_params[1] = item.material->uv_anim_scroll_y;
            cb.uv_anim_params[2] = item.material->uv_anim_rotation;
            cb.uv_anim_params[3] = item.material->uv_anim_enabled ? 1.0f : 0.0f;
            cb.time_params[0] = g_state.runtime_time_seconds;
            cb.time_params[1] = frame_dt;
            cb.time_params[2] = 0.0f;
            cb.time_params[3] = 0.0f;
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
            cb.outline_params[2] = 0.0f;
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
        cb.alpha_misc[0] = is_mask ? alpha_cutoff : 0.0f;
        cb.alpha_misc[1] = is_mask ? 1.0f : 0.0f;
        cb.alpha_misc[2] = base_srv != nullptr ? 1.0f : 0.0f;
        cb.alpha_misc[3] = (is_mask || is_blend) ? 1.0f : 0.0f;

        D3D11_MAPPED_SUBRESOURCE mapped {};
        if (SUCCEEDED(device_ctx->Map(renderer.constant_buffer, 0U, D3D11_MAP_WRITE_DISCARD, 0U, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            device_ctx->Unmap(renderer.constant_buffer, 0U);
        }
        device_ctx->DrawIndexed(item.mesh->index_count, 0U, 0);
        ID3D11ShaderResourceView* null_srvs[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
        device_ctx->PSSetShaderResources(0U, 6U, null_srvs);
        ++frame_draw_calls;
    };
    std::uint32_t frame_pass_count = 0U;
    if (!opaque_draws.empty()) {
        ++frame_pass_count;
    }
    if (!mask_draws.empty()) {
        ++frame_pass_count;
    }
    if (!outline_draws.empty()) {
        ++frame_pass_count;
    }
    if (!blend_draws.empty()) {
        ++frame_pass_count;
    }
    for (const auto& item : opaque_draws) {
        draw_pass(item, false);
    }
    for (const auto& item : mask_draws) {
        draw_pass(item, false);
    }
    for (const auto& item : outline_draws) {
        draw_pass(item, true);
    }
    for (const auto& item : blend_draws) {
        draw_pass(item, false);
    }

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

}  // namespace vsfclone::nativecore

NcResultCode nc_initialize(const NcInitOptions* options) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);

    if (options == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "runtime", "options must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    vsfclone::nativecore::g_state.initialized = true;
    vsfclone::nativecore::g_state.next_avatar_handle = 1;
    vsfclone::nativecore::g_state.avatars.clear();
    vsfclone::nativecore::g_state.secondary_motion_states.clear();
    vsfclone::nativecore::g_state.arm_pose_states.clear();
    vsfclone::nativecore::g_state.render_ready_avatars.clear();
    vsfclone::nativecore::g_state.render_quality = vsfclone::nativecore::MakeDefaultRenderQualityOptions();
    vsfclone::nativecore::g_state.pose_offsets = vsfclone::nativecore::MakeDefaultPoseOffsets();
    vsfclone::nativecore::g_state.last_frame_ms = 0.0f;
    vsfclone::nativecore::g_state.last_gpu_frame_ms = 0.0f;
    vsfclone::nativecore::g_state.last_cpu_frame_ms = 0.0f;
    vsfclone::nativecore::g_state.last_material_resolve_ms = 0.0f;
    vsfclone::nativecore::g_state.last_pass_count = 0U;
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_shutdown(void) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);

    if (!vsfclone::nativecore::g_state.initialized) {
        return NC_OK;
    }

    vsfclone::nativecore::g_state.spout.Stop();
    vsfclone::nativecore::g_state.osc.Close();
#if defined(_WIN32)
    for (auto& [_, state] : vsfclone::nativecore::g_state.window_targets) {
        vsfclone::nativecore::ReleaseWindowState(&state);
    }
    vsfclone::nativecore::g_state.window_targets.clear();
    vsfclone::nativecore::ResetRendererResources(&vsfclone::nativecore::g_state.renderer);
#endif
    vsfclone::nativecore::g_state.avatars.clear();
    vsfclone::nativecore::g_state.arm_pose_states.clear();
    vsfclone::nativecore::g_state.render_ready_avatars.clear();
    vsfclone::nativecore::g_state.render_quality = vsfclone::nativecore::MakeDefaultRenderQualityOptions();
    vsfclone::nativecore::g_state.pose_offsets = vsfclone::nativecore::MakeDefaultPoseOffsets();
    vsfclone::nativecore::g_state.initialized = false;
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_load_avatar(const NcAvatarLoadRequest* request, NcAvatarHandle* out_handle, NcAvatarInfo* out_info) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (request == nullptr || out_handle == nullptr || request->path == nullptr || request->path[0] == '\0') {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "request/path/out_handle must be valid", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    auto loaded = vsfclone::nativecore::g_state.loader.Load(request->path);
    if (!loaded.ok) {
        vsfclone::nativecore::SetError(NC_ERROR_IO, "avatar", loaded.error, true);
        return NC_ERROR_IO;
    }
    vsfclone::nativecore::BuildArkit52ExpressionBindings(&loaded.value);
    if (loaded.value.source_type == vsfclone::avatar::AvatarSourceType::Vrm && loaded.value.expressions.empty()) {
        loaded.value.expressions.push_back({"blink", "blink", 0.0f, 0.0f});
        loaded.value.expressions.push_back({"aa", "viseme_aa", 0.0f, 0.0f});
        loaded.value.expressions.push_back({"joy", "joy", 0.0f, 0.0f});
        loaded.value.warnings.push_back("W_VRM_EXPRESSION_FALLBACK: runtime injected blink/aa/joy expression defaults");
    }

    const std::uint64_t handle = vsfclone::nativecore::g_state.next_avatar_handle++;
    vsfclone::nativecore::g_state.avatars[handle] = loaded.value;
    vsfclone::nativecore::g_state.secondary_motion_states.erase(handle);
    vsfclone::nativecore::g_state.arm_pose_states.erase(handle);
    *out_handle = handle;
    vsfclone::nativecore::FillAvatarInfo(loaded.value, handle, out_info);
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_unload_avatar(NcAvatarHandle handle) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }

    auto it = vsfclone::nativecore::g_state.avatars.find(handle);
    if (it == vsfclone::nativecore::g_state.avatars.end()) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    vsfclone::nativecore::g_state.render_ready_avatars.erase(handle);
    vsfclone::nativecore::g_state.secondary_motion_states.erase(handle);
    vsfclone::nativecore::g_state.arm_pose_states.erase(handle);
#if defined(_WIN32)
    auto mesh_it = vsfclone::nativecore::g_state.renderer.avatar_meshes.find(handle);
    if (mesh_it != vsfclone::nativecore::g_state.renderer.avatar_meshes.end()) {
        for (auto& mesh : mesh_it->second) {
            vsfclone::nativecore::ReleaseGpuMeshResource(&mesh);
        }
        vsfclone::nativecore::g_state.renderer.avatar_meshes.erase(mesh_it);
    }
    auto material_it = vsfclone::nativecore::g_state.renderer.avatar_materials.find(handle);
    if (material_it != vsfclone::nativecore::g_state.renderer.avatar_materials.end()) {
        for (auto& material : material_it->second) {
            vsfclone::nativecore::ReleaseGpuMaterialResource(&material);
        }
        vsfclone::nativecore::g_state.renderer.avatar_materials.erase(material_it);
    }
#endif
    vsfclone::nativecore::g_state.avatars.erase(it);
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_get_avatar_info(NcAvatarHandle handle, NcAvatarInfo* out_info) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (out_info == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "out_info must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    auto it = vsfclone::nativecore::g_state.avatars.find(handle);
    if (it == vsfclone::nativecore::g_state.avatars.end()) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    vsfclone::nativecore::FillAvatarInfo(it->second, handle, out_info);
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_get_expression_count(NcAvatarHandle handle, uint32_t* out_count) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (out_count == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "out_count must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    auto it = vsfclone::nativecore::g_state.avatars.find(handle);
    if (it == vsfclone::nativecore::g_state.avatars.end()) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    *out_count = static_cast<std::uint32_t>(it->second.expressions.size());
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_get_expression_infos(
    NcAvatarHandle handle,
    NcExpressionInfo* out_infos,
    uint32_t capacity,
    uint32_t* out_written) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (out_written == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "out_written must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    auto it = vsfclone::nativecore::g_state.avatars.find(handle);
    if (it == vsfclone::nativecore::g_state.avatars.end()) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    *out_written = 0U;
    const auto& expressions = it->second.expressions;
    const auto to_write = static_cast<std::uint32_t>(std::min<std::size_t>(capacity, expressions.size()));
    if (to_write > 0U && out_infos == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "out_infos must not be null when capacity > 0", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    for (std::uint32_t i = 0U; i < to_write; ++i) {
        std::memset(&out_infos[i], 0, sizeof(NcExpressionInfo));
        const auto& expr = expressions[i];
        vsfclone::nativecore::CopyString(out_infos[i].name, sizeof(out_infos[i].name), expr.name);
        vsfclone::nativecore::CopyString(out_infos[i].mapping_kind, sizeof(out_infos[i].mapping_kind), expr.mapping_kind);
        out_infos[i].default_weight = expr.default_weight;
        out_infos[i].runtime_weight = expr.runtime_weight;
        out_infos[i].bind_count = static_cast<std::uint32_t>(expr.binds.size());
    }
    *out_written = to_write;
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_get_springbone_info(NcAvatarHandle handle, NcSpringBoneInfo* out_info) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (out_info == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "out_info must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    auto it = vsfclone::nativecore::g_state.avatars.find(handle);
    if (it == vsfclone::nativecore::g_state.avatars.end()) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    std::memset(out_info, 0, sizeof(*out_info));
    out_info->present = it->second.springbone_summary.present ? 1U : 0U;
    out_info->spring_count = it->second.springbone_summary.spring_count;
    out_info->joint_count = it->second.springbone_summary.joint_count;
    out_info->collider_count = it->second.springbone_summary.collider_count;
    out_info->collider_group_count = it->second.springbone_summary.collider_group_count;
    const auto state_it = vsfclone::nativecore::g_state.secondary_motion_states.find(handle);
    if (state_it != vsfclone::nativecore::g_state.secondary_motion_states.end()) {
        out_info->active_chain_count = state_it->second.active_chain_count;
        out_info->corrected_chain_count = state_it->second.corrected_chain_count;
        out_info->disabled_chain_count = state_it->second.disabled_chain_count;
        out_info->unsupported_collider_chain_count = state_it->second.unsupported_collider_chain_count;
        out_info->avg_substeps = state_it->second.avg_substeps;
    }
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_get_avatar_runtime_metrics_v2(NcAvatarHandle handle, NcAvatarRuntimeMetricsV2* out_info) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (out_info == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "out_info must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    auto it = vsfclone::nativecore::g_state.avatars.find(handle);
    if (it == vsfclone::nativecore::g_state.avatars.end()) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    vsfclone::nativecore::FillAvatarRuntimeMetricsV2(it->second, handle, out_info);
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_set_tracking_frame(const NcTrackingFrame* frame) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (frame == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "tracking", "frame must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    NcTrackingFrame sanitized = *frame;
    vsfclone::nativecore::SanitizeTrackingFrame(&sanitized);
    vsfclone::nativecore::g_state.latest_tracking = sanitized;
    const float blink_avg = std::max(0.0f, std::min(1.0f, (frame->blink_l + frame->blink_r) * 0.5f));
    const float mouth_open = std::max(0.0f, std::min(1.0f, frame->mouth_open));
    for (auto& [handle, pkg] : vsfclone::nativecore::g_state.avatars) {
        (void)handle;
        if (pkg.expressions.empty()) {
            continue;
        }
        std::string summary;
        std::size_t shown = 0U;
        for (auto& expr : pkg.expressions) {
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
        pkg.last_expression_summary = summary;
    }
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_set_expression_weights(const NcExpressionWeight* weights, uint32_t count) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if ((count > 0U) && weights == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "tracking", "weights must not be null when count > 0", true);
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

    for (auto& [handle, pkg] : vsfclone::nativecore::g_state.avatars) {
        (void)handle;
        if (pkg.expressions.empty()) {
            continue;
        }
        const bool arkit52_mode = vsfclone::nativecore::HasArkit52ExpressionBindings(pkg);
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
                if (vsfclone::nativecore::TryResolveArkitFallbackWeight(expr_name, incoming, &fallback_weight)) {
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
            vsfclone::nativecore::PushAvatarWarningUnique(&pkg, message.str(), "W_ARKIT52_FALLBACK_APPLIED");
        }
    }

    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_create_render_resources(NcAvatarHandle handle) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }

    auto it = vsfclone::nativecore::g_state.avatars.find(handle);
    if (it == vsfclone::nativecore::g_state.avatars.end()) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (it->second.mesh_payloads.empty()) {
        const char* format_name = "unknown";
        switch (it->second.source_type) {
            case vsfclone::avatar::AvatarSourceType::Vrm:
                format_name = "vrm";
                break;
            case vsfclone::avatar::AvatarSourceType::VxAvatar:
                format_name = "vxavatar";
                break;
            case vsfclone::avatar::AvatarSourceType::Vxa2:
                format_name = "vxa2";
                break;
            case vsfclone::avatar::AvatarSourceType::Xav2:
                format_name = "xav2";
                break;
            case vsfclone::avatar::AvatarSourceType::VsfAvatar:
                format_name = "vsfavatar";
                break;
            default:
                break;
        }
        std::ostringstream detail;
        detail << "avatar has no renderable mesh payloads"
               << " (format=" << format_name
               << ", parser_stage=" << (it->second.parser_stage.empty() ? "unknown" : it->second.parser_stage)
               << ", primary_error=" << (it->second.primary_error_code.empty() ? "NONE" : it->second.primary_error_code)
               << ", mesh_count=" << it->second.meshes.size()
               << ", material_count=" << it->second.materials.size()
               << ")";
        vsfclone::nativecore::SetError(
            NC_ERROR_UNSUPPORTED,
            "render",
            detail.str(),
            true);
        return NC_ERROR_UNSUPPORTED;
    }

    vsfclone::nativecore::g_state.render_ready_avatars.insert(handle);
#if defined(_WIN32)
    auto mesh_it = vsfclone::nativecore::g_state.renderer.avatar_meshes.find(handle);
    if (mesh_it != vsfclone::nativecore::g_state.renderer.avatar_meshes.end()) {
        for (auto& mesh : mesh_it->second) {
            vsfclone::nativecore::ReleaseGpuMeshResource(&mesh);
        }
        vsfclone::nativecore::g_state.renderer.avatar_meshes.erase(mesh_it);
    }
    auto material_it = vsfclone::nativecore::g_state.renderer.avatar_materials.find(handle);
    if (material_it != vsfclone::nativecore::g_state.renderer.avatar_materials.end()) {
        for (auto& material : material_it->second) {
            vsfclone::nativecore::ReleaseGpuMaterialResource(&material);
        }
        vsfclone::nativecore::g_state.renderer.avatar_materials.erase(material_it);
    }
#endif
    vsfclone::nativecore::g_state.secondary_motion_states.erase(handle);
    vsfclone::nativecore::g_state.arm_pose_states.erase(handle);
    it->second.last_render_draw_calls = 0U;
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_destroy_render_resources(NcAvatarHandle handle) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }

    auto it = vsfclone::nativecore::g_state.avatars.find(handle);
    if (it == vsfclone::nativecore::g_state.avatars.end()) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    vsfclone::nativecore::g_state.render_ready_avatars.erase(handle);
#if defined(_WIN32)
    auto mesh_it = vsfclone::nativecore::g_state.renderer.avatar_meshes.find(handle);
    if (mesh_it != vsfclone::nativecore::g_state.renderer.avatar_meshes.end()) {
        for (auto& mesh : mesh_it->second) {
            vsfclone::nativecore::ReleaseGpuMeshResource(&mesh);
        }
        vsfclone::nativecore::g_state.renderer.avatar_meshes.erase(mesh_it);
    }
    auto material_it = vsfclone::nativecore::g_state.renderer.avatar_materials.find(handle);
    if (material_it != vsfclone::nativecore::g_state.renderer.avatar_materials.end()) {
        for (auto& material : material_it->second) {
            vsfclone::nativecore::ReleaseGpuMaterialResource(&material);
        }
        vsfclone::nativecore::g_state.renderer.avatar_materials.erase(material_it);
    }
#endif
    vsfclone::nativecore::g_state.secondary_motion_states.erase(handle);
    vsfclone::nativecore::g_state.arm_pose_states.erase(handle);
    it->second.last_render_draw_calls = 0U;
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_render_frame(const NcRenderContext* ctx) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    return vsfclone::nativecore::RenderFrameLocked(ctx);
}

NcResultCode nc_create_window_render_target(const NcWindowRenderTarget* target) {
#if !defined(_WIN32)
    (void)target;
    return NC_ERROR_UNSUPPORTED;
#else
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (target == nullptr || target->hwnd == nullptr || target->width == 0U || target->height == 0U) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "window target is invalid", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    auto& state = vsfclone::nativecore::g_state.window_targets[target->hwnd];
    vsfclone::nativecore::ReleaseWindowState(&state);

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

    D3D_FEATURE_LEVEL feature_levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};
    D3D_FEATURE_LEVEL selected_level = D3D_FEATURE_LEVEL_11_0;

    const UINT device_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
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
        vsfclone::nativecore::g_state.window_targets.erase(target->hwnd);
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "render", "failed to create d3d11 device/swapchain", true);
        return NC_ERROR_INTERNAL;
    }

    ID3D11Texture2D* backbuffer = nullptr;
    HRESULT bb_hr = state.swap_chain->GetBuffer(0U, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuffer));
    if (FAILED(bb_hr) || backbuffer == nullptr) {
        vsfclone::nativecore::ReleaseWindowState(&state);
        vsfclone::nativecore::g_state.window_targets.erase(target->hwnd);
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "render", "failed to fetch swapchain backbuffer", true);
        return NC_ERROR_INTERNAL;
    }

    HRESULT rtv_hr = state.device->CreateRenderTargetView(backbuffer, nullptr, &state.rtv);
    backbuffer->Release();
    if (FAILED(rtv_hr) || state.rtv == nullptr) {
        vsfclone::nativecore::ReleaseWindowState(&state);
        vsfclone::nativecore::g_state.window_targets.erase(target->hwnd);
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "render", "failed to create render target view", true);
        return NC_ERROR_INTERNAL;
    }

    state.width = target->width;
    state.height = target->height;

    vsfclone::nativecore::ClearError();
    return NC_OK;
#endif
}

NcResultCode nc_resize_window_render_target(const NcWindowRenderTarget* target) {
#if !defined(_WIN32)
    (void)target;
    return NC_ERROR_UNSUPPORTED;
#else
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (target == nullptr || target->hwnd == nullptr || target->width == 0U || target->height == 0U) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "window target is invalid", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    auto it = vsfclone::nativecore::g_state.window_targets.find(target->hwnd);
    if (it == vsfclone::nativecore::g_state.window_targets.end()) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "unknown window render target", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    auto& state = it->second;
    if (state.swap_chain == nullptr || state.device == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "render", "window render target is not initialized", true);
        return NC_ERROR_INTERNAL;
    }

    if (state.rtv != nullptr) {
        state.rtv->Release();
        state.rtv = nullptr;
    }

    HRESULT resize_hr = state.swap_chain->ResizeBuffers(0U, target->width, target->height, DXGI_FORMAT_UNKNOWN, 0U);
    if (FAILED(resize_hr)) {
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "render", "swapchain resize failed", true);
        return NC_ERROR_INTERNAL;
    }

    ID3D11Texture2D* backbuffer = nullptr;
    HRESULT bb_hr = state.swap_chain->GetBuffer(0U, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuffer));
    if (FAILED(bb_hr) || backbuffer == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "render", "failed to fetch resized backbuffer", true);
        return NC_ERROR_INTERNAL;
    }

    HRESULT rtv_hr = state.device->CreateRenderTargetView(backbuffer, nullptr, &state.rtv);
    backbuffer->Release();
    if (FAILED(rtv_hr) || state.rtv == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "render", "failed to create resized render target view", true);
        return NC_ERROR_INTERNAL;
    }

    state.width = target->width;
    state.height = target->height;
    vsfclone::nativecore::ClearError();
    return NC_OK;
#endif
}

NcResultCode nc_destroy_window_render_target(void* hwnd) {
#if !defined(_WIN32)
    (void)hwnd;
    return NC_ERROR_UNSUPPORTED;
#else
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (hwnd == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "hwnd must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    auto it = vsfclone::nativecore::g_state.window_targets.find(hwnd);
    if (it == vsfclone::nativecore::g_state.window_targets.end()) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "unknown window render target", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    vsfclone::nativecore::ReleaseWindowState(&it->second);
    vsfclone::nativecore::g_state.window_targets.erase(it);
    vsfclone::nativecore::ClearError();
    return NC_OK;
#endif
}

NcResultCode nc_render_frame_to_window(void* hwnd, float delta_time_seconds) {
#if !defined(_WIN32)
    (void)hwnd;
    (void)delta_time_seconds;
    return NC_ERROR_UNSUPPORTED;
#else
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (hwnd == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "hwnd must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    auto it = vsfclone::nativecore::g_state.window_targets.find(hwnd);
    if (it == vsfclone::nativecore::g_state.window_targets.end()) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "unknown window render target", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    auto& state = it->second;
    if (state.device == nullptr || state.device_context == nullptr || state.rtv == nullptr || state.swap_chain == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "render", "window render target is incomplete", true);
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

    NcResultCode rc = vsfclone::nativecore::RenderFrameLocked(&ctx);
    if (rc != NC_OK) {
        return rc;
    }

    const HRESULT present_hr = state.swap_chain->Present(1U, 0U);
    if (FAILED(present_hr)) {
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "render", "swapchain present failed", true);
        return NC_ERROR_INTERNAL;
    }

    vsfclone::nativecore::ClearError();
    return NC_OK;
#endif
}

NcResultCode nc_render_avatar_thumbnail_png(const NcThumbnailRequest* request) {
#if !defined(_WIN32)
    (void)request;
    return NC_ERROR_UNSUPPORTED;
#else
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (request == nullptr ||
        request->handle == 0U ||
        request->output_path == nullptr ||
        request->output_path[0] == '\0' ||
        request->width == 0U ||
        request->height == 0U) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "thumbnail", "thumbnail request is invalid", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (request->width > 4096U || request->height > 4096U) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "thumbnail", "thumbnail size exceeds max 4096x4096", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (vsfclone::nativecore::g_state.avatars.find(request->handle) == vsfclone::nativecore::g_state.avatars.end()) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "thumbnail", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (vsfclone::nativecore::g_state.render_ready_avatars.find(request->handle) == vsfclone::nativecore::g_state.render_ready_avatars.end()) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "thumbnail", "avatar render resources are not ready", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    D3D_FEATURE_LEVEL feature_levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};
    D3D_FEATURE_LEVEL selected_level = D3D_FEATURE_LEVEL_11_0;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* device_ctx = nullptr;
    const HRESULT device_hr = D3D11CreateDevice(
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
        }
        if (device != nullptr) {
            device->Release();
        }
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "thumbnail", "failed to create thumbnail d3d11 device", true);
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
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "thumbnail", "failed to create thumbnail render target texture", true);
        return NC_ERROR_INTERNAL;
    }

    ID3D11RenderTargetView* rtv = nullptr;
    const HRESULT rtv_hr = device->CreateRenderTargetView(target_texture, nullptr, &rtv);
    if (FAILED(rtv_hr) || rtv == nullptr) {
        target_texture->Release();
        device_ctx->Release();
        device->Release();
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "thumbnail", "failed to create thumbnail render target view", true);
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

    const auto previous_ready = vsfclone::nativecore::g_state.render_ready_avatars;
    vsfclone::nativecore::g_state.render_ready_avatars.clear();
    vsfclone::nativecore::g_state.render_ready_avatars.insert(request->handle);
    const NcResultCode render_rc = vsfclone::nativecore::RenderFrameLocked(&ctx);
    vsfclone::nativecore::g_state.render_ready_avatars = previous_ready;
    if (render_rc != NC_OK) {
        rtv->Release();
        target_texture->Release();
        device_ctx->Release();
        device->Release();
        return render_rc;
    }

    std::vector<std::uint8_t> pixels;
    if (!vsfclone::nativecore::CaptureRtvBgra(device, device_ctx, rtv, request->width, request->height, &pixels)) {
        rtv->Release();
        target_texture->Release();
        device_ctx->Release();
        device->Release();
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "thumbnail", "failed to capture thumbnail pixels", true);
        return NC_ERROR_INTERNAL;
    }
    if (!vsfclone::nativecore::EncodeBgraToPngFile(pixels, request->width, request->height, request->output_path)) {
        rtv->Release();
        target_texture->Release();
        device_ctx->Release();
        device->Release();
        vsfclone::nativecore::SetError(NC_ERROR_IO, "thumbnail", "failed to encode thumbnail png", true);
        return NC_ERROR_IO;
    }

    rtv->Release();
    target_texture->Release();
    device_ctx->Release();
    device->Release();
    vsfclone::nativecore::ClearError();
    return NC_OK;
#endif
}

NcResultCode nc_set_render_quality_options(const NcRenderQualityOptions* options) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (options == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "options must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    vsfclone::nativecore::g_state.render_quality = vsfclone::nativecore::SanitizeRenderQualityOptions(*options);
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_get_render_quality_options(NcRenderQualityOptions* out_options) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (out_options == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "out_options must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    *out_options = vsfclone::nativecore::SanitizeRenderQualityOptions(vsfclone::nativecore::g_state.render_quality);
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_set_pose_offsets(const NcPoseBoneOffset* offsets, uint32_t count) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if ((count > 0U) && offsets == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "pose", "offsets must not be null when count > 0", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    auto next = vsfclone::nativecore::MakeDefaultPoseOffsets();
    for (uint32_t i = 0U; i < count; ++i) {
        const auto sanitized = vsfclone::nativecore::SanitizePoseOffset(offsets[i]);
        if (!vsfclone::nativecore::IsValidPoseBoneId(sanitized.bone_id)) {
            continue;
        }
        next[sanitized.bone_id] = sanitized;
    }
    vsfclone::nativecore::g_state.pose_offsets = next;
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_clear_pose_offsets(void) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    vsfclone::nativecore::g_state.pose_offsets = vsfclone::nativecore::MakeDefaultPoseOffsets();
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_start_spout(const NcSpoutOptions* options) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (options == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "spout", "options must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    vsfclone::stream::StreamConfig cfg;
    cfg.width = options->width > 0U ? options->width : 1920U;
    cfg.height = options->height > 0U ? options->height : 1080U;
    cfg.fps = options->fps > 0U ? options->fps : 60U;
    cfg.channel_name = (options->channel_name != nullptr && options->channel_name[0] != '\0') ? options->channel_name : "VsfClone";

    if (!vsfclone::nativecore::g_state.spout.Start(cfg)) {
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "spout", "failed to start spout output", true);
        return NC_ERROR_INTERNAL;
    }
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_stop_spout(void) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    vsfclone::nativecore::g_state.spout.Stop();
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_start_osc(const NcOscOptions* options) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (options == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "osc", "options must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    const std::uint16_t port = options->bind_port > 0U ? options->bind_port : 39539U;
    if (!vsfclone::nativecore::g_state.osc.SetDestination(
            (options->publish_address != nullptr && options->publish_address[0] != '\0')
                ? options->publish_address
                : "127.0.0.1:39539")) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "osc", "invalid publish_address format", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (!vsfclone::nativecore::g_state.osc.Bind(port)) {
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "osc", "failed to bind osc endpoint", true);
        return NC_ERROR_INTERNAL;
    }
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_stop_osc(void) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    vsfclone::nativecore::g_state.osc.Close();
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_get_last_error(NcErrorInfo* out_error) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (out_error == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }

    std::memset(out_error, 0, sizeof(*out_error));
    out_error->code = vsfclone::nativecore::g_state.last_error_code;
    vsfclone::nativecore::CopyString(out_error->subsystem, sizeof(out_error->subsystem), vsfclone::nativecore::g_state.last_error_subsystem);
    vsfclone::nativecore::CopyString(out_error->message, sizeof(out_error->message), vsfclone::nativecore::g_state.last_error_message);
    out_error->recoverable = vsfclone::nativecore::g_state.last_error_recoverable ? 1U : 0U;
    return NC_OK;
}

NcResultCode nc_get_runtime_stats(NcRuntimeStats* out_stats) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (out_stats == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "runtime", "out_stats must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    std::memset(out_stats, 0, sizeof(*out_stats));
    out_stats->render_ready_avatar_count = static_cast<std::uint32_t>(vsfclone::nativecore::g_state.render_ready_avatars.size());
    out_stats->spout_active = vsfclone::nativecore::g_state.spout.IsActive() ? 1U : 0U;
    out_stats->osc_active = vsfclone::nativecore::g_state.osc.IsBound() ? 1U : 0U;
    out_stats->last_frame_ms = vsfclone::nativecore::g_state.last_frame_ms;
    out_stats->gpu_frame_ms = vsfclone::nativecore::g_state.last_gpu_frame_ms;
    out_stats->cpu_frame_ms = vsfclone::nativecore::g_state.last_cpu_frame_ms;
    out_stats->material_resolve_ms = vsfclone::nativecore::g_state.last_material_resolve_ms;
    out_stats->pass_count = vsfclone::nativecore::g_state.last_pass_count;
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_get_spout_diagnostics(NcSpoutDiagnostics* out_diag) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (out_diag == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "spout", "out_diag must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    std::memset(out_diag, 0, sizeof(*out_diag));
    out_diag->strict_mode = vsfclone::nativecore::g_state.spout.IsStrictMode() ? 1U : 0U;
    out_diag->fallback_count = vsfclone::nativecore::g_state.spout.FallbackCount();
    const auto backend = vsfclone::nativecore::g_state.spout.ActiveBackendKind();
    switch (backend) {
        case vsfclone::stream::SpoutSender::BackendKind::Spout2Gpu:
            out_diag->backend_kind = NC_SPOUT_BACKEND_SPOUT2_GPU;
            break;
        case vsfclone::stream::SpoutSender::BackendKind::LegacySharedMemory:
            out_diag->backend_kind = NC_SPOUT_BACKEND_LEGACY_SHARED_MEMORY;
            break;
        default:
            out_diag->backend_kind = NC_SPOUT_BACKEND_INACTIVE;
            break;
    }
    vsfclone::nativecore::CopyString(out_diag->last_error_code, sizeof(out_diag->last_error_code), vsfclone::nativecore::g_state.spout.LastErrorCode());
    vsfclone::nativecore::ClearError();
    return NC_OK;
}
