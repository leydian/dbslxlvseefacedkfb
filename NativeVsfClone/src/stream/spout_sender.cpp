#include "vsfclone/stream/spout_sender.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>

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
#if !defined(_WIN32)
    (void)cfg;
    return false;
#else
    config_ = cfg;
    config_.channel_name = SanitizeChannelName(config_.channel_name);
    mapping_name_ = "Local\\VsfCloneSpout_" + config_.channel_name;
    frame_counter_ = 0;

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
    active_ = true;
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

void SpoutSender::Stop() {
#if defined(_WIN32)
    if (mapping_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(mapping_));
        mapping_ = nullptr;
    }
#endif
    active_ = false;
    frame_counter_ = 0;
}

bool SpoutSender::IsActive() const {
    return active_;
}

std::uint64_t SpoutSender::FrameCount() const {
    return frame_counter_;
}

}  // namespace vsfclone::stream
