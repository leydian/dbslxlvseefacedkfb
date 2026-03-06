#pragma once

#include <stdint.h>

#if defined(_WIN32)
#if defined(VSFCLONE_NATIVECORE_EXPORTS)
#define VSFCLONE_API __declspec(dllexport)
#else
#define VSFCLONE_API __declspec(dllimport)
#endif
#else
#define VSFCLONE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t NcAvatarHandle;

typedef enum NcResultCode {
    NC_OK = 0,
    NC_ERROR_NOT_INITIALIZED = 1,
    NC_ERROR_INVALID_ARGUMENT = 2,
    NC_ERROR_IO = 3,
    NC_ERROR_UNSUPPORTED = 4,
    NC_ERROR_INTERNAL = 5
} NcResultCode;

typedef enum NcAvatarFormatHint {
    NC_AVATAR_FORMAT_AUTO = 0,
    NC_AVATAR_FORMAT_VRM = 1,
    NC_AVATAR_FORMAT_VXAVATAR = 2,
    NC_AVATAR_FORMAT_VSFAVATAR = 3,
    NC_AVATAR_FORMAT_VXA2 = 4,
    NC_AVATAR_FORMAT_XAV2 = 5
} NcAvatarFormatHint;

typedef enum NcCompatLevel {
    NC_COMPAT_UNKNOWN = 0,
    NC_COMPAT_FULL = 1,
    NC_COMPAT_PARTIAL = 2,
    NC_COMPAT_FAILED = 3
} NcCompatLevel;

typedef struct NcInitOptions {
    uint32_t api_version;
    uint32_t reserved;
} NcInitOptions;

typedef struct NcAvatarLoadRequest {
    const char* path;
    NcAvatarFormatHint format_hint;
    uint32_t shader_profile;
    uint32_t fallback_policy;
} NcAvatarLoadRequest;

typedef struct NcAvatarInfo {
    NcAvatarHandle handle;
    NcAvatarFormatHint detected_format;
    NcCompatLevel compat_level;
    uint32_t mesh_count;
    uint32_t material_count;
    uint32_t mesh_payload_count;
    uint32_t material_payload_count;
    uint32_t texture_payload_count;
    uint32_t format_section_count;
    uint32_t format_decoded_section_count;
    uint32_t format_unknown_section_count;
    uint32_t warning_count;
    uint32_t warning_code_count;
    uint32_t critical_warning_count;
    uint32_t material_diag_count;
    uint32_t opaque_material_count;
    uint32_t mask_material_count;
    uint32_t blend_material_count;
    uint32_t missing_feature_count;
    uint32_t expression_count;
    uint32_t last_render_draw_calls;
    uint32_t spring_active_chain_count;
    uint32_t spring_corrected_chain_count;
    uint32_t spring_disabled_chain_count;
    uint32_t spring_unsupported_collider_chain_count;
    uint32_t mtoon_advanced_param_material_count;
    uint32_t mtoon_fallback_material_count;
    float spring_avg_substeps;
    char display_name[128];
    char source_path[260];
    char parser_stage[32];
    char primary_error_code[64];
    char last_warning_code[64];
    char last_warning_severity[16];
    char last_warning_category[16];
    char last_expression_summary[128];
    char last_warning[256];
    char last_material_diag[256];
    char last_render_pass_summary[128];
    char last_missing_feature[256];
    float parity_score;
    char variant_id[64];
    char parity_fallback_reason[256];
    char quality_mode[16];
    uint32_t family_backend_fallback_count;
    char selected_family_backend[32];
    char active_passes[128];
    uint32_t material_parity_mismatch_count;
    uint32_t texture_resolve_ambiguous_count;
    char material_parity_last_mismatch[256];
} NcAvatarInfo;

typedef struct NcExpressionInfo {
    char name[64];
    char mapping_kind[32];
    float default_weight;
    float runtime_weight;
    uint32_t bind_count;
} NcExpressionInfo;

typedef struct NcSpringBoneInfo {
    uint32_t present;
    uint32_t spring_count;
    uint32_t joint_count;
    uint32_t collider_count;
    uint32_t collider_group_count;
    uint32_t active_chain_count;
    uint32_t corrected_chain_count;
    uint32_t disabled_chain_count;
    uint32_t unsupported_collider_chain_count;
    float avg_substeps;
} NcSpringBoneInfo;

typedef struct NcAvatarRuntimeMetricsV2 {
    uint32_t spring_active_chain_count;
    uint32_t spring_constraint_hit_count;
    uint32_t spring_damping_event_count;
    float spring_avg_offset_magnitude;
    float spring_peak_offset_magnitude;
    uint32_t mtoon_outline_material_count;
    uint32_t mtoon_uv_anim_material_count;
    uint32_t mtoon_matcap_material_count;
    uint32_t mtoon_blend_material_count;
    uint32_t mtoon_mask_material_count;
    float last_frame_ms;
    float target_frame_ms;
    char physics_solver[32];
    char mtoon_runtime_mode[32];
} NcAvatarRuntimeMetricsV2;

typedef struct NcTrackingFrame {
    float head_pos[3];
    float head_rot_quat[4];
    float eye_gaze_l[3];
    float eye_gaze_r[3];
    float blink_l;
    float blink_r;
    float mouth_open;
} NcTrackingFrame;

typedef struct NcExpressionWeight {
    char name[64];
    float weight;
} NcExpressionWeight;

typedef struct NcRenderContext {
    void* hwnd;
    void* d3d11_device;
    void* d3d11_device_context;
    void* d3d11_rtv;
    uint32_t width;
    uint32_t height;
    float delta_time_seconds;
} NcRenderContext;

typedef enum NcCameraMode {
    NC_CAMERA_MODE_AUTO_FIT_FULL = 0,
    NC_CAMERA_MODE_AUTO_FIT_BUST = 1,
    NC_CAMERA_MODE_MANUAL = 2
} NcCameraMode;

typedef enum NcRenderQualityProfile {
    NC_RENDER_QUALITY_DEFAULT = 0,
    NC_RENDER_QUALITY_BALANCED = 1,
    NC_RENDER_QUALITY_ULTRA_PARITY = 2,
    NC_RENDER_QUALITY_FAST_FALLBACK = 3
} NcRenderQualityProfile;

typedef struct NcRenderQualityOptions {
    NcCameraMode camera_mode;
    float framing_target;
    float headroom;
    float yaw_deg;
    float fov_deg;
    float background_rgba[4];
    uint32_t quality_profile;
    uint32_t show_debug_overlay;
} NcRenderQualityOptions;

typedef enum NcPoseBoneId {
    NC_POSE_BONE_UNKNOWN = 0,
    NC_POSE_BONE_HIPS = 1,
    NC_POSE_BONE_SPINE = 2,
    NC_POSE_BONE_CHEST = 3,
    NC_POSE_BONE_UPPER_CHEST = 4,
    NC_POSE_BONE_NECK = 5,
    NC_POSE_BONE_HEAD = 6,
    NC_POSE_BONE_LEFT_UPPER_ARM = 7,
    NC_POSE_BONE_RIGHT_UPPER_ARM = 8,
    NC_POSE_BONE_LEFT_SHOULDER = 9,
    NC_POSE_BONE_RIGHT_SHOULDER = 10,
    NC_POSE_BONE_LEFT_LOWER_ARM = 11,
    NC_POSE_BONE_RIGHT_LOWER_ARM = 12,
    NC_POSE_BONE_LEFT_HAND = 13,
    NC_POSE_BONE_RIGHT_HAND = 14
} NcPoseBoneId;

typedef struct NcPoseBoneOffset {
    uint32_t bone_id;
    float pitch_deg;
    float yaw_deg;
    float roll_deg;
} NcPoseBoneOffset;

typedef struct NcSpoutOptions {
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    const char* channel_name;
} NcSpoutOptions;

typedef struct NcOscOptions {
    uint16_t bind_port;
    const char* publish_address;
} NcOscOptions;

typedef struct NcWindowRenderTarget {
    void* hwnd;
    uint32_t width;
    uint32_t height;
} NcWindowRenderTarget;

typedef struct NcThumbnailRequest {
    NcAvatarHandle handle;
    const char* output_path;
    uint32_t width;
    uint32_t height;
    float delta_time_seconds;
} NcThumbnailRequest;

typedef struct NcRuntimeStats {
    uint32_t render_ready_avatar_count;
    uint32_t spout_active;
    uint32_t osc_active;
    float last_frame_ms;
    float gpu_frame_ms;
    float cpu_frame_ms;
    float material_resolve_ms;
    uint32_t pass_count;
} NcRuntimeStats;

typedef enum NcSpoutBackendKind {
    NC_SPOUT_BACKEND_INACTIVE = 0,
    NC_SPOUT_BACKEND_LEGACY_SHARED_MEMORY = 1,
    NC_SPOUT_BACKEND_SPOUT2_GPU = 2
} NcSpoutBackendKind;

typedef struct NcSpoutDiagnostics {
    uint32_t backend_kind;
    uint32_t strict_mode;
    uint64_t fallback_count;
    char last_error_code[64];
} NcSpoutDiagnostics;

typedef struct NcErrorInfo {
    NcResultCode code;
    char subsystem[32];
    char message[256];
    uint8_t recoverable;
} NcErrorInfo;

VSFCLONE_API NcResultCode nc_initialize(const NcInitOptions* options);
VSFCLONE_API NcResultCode nc_shutdown(void);

VSFCLONE_API NcResultCode nc_load_avatar(const NcAvatarLoadRequest* request, NcAvatarHandle* out_handle, NcAvatarInfo* out_info);
VSFCLONE_API NcResultCode nc_unload_avatar(NcAvatarHandle handle);
VSFCLONE_API NcResultCode nc_get_avatar_info(NcAvatarHandle handle, NcAvatarInfo* out_info);
VSFCLONE_API NcResultCode nc_get_expression_count(NcAvatarHandle handle, uint32_t* out_count);
VSFCLONE_API NcResultCode nc_get_expression_infos(NcAvatarHandle handle, NcExpressionInfo* out_infos, uint32_t capacity, uint32_t* out_written);
VSFCLONE_API NcResultCode nc_get_springbone_info(NcAvatarHandle handle, NcSpringBoneInfo* out_info);
VSFCLONE_API NcResultCode nc_get_avatar_runtime_metrics_v2(NcAvatarHandle handle, NcAvatarRuntimeMetricsV2* out_info);

VSFCLONE_API NcResultCode nc_set_tracking_frame(const NcTrackingFrame* frame);
VSFCLONE_API NcResultCode nc_set_expression_weights(const NcExpressionWeight* weights, uint32_t count);
VSFCLONE_API NcResultCode nc_create_render_resources(NcAvatarHandle handle);
VSFCLONE_API NcResultCode nc_destroy_render_resources(NcAvatarHandle handle);
VSFCLONE_API NcResultCode nc_render_frame(const NcRenderContext* ctx);
VSFCLONE_API NcResultCode nc_create_window_render_target(const NcWindowRenderTarget* target);
VSFCLONE_API NcResultCode nc_resize_window_render_target(const NcWindowRenderTarget* target);
VSFCLONE_API NcResultCode nc_destroy_window_render_target(void* hwnd);
VSFCLONE_API NcResultCode nc_render_frame_to_window(void* hwnd, float delta_time_seconds);
VSFCLONE_API NcResultCode nc_render_avatar_thumbnail_png(const NcThumbnailRequest* request);
VSFCLONE_API NcResultCode nc_set_render_quality_options(const NcRenderQualityOptions* options);
VSFCLONE_API NcResultCode nc_get_render_quality_options(NcRenderQualityOptions* out_options);
VSFCLONE_API NcResultCode nc_set_pose_offsets(const NcPoseBoneOffset* offsets, uint32_t count);
VSFCLONE_API NcResultCode nc_clear_pose_offsets(void);

VSFCLONE_API NcResultCode nc_start_spout(const NcSpoutOptions* options);
VSFCLONE_API NcResultCode nc_stop_spout(void);

VSFCLONE_API NcResultCode nc_start_osc(const NcOscOptions* options);
VSFCLONE_API NcResultCode nc_stop_osc(void);

VSFCLONE_API NcResultCode nc_get_last_error(NcErrorInfo* out_error);
VSFCLONE_API NcResultCode nc_get_runtime_stats(NcRuntimeStats* out_stats);
VSFCLONE_API NcResultCode nc_get_spout_diagnostics(NcSpoutDiagnostics* out_diag);

#ifdef __cplusplus
}
#endif
