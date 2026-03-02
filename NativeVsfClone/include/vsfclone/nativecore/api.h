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
    NC_AVATAR_FORMAT_VSFAVATAR = 3
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
    uint32_t warning_count;
    uint32_t missing_feature_count;
    char display_name[128];
    char source_path[260];
    char last_warning[256];
    char last_missing_feature[256];
} NcAvatarInfo;

typedef struct NcTrackingFrame {
    float head_pos[3];
    float head_rot_quat[4];
    float eye_gaze_l[3];
    float eye_gaze_r[3];
    float blink_l;
    float blink_r;
    float mouth_open;
} NcTrackingFrame;

typedef struct NcRenderContext {
    void* hwnd;
    uint32_t width;
    uint32_t height;
    float delta_time_seconds;
} NcRenderContext;

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

VSFCLONE_API NcResultCode nc_set_tracking_frame(const NcTrackingFrame* frame);
VSFCLONE_API NcResultCode nc_render_frame(const NcRenderContext* ctx);

VSFCLONE_API NcResultCode nc_start_spout(const NcSpoutOptions* options);
VSFCLONE_API NcResultCode nc_stop_spout(void);

VSFCLONE_API NcResultCode nc_start_osc(const NcOscOptions* options);
VSFCLONE_API NcResultCode nc_stop_osc(void);

VSFCLONE_API NcResultCode nc_get_last_error(NcErrorInfo* out_error);

#ifdef __cplusplus
}
#endif
