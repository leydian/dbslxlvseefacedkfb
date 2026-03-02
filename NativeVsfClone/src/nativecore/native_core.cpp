#include "vsfclone/nativecore/api.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "vsfclone/avatar/avatar_loader_facade.h"
#include "vsfclone/avatar/avatar_package.h"
#include "vsfclone/osc/osc_endpoint_stub.h"
#include "vsfclone/stream/spout_sender_stub.h"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#endif

namespace vsfclone::nativecore {

namespace {

using vsfclone::avatar::AvatarCompatLevel;
using vsfclone::avatar::AvatarPackage;
using vsfclone::avatar::AvatarSourceType;

struct CoreState {
    bool initialized = false;
    std::uint64_t next_avatar_handle = 1;
    std::unordered_map<std::uint64_t, AvatarPackage> avatars;
    std::unordered_set<std::uint64_t> render_ready_avatars;
    avatar::AvatarLoaderFacade loader;
    stream::SpoutSenderStub spout;
    osc::OscEndpointStub osc;
    NcTrackingFrame latest_tracking {};

    NcResultCode last_error_code = NC_OK;
    std::string last_error_subsystem = "none";
    std::string last_error_message;
    bool last_error_recoverable = true;
};

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
        case AvatarSourceType::VsfAvatar:
            return NC_AVATAR_FORMAT_VSFAVATAR;
        default:
            return NC_AVATAR_FORMAT_AUTO;
    }
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
    out_info->missing_feature_count = static_cast<std::uint32_t>(pkg.missing_features.size());
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
    if (!pkg.warnings.empty()) {
        CopyString(out_info->last_warning, sizeof(out_info->last_warning), pkg.warnings.back());
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
    vsfclone::nativecore::g_state.avatars.clear();
    vsfclone::nativecore::g_state.render_ready_avatars.clear();
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

    const std::uint64_t handle = vsfclone::nativecore::g_state.next_avatar_handle++;
    vsfclone::nativecore::g_state.avatars[handle] = loaded.value;
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

NcResultCode nc_set_tracking_frame(const NcTrackingFrame* frame) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (frame == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "tracking", "frame must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    vsfclone::nativecore::g_state.latest_tracking = *frame;
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
        vsfclone::nativecore::SetError(
            NC_ERROR_UNSUPPORTED,
            "render",
            "avatar has no renderable mesh payloads",
            true);
        return NC_ERROR_UNSUPPORTED;
    }

    vsfclone::nativecore::g_state.render_ready_avatars.insert(handle);
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
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_render_frame(const NcRenderContext* ctx) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (ctx == nullptr || ctx->width == 0U || ctx->height == 0U) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "render context/size is invalid", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (ctx->d3d11_device == nullptr || ctx->d3d11_device_context == nullptr || ctx->d3d11_rtv == nullptr) {
        vsfclone::nativecore::SetError(
            NC_ERROR_INVALID_ARGUMENT,
            "render",
            "d3d11_device/d3d11_device_context/d3d11_rtv must be non-null",
            true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (vsfclone::nativecore::g_state.render_ready_avatars.empty()) {
        vsfclone::nativecore::SetError(NC_ERROR_UNSUPPORTED, "render", "no avatar has render resources", true);
        return NC_ERROR_UNSUPPORTED;
    }
#if defined(_WIN32)
    auto* device_ctx = reinterpret_cast<ID3D11DeviceContext*>(ctx->d3d11_device_context);
    auto* rtv = reinterpret_cast<ID3D11RenderTargetView*>(ctx->d3d11_rtv);
    if (device_ctx == nullptr || rtv == nullptr) {
        vsfclone::nativecore::SetError(
            NC_ERROR_INVALID_ARGUMENT,
            "render",
            "invalid d3d11 context pointers",
            true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const float clear_color[4] = {0.08f, 0.12f, 0.18f, 1.0f};
    device_ctx->OMSetRenderTargets(1, &rtv, nullptr);
    device_ctx->ClearRenderTargetView(rtv, clear_color);
#endif
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
