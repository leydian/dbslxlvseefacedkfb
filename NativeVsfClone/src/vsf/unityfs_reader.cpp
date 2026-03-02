#include "vsfclone/vsf/unityfs_reader.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace vsfclone::vsf {

namespace {

std::uint32_t ReadU32BE(std::istream& in) {
    std::array<unsigned char, 4> b {};
    in.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return (static_cast<std::uint32_t>(b[0]) << 24U) |
           (static_cast<std::uint32_t>(b[1]) << 16U) |
           (static_cast<std::uint32_t>(b[2]) << 8U) |
           static_cast<std::uint32_t>(b[3]);
}

std::uint64_t ReadU64BE(std::istream& in) {
    std::array<unsigned char, 8> b {};
    in.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return (static_cast<std::uint64_t>(b[0]) << 56U) |
           (static_cast<std::uint64_t>(b[1]) << 48U) |
           (static_cast<std::uint64_t>(b[2]) << 40U) |
           (static_cast<std::uint64_t>(b[3]) << 32U) |
           (static_cast<std::uint64_t>(b[4]) << 24U) |
           (static_cast<std::uint64_t>(b[5]) << 16U) |
           (static_cast<std::uint64_t>(b[6]) << 8U) |
           static_cast<std::uint64_t>(b[7]);
}

std::string ReadNullTerminated(std::istream& in, std::size_t max_chars = 1024U) {
    std::string out;
    out.reserve(32U);
    for (std::size_t i = 0; i < max_chars; ++i) {
        char c = 0;
        in.read(&c, 1);
        if (!in.good()) {
            break;
        }
        if (c == '\0') {
            break;
        }
        out.push_back(c);
    }
    return out;
}

UnityFsCompressionMode ParseCompressionMode(std::uint32_t flags) {
    const auto mode = static_cast<std::uint8_t>(flags & 0x3FU);
    switch (mode) {
        case 0:
            return UnityFsCompressionMode::None;
        case 1:
            return UnityFsCompressionMode::Lzma;
        case 2:
            return UnityFsCompressionMode::Lz4;
        case 3:
            return UnityFsCompressionMode::Lz4Hc;
        default:
            return UnityFsCompressionMode::Unknown;
    }
}

std::uint32_t CountNeedle(const std::vector<char>& buffer, const char* needle, std::size_t needle_len) {
    if (needle_len == 0 || buffer.size() < needle_len) {
        return 0;
    }
    std::uint32_t count = 0;
    for (std::size_t i = 0; i + needle_len <= buffer.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle_len; ++j) {
            if (buffer[i + j] != needle[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            ++count;
        }
    }
    return count;
}

}  // namespace

core::Result<UnityFsProbe> UnityFsReader::Probe(const std::string& path) const {
    if (!fs::exists(path)) {
        return core::Result<UnityFsProbe>::Fail("file not found");
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return core::Result<UnityFsProbe>::Fail("failed to open file");
    }

    UnityFsProbe probe;
    probe.header.signature = ReadNullTerminated(in, 64U);
    if (probe.header.signature != "UnityFS") {
        return core::Result<UnityFsProbe>::Fail("not a UnityFS bundle");
    }

    probe.header.format_version = ReadU32BE(in);
    probe.header.minimum_player_version = ReadNullTerminated(in, 64U);
    probe.header.engine_version = ReadNullTerminated(in, 64U);
    probe.header.bundle_file_size = ReadU64BE(in);
    probe.header.compressed_metadata_size = ReadU32BE(in);
    probe.header.uncompressed_metadata_size = ReadU32BE(in);
    probe.header.flags = ReadU32BE(in);
    probe.header.compression_mode = ParseCompressionMode(probe.header.flags);

    constexpr std::size_t kScanWindow = 4U * 1024U * 1024U;
    std::vector<char> buf(kScanWindow, 0);
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    const auto actual = static_cast<std::size_t>(in.gcount());
    buf.resize(actual);

    probe.has_cab_token = CountNeedle(buf, "CAB-", 4U) > 0U;
    probe.vrm_token_hits = CountNeedle(buf, "VRM", 3U);
    return core::Result<UnityFsProbe>::Ok(probe);
}

}  // namespace vsfclone::vsf

