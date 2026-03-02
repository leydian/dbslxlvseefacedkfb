#include "vsfclone/vsf/serialized_file_reader.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace vsfclone::vsf {

namespace {

enum class Endian {
    Little,
    Big,
};

std::uint32_t ReadU32(const std::vector<unsigned char>& data, std::size_t at, Endian e) {
    if (e == Endian::Little) {
        return static_cast<std::uint32_t>(data[at]) |
               (static_cast<std::uint32_t>(data[at + 1U]) << 8U) |
               (static_cast<std::uint32_t>(data[at + 2U]) << 16U) |
               (static_cast<std::uint32_t>(data[at + 3U]) << 24U);
    }
    return (static_cast<std::uint32_t>(data[at]) << 24U) |
           (static_cast<std::uint32_t>(data[at + 1U]) << 16U) |
           (static_cast<std::uint32_t>(data[at + 2U]) << 8U) |
           static_cast<std::uint32_t>(data[at + 3U]);
}

std::uint16_t ReadU16(const std::vector<unsigned char>& data, std::size_t at, Endian e) {
    if (e == Endian::Little) {
        return static_cast<std::uint16_t>(data[at]) |
               static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[at + 1U]) << 8U);
    }
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[at]) << 8U) |
                                      static_cast<std::uint16_t>(data[at + 1U]));
}

std::uint64_t ReadU64(const std::vector<unsigned char>& data, std::size_t at, Endian e) {
    if (e == Endian::Little) {
        return static_cast<std::uint64_t>(ReadU32(data, at, e)) |
               (static_cast<std::uint64_t>(ReadU32(data, at + 4U, e)) << 32U);
    }
    return (static_cast<std::uint64_t>(ReadU32(data, at, e)) << 32U) |
           static_cast<std::uint64_t>(ReadU32(data, at + 4U, e));
}

struct Cursor {
    const std::vector<unsigned char>& d;
    std::size_t at = 0;
    Endian e = Endian::Little;

    bool Skip(std::size_t n) {
        if (at + n > d.size()) {
            return false;
        }
        at += n;
        return true;
    }

    bool Align(std::size_t n) {
        if (n == 0U) {
            return true;
        }
        const std::size_t aligned = (at + (n - 1U)) & ~(n - 1U);
        if (aligned > d.size()) {
            return false;
        }
        at = aligned;
        return true;
    }

    bool U8(std::uint8_t& out) {
        if (at + 1U > d.size()) {
            return false;
        }
        out = d[at++];
        return true;
    }

    bool U16(std::uint16_t& out) {
        if (at + 2U > d.size()) {
            return false;
        }
        out = ReadU16(d, at, e);
        at += 2U;
        return true;
    }

    bool U32(std::uint32_t& out) {
        if (at + 4U > d.size()) {
            return false;
        }
        out = ReadU32(d, at, e);
        at += 4U;
        return true;
    }

    bool U64(std::uint64_t& out) {
        if (at + 8U > d.size()) {
            return false;
        }
        out = ReadU64(d, at, e);
        at += 8U;
        return true;
    }

    bool I32(std::int32_t& out) {
        std::uint32_t u = 0;
        if (!U32(u)) {
            return false;
        }
        out = static_cast<std::int32_t>(u);
        return true;
    }

    bool CString(std::string& out, std::size_t max_len = 512U) {
        out.clear();
        out.reserve(32U);
        for (std::size_t i = 0; i < max_len; ++i) {
            if (at >= d.size()) {
                return false;
            }
            const char c = static_cast<char>(d[at++]);
            if (c == '\0') {
                return true;
            }
            out.push_back(c);
        }
        return false;
    }
};

bool ParseTypeTree(Cursor& c, std::int32_t version) {
    std::int32_t node_count = 0;
    std::int32_t string_buffer_size = 0;
    if (!c.I32(node_count) || !c.I32(string_buffer_size)) {
        return false;
    }
    if (node_count < 0 || string_buffer_size < 0) {
        return false;
    }

    if (version >= 12 || version == 10) {
        const std::size_t bytes_per_node = (version >= 19) ? 32U : 24U;
        const std::size_t node_bytes = static_cast<std::size_t>(node_count) * bytes_per_node;
        if (!c.Skip(node_bytes)) {
            return false;
        }
    } else {
        return false;
    }

    return c.Skip(static_cast<std::size_t>(string_buffer_size));
}

std::string ComposeMajorTypes(const SerializedFileSummary& s) {
    std::ostringstream out;
    bool first = true;
    auto append = [&](const char* name, std::uint32_t n) {
        if (n == 0U) {
            return;
        }
        if (!first) {
            out << ", ";
        }
        first = false;
        out << name << "=" << n;
    };

    append("GameObject", s.game_object_count);
    append("Mesh", s.mesh_object_count);
    append("Material", s.material_object_count);
    append("Texture2D", s.texture_object_count);
    append("SkinnedMeshRenderer", s.skinned_mesh_renderer_count);
    return out.str();
}

std::string ClassifySerializedParseError(const std::string& error) {
    if (error.find("too small") != std::string::npos) {
        return "SF_TOO_SMALL";
    }
    if (error.find("metadata size") != std::string::npos || error.find("metadata range") != std::string::npos) {
        return "SF_METADATA_RANGE_INVALID";
    }
    if (error.find("unsupported serialized file version") != std::string::npos) {
        return "SF_UNSUPPORTED_VERSION";
    }
    if (error.find("type count") != std::string::npos || error.find("class id") != std::string::npos ||
        error.find("type tree") != std::string::npos) {
        return "SF_TYPE_TABLE_INVALID";
    }
    if (error.find("object count") != std::string::npos || error.find("object") != std::string::npos) {
        return "SF_OBJECT_TABLE_INVALID";
    }
    if (error.find("unity version string") != std::string::npos ||
        error.find("target platform") != std::string::npos ||
        error.find("type tree flag") != std::string::npos) {
        return "SF_HEADER_INVALID";
    }
    return "SF_PARSE_INVALID";
}

core::Result<SerializedFileSummary> ParseWithMetadataEndian(const std::vector<unsigned char>& bytes, Endian metadata_endian) {
    if (bytes.size() < 20U) {
        return core::Result<SerializedFileSummary>::Fail("serialized file too small");
    }

    const std::uint32_t metadata_size_be = ReadU32(bytes, 0U, Endian::Big);
    const std::uint32_t version_be = ReadU32(bytes, 8U, Endian::Big);
    if (metadata_size_be == 0U || metadata_size_be > bytes.size()) {
        return core::Result<SerializedFileSummary>::Fail("invalid metadata size");
    }

    if (version_be > 40U) {
        return core::Result<SerializedFileSummary>::Fail("unsupported serialized file version");
    }

    std::size_t header_size = 20U;
    if (version_be >= 22U) {
        header_size = 48U;
    }
    if (header_size + metadata_size_be > bytes.size()) {
        return core::Result<SerializedFileSummary>::Fail("metadata range out of bounds");
    }

    std::vector<unsigned char> metadata(bytes.begin() + static_cast<std::ptrdiff_t>(header_size),
                                        bytes.begin() + static_cast<std::ptrdiff_t>(header_size + metadata_size_be));
    Cursor c {metadata, 0U, metadata_endian};

    std::string unity_version;
    if (!c.CString(unity_version, 256U)) {
        return core::Result<SerializedFileSummary>::Fail("failed to read unity version string");
    }

    std::int32_t target_platform = 0;
    if (!c.I32(target_platform)) {
        return core::Result<SerializedFileSummary>::Fail("failed to read target platform");
    }
    (void)target_platform;

    std::uint8_t enable_type_tree = 0;
    if (!c.U8(enable_type_tree)) {
        return core::Result<SerializedFileSummary>::Fail("failed to read type tree flag");
    }

    std::int32_t type_count = 0;
    if (!c.I32(type_count) || type_count < 0 || type_count > 200000) {
        return core::Result<SerializedFileSummary>::Fail("invalid type count");
    }

    std::vector<std::int32_t> class_ids;
    class_ids.reserve(static_cast<std::size_t>(type_count));

    for (std::int32_t i = 0; i < type_count; ++i) {
        std::int32_t class_id = 0;
        if (!c.I32(class_id)) {
            return core::Result<SerializedFileSummary>::Fail("failed to read class id");
        }
        class_ids.push_back(class_id);

        if (version_be >= 16U) {
            std::uint8_t is_stripped = 0;
            if (!c.U8(is_stripped)) {
                return core::Result<SerializedFileSummary>::Fail("failed to read stripped flag");
            }
            (void)is_stripped;
        }
        if (version_be >= 17U) {
            std::uint16_t script_type_index = 0;
            if (!c.U16(script_type_index)) {
                return core::Result<SerializedFileSummary>::Fail("failed to read script type index");
            }
        }

        if (version_be >= 13U) {
            const bool has_script_id = (version_be < 16U && class_id < 0) || (version_be >= 16U && class_id == 114);
            if (has_script_id && !c.Skip(16U)) {
                return core::Result<SerializedFileSummary>::Fail("failed to read script id");
            }
            if (!c.Skip(16U)) {
                return core::Result<SerializedFileSummary>::Fail("failed to read old type hash");
            }
        }

        if (enable_type_tree != 0U) {
            if (!ParseTypeTree(c, static_cast<std::int32_t>(version_be))) {
                return core::Result<SerializedFileSummary>::Fail("failed to parse type tree");
            }
        }
    }

    if (version_be >= 7U && version_be < 14U) {
        if (!c.Align(4U)) {
            return core::Result<SerializedFileSummary>::Fail("failed to align metadata cursor");
        }
    }

    std::int32_t object_count = 0;
    if (!c.I32(object_count) || object_count < 0 || object_count > 20000000) {
        return core::Result<SerializedFileSummary>::Fail("invalid object count");
    }

    SerializedFileSummary summary;
    summary.object_count = static_cast<std::uint32_t>(object_count);
    summary.parse_path = metadata_endian == Endian::Little ? "metadata-endian-little" : "metadata-endian-big";

    for (std::int32_t i = 0; i < object_count; ++i) {
        if (version_be >= 14U && !c.Align(4U)) {
            return core::Result<SerializedFileSummary>::Fail("failed to align object table");
        }

        std::uint64_t path_id = 0;
        if (version_be < 14U) {
            std::uint32_t path_id32 = 0;
            if (!c.U32(path_id32)) {
                return core::Result<SerializedFileSummary>::Fail("failed to read object path id");
            }
            path_id = path_id32;
        } else if (!c.U64(path_id)) {
            return core::Result<SerializedFileSummary>::Fail("failed to read object path id");
        }
        (void)path_id;

        std::uint32_t byte_start = 0;
        std::uint32_t byte_size = 0;
        std::int32_t type_id = -1;
        if (!c.U32(byte_start) || !c.U32(byte_size) || !c.I32(type_id)) {
            return core::Result<SerializedFileSummary>::Fail("failed to read object entry core");
        }
        (void)byte_start;
        (void)byte_size;

        std::int32_t class_id = type_id;
        if (type_id >= 0 && static_cast<std::size_t>(type_id) < class_ids.size()) {
            class_id = class_ids[static_cast<std::size_t>(type_id)];
        }

        if (version_be < 16U) {
            std::uint16_t class_id_legacy = 0;
            if (!c.U16(class_id_legacy)) {
                return core::Result<SerializedFileSummary>::Fail("failed to read legacy class id");
            }
            class_id = static_cast<std::int32_t>(class_id_legacy);
        }

        if (version_be < 11U) {
            std::uint16_t is_destroyed = 0;
            if (!c.U16(is_destroyed)) {
                return core::Result<SerializedFileSummary>::Fail("failed to read legacy destroyed flag");
            }
        }

        if (version_be >= 11U && version_be < 17U) {
            std::uint16_t script_type_index = 0;
            if (!c.U16(script_type_index)) {
                return core::Result<SerializedFileSummary>::Fail("failed to read object script type index");
            }
        }

        if (version_be == 15U || version_be == 16U) {
            std::uint8_t stripped = 0;
            if (!c.U8(stripped)) {
                return core::Result<SerializedFileSummary>::Fail("failed to read stripped byte");
            }
        }

        switch (class_id) {
            case 1:
                ++summary.game_object_count;
                break;
            case 21:
                ++summary.material_object_count;
                break;
            case 28:
                ++summary.texture_object_count;
                break;
            case 43:
                ++summary.mesh_object_count;
                break;
            case 137:
                ++summary.skinned_mesh_renderer_count;
                break;
            default:
                break;
        }
    }

    summary.major_types_found = ComposeMajorTypes(summary);
    summary.error_code = "NONE";
    return core::Result<SerializedFileSummary>::Ok(summary);
}

}  // namespace

core::Result<SerializedFileSummary> SerializedFileReader::ParseObjectSummary(const std::vector<unsigned char>& bytes) const {
    // Unity serialized metadata is almost always little-endian in modern bundles.
    auto little = ParseWithMetadataEndian(bytes, Endian::Little);
    if (little.ok && little.value.object_count > 0U) {
        return little;
    }

    auto big = ParseWithMetadataEndian(bytes, Endian::Big);
    if (big.ok && big.value.object_count > 0U) {
        return big;
    }

    if (little.ok) {
        return little;
    }
    if (big.ok) {
        return big;
    }

    auto LooksLikeHeaderAt = [&](std::size_t at) -> bool {
        if (at + 20U > bytes.size()) {
            return false;
        }
        const auto metadata_size_be = ReadU32(bytes, at, Endian::Big);
        const auto version_be = ReadU32(bytes, at + 8U, Endian::Big);
        if (metadata_size_be == 0U || metadata_size_be > (64U * 1024U * 1024U)) {
            return false;
        }
        if (version_be == 0U || version_be > 40U) {
            return false;
        }
        const std::size_t header_size = version_be >= 22U ? 48U : 20U;
        return at + header_size + metadata_size_be <= bytes.size();
    };
    auto TryOffsetScan = [&](Endian metadata_endian, SerializedFileSummary* out_best) -> bool {
        if (out_best == nullptr || bytes.size() < 64U) {
            return false;
        }
        bool found = false;
        std::uint32_t best_objects = 0U;
        std::int32_t best_groups = std::numeric_limits<std::int32_t>::min();
        const std::size_t scan_limit = std::min<std::size_t>(bytes.size(), 8U * 1024U * 1024U);
        std::size_t scan_hits = 0U;
        for (std::size_t at = 8U; at + 64U <= scan_limit && scan_hits < 512U; at += 8U) {
            if (!LooksLikeHeaderAt(at)) {
                continue;
            }
            ++scan_hits;
            std::vector<unsigned char> sliced(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.end());
            auto parsed = ParseWithMetadataEndian(sliced, metadata_endian);
            if (!parsed.ok) {
                continue;
            }
            std::int32_t groups = 0;
            if (parsed.value.game_object_count > 0U) {
                ++groups;
            }
            if (parsed.value.mesh_object_count > 0U) {
                ++groups;
            }
            if (parsed.value.material_object_count > 0U) {
                ++groups;
            }
            if (parsed.value.texture_object_count > 0U) {
                ++groups;
            }
            if (parsed.value.skinned_mesh_renderer_count > 0U) {
                ++groups;
            }
            if (!found ||
                parsed.value.object_count > best_objects ||
                (parsed.value.object_count == best_objects && groups > best_groups)) {
                found = true;
                best_objects = parsed.value.object_count;
                best_groups = groups;
                *out_best = parsed.value;
                out_best->parse_path += "+scan@" + std::to_string(at);
            }
        }
        return found;
    };

    SerializedFileSummary scanned_best;
    if (TryOffsetScan(Endian::Little, &scanned_best) || TryOffsetScan(Endian::Big, &scanned_best)) {
        return core::Result<SerializedFileSummary>::Ok(scanned_best);
    }

    const auto little_code = ClassifySerializedParseError(little.error);
    const auto big_code = ClassifySerializedParseError(big.error);
    return core::Result<SerializedFileSummary>::Fail(
        "SF_PARSE_BOTH_ENDIAN_FAILED[" + little_code + "|" + big_code + "]: little={" + little.error + "}, big={" +
        big.error + "}");
}

}  // namespace vsfclone::vsf
