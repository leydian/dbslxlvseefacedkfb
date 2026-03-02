#include "vxavatar_loader.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace vsfclone::avatar {

namespace {

constexpr std::uint32_t kZipEocdSig = 0x06054B50U;
constexpr std::uint32_t kZipCdfhSig = 0x02014B50U;
constexpr std::uint32_t kZipLocalSig = 0x04034B50U;
constexpr std::uint16_t kZipStored = 0U;

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string NormalizeZipPath(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        out.push_back(c == '\\' ? '/' : c);
    }
    return ToLower(out);
}

std::optional<std::uint16_t> ReadU16Le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + 2U > bytes.size()) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

std::optional<std::uint32_t> ReadU32Le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + 4U > bytes.size()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(
        bytes[offset] | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) | (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U));
}

bool ReadFileBytes(const std::string& path, std::vector<std::uint8_t>* out) {
    if (out == nullptr) {
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        return false;
    }
    in.seekg(0, std::ios::beg);
    out->assign(static_cast<std::size_t>(size), 0U);
    if (!out->empty()) {
        in.read(reinterpret_cast<char*>(out->data()), static_cast<std::streamsize>(out->size()));
    }
    return in.good() || in.eof();
}

struct ZipEntryMeta {
    std::string name;
    std::uint16_t compression_method = 0U;
    std::uint32_t compressed_size = 0U;
    std::uint32_t uncompressed_size = 0U;
    std::uint32_t local_header_offset = 0U;
};

bool FindEocdOffset(const std::vector<std::uint8_t>& file_bytes, std::size_t* out_offset) {
    if (out_offset == nullptr || file_bytes.size() < 22U) {
        return false;
    }
    const std::size_t lower_bound = file_bytes.size() > (0xFFFFU + 22U) ? (file_bytes.size() - (0xFFFFU + 22U)) : 0U;
    for (std::size_t pos = file_bytes.size() - 22U;; --pos) {
        const auto sig = ReadU32Le(file_bytes, pos);
        if (sig && *sig == kZipEocdSig) {
            *out_offset = pos;
            return true;
        }
        if (pos == lower_bound) {
            break;
        }
    }
    return false;
}

bool ParseZipCentralDirectory(
    const std::vector<std::uint8_t>& file_bytes,
    std::unordered_map<std::string, ZipEntryMeta>* out_entries,
    std::string* out_error) {
    if (out_entries == nullptr || out_error == nullptr) {
        return false;
    }
    out_entries->clear();
    out_error->clear();

    std::size_t eocd_offset = 0U;
    if (!FindEocdOffset(file_bytes, &eocd_offset)) {
        *out_error = "ZIP EOCD not found";
        return false;
    }

    const auto entry_count = ReadU16Le(file_bytes, eocd_offset + 10U);
    const auto cd_size = ReadU32Le(file_bytes, eocd_offset + 12U);
    const auto cd_offset = ReadU32Le(file_bytes, eocd_offset + 16U);
    if (!entry_count || !cd_size || !cd_offset) {
        *out_error = "ZIP EOCD is truncated";
        return false;
    }
    const std::size_t cdir_begin = static_cast<std::size_t>(*cd_offset);
    const std::size_t cdir_end = cdir_begin + static_cast<std::size_t>(*cd_size);
    if (cdir_begin > file_bytes.size() || cdir_end > file_bytes.size()) {
        *out_error = "ZIP central directory is out of range";
        return false;
    }

    std::size_t cursor = cdir_begin;
    for (std::uint16_t i = 0U; i < *entry_count; ++i) {
        const auto sig = ReadU32Le(file_bytes, cursor);
        if (!sig || *sig != kZipCdfhSig) {
            *out_error = "ZIP central directory entry signature mismatch";
            return false;
        }
        const auto method = ReadU16Le(file_bytes, cursor + 10U);
        const auto csize = ReadU32Le(file_bytes, cursor + 20U);
        const auto usize = ReadU32Le(file_bytes, cursor + 24U);
        const auto name_len = ReadU16Le(file_bytes, cursor + 28U);
        const auto extra_len = ReadU16Le(file_bytes, cursor + 30U);
        const auto comment_len = ReadU16Le(file_bytes, cursor + 32U);
        const auto local_offset = ReadU32Le(file_bytes, cursor + 42U);
        if (!method || !csize || !usize || !name_len || !extra_len || !comment_len || !local_offset) {
            *out_error = "ZIP central directory entry is truncated";
            return false;
        }
        const std::size_t name_start = cursor + 46U;
        const std::size_t name_end = name_start + static_cast<std::size_t>(*name_len);
        if (name_end > file_bytes.size()) {
            *out_error = "ZIP filename range is invalid";
            return false;
        }
        ZipEntryMeta meta;
        meta.name.assign(reinterpret_cast<const char*>(&file_bytes[name_start]), static_cast<std::size_t>(*name_len));
        meta.compression_method = *method;
        meta.compressed_size = *csize;
        meta.uncompressed_size = *usize;
        meta.local_header_offset = *local_offset;
        (*out_entries)[NormalizeZipPath(meta.name)] = std::move(meta);

        const std::size_t step = 46U + static_cast<std::size_t>(*name_len) + static_cast<std::size_t>(*extra_len) +
                                 static_cast<std::size_t>(*comment_len);
        cursor += step;
        if (cursor > cdir_end) {
            *out_error = "ZIP central directory traversal overflow";
            return false;
        }
    }
    return true;
}

bool ReadStoredZipEntry(
    const std::vector<std::uint8_t>& file_bytes,
    const ZipEntryMeta& entry,
    std::vector<std::uint8_t>* out_bytes,
    std::string* out_error) {
    if (out_bytes == nullptr || out_error == nullptr) {
        return false;
    }
    out_bytes->clear();
    out_error->clear();

    if (entry.compression_method != kZipStored) {
        *out_error = "compression method is not stored(0)";
        return false;
    }
    const std::size_t local_offset = static_cast<std::size_t>(entry.local_header_offset);
    const auto sig = ReadU32Le(file_bytes, local_offset);
    if (!sig || *sig != kZipLocalSig) {
        *out_error = "local header signature mismatch";
        return false;
    }
    const auto name_len = ReadU16Le(file_bytes, local_offset + 26U);
    const auto extra_len = ReadU16Le(file_bytes, local_offset + 28U);
    if (!name_len || !extra_len) {
        *out_error = "local header truncated";
        return false;
    }
    const std::size_t data_offset =
        local_offset + 30U + static_cast<std::size_t>(*name_len) + static_cast<std::size_t>(*extra_len);
    const std::size_t data_end = data_offset + static_cast<std::size_t>(entry.compressed_size);
    if (data_offset > file_bytes.size() || data_end > file_bytes.size()) {
        *out_error = "entry payload range invalid";
        return false;
    }
    out_bytes->assign(file_bytes.begin() + static_cast<std::ptrdiff_t>(data_offset), file_bytes.begin() + static_cast<std::ptrdiff_t>(data_end));
    return true;
}

enum class JsonType { Null, Bool, Number, String, Array, Object };

struct JsonValue {
    JsonType type = JsonType::Null;
    bool bool_value = false;
    double number_value = 0.0;
    std::string string_value;
    std::vector<JsonValue> array_value;
    std::unordered_map<std::string, JsonValue> object_value;
};

class JsonParser {
  public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    bool Parse(JsonValue* out_value, std::string* out_error) {
        if (out_value == nullptr || out_error == nullptr) {
            return false;
        }
        out_error->clear();
        SkipWs();
        if (!ParseValue(out_value, out_error)) {
            return false;
        }
        SkipWs();
        if (pos_ != text_.size()) {
            *out_error = "trailing characters in json";
            return false;
        }
        return true;
    }

  private:
    bool ParseValue(JsonValue* out_value, std::string* out_error) {
        if (pos_ >= text_.size()) {
            *out_error = "unexpected end of json";
            return false;
        }
        const char c = text_[pos_];
        if (c == '"') {
            out_value->type = JsonType::String;
            return ParseString(&out_value->string_value, out_error);
        }
        if (c == '{') {
            return ParseObject(out_value, out_error);
        }
        if (c == '[') {
            return ParseArray(out_value, out_error);
        }
        if (c == 't') {
            return ParseLiteral("true", JsonType::Bool, out_value, out_error, true);
        }
        if (c == 'f') {
            return ParseLiteral("false", JsonType::Bool, out_value, out_error, false);
        }
        if (c == 'n') {
            return ParseLiteral("null", JsonType::Null, out_value, out_error, false);
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return ParseNumber(out_value, out_error);
        }
        *out_error = "invalid json token";
        return false;
    }

    bool ParseLiteral(
        const char* literal,
        JsonType type,
        JsonValue* out_value,
        std::string* out_error,
        bool bool_value) {
        const std::size_t len = std::char_traits<char>::length(literal);
        if (pos_ + len > text_.size() || text_.substr(pos_, len) != literal) {
            *out_error = "invalid literal token";
            return false;
        }
        pos_ += len;
        out_value->type = type;
        out_value->bool_value = bool_value;
        return true;
    }

    bool ParseString(std::string* out, std::string* out_error) {
        if (pos_ >= text_.size() || text_[pos_] != '"') {
            *out_error = "expected string";
            return false;
        }
        ++pos_;
        out->clear();
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') {
                return true;
            }
            if (c == '\\') {
                if (pos_ >= text_.size()) {
                    *out_error = "unterminated escape sequence";
                    return false;
                }
                const char esc = text_[pos_++];
                switch (esc) {
                    case '"':
                    case '\\':
                    case '/':
                        out->push_back(esc);
                        break;
                    case 'b':
                        out->push_back('\b');
                        break;
                    case 'f':
                        out->push_back('\f');
                        break;
                    case 'n':
                        out->push_back('\n');
                        break;
                    case 'r':
                        out->push_back('\r');
                        break;
                    case 't':
                        out->push_back('\t');
                        break;
                    case 'u':
                        if (pos_ + 4U > text_.size()) {
                            *out_error = "invalid unicode escape";
                            return false;
                        }
                        pos_ += 4U;
                        out->push_back('?');
                        break;
                    default:
                        *out_error = "unsupported escape sequence";
                        return false;
                }
            } else {
                out->push_back(c);
            }
        }
        *out_error = "unterminated string";
        return false;
    }

    bool ParseNumber(JsonValue* out_value, std::string* out_error) {
        const std::size_t begin = pos_;
        if (text_[pos_] == '-') {
            ++pos_;
        }
        while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                ++pos_;
            }
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                ++pos_;
            }
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                ++pos_;
            }
        }
        const std::string token(text_.substr(begin, pos_ - begin));
        char* end_ptr = nullptr;
        const double parsed = std::strtod(token.c_str(), &end_ptr);
        if (end_ptr == token.c_str()) {
            *out_error = "invalid number token";
            return false;
        }
        out_value->type = JsonType::Number;
        out_value->number_value = parsed;
        return true;
    }

    bool ParseArray(JsonValue* out_value, std::string* out_error) {
        if (text_[pos_] != '[') {
            *out_error = "expected array";
            return false;
        }
        ++pos_;
        out_value->type = JsonType::Array;
        out_value->array_value.clear();
        SkipWs();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (pos_ < text_.size()) {
            JsonValue child;
            if (!ParseValue(&child, out_error)) {
                return false;
            }
            out_value->array_value.push_back(std::move(child));
            SkipWs();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                ++pos_;
                SkipWs();
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            *out_error = "array delimiter expected";
            return false;
        }
        *out_error = "unterminated array";
        return false;
    }

    bool ParseObject(JsonValue* out_value, std::string* out_error) {
        if (text_[pos_] != '{') {
            *out_error = "expected object";
            return false;
        }
        ++pos_;
        out_value->type = JsonType::Object;
        out_value->object_value.clear();
        SkipWs();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (pos_ < text_.size()) {
            std::string key;
            if (!ParseString(&key, out_error)) {
                return false;
            }
            SkipWs();
            if (pos_ >= text_.size() || text_[pos_] != ':') {
                *out_error = "object key separator missing";
                return false;
            }
            ++pos_;
            SkipWs();
            JsonValue value;
            if (!ParseValue(&value, out_error)) {
                return false;
            }
            out_value->object_value[std::move(key)] = std::move(value);
            SkipWs();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                ++pos_;
                SkipWs();
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            *out_error = "object delimiter expected";
            return false;
        }
        *out_error = "unterminated object";
        return false;
    }

    void SkipWs() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0U;
};

const JsonValue* FindKey(const JsonValue& root, const std::string& key) {
    if (root.type != JsonType::Object) {
        return nullptr;
    }
    auto it = root.object_value.find(key);
    if (it == root.object_value.end()) {
        return nullptr;
    }
    return &it->second;
}

bool GetRequiredString(const JsonValue& root, const std::string& key, std::string* out) {
    const JsonValue* v = FindKey(root, key);
    if (v == nullptr || v->type != JsonType::String || v->string_value.empty()) {
        return false;
    }
    *out = v->string_value;
    return true;
}

bool GetStringArray(const JsonValue& root, const std::string& key, std::vector<std::string>* out, bool required) {
    const JsonValue* v = FindKey(root, key);
    if (v == nullptr) {
        return !required;
    }
    if (v->type != JsonType::Array) {
        return false;
    }
    out->clear();
    for (const JsonValue& child : v->array_value) {
        if (child.type != JsonType::String || child.string_value.empty()) {
            return false;
        }
        out->push_back(child.string_value);
    }
    return true;
}

bool IsSafeRelativeAssetPath(const std::string& p) {
    if (p.empty()) {
        return false;
    }
    if (p.find(':') != std::string::npos) {
        return false;
    }
    if (!p.empty() && (p[0] == '/' || p[0] == '\\')) {
        return false;
    }
    const std::string norm = NormalizeZipPath(p);
    if (norm.find("../") != std::string::npos || norm == "..") {
        return false;
    }
    return true;
}

std::string Join(const std::vector<std::string>& items) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0U) {
            oss << ", ";
        }
        oss << items[i];
    }
    return oss.str();
}

std::string DetectTextureFormat(const std::string& path) {
    const std::string ext = ToLower(fs::path(path).extension().string());
    if (ext == ".png") {
        return "png";
    }
    if (ext == ".jpg" || ext == ".jpeg") {
        return "jpeg";
    }
    if (ext == ".tga") {
        return "tga";
    }
    if (ext == ".bmp") {
        return "bmp";
    }
    return "binary";
}

std::optional<std::string> FindManifestKey(const std::unordered_map<std::string, ZipEntryMeta>& entries) {
    auto it = entries.find("manifest.json");
    if (it != entries.end()) {
        return it->first;
    }
    for (const auto& kv : entries) {
        const std::string& key = kv.first;
        if (key.size() >= 13U && key.substr(key.size() - 13U) == "/manifest.json") {
            return key;
        }
    }
    return std::nullopt;
}

}  // namespace

bool VxAvatarLoader::CanLoadPath(const std::string& path) const {
    const auto ext = ToLower(fs::path(path).extension().string());
    return ext == ".vxavatar";
}

core::Result<AvatarPackage> VxAvatarLoader::Load(const std::string& path) const {
    std::vector<std::uint8_t> file_bytes;
    if (!ReadFileBytes(path, &file_bytes)) {
        return core::Result<AvatarPackage>::Fail("could not open vxavatar file");
    }
    if (file_bytes.size() < 4U) {
        return core::Result<AvatarPackage>::Fail("vxavatar file is too small");
    }

    AvatarPackage pkg;
    pkg.source_type = AvatarSourceType::VxAvatar;
    pkg.compat_level = AvatarCompatLevel::Partial;
    pkg.parser_stage = "parse";
    pkg.primary_error_code = "NONE";
    pkg.source_path = path;
    pkg.display_name = fs::path(path).stem().string();

    const bool is_zip = (file_bytes[0] == 0x50U && file_bytes[1] == 0x4BU);
    if (!is_zip) {
        pkg.compat_level = AvatarCompatLevel::Failed;
        pkg.primary_error_code = "VX_SCHEMA_INVALID";
        pkg.warnings.push_back("E_PARSE: VX_SCHEMA_INVALID: vxavatar must be a ZIP container.");
        return core::Result<AvatarPackage>::Ok(pkg);
    }

    std::unordered_map<std::string, ZipEntryMeta> entries;
    std::string zip_error;
    if (!ParseZipCentralDirectory(file_bytes, &entries, &zip_error)) {
        pkg.compat_level = AvatarCompatLevel::Failed;
        pkg.primary_error_code = "VX_SCHEMA_INVALID";
        pkg.warnings.push_back("E_PARSE: VX_SCHEMA_INVALID: " + zip_error);
        return core::Result<AvatarPackage>::Ok(pkg);
    }

    const auto manifest_key = FindManifestKey(entries);
    if (!manifest_key.has_value()) {
        pkg.compat_level = AvatarCompatLevel::Failed;
        pkg.primary_error_code = "VX_MANIFEST_MISSING";
        pkg.warnings.push_back("E_PARSE: VX_MANIFEST_MISSING: manifest.json not found.");
        return core::Result<AvatarPackage>::Ok(pkg);
    }

    std::vector<std::uint8_t> manifest_bytes;
    std::string entry_error;
    const ZipEntryMeta& manifest_entry = entries[*manifest_key];
    if (manifest_entry.compression_method != kZipStored) {
        pkg.compat_level = AvatarCompatLevel::Failed;
        pkg.primary_error_code = "VX_SCHEMA_INVALID";
        pkg.warnings.push_back("E_PARSE: VX_SCHEMA_INVALID: manifest.json must use stored ZIP method(0) for MVP.");
        return core::Result<AvatarPackage>::Ok(pkg);
    }
    if (!ReadStoredZipEntry(file_bytes, manifest_entry, &manifest_bytes, &entry_error)) {
        pkg.compat_level = AvatarCompatLevel::Failed;
        pkg.primary_error_code = "VX_SCHEMA_INVALID";
        pkg.warnings.push_back("E_PARSE: VX_SCHEMA_INVALID: failed to read manifest.json: " + entry_error);
        return core::Result<AvatarPackage>::Ok(pkg);
    }

    JsonValue root;
    std::string parse_error;
    JsonParser parser(std::string_view(reinterpret_cast<const char*>(manifest_bytes.data()), manifest_bytes.size()));
    if (!parser.Parse(&root, &parse_error) || root.type != JsonType::Object) {
        pkg.compat_level = AvatarCompatLevel::Failed;
        pkg.primary_error_code = "VX_SCHEMA_INVALID";
        pkg.warnings.push_back("E_PARSE: VX_SCHEMA_INVALID: manifest.json parse failed: " + parse_error);
        return core::Result<AvatarPackage>::Ok(pkg);
    }

    std::string avatar_id;
    if (!GetRequiredString(root, "avatarId", &avatar_id) && !GetRequiredString(root, "avatar_id", &avatar_id)) {
        pkg.compat_level = AvatarCompatLevel::Failed;
        pkg.primary_error_code = "VX_SCHEMA_INVALID";
        pkg.warnings.push_back("E_PARSE: VX_SCHEMA_INVALID: missing required key avatarId.");
        return core::Result<AvatarPackage>::Ok(pkg);
    }

    std::vector<std::string> mesh_refs;
    std::vector<std::string> material_refs;
    std::vector<std::string> texture_refs;
    if (!GetStringArray(root, "meshRefs", &mesh_refs, true) || !GetStringArray(root, "materialRefs", &material_refs, true) ||
        !GetStringArray(root, "textureRefs", &texture_refs, true)) {
        pkg.compat_level = AvatarCompatLevel::Failed;
        pkg.primary_error_code = "VX_SCHEMA_INVALID";
        pkg.warnings.push_back(
            "E_PARSE: VX_SCHEMA_INVALID: required arrays meshRefs/materialRefs/textureRefs must exist and contain strings.");
        return core::Result<AvatarPackage>::Ok(pkg);
    }

    std::string display_name = avatar_id;
    GetRequiredString(root, "displayName", &display_name);
    if (display_name == avatar_id) {
        GetRequiredString(root, "name", &display_name);
    }
    pkg.display_name = display_name.empty() ? avatar_id : display_name;
    pkg.warnings.push_back("W_STAGE: parse");

    pkg.parser_stage = "resolve";
    std::vector<std::string> missing_assets;
    auto resolve_entry = [&](const std::string& ref) -> const ZipEntryMeta* {
        if (!IsSafeRelativeAssetPath(ref)) {
            missing_assets.push_back(ref + " (unsafe path)");
            return nullptr;
        }
        const std::string key = NormalizeZipPath(ref);
        auto it = entries.find(key);
        if (it == entries.end()) {
            missing_assets.push_back(ref);
            return nullptr;
        }
        return &it->second;
    };

    std::vector<const ZipEntryMeta*> resolved_meshes;
    resolved_meshes.reserve(mesh_refs.size());
    for (const std::string& ref : mesh_refs) {
        resolved_meshes.push_back(resolve_entry(ref));
    }
    std::vector<const ZipEntryMeta*> resolved_textures;
    resolved_textures.reserve(texture_refs.size());
    for (const std::string& ref : texture_refs) {
        resolved_textures.push_back(resolve_entry(ref));
    }

    if (!missing_assets.empty()) {
        pkg.compat_level = AvatarCompatLevel::Failed;
        pkg.primary_error_code = "VX_ASSET_MISSING";
        pkg.warnings.push_back("E_RESOLVE: VX_ASSET_MISSING: " + Join(missing_assets));
        return core::Result<AvatarPackage>::Ok(pkg);
    }
    pkg.warnings.push_back("W_STAGE: resolve");

    pkg.parser_stage = "payload";
    bool has_payload_gap = false;
    for (std::size_t i = 0; i < mesh_refs.size(); ++i) {
        MeshAssetSummary ms;
        ms.name = mesh_refs[i];
        pkg.meshes.push_back(ms);

        MeshRenderPayload payload;
        payload.name = mesh_refs[i];
        std::vector<std::uint8_t> payload_bytes;
        std::string payload_error;
        if (resolved_meshes[i] != nullptr && ReadStoredZipEntry(file_bytes, *resolved_meshes[i], &payload_bytes, &payload_error)) {
            payload.vertex_blob = std::move(payload_bytes);
        } else {
            has_payload_gap = true;
            pkg.warnings.push_back("W_PAYLOAD: VX_UNSUPPORTED_COMPRESSION: mesh=" + mesh_refs[i]);
        }
        pkg.mesh_payloads.push_back(std::move(payload));
    }

    for (const std::string& material_name : material_refs) {
        MaterialAssetSummary ms;
        ms.name = material_name;
        ms.shader_name = "mtoon";
        pkg.materials.push_back(ms);

        MaterialRenderPayload payload;
        payload.name = material_name;
        payload.shader_name = "mtoon";
        if (!texture_refs.empty()) {
            payload.base_color_texture_name = texture_refs.front();
        }
        pkg.material_payloads.push_back(std::move(payload));
    }

    for (std::size_t i = 0; i < texture_refs.size(); ++i) {
        TextureRenderPayload payload;
        payload.name = texture_refs[i];
        payload.format = DetectTextureFormat(texture_refs[i]);
        std::vector<std::uint8_t> texture_bytes;
        std::string payload_error;
        if (resolved_textures[i] != nullptr && ReadStoredZipEntry(file_bytes, *resolved_textures[i], &texture_bytes, &payload_error)) {
            payload.bytes = std::move(texture_bytes);
        } else {
            has_payload_gap = true;
            pkg.warnings.push_back("W_PAYLOAD: VX_UNSUPPORTED_COMPRESSION: texture=" + texture_refs[i]);
        }
        pkg.texture_payloads.push_back(std::move(payload));
    }
    pkg.warnings.push_back("W_STAGE: payload");

    pkg.parser_stage = "runtime-ready";
    pkg.warnings.push_back("W_STAGE: runtime-ready");
    if (has_payload_gap) {
        pkg.compat_level = AvatarCompatLevel::Partial;
        pkg.primary_error_code = "VX_UNSUPPORTED_COMPRESSION";
        pkg.missing_features.push_back("ZIP deflate decompression");
    } else {
        pkg.compat_level = AvatarCompatLevel::Full;
        pkg.primary_error_code = "NONE";
    }

    return core::Result<AvatarPackage>::Ok(pkg);
}

}  // namespace vsfclone::avatar
