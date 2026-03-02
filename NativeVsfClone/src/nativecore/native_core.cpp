#include "vsfclone/nativecore/api.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
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
#include <dxgi.h>
#endif

namespace vsfclone::nativecore {

namespace {

using vsfclone::avatar::AvatarCompatLevel;
using vsfclone::avatar::AvatarPackage;
using vsfclone::avatar::AvatarSourceType;

#if defined(_WIN32)
struct WindowRenderState {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* device_context = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};
#endif

struct CoreState {
    bool initialized = false;
    std::uint64_t next_avatar_handle = 1;
    std::unordered_map<std::uint64_t, AvatarPackage> avatars;
    std::unordered_set<std::uint64_t> render_ready_avatars;
    avatar::AvatarLoaderFacade loader;
    stream::SpoutSender spout;
    osc::OscEndpoint osc;
    NcTrackingFrame latest_tracking {};
    float last_frame_ms = 0.0f;

#if defined(_WIN32)
    std::unordered_map<void*, WindowRenderState> window_targets;
#endif

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
    out_info->expression_count = static_cast<std::uint32_t>(pkg.expressions.size());
    if (out_info->detected_format == NC_AVATAR_FORMAT_VRM && out_info->expression_count == 0U) {
        out_info->expression_count = 3U;
    }
    out_info->last_render_draw_calls = pkg.last_render_draw_calls;
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
    const std::string expression_summary =
        (!pkg.last_expression_summary.empty())
            ? pkg.last_expression_summary
            : (out_info->detected_format == NC_AVATAR_FORMAT_VRM ? "blink=0.000000, aa=0.000000, joy=0.000000" : "");
    CopyString(
        out_info->last_expression_summary,
        sizeof(out_info->last_expression_summary),
        expression_summary);
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
    std::uint32_t frame_draw_calls = 0U;
    for (const auto handle : g_state.render_ready_avatars) {
        const auto it = g_state.avatars.find(handle);
        if (it == g_state.avatars.end()) {
            continue;
        }
        frame_draw_calls += static_cast<std::uint32_t>(it->second.mesh_payloads.size());
    }

    const auto frame_begin = std::chrono::steady_clock::now();

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

    const float clear_color[4] = {0.08f, 0.12f, 0.18f, 1.0f};
    device_ctx->OMSetRenderTargets(1, &rtv, nullptr);
    device_ctx->ClearRenderTargetView(rtv, clear_color);

    if (g_state.spout.IsActive()) {
        std::vector<std::uint8_t> pixels;
        if (CaptureRtvBgra(device, device_ctx, rtv, ctx->width, ctx->height, &pixels)) {
            g_state.spout.SubmitFrame(pixels.data(), static_cast<std::uint32_t>(pixels.size()));
        }
    }
#endif

    PublishTrackingFrame();
    for (const auto handle : g_state.render_ready_avatars) {
        auto it = g_state.avatars.find(handle);
        if (it == g_state.avatars.end()) {
            continue;
        }
        it->second.last_render_draw_calls = static_cast<std::uint32_t>(it->second.mesh_payloads.size());
    }
    if (frame_draw_calls == 0U) {
        SetError(NC_ERROR_UNSUPPORTED, "render", "render-ready avatars produced zero draw calls", true);
        return NC_ERROR_UNSUPPORTED;
    }

    const auto frame_end = std::chrono::steady_clock::now();
    g_state.last_frame_ms = static_cast<float>(
        std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_begin).count()) /
        1000.0f;

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
    vsfclone::nativecore::g_state.render_ready_avatars.clear();
    vsfclone::nativecore::g_state.last_frame_ms = 0.0f;
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
#endif
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
    if (loaded.value.source_type == vsfclone::avatar::AvatarSourceType::Vrm && loaded.value.expressions.empty()) {
        loaded.value.expressions.push_back({"blink", "blink", 0.0f, 0.0f});
        loaded.value.expressions.push_back({"aa", "viseme_aa", 0.0f, 0.0f});
        loaded.value.expressions.push_back({"joy", "joy", 0.0f, 0.0f});
        loaded.value.warnings.push_back("W_VRM_EXPRESSION_FALLBACK: runtime injected blink/aa/joy expression defaults");
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
    vsfclone::nativecore::ClearError();
    return NC_OK;
}
