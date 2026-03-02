#include "vsfclone/vsf/unityfs_reader.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "vsfclone/vsf/serialized_file_reader.h"

namespace fs = std::filesystem;

namespace vsfclone::vsf {

namespace {

struct BlockInfo {
    std::uint32_t uncompressed_size = 0;
    std::uint32_t compressed_size = 0;
    std::uint16_t flags = 0;
};

struct NodeInfo {
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint32_t flags = 0;
    std::string path;
};

struct ParsedMetadata {
    std::vector<BlockInfo> blocks;
    std::vector<NodeInfo> nodes;
};

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
        if (ip + literal_len > src.size() || dst.size() + literal_len > expected_size) {
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

bool ParseMetadataTables(const std::vector<unsigned char>& metadata, ParsedMetadata& out, std::string& error) {
    constexpr std::size_t kExpectedHashBytes = 16U;
    if (metadata.size() < kExpectedHashBytes + 8U) {
        error = "metadata buffer too small";
        return false;
    }

    out.blocks.clear();
    out.nodes.clear();

    std::size_t at = kExpectedHashBytes;
    std::uint32_t block_count = 0;
    if (!ReadU32BE(metadata, at, block_count)) {
        error = "failed to read block count";
        return false;
    }

    out.blocks.reserve(block_count);
    for (std::uint32_t i = 0; i < block_count; ++i) {
        BlockInfo b;
        if (!ReadU32BE(metadata, at, b.uncompressed_size) ||
            !ReadU32BE(metadata, at, b.compressed_size) ||
            !ReadU16BE(metadata, at, b.flags)) {
            error = "failed to read block info table";
            return false;
        }
        out.blocks.push_back(b);
    }

    std::uint32_t node_count = 0;
    if (!ReadU32BE(metadata, at, node_count)) {
        error = "failed to read node count";
        return false;
    }

    out.nodes.reserve(node_count);
    for (std::uint32_t i = 0; i < node_count; ++i) {
        NodeInfo n;
        if (!ReadU64BE(metadata, at, n.offset) ||
            !ReadU64BE(metadata, at, n.size) ||
            !ReadU32BE(metadata, at, n.flags) ||
            !ReadCString(metadata, at, n.path)) {
            error = "failed to read node table";
            return false;
        }
        out.nodes.push_back(std::move(n));
    }
    return true;
}

bool DecompressByMode(const std::vector<unsigned char>& src, std::size_t expected_size, std::uint8_t mode, std::vector<unsigned char>& dst, std::string& error) {
    dst.clear();
    switch (mode) {
        case 0:
            if (src.size() != expected_size) {
                error = "raw block size mismatch";
                return false;
            }
            dst = src;
            return true;
        case 2:
        case 3:
            if (!Lz4DecompressRaw(src, expected_size, dst)) {
                error = "LZ4 decompression failed";
                return false;
            }
            return true;
        case 1:
            error = "LZMA decompression is not implemented";
            return false;
        default:
            error = "unsupported compression mode";
            return false;
    }
}

bool ParseMetadataBlock(std::ifstream& in,
                        std::uint64_t metadata_offset,
                        const UnityFsHeader& header,
                        UnityFsProbe& probe,
                        ParsedMetadata& metadata,
                        std::string& error) {
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

    std::vector<unsigned char> decoded;
    const auto mode = static_cast<std::uint8_t>(header.flags & 0x3FU);
    if (!DecompressByMode(compressed, header.uncompressed_metadata_size, mode, decoded, error)) {
        return false;
    }

    if (!ParseMetadataTables(decoded, metadata, error)) {
        return false;
    }

    probe.metadata_parsed = true;
    probe.block_count = static_cast<std::uint32_t>(metadata.blocks.size());
    probe.node_count = static_cast<std::uint32_t>(metadata.nodes.size());
    if (!metadata.nodes.empty()) {
        probe.first_node_path = metadata.nodes.front().path;
    }
    return true;
}

bool ReconstructStreamAt(std::ifstream& in,
                         const UnityFsHeader& header,
                         const ParsedMetadata& metadata,
                         std::uint64_t data_start,
                         std::vector<unsigned char>& out_stream,
                         std::string& error) {
    out_stream.clear();
    std::uint64_t offset = data_start;
    for (const auto& b : metadata.blocks) {
        in.clear();
        in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!in.good()) {
            error = "failed to seek data block";
            return false;
        }

        std::vector<unsigned char> compressed(b.compressed_size, 0U);
        in.read(reinterpret_cast<char*>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
        if (static_cast<std::size_t>(in.gcount()) != compressed.size()) {
            error = "failed to read data block";
            return false;
        }
        offset += b.compressed_size;

        std::vector<unsigned char> decoded;
        const auto mode = static_cast<std::uint8_t>(b.flags & 0x3FU);
        std::string local_err;
        if (!DecompressByMode(compressed, b.uncompressed_size, mode, decoded, local_err)) {
            // Some bundles carry block flags that don't map cleanly in this scaffold parser.
            // Fall back to the header compression mode before failing hard.
            const auto header_mode = static_cast<std::uint8_t>(header.flags & 0x3FU);
            if (!DecompressByMode(compressed, b.uncompressed_size, header_mode, decoded, local_err)) {
                error = "data block decode failed: " + local_err;
                return false;
            }
        }
        out_stream.insert(out_stream.end(), decoded.begin(), decoded.end());
    }
    return true;
}

bool StreamMatchesNodes(const ParsedMetadata& metadata, const std::vector<unsigned char>& stream) {
    for (const auto& n : metadata.nodes) {
        if (n.offset + n.size > stream.size()) {
            return false;
        }
    }
    return true;
}

bool TryReconstructDataStream(std::ifstream& in,
                              const UnityFsHeader& header,
                              const ParsedMetadata& metadata,
                              std::uint64_t after_header,
                              std::uint64_t metadata_offset,
                              bool metadata_at_end,
                              std::vector<unsigned char>& out_stream,
                              std::string& error) {
    std::uint64_t total_compressed = 0;
    for (const auto& b : metadata.blocks) {
        total_compressed += b.compressed_size;
    }

    std::vector<std::uint64_t> candidates;
    candidates.push_back(after_header);
    candidates.push_back((after_header + 15ULL) & ~15ULL);
    candidates.push_back(after_header + header.compressed_metadata_size);
    candidates.push_back(((after_header + header.compressed_metadata_size) + 15ULL) & ~15ULL);
    if (metadata_at_end) {
        if (metadata_offset >= total_compressed) {
            candidates.push_back(metadata_offset - total_compressed);
            candidates.push_back((metadata_offset - total_compressed) & ~15ULL);
        }
    }

    std::string attempt_errors;
    for (const auto start : candidates) {
        std::vector<unsigned char> reconstructed;
        std::string local_err;
        if (!ReconstructStreamAt(in, header, metadata, start, reconstructed, local_err)) {
            attempt_errors += "start=" + std::to_string(start) + ": " + local_err + "; ";
            continue;
        }
        if (!StreamMatchesNodes(metadata, reconstructed)) {
            attempt_errors += "start=" + std::to_string(start) + ": node range mismatch; ";
            continue;
        }
        out_stream = std::move(reconstructed);
        return true;
    }

    error = "failed to reconstruct uncompressed bundle data stream. " + attempt_errors;
    return false;
}

bool ParseSerializedFromNodes(const ParsedMetadata& metadata,
                              const std::vector<unsigned char>& stream,
                              UnityFsProbe& probe,
                              std::string& error) {
    SerializedFileReader serialized;
    bool found_candidate = false;
    SerializedFileSummary best {};
    for (const auto& node : metadata.nodes) {
        if (!(node.path.rfind("CAB-", 0U) == 0U || node.path.find(".assets") != std::string::npos)) {
            continue;
        }
        if (node.size == 0U || node.offset + node.size > stream.size()) {
            continue;
        }
        found_candidate = true;
        const auto begin = stream.begin() + static_cast<std::ptrdiff_t>(node.offset);
        const auto end = begin + static_cast<std::ptrdiff_t>(node.size);
        std::vector<unsigned char> file_bytes(begin, end);
        auto parsed = serialized.ParseObjectSummary(file_bytes);
        if (!parsed.ok) {
            continue;
        }
        if (!probe.object_table_parsed || parsed.value.object_count > best.object_count) {
            best = parsed.value;
            probe.object_table_parsed = true;
        }
    }

    if (!probe.object_table_parsed) {
        error = found_candidate ? "no candidate node could be parsed as SerializedFile" : "no serialized candidate nodes found";
        return false;
    }

    probe.object_count = best.object_count;
    probe.mesh_object_count = best.mesh_object_count;
    probe.material_object_count = best.material_object_count;
    probe.texture_object_count = best.texture_object_count;
    probe.game_object_count = best.game_object_count;
    probe.skinned_mesh_renderer_count = best.skinned_mesh_renderer_count;
    probe.major_types_found = best.major_types_found;
    return true;
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
        metadata_at_end ? (probe.header.bundle_file_size - probe.header.compressed_metadata_size) : after_header;

    ParsedMetadata metadata;
    std::string meta_err;
    if (!ParseMetadataBlock(in, primary_metadata_offset, probe.header, probe, metadata, meta_err) && !metadata_at_end) {
        const std::uint64_t aligned = (after_header + 15ULL) & ~15ULL;
        std::string aligned_err;
        if (!ParseMetadataBlock(in, aligned, probe.header, probe, metadata, aligned_err)) {
            meta_err += "; aligned parse failed: " + aligned_err;
        } else {
            meta_err.clear();
        }
    }
    probe.metadata_error = meta_err;

    if (probe.metadata_parsed) {
        std::vector<unsigned char> stream;
        std::string stream_err;
        if (TryReconstructDataStream(
                in,
                probe.header,
                metadata,
                after_header,
                primary_metadata_offset,
                metadata_at_end,
                stream,
                stream_err)) {
            std::string serialized_err;
            if (!ParseSerializedFromNodes(metadata, stream, probe, serialized_err)) {
                if (!probe.metadata_error.empty()) {
                    probe.metadata_error += "; ";
                }
                probe.metadata_error += serialized_err;
            }
        } else {
            if (!probe.metadata_error.empty()) {
                probe.metadata_error += "; ";
            }
            probe.metadata_error += stream_err;
        }
    }

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
