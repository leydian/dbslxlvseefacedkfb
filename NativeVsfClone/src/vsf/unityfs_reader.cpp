#include "vsfclone/vsf/unityfs_reader.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "vsfclone/vsf/serialized_file_reader.h"
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#ifdef __cplusplus
extern "C" {
#endif
#include "LzmaDec.h"
#ifdef __cplusplus
}
#endif

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

bool HasLikelySerializedNode(const ParsedMetadata& metadata);

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

bool ReadU32LESafe(const std::vector<unsigned char>& buf, std::size_t& at, std::uint32_t& out) {
    if (at + 4U > buf.size()) {
        return false;
    }
    out = static_cast<std::uint32_t>(buf[at]) |
          (static_cast<std::uint32_t>(buf[at + 1U]) << 8U) |
          (static_cast<std::uint32_t>(buf[at + 2U]) << 16U) |
          (static_cast<std::uint32_t>(buf[at + 3U]) << 24U);
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

bool Lz4DecompressRawBounded(const std::vector<unsigned char>& src, std::size_t max_output_size, std::vector<unsigned char>& dst) {
    dst.clear();
    dst.reserve(max_output_size);

    std::size_t ip = 0U;
    while (ip < src.size()) {
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
        if (ip + literal_len > src.size() || dst.size() + literal_len > max_output_size) {
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
        if (dst.size() + match_len > max_output_size) {
            return false;
        }

        const std::size_t copy_from = dst.size() - offset;
        for (std::size_t i = 0; i < match_len; ++i) {
            dst.push_back(dst[copy_from + i]);
        }
    }
    return true;
}

bool Lz4DecompressRawExact(const std::vector<unsigned char>& src, std::size_t expected_size, std::vector<unsigned char>& dst) {
    if (!Lz4DecompressRawBounded(src, expected_size, dst)) {
        return false;
    }
    return dst.size() == expected_size;
}

std::uint32_t ReadU32LE(const std::vector<unsigned char>& buf, std::size_t at);

bool Lz4DecompressSizePrefixed(const std::vector<unsigned char>& src, std::size_t expected_size, std::vector<unsigned char>& dst) {
    if (src.size() <= 4U) {
        return false;
    }
    const std::uint32_t prefixed = ReadU32LE(src, 0U);
    if (prefixed != expected_size) {
        return false;
    }
    std::vector<unsigned char> payload(src.begin() + 4, src.end());
    return Lz4DecompressRawExact(payload, expected_size, dst);
}

std::uint32_t ReadU32LE(const std::vector<unsigned char>& buf, std::size_t at) {
    return static_cast<std::uint32_t>(buf[at]) |
           (static_cast<std::uint32_t>(buf[at + 1U]) << 8U) |
           (static_cast<std::uint32_t>(buf[at + 2U]) << 16U) |
           (static_cast<std::uint32_t>(buf[at + 3U]) << 24U);
}

bool Lz4DecompressFrame(const std::vector<unsigned char>& src, std::size_t expected_size, std::vector<unsigned char>& dst) {
    // Minimal LZ4 frame decoder for independent blocks.
    if (src.size() < 7U) {
        return false;
    }
    if (!(src[0] == 0x04U && src[1] == 0x22U && src[2] == 0x4DU && src[3] == 0x18U)) {
        return false;
    }

    std::size_t at = 4U;
    const std::uint8_t flg = src[at++];
    const std::uint8_t bd = src[at++];

    const bool has_content_size = (flg & 0x08U) != 0U;
    const bool has_dict_id = (flg & 0x01U) != 0U;
    const bool has_block_checksum = (flg & 0x10U) != 0U;
    const bool has_content_checksum = (flg & 0x04U) != 0U;

    if (has_content_size) {
        if (at + 8U > src.size()) {
            return false;
        }
        at += 8U;
    }
    if (has_dict_id) {
        if (at + 4U > src.size()) {
            return false;
        }
        at += 4U;
    }

    // Header checksum byte.
    if (at + 1U > src.size()) {
        return false;
    }
    at += 1U;

    const std::uint8_t block_max_code = static_cast<std::uint8_t>((bd >> 4U) & 0x7U);
    std::size_t max_block_size = 4U * 1024U * 1024U;
    switch (block_max_code) {
        case 4:
            max_block_size = 64U * 1024U;
            break;
        case 5:
            max_block_size = 256U * 1024U;
            break;
        case 6:
            max_block_size = 1024U * 1024U;
            break;
        case 7:
            max_block_size = 4U * 1024U * 1024U;
            break;
        default:
            break;
    }

    dst.clear();
    dst.reserve(expected_size);

    while (true) {
        if (at + 4U > src.size()) {
            return false;
        }
        std::uint32_t block_size = ReadU32LE(src, at);
        at += 4U;
        if (block_size == 0U) {
            break;
        }

        const bool uncompressed = (block_size & 0x80000000U) != 0U;
        block_size &= 0x7FFFFFFFU;
        if (at + block_size > src.size()) {
            return false;
        }

        if (uncompressed) {
            if (dst.size() + block_size > expected_size) {
                return false;
            }
            dst.insert(dst.end(), src.begin() + static_cast<std::ptrdiff_t>(at), src.begin() + static_cast<std::ptrdiff_t>(at + block_size));
        } else {
            std::vector<unsigned char> decoded_block;
            const std::size_t remaining = expected_size - dst.size();
            const std::size_t max_out = std::min(max_block_size, remaining);
            const std::vector<unsigned char> compressed(src.begin() + static_cast<std::ptrdiff_t>(at),
                                                        src.begin() + static_cast<std::ptrdiff_t>(at + block_size));
            if (!Lz4DecompressRawBounded(compressed, max_out, decoded_block)) {
                return false;
            }
            if (dst.size() + decoded_block.size() > expected_size) {
                return false;
            }
            dst.insert(dst.end(), decoded_block.begin(), decoded_block.end());
        }
        at += block_size;

        if (has_block_checksum) {
            if (at + 4U > src.size()) {
                return false;
            }
            at += 4U;
        }
    }

    if (has_content_checksum) {
        if (at + 4U > src.size()) {
            return false;
        }
        at += 4U;
    }

    return dst.size() == expected_size;
}

void* SzAllocForLzma(void* /*p*/, size_t size) {
    if (size == 0U) {
        return nullptr;
    }
    return std::malloc(size);
}

void SzFreeForLzma(void* /*p*/, void* address) {
    std::free(address);
}

bool LzmaDecompressExact(const std::vector<unsigned char>& src,
                         std::size_t expected_size,
                         std::vector<unsigned char>& dst,
                         std::string& variant_used,
                         std::string& error) {
    if (src.size() <= LZMA_PROPS_SIZE) {
        error = "LZMA decompression failed: missing properties";
        return false;
    }

    struct Attempt {
        std::size_t src_offset = 0U;
        const char* variant = "";
    };
    std::vector<Attempt> attempts;
    attempts.push_back({LZMA_PROPS_SIZE, "props-only-header"});
    if (src.size() > (LZMA_PROPS_SIZE + 8U)) {
        attempts.push_back({LZMA_PROPS_SIZE + 8U, "props+size-header"});
    }

    ISzAlloc alloc {};
    alloc.Alloc = SzAllocForLzma;
    alloc.Free = SzFreeForLzma;

    std::string last_err;
    for (const auto& attempt : attempts) {
        if (attempt.src_offset >= src.size()) {
            continue;
        }
        variant_used = attempt.variant;
        dst.assign(expected_size, 0U);
        SizeT dest_len = static_cast<SizeT>(expected_size);
        SizeT src_len = static_cast<SizeT>(src.size() - attempt.src_offset);
        ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
        const SRes res = LzmaDecode(
            dst.data(),
            &dest_len,
            src.data() + attempt.src_offset,
            &src_len,
            src.data(),
            LZMA_PROPS_SIZE,
            LZMA_FINISH_END,
            &status,
            &alloc);
        if (res == SZ_OK && dest_len == expected_size &&
            (status == LZMA_STATUS_FINISHED_WITH_MARK || status == LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK ||
             status == LZMA_STATUS_NOT_FINISHED)) {
            variant_used = attempt.variant;
            return true;
        }
        last_err = "variant=" + std::string(attempt.variant) + ", res=" + std::to_string(res) +
                   ", status=" + std::to_string(static_cast<int>(status)) +
                   ", out=" + std::to_string(static_cast<std::size_t>(dest_len));
    }

    error = "LZMA decompression failed: " + last_err;
    dst.clear();
    return false;
}

std::int64_t ScoreBlockLayout(const std::vector<BlockInfo>& blocks) {
    if (blocks.empty()) {
        return std::numeric_limits<std::int64_t>::min() / 2;
    }
    std::int64_t score = 0;
    std::uint64_t total_compressed = 0U;
    std::uint64_t total_uncompressed = 0U;
    for (std::size_t i = 0U; i < blocks.size(); ++i) {
        const auto& b = blocks[i];
        if (b.compressed_size == 0U || b.uncompressed_size == 0U) {
            return std::numeric_limits<std::int64_t>::min() / 2;
        }
        total_compressed += b.compressed_size;
        total_uncompressed += b.uncompressed_size;
        const auto mode = static_cast<std::uint8_t>(b.flags & 0x3FU);
        if (mode == 2U || mode == 3U) {
            score += 6;
        } else if (mode == 0U) {
            score += 1;
            if (b.compressed_size != b.uncompressed_size) {
                score -= 10;
            }
        } else if (mode == 1U) {
            score -= 3;
        } else {
            score -= 8;
        }
        if (i == 0U) {
            if (mode == 2U || mode == 3U) {
                score += 10;
            } else if (mode == 1U) {
                score -= 6;
            }
        }
        if (b.uncompressed_size >= b.compressed_size) {
            score += 1;
            if (b.compressed_size > 0U) {
                const double ratio =
                    static_cast<double>(b.uncompressed_size) / static_cast<double>(b.compressed_size);
                if (ratio >= 1.0 && ratio <= 4.0) {
                    score += 1;
                } else if (ratio > 10.0) {
                    score -= 2;
                }
            }
        }
        if (b.uncompressed_size > (64U * 1024U * 1024U)) {
            score -= 4;
        }
    }
    if (total_compressed > (1024ULL * 1024ULL * 1024ULL)) {
        score -= 16;
    }
    if (total_uncompressed > (1024ULL * 1024ULL * 1024ULL)) {
        score -= 16;
    }
    return score;
}

bool ParseMetadataTables(const std::vector<unsigned char>& metadata, ParsedMetadata& out, std::string& selected_layout, std::string& error) {
    constexpr std::size_t kExpectedHashBytes = 16U;
    if (metadata.size() < kExpectedHashBytes + 8U) {
        error = "metadata buffer too small";
        return false;
    }

    out.blocks.clear();
    out.nodes.clear();
    selected_layout.clear();
    const std::size_t header_at = kExpectedHashBytes;

    struct BlockLayoutVariant {
        const char* label;
        bool size_little_endian;
        bool swap_size_order;
        bool swap_flag_bytes;
    };
    struct CountEndianVariant {
        const char* label;
        bool little_endian;
    };
    constexpr std::array<BlockLayoutVariant, 8> kLayouts {{
        {"be", false, false, false},
        {"be-swap-size", false, true, false},
        {"be-swap-flags", false, false, true},
        {"be-swap-size-flags", false, true, true},
        {"le", true, false, false},
        {"le-swap-size", true, true, false},
        {"le-swap-flags", true, false, true},
        {"le-swap-size-flags", true, true, true},
    }};
    constexpr std::array<CountEndianVariant, 2> kCountEndians {{
        {"be", false},
        {"le", true},
    }};

    ParsedMetadata best_candidate {};
    std::int64_t best_score = std::numeric_limits<std::int64_t>::min();
    bool found = false;
    std::string last_layout_err;

    for (const auto& block_count_endian : kCountEndians) {
        for (const auto& node_count_endian : kCountEndians) {
            std::uint32_t block_count = 0;
            {
                std::size_t count_at = header_at;
                const bool ok_count = block_count_endian.little_endian
                                          ? ReadU32LESafe(metadata, count_at, block_count)
                                          : ReadU32BE(metadata, count_at, block_count);
                if (!ok_count || block_count == 0U || block_count > 16384U) {
                    continue;
                }
            }
            for (const auto& layout : kLayouts) {
        ParsedMetadata candidate {};
        std::size_t at = header_at;
        bool ok = true;
        if (block_count_endian.little_endian) {
            ok = ReadU32LESafe(metadata, at, block_count);
        } else {
            ok = ReadU32BE(metadata, at, block_count);
        }
        if (!ok) {
            last_layout_err = std::string(layout.label) + ": failed to read block count";
            continue;
        }
        candidate.blocks.reserve(static_cast<std::size_t>(block_count));
        for (std::uint32_t i = 0; i < block_count; ++i) {
            std::uint32_t a = 0U;
            std::uint32_t b = 0U;
            std::uint16_t flags_raw = 0U;
            if (layout.size_little_endian) {
                ok = ReadU32LESafe(metadata, at, a) && ReadU32LESafe(metadata, at, b) && ReadU16BE(metadata, at, flags_raw);
            } else {
                ok = ReadU32BE(metadata, at, a) && ReadU32BE(metadata, at, b) && ReadU16BE(metadata, at, flags_raw);
            }
            if (!ok) {
                last_layout_err = std::string(layout.label) + ": failed to read block info table";
                break;
            }
            BlockInfo bi;
            if (layout.swap_size_order) {
                bi.uncompressed_size = b;
                bi.compressed_size = a;
            } else {
                bi.uncompressed_size = a;
                bi.compressed_size = b;
            }
            bi.flags = layout.swap_flag_bytes
                           ? static_cast<std::uint16_t>((flags_raw >> 8U) | (flags_raw << 8U))
                           : flags_raw;
            candidate.blocks.push_back(bi);
        }
        if (!ok) {
            continue;
        }

        std::uint32_t node_count = 0;
        if (node_count_endian.little_endian) {
            ok = ReadU32LESafe(metadata, at, node_count);
        } else {
            ok = ReadU32BE(metadata, at, node_count);
        }
        if (!ok) {
            last_layout_err = std::string(layout.label) + ": failed to read node count";
            continue;
        }
        if (node_count == 0U || node_count > 262144U) {
            last_layout_err = std::string(layout.label) + ": invalid node count";
            continue;
        }
        candidate.nodes.reserve(node_count);
        for (std::uint32_t i = 0; i < node_count; ++i) {
            NodeInfo n;
            if (!ReadU64BE(metadata, at, n.offset) ||
                !ReadU64BE(metadata, at, n.size) ||
                !ReadU32BE(metadata, at, n.flags) ||
                !ReadCString(metadata, at, n.path)) {
                ok = false;
                last_layout_err = std::string(layout.label) + ": failed to read node table";
                break;
            }
            candidate.nodes.push_back(std::move(n));
        }
        if (!ok) {
            continue;
        }

        std::uint64_t total_uncompressed = 0U;
        for (const auto& b : candidate.blocks) {
            total_uncompressed += b.uncompressed_size;
        }
        std::uint64_t max_node_end = 0U;
        for (const auto& n : candidate.nodes) {
            max_node_end = std::max<std::uint64_t>(max_node_end, n.offset + n.size);
        }
        if (max_node_end > total_uncompressed) {
            last_layout_err = std::string(layout.label) + ": node range exceeds block total";
            continue;
        }

        auto score = ScoreBlockLayout(candidate.blocks);
        if (!candidate.nodes.empty() && candidate.nodes.front().path.rfind("CAB-", 0U) == 0U) {
            score += 4;
        }
        if (HasLikelySerializedNode(candidate)) {
            score += 6;
        }
        if (!found || score > best_score) {
            best_candidate = std::move(candidate);
            selected_layout = std::string(layout.label) + "/bc-" + block_count_endian.label + "/nc-" + node_count_endian.label;
            best_score = score;
            found = true;
        }
    }
        }
    }

    if (!found) {
        error = last_layout_err.empty() ? "failed to parse metadata tables" : last_layout_err;
        return false;
    }
    out = std::move(best_candidate);
    return true;
}

bool DecompressByMode(const std::vector<unsigned char>& src,
                      std::size_t expected_size,
                      std::uint8_t mode,
                      std::vector<unsigned char>& dst,
                      std::string& error,
                      bool* out_lzma_attempted = nullptr,
                      std::string* out_lzma_variant = nullptr) {
    dst.clear();
    if (out_lzma_attempted != nullptr) {
        *out_lzma_attempted = false;
    }
    if (out_lzma_variant != nullptr) {
        out_lzma_variant->clear();
    }
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
            if (!Lz4DecompressRawExact(src, expected_size, dst)) {
                if (!Lz4DecompressFrame(src, expected_size, dst) &&
                    !Lz4DecompressSizePrefixed(src, expected_size, dst)) {
                    constexpr std::size_t kHardCap = 256U * 1024U * 1024U;
                    std::size_t fallback_cap = expected_size;
                    if (fallback_cap < src.size()) {
                        fallback_cap = src.size() * 8U;
                    } else {
                        fallback_cap = std::min(kHardCap, expected_size + (src.size() * 4U));
                    }
                    fallback_cap = std::max<std::size_t>(fallback_cap, src.size() * 2U);
                    fallback_cap = std::min(kHardCap, fallback_cap);
                    if (!Lz4DecompressRawBounded(src, fallback_cap, dst) || dst.empty()) {
                        error = "LZ4 decompression failed";
                        return false;
                    }
                }
            }
            return true;
        case 1:
            if (out_lzma_attempted != nullptr) {
                *out_lzma_attempted = true;
            }
            {
                std::string variant;
                if (!LzmaDecompressExact(src, expected_size, dst, variant, error)) {
                    if (out_lzma_variant != nullptr && !variant.empty()) {
                        *out_lzma_variant = variant;
                    }
                    return false;
                }
                if (out_lzma_variant != nullptr && !variant.empty()) {
                    *out_lzma_variant = variant;
                }
            }
            return true;
        default:
            error = "unsupported compression mode";
            return false;
    }
}

std::vector<std::uint8_t> BuildModeCandidates(std::uint8_t primary, std::uint8_t secondary) {
    std::vector<std::uint8_t> out;
    auto add_unique = [&](std::uint8_t m) {
        if (std::find(out.begin(), out.end(), m) == out.end()) {
            out.push_back(m);
        }
    };
    add_unique(primary);
    add_unique(secondary);
    add_unique(2U);
    add_unique(3U);
    add_unique(1U);
    add_unique(0U);
    return out;
}

std::vector<std::uint8_t> BuildBlockModeCandidates(std::uint8_t block_mode,
                                                   std::uint8_t header_mode,
                                                   bool block0,
                                                   const std::unordered_map<std::uint8_t, std::uint32_t>& mode_fail_hits) {
    std::vector<std::uint8_t> out;
    auto add_unique = [&](std::uint8_t m) {
        if (std::find(out.begin(), out.end(), m) == out.end()) {
            out.push_back(m);
        }
    };
    add_unique(block_mode);
    add_unique(header_mode);
    add_unique(2U);
    add_unique(3U);
    add_unique(1U);
    add_unique(0U);

    auto BasePriority = [&](std::uint8_t mode) -> std::uint32_t {
        if (block0) {
            if (mode == 2U) {
                return 0U;
            }
            if (mode == 3U) {
                return 1U;
            }
            if (mode == block_mode) {
                return 2U;
            }
            if (mode == header_mode) {
                return 3U;
            }
            if (mode == 1U) {
                return 4U;
            }
            if (mode == 0U) {
                return 5U;
            }
            return 6U;
        }
        if (mode == block_mode) {
            return 0U;
        }
        if (mode == header_mode) {
            return 1U;
        }
        if (mode == 2U || mode == 3U) {
            return 2U;
        }
        if (mode == 1U) {
            return 3U;
        }
        if (mode == 0U) {
            return 4U;
        }
        return 5U;
    };

    std::stable_sort(out.begin(), out.end(), [&](std::uint8_t lhs, std::uint8_t rhs) {
        const auto lhs_hits = mode_fail_hits.contains(lhs) ? mode_fail_hits.at(lhs) : 0U;
        const auto rhs_hits = mode_fail_hits.contains(rhs) ? mode_fail_hits.at(rhs) : 0U;
        if (lhs_hits != rhs_hits) {
            return lhs_hits < rhs_hits;
        }
        return BasePriority(lhs) < BasePriority(rhs);
    });
    return out;
}

std::vector<std::uint64_t> BuildMetadataOffsetCandidates(std::uint64_t primary_offset, std::uint64_t bundle_size, std::uint32_t compressed_metadata_size) {
    std::set<std::uint64_t> unique;
    const auto add = [&](std::uint64_t v) {
        if (v < bundle_size && v + compressed_metadata_size <= bundle_size) {
            unique.insert(v);
        }
    };

    add(primary_offset);
    add(primary_offset >= 16ULL ? primary_offset - 16ULL : primary_offset);
    add(primary_offset + 16ULL);
    add(primary_offset >= 32ULL ? primary_offset - 32ULL : primary_offset);
    add(primary_offset + 32ULL);
    add(primary_offset & ~15ULL);
    add((primary_offset + 15ULL) & ~15ULL);

    // Tail-window scan for bundles where metadata-at-end offset is shifted by padding.
    const std::uint64_t nominal = (bundle_size >= compressed_metadata_size) ? (bundle_size - compressed_metadata_size) : 0ULL;
    const std::uint64_t start = (nominal > 4096ULL) ? (nominal - 4096ULL) : 0ULL;
    const std::uint64_t end = std::min<std::uint64_t>(bundle_size, nominal + 4096ULL);
    for (std::uint64_t off = start; off + compressed_metadata_size <= end; off += 16ULL) {
        add(off);
    }

    return std::vector<std::uint64_t>(unique.begin(), unique.end());
}

std::vector<std::uint64_t> MergeOffsets(const std::vector<std::uint64_t>& a, const std::vector<std::uint64_t>& b) {
    std::set<std::uint64_t> s;
    s.insert(a.begin(), a.end());
    s.insert(b.begin(), b.end());
    return std::vector<std::uint64_t>(s.begin(), s.end());
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

    const auto mode = static_cast<std::uint8_t>(header.flags & 0x3FU);
    std::string errs;
    const auto mode_candidates = BuildModeCandidates(mode, mode);

    auto TryDecoded = [&](const std::vector<unsigned char>& decoded, const char* label, std::uint8_t mode_used) -> bool {
        if (decoded.size() != header.uncompressed_metadata_size) {
            errs += std::string(label) + ": metadata size mismatch; ";
            return false;
        }
        std::string parse_err;
        ParsedMetadata parsed {};
        std::string block_layout;
        if (!ParseMetadataTables(decoded, parsed, block_layout, parse_err)) {
            errs += std::string(label) + ": " + parse_err + "; ";
            return false;
        }
        metadata = std::move(parsed);
        probe.metadata_decode_strategy = label;
        probe.metadata_decode_mode = static_cast<std::uint32_t>(mode_used);
        probe.selected_block_layout = block_layout;
        probe.metadata_decode_error_code.clear();
        return true;
    };

    // Attempt 1: whole metadata blob is compressed.
    for (const auto m : mode_candidates) {
        {
            std::vector<unsigned char> decoded;
            std::string dec_err;
            if (DecompressByMode(compressed, header.uncompressed_metadata_size, m, decoded, dec_err)) {
                if (TryDecoded(decoded, "whole", m)) {
                    break;
                }
            } else {
                errs += "whole(mode=" + std::to_string(m) + "): " + dec_err + "; ";
            }
        }
    }

    // Attempt 2: raw prefix + compressed tail. Try multiple prefix sizes.
    if (!probe.metadata_parsed) {
        const std::array<std::size_t, 9> prefixes {0U, 4U, 8U, 12U, 16U, 20U, 24U, 28U, 32U};
        for (const auto prefix : prefixes) {
            if (probe.metadata_parsed) {
                break;
            }
            if (header.uncompressed_metadata_size < prefix || header.compressed_metadata_size < prefix) {
                continue;
            }
            std::vector<unsigned char> tail_src(
                compressed.begin() + static_cast<std::ptrdiff_t>(prefix),
                compressed.end());
            for (const auto m : mode_candidates) {
                std::vector<unsigned char> tail_decoded;
                std::string dec_err;
                if (DecompressByMode(
                        tail_src,
                        static_cast<std::size_t>(header.uncompressed_metadata_size - prefix),
                        m,
                        tail_decoded,
                        dec_err)) {
                    std::vector<unsigned char> decoded;
                    decoded.reserve(header.uncompressed_metadata_size);
                    decoded.insert(decoded.end(), compressed.begin(), compressed.begin() + static_cast<std::ptrdiff_t>(prefix));
                    decoded.insert(decoded.end(), tail_decoded.begin(), tail_decoded.end());
                    const std::string label = "prefix-" + std::to_string(prefix);
                    if (TryDecoded(decoded, label.c_str(), m)) {
                        break;
                    }
                } else {
                    errs += "prefix-" + std::to_string(prefix) + "(mode=" + std::to_string(m) + "): " + dec_err + "; ";
                }
            }
        }
    }

    if (metadata.blocks.empty() && metadata.nodes.empty()) {
        ParsedMetadata raw_parsed {};
        std::string raw_err;
        std::string block_layout;
        if (ParseMetadataTables(compressed, raw_parsed, block_layout, raw_err)) {
            metadata = std::move(raw_parsed);
            probe.metadata_decode_strategy = "raw-direct";
            probe.metadata_decode_mode = static_cast<std::uint32_t>(mode);
            probe.selected_block_layout = block_layout;
            probe.metadata_decode_error_code = "META_HEADER_SIZE_MISMATCH_RECOVERED";
        }
    }

    if (metadata.blocks.empty() && metadata.nodes.empty()) {
        probe.metadata_decode_error_code = "META_DECODE_FAILED";
        error = errs.empty() ? "metadata decompression failed" : errs;
        return false;
    }

    probe.metadata_parsed = true;
    probe.metadata_offset = metadata_offset;
    probe.block_count = static_cast<std::uint32_t>(metadata.blocks.size());
    probe.node_count = static_cast<std::uint32_t>(metadata.nodes.size());
    probe.total_block_compressed_size = 0U;
    probe.total_block_uncompressed_size = 0U;
    for (const auto& b : metadata.blocks) {
        probe.total_block_compressed_size += b.compressed_size;
        probe.total_block_uncompressed_size += b.uncompressed_size;
    }
    if (!metadata.nodes.empty()) {
        probe.first_node_path = metadata.nodes.front().path;
    }
    return true;
}

bool HasLikelySerializedNode(const ParsedMetadata& metadata) {
    for (const auto& n : metadata.nodes) {
        if (n.path.rfind("CAB-", 0U) == 0U || n.path.find(".assets") != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool ValidateParsedMetadataCandidate(const UnityFsHeader& header,
                                     const ParsedMetadata& metadata,
                                     std::uint64_t after_header,
                                     std::uint64_t metadata_offset,
                                     bool metadata_at_end,
                                     std::string& error) {
    if (metadata.blocks.empty() || metadata.nodes.empty()) {
        error = "metadata has empty block/node table";
        return false;
    }

    std::uint64_t total_compressed = 0U;
    std::uint64_t total_uncompressed = 0U;
    for (const auto& b : metadata.blocks) {
        total_compressed += b.compressed_size;
        total_uncompressed += b.uncompressed_size;
        if (b.compressed_size == 0U || b.uncompressed_size == 0U) {
            error = "metadata block has zero size";
            return false;
        }
    }

    if (total_compressed == 0U || total_uncompressed == 0U) {
        error = "invalid metadata block totals";
        return false;
    }

    const std::uint64_t data_start_after_metadata = metadata_offset + header.compressed_metadata_size;
    if (data_start_after_metadata > header.bundle_file_size) {
        error = "metadata range exceeds bundle size";
        return false;
    }

    for (const auto& n : metadata.nodes) {
        if (n.size == 0U) {
            continue;
        }
        if (n.offset + n.size > total_uncompressed) {
            error = "node range exceeds reconstructed stream size";
            return false;
        }
    }

    if (!HasLikelySerializedNode(metadata)) {
        error = "no likely serialized node path in metadata";
        return false;
    }

    return true;
}

std::int64_t ScoreMetadataCandidate(const UnityFsHeader& header,
                                    const ParsedMetadata& metadata,
                                    std::uint64_t after_header,
                                    std::uint64_t metadata_offset,
                                    std::uint64_t primary_metadata_offset,
                                    bool metadata_at_end) {
    std::int64_t score = 0;
    std::uint64_t total_compressed = 0U;
    std::uint64_t total_uncompressed = 0U;
    for (const auto& b : metadata.blocks) {
        total_compressed += b.compressed_size;
        total_uncompressed += b.uncompressed_size;
    }

    if (metadata_offset >= after_header) {
        score += 2;
    }
    if (HasLikelySerializedNode(metadata)) {
        score += 10;
    }
    if (metadata_at_end && metadata_offset > header.bundle_file_size / 2U) {
        score += 8;
    }

    const auto diff = (metadata_offset > primary_metadata_offset)
                          ? (metadata_offset - primary_metadata_offset)
                          : (primary_metadata_offset - metadata_offset);
    if (diff < 4U * 1024U) {
        score += 8;
    } else if (diff < 64U * 1024U) {
        score += 4;
    } else if (diff < 1024U * 1024U) {
        score += 2;
    }

    if (!metadata.nodes.empty() && metadata.nodes.front().path.rfind("CAB-", 0U) == 0U) {
        score += 4;
    }
    if (total_uncompressed > 0U) {
        score += 1;
    }
    if (metadata_at_end) {
        if (total_compressed <= metadata_offset) {
            score += 8;
        } else {
            score -= 12;
        }
    } else {
        const auto data_after_meta = metadata_offset + header.compressed_metadata_size;
        if (data_after_meta <= header.bundle_file_size &&
            data_after_meta + total_compressed <= header.bundle_file_size) {
            score += 8;
        } else {
            score -= 8;
        }
    }
    if (total_compressed > header.bundle_file_size) {
        score -= 24;
    }
    return score;
}

std::string ExtractFailureCode(const std::string& message) {
    const std::string token = "code=";
    const auto at = message.find(token);
    if (at == std::string::npos) {
        return {};
    }
    const auto start = at + token.size();
    auto end = message.find(',', start);
    if (end == std::string::npos) {
        end = message.find(';', start);
    }
    if (end == std::string::npos) {
        end = message.size();
    }
    if (end <= start) {
        return {};
    }
    return message.substr(start, end - start);
}

std::string ClassifyFailureCode(const std::string& detail) {
    if (detail.find("size implausible") != std::string::npos) {
        return "DATA_BLOCK_SIZE_IMPLAUSIBLE";
    }
    if (detail.find("failed to seek data block") != std::string::npos) {
        return "DATA_BLOCK_SEEK_FAILED";
    }
    if (detail.find("out of bundle range") != std::string::npos) {
        return "DATA_BLOCK_RANGE_FAILED";
    }
    if (detail.find("failed to read data block") != std::string::npos) {
        return "DATA_BLOCK_READ_FAILED";
    }
    if (detail.find("raw block size mismatch") != std::string::npos) {
        return "DATA_BLOCK_RAW_MISMATCH";
    }
    if (detail.find("LZ4 decompression failed") != std::string::npos) {
        return "DATA_BLOCK_LZ4_FAIL";
    }
    if (detail.find("LZMA decompression failed") != std::string::npos) {
        if (detail.find("missing properties") != std::string::npos) {
            return "DATA_BLOCK_LZMA_HEADER_INVALID";
        }
        return "DATA_BLOCK_LZMA_DECODE_FAILED";
    }
    if (detail.find("unsupported compression mode") != std::string::npos) {
        return "DATA_BLOCK_MODE_UNSUPPORTED";
    }
    if (detail.find("decompression failed") != std::string::npos) {
        return "DATA_BLOCK_DECOMPRESS_FAILED";
    }
    return "DATA_BLOCK_DECODE_FAILED";
}

bool ReconstructStreamAt(std::ifstream& in,
                         const UnityFsHeader& header,
                         const ParsedMetadata& metadata,
                         std::uint64_t data_start,
                         UnityFsProbe& probe,
                         std::vector<unsigned char>& out_stream,
                         std::uint32_t& decoded_block_count,
                         std::string& error) {
    out_stream.clear();
    decoded_block_count = 0U;
    probe.failed_block_index = 0U;
    probe.failed_block_mode = 0U;
    probe.failed_block_expected_size = 0U;
    probe.failed_block_read_offset = 0U;
    probe.failed_block_compressed_size = 0U;
    probe.failed_block_uncompressed_size = 0U;
    probe.failed_block_error_code.clear();
    probe.selected_reconstruction_layout.clear();
    probe.selected_block0_hypothesis.clear();
    probe.block0_attempt_count = 0U;
    probe.block0_mode_rank = 0U;
    probe.block0_selected_offset = 0U;
    probe.block0_selected_mode_source.clear();
    probe.lzma_decode_attempted = false;
    probe.lzma_decode_variant.clear();
    probe.recon_failure_detail_code.clear();
    std::uint32_t block_index = 0U;
    std::uint64_t offset = data_start;
    for (const auto& b : metadata.blocks) {
        struct BlockVariant {
            std::uint32_t uncompressed_size = 0U;
            std::uint32_t compressed_size = 0U;
            std::uint16_t flags = 0U;
            std::uint32_t offset_adjust = 0U;
            const char* label = "";
            std::int32_t score = 0;
        };
        std::vector<BlockVariant> variants;
        variants.reserve(8);
        const auto swapped_flags = static_cast<std::uint16_t>((b.flags >> 8U) | (b.flags << 8U));
        variants.push_back({b.uncompressed_size, b.compressed_size, b.flags, 0U, "orig", 0});
        variants.push_back({b.compressed_size, b.uncompressed_size, b.flags, 0U, "swap-size", -1});
        variants.push_back({b.uncompressed_size, b.compressed_size, swapped_flags, 0U, "swap-flags", -2});
        variants.push_back({b.compressed_size, b.uncompressed_size, swapped_flags, 0U, "swap-size-flags", -3});
        if (block_index == 0U) {
            if (b.compressed_size > 16U) {
                variants.push_back({b.uncompressed_size, static_cast<std::uint32_t>(b.compressed_size - 16U), b.flags, 0U, "orig-trim16", 3});
                variants.push_back({b.uncompressed_size, static_cast<std::uint32_t>(b.compressed_size - 16U), b.flags, 16U, "prefix-skip-16", 4});
            }
            if (b.compressed_size > 32U) {
                variants.push_back({b.uncompressed_size, static_cast<std::uint32_t>(b.compressed_size - 32U), b.flags, 0U, "orig-trim32", 2});
                variants.push_back({b.uncompressed_size, static_cast<std::uint32_t>(b.compressed_size - 32U), b.flags, 32U, "prefix-skip-32", 3});
            }
            if (offset < header.bundle_file_size) {
                const auto remaining = header.bundle_file_size - offset;
                if (remaining > 0U && remaining <= std::numeric_limits<std::uint32_t>::max()) {
                    variants.push_back({b.uncompressed_size, static_cast<std::uint32_t>(remaining), b.flags, 0U, "orig-clamp-range", 1});
                }
            }
        }
        std::stable_sort(variants.begin(), variants.end(), [](const BlockVariant& lhs, const BlockVariant& rhs) {
            return lhs.score > rhs.score;
        });

        bool decoded_ok = false;
        std::string last_err;
        std::string variant_errors;
        std::vector<unsigned char> decoded;
        std::uint64_t next_offset = offset;
        std::string chosen_variant;
        std::uint8_t chosen_mode = 0U;
        std::uint32_t chosen_mode_rank = 0U;
        std::string chosen_mode_source;
        std::unordered_map<std::uint8_t, std::uint32_t> mode_fail_hits;
        for (const auto& v : variants) {
            if (v.compressed_size == 0U || v.uncompressed_size == 0U) {
                continue;
            }
            const std::uint64_t read_offset = offset + v.offset_adjust;
            if (v.uncompressed_size > (256U * 1024U * 1024U)) {
                last_err = std::string(v.label) + ": uncompressed size implausible";
                if (variant_errors.size() < 512U) {
                    variant_errors += last_err + " | ";
                }
                continue;
            }
            if (read_offset + v.compressed_size > header.bundle_file_size) {
                last_err = std::string(v.label) + ": compressed size out of bundle range";
                if (variant_errors.size() < 512U) {
                    variant_errors += last_err + " | ";
                }
                continue;
            }

            in.clear();
            in.seekg(static_cast<std::streamoff>(read_offset), std::ios::beg);
            if (!in.good()) {
                probe.failed_block_index = block_index;
                probe.failed_block_mode = static_cast<std::uint32_t>(v.flags & 0x3FU);
                probe.failed_block_expected_size = v.uncompressed_size;
                probe.failed_block_read_offset = read_offset;
                probe.failed_block_compressed_size = v.compressed_size;
                probe.failed_block_uncompressed_size = v.uncompressed_size;
                probe.failed_block_error_code = "DATA_BLOCK_SEEK_FAILED";
                error = "failed to seek data block";
                return false;
            }

            std::vector<unsigned char> compressed(v.compressed_size, 0U);
            in.read(reinterpret_cast<char*>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
            if (static_cast<std::size_t>(in.gcount()) != compressed.size()) {
                last_err = std::string(v.label) + ": failed to read data block";
                if (variant_errors.size() < 512U) {
                    variant_errors += last_err + " | ";
                }
                continue;
            }

            const auto mode = static_cast<std::uint8_t>(v.flags & 0x3FU);
            const auto header_mode = static_cast<std::uint8_t>(header.flags & 0x3FU);
            const auto candidates = BuildBlockModeCandidates(mode, header_mode, block_index == 0U, mode_fail_hits);
            for (std::size_t mode_rank = 0U; mode_rank < candidates.size(); ++mode_rank) {
                const auto m = candidates[mode_rank];
                if (block_index == 0U) {
                    ++probe.block0_attempt_count;
                }
                std::string local_err;
                bool lzma_attempted = false;
                std::string lzma_variant;
                if (DecompressByMode(
                        compressed,
                        v.uncompressed_size,
                        m,
                        decoded,
                        local_err,
                        &lzma_attempted,
                        &lzma_variant)) {
                    probe.failed_block_mode = static_cast<std::uint32_t>(mode);
                    probe.failed_block_expected_size = v.uncompressed_size;
                    probe.failed_block_read_offset = read_offset;
                    probe.failed_block_compressed_size = v.compressed_size;
                    probe.failed_block_uncompressed_size = v.uncompressed_size;
                    decoded_ok = true;
                    next_offset = read_offset + v.compressed_size;
                    chosen_variant = v.label;
                    chosen_mode = m;
                    chosen_mode_rank = static_cast<std::uint32_t>(mode_rank + 1U);
                    if (m == header_mode) {
                        chosen_mode_source = "header-derived";
                    } else if (m == mode) {
                        chosen_mode_source = "block-flag";
                    } else {
                        chosen_mode_source = "fallback";
                    }
                    break;
                }
                if (lzma_attempted) {
                    probe.lzma_decode_attempted = true;
                    if (!lzma_variant.empty()) {
                        probe.lzma_decode_variant = lzma_variant;
                    }
                }
                ++mode_fail_hits[m];
                last_err = std::string(v.label) + ": " + local_err;
                if (variant_errors.size() < 512U) {
                    variant_errors += std::string(v.label) + "/mode=" + std::to_string(m) + ": " + local_err + " | ";
                }
            }
            if (decoded_ok) {
                break;
            }
        }
        if (!decoded_ok) {
            probe.failed_block_index = block_index;
            probe.failed_block_mode = static_cast<std::uint32_t>(b.flags & 0x3FU);
            probe.failed_block_expected_size = b.uncompressed_size;
            probe.failed_block_read_offset = offset;
            probe.failed_block_compressed_size = b.compressed_size;
            probe.failed_block_uncompressed_size = b.uncompressed_size;
            probe.failed_block_error_code = ClassifyFailureCode(last_err);
            probe.recon_failure_detail_code = probe.failed_block_error_code;
            if (block_index == 0U && probe.selected_block0_hypothesis.empty()) {
                const auto at = last_err.find(':');
                if (at != std::string::npos && at > 0U) {
                    probe.selected_block0_hypothesis = last_err.substr(0U, at);
                }
                probe.block0_selected_offset = data_start;
                probe.block0_selected_mode_source = "failed-candidate";
            }
            error = "data block decode failed: block=" + std::to_string(block_index) +
                    ", mode=" + std::to_string(probe.failed_block_mode) +
                    ", expected=" + std::to_string(b.uncompressed_size) +
                    ", code=" + probe.failed_block_error_code +
                    ", detail=" + (variant_errors.empty() ? last_err : variant_errors);
            return false;
        }
        offset = next_offset;
        if (block_index == 0U && !chosen_variant.empty()) {
            probe.selected_reconstruction_layout = chosen_variant + "/mode=" + std::to_string(chosen_mode);
            probe.selected_block0_hypothesis = probe.selected_reconstruction_layout;
            probe.block0_mode_rank = chosen_mode_rank;
            probe.block0_selected_offset = data_start;
            probe.block0_selected_mode_source = chosen_mode_source;
        }
        out_stream.insert(out_stream.end(), decoded.begin(), decoded.end());
        ++block_index;
        decoded_block_count = block_index;
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
                              UnityFsProbe& probe,
                              std::vector<unsigned char>& out_stream,
                              std::string& error) {
    std::uint64_t total_compressed = 0;
    for (const auto& b : metadata.blocks) {
        total_compressed += b.compressed_size;
    }

    struct CandidateStart {
        std::uint64_t offset = 0U;
        std::string family;
    };
    std::unordered_map<std::uint64_t, std::string> candidate_map;
    const auto add_candidate = [&](std::uint64_t v, const char* family) {
        if (v >= header.bundle_file_size) {
            return;
        }
        auto it = candidate_map.find(v);
        if (it == candidate_map.end()) {
            candidate_map.emplace(v, family);
        }
    };
    const auto add_window = [&](std::uint64_t anchor, const char* family, std::int64_t window, std::int64_t step) {
        for (std::int64_t d = -window; d <= window; d += step) {
            const auto value = static_cast<std::int64_t>(anchor) + d;
            if (value >= 0) {
                add_candidate(static_cast<std::uint64_t>(value), family);
            }
        }
    };

    const std::uint64_t after_metadata = after_header + header.compressed_metadata_size;
    const std::uint64_t aligned_after_metadata = (after_metadata + 15ULL) & ~15ULL;
    add_candidate(after_metadata, "after-metadata");
    add_candidate(aligned_after_metadata, "aligned-after-metadata");
    add_candidate(metadata_offset + header.compressed_metadata_size, "after-metadata");
    if (total_compressed <= header.bundle_file_size) {
        add_candidate(header.bundle_file_size - total_compressed, "tail-packed");
    }
    // Coarse pass for broad start offsets, then local fine pass around preferred anchors.
    add_window(after_metadata, "after-metadata", 4096, 128);
    add_window(aligned_after_metadata, "aligned-after-metadata", 4096, 128);
    add_window(after_header, "header-window", 4096, 128);
    if (total_compressed <= header.bundle_file_size) {
        add_window(header.bundle_file_size - total_compressed, "tail-window", 4096, 128);
    }
    if (metadata_at_end && metadata_offset >= total_compressed) {
        add_window(metadata_offset - total_compressed, "tail-window", 4096, 128);
    }
    add_window(after_metadata, "after-metadata", 512, 16);
    add_window(aligned_after_metadata, "aligned-after-metadata", 512, 16);
    add_window(metadata_offset + header.compressed_metadata_size, "after-metadata", 512, 16);
    std::vector<CandidateStart> candidates;
    candidates.reserve(candidate_map.size());
    for (const auto& [offset, family] : candidate_map) {
        candidates.push_back({offset, family});
    }
    std::sort(candidates.begin(), candidates.end(), [](const CandidateStart& lhs, const CandidateStart& rhs) {
        return lhs.offset < rhs.offset;
    });

    std::string attempt_errors;
    std::uint32_t best_partial_blocks = 0U;
    std::uint64_t best_partial_start = 0U;
    std::string best_partial_family;
    std::string best_partial_block0_hypothesis;
    std::uint32_t best_partial_block0_attempt_count = 0U;
    std::uint64_t best_partial_block0_selected_offset = 0U;
    std::string best_partial_block0_mode_source;
    std::vector<unsigned char> best_partial_stream;
    std::int32_t best_partial_score = std::numeric_limits<std::int32_t>::min();
    std::vector<unsigned char> best_success_stream;
    std::uint64_t best_success_start = 0U;
    std::string best_success_family;
    std::string best_success_block0_hypothesis;
    std::uint32_t best_success_block0_attempt_count = 0U;
    std::uint64_t best_success_block0_selected_offset = 0U;
    std::string best_success_block0_mode_source;
    std::int64_t best_success_score = std::numeric_limits<std::int64_t>::min();
    std::uint32_t best_success_blocks = 0U;
    probe.reconstruction_best_partial_blocks = 0U;
    probe.reconstruction_failure_summary_code.clear();
    probe.selected_offset_family.clear();
    probe.reconstruction_attempts = static_cast<std::uint32_t>(candidates.size());
    probe.reconstruction_candidate_count = static_cast<std::uint32_t>(candidates.size());
    probe.best_candidate_score = std::numeric_limits<std::int32_t>::min();
    std::unordered_map<std::string, std::uint32_t> failure_code_hits;
    auto FamilyPriority = [](const std::string& family) -> std::int32_t {
        if (family == "after-metadata") {
            return 4;
        }
        if (family == "aligned-after-metadata") {
            return 3;
        }
        if (family == "tail-packed") {
            return 2;
        }
        if (family == "header-window") {
            return 1;
        }
        if (family == "tail-window") {
            return 0;
        }
        return -1;
    };
    const auto ComputeCandidateScore = [&](std::uint32_t decoded_blocks,
                                           bool nodes_ok,
                                           const std::string& mode_source) -> std::int32_t {
        std::int32_t score = 0;
        const auto total_blocks = static_cast<std::uint32_t>(metadata.blocks.size());
        if (total_blocks > 0U) {
            const auto ratio_score = static_cast<std::int32_t>((100U * decoded_blocks) / total_blocks);
            score += ratio_score;
            if (decoded_blocks > 0U) {
                score += static_cast<std::int32_t>((40U * decoded_blocks * decoded_blocks) /
                                                   (total_blocks * total_blocks));
            }
        }
        if (nodes_ok) {
            score += 45;
        } else if (decoded_blocks > 0U) {
            score += 10;
        }
        if (mode_source == "header-derived" || mode_source == "block-flag") {
            score += 15;
        } else if (!mode_source.empty()) {
            score += 10;
        }
        return score;
    };
    for (const auto& candidate : candidates) {
        const auto start = candidate.offset;
        std::vector<unsigned char> reconstructed;
        std::uint32_t decoded_block_count = 0U;
        std::string local_err;
        if (!ReconstructStreamAt(in, header, metadata, start, probe, reconstructed, decoded_block_count, local_err)) {
            const auto failed_score = ComputeCandidateScore(decoded_block_count, false, probe.block0_selected_mode_source);
            probe.best_candidate_score = std::max(probe.best_candidate_score, failed_score);
            const auto code = ExtractFailureCode(local_err);
            if (!code.empty()) {
                ++failure_code_hits[code];
            }
            const auto better_partial_family = FamilyPriority(candidate.family) > FamilyPriority(best_partial_family);
            if (decoded_block_count > best_partial_blocks ||
                (decoded_block_count == best_partial_blocks && failed_score > best_partial_score) ||
                (decoded_block_count == best_partial_blocks && failed_score == best_partial_score && better_partial_family)) {
                best_partial_blocks = decoded_block_count;
                best_partial_score = failed_score;
                best_partial_start = start;
                best_partial_family = candidate.family;
                best_partial_block0_hypothesis = probe.selected_block0_hypothesis;
                best_partial_block0_attempt_count = probe.block0_attempt_count;
                best_partial_block0_selected_offset = probe.block0_selected_offset;
                best_partial_block0_mode_source = probe.block0_selected_mode_source;
                best_partial_stream = reconstructed;
                probe.reconstruction_best_partial_blocks = decoded_block_count;
            }
            if (attempt_errors.size() < 4096U) {
                attempt_errors += "start=" + std::to_string(start) + " (" + candidate.family + "): " + local_err + "; ";
            }
            continue;
        }
        if (!StreamMatchesNodes(metadata, reconstructed)) {
            const auto mismatch_score = ComputeCandidateScore(decoded_block_count, false, probe.block0_selected_mode_source);
            probe.best_candidate_score = std::max(probe.best_candidate_score, mismatch_score);
            if (attempt_errors.size() < 4096U) {
                attempt_errors += "start=" + std::to_string(start) + " (" + candidate.family + "): node range mismatch; ";
            }
            continue;
        }
        const auto success_score = ComputeCandidateScore(decoded_block_count, true, probe.block0_selected_mode_source);
        probe.best_candidate_score = std::max(probe.best_candidate_score, success_score);
        const auto better_family = FamilyPriority(candidate.family) > FamilyPriority(best_success_family);
        if (decoded_block_count > best_success_blocks ||
            (decoded_block_count == best_success_blocks && success_score > best_success_score) ||
            (decoded_block_count == best_success_blocks && success_score == best_success_score && better_family)) {
            best_success_score = success_score;
            best_success_blocks = decoded_block_count;
            best_success_start = start;
            best_success_family = candidate.family;
            best_success_stream = std::move(reconstructed);
            best_success_block0_hypothesis = probe.selected_block0_hypothesis;
            best_success_block0_attempt_count = probe.block0_attempt_count;
            best_success_block0_selected_offset = probe.block0_selected_offset;
            best_success_block0_mode_source = probe.block0_selected_mode_source;
        }
    }

    if (best_success_score != std::numeric_limits<std::int64_t>::min()) {
        out_stream = std::move(best_success_stream);
        probe.best_candidate_score = static_cast<std::int32_t>(best_success_score);
        probe.reconstruction_success_offset = best_success_start;
        probe.selected_offset_family = best_success_family;
        probe.selected_block0_hypothesis = best_success_block0_hypothesis;
        probe.block0_attempt_count = best_success_block0_attempt_count;
        probe.block0_selected_offset = best_success_block0_selected_offset;
        probe.block0_selected_mode_source = best_success_block0_mode_source;
        probe.reconstruction_failure_summary_code.clear();
        return true;
    }

    std::uint32_t best_hits = 0U;
    for (const auto& [code, hits] : failure_code_hits) {
        if (hits > best_hits) {
            best_hits = hits;
            probe.reconstruction_failure_summary_code = code;
        }
    }
    if (!probe.reconstruction_failure_summary_code.empty()) {
        probe.probe_primary_error = probe.reconstruction_failure_summary_code;
        probe.recon_failure_detail_code = probe.reconstruction_failure_summary_code;
    }
    if (probe.best_candidate_score == std::numeric_limits<std::int32_t>::min()) {
        probe.best_candidate_score = 0;
    }
    if (!best_partial_family.empty()) {
        probe.selected_offset_family = best_partial_family;
        if (probe.selected_block0_hypothesis.empty()) {
            probe.selected_block0_hypothesis = best_partial_block0_hypothesis;
        }
        if (probe.block0_attempt_count == 0U) {
            probe.block0_attempt_count = best_partial_block0_attempt_count;
        }
        if (probe.block0_selected_offset == 0U) {
            probe.block0_selected_offset = best_partial_block0_selected_offset;
        }
        if (probe.block0_selected_mode_source.empty()) {
            probe.block0_selected_mode_source = best_partial_block0_mode_source;
        }
        out_stream = std::move(best_partial_stream);
    }
    error = "failed to reconstruct uncompressed bundle data stream. " + attempt_errors;
    if (best_partial_blocks > 0U) {
        error += "best partial: start=" + std::to_string(best_partial_start) +
                 " (" + best_partial_family + ")" +
                 ", decoded_blocks=" + std::to_string(best_partial_blocks) +
                 "/" + std::to_string(metadata.blocks.size()) + "; ";
    }
    return false;
}

bool ParseSerializedFromNodes(const ParsedMetadata& metadata,
                              const std::vector<unsigned char>& stream,
                              UnityFsProbe& probe,
                              std::string& error) {
    SerializedFileReader serialized;
    auto CompactSerializedErrorCode = [](const std::string& message) -> std::string {
        if (message.empty()) {
            return {};
        }
        const auto colon = message.find(':');
        if (colon == std::string::npos) {
            return message;
        }
        return message.substr(0U, colon);
    };
    bool found_candidate = false;
    bool attempted_candidate = false;
    SerializedFileSummary best {};
    struct CandidateRef {
        std::uint64_t offset = 0U;
        std::uint64_t window_size = 0U;
        std::string label;
        std::int64_t score = 0;
    };
    std::vector<CandidateRef> candidates;
    candidates.reserve(metadata.nodes.size());
    std::unordered_set<std::uint64_t> candidate_keys;
    candidate_keys.reserve(metadata.nodes.size() * 2U + 64U);
    auto AddCandidate = [&](std::uint64_t offset,
                            std::uint64_t window_size,
                            std::string label,
                            std::int64_t score) {
        if (offset >= stream.size()) {
            return;
        }
        const auto remaining = static_cast<std::uint64_t>(stream.size()) - offset;
        if (remaining < 256U) {
            return;
        }
        window_size = std::min<std::uint64_t>(window_size, remaining);
        if (window_size < 256U) {
            return;
        }
        const std::uint64_t key = (offset << 20U) ^ window_size;
        if (!candidate_keys.insert(key).second) {
            return;
        }
        candidates.push_back({offset, window_size, std::move(label), score});
    };

    std::size_t named_candidates = 0U;
    for (std::size_t i = 0; i < metadata.nodes.size(); ++i) {
        const auto& node = metadata.nodes[i];
        if (node.size == 0U) {
            continue;
        }
        const bool cab_like = node.path.rfind("CAB-", 0U) == 0U;
        const bool assets_like = node.path.find(".assets") != std::string::npos;
        if (!cab_like && !assets_like) {
            continue;
        }
        std::int64_t score = 0;
        if (cab_like) {
            score += 10;
        }
        if (assets_like) {
            score += 8;
        }
        if (node.size >= 1024U) {
            score += 2;
        }
        if (node.size > (64U * 1024U * 1024U)) {
            score -= 4;
        }
        if (node.offset >= stream.size()) {
            score -= 10;
            continue;
        }
        const auto available = static_cast<std::uint64_t>(stream.size()) - node.offset;
        if (available < node.size) {
            score -= 5;
        }
        AddCandidate(
            node.offset,
            std::min<std::uint64_t>(node.size, available),
            node.path.empty() ? ("node#" + std::to_string(i)) : node.path,
            score);
        ++named_candidates;
    }

    // Fallback 1: if named candidates are unusable, try all nodes in the reconstructed stream.
    if (candidates.empty()) {
        for (std::size_t i = 0; i < metadata.nodes.size(); ++i) {
            const auto& node = metadata.nodes[i];
            if (node.size == 0U || node.offset >= stream.size()) {
                continue;
            }
            const auto available = static_cast<std::uint64_t>(stream.size()) - node.offset;
            std::int64_t score = 1;
            if (node.size >= 1024U) {
                score += 2;
            }
            if (!node.path.empty()) {
                score += 1;
            }
            if (available < node.size) {
                score -= 4;
            }
            AddCandidate(
                node.offset,
                std::min<std::uint64_t>(node.size, available),
                node.path.empty() ? ("node#" + std::to_string(i)) : node.path,
                score);
        }
    }

    // If node-derived candidates are scarce, widen around them to avoid header-alignment misses.
    if (!candidates.empty() && candidates.size() <= 2U) {
        const std::array<std::int64_t, 8U> expand_deltas = {-4096, -2048, -1024, -512, 512, 1024, 2048, 4096};
        const auto base_candidates = candidates;
        for (const auto& base : base_candidates) {
            for (const auto delta : expand_deltas) {
                if (delta < 0 && base.offset < static_cast<std::uint64_t>(-delta)) {
                    continue;
                }
                const auto expanded_offset =
                    static_cast<std::uint64_t>(static_cast<std::int64_t>(base.offset) + delta);
                if (expanded_offset >= stream.size()) {
                    continue;
                }
                const auto remaining = static_cast<std::uint64_t>(stream.size()) - expanded_offset;
                const auto expanded_window = std::min<std::uint64_t>(
                    remaining,
                    std::max<std::uint64_t>(base.window_size + 64U * 1024U, 512U * 1024U));
                AddCandidate(
                    expanded_offset,
                    expanded_window,
                    base.label + "@expand" + std::to_string(delta),
                    base.score - 1);
            }
        }
    }

    // Fallback 2: if node-based selection still finds nothing, scan for SerializedFile-like headers.
    if (candidates.empty()) {
        auto ReadU32BEAt = [&](std::size_t at) -> std::uint32_t {
            return (static_cast<std::uint32_t>(stream[at]) << 24U) |
                   (static_cast<std::uint32_t>(stream[at + 1U]) << 16U) |
                   (static_cast<std::uint32_t>(stream[at + 2U]) << 8U) |
                   static_cast<std::uint32_t>(stream[at + 3U]);
        };
        auto LooksLikeSerializedHeader = [&](std::size_t at) -> bool {
            if (at + 20U > stream.size()) {
                return false;
            }
            const auto metadata_size_be = ReadU32BEAt(at);
            const auto version_be = ReadU32BEAt(at + 8U);
            if (metadata_size_be == 0U || metadata_size_be > (64U * 1024U * 1024U)) {
                return false;
            }
            if (version_be == 0U || version_be > 40U) {
                return false;
            }
            const std::size_t header_size = version_be >= 22U ? 48U : 20U;
            const auto needed = static_cast<std::uint64_t>(header_size) + metadata_size_be;
            return static_cast<std::uint64_t>(stream.size()) - at >= needed;
        };

        const std::size_t scan_limit = std::min<std::size_t>(stream.size(), 16U * 1024U * 1024U);
        std::size_t scan_hits = 0U;
        for (std::size_t at = 0U; at + 64U <= scan_limit && scan_hits < 96U; at += 16U) {
            if (!LooksLikeSerializedHeader(at)) {
                continue;
            }
            AddCandidate(
                static_cast<std::uint64_t>(at),
                static_cast<std::uint64_t>(stream.size() - at),
                "scan#" + std::to_string(at),
                6);
            ++scan_hits;
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const CandidateRef& a, const CandidateRef& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        if (a.window_size != b.window_size) {
            return a.window_size > b.window_size;
        }
        return a.offset < b.offset;
    });
    probe.serialized_candidate_count = static_cast<std::uint32_t>(candidates.size());
    probe.serialized_attempt_count = 0U;
    probe.serialized_best_candidate_path.clear();
    probe.serialized_best_candidate_score = std::numeric_limits<std::int32_t>::min();
    probe.serialized_detail_error_code.clear();
    probe.serialized_last_failure_offset = 0U;
    probe.serialized_last_failure_window_size = 0U;
    probe.serialized_last_failure_code.clear();
    std::string best_failure_error;
    std::string best_failure_detail_code;
    std::uint64_t best_failure_offset = 0U;
    std::uint64_t best_failure_window_size = 0U;
    std::int32_t best_failure_score = std::numeric_limits<std::int32_t>::min();
    auto CountMajorTypeGroups = [](const SerializedFileSummary& summary) -> std::int32_t {
        std::int32_t groups = 0;
        if (summary.game_object_count > 0U) {
            ++groups;
        }
        if (summary.mesh_object_count > 0U) {
            ++groups;
        }
        if (summary.material_object_count > 0U) {
            ++groups;
        }
        if (summary.texture_object_count > 0U) {
            ++groups;
        }
        if (summary.skinned_mesh_renderer_count > 0U) {
            ++groups;
        }
        return groups;
    };
    auto ScoreParsedSummary = [&](const SerializedFileSummary& summary, std::int64_t candidate_score) -> std::int32_t {
        std::int32_t score = static_cast<std::int32_t>(candidate_score);
        score += static_cast<std::int32_t>(summary.object_count * 4U);
        score += CountMajorTypeGroups(summary) * 15;
        if (!summary.major_types_found.empty()) {
            score += 5;
        }
        return score;
    };
    // Keep near offsets first. Use extended sweep only when candidate count is low.
    const std::vector<std::int64_t> node_offset_deltas = (candidates.size() <= 8U)
                                                              ? std::vector<std::int64_t> {
                                                                    0, 16,  -16, 32,   -32,  64,   -64,
                                                                    128, -128, 256,  -256, 512,  -512, 1024,
                                                                    -1024, 1536, -1536, 2048, -2048, 2560,
                                                                    -2560, 3072, -3072, 3584, -3584, 4096, -4096}
                                                              : std::vector<std::int64_t> {
                                                                    0, 16, -16, 32, -32, 64, -64, 128, -128, 256, -256};
    std::unordered_set<std::uint64_t> tried_ranges;
    tried_ranges.reserve(candidates.size() * node_offset_deltas.size());
    const std::uint64_t kMinParseWindow = 512U * 1024U;

    for (const auto& candidate : candidates) {
        found_candidate = true;
        for (const auto delta : node_offset_deltas) {
            if (delta < 0 && candidate.offset < static_cast<std::uint64_t>(-delta)) {
                continue;
            }
            const auto adjusted_offset = static_cast<std::uint64_t>(static_cast<std::int64_t>(candidate.offset) + delta);
            if (adjusted_offset >= stream.size()) {
                continue;
            }
            const auto remaining = static_cast<std::uint64_t>(stream.size()) - adjusted_offset;
            const auto widened_window = std::max<std::uint64_t>(candidate.window_size, kMinParseWindow);
            const auto window_size = std::min<std::uint64_t>(widened_window, remaining);
            if (window_size < 256U) {
                continue;
            }
            const std::uint64_t key = (adjusted_offset << 16U) ^ window_size;
            if (!tried_ranges.insert(key).second) {
                continue;
            }

            attempted_candidate = true;
            ++probe.serialized_attempt_count;
            const auto begin = stream.begin() + static_cast<std::ptrdiff_t>(adjusted_offset);
            const auto end = begin + static_cast<std::ptrdiff_t>(window_size);
            std::vector<unsigned char> file_bytes(begin, end);
            auto parsed = serialized.ParseObjectSummary(file_bytes);
            if (!parsed.ok) {
                const auto fail_score = static_cast<std::int32_t>(candidate.score);
                if (fail_score >= best_failure_score) {
                    best_failure_score = fail_score;
                    best_failure_error = parsed.error;
                    best_failure_detail_code = CompactSerializedErrorCode(parsed.error);
                    best_failure_offset = adjusted_offset;
                    best_failure_window_size = window_size;
                    probe.serialized_best_candidate_path =
                        candidate.label + "@offset=" + std::to_string(adjusted_offset);
                }
                continue;
            }

            const auto parsed_score = ScoreParsedSummary(parsed.value, candidate.score);
            if (!probe.object_table_parsed ||
                parsed_score > probe.serialized_best_candidate_score ||
                (parsed_score == probe.serialized_best_candidate_score &&
                 CountMajorTypeGroups(parsed.value) > CountMajorTypeGroups(best))) {
                best = parsed.value;
                probe.object_table_parsed = true;
                probe.serialized_best_candidate_score = parsed_score;
                probe.serialized_best_candidate_path = candidate.label + "@offset=" + std::to_string(adjusted_offset);
            }
        }
    }

    if (!probe.object_table_parsed) {
        if (!best_failure_error.empty()) {
            probe.serialized_parse_error_code = best_failure_error;
            probe.serialized_detail_error_code = best_failure_detail_code;
            probe.serialized_last_failure_offset = best_failure_offset;
            probe.serialized_last_failure_window_size = best_failure_window_size;
            probe.serialized_last_failure_code = best_failure_detail_code;
        } else if (probe.serialized_parse_error_code.empty()) {
            if (!found_candidate) {
                probe.serialized_parse_error_code = named_candidates > 0U
                                                        ? "SF_NO_CANDIDATE_WINDOW"
                                                        : "SF_NO_CANDIDATE_NODE";
            } else if (!attempted_candidate) {
                probe.serialized_parse_error_code = "SF_NO_VALID_OFFSET_WINDOW";
            } else {
                probe.serialized_parse_error_code = "SF_NO_VALID_NODE_PARSED";
            }
            probe.serialized_detail_error_code = probe.serialized_parse_error_code;
        }
        probe.serialized_best_candidate_score = std::max(probe.serialized_best_candidate_score, 0);
        probe.probe_primary_error = probe.serialized_parse_error_code;
        error = found_candidate
                    ? "no candidate node could be parsed as SerializedFile"
                    : "no serialized candidate nodes found";
        return false;
    }

    if (probe.serialized_best_candidate_score == std::numeric_limits<std::int32_t>::min()) {
        probe.serialized_best_candidate_score = 0;
    }
    probe.serialized_parse_error_code.clear();
    probe.serialized_detail_error_code.clear();
    probe.serialized_last_failure_offset = 0U;
    probe.serialized_last_failure_window_size = 0U;
    probe.serialized_last_failure_code.clear();
    probe.object_count = best.object_count;
    probe.mesh_object_count = best.mesh_object_count;
    probe.material_object_count = best.material_object_count;
    probe.texture_object_count = best.texture_object_count;
    probe.game_object_count = best.game_object_count;
    probe.skinned_mesh_renderer_count = best.skinned_mesh_renderer_count;
    probe.major_types_found = best.major_types_found;
    probe.probe_primary_error.clear();
    return true;
}

bool TryParseSerializedFromRawBundle(std::ifstream& in, UnityFsProbe& probe, std::string& error) {
    auto CompactSerializedErrorCode = [](const std::string& message) -> std::string {
        if (message.empty()) {
            return {};
        }
        const auto colon = message.find(':');
        if (colon == std::string::npos) {
            return message;
        }
        return message.substr(0U, colon);
    };
    const auto original_pos = in.tellg();
    in.clear();
    in.seekg(0, std::ios::end);
    const auto file_size_stream = in.tellg();
    if (file_size_stream <= 0) {
        in.clear();
        in.seekg(original_pos, std::ios::beg);
        error = "raw serialized scan: failed to determine file size";
        return false;
    }
    const auto file_size = static_cast<std::uint64_t>(file_size_stream);
    const std::size_t max_read = 512U * 1024U * 1024U;
    const std::size_t read_size = static_cast<std::size_t>(std::min<std::uint64_t>(file_size, max_read));
    if (read_size < 64U) {
        in.clear();
        in.seekg(original_pos, std::ios::beg);
        error = "raw serialized scan: file too small";
        return false;
    }

    in.clear();
    in.seekg(0, std::ios::beg);
    std::vector<unsigned char> raw_bytes(read_size, 0U);
    in.read(reinterpret_cast<char*>(raw_bytes.data()), static_cast<std::streamsize>(raw_bytes.size()));
    const auto bytes_read = static_cast<std::size_t>(in.gcount());
    raw_bytes.resize(bytes_read);
    in.clear();
    in.seekg(original_pos, std::ios::beg);
    if (raw_bytes.size() < 64U) {
        error = "raw serialized scan: failed to read enough bytes";
        return false;
    }

    auto ReadU32BEAt = [&](std::size_t at) -> std::uint32_t {
        return (static_cast<std::uint32_t>(raw_bytes[at]) << 24U) |
               (static_cast<std::uint32_t>(raw_bytes[at + 1U]) << 16U) |
               (static_cast<std::uint32_t>(raw_bytes[at + 2U]) << 8U) |
               static_cast<std::uint32_t>(raw_bytes[at + 3U]);
    };
    auto LooksLikeSerializedHeader = [&](std::size_t at, std::uint32_t* out_metadata_size, std::uint32_t* out_version) -> bool {
        if (at + 20U > raw_bytes.size() || out_metadata_size == nullptr || out_version == nullptr) {
            return false;
        }
        const auto metadata_size = ReadU32BEAt(at);
        const auto version = ReadU32BEAt(at + 8U);
        if (metadata_size == 0U || metadata_size > (64U * 1024U * 1024U)) {
            return false;
        }
        if (version == 0U || version > 40U) {
            return false;
        }
        const std::size_t header_size = version >= 22U ? 48U : 20U;
        if (at + header_size + metadata_size > raw_bytes.size()) {
            return false;
        }
        *out_metadata_size = metadata_size;
        *out_version = version;
        return true;
    };
    auto CountMajorTypeGroups = [](const SerializedFileSummary& summary) -> std::int32_t {
        std::int32_t groups = 0;
        if (summary.game_object_count > 0U) {
            ++groups;
        }
        if (summary.mesh_object_count > 0U) {
            ++groups;
        }
        if (summary.material_object_count > 0U) {
            ++groups;
        }
        if (summary.texture_object_count > 0U) {
            ++groups;
        }
        if (summary.skinned_mesh_renderer_count > 0U) {
            ++groups;
        }
        return groups;
    };

    SerializedFileReader serialized;
    bool success = false;
    std::uint32_t best_objects = 0U;
    std::int32_t best_groups = std::numeric_limits<std::int32_t>::min();
    std::size_t attempts = 0U;
    std::size_t candidates = 0U;
    std::string best_error;
    std::string best_error_code;
    std::size_t best_error_offset = 0U;
    std::size_t best_error_window = 0U;
    SerializedFileSummary best {};
    std::size_t best_offset = 0U;
    const std::size_t scan_limit = raw_bytes.size();
    for (std::size_t at = 0U; at + 64U <= scan_limit && candidates < 20000U; at += 4U) {
        std::uint32_t metadata_size = 0U;
        std::uint32_t version = 0U;
        if (!LooksLikeSerializedHeader(at, &metadata_size, &version)) {
            continue;
        }
        ++candidates;
        ++attempts;
        const std::size_t header_size = version >= 22U ? 48U : 20U;
        const std::size_t needed_size = header_size + static_cast<std::size_t>(metadata_size);
        const std::size_t sample_window = std::min<std::size_t>(
            static_cast<std::size_t>(raw_bytes.size() - at),
            std::max<std::size_t>(needed_size, 4U * 1024U * 1024U));
        std::vector<unsigned char> candidate_bytes(
            raw_bytes.begin() + static_cast<std::ptrdiff_t>(at),
            raw_bytes.begin() + static_cast<std::ptrdiff_t>(at + sample_window));
        auto parsed = serialized.ParseObjectSummary(candidate_bytes);
        if (!parsed.ok) {
            best_error = parsed.error;
            best_error_code = CompactSerializedErrorCode(parsed.error);
            best_error_offset = at;
            best_error_window = sample_window;
            continue;
        }
        const auto groups = CountMajorTypeGroups(parsed.value);
        if (!success ||
            parsed.value.object_count > best_objects ||
            (parsed.value.object_count == best_objects && groups > best_groups)) {
            success = true;
            best = parsed.value;
            best_objects = parsed.value.object_count;
            best_groups = groups;
            best_offset = at;
        }
    }

    probe.serialized_candidate_count = std::max(probe.serialized_candidate_count, static_cast<std::uint32_t>(candidates));
    probe.serialized_attempt_count += static_cast<std::uint32_t>(attempts);

    if (!success) {
        probe.serialized_last_failure_offset = best_error_offset;
        probe.serialized_last_failure_window_size = best_error_window;
        if (!best_error_code.empty()) {
            probe.serialized_last_failure_code = best_error_code;
            probe.serialized_detail_error_code = best_error_code;
        } else if (candidates == 0U) {
            probe.serialized_last_failure_code = "SF_NO_RAW_HEADER_CANDIDATE";
            probe.serialized_detail_error_code = "SF_NO_RAW_HEADER_CANDIDATE";
        } else {
            probe.serialized_last_failure_code = "SF_RAW_PARSE_FAILED";
            probe.serialized_detail_error_code = "SF_RAW_PARSE_FAILED";
        }
        if (best_error.empty()) {
            error = "raw serialized scan: no valid serialized header candidates";
        } else {
            error = "raw serialized scan: " + best_error;
        }
        return false;
    }

    probe.object_table_parsed = true;
    probe.object_count = best.object_count;
    probe.mesh_object_count = best.mesh_object_count;
    probe.material_object_count = best.material_object_count;
    probe.texture_object_count = best.texture_object_count;
    probe.game_object_count = best.game_object_count;
    probe.skinned_mesh_renderer_count = best.skinned_mesh_renderer_count;
    probe.major_types_found = best.major_types_found;
    probe.serialized_best_candidate_score =
        std::max(probe.serialized_best_candidate_score, static_cast<std::int32_t>(best.object_count * 4U + best_groups * 15));
    probe.serialized_best_candidate_path = "raw-scan@offset=" + std::to_string(best_offset);
    probe.serialized_parse_error_code.clear();
    probe.serialized_detail_error_code.clear();
    probe.serialized_last_failure_offset = 0U;
    probe.serialized_last_failure_window_size = 0U;
    probe.serialized_last_failure_code.clear();
    probe.probe_primary_error.clear();
    error.clear();
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
    probe.probe_stage = "header";
    probe.probe_primary_error.clear();
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
    probe.probe_stage = "metadata-candidate";

    const auto after_header = static_cast<std::uint64_t>(in.tellg());
    // UnityFS bundles in the sample set use 0x40 for "block info at end".
    // Some variants also use 0x80, so treat either as metadata-at-end.
    const bool metadata_at_end = (probe.header.flags & 0x40U) != 0U || (probe.header.flags & 0x80U) != 0U;
    const std::uint64_t primary_metadata_offset =
        metadata_at_end ? (probe.header.bundle_file_size - probe.header.compressed_metadata_size) : after_header;

    ParsedMetadata metadata;
    std::string meta_err;
    bool metadata_ok = false;
    auto metadata_candidates = BuildMetadataOffsetCandidates(
        primary_metadata_offset,
        probe.header.bundle_file_size,
        probe.header.compressed_metadata_size);
    // Always include header-adjacent candidates because sample bundles may diverge from flag semantics.
    const auto header_candidates = BuildMetadataOffsetCandidates(
        after_header,
        probe.header.bundle_file_size,
        probe.header.compressed_metadata_size);
    metadata_candidates = MergeOffsets(metadata_candidates, header_candidates);
    UnityFsProbe best_probe = probe;
    ParsedMetadata best_metadata;
    std::int64_t best_score = std::numeric_limits<std::int64_t>::min();
    bool best_found = false;
    std::size_t err_count = 0U;
    for (const auto candidate : metadata_candidates) {
        std::string candidate_err;
        UnityFsProbe probe_candidate = probe;
        ParsedMetadata metadata_candidate;
        if (ParseMetadataBlock(in, candidate, probe.header, probe_candidate, metadata_candidate, candidate_err)) {
            std::string validate_err;
            if (ValidateParsedMetadataCandidate(
                    probe.header,
                    metadata_candidate,
                    after_header,
                    candidate,
                    metadata_at_end,
                    validate_err)) {
                const auto score = ScoreMetadataCandidate(
                    probe.header,
                    metadata_candidate,
                    after_header,
                    candidate,
                    primary_metadata_offset,
                    metadata_at_end);
                if (!best_found || score > best_score) {
                    best_probe = std::move(probe_candidate);
                    best_metadata = std::move(metadata_candidate);
                    best_score = score;
                    best_found = true;
                }
                continue;
            }
            if (err_count < 8U) {
                meta_err += "offset=" + std::to_string(candidate) + ": " + validate_err + "; ";
            }
            ++err_count;
            continue;
        }
        if (err_count < 8U) {
            meta_err += "offset=" + std::to_string(candidate) + ": " + candidate_err + "; ";
        }
        ++err_count;
    }
    if (best_found) {
        probe = std::move(best_probe);
        metadata = std::move(best_metadata);
        metadata_ok = true;
        meta_err.clear();
        probe.probe_stage = "metadata-parsed";
    }
    if (!metadata_ok && err_count > 8U) {
        meta_err += "(+ " + std::to_string(err_count - 8U) + " more offsets)";
    }

    if (!metadata_ok && !metadata_at_end) {
        const std::uint64_t aligned = (after_header + 15ULL) & ~15ULL;
        std::string aligned_err;
        UnityFsProbe probe_candidate = probe;
        ParsedMetadata metadata_candidate;
        if (ParseMetadataBlock(in, aligned, probe.header, probe_candidate, metadata_candidate, aligned_err)) {
            std::string validate_err;
            if (ValidateParsedMetadataCandidate(
                    probe.header,
                    metadata_candidate,
                    after_header,
                    aligned,
                    metadata_at_end,
                    validate_err)) {
                probe = std::move(probe_candidate);
                metadata = std::move(metadata_candidate);
                metadata_ok = true;
                meta_err.clear();
            } else {
                meta_err += "aligned-fallback=" + std::to_string(aligned) + ": " + validate_err + "; ";
            }
        } else {
            meta_err += "aligned-fallback=" + std::to_string(aligned) + ": " + aligned_err + "; ";
        }
    }
    probe.metadata_error = meta_err;
    if (!probe.metadata_parsed) {
        probe.probe_stage = "failed-metadata";
        if (!probe.metadata_decode_error_code.empty()) {
            probe.probe_primary_error = probe.metadata_decode_error_code;
        } else {
            probe.probe_primary_error = "META_PARSE_FAILED";
        }
    }

    if (probe.metadata_parsed) {
        probe.probe_stage = "reconstruction";
        std::vector<unsigned char> stream;
        std::string stream_err;
        if (TryReconstructDataStream(
                in,
                probe.header,
                metadata,
                after_header,
                probe.metadata_offset > 0U ? probe.metadata_offset : primary_metadata_offset,
                metadata_at_end,
                probe,
                stream,
                stream_err)) {
            probe.probe_stage = "serialized";
            std::string serialized_err;
            if (!ParseSerializedFromNodes(metadata, stream, probe, serialized_err)) {
                std::string raw_scan_err;
                if (TryParseSerializedFromRawBundle(in, probe, raw_scan_err)) {
                    probe.probe_stage = "complete";
                    if (!probe.metadata_error.empty()) {
                        probe.metadata_error += "; ";
                    }
                    probe.metadata_error += "raw serialized fallback engaged";
                } else {
                    if (!probe.metadata_error.empty()) {
                        probe.metadata_error += "; ";
                    }
                    probe.metadata_error += serialized_err;
                    if (!raw_scan_err.empty()) {
                        probe.metadata_error += "; " + raw_scan_err;
                    }
                    probe.probe_stage = "failed-serialized";
                    if (probe.probe_primary_error.empty()) {
                        probe.probe_primary_error = probe.serialized_parse_error_code.empty()
                                                        ? "SF_PARSE_FAILED"
                                                        : probe.serialized_parse_error_code;
                    }
                }
            } else {
                probe.probe_stage = "complete";
            }
        } else {
            if (!probe.metadata_error.empty()) {
                probe.metadata_error += "; ";
            }
            probe.metadata_error += stream_err;
            // Even on reconstruction failure, attempt serialized probing with best-partial stream
            // to preserve stage progression and candidate diagnostics.
            probe.probe_stage = "serialized";
            std::string serialized_err;
            if (!ParseSerializedFromNodes(metadata, stream, probe, serialized_err)) {
                std::string raw_scan_err;
                if (TryParseSerializedFromRawBundle(in, probe, raw_scan_err)) {
                    probe.probe_stage = "complete";
                    if (!probe.metadata_error.empty()) {
                        probe.metadata_error += "; ";
                    }
                    probe.metadata_error += "raw serialized fallback engaged";
                } else {
                    if (!serialized_err.empty()) {
                        if (!probe.metadata_error.empty()) {
                            probe.metadata_error += "; ";
                        }
                        probe.metadata_error += "partial serialized probe: " + serialized_err;
                    }
                    if (!raw_scan_err.empty()) {
                        if (!probe.metadata_error.empty()) {
                            probe.metadata_error += "; ";
                        }
                        probe.metadata_error += raw_scan_err;
                    }
                    probe.probe_stage = "failed-serialized";
                    if (!probe.reconstruction_failure_summary_code.empty()) {
                        probe.probe_primary_error = probe.reconstruction_failure_summary_code;
                    } else if (probe.probe_primary_error.empty()) {
                        probe.probe_primary_error = "RECONSTRUCT_FAILED";
                    }
                }
            } else {
                probe.probe_stage = "complete";
            }
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
