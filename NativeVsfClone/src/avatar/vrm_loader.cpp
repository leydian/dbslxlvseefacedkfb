#include "vrm_loader.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace vsfclone::avatar {

namespace {

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool bool_value = false;
    double number_value = 0.0;
    std::string string_value;
    std::vector<JsonValue> array_value;
    std::unordered_map<std::string, JsonValue> object_value;
};

class JsonParser {
  public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    bool Parse(JsonValue* out_value, std::string* out_error) {
        SkipWs();
        if (!ParseValue(out_value, out_error)) {
            return false;
        }
        SkipWs();
        if (pos_ != input_.size()) {
            if (out_error != nullptr) {
                *out_error = "trailing characters after JSON document";
            }
            return false;
        }
        return true;
    }

  private:
    bool ParseValue(JsonValue* out_value, std::string* out_error) {
        if (pos_ >= input_.size()) {
            if (out_error != nullptr) {
                *out_error = "unexpected end of JSON";
            }
            return false;
        }
        const char c = input_[pos_];
        if (c == '"') {
            out_value->type = JsonValue::Type::String;
            return ParseString(&out_value->string_value, out_error);
        }
        if (c == '{') {
            out_value->type = JsonValue::Type::Object;
            return ParseObject(out_value, out_error);
        }
        if (c == '[') {
            out_value->type = JsonValue::Type::Array;
            return ParseArray(out_value, out_error);
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            out_value->type = JsonValue::Type::Number;
            return ParseNumber(out_value, out_error);
        }
        if (StartsWith("true")) {
            pos_ += 4;
            out_value->type = JsonValue::Type::Bool;
            out_value->bool_value = true;
            return true;
        }
        if (StartsWith("false")) {
            pos_ += 5;
            out_value->type = JsonValue::Type::Bool;
            out_value->bool_value = false;
            return true;
        }
        if (StartsWith("null")) {
            pos_ += 4;
            out_value->type = JsonValue::Type::Null;
            return true;
        }
        if (out_error != nullptr) {
            *out_error = "unsupported JSON token";
        }
        return false;
    }

    bool ParseString(std::string* out, std::string* out_error) {
        if (input_[pos_] != '"') {
            if (out_error != nullptr) {
                *out_error = "expected opening quote";
            }
            return false;
        }
        ++pos_;
        out->clear();
        while (pos_ < input_.size()) {
            const char c = input_[pos_++];
            if (c == '"') {
                return true;
            }
            if (c == '\\') {
                if (pos_ >= input_.size()) {
                    if (out_error != nullptr) {
                        *out_error = "unterminated escape sequence";
                    }
                    return false;
                }
                const char esc = input_[pos_++];
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
                        // Keep parser minimal: skip exact 4 hex digits and emit placeholder.
                        if (pos_ + 4 > input_.size()) {
                            if (out_error != nullptr) {
                                *out_error = "invalid unicode escape";
                            }
                            return false;
                        }
                        pos_ += 4;
                        out->push_back('?');
                        break;
                    default:
                        if (out_error != nullptr) {
                            *out_error = "unsupported escape character";
                        }
                        return false;
                }
                continue;
            }
            out->push_back(c);
        }
        if (out_error != nullptr) {
            *out_error = "unterminated string";
        }
        return false;
    }

    bool ParseNumber(JsonValue* out_value, std::string* out_error) {
        std::size_t start = pos_;
        if (input_[pos_] == '-') {
            ++pos_;
        }
        while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }
        if (pos_ < input_.size() && input_[pos_] == '.') {
            ++pos_;
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                ++pos_;
            }
        }
        if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) {
                ++pos_;
            }
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                ++pos_;
            }
        }
        const auto token = input_.substr(start, pos_ - start);
        try {
            out_value->number_value = std::stod(std::string(token));
            return true;
        } catch (...) {
            if (out_error != nullptr) {
                *out_error = "invalid number";
            }
            return false;
        }
    }

    bool ParseArray(JsonValue* out_value, std::string* out_error) {
        ++pos_;
        SkipWs();
        if (pos_ < input_.size() && input_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            JsonValue item;
            if (!ParseValue(&item, out_error)) {
                return false;
            }
            out_value->array_value.push_back(std::move(item));
            SkipWs();
            if (pos_ >= input_.size()) {
                if (out_error != nullptr) {
                    *out_error = "unterminated array";
                }
                return false;
            }
            const char c = input_[pos_++];
            if (c == ']') {
                return true;
            }
            if (c != ',') {
                if (out_error != nullptr) {
                    *out_error = "expected ',' or ']'";
                }
                return false;
            }
            SkipWs();
        }
    }

    bool ParseObject(JsonValue* out_value, std::string* out_error) {
        ++pos_;
        SkipWs();
        if (pos_ < input_.size() && input_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            std::string key;
            if (!ParseString(&key, out_error)) {
                return false;
            }
            SkipWs();
            if (pos_ >= input_.size() || input_[pos_] != ':') {
                if (out_error != nullptr) {
                    *out_error = "expected ':' in object";
                }
                return false;
            }
            ++pos_;
            SkipWs();
            JsonValue value;
            if (!ParseValue(&value, out_error)) {
                return false;
            }
            out_value->object_value[key] = std::move(value);
            SkipWs();
            if (pos_ >= input_.size()) {
                if (out_error != nullptr) {
                    *out_error = "unterminated object";
                }
                return false;
            }
            const char c = input_[pos_++];
            if (c == '}') {
                return true;
            }
            if (c != ',') {
                if (out_error != nullptr) {
                    *out_error = "expected ',' or '}'";
                }
                return false;
            }
            SkipWs();
        }
    }

    void SkipWs() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }
    }

    bool StartsWith(std::string_view token) const {
        if (pos_ + token.size() > input_.size()) {
            return false;
        }
        return input_.substr(pos_, token.size()) == token;
    }

    std::string_view input_;
    std::size_t pos_ = 0;
};

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

const JsonValue* FindKey(const JsonValue& root, const std::string& key) {
    if (root.type != JsonValue::Type::Object) {
        return nullptr;
    }
    const auto it = root.object_value.find(key);
    if (it == root.object_value.end()) {
        return nullptr;
    }
    return &it->second;
}

bool TryGetString(const JsonValue& root, const std::string& key, std::string* out) {
    const auto* v = FindKey(root, key);
    if (v == nullptr || v->type != JsonValue::Type::String) {
        return false;
    }
    *out = v->string_value;
    return true;
}

bool TryGetNumber(const JsonValue& root, const std::string& key, double* out) {
    const auto* v = FindKey(root, key);
    if (v == nullptr || v->type != JsonValue::Type::Number) {
        return false;
    }
    *out = v->number_value;
    return true;
}

bool TryGetBool(const JsonValue& root, const std::string& key, bool* out) {
    const auto* v = FindKey(root, key);
    if (v == nullptr || v->type != JsonValue::Type::Bool) {
        return false;
    }
    *out = v->bool_value;
    return true;
}

bool TryGetIndex(const JsonValue& root, const std::string& key, std::size_t* out) {
    double n = 0.0;
    if (!TryGetNumber(root, key, &n)) {
        return false;
    }
    if (n < 0.0 || n > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
    *out = static_cast<std::size_t>(static_cast<std::uint32_t>(n));
    return true;
}

std::string DetectTextureFormat(const std::string& mime_type, const std::string& name_hint) {
    const auto mime = ToLower(mime_type);
    if (mime == "image/png") {
        return "png";
    }
    if (mime == "image/jpeg" || mime == "image/jpg") {
        return "jpeg";
    }
    if (mime == "image/bmp") {
        return "bmp";
    }
    if (mime == "image/tga") {
        return "tga";
    }
    const auto lower_name = ToLower(name_hint);
    const auto dot = lower_name.find_last_of('.');
    if (dot != std::string::npos && dot + 1U < lower_name.size()) {
        return lower_name.substr(dot + 1U);
    }
    return "binary";
}

std::uint32_t ReadU32Le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

float ReadF32Le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    const auto u = ReadU32Le(bytes, offset);
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(float));
    return f;
}

struct GlbChunk {
    std::uint32_t type = 0U;
    std::vector<std::uint8_t> bytes;
};

bool ParseGlb(const std::vector<std::uint8_t>& file_bytes, GlbChunk* out_json, GlbChunk* out_bin, std::string* out_error) {
    if (file_bytes.size() < 20U) {
        *out_error = "GLB file is too small";
        return false;
    }
    if (ReadU32Le(file_bytes, 0U) != 0x46546C67U) {  // 'glTF'
        *out_error = "invalid GLB magic";
        return false;
    }
    const auto version = ReadU32Le(file_bytes, 4U);
    if (version != 2U) {
        *out_error = "unsupported GLB version";
        return false;
    }
    const auto total_length = ReadU32Le(file_bytes, 8U);
    if (total_length > file_bytes.size()) {
        *out_error = "declared GLB length exceeds file size";
        return false;
    }

    std::size_t cursor = 12U;
    while (cursor + 8U <= total_length) {
        const auto chunk_len = ReadU32Le(file_bytes, cursor);
        const auto chunk_type = ReadU32Le(file_bytes, cursor + 4U);
        cursor += 8U;
        if (cursor + chunk_len > total_length) {
            *out_error = "chunk length out of range";
            return false;
        }
        GlbChunk chunk;
        chunk.type = chunk_type;
        chunk.bytes.insert(chunk.bytes.end(), file_bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                           file_bytes.begin() + static_cast<std::ptrdiff_t>(cursor + chunk_len));
        if (chunk_type == 0x4E4F534AU) {  // JSON
            *out_json = std::move(chunk);
        } else if (chunk_type == 0x004E4942U) {  // BIN
            *out_bin = std::move(chunk);
        }
        cursor += chunk_len;
    }

    if (out_json->bytes.empty()) {
        *out_error = "GLB JSON chunk missing";
        return false;
    }
    if (out_bin->bytes.empty()) {
        *out_error = "GLB BIN chunk missing";
        return false;
    }
    return true;
}

struct BufferViewMeta {
    std::size_t buffer = 0U;
    std::uint32_t byte_offset = 0U;
    std::uint32_t byte_length = 0U;
    std::uint32_t byte_stride = 0U;
};

struct AccessorMeta {
    std::size_t buffer_view = 0U;
    std::uint32_t byte_offset = 0U;
    std::uint32_t count = 0U;
    std::uint32_t component_type = 0U;
    std::string type;
};

bool LoadAccessorMeta(const JsonValue& accessor_obj, AccessorMeta* out) {
    double count_num = 0.0;
    double ctype_num = 0.0;
    if (!TryGetIndex(accessor_obj, "bufferView", &out->buffer_view) ||
        !TryGetNumber(accessor_obj, "count", &count_num) ||
        !TryGetNumber(accessor_obj, "componentType", &ctype_num) ||
        !TryGetString(accessor_obj, "type", &out->type)) {
        return false;
    }
    out->count = static_cast<std::uint32_t>(count_num);
    out->component_type = static_cast<std::uint32_t>(ctype_num);
    std::size_t accessor_off = 0U;
    if (TryGetIndex(accessor_obj, "byteOffset", &accessor_off)) {
        out->byte_offset = static_cast<std::uint32_t>(accessor_off);
    }
    return true;
}

bool LoadBufferViewMeta(const JsonValue& view_obj, BufferViewMeta* out) {
    std::size_t bo = 0U;
    std::size_t bl = 0U;
    if (!TryGetIndex(view_obj, "buffer", &out->buffer) || !TryGetIndex(view_obj, "byteLength", &bl)) {
        return false;
    }
    out->byte_length = static_cast<std::uint32_t>(bl);
    if (TryGetIndex(view_obj, "byteOffset", &bo)) {
        out->byte_offset = static_cast<std::uint32_t>(bo);
    }
    std::size_t bs = 0U;
    if (TryGetIndex(view_obj, "byteStride", &bs)) {
        out->byte_stride = static_cast<std::uint32_t>(bs);
    }
    return true;
}

bool ExtractPositions(const std::vector<std::uint8_t>& bin,
                      const std::vector<AccessorMeta>& accessors,
                      const std::vector<BufferViewMeta>& views,
                      std::size_t accessor_index,
                      std::vector<std::uint8_t>* out_vertex_blob,
                      std::uint32_t* out_vertex_count,
                      std::string* out_error) {
    if (accessor_index >= accessors.size()) {
        *out_error = "position accessor index out of range";
        return false;
    }
    const auto& a = accessors[accessor_index];
    if (a.component_type != 5126U || a.type != "VEC3") {
        *out_error = "POSITION accessor must be FLOAT VEC3";
        return false;
    }
    if (a.buffer_view >= views.size()) {
        *out_error = "position bufferView index out of range";
        return false;
    }
    const auto& bv = views[a.buffer_view];
    const std::size_t start = static_cast<std::size_t>(bv.byte_offset) + static_cast<std::size_t>(a.byte_offset);
    const std::size_t stride = bv.byte_stride > 0U ? bv.byte_stride : 12U;
    const std::size_t needed = start + (static_cast<std::size_t>(a.count) - 1U) * stride + 12U;
    if (a.count == 0U || needed > bin.size()) {
        *out_error = "position accessor data out of range";
        return false;
    }
    out_vertex_blob->clear();
    out_vertex_blob->reserve(static_cast<std::size_t>(a.count) * 12U);
    for (std::uint32_t i = 0U; i < a.count; ++i) {
        const std::size_t base = start + static_cast<std::size_t>(i) * stride;
        const float px = ReadF32Le(bin, base);
        const float py = ReadF32Le(bin, base + 4U);
        const float pz = ReadF32Le(bin, base + 8U);
        const std::array<float, 3U> v = {px, py, pz};
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(v.data());
        out_vertex_blob->insert(out_vertex_blob->end(), bytes, bytes + sizeof(v));
    }
    *out_vertex_count = a.count;
    return true;
}

bool ExtractPositionDeltas(const std::vector<std::uint8_t>& bin,
                           const std::vector<AccessorMeta>& accessors,
                           const std::vector<BufferViewMeta>& views,
                           std::size_t accessor_index,
                           std::vector<std::uint8_t>* out_delta_vertices,
                           std::uint32_t expected_vertex_count,
                           std::string* out_error) {
    if (accessor_index >= accessors.size()) {
        *out_error = "morph target accessor index out of range";
        return false;
    }
    const auto& a = accessors[accessor_index];
    if (a.component_type != 5126U || a.type != "VEC3") {
        *out_error = "morph target accessor must be FLOAT VEC3";
        return false;
    }
    if (expected_vertex_count > 0U && a.count != expected_vertex_count) {
        *out_error = "morph target vertex count mismatch";
        return false;
    }
    if (a.buffer_view >= views.size()) {
        *out_error = "morph target bufferView index out of range";
        return false;
    }
    const auto& bv = views[a.buffer_view];
    const std::size_t start = static_cast<std::size_t>(bv.byte_offset) + static_cast<std::size_t>(a.byte_offset);
    const std::size_t stride = bv.byte_stride > 0U ? bv.byte_stride : 12U;
    const std::size_t needed = start + (static_cast<std::size_t>(a.count) - 1U) * stride + 12U;
    if (a.count == 0U || needed > bin.size()) {
        *out_error = "morph target accessor data out of range";
        return false;
    }
    out_delta_vertices->clear();
    out_delta_vertices->reserve(static_cast<std::size_t>(a.count) * 12U);
    for (std::uint32_t i = 0U; i < a.count; ++i) {
        const std::size_t base = start + static_cast<std::size_t>(i) * stride;
        const float px = ReadF32Le(bin, base);
        const float py = ReadF32Le(bin, base + 4U);
        const float pz = ReadF32Le(bin, base + 8U);
        const std::array<float, 3U> v = {px, py, pz};
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(v.data());
        out_delta_vertices->insert(out_delta_vertices->end(), bytes, bytes + sizeof(v));
    }
    return true;
}

bool ExtractIndices(const std::vector<std::uint8_t>& bin,
                    const std::vector<AccessorMeta>& accessors,
                    const std::vector<BufferViewMeta>& views,
                    std::size_t accessor_index,
                    std::vector<std::uint32_t>* out_indices,
                    std::string* out_error) {
    if (accessor_index >= accessors.size()) {
        *out_error = "index accessor index out of range";
        return false;
    }
    const auto& a = accessors[accessor_index];
    if (a.type != "SCALAR") {
        *out_error = "index accessor must be SCALAR";
        return false;
    }
    if (a.buffer_view >= views.size()) {
        *out_error = "index bufferView index out of range";
        return false;
    }
    const auto& bv = views[a.buffer_view];
    const std::size_t start = static_cast<std::size_t>(bv.byte_offset) + static_cast<std::size_t>(a.byte_offset);
    std::size_t comp_size = 0U;
    switch (a.component_type) {
        case 5121U:
            comp_size = 1U;
            break;  // U8
        case 5123U:
            comp_size = 2U;
            break;  // U16
        case 5125U:
            comp_size = 4U;
            break;  // U32
        default:
            *out_error = "unsupported index component type";
            return false;
    }
    const std::size_t stride = bv.byte_stride > 0U ? bv.byte_stride : comp_size;
    const std::size_t needed = start + (static_cast<std::size_t>(a.count) - 1U) * stride + comp_size;
    if (a.count == 0U || needed > bin.size()) {
        *out_error = "index accessor data out of range";
        return false;
    }
    out_indices->clear();
    out_indices->reserve(a.count);
    for (std::uint32_t i = 0U; i < a.count; ++i) {
        const std::size_t base = start + static_cast<std::size_t>(i) * stride;
        std::uint32_t idx = 0U;
        if (comp_size == 1U) {
            idx = bin[base];
        } else if (comp_size == 2U) {
            idx = static_cast<std::uint32_t>(bin[base]) | (static_cast<std::uint32_t>(bin[base + 1U]) << 8U);
        } else {
            idx = ReadU32Le(bin, base);
        }
        out_indices->push_back(idx);
    }
    return true;
}

bool ExtractTexcoord0(const std::vector<std::uint8_t>& bin,
                      const std::vector<AccessorMeta>& accessors,
                      const std::vector<BufferViewMeta>& views,
                      std::size_t accessor_index,
                      std::vector<std::array<float, 2U>>* out_uvs,
                      std::string* out_error) {
    if (accessor_index >= accessors.size()) {
        *out_error = "texcoord accessor index out of range";
        return false;
    }
    const auto& a = accessors[accessor_index];
    if (a.component_type != 5126U || a.type != "VEC2") {
        *out_error = "TEXCOORD_0 accessor must be FLOAT VEC2";
        return false;
    }
    if (a.buffer_view >= views.size()) {
        *out_error = "texcoord bufferView index out of range";
        return false;
    }
    const auto& bv = views[a.buffer_view];
    const std::size_t start = static_cast<std::size_t>(bv.byte_offset) + static_cast<std::size_t>(a.byte_offset);
    const std::size_t stride = bv.byte_stride > 0U ? bv.byte_stride : 8U;
    const std::size_t needed = start + (static_cast<std::size_t>(a.count) - 1U) * stride + 8U;
    if (a.count == 0U || needed > bin.size()) {
        *out_error = "texcoord accessor data out of range";
        return false;
    }
    out_uvs->clear();
    out_uvs->reserve(a.count);
    for (std::uint32_t i = 0U; i < a.count; ++i) {
        const std::size_t base = start + static_cast<std::size_t>(i) * stride;
        const float u = ReadF32Le(bin, base);
        const float v = ReadF32Le(bin, base + 4U);
        out_uvs->push_back({u, v});
    }
    return true;
}

struct MaterialInfo {
    std::string name;
    std::string shader_name = "MToon (minimal)";
    std::string base_color_texture_name;
    bool double_sided = false;
    std::string alpha_mode = "OPAQUE";
    float alpha_cutoff = 0.5f;
};

void AddExpressionIfMissing(std::vector<ExpressionState>* out,
                            const std::string& name,
                            const std::string& mapping_kind,
                            float default_weight = 0.0f) {
    for (const auto& existing : *out) {
        if (ToLower(existing.name) == ToLower(name)) {
            return;
        }
    }
    ExpressionState expr;
    expr.name = name;
    expr.mapping_kind = mapping_kind;
    expr.default_weight = default_weight;
    expr.runtime_weight = default_weight;
    out->push_back(std::move(expr));
}

ExpressionState* FindExpression(std::vector<ExpressionState>* out, const std::string& name) {
    if (out == nullptr) {
        return nullptr;
    }
    for (auto& expr : *out) {
        if (ToLower(expr.name) == ToLower(name)) {
            return &expr;
        }
    }
    return nullptr;
}

void AddExpressionBindIfMissing(ExpressionState* expr,
                                const std::string& mesh_name,
                                const std::string& frame_name,
                                float weight_scale) {
    if (expr == nullptr || mesh_name.empty() || frame_name.empty()) {
        return;
    }
    for (const auto& existing : expr->binds) {
        if (ToLower(existing.mesh_name) == ToLower(mesh_name) &&
            ToLower(existing.frame_name) == ToLower(frame_name)) {
            return;
        }
    }
    ExpressionState::Bind bind;
    bind.mesh_name = mesh_name;
    bind.frame_name = frame_name;
    bind.weight_scale = std::max(0.0f, std::min(1.0f, weight_scale));
    expr->binds.push_back(std::move(bind));
}

void AddHeuristicExpressionBinds(
    std::vector<ExpressionState>* out_expressions,
    const std::vector<BlendShapeRenderPayload>& blendshape_payloads) {
    if (out_expressions == nullptr || blendshape_payloads.empty()) {
        return;
    }
    for (auto& expr : *out_expressions) {
        for (const auto& mesh_bs : blendshape_payloads) {
            for (const auto& frame : mesh_bs.frames) {
                const auto lname = ToLower(frame.name);
                bool match = false;
                if (expr.mapping_kind == "blink" && lname.find("blink") != std::string::npos) {
                    match = true;
                } else if (expr.mapping_kind == "viseme_aa" &&
                           (lname == "a" || lname == "aa" || lname.find("mouth") != std::string::npos ||
                            lname.find("viseme") != std::string::npos)) {
                    match = true;
                } else if (expr.mapping_kind == "joy" &&
                           (lname.find("joy") != std::string::npos || lname.find("happy") != std::string::npos ||
                            lname.find("smile") != std::string::npos)) {
                    match = true;
                }
                if (match) {
                    AddExpressionBindIfMissing(&expr, mesh_bs.mesh_name, frame.name, 1.0f);
                }
            }
        }
    }
}

void ParseVrmExpressionEntries(
    const JsonValue& root,
    const std::vector<BlendShapeRenderPayload>& blendshape_payloads,
    const std::vector<std::size_t>& node_to_mesh_index,
    const std::vector<std::string>& mesh_names_by_index,
    const std::unordered_map<std::string, std::vector<std::string>>& mesh_frame_names,
    std::vector<ExpressionState>* out_expressions) {
    const auto* extensions = FindKey(root, "extensions");
    if (extensions == nullptr || extensions->type != JsonValue::Type::Object) {
        return;
    }

    const auto* vrmc_vrm = FindKey(*extensions, "VRMC_vrm");
    if (vrmc_vrm != nullptr && vrmc_vrm->type == JsonValue::Type::Object) {
        const auto* expressions = FindKey(*vrmc_vrm, "expressions");
        if (expressions != nullptr && expressions->type == JsonValue::Type::Object) {
            const auto* preset = FindKey(*expressions, "preset");
            if (preset != nullptr && preset->type == JsonValue::Type::Object) {
                const std::array<std::pair<const char*, const char*>, 6U> preset_mappings = {
                    std::pair<const char*, const char*> {"blink", "blink"},
                    {"aa", "viseme_aa"},
                    {"joy", "joy"},
                    {"angry", "none"},
                    {"sorrow", "none"},
                    {"fun", "none"},
                };
                for (const auto& [name, mapping] : preset_mappings) {
                    const auto* entry = FindKey(*preset, name);
                    if (entry != nullptr && entry->type == JsonValue::Type::Object) {
                        AddExpressionIfMissing(out_expressions, name, mapping);
                        auto* expr = FindExpression(out_expressions, name);
                        const auto* mtb = FindKey(*entry, "morphTargetBinds");
                        if (expr != nullptr && mtb != nullptr && mtb->type == JsonValue::Type::Array) {
                            for (const auto& bind : mtb->array_value) {
                                if (bind.type != JsonValue::Type::Object) {
                                    continue;
                                }
                                std::size_t node_index = std::numeric_limits<std::size_t>::max();
                                std::size_t target_index = std::numeric_limits<std::size_t>::max();
                                if (!TryGetIndex(bind, "node", &node_index) ||
                                    !TryGetIndex(bind, "index", &target_index)) {
                                    continue;
                                }
                                if (node_index >= node_to_mesh_index.size()) {
                                    continue;
                                }
                                const auto mesh_index = node_to_mesh_index[node_index];
                                if (mesh_index >= mesh_names_by_index.size()) {
                                    continue;
                                }
                                const auto& mesh_name = mesh_names_by_index[mesh_index];
                                const auto frame_it = mesh_frame_names.find(mesh_name);
                                if (frame_it == mesh_frame_names.end() || target_index >= frame_it->second.size()) {
                                    continue;
                                }
                                double weight = 1.0;
                                TryGetNumber(bind, "weight", &weight);
                                AddExpressionBindIfMissing(
                                    expr, mesh_name, frame_it->second[target_index], static_cast<float>(weight));
                            }
                        }
                    }
                }
            }
            const auto* custom = FindKey(*expressions, "custom");
            if (custom != nullptr && custom->type == JsonValue::Type::Object) {
                for (const auto& [name, value] : custom->object_value) {
                    if (value.type == JsonValue::Type::Object) {
                        AddExpressionIfMissing(out_expressions, name, "none");
                        auto* expr = FindExpression(out_expressions, name);
                        const auto* mtb = FindKey(value, "morphTargetBinds");
                        if (expr != nullptr && mtb != nullptr && mtb->type == JsonValue::Type::Array) {
                            for (const auto& bind : mtb->array_value) {
                                if (bind.type != JsonValue::Type::Object) {
                                    continue;
                                }
                                std::size_t node_index = std::numeric_limits<std::size_t>::max();
                                std::size_t target_index = std::numeric_limits<std::size_t>::max();
                                if (!TryGetIndex(bind, "node", &node_index) ||
                                    !TryGetIndex(bind, "index", &target_index)) {
                                    continue;
                                }
                                if (node_index >= node_to_mesh_index.size()) {
                                    continue;
                                }
                                const auto mesh_index = node_to_mesh_index[node_index];
                                if (mesh_index >= mesh_names_by_index.size()) {
                                    continue;
                                }
                                const auto& mesh_name = mesh_names_by_index[mesh_index];
                                const auto frame_it = mesh_frame_names.find(mesh_name);
                                if (frame_it == mesh_frame_names.end() || target_index >= frame_it->second.size()) {
                                    continue;
                                }
                                double weight = 1.0;
                                TryGetNumber(bind, "weight", &weight);
                                AddExpressionBindIfMissing(
                                    expr, mesh_name, frame_it->second[target_index], static_cast<float>(weight));
                            }
                        }
                    }
                }
            }
        }
    }

    // Legacy VRM0.x path: extensions.VRM.blendShapeMaster.blendShapeGroups[]
    const auto* vrm_legacy = FindKey(*extensions, "VRM");
    if (vrm_legacy != nullptr && vrm_legacy->type == JsonValue::Type::Object) {
        const auto* bsm = FindKey(*vrm_legacy, "blendShapeMaster");
        if (bsm == nullptr || bsm->type != JsonValue::Type::Object) {
            return;
        }
        const auto* groups = FindKey(*bsm, "blendShapeGroups");
        if (groups == nullptr || groups->type != JsonValue::Type::Array) {
            return;
        }
        for (const auto& group : groups->array_value) {
            if (group.type != JsonValue::Type::Object) {
                continue;
            }
            std::string expr_name;
            if (!TryGetString(group, "presetName", &expr_name)) {
                TryGetString(group, "name", &expr_name);
            }
            if (expr_name.empty()) {
                continue;
            }
            const auto lowered = ToLower(expr_name);
            std::string mapping_kind = "none";
            if (lowered.find("blink") != std::string::npos) {
                mapping_kind = "blink";
            } else if (lowered == "aa" || lowered.find("mouth") != std::string::npos) {
                mapping_kind = "viseme_aa";
            } else if (lowered.find("joy") != std::string::npos || lowered.find("happy") != std::string::npos) {
                mapping_kind = "joy";
            }
            AddExpressionIfMissing(out_expressions, expr_name, mapping_kind);
            auto* expr = FindExpression(out_expressions, expr_name);
            const auto* binds = FindKey(group, "binds");
            if (expr != nullptr && binds != nullptr && binds->type == JsonValue::Type::Array) {
                for (const auto& bind : binds->array_value) {
                    if (bind.type != JsonValue::Type::Object) {
                        continue;
                    }
                    std::size_t mesh_index = std::numeric_limits<std::size_t>::max();
                    std::size_t target_index = std::numeric_limits<std::size_t>::max();
                    if (!TryGetIndex(bind, "mesh", &mesh_index) ||
                        !TryGetIndex(bind, "index", &target_index)) {
                        continue;
                    }
                    if (mesh_index >= mesh_names_by_index.size()) {
                        continue;
                    }
                    const auto& mesh_name = mesh_names_by_index[mesh_index];
                    const auto frame_it = mesh_frame_names.find(mesh_name);
                    if (frame_it == mesh_frame_names.end() || target_index >= frame_it->second.size()) {
                        continue;
                    }
                    double weight = 100.0;
                    TryGetNumber(bind, "weight", &weight);
                    const float normalized = static_cast<float>(std::max(0.0, std::min(1.0, weight / 100.0)));
                    AddExpressionBindIfMissing(expr, mesh_name, frame_it->second[target_index], normalized);
                }
            }
        }
    }

    AddHeuristicExpressionBinds(out_expressions, blendshape_payloads);
}

void ParseSpringBoneSummary(const JsonValue& root, SpringBoneSummary* out_summary) {
    if (out_summary == nullptr) {
        return;
    }
    const auto* extensions = FindKey(root, "extensions");
    if (extensions == nullptr || extensions->type != JsonValue::Type::Object) {
        return;
    }

    const auto* vrmc_spring = FindKey(*extensions, "VRMC_springBone");
    if (vrmc_spring != nullptr && vrmc_spring->type == JsonValue::Type::Object) {
        out_summary->present = true;
        const auto* colliders = FindKey(*vrmc_spring, "colliders");
        if (colliders != nullptr && colliders->type == JsonValue::Type::Array) {
            out_summary->collider_count = static_cast<std::uint32_t>(colliders->array_value.size());
        }
        const auto* collider_groups = FindKey(*vrmc_spring, "colliderGroups");
        if (collider_groups != nullptr && collider_groups->type == JsonValue::Type::Array) {
            out_summary->collider_group_count = static_cast<std::uint32_t>(collider_groups->array_value.size());
        }
        const auto* springs = FindKey(*vrmc_spring, "springs");
        if (springs != nullptr && springs->type == JsonValue::Type::Array) {
            out_summary->spring_count = static_cast<std::uint32_t>(springs->array_value.size());
            std::uint32_t joints = 0U;
            for (const auto& spring : springs->array_value) {
                if (spring.type != JsonValue::Type::Object) {
                    continue;
                }
                const auto* spring_joints = FindKey(spring, "joints");
                if (spring_joints != nullptr && spring_joints->type == JsonValue::Type::Array) {
                    joints += static_cast<std::uint32_t>(spring_joints->array_value.size());
                }
            }
            out_summary->joint_count = joints;
        }
        return;
    }

    const auto* vrm_legacy = FindKey(*extensions, "VRM");
    if (vrm_legacy == nullptr || vrm_legacy->type != JsonValue::Type::Object) {
        return;
    }
    const auto* secondary = FindKey(*vrm_legacy, "secondaryAnimation");
    if (secondary == nullptr || secondary->type != JsonValue::Type::Object) {
        return;
    }
    out_summary->present = true;
    const auto* collider_groups = FindKey(*secondary, "colliderGroups");
    if (collider_groups != nullptr && collider_groups->type == JsonValue::Type::Array) {
        out_summary->collider_group_count = static_cast<std::uint32_t>(collider_groups->array_value.size());
        std::uint32_t colliders = 0U;
        for (const auto& group : collider_groups->array_value) {
            if (group.type != JsonValue::Type::Object) {
                continue;
            }
            const auto* cols = FindKey(group, "colliders");
            if (cols != nullptr && cols->type == JsonValue::Type::Array) {
                colliders += static_cast<std::uint32_t>(cols->array_value.size());
            }
        }
        out_summary->collider_count = colliders;
    }
    const auto* bone_groups = FindKey(*secondary, "boneGroups");
    if (bone_groups != nullptr && bone_groups->type == JsonValue::Type::Array) {
        out_summary->spring_count = static_cast<std::uint32_t>(bone_groups->array_value.size());
        std::uint32_t joints = 0U;
        for (const auto& group : bone_groups->array_value) {
            if (group.type != JsonValue::Type::Object) {
                continue;
            }
            const auto* bones = FindKey(group, "bones");
            if (bones != nullptr && bones->type == JsonValue::Type::Array) {
                joints += static_cast<std::uint32_t>(bones->array_value.size());
            }
        }
        out_summary->joint_count = joints;
    }
}

bool ReadBufferViewBytes(const std::vector<std::uint8_t>& bin,
                         const std::vector<BufferViewMeta>& views,
                         std::size_t view_index,
                         std::vector<std::uint8_t>* out_bytes,
                         std::string* out_error) {
    if (view_index >= views.size()) {
        if (out_error != nullptr) {
            *out_error = "bufferView index out of range";
        }
        return false;
    }
    const auto& view = views[view_index];
    const std::size_t start = static_cast<std::size_t>(view.byte_offset);
    const std::size_t length = static_cast<std::size_t>(view.byte_length);
    if (length == 0U || start + length > bin.size()) {
        if (out_error != nullptr) {
            *out_error = "bufferView range out of BIN bounds";
        }
        return false;
    }
    out_bytes->assign(
        bin.begin() + static_cast<std::ptrdiff_t>(start),
        bin.begin() + static_cast<std::ptrdiff_t>(start + length));
    return true;
}

}  // namespace

bool VrmLoader::CanLoadPath(const std::string& path) const {
    const auto ext = ToLower(fs::path(path).extension().string());
    return ext == ".vrm";
}

bool VrmLoader::CanLoadBytes(const std::vector<std::uint8_t>& head) const {
    return head.size() >= 4U && head[0] == 'g' && head[1] == 'l' && head[2] == 'T' && head[3] == 'F';
}

core::Result<AvatarPackage> VrmLoader::Load(const std::string& path) const {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return core::Result<AvatarPackage>::Fail("could not open vrm file");
    }

    AvatarPackage pkg;
    pkg.source_type = AvatarSourceType::Vrm;
    pkg.compat_level = AvatarCompatLevel::Failed;
    pkg.parser_stage = "parse";
    pkg.primary_error_code = "NONE";
    pkg.source_path = path;
    pkg.display_name = fs::path(path).stem().string();

    std::vector<std::uint8_t> file_bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    GlbChunk json_chunk;
    GlbChunk bin_chunk;
    std::string glb_error;
    if (!ParseGlb(file_bytes, &json_chunk, &bin_chunk, &glb_error)) {
        pkg.primary_error_code = "VRM_SCHEMA_INVALID";
        pkg.warnings.push_back("E_PARSE: VRM_SCHEMA_INVALID: " + glb_error);
        return core::Result<AvatarPackage>::Ok(pkg);
    }

    std::string json_text(reinterpret_cast<const char*>(json_chunk.bytes.data()), json_chunk.bytes.size());
    JsonValue root;
    std::string json_error;
    JsonParser parser(json_text);
    if (!parser.Parse(&root, &json_error)) {
        pkg.primary_error_code = "VRM_SCHEMA_INVALID";
        pkg.warnings.push_back("E_PARSE: VRM_SCHEMA_INVALID: glTF JSON parse failed: " + json_error);
        return core::Result<AvatarPackage>::Ok(pkg);
    }

    const auto* accessors_v = FindKey(root, "accessors");
    const auto* views_v = FindKey(root, "bufferViews");
    const auto* meshes_v = FindKey(root, "meshes");
    if (accessors_v == nullptr || views_v == nullptr || meshes_v == nullptr ||
        accessors_v->type != JsonValue::Type::Array || views_v->type != JsonValue::Type::Array ||
        meshes_v->type != JsonValue::Type::Array) {
        pkg.primary_error_code = "VRM_SCHEMA_INVALID";
        pkg.warnings.push_back("E_PARSE: VRM_SCHEMA_INVALID: required arrays accessors/bufferViews/meshes missing");
        return core::Result<AvatarPackage>::Ok(pkg);
    }

    std::vector<AccessorMeta> accessors;
    accessors.reserve(accessors_v->array_value.size());
    for (const auto& item : accessors_v->array_value) {
        if (item.type != JsonValue::Type::Object) {
            continue;
        }
        AccessorMeta a;
        if (LoadAccessorMeta(item, &a)) {
            accessors.push_back(a);
        } else {
            accessors.push_back(AccessorMeta{});
        }
    }

    std::vector<BufferViewMeta> views;
    views.reserve(views_v->array_value.size());
    for (const auto& item : views_v->array_value) {
        if (item.type != JsonValue::Type::Object) {
            continue;
        }
        BufferViewMeta v;
        if (LoadBufferViewMeta(item, &v)) {
            views.push_back(v);
        } else {
            views.push_back(BufferViewMeta{});
        }
    }

    std::vector<std::string> mesh_names_by_index;
    mesh_names_by_index.resize(meshes_v->array_value.size());
    for (std::size_t mesh_i = 0U; mesh_i < meshes_v->array_value.size(); ++mesh_i) {
        const auto& mesh = meshes_v->array_value[mesh_i];
        std::string mesh_name = "Mesh" + std::to_string(mesh_i);
        if (mesh.type == JsonValue::Type::Object) {
            TryGetString(mesh, "name", &mesh_name);
        }
        mesh_names_by_index[mesh_i] = mesh_name;
    }

    std::vector<std::size_t> node_to_mesh_index;
    const auto* nodes_v = FindKey(root, "nodes");
    if (nodes_v != nullptr && nodes_v->type == JsonValue::Type::Array) {
        node_to_mesh_index.assign(
            nodes_v->array_value.size(),
            std::numeric_limits<std::size_t>::max());
        for (std::size_t node_i = 0U; node_i < nodes_v->array_value.size(); ++node_i) {
            const auto& node = nodes_v->array_value[node_i];
            if (node.type != JsonValue::Type::Object) {
                continue;
            }
            std::size_t mesh_index = std::numeric_limits<std::size_t>::max();
            if (TryGetIndex(node, "mesh", &mesh_index)) {
                node_to_mesh_index[node_i] = mesh_index;
            }
        }
    }

    std::unordered_map<std::string, std::vector<std::string>> mesh_frame_names;

    pkg.parser_stage = "resolve";
    const auto* textures_v = FindKey(root, "textures");
    const auto* images_v = FindKey(root, "images");
    const auto* materials_v = FindKey(root, "materials");

    struct TextureRef {
        std::string name;
        std::vector<std::uint8_t> bytes;
        std::string format;
        bool valid = false;
    };
    std::vector<TextureRef> image_table;
    if (images_v != nullptr && images_v->type == JsonValue::Type::Array) {
        image_table.resize(images_v->array_value.size());
        for (std::size_t i = 0U; i < images_v->array_value.size(); ++i) {
            const auto& img = images_v->array_value[i];
            if (img.type != JsonValue::Type::Object) {
                continue;
            }
            std::string image_name = "Image_" + std::to_string(i);
            TryGetString(img, "name", &image_name);
            std::string mime_type;
            TryGetString(img, "mimeType", &mime_type);
            image_table[i].name = image_name;
            image_table[i].format = DetectTextureFormat(mime_type, image_name);

            std::size_t buffer_view_index = 0U;
            if (!TryGetIndex(img, "bufferView", &buffer_view_index)) {
                pkg.warnings.push_back("W_PAYLOAD: VRM_TEXTURE_MISSING: image has no bufferView: " + image_name);
                continue;
            }
            std::string texture_err;
            if (!ReadBufferViewBytes(bin_chunk.bytes, views, buffer_view_index, &image_table[i].bytes, &texture_err)) {
                pkg.warnings.push_back(
                    "W_PAYLOAD: VRM_TEXTURE_MISSING: failed to read image '" + image_name + "': " + texture_err);
                continue;
            }
            image_table[i].valid = true;
        }
    }

    std::vector<std::size_t> texture_to_image;
    if (textures_v != nullptr && textures_v->type == JsonValue::Type::Array) {
        texture_to_image.assign(textures_v->array_value.size(), std::numeric_limits<std::size_t>::max());
        for (std::size_t i = 0U; i < textures_v->array_value.size(); ++i) {
            const auto& tex = textures_v->array_value[i];
            if (tex.type != JsonValue::Type::Object) {
                continue;
            }
            std::size_t source_index = std::numeric_limits<std::size_t>::max();
            if (TryGetIndex(tex, "source", &source_index)) {
                texture_to_image[i] = source_index;
            }
        }
    }

    std::vector<MaterialInfo> parsed_materials;
    std::uint32_t missing_texture_refs = 0U;
    std::uint32_t unsupported_materials = 0U;
    if (materials_v != nullptr && materials_v->type == JsonValue::Type::Array) {
        parsed_materials.reserve(materials_v->array_value.size());
        for (std::size_t i = 0U; i < materials_v->array_value.size(); ++i) {
            const auto& material = materials_v->array_value[i];
            if (material.type != JsonValue::Type::Object) {
                ++unsupported_materials;
                pkg.warnings.push_back("W_PARSE: VRM_MATERIAL_UNSUPPORTED: material entry is not an object");
                continue;
            }
            MaterialInfo info;
            info.name = "Material_" + std::to_string(i);
            TryGetString(material, "name", &info.name);
            TryGetBool(material, "doubleSided", &info.double_sided);
            TryGetString(material, "alphaMode", &info.alpha_mode);
            double alpha_cutoff = static_cast<double>(info.alpha_cutoff);
            if (TryGetNumber(material, "alphaCutoff", &alpha_cutoff)) {
                info.alpha_cutoff = static_cast<float>(alpha_cutoff);
            }

            const auto* pbr = FindKey(material, "pbrMetallicRoughness");
            if (pbr != nullptr && pbr->type == JsonValue::Type::Object) {
                const auto* tex_obj = FindKey(*pbr, "baseColorTexture");
                if (tex_obj != nullptr && tex_obj->type == JsonValue::Type::Object) {
                    std::size_t texture_index = std::numeric_limits<std::size_t>::max();
                    if (TryGetIndex(*tex_obj, "index", &texture_index)) {
                        if (texture_index < texture_to_image.size()) {
                            const auto image_index = texture_to_image[texture_index];
                            if (image_index < image_table.size() && image_table[image_index].valid) {
                                info.base_color_texture_name = image_table[image_index].name;
                            } else {
                                ++missing_texture_refs;
                                pkg.warnings.push_back(
                                    "W_PAYLOAD: VRM_TEXTURE_MISSING: baseColorTexture source missing for material '" +
                                    info.name + "'");
                            }
                        } else {
                            ++missing_texture_refs;
                            pkg.warnings.push_back(
                                "W_PAYLOAD: VRM_TEXTURE_MISSING: baseColorTexture index out of range for material '" +
                                info.name + "'");
                        }
                    }
                }
            }
            parsed_materials.push_back(std::move(info));
        }
    }

    for (const auto& img : image_table) {
        if (!img.valid) {
            continue;
        }
        TextureRenderPayload texture_payload;
        texture_payload.name = img.name;
        texture_payload.format = img.format;
        texture_payload.bytes = img.bytes;
        pkg.texture_payloads.push_back(std::move(texture_payload));
    }

    for (const auto& m : parsed_materials) {
        pkg.materials.push_back({m.name, m.shader_name});
        MaterialRenderPayload material_payload;
        material_payload.name = m.name;
        material_payload.shader_name = m.shader_name;
        material_payload.base_color_texture_name = m.base_color_texture_name;
        material_payload.alpha_mode = m.alpha_mode;
        material_payload.alpha_cutoff = m.alpha_cutoff;
        material_payload.double_sided = m.double_sided;
        pkg.material_payloads.push_back(std::move(material_payload));
        std::ostringstream material_diag;
        material_diag << "W_MATERIAL: " << m.name
                      << ", alphaMode=" << m.alpha_mode
                      << ", alphaCutoff=" << m.alpha_cutoff
                      << ", doubleSided=" << (m.double_sided ? "true" : "false");
        if (!m.base_color_texture_name.empty()) {
            material_diag << ", baseTexture=" << m.base_color_texture_name;
        }
        pkg.warnings.push_back(material_diag.str());
    }

    std::size_t mesh_added = 0U;
    for (std::size_t mesh_i = 0U; mesh_i < meshes_v->array_value.size(); ++mesh_i) {
        const auto& mesh = meshes_v->array_value[mesh_i];
        if (mesh.type != JsonValue::Type::Object) {
            continue;
        }
        const auto* prims_v = FindKey(mesh, "primitives");
        if (prims_v == nullptr || prims_v->type != JsonValue::Type::Array || prims_v->array_value.empty()) {
            continue;
        }
        const auto& prim = prims_v->array_value.front();
        if (prim.type != JsonValue::Type::Object) {
            continue;
        }
        const auto* attrs_v = FindKey(prim, "attributes");
        if (attrs_v == nullptr || attrs_v->type != JsonValue::Type::Object) {
            continue;
        }
        const auto* pos_v = FindKey(*attrs_v, "POSITION");
        if (pos_v == nullptr || pos_v->type != JsonValue::Type::Number) {
            continue;
        }
        const std::size_t pos_accessor = static_cast<std::size_t>(static_cast<std::uint32_t>(pos_v->number_value));

        MeshRenderPayload mesh_payload;
        std::string mesh_name;
        if (!TryGetString(mesh, "name", &mesh_name)) {
            mesh_name = "Mesh" + std::to_string(mesh_i);
        }
        mesh_payload.name = mesh_name;
        mesh_payload.vertex_stride = 12U;
        std::uint32_t vtx_count = 0U;
        std::string read_error;
        if (!ExtractPositions(bin_chunk.bytes, accessors, views, pos_accessor, &mesh_payload.vertex_blob, &vtx_count, &read_error)) {
            pkg.warnings.push_back("W_PAYLOAD: VRM_POSITION_READ_FAILED: " + read_error);
            continue;
        }

        const auto* uv0_v = FindKey(*attrs_v, "TEXCOORD_0");
        if (uv0_v != nullptr && uv0_v->type == JsonValue::Type::Number) {
            const std::size_t uv_accessor = static_cast<std::size_t>(static_cast<std::uint32_t>(uv0_v->number_value));
            std::vector<std::array<float, 2U>> uvs;
            if (ExtractTexcoord0(bin_chunk.bytes, accessors, views, uv_accessor, &uvs, &read_error) &&
                uvs.size() == static_cast<std::size_t>(vtx_count)) {
                std::vector<std::uint8_t> interleaved;
                interleaved.reserve(static_cast<std::size_t>(vtx_count) * 20U);
                const auto* pos_bytes = mesh_payload.vertex_blob.data();
                for (std::uint32_t i = 0U; i < vtx_count; ++i) {
                    const std::size_t pos_off = static_cast<std::size_t>(i) * 12U;
                    interleaved.insert(interleaved.end(), pos_bytes + pos_off, pos_bytes + pos_off + 12U);
                    const auto* uv_bytes = reinterpret_cast<const std::uint8_t*>(uvs[i].data());
                    interleaved.insert(interleaved.end(), uv_bytes, uv_bytes + 8U);
                }
                mesh_payload.vertex_blob = std::move(interleaved);
                mesh_payload.vertex_stride = 20U;
            } else {
                pkg.warnings.push_back("W_PAYLOAD: VRM_TEXCOORD0_READ_FAILED: " + read_error);
            }
        }

        std::size_t idx_accessor = std::numeric_limits<std::size_t>::max();
        if (TryGetIndex(prim, "indices", &idx_accessor)) {
            if (!ExtractIndices(bin_chunk.bytes, accessors, views, idx_accessor, &mesh_payload.indices, &read_error)) {
                pkg.warnings.push_back("W_PAYLOAD: VRM_INDEX_READ_FAILED: " + read_error);
                mesh_payload.indices.clear();
            }
        }
        std::size_t material_index = std::numeric_limits<std::size_t>::max();
        if (TryGetIndex(prim, "material", &material_index)) {
            mesh_payload.material_index = static_cast<std::int32_t>(material_index);
        }

        if (mesh_payload.indices.empty()) {
            mesh_payload.indices.reserve(vtx_count);
            for (std::uint32_t i = 0U; i < vtx_count; ++i) {
                mesh_payload.indices.push_back(i);
            }
        }

        const auto* targets_v = FindKey(prim, "targets");
        std::vector<std::string> target_names;
        const auto* extras_v = FindKey(mesh, "extras");
        if (extras_v != nullptr && extras_v->type == JsonValue::Type::Object) {
            const auto* target_names_v = FindKey(*extras_v, "targetNames");
            if (target_names_v != nullptr && target_names_v->type == JsonValue::Type::Array) {
                target_names.reserve(target_names_v->array_value.size());
                for (const auto& n : target_names_v->array_value) {
                    if (n.type == JsonValue::Type::String) {
                        target_names.push_back(n.string_value);
                    } else {
                        target_names.push_back(std::string());
                    }
                }
            }
        }
        std::vector<float> target_default_weights;
        const auto* weights_v = FindKey(mesh, "weights");
        if (weights_v != nullptr && weights_v->type == JsonValue::Type::Array) {
            target_default_weights.reserve(weights_v->array_value.size());
            for (const auto& w : weights_v->array_value) {
                if (w.type == JsonValue::Type::Number) {
                    target_default_weights.push_back(static_cast<float>(w.number_value));
                } else {
                    target_default_weights.push_back(0.0f);
                }
            }
        }
        BlendShapeRenderPayload blendshape_payload;
        blendshape_payload.mesh_name = mesh_name;
        if (targets_v != nullptr && targets_v->type == JsonValue::Type::Array) {
            for (std::size_t target_i = 0U; target_i < targets_v->array_value.size(); ++target_i) {
                const auto& target = targets_v->array_value[target_i];
                if (target.type != JsonValue::Type::Object) {
                    continue;
                }
                const auto* pos_acc = FindKey(target, "POSITION");
                if (pos_acc == nullptr || pos_acc->type != JsonValue::Type::Number) {
                    continue;
                }
                const std::size_t pos_accessor =
                    static_cast<std::size_t>(static_cast<std::uint32_t>(pos_acc->number_value));
                BlendShapeFramePayload frame;
                frame.name = "target_" + std::to_string(target_i);
                if (target_i < target_names.size() && !target_names[target_i].empty()) {
                    frame.name = target_names[target_i];
                }
                if (target_i < target_default_weights.size()) {
                    frame.weight = target_default_weights[target_i];
                }
                if (!ExtractPositionDeltas(
                        bin_chunk.bytes, accessors, views, pos_accessor, &frame.delta_vertices, vtx_count, &read_error)) {
                    pkg.warnings.push_back("W_PAYLOAD: VRM_BLENDSHAPE_READ_FAILED: " + read_error);
                    continue;
                }
                blendshape_payload.frames.push_back(std::move(frame));
            }
        }
        if (!blendshape_payload.frames.empty()) {
            std::vector<std::string> frame_names;
            frame_names.reserve(blendshape_payload.frames.size());
            for (const auto& frame : blendshape_payload.frames) {
                frame_names.push_back(frame.name);
            }
            mesh_frame_names[mesh_name] = std::move(frame_names);
            pkg.blendshape_payloads.push_back(std::move(blendshape_payload));
        }

        pkg.mesh_payloads.push_back(std::move(mesh_payload));
        pkg.meshes.push_back(MeshAssetSummary{mesh_name, vtx_count, static_cast<std::uint32_t>(pkg.mesh_payloads.back().indices.size())});
        ++mesh_added;
    }

    pkg.parser_stage = "payload";
    if (pkg.mesh_payloads.empty()) {
        pkg.primary_error_code = "VRM_ASSET_MISSING";
        pkg.compat_level = AvatarCompatLevel::Failed;
        pkg.warnings.push_back("E_PAYLOAD: VRM_ASSET_MISSING: no mesh payload extracted.");
        pkg.missing_features.push_back("glTF mesh primitive extraction");
        return core::Result<AvatarPackage>::Ok(pkg);
    }

    if (pkg.materials.empty()) {
        pkg.materials.push_back({"Default", "MToon (minimal)"});
        MaterialRenderPayload default_material;
        default_material.name = "Default";
        default_material.shader_name = "MToon (minimal)";
        default_material.base_color_texture_name = "";
        default_material.alpha_mode = "OPAQUE";
        default_material.alpha_cutoff = 0.5f;
        default_material.double_sided = false;
        pkg.material_payloads.push_back(std::move(default_material));
        pkg.warnings.push_back("W_PARSE: VRM_MATERIAL_UNSUPPORTED: materials array missing or empty");
        ++unsupported_materials;
    }

    if (missing_texture_refs > 0U || unsupported_materials > 0U) {
        pkg.compat_level = AvatarCompatLevel::Partial;
        if (missing_texture_refs > 0U) {
            if (pkg.primary_error_code == "NONE") {
                pkg.primary_error_code = "VRM_TEXTURE_MISSING";
            }
            pkg.warnings.push_back(
                "W_PARSE: VRM_TEXTURE_MISSING: unresolved material texture refs=" + std::to_string(missing_texture_refs));
        }
        if (unsupported_materials > 0U) {
            if (pkg.primary_error_code == "NONE") {
                pkg.primary_error_code = "VRM_MATERIAL_UNSUPPORTED";
            }
            pkg.warnings.push_back(
                "W_PARSE: VRM_MATERIAL_UNSUPPORTED: unsupported material entries=" + std::to_string(unsupported_materials));
        }
    }

    ParseVrmExpressionEntries(
        root,
        pkg.blendshape_payloads,
        node_to_mesh_index,
        mesh_names_by_index,
        mesh_frame_names,
        &pkg.expressions);
    if (!pkg.expressions.empty()) {
        pkg.warnings.push_back("W_EXPRESSION: extracted expression entries=" + std::to_string(pkg.expressions.size()));
    } else {
        AddExpressionIfMissing(&pkg.expressions, "blink", "blink");
        AddExpressionIfMissing(&pkg.expressions, "aa", "viseme_aa");
        AddExpressionIfMissing(&pkg.expressions, "joy", "joy");
        pkg.warnings.push_back(
            "W_VRM_EXPRESSION_FALLBACK: no expression entries found, injected default blink/aa/joy mappings");
    }

    ParseSpringBoneSummary(root, &pkg.springbone_summary);
    if (pkg.springbone_summary.present) {
        pkg.warnings.push_back(
            "W_SPRINGBONE: parsed springbone metadata spring=" + std::to_string(pkg.springbone_summary.spring_count) +
            ", joint=" + std::to_string(pkg.springbone_summary.joint_count) +
            ", collider=" + std::to_string(pkg.springbone_summary.collider_count));
        pkg.missing_features.push_back("SpringBone runtime simulation");
    } else {
        pkg.missing_features.push_back("SpringBone metadata");
    }
    pkg.missing_features.push_back("MToon advanced parameter binding");

    pkg.parser_stage = "runtime-ready";
    if (pkg.compat_level != AvatarCompatLevel::Partial) {
        pkg.compat_level = AvatarCompatLevel::Full;
        pkg.primary_error_code = "NONE";
    }
    pkg.warnings.push_back("W_STAGE: runtime-ready");
    return core::Result<AvatarPackage>::Ok(pkg);
}

}  // namespace vsfclone::avatar
