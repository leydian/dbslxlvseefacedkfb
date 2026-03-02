#include "vsfclone/vsf/unityfs_reader.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace vsfclone::vsf {

namespace {

std::uint16_t ReadU16BE(std::istream& in) {
    std::array<unsigned char, 2> b {};
    in.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(b[0]) << 8U) | static_cast<std::uint16_t>(b[1]));
}

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

bool ReadU16BE(const std::vector<unsigned char>& buf, std::size_t& at, std::uint16_t& out) {
    if (at + 2U > buf.size()) {
        return false;
    }
    out = static_cast<std::uint16_t>((static_cast<std::uint16_t>(buf[at]) << 8U) |
                                     static_cast<std::uint16_t>(buf[at + 1U]));
    at += 2U;
    return true;
}

bool ReadU32BE(const std::vector<unsigned char>& buf, std::size_t& at, std::uint32_t& out) {
    if (at + 4U > buf.size()) {
        return false;
    }
    out = (static_cast<std::uint32_t>(buf[at]) << 24U) |
          (static_cast<std::uint32_t>(buf[at + 1U]) << 16U) |
          (static_cast<std::uint32_t>(buf[at + 2U]) << 8U) |
          static_cast<std::uint32_t>(buf[at + 3U]);
    at += 4U;
    return true;
}

bool ReadU64BE(const std::vector<unsigned char>& buf, std::size_t& at, std::uint64_t& out) {
    if (at + 8U > buf.size()) {
        return false;
    }
    out = (static_cast<std::uint64_t>(buf[at]) << 56U) |
          (static_cast<std::uint64_t>(buf[at + 1U]) << 48U) |
          (static_cast<std::uint64_t>(buf[at + 2U]) << 40U) |
          (static_cast<std::uint64_t>(buf[at + 3U]) << 32U) |
          (static_cast<std::uint64_t>(buf[at + 4U]) << 24U) |
          (static_cast<std::uint64_t>(buf[at + 5U]) << 16U) |
          (static_cast<std::uint64_t>(buf[at + 6U]) << 8U) |
          static_cast<std::uint64_t>(buf[at + 7U]);
    at += 8U;
    return true;
}

bool ReadCString(const std::vector<unsigned char>& buf, std::size_t& at, std::string& out, std::size_t max_chars = 4096U) {
    out.clear();
    out.reserve(64U);
    for (std::size_t i = 0; i < max_chars; ++i) {
        if (at >= buf.size()) {
            return false;
        }
        const auto c = static_cast<char>(buf[at++]);
        if (c == '\0') {
            return true;
        }
        out.push_back(c);
    }
    return false;
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

bool Lz4DecompressRaw(const std::vector<unsigned char>& src, std::size_t expected_size, std::vector<unsigned char>& dst) {
    dst.clear();
    dst.reserve(expected_size);

    std::size_t ip = 0U;
    while (ip < src.size() && dst.size() < expected_size) {
        const std::uint8_t token = src[ip++];
        std::size_t literal_len = static_cast<std::size_t>(token >> 4U);
        if (literal_len == 15U) {
            while (ip < src.size() && src[ip] == 255U) {
                literal_len += 255U;
                ++ip;
            }
            if (ip >= src.size()) {
                return false;
            }
            literal_len += src[ip++];
        }
        if (ip + literal_len > src.size()) {
            return false;
        }
        if (dst.size() + literal_len > expected_size) {
            return false;
        }
        dst.insert(dst.end(), src.begin() + static_cast<std::ptrdiff_t>(ip), src.begin() + static_cast<std::ptrdiff_t>(ip + literal_len));
        ip += literal_len;

        if (ip >= src.size()) {
            break;
        }
        if (ip + 2U > src.size()) {
            return false;
        }
        const std::size_t offset = static_cast<std::size_t>(src[ip]) | (static_cast<std::size_t>(src[ip + 1U]) << 8U);
        ip += 2U;
        if (offset == 0U || offset > dst.size()) {
            return false;
        }

        std::size_t match_len = static_cast<std::size_t>(token & 0x0FU) + 4U;
        if ((token & 0x0FU) == 15U) {
            while (ip < src.size() && src[ip] == 255U) {
                match_len += 255U;
                ++ip;
            }
            if (ip >= src.size()) {
                return false;
            }
            match_len += src[ip++];
        }
        if (dst.size() + match_len > expected_size) {
            return false;
        }

        const std::size_t copy_from = dst.size() - offset;
        for (std::size_t i = 0; i < match_len; ++i) {
            dst.push_back(dst[copy_from + i]);
        }
    }
    return dst.size() == expected_size;
}

bool TryParseMetadata(const std::vector<unsigned char>& metadata, UnityFsProbe& probe, std::string& error) {
    constexpr std::size_t kExpectedHashBytes = 16U;
    if (metadata.size() < kExpectedHashBytes + 8U) {
        error = "metadata buffer too small";
        return false;
    }

    std::size_t at = kExpectedHashBytes;
    std::uint32_t block_count = 0;
    if (!ReadU32BE(metadata, at, block_count)) {
        error = "failed to read block count";
        return false;
    }

    for (std::uint32_t i = 0; i < block_count; ++i) {
        std::uint32_t uncompressed_size = 0;
        std::uint32_t compressed_size = 0;
        std::uint16_t block_flags = 0;
        if (!ReadU32BE(metadata, at, uncompressed_size) ||
            !ReadU32BE(metadata, at, compressed_size) ||
            !ReadU16BE(metadata, at, block_flags)) {
            error = "failed to read block info table";
            return false;
        }
        (void)uncompressed_size;
        (void)compressed_size;
        (void)block_flags;
    }

    std::uint32_t node_count = 0;
    if (!ReadU32BE(metadata, at, node_count)) {
        error = "failed to read node count";
        return false;
    }

    std::string first_path;
    for (std::uint32_t i = 0; i < node_count; ++i) {
        std::uint64_t node_offset = 0;
        std::uint64_t node_size = 0;
        std::uint32_t node_flags = 0;
        std::string node_path;
        if (!ReadU64BE(metadata, at, node_offset) ||
            !ReadU64BE(metadata, at, node_size) ||
            !ReadU32BE(metadata, at, node_flags) ||
            !ReadCString(metadata, at, node_path)) {
            error = "failed to read node table";
            return false;
        }
        (void)node_offset;
        (void)node_size;
        (void)node_flags;
        if (i == 0U) {
            first_path = node_path;
        }
    }

    probe.metadata_parsed = true;
    probe.block_count = block_count;
    probe.node_count = node_count;
    probe.first_node_path = first_path;
    return true;
}

bool ParseMetadataBlock(std::ifstream& in, std::uint64_t metadata_offset, const UnityFsHeader& header, UnityFsProbe& probe, std::string& error) {
    if (metadata_offset + header.compressed_metadata_size > header.bundle_file_size) {
        error = "metadata offset out of range";
        return false;
    }

    in.clear();
    in.seekg(static_cast<std::streamoff>(metadata_offset), std::ios::beg);
    if (!in.good()) {
        error = "failed to seek metadata block";
        return false;
    }

    std::vector<unsigned char> compressed(header.compressed_metadata_size, 0U);
    in.read(reinterpret_cast<char*>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
    if (static_cast<std::size_t>(in.gcount()) != compressed.size()) {
        error = "failed to read metadata block";
        return false;
    }

    std::vector<unsigned char> metadata;
    switch (header.compression_mode) {
        case UnityFsCompressionMode::None:
            metadata = std::move(compressed);
            break;
        case UnityFsCompressionMode::Lz4:
        case UnityFsCompressionMode::Lz4Hc:
            if (!Lz4DecompressRaw(compressed, header.uncompressed_metadata_size, metadata)) {
                error = "LZ4 metadata decompression failed";
                return false;
            }
            break;
        case UnityFsCompressionMode::Lzma:
            error = "LZMA metadata decompression is not implemented";
            return false;
        default:
            error = "unsupported metadata compression mode";
            return false;
    }
    if (metadata.size() != header.uncompressed_metadata_size) {
        error = "decompressed metadata size mismatch";
        return false;
    }
    return TryParseMetadata(metadata, probe, error);
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

    const auto after_header = static_cast<std::uint64_t>(in.tellg());
    const bool metadata_at_end = (probe.header.flags & 0x80U) != 0U;
    const std::uint64_t primary_metadata_offset =
        metadata_at_end
            ? (probe.header.bundle_file_size - probe.header.compressed_metadata_size)
            : after_header;

    std::string meta_err;
    if (!ParseMetadataBlock(in, primary_metadata_offset, probe.header, probe, meta_err)) {
        // Some bundles expect 16-byte alignment before metadata when not located at end.
        if (!metadata_at_end) {
            const std::uint64_t aligned = (after_header + 15ULL) & ~15ULL;
            std::string aligned_err;
            if (ParseMetadataBlock(in, aligned, probe.header, probe, aligned_err)) {
                meta_err.clear();
            } else {
                meta_err += "; aligned parse failed: " + aligned_err;
            }
        }
    }
    probe.metadata_error = meta_err;

    in.clear();
    in.seekg(static_cast<std::streamoff>(after_header), std::ios::beg);
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
