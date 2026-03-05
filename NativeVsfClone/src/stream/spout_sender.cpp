#include "vsfclone/stream/spout_sender.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#if defined(VSFCLONE_SPOUT2_ENABLED)
#include "SpoutDX/SpoutDX.h"
#endif
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

namespace vsfclone::stream {

namespace {

constexpr std::uint32_t kSharedMagic = 0x56534653;  // "VSFS"
constexpr std::uint32_t kSharedVersion = 1;

struct SharedFrameHeader {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t fps;
    std::uint64_t frame_index;
    std::uint64_t pixel_bytes;
};

std::string SanitizeChannelName(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (const char c : input) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        return "VsfClone";
    }
    return out;
}

}  // namespace

bool SpoutSender::Start(const StreamConfig& cfg) {
    Stop();
    if (cfg.width == 0U || cfg.height == 0U) {
        return false;
    }
    config_ = cfg;
    config_.channel_name = SanitizeChannelName(config_.channel_name);
    frame_counter_ = 0U;
    fallback_count_ = 0U;
    strict_mode_ = ResolveStrictMode();
    last_error_code_.clear();
    backend_mode_ = BackendMode::None;

#if !defined(_WIN32)
    return false;
#else
    if (strict_mode_) {
        // Strict mode: no legacy fallback initialization on start.
        active_ = true;
        return true;
    }
    // Start on legacy transport first and allow runtime GPU-path takeover on first texture submit.
    // This keeps startup deterministic and preserves v1 fallback behavior.
    if (!StartLegacySharedMemory(config_)) {
        last_error_code_ = "LEGACY_START_FAILED";
        return false;
    }
    backend_mode_ = BackendMode::LegacySharedMemory;
    active_ = true;
    return true;
#endif
}

bool SpoutSender::StartLegacySharedMemory(const StreamConfig& cfg) {
#if !defined(_WIN32)
    (void)cfg;
    return false;
#else
    config_ = cfg;
    mapping_name_ = "Local\\VsfCloneSpout_" + config_.channel_name;

    const std::uint64_t pixel_bytes = static_cast<std::uint64_t>(config_.width) * static_cast<std::uint64_t>(config_.height) * 4ULL;
    const std::uint64_t total_bytes = pixel_bytes + sizeof(SharedFrameHeader);
    if (total_bytes > static_cast<std::uint64_t>(std::numeric_limits<DWORD>::max()) * 2ULL) {
        return false;
    }

    const DWORD total_low = static_cast<DWORD>(total_bytes & 0xFFFFFFFFULL);
    const DWORD total_high = static_cast<DWORD>((total_bytes >> 32U) & 0xFFFFFFFFULL);
    HANDLE mapping = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        total_high,
        total_low,
        mapping_name_.c_str());
    if (mapping == nullptr) {
        return false;
    }

    void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0U, 0U, static_cast<SIZE_T>(total_bytes));
    if (view == nullptr) {
        CloseHandle(mapping);
        return false;
    }

    std::memset(view, 0, static_cast<std::size_t>(total_bytes));
    auto* header = static_cast<SharedFrameHeader*>(view);
    header->magic = kSharedMagic;
    header->version = kSharedVersion;
    header->width = config_.width;
    header->height = config_.height;
    header->fps = config_.fps;
    header->frame_index = 0ULL;
    header->pixel_bytes = pixel_bytes;
    UnmapViewOfFile(view);

    mapping_ = mapping;
    return true;
#endif
}

void SpoutSender::SubmitFrame(const void* bgra_pixels, std::uint32_t bytes) {
    if (!active_ || bgra_pixels == nullptr) {
        return;
    }
#if !defined(_WIN32)
    (void)bgra_pixels;
    (void)bytes;
#else
    if (backend_mode_ != BackendMode::LegacySharedMemory) {
        // GPU path became active after start; keep CPU copy path disabled.
        return;
    }
    SubmitLegacySharedMemory(bgra_pixels, bytes);
#endif
}

void SpoutSender::SubmitLegacySharedMemory(const void* bgra_pixels, std::uint32_t bytes) {
#if !defined(_WIN32)
    (void)bgra_pixels;
    (void)bytes;
#else
    const std::uint64_t expected_bytes = static_cast<std::uint64_t>(config_.width) * static_cast<std::uint64_t>(config_.height) * 4ULL;
    if (bytes != expected_bytes || mapping_ == nullptr) {
        return;
    }
    const std::uint64_t total_bytes = expected_bytes + sizeof(SharedFrameHeader);
    void* view = MapViewOfFile(
        static_cast<HANDLE>(mapping_),
        FILE_MAP_ALL_ACCESS,
        0U,
        0U,
        static_cast<SIZE_T>(total_bytes));
    if (view == nullptr) {
        return;
    }

    auto* header = static_cast<SharedFrameHeader*>(view);
    void* payload = static_cast<void*>(header + 1);
    std::memcpy(payload, bgra_pixels, static_cast<std::size_t>(expected_bytes));
    ++frame_counter_;
    header->frame_index = frame_counter_;
    header->pixel_bytes = expected_bytes;
    UnmapViewOfFile(view);
#endif
}

bool SpoutSender::WantsGpuTextureSubmit() const {
    return active_;
}

bool SpoutSender::SubmitFrameTexture(void* d3d11_device, void* d3d11_texture) {
    if (!active_ || d3d11_device == nullptr || d3d11_texture == nullptr) {
        last_error_code_ = "GPU_SUBMIT_INVALID_ARGUMENT";
        return false;
    }

    if (backend_mode_ == BackendMode::Spout2Gpu) {
        if (TrySendSpout2(d3d11_device, d3d11_texture)) {
            ++frame_counter_;
            last_error_code_.clear();
            return true;
        }
        // Degrade to legacy transport without dropping stream state.
        StopSpout2();
        if (strict_mode_) {
            backend_mode_ = BackendMode::None;
            last_error_code_ = "SPOUT2_SEND_FAILED_STRICT";
        } else {
            backend_mode_ = BackendMode::LegacySharedMemory;
            ++fallback_count_;
            last_error_code_ = "SPOUT2_SEND_FAILED_FALLBACK";
        }
        return false;
    }

    if (TryInitSpout2(d3d11_device, d3d11_texture) && TrySendSpout2(d3d11_device, d3d11_texture)) {
        backend_mode_ = BackendMode::Spout2Gpu;
        ++frame_counter_;
        last_error_code_.clear();
        return true;
    }
    last_error_code_ = strict_mode_ ? "SPOUT2_INIT_FAILED_STRICT" : "SPOUT2_INIT_FAILED";
    return false;
}

const char* SpoutSender::ActiveBackendName() const {
    switch (backend_mode_) {
        case BackendMode::Spout2Gpu:
            return "spout2-gpu";
        case BackendMode::LegacySharedMemory:
            return "legacy-shared-memory";
        default:
            return "inactive";
    }
}

std::uint64_t SpoutSender::FallbackCount() const {
    return fallback_count_;
}

void SpoutSender::Stop() {
#if defined(_WIN32)
    StopSpout2();
    StopLegacySharedMemory();
#endif
    active_ = false;
    strict_mode_ = false;
    frame_counter_ = 0U;
    fallback_count_ = 0U;
    last_error_code_.clear();
    backend_mode_ = BackendMode::None;
}

void SpoutSender::StopLegacySharedMemory() {
#if defined(_WIN32)
    if (mapping_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(mapping_));
        mapping_ = nullptr;
    }
#endif
}

bool SpoutSender::IsActive() const {
    return active_;
}

std::uint64_t SpoutSender::FrameCount() const {
    return frame_counter_;
}

bool SpoutSender::IsStrictMode() const {
    return strict_mode_;
}

SpoutSender::BackendKind SpoutSender::ActiveBackendKind() const {
    switch (backend_mode_) {
        case BackendMode::Spout2Gpu:
            return BackendKind::Spout2Gpu;
        case BackendMode::LegacySharedMemory:
            return BackendKind::LegacySharedMemory;
        default:
            return BackendKind::Inactive;
    }
}

const std::string& SpoutSender::LastErrorCode() const {
    return last_error_code_;
}

bool SpoutSender::TryInitSpout2(void* d3d11_device, void* d3d11_texture) {
    (void)d3d11_device;
    (void)d3d11_texture;
#if defined(_WIN32) && defined(VSFCLONE_SPOUT2_ENABLED)
    if (d3d11_device == nullptr) {
        last_error_code_ = "SPOUT2_DEVICE_NULL";
        return false;
    }

    auto* sender = static_cast<spoutDX*>(spout_dx_sender_);
    if (sender == nullptr) {
        sender = new (std::nothrow) spoutDX();
        if (sender == nullptr) {
            last_error_code_ = "SPOUT2_ALLOC_FAILED";
            return false;
        }
        spout_dx_sender_ = sender;
    }

    auto* device = static_cast<ID3D11Device*>(d3d11_device);
    if (!sender->OpenDirectX11(device)) {
        last_error_code_ = "SPOUT2_OPEN_DX11_FAILED";
        return false;
    }

    if (!sender->SetSenderName(config_.channel_name.c_str())) {
        last_error_code_ = "SPOUT2_SET_SENDER_NAME_FAILED";
        return false;
    }

    sender->SetSenderFormat(DXGI_FORMAT_B8G8R8A8_UNORM);

    last_error_code_.clear();
    return true;
#else
    return false;
#endif
}

bool SpoutSender::TrySendSpout2(void* d3d11_device, void* d3d11_texture) {
    (void)d3d11_device;
    (void)d3d11_texture;
#if defined(_WIN32) && defined(VSFCLONE_SPOUT2_ENABLED)
    auto* sender = static_cast<spoutDX*>(spout_dx_sender_);
    if (sender == nullptr) {
        last_error_code_ = "SPOUT2_SENDER_NULL";
        return false;
    }

    auto* texture = static_cast<ID3D11Texture2D*>(d3d11_texture);
    if (texture == nullptr) {
        last_error_code_ = "SPOUT2_TEXTURE_NULL";
        return false;
    }

    D3D11_TEXTURE2D_DESC desc {};
    texture->GetDesc(&desc);
    if (desc.Width == 0U || desc.Height == 0U) {
        last_error_code_ = "SPOUT2_TEXTURE_INVALID_SIZE";
        return false;
    }
    if (desc.Width != config_.width || desc.Height != config_.height) {
        config_.width = desc.Width;
        config_.height = desc.Height;
        // spoutDX internally updates sender size when the submitted texture size changes.
    }

    if (!sender->SendTexture(texture)) {
        last_error_code_ = "SPOUT2_SEND_TEXTURE_FAILED";
        return false;
    }

    last_error_code_.clear();
    return true;
#else
    return false;
#endif
}

void SpoutSender::StopSpout2() {
#if defined(_WIN32) && defined(VSFCLONE_SPOUT2_ENABLED)
    auto* sender = static_cast<spoutDX*>(spout_dx_sender_);
    if (sender != nullptr) {
        sender->ReleaseSender();
        sender->CloseDirectX11();
        delete sender;
        spout_dx_sender_ = nullptr;
    }
#endif
}

bool SpoutSender::ResolveStrictMode() const {
    auto equals_ignore_case = [](const char* lhs, const char* rhs) -> bool {
        if (lhs == nullptr || rhs == nullptr) {
            return false;
        }
        while (*lhs != '\0' && *rhs != '\0') {
            const char l = static_cast<char>(std::tolower(static_cast<unsigned char>(*lhs)));
            const char r = static_cast<char>(std::tolower(static_cast<unsigned char>(*rhs)));
            if (l != r) {
                return false;
            }
            ++lhs;
            ++rhs;
        }
        return *lhs == '\0' && *rhs == '\0';
    };

    const char* mode = std::getenv("VSF_SPOUT_MODE");
    if (mode != nullptr) {
        if (equals_ignore_case(mode, "spout2-strict") || equals_ignore_case(mode, "strict")) {
            return true;
        }
        if (equals_ignore_case(mode, "legacy") || equals_ignore_case(mode, "shared-memory")) {
            return false;
        }
    }
    const char* strict = std::getenv("VSF_SPOUT_STRICT");
    if (strict != nullptr) {
        return equals_ignore_case(strict, "1") || equals_ignore_case(strict, "true") || equals_ignore_case(strict, "yes");
    }
    return false;
}

}  // namespace vsfclone::stream
