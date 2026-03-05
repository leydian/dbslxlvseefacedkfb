#include "vrm_loader.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
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

std::string NormalizeAlphaMode(const std::string& raw_alpha_mode) {
    const auto mode = ToLower(raw_alpha_mode);
    if (mode == "mask") {
        return "MASK";
    }
    if (mode == "blend") {
        return "BLEND";
    }
    return "OPAQUE";
}

const JsonValue* FindKey(const JsonValue& root, const std::string& key);
bool TryGetString(const JsonValue& root, const std::string& key, std::string* out);
bool TryGetNumber(const JsonValue& root, const std::string& key, double* out);
bool TryGetBool(const JsonValue& root, const std::string& key, bool* out);

float Clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

bool TryGetBoolLike(const JsonValue& root, const std::string& key, bool* out) {
    if (out == nullptr) {
        return false;
    }
    if (TryGetBool(root, key, out)) {
        return true;
    }
    double n = 0.0;
    if (TryGetNumber(root, key, &n)) {
        *out = n > 0.5;
        return true;
    }
    std::string s;
    if (TryGetString(root, key, &s)) {
        const auto v = ToLower(s);
        if (v == "true" || v == "on" || v == "1" || v == "yes") {
            *out = true;
            return true;
        }
        if (v == "false" || v == "off" || v == "0" || v == "no") {
            *out = false;
            return true;
        }
    }
    return false;
}

std::string ResolveAlphaModeFromModeValue(double mode_number) {
    const int mode = static_cast<int>(mode_number + 0.5);
    if (mode == 1) {
        return "MASK";
    }
    if (mode == 2 || mode == 3) {
        return "BLEND";
    }
    return "OPAQUE";
}

bool TryResolveAlphaModeFromString(const std::string& raw, std::string* out_mode) {
    if (out_mode == nullptr) {
        return false;
    }
    const auto v = ToLower(raw);
    if (v.find("blend") != std::string::npos || v.find("transparent") != std::string::npos) {
        *out_mode = "BLEND";
        return true;
    }
    if (v.find("mask") != std::string::npos || v.find("cutout") != std::string::npos || v.find("clip") != std::string::npos) {
        *out_mode = "MASK";
        return true;
    }
    if (v.find("opaque") != std::string::npos) {
        *out_mode = "OPAQUE";
        return true;
    }
    return false;
}

void TryApplyAlphaModeHint(
    const std::string& candidate_mode,
    const std::string& source,
    std::string* io_alpha_mode,
    std::string* io_alpha_source) {
    if (io_alpha_mode == nullptr || io_alpha_source == nullptr || candidate_mode.empty()) {
        return;
    }
    const std::string normalized = NormalizeAlphaMode(candidate_mode);
    if (normalized == "OPAQUE") {
        return;
    }
    if (*io_alpha_mode == "OPAQUE") {
        *io_alpha_mode = normalized;
        *io_alpha_source = source;
    }
}

void ApplyAlphaHintsFromObject(
    const JsonValue& object_root,
    const std::string& source_prefix,
    std::string* io_alpha_mode,
    std::string* io_alpha_source,
    float* io_alpha_cutoff,
    int depth = 0) {
    if (object_root.type != JsonValue::Type::Object || io_alpha_mode == nullptr || io_alpha_source == nullptr || io_alpha_cutoff == nullptr) {
        return;
    }
    if (depth > 2) {
        return;
    }

    bool b = false;
    double n = 0.0;
    std::string s;

    if (TryGetBoolLike(object_root, "transparentWithZWrite", &b) && b) {
        TryApplyAlphaModeHint("BLEND", source_prefix + ".transparentWithZWrite", io_alpha_mode, io_alpha_source);
    }
    if (TryGetString(object_root, "renderMode", &s)) {
        std::string mode;
        if (TryResolveAlphaModeFromString(s, &mode)) {
            TryApplyAlphaModeHint(mode, source_prefix + ".renderMode", io_alpha_mode, io_alpha_source);
        }
    }
    if (TryGetNumber(object_root, "renderMode", &n)) {
        TryApplyAlphaModeHint(ResolveAlphaModeFromModeValue(n), source_prefix + ".renderMode", io_alpha_mode, io_alpha_source);
    }
    if (TryGetNumber(object_root, "_Mode", &n)) {
        TryApplyAlphaModeHint(ResolveAlphaModeFromModeValue(n), source_prefix + "._Mode", io_alpha_mode, io_alpha_source);
    }
    if (TryGetNumber(object_root, "_Surface", &n) && n >= 0.5) {
        TryApplyAlphaModeHint("BLEND", source_prefix + "._Surface", io_alpha_mode, io_alpha_source);
    }
    if (TryGetNumber(object_root, "renderQueue", &n)) {
        if (n >= 3000.0) {
            TryApplyAlphaModeHint("BLEND", source_prefix + ".renderQueue", io_alpha_mode, io_alpha_source);
        } else if (n >= 2450.0) {
            TryApplyAlphaModeHint("MASK", source_prefix + ".renderQueue", io_alpha_mode, io_alpha_source);
        }
    }
    if (TryGetBoolLike(object_root, "_ALPHABLEND_ON", &b) && b) {
        TryApplyAlphaModeHint("BLEND", source_prefix + "._ALPHABLEND_ON", io_alpha_mode, io_alpha_source);
    }
    if (TryGetBoolLike(object_root, "_ALPHATEST_ON", &b) && b) {
        TryApplyAlphaModeHint("MASK", source_prefix + "._ALPHATEST_ON", io_alpha_mode, io_alpha_source);
    }
    const bool has_alpha_clip =
        (TryGetBoolLike(object_root, "_AlphaClip", &b) && b) ||
        (TryGetBoolLike(object_root, "_UseAlphaClipping", &b) && b);
    if (has_alpha_clip) {
        TryApplyAlphaModeHint("MASK", source_prefix + "._AlphaClip", io_alpha_mode, io_alpha_source);
    }
    if (TryGetNumber(object_root, "_Cutoff", &n) && std::isfinite(n)) {
        const float cutoff = Clamp01(static_cast<float>(n));
        if (*io_alpha_mode == "MASK" || has_alpha_clip) {
            *io_alpha_cutoff = cutoff;
        }
    }

    const auto* keyword_map = FindKey(object_root, "keywordMap");
    if (keyword_map != nullptr) {
        ApplyAlphaHintsFromObject(
            *keyword_map,
            source_prefix + ".keywordMap",
            io_alpha_mode,
            io_alpha_source,
            io_alpha_cutoff,
            depth + 1);
    }
    const auto* float_properties = FindKey(object_root, "floatProperties");
    if (float_properties != nullptr) {
        ApplyAlphaHintsFromObject(
            *float_properties,
            source_prefix + ".floatProperties",
            io_alpha_mode,
            io_alpha_source,
            io_alpha_cutoff,
            depth + 1);
    }
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

bool TryGetNumberArray(const JsonValue& root, const std::string& key, std::size_t expected_count, std::vector<float>* out) {
    if (out == nullptr) {
        return false;
    }
    const auto* v = FindKey(root, key);
    if (v == nullptr || v->type != JsonValue::Type::Array || v->array_value.size() < expected_count) {
        return false;
    }
    out->clear();
    out->reserve(expected_count);
    for (std::size_t i = 0U; i < expected_count; ++i) {
        if (v->array_value[i].type != JsonValue::Type::Number) {
            return false;
        }
        out->push_back(static_cast<float>(v->array_value[i].number_value));
    }
    return true;
}

std::array<float, 16U> MakeIdentityMatrix4x4() {
    return {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
}

std::array<float, 16U> MulMatrix4x4(
    const std::array<float, 16U>& a,
    const std::array<float, 16U>& b) {
    std::array<float, 16U> out {};
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            out[static_cast<std::size_t>(c) * 4U + static_cast<std::size_t>(r)] =
                (a[0U * 4U + static_cast<std::size_t>(r)] * b[static_cast<std::size_t>(c) * 4U + 0U]) +
                (a[1U * 4U + static_cast<std::size_t>(r)] * b[static_cast<std::size_t>(c) * 4U + 1U]) +
                (a[2U * 4U + static_cast<std::size_t>(r)] * b[static_cast<std::size_t>(c) * 4U + 2U]) +
                (a[3U * 4U + static_cast<std::size_t>(r)] * b[static_cast<std::size_t>(c) * 4U + 3U]);
        }
    }
    return out;
}

bool TryInvertMatrix4x4(
    const std::array<float, 16U>& m,
    std::array<float, 16U>* out_inv) {
    if (out_inv == nullptr) {
        return false;
    }
    std::array<float, 16U> inv {};
    inv[0] = m[5] * m[10] * m[15] -
             m[5] * m[11] * m[14] -
             m[9] * m[6] * m[15] +
             m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] -
             m[13] * m[7] * m[10];

    inv[4] = -m[4] * m[10] * m[15] +
             m[4] * m[11] * m[14] +
             m[8] * m[6] * m[15] -
             m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] +
             m[12] * m[7] * m[10];

    inv[8] = m[4] * m[9] * m[15] -
             m[4] * m[11] * m[13] -
             m[8] * m[5] * m[15] +
             m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] -
             m[12] * m[7] * m[9];

    inv[12] = -m[4] * m[9] * m[14] +
              m[4] * m[10] * m[13] +
              m[8] * m[5] * m[14] -
              m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] +
              m[12] * m[6] * m[9];

    inv[1] = -m[1] * m[10] * m[15] +
             m[1] * m[11] * m[14] +
             m[9] * m[2] * m[15] -
             m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] +
             m[13] * m[3] * m[10];

    inv[5] = m[0] * m[10] * m[15] -
             m[0] * m[11] * m[14] -
             m[8] * m[2] * m[15] +
             m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] -
             m[12] * m[3] * m[10];

    inv[9] = -m[0] * m[9] * m[15] +
             m[0] * m[11] * m[13] +
             m[8] * m[1] * m[15] -
             m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] +
             m[12] * m[3] * m[9];

    inv[13] = m[0] * m[9] * m[14] -
              m[0] * m[10] * m[13] -
              m[8] * m[1] * m[14] +
              m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] -
              m[12] * m[2] * m[9];

    inv[2] = m[1] * m[6] * m[15] -
             m[1] * m[7] * m[14] -
             m[5] * m[2] * m[15] +
             m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] -
             m[13] * m[3] * m[6];

    inv[6] = -m[0] * m[6] * m[15] +
             m[0] * m[7] * m[14] +
             m[4] * m[2] * m[15] -
             m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] +
             m[12] * m[3] * m[6];

    inv[10] = m[0] * m[5] * m[15] -
              m[0] * m[7] * m[13] -
              m[4] * m[1] * m[15] +
              m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] -
              m[12] * m[3] * m[5];

    inv[14] = -m[0] * m[5] * m[14] +
              m[0] * m[6] * m[13] +
              m[4] * m[1] * m[14] -
              m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] +
              m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] +
             m[1] * m[7] * m[10] +
             m[5] * m[2] * m[11] -
             m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] +
             m[9] * m[3] * m[6];

    inv[7] = m[0] * m[6] * m[11] -
             m[0] * m[7] * m[10] -
             m[4] * m[2] * m[11] +
             m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] -
             m[8] * m[3] * m[6];

    inv[11] = -m[0] * m[5] * m[11] +
              m[0] * m[7] * m[9] +
              m[4] * m[1] * m[11] -
              m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] +
              m[8] * m[3] * m[5];

    inv[15] = m[0] * m[5] * m[10] -
              m[0] * m[6] * m[9] -
              m[4] * m[1] * m[10] +
              m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];

    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (!std::isfinite(det) || std::abs(det) <= 1e-8f) {
        return false;
    }
    const float inv_det = 1.0f / det;
    for (std::size_t i = 0U; i < 16U; ++i) {
        (*out_inv)[i] = inv[i] * inv_det;
    }
    return true;
}

bool AreMatrix4x4NearlyEqual(
    const std::array<float, 16U>& a,
    const std::array<float, 16U>& b,
    float epsilon = 1e-5f) {
    for (std::size_t i = 0U; i < 16U; ++i) {
        if (std::abs(a[i] - b[i]) > epsilon) {
            return false;
        }
    }
    return true;
}

bool IsIdentityMatrix4x4(const std::array<float, 16U>& m, float epsilon = 1e-5f) {
    return AreMatrix4x4NearlyEqual(m, MakeIdentityMatrix4x4(), epsilon);
}

bool TryBuildNodeTransformMatrix(const JsonValue& node, std::array<float, 16U>* out_matrix, bool* out_non_identity) {
    if (out_matrix == nullptr || out_non_identity == nullptr) {
        return false;
    }
    *out_matrix = MakeIdentityMatrix4x4();
    *out_non_identity = false;
    if (node.type != JsonValue::Type::Object) {
        return false;
    }

    const auto* matrix_v = FindKey(node, "matrix");
    if (matrix_v != nullptr && matrix_v->type == JsonValue::Type::Array && matrix_v->array_value.size() >= 16U) {
        for (std::size_t i = 0U; i < 16U; ++i) {
            if (matrix_v->array_value[i].type != JsonValue::Type::Number) {
                return false;
            }
            (*out_matrix)[i] = static_cast<float>(matrix_v->array_value[i].number_value);
        }
        for (std::size_t i = 0U; i < 16U; ++i) {
            const float expected = (i % 5U) == 0U ? 1.0f : 0.0f;
            if (std::abs((*out_matrix)[i] - expected) > 1e-5f) {
                *out_non_identity = true;
                break;
            }
        }
        return true;
    }

    std::array<float, 3U> t = {0.0f, 0.0f, 0.0f};
    std::array<float, 3U> s = {1.0f, 1.0f, 1.0f};
    std::array<float, 4U> q = {0.0f, 0.0f, 0.0f, 1.0f};
    std::vector<float> arr;
    if (TryGetNumberArray(node, "translation", 3U, &arr)) {
        t = {arr[0], arr[1], arr[2]};
    }
    if (TryGetNumberArray(node, "scale", 3U, &arr)) {
        s = {arr[0], arr[1], arr[2]};
    }
    if (TryGetNumberArray(node, "rotation", 4U, &arr)) {
        q = {arr[0], arr[1], arr[2], arr[3]};
    }

    const float q_len = std::sqrt((q[0] * q[0]) + (q[1] * q[1]) + (q[2] * q[2]) + (q[3] * q[3]));
    if (q_len > 1e-6f && std::isfinite(q_len)) {
        const float inv = 1.0f / q_len;
        q[0] *= inv;
        q[1] *= inv;
        q[2] *= inv;
        q[3] *= inv;
    } else {
        q = {0.0f, 0.0f, 0.0f, 1.0f};
    }

    const float x = q[0];
    const float y = q[1];
    const float z = q[2];
    const float w = q[3];
    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;

    std::array<float, 16U> rot = {
        1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz),        2.0f * (xz - wy),        0.0f,
        2.0f * (xy - wz),        1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx),        0.0f,
        2.0f * (xz + wy),        2.0f * (yz - wx),        1.0f - 2.0f * (xx + yy), 0.0f,
        0.0f,                    0.0f,                    0.0f,                    1.0f};
    std::array<float, 16U> scale = {
        s[0], 0.0f, 0.0f, 0.0f,
        0.0f, s[1], 0.0f, 0.0f,
        0.0f, 0.0f, s[2], 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 16U> trans = MakeIdentityMatrix4x4();
    trans[12] = t[0];
    trans[13] = t[1];
    trans[14] = t[2];

    *out_matrix = MulMatrix4x4(MulMatrix4x4(trans, rot), scale);
    *out_non_identity =
        std::abs(t[0]) > 1e-6f || std::abs(t[1]) > 1e-6f || std::abs(t[2]) > 1e-6f ||
        std::abs(s[0] - 1.0f) > 1e-6f || std::abs(s[1] - 1.0f) > 1e-6f || std::abs(s[2] - 1.0f) > 1e-6f ||
        std::abs(q[0]) > 1e-6f || std::abs(q[1]) > 1e-6f || std::abs(q[2]) > 1e-6f || std::abs(q[3] - 1.0f) > 1e-6f;
    return true;
}

bool ApplyPositionTransformToVertexBlob(
    std::vector<std::uint8_t>* vertex_blob,
    std::uint32_t vertex_stride,
    const std::array<float, 16U>& m) {
    if (vertex_blob == nullptr || vertex_blob->empty() || vertex_stride < 12U) {
        return false;
    }
    if ((vertex_blob->size() % vertex_stride) != 0U) {
        return false;
    }
    for (const auto v : m) {
        if (!std::isfinite(v) || std::abs(v) > 1.0e6f) {
            return false;
        }
    }
    const std::size_t vertex_count = vertex_blob->size() / static_cast<std::size_t>(vertex_stride);
    std::vector<std::uint8_t> transformed = *vertex_blob;
    for (std::size_t i = 0U; i < vertex_count; ++i) {
        const std::size_t base = i * static_cast<std::size_t>(vertex_stride);
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        std::memcpy(&x, vertex_blob->data() + base, sizeof(float));
        std::memcpy(&y, vertex_blob->data() + base + 4U, sizeof(float));
        std::memcpy(&z, vertex_blob->data() + base + 8U, sizeof(float));
        const float tx = (m[0] * x) + (m[4] * y) + (m[8] * z) + m[12];
        const float ty = (m[1] * x) + (m[5] * y) + (m[9] * z) + m[13];
        const float tz = (m[2] * x) + (m[6] * y) + (m[10] * z) + m[14];
        if (!std::isfinite(tx) || !std::isfinite(ty) || !std::isfinite(tz) ||
            std::abs(tx) > 1.0e6f || std::abs(ty) > 1.0e6f || std::abs(tz) > 1.0e6f) {
            return false;
        }
        std::memcpy(transformed.data() + base, &tx, sizeof(float));
        std::memcpy(transformed.data() + base + 4U, &ty, sizeof(float));
        std::memcpy(transformed.data() + base + 8U, &tz, sizeof(float));
    }
    *vertex_blob = std::move(transformed);
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

std::uint32_t ReadU32Be(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3U]);
}

bool BytesStartWith(const std::vector<std::uint8_t>& bytes, std::initializer_list<std::uint8_t> prefix) {
    if (bytes.size() < prefix.size()) {
        return false;
    }
    std::size_t index = 0U;
    for (const auto value : prefix) {
        if (bytes[index] != value) {
            return false;
        }
        ++index;
    }
    return true;
}

bool IsPngWithAlphaCapability(const std::vector<std::uint8_t>& bytes) {
    if (!BytesStartWith(bytes, {0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU})) {
        return false;
    }
    if (bytes.size() < 33U) {
        return false;
    }
    const auto ihdr_len = ReadU32Be(bytes, 8U);
    if (ihdr_len < 13U || !BytesStartWith(
                              std::vector<std::uint8_t>(bytes.begin() + 12U, bytes.begin() + 16U),
                              {0x49U, 0x48U, 0x44U, 0x52U})) {
        return false;
    }
    // PNG color type 4(gray+alpha) / 6(RGBA) always carry alpha.
    const std::uint8_t color_type = bytes[25U];
    if (color_type == 4U || color_type == 6U) {
        return true;
    }
    // Indexed/truecolor PNG can carry transparency via tRNS.
    std::size_t cursor = 8U;
    while (cursor + 8U <= bytes.size()) {
        const auto chunk_len = ReadU32Be(bytes, cursor);
        if (cursor + 12U + static_cast<std::size_t>(chunk_len) > bytes.size()) {
            return false;
        }
        const auto type_offset = cursor + 4U;
        if (bytes[type_offset] == 0x74U &&
            bytes[type_offset + 1U] == 0x52U &&
            bytes[type_offset + 2U] == 0x4EU &&
            bytes[type_offset + 3U] == 0x53U) {
            return true;
        }
        if (bytes[type_offset] == 0x49U &&
            bytes[type_offset + 1U] == 0x45U &&
            bytes[type_offset + 2U] == 0x4EU &&
            bytes[type_offset + 3U] == 0x44U) {
            break;
        }
        cursor += 12U + static_cast<std::size_t>(chunk_len);
    }
    return false;
}

bool TextureHasAlphaCapability(const std::string& format, const std::vector<std::uint8_t>& bytes) {
    const auto lower = ToLower(format);
    if (lower == "png") {
        return IsPngWithAlphaCapability(bytes);
    }
    return false;
}

bool MaterialNameSuggestsBlend(const std::string& material_name) {
    const auto lower = ToLower(material_name);
    static const std::array<std::string, 9U> kBlendHintTokens = {
        "hologram",
        "effect",
        "glass",
        "transparent",
        "trans",
        "alpha",
        "ghost",
        "clear",
        "fx"};
    for (const auto& token : kBlendHintTokens) {
        if (lower.find(token) != std::string::npos) {
            return true;
        }
    }
    return false;
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

std::uint16_t ReadU16Le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

std::int16_t ReadI16Le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    const auto u = ReadU16Le(bytes, offset);
    std::int16_t v = 0;
    std::memcpy(&v, &u, sizeof(v));
    return v;
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

std::size_t GetTypeComponentCount(const std::string& type_name) {
    if (type_name == "SCALAR") {
        return 1U;
    }
    if (type_name == "VEC2") {
        return 2U;
    }
    if (type_name == "VEC3") {
        return 3U;
    }
    if (type_name == "VEC4") {
        return 4U;
    }
    if (type_name == "MAT4") {
        return 16U;
    }
    return 0U;
}

std::size_t GetComponentByteSize(std::uint32_t component_type) {
    switch (component_type) {
        case 5120U:  // BYTE
        case 5121U:  // UBYTE
            return 1U;
        case 5122U:  // SHORT
        case 5123U:  // USHORT
            return 2U;
        case 5125U:  // UINT
        case 5126U:  // FLOAT
            return 4U;
        default:
            return 0U;
    }
}

bool ResolveAccessorDataRange(
    const std::vector<std::uint8_t>& bin,
    const std::vector<AccessorMeta>& accessors,
    const std::vector<BufferViewMeta>& views,
    std::size_t accessor_index,
    std::size_t* out_start,
    std::size_t* out_stride,
    std::size_t* out_component_size,
    std::size_t* out_component_count,
    std::string* out_error) {
    if (out_start == nullptr || out_stride == nullptr || out_component_size == nullptr || out_component_count == nullptr) {
        return false;
    }
    if (accessor_index >= accessors.size()) {
        if (out_error != nullptr) {
            *out_error = "accessor index out of range";
        }
        return false;
    }
    const auto& a = accessors[accessor_index];
    if (a.buffer_view >= views.size()) {
        if (out_error != nullptr) {
            *out_error = "accessor bufferView index out of range";
        }
        return false;
    }
    const std::size_t component_count = GetTypeComponentCount(a.type);
    const std::size_t component_size = GetComponentByteSize(a.component_type);
    if (component_count == 0U || component_size == 0U) {
        if (out_error != nullptr) {
            *out_error = "unsupported accessor type/componentType";
        }
        return false;
    }
    const auto& bv = views[a.buffer_view];
    const std::size_t start = static_cast<std::size_t>(bv.byte_offset) + static_cast<std::size_t>(a.byte_offset);
    const std::size_t packed_stride = component_count * component_size;
    const std::size_t stride = bv.byte_stride > 0U ? bv.byte_stride : packed_stride;
    if (stride < packed_stride) {
        if (out_error != nullptr) {
            *out_error = "bufferView stride smaller than packed accessor element size";
        }
        return false;
    }
    if (a.count == 0U) {
        if (out_error != nullptr) {
            *out_error = "accessor count is zero";
        }
        return false;
    }
    const std::size_t needed = start + (static_cast<std::size_t>(a.count) - 1U) * stride + packed_stride;
    if (needed > bin.size()) {
        if (out_error != nullptr) {
            *out_error = "accessor data out of BIN bounds";
        }
        return false;
    }
    *out_start = start;
    *out_stride = stride;
    *out_component_size = component_size;
    *out_component_count = component_count;
    return true;
}

bool ReadAccessorComponentAsFloat(
    const std::vector<std::uint8_t>& bin,
    std::size_t offset,
    std::uint32_t component_type,
    bool normalized,
    float* out_value) {
    if (out_value == nullptr) {
        return false;
    }
    switch (component_type) {
        case 5120U: {  // BYTE
            const std::int8_t raw = static_cast<std::int8_t>(bin[offset]);
            *out_value = normalized
                ? std::max(-1.0f, static_cast<float>(raw) / 127.0f)
                : static_cast<float>(raw);
            return true;
        }
        case 5121U: {  // UBYTE
            const std::uint8_t raw = bin[offset];
            *out_value = normalized ? static_cast<float>(raw) / 255.0f : static_cast<float>(raw);
            return true;
        }
        case 5122U: {  // SHORT
            const std::int16_t raw = ReadI16Le(bin, offset);
            *out_value = normalized
                ? std::max(-1.0f, static_cast<float>(raw) / 32767.0f)
                : static_cast<float>(raw);
            return true;
        }
        case 5123U: {  // USHORT
            const std::uint16_t raw = ReadU16Le(bin, offset);
            *out_value = normalized ? static_cast<float>(raw) / 65535.0f : static_cast<float>(raw);
            return true;
        }
        case 5125U: {  // UINT
            const std::uint32_t raw = ReadU32Le(bin, offset);
            *out_value = static_cast<float>(raw);
            return true;
        }
        case 5126U: {  // FLOAT
            *out_value = ReadF32Le(bin, offset);
            return true;
        }
        default:
            return false;
    }
}

bool ReadAccessorComponentAsInt(
    const std::vector<std::uint8_t>& bin,
    std::size_t offset,
    std::uint32_t component_type,
    std::int32_t* out_value) {
    if (out_value == nullptr) {
        return false;
    }
    switch (component_type) {
        case 5120U:  // BYTE
            *out_value = static_cast<std::int8_t>(bin[offset]);
            return true;
        case 5121U:  // UBYTE
            *out_value = static_cast<std::int32_t>(bin[offset]);
            return true;
        case 5122U:  // SHORT
            *out_value = static_cast<std::int32_t>(ReadI16Le(bin, offset));
            return true;
        case 5123U:  // USHORT
            *out_value = static_cast<std::int32_t>(ReadU16Le(bin, offset));
            return true;
        case 5125U: {  // UINT
            const auto v = ReadU32Le(bin, offset);
            if (v > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
                return false;
            }
            *out_value = static_cast<std::int32_t>(v);
            return true;
        }
        default:
            return false;
    }
}

bool ExtractMat4FloatAccessor(
    const std::vector<std::uint8_t>& bin,
    const std::vector<AccessorMeta>& accessors,
    const std::vector<BufferViewMeta>& views,
    std::size_t accessor_index,
    std::vector<float>* out_values,
    std::string* out_error) {
    if (out_values == nullptr) {
        return false;
    }
    if (accessor_index >= accessors.size()) {
        if (out_error != nullptr) {
            *out_error = "MAT4 accessor index out of range";
        }
        return false;
    }
    const auto& a = accessors[accessor_index];
    if (a.component_type != 5126U || a.type != "MAT4") {
        if (out_error != nullptr) {
            *out_error = "inverseBindMatrices accessor must be FLOAT MAT4";
        }
        return false;
    }
    std::size_t start = 0U;
    std::size_t stride = 0U;
    std::size_t component_size = 0U;
    std::size_t component_count = 0U;
    if (!ResolveAccessorDataRange(
            bin,
            accessors,
            views,
            accessor_index,
            &start,
            &stride,
            &component_size,
            &component_count,
            out_error)) {
        return false;
    }
    if (component_size != 4U || component_count != 16U) {
        if (out_error != nullptr) {
            *out_error = "invalid MAT4 accessor component layout";
        }
        return false;
    }
    out_values->clear();
    out_values->reserve(static_cast<std::size_t>(a.count) * 16U);
    for (std::uint32_t i = 0U; i < a.count; ++i) {
        const std::size_t base = start + static_cast<std::size_t>(i) * stride;
        for (std::size_t c = 0U; c < 16U; ++c) {
            out_values->push_back(ReadF32Le(bin, base + c * 4U));
        }
    }
    return true;
}

bool ExtractSkinWeightBlob(
    const std::vector<std::uint8_t>& bin,
    const std::vector<AccessorMeta>& accessors,
    const std::vector<BufferViewMeta>& views,
    std::size_t joints_accessor_index,
    std::size_t weights_accessor_index,
    std::uint32_t expected_vertex_count,
    std::vector<std::uint8_t>* out_weight_blob,
    std::string* out_error) {
    if (out_weight_blob == nullptr) {
        return false;
    }
    if (joints_accessor_index >= accessors.size() || weights_accessor_index >= accessors.size()) {
        if (out_error != nullptr) {
            *out_error = "skin accessor index out of range";
        }
        return false;
    }
    const auto& joints_accessor = accessors[joints_accessor_index];
    const auto& weights_accessor = accessors[weights_accessor_index];
    if (joints_accessor.type != "VEC4") {
        if (out_error != nullptr) {
            *out_error = "JOINTS_0 accessor must be VEC4";
        }
        return false;
    }
    if (weights_accessor.type != "VEC4") {
        if (out_error != nullptr) {
            *out_error = "WEIGHTS_0 accessor must be VEC4";
        }
        return false;
    }
    if (joints_accessor.count != expected_vertex_count || weights_accessor.count != expected_vertex_count) {
        if (out_error != nullptr) {
            *out_error = "skin accessor count mismatch with vertex count";
        }
        return false;
    }

    std::size_t joints_start = 0U;
    std::size_t joints_stride = 0U;
    std::size_t joints_comp_size = 0U;
    std::size_t joints_comp_count = 0U;
    if (!ResolveAccessorDataRange(
            bin,
            accessors,
            views,
            joints_accessor_index,
            &joints_start,
            &joints_stride,
            &joints_comp_size,
            &joints_comp_count,
            out_error)) {
        return false;
    }
    std::size_t weights_start = 0U;
    std::size_t weights_stride = 0U;
    std::size_t weights_comp_size = 0U;
    std::size_t weights_comp_count = 0U;
    if (!ResolveAccessorDataRange(
            bin,
            accessors,
            views,
            weights_accessor_index,
            &weights_start,
            &weights_stride,
            &weights_comp_size,
            &weights_comp_count,
            out_error)) {
        return false;
    }
    if (joints_comp_count != 4U || weights_comp_count != 4U) {
        if (out_error != nullptr) {
            *out_error = "skin accessor must contain exactly four components";
        }
        return false;
    }
    if (!(joints_accessor.component_type == 5121U || joints_accessor.component_type == 5123U ||
          joints_accessor.component_type == 5125U || joints_accessor.component_type == 5120U ||
          joints_accessor.component_type == 5122U)) {
        if (out_error != nullptr) {
            *out_error = "unsupported JOINTS_0 componentType";
        }
        return false;
    }
    if (!(weights_accessor.component_type == 5126U || weights_accessor.component_type == 5121U ||
          weights_accessor.component_type == 5123U || weights_accessor.component_type == 5120U ||
          weights_accessor.component_type == 5122U || weights_accessor.component_type == 5125U)) {
        if (out_error != nullptr) {
            *out_error = "unsupported WEIGHTS_0 componentType";
        }
        return false;
    }

    out_weight_blob->clear();
    out_weight_blob->reserve(static_cast<std::size_t>(expected_vertex_count) * 32U);
    const bool weights_normalized = (weights_accessor.component_type != 5126U);
    for (std::uint32_t i = 0U; i < expected_vertex_count; ++i) {
        const std::size_t joints_base = joints_start + static_cast<std::size_t>(i) * joints_stride;
        const std::size_t weights_base = weights_start + static_cast<std::size_t>(i) * weights_stride;
        std::array<std::int32_t, 4U> joint_indices = {0, 0, 0, 0};
        std::array<float, 4U> weights = {0.0f, 0.0f, 0.0f, 0.0f};
        for (std::size_t c = 0U; c < 4U; ++c) {
            const std::size_t joint_off = joints_base + c * joints_comp_size;
            const std::size_t weight_off = weights_base + c * weights_comp_size;
            if (!ReadAccessorComponentAsInt(bin, joint_off, joints_accessor.component_type, &joint_indices[c])) {
                if (out_error != nullptr) {
                    *out_error = "failed to decode JOINTS_0 component";
                }
                return false;
            }
            if (!ReadAccessorComponentAsFloat(
                    bin,
                    weight_off,
                    weights_accessor.component_type,
                    weights_normalized,
                    &weights[c])) {
                if (out_error != nullptr) {
                    *out_error = "failed to decode WEIGHTS_0 component";
                }
                return false;
            }
            if (!std::isfinite(weights[c])) {
                weights[c] = 0.0f;
            }
            if (weights[c] < 0.0f) {
                weights[c] = 0.0f;
            }
        }
        const auto* joint_bytes = reinterpret_cast<const std::uint8_t*>(joint_indices.data());
        const auto* weight_bytes = reinterpret_cast<const std::uint8_t*>(weights.data());
        out_weight_blob->insert(out_weight_blob->end(), joint_bytes, joint_bytes + sizeof(joint_indices));
        out_weight_blob->insert(out_weight_blob->end(), weight_bytes, weight_bytes + sizeof(weights));
    }
    return true;
}

std::array<float, 16U> ReadNodeLocalMatrixOrIdentity(const JsonValue& node) {
    std::array<float, 16U> out = MakeIdentityMatrix4x4();
    bool non_identity = false;
    if (TryBuildNodeTransformMatrix(node, &out, &non_identity)) {
        return out;
    }
    return MakeIdentityMatrix4x4();
}

void BuildNodeGlobalTransforms(
    const JsonValue* nodes_v,
    std::vector<std::array<float, 16U>>* out_global,
    std::vector<std::size_t>* out_parent) {
    if (out_global == nullptr || out_parent == nullptr) {
        return;
    }
    out_global->clear();
    out_parent->clear();
    if (nodes_v == nullptr || nodes_v->type != JsonValue::Type::Array) {
        return;
    }
    const std::size_t node_count = nodes_v->array_value.size();
    out_global->assign(node_count, MakeIdentityMatrix4x4());
    out_parent->assign(node_count, std::numeric_limits<std::size_t>::max());
    std::vector<std::array<float, 16U>> locals(node_count, MakeIdentityMatrix4x4());
    std::vector<std::vector<std::size_t>> children(node_count);

    for (std::size_t node_i = 0U; node_i < node_count; ++node_i) {
        const auto& node = nodes_v->array_value[node_i];
        if (node.type != JsonValue::Type::Object) {
            continue;
        }
        locals[node_i] = ReadNodeLocalMatrixOrIdentity(node);
        const auto* children_v = FindKey(node, "children");
        if (children_v == nullptr || children_v->type != JsonValue::Type::Array) {
            continue;
        }
        for (const auto& child : children_v->array_value) {
            if (child.type != JsonValue::Type::Number) {
                continue;
            }
            const double n = child.number_value;
            if (n < 0.0 || n > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
                continue;
            }
            const auto child_index = static_cast<std::size_t>(static_cast<std::uint32_t>(n));
            if (child_index >= node_count || child_index == node_i) {
                continue;
            }
            children[node_i].push_back(child_index);
            if ((*out_parent)[child_index] == std::numeric_limits<std::size_t>::max()) {
                (*out_parent)[child_index] = node_i;
            }
        }
    }

    std::vector<std::size_t> stack;
    stack.reserve(node_count);
    std::vector<std::array<float, 16U>> stack_parent_world;
    stack_parent_world.reserve(node_count);
    std::vector<bool> visited(node_count, false);

    for (std::size_t node_i = 0U; node_i < node_count; ++node_i) {
        if ((*out_parent)[node_i] != std::numeric_limits<std::size_t>::max()) {
            continue;
        }
        stack.push_back(node_i);
        stack_parent_world.push_back(MakeIdentityMatrix4x4());
        while (!stack.empty()) {
            const auto current = stack.back();
            const auto parent_world = stack_parent_world.back();
            stack.pop_back();
            stack_parent_world.pop_back();
            if (current >= node_count) {
                continue;
            }
            (*out_global)[current] = MulMatrix4x4(parent_world, locals[current]);
            visited[current] = true;
            const auto current_world = (*out_global)[current];
            for (const auto child : children[current]) {
                stack.push_back(child);
                stack_parent_world.push_back(current_world);
            }
        }
    }

    // Handle disconnected/cyclic nodes defensively.
    for (std::size_t node_i = 0U; node_i < node_count; ++node_i) {
        if (visited[node_i]) {
            continue;
        }
        std::size_t cur = node_i;
        std::array<float, 16U> accum = MakeIdentityMatrix4x4();
        std::size_t hop = 0U;
        while (cur < node_count && hop <= node_count) {
            accum = MulMatrix4x4(locals[cur], accum);
            const auto parent = (*out_parent)[cur];
            if (parent == std::numeric_limits<std::size_t>::max() || parent >= node_count) {
                break;
            }
            cur = parent;
            ++hop;
        }
        (*out_global)[node_i] = accum;
        visited[node_i] = true;
    }
}

struct MaterialInfo {
    std::string name;
    std::string shader_name = "MToon (minimal)";
    std::string base_color_texture_name;
    std::string normal_texture_name;
    std::string emission_texture_name;
    std::string rim_texture_name;
    std::string matcap_texture_name;
    std::string uv_anim_mask_texture_name;
    bool base_color_texture_alpha_capable = false;
    bool double_sided = false;
    std::string alpha_mode = "OPAQUE";
    std::string alpha_source = "default.opaque";
    float alpha_cutoff = 0.5f;
    std::array<float, 4U> base_color = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4U> shade_color = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4U> emission_color = {0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 4U> rim_color = {0.0f, 0.0f, 0.0f, 1.0f};
    float bump_scale = 0.0f;
    float rim_power = 2.0f;
    float rim_lighting_mix = 0.0f;
    float matcap_strength = 0.0f;
    std::array<float, 4U> matcap_color = {0.0f, 0.0f, 0.0f, 1.0f};
    float outline_width = 0.0f;
    float outline_lighting_mix = 0.0f;
    float uv_anim_scroll_x = 0.0f;
    float uv_anim_scroll_y = 0.0f;
    float uv_anim_rotation = 0.0f;
    bool has_matcap_binding = false;
    bool matcap_declared = false;
    bool has_outline_binding = false;
    bool has_uv_anim_binding = false;
    bool has_mtoon_binding = false;
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

HumanoidBoneId ToHumanoidBoneId(std::string name) {
    name = ToLower(name);
    std::string key;
    key.reserve(name.size());
    for (const unsigned char c : name) {
        if (std::isalnum(c) != 0) {
            key.push_back(static_cast<char>(c));
        }
    }
    if (key == "hips") {
        return HumanoidBoneId::Hips;
    }
    if (key == "spine") {
        return HumanoidBoneId::Spine;
    }
    if (key == "chest") {
        return HumanoidBoneId::Chest;
    }
    if (key == "upperchest") {
        return HumanoidBoneId::UpperChest;
    }
    if (key == "neck") {
        return HumanoidBoneId::Neck;
    }
    if (key == "head") {
        return HumanoidBoneId::Head;
    }
    if (key == "leftupperarm" || key == "leftarm") {
        return HumanoidBoneId::LeftUpperArm;
    }
    if (key == "rightupperarm" || key == "rightarm") {
        return HumanoidBoneId::RightUpperArm;
    }
    if (key == "leftshoulder") {
        return HumanoidBoneId::LeftShoulder;
    }
    if (key == "rightshoulder") {
        return HumanoidBoneId::RightShoulder;
    }
    if (key == "leftlowerarm" || key == "leftforearm") {
        return HumanoidBoneId::LeftLowerArm;
    }
    if (key == "rightlowerarm" || key == "rightforearm") {
        return HumanoidBoneId::RightLowerArm;
    }
    if (key == "lefthand") {
        return HumanoidBoneId::LeftHand;
    }
    if (key == "righthand") {
        return HumanoidBoneId::RightHand;
    }
    return HumanoidBoneId::Unknown;
}

void ParseVrmHumanoidNodeMap(
    const JsonValue& root,
    std::unordered_map<std::size_t, HumanoidBoneId>* out_node_to_humanoid) {
    if (out_node_to_humanoid == nullptr) {
        return;
    }
    out_node_to_humanoid->clear();
    const auto* extensions = FindKey(root, "extensions");
    if (extensions == nullptr || extensions->type != JsonValue::Type::Object) {
        return;
    }
    const auto try_add = [&](const std::string& bone_name, const JsonValue& node_value) {
        if (node_value.type != JsonValue::Type::Number) {
            return;
        }
        const double n = node_value.number_value;
        if (n < 0.0 || n > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
            return;
        }
        const auto humanoid_id = ToHumanoidBoneId(bone_name);
        if (humanoid_id == HumanoidBoneId::Unknown) {
            return;
        }
        (*out_node_to_humanoid)[static_cast<std::size_t>(static_cast<std::uint32_t>(n))] = humanoid_id;
    };

    if (const auto* vrmc_vrm = FindKey(*extensions, "VRMC_vrm");
        vrmc_vrm != nullptr && vrmc_vrm->type == JsonValue::Type::Object) {
        if (const auto* humanoid = FindKey(*vrmc_vrm, "humanoid");
            humanoid != nullptr && humanoid->type == JsonValue::Type::Object) {
            if (const auto* human_bones = FindKey(*humanoid, "humanBones");
                human_bones != nullptr && human_bones->type == JsonValue::Type::Object) {
                for (const auto& kv : human_bones->object_value) {
                    if (kv.second.type != JsonValue::Type::Object) {
                        continue;
                    }
                    if (const auto* node = FindKey(kv.second, "node"); node != nullptr) {
                        try_add(kv.first, *node);
                    }
                }
            }
        }
    }

    if (const auto* vrm_legacy = FindKey(*extensions, "VRM");
        vrm_legacy != nullptr && vrm_legacy->type == JsonValue::Type::Object) {
        if (const auto* humanoid = FindKey(*vrm_legacy, "humanoid");
            humanoid != nullptr && humanoid->type == JsonValue::Type::Object) {
            if (const auto* human_bones = FindKey(*humanoid, "humanBones");
                human_bones != nullptr && human_bones->type == JsonValue::Type::Array) {
                for (const auto& item : human_bones->array_value) {
                    if (item.type != JsonValue::Type::Object) {
                        continue;
                    }
                    std::string bone_name;
                    if (!TryGetString(item, "bone", &bone_name)) {
                        continue;
                    }
                    if (const auto* node = FindKey(item, "node"); node != nullptr) {
                        try_add(bone_name, *node);
                    }
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

std::string MakeNodePath(const JsonValue* nodes_v, std::size_t node_index) {
    if (nodes_v != nullptr &&
        nodes_v->type == JsonValue::Type::Array &&
        node_index < nodes_v->array_value.size()) {
        const auto& node = nodes_v->array_value[node_index];
        if (node.type == JsonValue::Type::Object) {
            std::string name;
            if (TryGetString(node, "name", &name) && !name.empty()) {
                return name;
            }
        }
    }
    return "node_" + std::to_string(node_index);
}

void ParseVrmSpringBonePayloads(
    const JsonValue& root,
    const JsonValue* nodes_v,
    std::vector<PhysicsColliderPayload>* out_colliders,
    std::vector<SpringBonePayload>* out_springs) {
    if (out_colliders == nullptr || out_springs == nullptr) {
        return;
    }
    out_colliders->clear();
    out_springs->clear();

    const auto* extensions = FindKey(root, "extensions");
    if (extensions == nullptr || extensions->type != JsonValue::Type::Object) {
        return;
    }

    const auto* vrmc_spring = FindKey(*extensions, "VRMC_springBone");
    if (vrmc_spring != nullptr && vrmc_spring->type == JsonValue::Type::Object) {
        std::vector<std::vector<std::size_t>> collider_groups;
        const auto* colliders = FindKey(*vrmc_spring, "colliders");
        if (colliders != nullptr && colliders->type == JsonValue::Type::Array) {
            out_colliders->reserve(colliders->array_value.size());
            for (std::size_t ci = 0U; ci < colliders->array_value.size(); ++ci) {
                const auto& collider = colliders->array_value[ci];
                if (collider.type != JsonValue::Type::Object) {
                    continue;
                }
                PhysicsColliderPayload payload;
                payload.name = "vrm1_collider_" + std::to_string(ci);
                std::size_t node_index = std::numeric_limits<std::size_t>::max();
                if (TryGetIndex(collider, "node", &node_index)) {
                    payload.bone_path = MakeNodePath(nodes_v, node_index);
                }
                const auto* shape = FindKey(collider, "shape");
                if (shape != nullptr && shape->type == JsonValue::Type::Object) {
                    const auto* sphere = FindKey(*shape, "sphere");
                    if (sphere != nullptr && sphere->type == JsonValue::Type::Object) {
                        payload.shape = PhysicsColliderShape::Sphere;
                        std::vector<float> offset;
                        if (TryGetNumberArray(*sphere, "offset", 3U, &offset)) {
                            payload.local_position[0] = offset[0];
                            payload.local_position[1] = offset[1];
                            payload.local_position[2] = offset[2];
                        }
                        double radius = 0.0;
                        if (TryGetNumber(*sphere, "radius", &radius)) {
                            payload.radius = static_cast<float>(radius);
                        }
                    }
                    const auto* capsule = FindKey(*shape, "capsule");
                    if (capsule != nullptr && capsule->type == JsonValue::Type::Object) {
                        payload.shape = PhysicsColliderShape::Capsule;
                        std::vector<float> offset;
                        if (TryGetNumberArray(*capsule, "offset", 3U, &offset)) {
                            payload.local_position[0] = offset[0];
                            payload.local_position[1] = offset[1];
                            payload.local_position[2] = offset[2];
                        }
                        std::vector<float> tail;
                        if (TryGetNumberArray(*capsule, "tail", 3U, &tail)) {
                            payload.local_direction[0] = tail[0] - payload.local_position[0];
                            payload.local_direction[1] = tail[1] - payload.local_position[1];
                            payload.local_direction[2] = tail[2] - payload.local_position[2];
                            payload.height = std::sqrt(
                                payload.local_direction[0] * payload.local_direction[0] +
                                payload.local_direction[1] * payload.local_direction[1] +
                                payload.local_direction[2] * payload.local_direction[2]);
                        }
                        double radius = 0.0;
                        if (TryGetNumber(*capsule, "radius", &radius)) {
                            payload.radius = static_cast<float>(radius);
                        }
                    }
                }
                out_colliders->push_back(std::move(payload));
            }
        }
        const auto* collider_groups_v = FindKey(*vrmc_spring, "colliderGroups");
        if (collider_groups_v != nullptr && collider_groups_v->type == JsonValue::Type::Array) {
            collider_groups.reserve(collider_groups_v->array_value.size());
            for (const auto& group : collider_groups_v->array_value) {
                std::vector<std::size_t> refs;
                if (group.type == JsonValue::Type::Object) {
                    const auto* cols = FindKey(group, "colliders");
                    if (cols != nullptr && cols->type == JsonValue::Type::Array) {
                        refs.reserve(cols->array_value.size());
                        for (const auto& c : cols->array_value) {
                            if (c.type != JsonValue::Type::Number || c.number_value < 0.0) {
                                continue;
                            }
                            refs.push_back(static_cast<std::size_t>(c.number_value));
                        }
                    }
                }
                collider_groups.push_back(std::move(refs));
            }
        }
        const auto* springs = FindKey(*vrmc_spring, "springs");
        if (springs != nullptr && springs->type == JsonValue::Type::Array) {
            out_springs->reserve(springs->array_value.size());
            for (std::size_t si = 0U; si < springs->array_value.size(); ++si) {
                const auto& spring = springs->array_value[si];
                if (spring.type != JsonValue::Type::Object) {
                    continue;
                }
                SpringBonePayload payload;
                payload.name = "vrm1_spring_" + std::to_string(si);
                (void)TryGetString(spring, "name", &payload.name);
                const auto* joints = FindKey(spring, "joints");
                if (joints != nullptr && joints->type == JsonValue::Type::Array) {
                    payload.bone_paths.reserve(joints->array_value.size());
                    for (std::size_t ji = 0U; ji < joints->array_value.size(); ++ji) {
                        const auto& joint = joints->array_value[ji];
                        if (joint.type != JsonValue::Type::Object) {
                            continue;
                        }
                        std::size_t node_index = std::numeric_limits<std::size_t>::max();
                        if (!TryGetIndex(joint, "node", &node_index)) {
                            continue;
                        }
                        const auto node_path = MakeNodePath(nodes_v, node_index);
                        payload.bone_paths.push_back(node_path);
                        if (payload.root_bone_path.empty()) {
                            payload.root_bone_path = node_path;
                        }
                        double stiffness = static_cast<double>(payload.stiffness);
                        if (TryGetNumber(joint, "stiffness", &stiffness) || TryGetNumber(joint, "stiffnessForce", &stiffness)) {
                            payload.stiffness = static_cast<float>(stiffness);
                        }
                        double drag = static_cast<double>(payload.drag);
                        if (TryGetNumber(joint, "dragForce", &drag) || TryGetNumber(joint, "drag", &drag)) {
                            payload.drag = static_cast<float>(drag);
                        }
                        double radius = static_cast<double>(payload.radius);
                        if (TryGetNumber(joint, "hitRadius", &radius)) {
                            payload.radius = static_cast<float>(radius);
                        }
                        double gravity_power = 0.0;
                        std::vector<float> gravity_dir;
                        if (TryGetNumber(joint, "gravityPower", &gravity_power) &&
                            TryGetNumberArray(joint, "gravityDir", 3U, &gravity_dir)) {
                            payload.gravity[0] = gravity_dir[0] * static_cast<float>(gravity_power);
                            payload.gravity[1] = gravity_dir[1] * static_cast<float>(gravity_power);
                            payload.gravity[2] = gravity_dir[2] * static_cast<float>(gravity_power);
                        }
                    }
                }
                const auto* group_refs = FindKey(spring, "colliderGroups");
                if (group_refs != nullptr && group_refs->type == JsonValue::Type::Array) {
                    for (const auto& ref : group_refs->array_value) {
                        if (ref.type != JsonValue::Type::Number || ref.number_value < 0.0) {
                            continue;
                        }
                        const auto gidx = static_cast<std::size_t>(ref.number_value);
                        if (gidx >= collider_groups.size()) {
                            continue;
                        }
                        for (const auto cidx : collider_groups[gidx]) {
                            if (cidx < out_colliders->size()) {
                                payload.collider_refs.push_back((*out_colliders)[cidx].name);
                            }
                        }
                    }
                }
                out_springs->push_back(std::move(payload));
            }
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
    std::vector<std::vector<std::size_t>> collider_groups;
    const auto* collider_groups_v = FindKey(*secondary, "colliderGroups");
    if (collider_groups_v != nullptr && collider_groups_v->type == JsonValue::Type::Array) {
        collider_groups.reserve(collider_groups_v->array_value.size());
        for (std::size_t gi = 0U; gi < collider_groups_v->array_value.size(); ++gi) {
            const auto& group = collider_groups_v->array_value[gi];
            std::vector<std::size_t> refs;
            if (group.type == JsonValue::Type::Object) {
                std::size_t node_index = std::numeric_limits<std::size_t>::max();
                (void)TryGetIndex(group, "node", &node_index);
                const auto* colliders = FindKey(group, "colliders");
                if (colliders != nullptr && colliders->type == JsonValue::Type::Array) {
                    refs.reserve(colliders->array_value.size());
                    for (std::size_t ci = 0U; ci < colliders->array_value.size(); ++ci) {
                        const auto& collider = colliders->array_value[ci];
                        if (collider.type != JsonValue::Type::Object) {
                            continue;
                        }
                        PhysicsColliderPayload payload;
                        payload.name = "vrm0_collider_" + std::to_string(gi) + "_" + std::to_string(ci);
                        payload.shape = PhysicsColliderShape::Sphere;
                        payload.bone_path = MakeNodePath(nodes_v, node_index);
                        std::vector<float> offset;
                        if (TryGetNumberArray(collider, "offset", 3U, &offset)) {
                            payload.local_position[0] = offset[0];
                            payload.local_position[1] = offset[1];
                            payload.local_position[2] = offset[2];
                        }
                        double radius = 0.0;
                        if (TryGetNumber(collider, "radius", &radius)) {
                            payload.radius = static_cast<float>(radius);
                        }
                        refs.push_back(out_colliders->size());
                        out_colliders->push_back(std::move(payload));
                    }
                }
            }
            collider_groups.push_back(std::move(refs));
        }
    }
    const auto* bone_groups = FindKey(*secondary, "boneGroups");
    if (bone_groups != nullptr && bone_groups->type == JsonValue::Type::Array) {
        out_springs->reserve(bone_groups->array_value.size());
        for (std::size_t bi = 0U; bi < bone_groups->array_value.size(); ++bi) {
            const auto& group = bone_groups->array_value[bi];
            if (group.type != JsonValue::Type::Object) {
                continue;
            }
            SpringBonePayload payload;
            payload.name = "vrm0_spring_" + std::to_string(bi);
            (void)TryGetString(group, "comment", &payload.name);
            const auto* bones = FindKey(group, "bones");
            if (bones != nullptr && bones->type == JsonValue::Type::Array) {
                payload.bone_paths.reserve(bones->array_value.size());
                for (const auto& b : bones->array_value) {
                    if (b.type != JsonValue::Type::Number || b.number_value < 0.0) {
                        continue;
                    }
                    const auto node_idx = static_cast<std::size_t>(b.number_value);
                    const auto node_path = MakeNodePath(nodes_v, node_idx);
                    payload.bone_paths.push_back(node_path);
                    if (payload.root_bone_path.empty()) {
                        payload.root_bone_path = node_path;
                    }
                }
            }
            double stiffness = static_cast<double>(payload.stiffness);
            if (TryGetNumber(group, "stiffness", &stiffness) || TryGetNumber(group, "stiffiness", &stiffness)) {
                payload.stiffness = static_cast<float>(stiffness);
            }
            double drag = static_cast<double>(payload.drag);
            if (TryGetNumber(group, "dragForce", &drag)) {
                payload.drag = static_cast<float>(drag);
            }
            double radius = static_cast<double>(payload.radius);
            if (TryGetNumber(group, "hitRadius", &radius)) {
                payload.radius = static_cast<float>(radius);
            }
            std::vector<float> gravity_dir;
            double gravity_power = 0.0;
            if (TryGetNumberArray(group, "gravityDir", 3U, &gravity_dir) && TryGetNumber(group, "gravityPower", &gravity_power)) {
                payload.gravity[0] = gravity_dir[0] * static_cast<float>(gravity_power);
                payload.gravity[1] = gravity_dir[1] * static_cast<float>(gravity_power);
                payload.gravity[2] = gravity_dir[2] * static_cast<float>(gravity_power);
            }
            const auto* group_refs = FindKey(group, "colliderGroups");
            if (group_refs != nullptr && group_refs->type == JsonValue::Type::Array) {
                for (const auto& ref : group_refs->array_value) {
                    if (ref.type != JsonValue::Type::Number || ref.number_value < 0.0) {
                        continue;
                    }
                    const auto gidx = static_cast<std::size_t>(ref.number_value);
                    if (gidx >= collider_groups.size()) {
                        continue;
                    }
                    for (const auto cidx : collider_groups[gidx]) {
                        if (cidx < out_colliders->size()) {
                            payload.collider_refs.push_back((*out_colliders)[cidx].name);
                        }
                    }
                }
            }
            out_springs->push_back(std::move(payload));
        }
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
    std::vector<std::size_t> node_to_skin_index;
    std::vector<std::array<float, 16U>> mesh_node_transforms;
    std::vector<bool> mesh_has_node_transform;
    std::vector<bool> mesh_node_transform_conflict;
    std::vector<bool> mesh_node_global_seen;
    std::vector<std::array<float, 16U>> mesh_first_node_global;
    std::vector<std::uint32_t> mesh_node_ref_counts;
    std::vector<std::size_t> mesh_skin_index;
    std::vector<bool> mesh_has_skin;
    mesh_node_transforms.assign(meshes_v->array_value.size(), MakeIdentityMatrix4x4());
    mesh_has_node_transform.assign(meshes_v->array_value.size(), false);
    mesh_node_transform_conflict.assign(meshes_v->array_value.size(), false);
    mesh_node_global_seen.assign(meshes_v->array_value.size(), false);
    mesh_first_node_global.assign(meshes_v->array_value.size(), MakeIdentityMatrix4x4());
    mesh_node_ref_counts.assign(meshes_v->array_value.size(), 0U);
    mesh_skin_index.assign(meshes_v->array_value.size(), std::numeric_limits<std::size_t>::max());
    mesh_has_skin.assign(meshes_v->array_value.size(), false);

    const auto* nodes_v = FindKey(root, "nodes");
    std::vector<std::array<float, 16U>> node_global_transforms;
    std::vector<std::size_t> node_parent_indices;
    std::unordered_map<std::size_t, HumanoidBoneId> node_to_humanoid;
    BuildNodeGlobalTransforms(nodes_v, &node_global_transforms, &node_parent_indices);
    ParseVrmHumanoidNodeMap(root, &node_to_humanoid);
    if (nodes_v != nullptr && nodes_v->type == JsonValue::Type::Array) {
        node_to_mesh_index.assign(
            nodes_v->array_value.size(),
            std::numeric_limits<std::size_t>::max());
        node_to_skin_index.assign(
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
                if (mesh_index < mesh_node_ref_counts.size()) {
                    mesh_node_ref_counts[mesh_index] += 1U;
                    std::size_t skin_index = std::numeric_limits<std::size_t>::max();
                    if (TryGetIndex(node, "skin", &skin_index)) {
                        node_to_skin_index[node_i] = skin_index;
                        if (!mesh_has_skin[mesh_index]) {
                            mesh_has_skin[mesh_index] = true;
                            mesh_skin_index[mesh_index] = skin_index;
                        } else if (mesh_skin_index[mesh_index] != skin_index) {
                            pkg.warnings.push_back(
                                "W_NODE: VRM_MESH_MULTI_SKIN_REF: mesh=" + mesh_names_by_index[mesh_index]);
                            pkg.warning_codes.push_back("VRM_MESH_MULTI_SKIN_REF");
                        }
                    }
                    std::array<float, 16U> node_transform = MakeIdentityMatrix4x4();
                    if (node_i < node_global_transforms.size()) {
                        node_transform = node_global_transforms[node_i];
                    }
                    if (!mesh_node_global_seen[mesh_index]) {
                        mesh_node_global_seen[mesh_index] = true;
                        mesh_first_node_global[mesh_index] = node_transform;
                    } else if (!AreMatrix4x4NearlyEqual(mesh_first_node_global[mesh_index], node_transform)) {
                        mesh_node_transform_conflict[mesh_index] = true;
                    }
                    if (!mesh_node_transform_conflict[mesh_index] && !IsIdentityMatrix4x4(mesh_first_node_global[mesh_index])) {
                        mesh_node_transforms[mesh_index] = mesh_first_node_global[mesh_index];
                        mesh_has_node_transform[mesh_index] = true;
                    } else {
                        mesh_node_transforms[mesh_index] = MakeIdentityMatrix4x4();
                        mesh_has_node_transform[mesh_index] = false;
                    }
                }
            }
        }
    }
    std::uint32_t multi_ref_mesh_count = 0U;
    for (const auto ref_count : mesh_node_ref_counts) {
        if (ref_count > 1U) {
            ++multi_ref_mesh_count;
        }
    }
    if (multi_ref_mesh_count > 0U) {
        pkg.warnings.push_back(
            "W_NODE: VRM_MESH_MULTI_NODE_REF: meshes=" + std::to_string(multi_ref_mesh_count));
        pkg.warning_codes.push_back("VRM_MESH_MULTI_NODE_REF");
    }
    std::uint32_t node_transform_conflict_mesh_count = 0U;
    for (const bool has_conflict : mesh_node_transform_conflict) {
        if (has_conflict) {
            ++node_transform_conflict_mesh_count;
        }
    }
    if (node_transform_conflict_mesh_count > 0U) {
        std::string sample_conflict_mesh = "unknown";
        for (std::size_t mesh_i = 0U; mesh_i < mesh_node_transform_conflict.size(); ++mesh_i) {
            if (mesh_node_transform_conflict[mesh_i] && mesh_i < mesh_names_by_index.size()) {
                sample_conflict_mesh = mesh_names_by_index[mesh_i];
                break;
            }
        }
        pkg.warnings.push_back(
            "W_NODE: VRM_NODE_TRANSFORM_CONFLICT: meshes=" + std::to_string(node_transform_conflict_mesh_count) +
            ", bake=skipped, sampleMesh=" + sample_conflict_mesh);
        pkg.warning_codes.push_back("VRM_NODE_TRANSFORM_CONFLICT");
    }

    struct SkinDef {
        std::vector<std::int32_t> joints;
        std::vector<float> bind_poses_16xn;
        bool valid = false;
    };
    std::vector<SkinDef> skin_defs;
    const auto* skins_v = FindKey(root, "skins");
    if (skins_v != nullptr && skins_v->type == JsonValue::Type::Array) {
        skin_defs.resize(skins_v->array_value.size());
        for (std::size_t skin_i = 0U; skin_i < skins_v->array_value.size(); ++skin_i) {
            const auto& skin = skins_v->array_value[skin_i];
            if (skin.type != JsonValue::Type::Object) {
                continue;
            }
            const auto* joints_v = FindKey(skin, "joints");
            if (joints_v == nullptr || joints_v->type != JsonValue::Type::Array || joints_v->array_value.empty()) {
                continue;
            }
            auto& dst = skin_defs[skin_i];
            dst.joints.clear();
            dst.joints.reserve(joints_v->array_value.size());
            for (const auto& joint : joints_v->array_value) {
                if (joint.type != JsonValue::Type::Number) {
                    continue;
                }
                const double n = joint.number_value;
                if (n < 0.0 || n > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
                    continue;
                }
                dst.joints.push_back(static_cast<std::int32_t>(n));
            }
            if (dst.joints.empty()) {
                continue;
            }
            dst.bind_poses_16xn.assign(dst.joints.size() * 16U, 0.0f);
            for (std::size_t i = 0U; i < dst.joints.size(); ++i) {
                dst.bind_poses_16xn[i * 16U + 0U] = 1.0f;
                dst.bind_poses_16xn[i * 16U + 5U] = 1.0f;
                dst.bind_poses_16xn[i * 16U + 10U] = 1.0f;
                dst.bind_poses_16xn[i * 16U + 15U] = 1.0f;
            }

            std::size_t ibm_accessor_index = std::numeric_limits<std::size_t>::max();
            if (TryGetIndex(skin, "inverseBindMatrices", &ibm_accessor_index)) {
                std::vector<float> ibm_values;
                std::string skin_error;
                if (ExtractMat4FloatAccessor(
                        bin_chunk.bytes,
                        accessors,
                        views,
                        ibm_accessor_index,
                        &ibm_values,
                        &skin_error)) {
                    const std::size_t matrix_count = ibm_values.size() / 16U;
                    if (matrix_count >= dst.joints.size()) {
                        dst.bind_poses_16xn.assign(
                            ibm_values.begin(),
                            ibm_values.begin() + static_cast<std::ptrdiff_t>(dst.joints.size() * 16U));
                    } else {
                        pkg.warnings.push_back(
                            "W_SKIN: VRM_INVERSE_BIND_COUNT_MISMATCH: skin=" + std::to_string(skin_i));
                        pkg.warning_codes.push_back("VRM_INVERSE_BIND_COUNT_MISMATCH");
                    }
                } else {
                    pkg.warnings.push_back(
                        "W_SKIN: VRM_INVERSE_BIND_READ_FAILED: skin=" + std::to_string(skin_i) + ", detail=" + skin_error);
                    pkg.warning_codes.push_back("VRM_INVERSE_BIND_READ_FAILED");
                }
            } else {
                pkg.warnings.push_back(
                    "W_SKIN: VRM_INVERSE_BIND_MISSING: skin=" + std::to_string(skin_i));
                pkg.warning_codes.push_back("VRM_INVERSE_BIND_MISSING");
            }
            dst.valid = true;
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
        bool alpha_capable = false;
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
            image_table[i].alpha_capable = TextureHasAlphaCapability(image_table[i].format, image_table[i].bytes);
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

    std::vector<const JsonValue*> vrm0_material_properties_by_index;
    std::unordered_map<std::string, const JsonValue*> vrm0_material_properties_by_name;
    const auto* root_extensions = FindKey(root, "extensions");
    if (root_extensions != nullptr && root_extensions->type == JsonValue::Type::Object) {
        const auto* vrm0 = FindKey(*root_extensions, "VRM");
        if (vrm0 != nullptr && vrm0->type == JsonValue::Type::Object) {
            const auto* material_props = FindKey(*vrm0, "materialProperties");
            if (material_props != nullptr && material_props->type == JsonValue::Type::Array) {
                vrm0_material_properties_by_index.reserve(material_props->array_value.size());
                for (const auto& prop : material_props->array_value) {
                    if (prop.type != JsonValue::Type::Object) {
                        vrm0_material_properties_by_index.push_back(nullptr);
                        continue;
                    }
                    vrm0_material_properties_by_index.push_back(&prop);
                    std::string prop_name;
                    if (TryGetString(prop, "name", &prop_name) && !prop_name.empty()) {
                        vrm0_material_properties_by_name[prop_name] = &prop;
                    }
                }
            }
        }
    }

    std::vector<MaterialInfo> parsed_materials;
    std::uint32_t missing_texture_refs = 0U;
    std::uint32_t unsupported_materials = 0U;
    auto resolve_texture_name = [&](std::size_t texture_index, std::string* out_name, const std::string& context) -> bool {
        if (out_name == nullptr) {
            return false;
        }
        if (texture_index >= texture_to_image.size()) {
            ++missing_texture_refs;
            pkg.warnings.push_back(
                "W_PAYLOAD: VRM_TEXTURE_MISSING: " + context + " texture index out of range");
            return false;
        }
        const auto image_index = texture_to_image[texture_index];
        if (image_index >= image_table.size() || !image_table[image_index].valid) {
            ++missing_texture_refs;
            pkg.warnings.push_back(
                "W_PAYLOAD: VRM_TEXTURE_MISSING: " + context + " texture source missing");
            return false;
        }
        *out_name = image_table[image_index].name;
        return true;
    };
    auto try_resolve_texture_alpha_capable = [&](std::size_t texture_index, bool* out_alpha_capable) -> bool {
        if (out_alpha_capable == nullptr) {
            return false;
        }
        *out_alpha_capable = false;
        if (texture_index >= texture_to_image.size()) {
            return false;
        }
        const auto image_index = texture_to_image[texture_index];
        if (image_index >= image_table.size() || !image_table[image_index].valid) {
            return false;
        }
        *out_alpha_capable = image_table[image_index].alpha_capable;
        return true;
    };
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
            std::string raw_alpha_mode;
            if (TryGetString(material, "alphaMode", &raw_alpha_mode)) {
                info.alpha_mode = NormalizeAlphaMode(raw_alpha_mode);
                info.alpha_source = "gltf.alphaMode";
            } else {
                info.alpha_mode = "OPAQUE";
                info.alpha_source = "default.opaque";
            }
            double alpha_cutoff = static_cast<double>(info.alpha_cutoff);
            if (TryGetNumber(material, "alphaCutoff", &alpha_cutoff)) {
                info.alpha_cutoff = static_cast<float>(alpha_cutoff);
            }
            info.alpha_cutoff = std::max(0.0f, std::min(1.0f, info.alpha_cutoff));
            ApplyAlphaHintsFromObject(
                material,
                "material",
                &info.alpha_mode,
                &info.alpha_source,
                &info.alpha_cutoff);

            const auto* pbr = FindKey(material, "pbrMetallicRoughness");
            if (pbr != nullptr && pbr->type == JsonValue::Type::Object) {
                std::vector<float> base_color_factor;
                if (TryGetNumberArray(*pbr, "baseColorFactor", 4U, &base_color_factor)) {
                    info.base_color = {
                        base_color_factor[0],
                        base_color_factor[1],
                        base_color_factor[2],
                        base_color_factor[3]};
                    info.has_mtoon_binding = true;
                }
                const auto* tex_obj = FindKey(*pbr, "baseColorTexture");
                if (tex_obj != nullptr && tex_obj->type == JsonValue::Type::Object) {
                    std::size_t texture_index = std::numeric_limits<std::size_t>::max();
                    if (TryGetIndex(*tex_obj, "index", &texture_index)) {
                        if (resolve_texture_name(texture_index, &info.base_color_texture_name, "baseColorTexture for material '" + info.name + "'")) {
                            info.has_mtoon_binding = true;
                            bool alpha_capable = false;
                            if (try_resolve_texture_alpha_capable(texture_index, &alpha_capable)) {
                                info.base_color_texture_alpha_capable = alpha_capable;
                            }
                        }
                    }
                }
            }

            const auto* normal_tex = FindKey(material, "normalTexture");
            if (normal_tex != nullptr && normal_tex->type == JsonValue::Type::Object) {
                std::size_t texture_index = std::numeric_limits<std::size_t>::max();
                if (TryGetIndex(*normal_tex, "index", &texture_index)) {
                    if (resolve_texture_name(texture_index, &info.normal_texture_name, "normalTexture for material '" + info.name + "'")) {
                        info.has_mtoon_binding = true;
                    }
                }
                double normal_scale = static_cast<double>(info.bump_scale);
                if (TryGetNumber(*normal_tex, "scale", &normal_scale)) {
                    info.bump_scale = static_cast<float>(normal_scale);
                    info.has_mtoon_binding = true;
                }
            }

            std::vector<float> emissive_factor;
            if (TryGetNumberArray(material, "emissiveFactor", 3U, &emissive_factor)) {
                info.emission_color = {emissive_factor[0], emissive_factor[1], emissive_factor[2], 1.0f};
                info.has_mtoon_binding = true;
            }
            const auto* emissive_tex = FindKey(material, "emissiveTexture");
            if (emissive_tex != nullptr && emissive_tex->type == JsonValue::Type::Object) {
                std::size_t texture_index = std::numeric_limits<std::size_t>::max();
                if (TryGetIndex(*emissive_tex, "index", &texture_index)) {
                    if (resolve_texture_name(texture_index, &info.emission_texture_name, "emissiveTexture for material '" + info.name + "'")) {
                        info.has_mtoon_binding = true;
                    }
                }
            }

            const auto* extensions = FindKey(material, "extensions");
            if (extensions != nullptr && extensions->type == JsonValue::Type::Object) {
                ApplyAlphaHintsFromObject(
                    *extensions,
                    "material.extensions",
                    &info.alpha_mode,
                    &info.alpha_source,
                    &info.alpha_cutoff);
                const auto* mtoon = FindKey(*extensions, "VRMC_materials_mtoon");
                if (mtoon != nullptr && mtoon->type == JsonValue::Type::Object) {
                    ApplyAlphaHintsFromObject(
                        *mtoon,
                        "material.extensions.VRMC_materials_mtoon",
                        &info.alpha_mode,
                        &info.alpha_source,
                        &info.alpha_cutoff);
                    std::vector<float> shade_color_factor;
                    if (TryGetNumberArray(*mtoon, "shadeColorFactor", 3U, &shade_color_factor)) {
                        info.shade_color = {shade_color_factor[0], shade_color_factor[1], shade_color_factor[2], 1.0f};
                        info.has_mtoon_binding = true;
                    }
                    std::vector<float> rim_color_factor;
                    if (TryGetNumberArray(*mtoon, "rimColorFactor", 3U, &rim_color_factor)) {
                        info.rim_color = {rim_color_factor[0], rim_color_factor[1], rim_color_factor[2], 1.0f};
                        info.has_mtoon_binding = true;
                    }
                    double rim_power = static_cast<double>(info.rim_power);
                    if (TryGetNumber(*mtoon, "rimFresnelPowerFactor", &rim_power)) {
                        info.rim_power = static_cast<float>(rim_power);
                        info.has_mtoon_binding = true;
                    }
                    double rim_mix = static_cast<double>(info.rim_lighting_mix);
                    if (TryGetNumber(*mtoon, "rimLightingMixFactor", &rim_mix)) {
                        info.rim_lighting_mix = static_cast<float>(rim_mix);
                        info.has_mtoon_binding = true;
                    }
                    const auto* shade_mul_tex = FindKey(*mtoon, "shadeMultiplyTexture");
                    if (shade_mul_tex != nullptr && shade_mul_tex->type == JsonValue::Type::Object) {
                        std::size_t texture_index = std::numeric_limits<std::size_t>::max();
                        if (TryGetIndex(*shade_mul_tex, "index", &texture_index)) {
                            (void)resolve_texture_name(texture_index, &info.rim_texture_name, "shadeMultiplyTexture for material '" + info.name + "'");
                        }
                    }
                    std::vector<float> matcap_factor;
                    if (TryGetNumberArray(*mtoon, "matcapFactor", 3U, &matcap_factor)) {
                        info.matcap_declared = true;
                        info.matcap_color = {matcap_factor[0], matcap_factor[1], matcap_factor[2], 1.0f};
                        info.matcap_strength = std::max(0.35f, info.matcap_strength);
                        info.has_matcap_binding = true;
                        info.has_mtoon_binding = true;
                    }
                    const auto* matcap_tex = FindKey(*mtoon, "matcapTexture");
                    if (matcap_tex != nullptr && matcap_tex->type == JsonValue::Type::Object) {
                        info.matcap_declared = true;
                        std::size_t texture_index = std::numeric_limits<std::size_t>::max();
                        if (TryGetIndex(*matcap_tex, "index", &texture_index)) {
                            if (resolve_texture_name(texture_index, &info.matcap_texture_name, "matcapTexture for material '" + info.name + "'")) {
                                info.has_matcap_binding = true;
                                info.has_mtoon_binding = true;
                            }
                        }
                    }
                    double outline_width = static_cast<double>(info.outline_width);
                    if (TryGetNumber(*mtoon, "outlineWidthFactor", &outline_width)) {
                        info.outline_width = static_cast<float>(outline_width);
                        info.has_outline_binding = true;
                        info.has_mtoon_binding = true;
                    }
                    double outline_mix = static_cast<double>(info.outline_lighting_mix);
                    if (TryGetNumber(*mtoon, "outlineLightingMixFactor", &outline_mix)) {
                        info.outline_lighting_mix = static_cast<float>(outline_mix);
                        info.has_outline_binding = true;
                        info.has_mtoon_binding = true;
                    }
                    const auto* uv_mask_tex = FindKey(*mtoon, "uvAnimationMaskTexture");
                    if (uv_mask_tex != nullptr && uv_mask_tex->type == JsonValue::Type::Object) {
                        std::size_t texture_index = std::numeric_limits<std::size_t>::max();
                        if (TryGetIndex(*uv_mask_tex, "index", &texture_index)) {
                            if (resolve_texture_name(texture_index, &info.uv_anim_mask_texture_name, "uvAnimationMaskTexture for material '" + info.name + "'")) {
                                info.has_uv_anim_binding = true;
                                info.has_mtoon_binding = true;
                            }
                        }
                    }
                    double uv_scroll_x = static_cast<double>(info.uv_anim_scroll_x);
                    if (TryGetNumber(*mtoon, "uvAnimationScrollXSpeedFactor", &uv_scroll_x)) {
                        info.uv_anim_scroll_x = static_cast<float>(uv_scroll_x);
                        info.has_uv_anim_binding = true;
                        info.has_mtoon_binding = true;
                    }
                    double uv_scroll_y = static_cast<double>(info.uv_anim_scroll_y);
                    if (TryGetNumber(*mtoon, "uvAnimationScrollYSpeedFactor", &uv_scroll_y)) {
                        info.uv_anim_scroll_y = static_cast<float>(uv_scroll_y);
                        info.has_uv_anim_binding = true;
                        info.has_mtoon_binding = true;
                    }
                    double uv_rot = static_cast<double>(info.uv_anim_rotation);
                    if (TryGetNumber(*mtoon, "uvAnimationRotationSpeedFactor", &uv_rot)) {
                        info.uv_anim_rotation = static_cast<float>(uv_rot);
                        info.has_uv_anim_binding = true;
                        info.has_mtoon_binding = true;
                    }
                }
            }
            const auto* extras = FindKey(material, "extras");
            if (extras != nullptr && extras->type == JsonValue::Type::Object) {
                ApplyAlphaHintsFromObject(
                    *extras,
                    "material.extras",
                    &info.alpha_mode,
                    &info.alpha_source,
                    &info.alpha_cutoff);
            }
            const JsonValue* vrm0_material_props = nullptr;
            if (i < vrm0_material_properties_by_index.size()) {
                vrm0_material_props = vrm0_material_properties_by_index[i];
            }
            if (vrm0_material_props == nullptr) {
                const auto by_name_it = vrm0_material_properties_by_name.find(info.name);
                if (by_name_it != vrm0_material_properties_by_name.end()) {
                    vrm0_material_props = by_name_it->second;
                }
            }
            if (vrm0_material_props != nullptr) {
                ApplyAlphaHintsFromObject(
                    *vrm0_material_props,
                    "root.extensions.VRM.materialProperties",
                    &info.alpha_mode,
                    &info.alpha_source,
                    &info.alpha_cutoff);
            }
            const bool opaque_from_default =
                info.alpha_mode == "OPAQUE" && info.alpha_source == "default.opaque";
            const bool fallback_texture_signal = info.base_color_texture_alpha_capable;
            const bool should_promote_blend =
                (opaque_from_default && fallback_texture_signal);
            if (should_promote_blend) {
                info.alpha_mode = "BLEND";
                info.alpha_source = "fallback.texture-alpha";
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
        material_payload.material_param_encoding = "typed-v1";
        material_payload.typed_schema_version = 1U;
        std::ostringstream shader_params;
        shader_params << std::fixed << std::setprecision(6);
        shader_params << "_BaseColor=(" << m.base_color[0] << "," << m.base_color[1] << "," << m.base_color[2] << "," << m.base_color[3] << ")";
        shader_params << ",_ShadeColor=(" << m.shade_color[0] << "," << m.shade_color[1] << "," << m.shade_color[2] << "," << m.shade_color[3] << ")";
        shader_params << ",_EmissionColor=(" << m.emission_color[0] << "," << m.emission_color[1] << "," << m.emission_color[2] << "," << m.emission_color[3] << ")";
        shader_params << ",_RimColor=(" << m.rim_color[0] << "," << m.rim_color[1] << "," << m.rim_color[2] << "," << m.rim_color[3] << ")";
        const auto alpha_mode = ToLower(m.alpha_mode);
        if (alpha_mode == "mask") {
            shader_params << ",_Cutoff=" << m.alpha_cutoff;
        }
        shader_params << ",_BumpScale=" << m.bump_scale;
        shader_params << ",_RimFresnelPower=" << m.rim_power;
        shader_params << ",_RimLightingMix=" << m.rim_lighting_mix;
        shader_params << ",_MatCapBlend=" << m.matcap_strength;
        shader_params << ",_OutlineWidth=" << m.outline_width;
        shader_params << ",_OutlineLightingMix=" << m.outline_lighting_mix;
        shader_params << ",_UvAnimScrollX=" << m.uv_anim_scroll_x;
        shader_params << ",_UvAnimScrollY=" << m.uv_anim_scroll_y;
        shader_params << ",_UvAnimRotation=" << m.uv_anim_rotation;
        if (alpha_mode == "mask") {
            shader_params << ",_AlphaClip=1";
        } else if (alpha_mode == "blend") {
            shader_params << ",_Surface=1,_Mode=2,_ALPHABLEND_ON=1";
        }
        material_payload.shader_params_json = shader_params.str();
        material_payload.typed_color_params.push_back({"_BaseColor", {m.base_color[0], m.base_color[1], m.base_color[2], m.base_color[3]}});
        material_payload.typed_color_params.push_back({"_ShadeColor", {m.shade_color[0], m.shade_color[1], m.shade_color[2], m.shade_color[3]}});
        material_payload.typed_color_params.push_back({"_EmissionColor", {m.emission_color[0], m.emission_color[1], m.emission_color[2], m.emission_color[3]}});
        material_payload.typed_color_params.push_back({"_RimColor", {m.rim_color[0], m.rim_color[1], m.rim_color[2], m.rim_color[3]}});
        material_payload.typed_color_params.push_back({"_MatCapColor", {m.matcap_color[0], m.matcap_color[1], m.matcap_color[2], m.matcap_color[3]}});
        if (alpha_mode == "mask") {
            material_payload.typed_float_params.push_back({"_Cutoff", m.alpha_cutoff});
        }
        material_payload.typed_float_params.push_back({"_BumpScale", m.bump_scale});
        material_payload.typed_float_params.push_back({"_RimFresnelPower", m.rim_power});
        material_payload.typed_float_params.push_back({"_RimLightingMix", m.rim_lighting_mix});
        material_payload.typed_float_params.push_back({"_MatCapBlend", m.matcap_strength});
        material_payload.typed_float_params.push_back({"_OutlineWidth", m.outline_width});
        material_payload.typed_float_params.push_back({"_OutlineLightingMix", m.outline_lighting_mix});
        material_payload.typed_float_params.push_back({"_UvAnimScrollX", m.uv_anim_scroll_x});
        material_payload.typed_float_params.push_back({"_UvAnimScrollY", m.uv_anim_scroll_y});
        material_payload.typed_float_params.push_back({"_UvAnimRotation", m.uv_anim_rotation});
        if (!m.base_color_texture_name.empty()) {
            material_payload.typed_texture_params.push_back({"base", m.base_color_texture_name});
        }
        if (!m.normal_texture_name.empty()) {
            material_payload.typed_texture_params.push_back({"normal", m.normal_texture_name});
        }
        if (!m.emission_texture_name.empty()) {
            material_payload.typed_texture_params.push_back({"emission", m.emission_texture_name});
        }
        if (!m.rim_texture_name.empty()) {
            material_payload.typed_texture_params.push_back({"rim", m.rim_texture_name});
        }
        if (!m.matcap_texture_name.empty()) {
            material_payload.typed_texture_params.push_back({"matcap", m.matcap_texture_name});
            material_payload.typed_texture_params.push_back({"_MatCapTex", m.matcap_texture_name});
        }
        if (!m.uv_anim_mask_texture_name.empty()) {
            material_payload.typed_texture_params.push_back({"uvAnimationMask", m.uv_anim_mask_texture_name});
        }
        MaterialDiagnosticsEntry material_diag_entry;
        material_diag_entry.material_name = m.name;
        material_diag_entry.alpha_mode = m.alpha_mode;
        material_diag_entry.alpha_source = m.alpha_source;
        material_diag_entry.alpha_cutoff = m.alpha_cutoff;
        material_diag_entry.double_sided = m.double_sided;
        material_diag_entry.has_mtoon_binding = m.has_mtoon_binding;
        material_diag_entry.has_base_texture = !m.base_color_texture_name.empty();
        material_diag_entry.has_normal_texture = !m.normal_texture_name.empty();
        material_diag_entry.has_emission_texture = !m.emission_texture_name.empty();
        material_diag_entry.has_rim_texture = !m.rim_texture_name.empty();
        material_diag_entry.typed_color_param_count = static_cast<std::uint32_t>(material_payload.typed_color_params.size());
        material_diag_entry.typed_float_param_count = static_cast<std::uint32_t>(material_payload.typed_float_params.size());
        material_diag_entry.typed_texture_param_count = static_cast<std::uint32_t>(material_payload.typed_texture_params.size());
        pkg.material_diagnostics.push_back(std::move(material_diag_entry));
        pkg.material_payloads.push_back(std::move(material_payload));
        std::ostringstream material_diag;
        material_diag << "W_MATERIAL: " << m.name
                      << ", alphaMode=" << m.alpha_mode
                      << ", alphaSource=" << m.alpha_source
                      << ", alphaCutoff=" << m.alpha_cutoff
                      << ", doubleSided=" << (m.double_sided ? "true" : "false")
                      << ", mtoonBinding=" << (m.has_mtoon_binding ? "true" : "false");
        if (!m.base_color_texture_name.empty()) {
            material_diag << ", baseTexture=" << m.base_color_texture_name;
        }
        pkg.warnings.push_back(material_diag.str());
    }

    std::size_t mesh_added = 0U;
    std::uint32_t skinned_primitive_count = 0U;
    std::uint32_t skinned_payload_emitted = 0U;
    std::uint32_t skinned_payload_failed = 0U;
    std::vector<bool> mesh_node_transform_applied;
    mesh_node_transform_applied.assign(meshes_v->array_value.size(), false);
    for (std::size_t mesh_i = 0U; mesh_i < meshes_v->array_value.size(); ++mesh_i) {
        const auto& mesh = meshes_v->array_value[mesh_i];
        if (mesh.type != JsonValue::Type::Object) {
            continue;
        }
        const auto* prims_v = FindKey(mesh, "primitives");
        if (prims_v == nullptr || prims_v->type != JsonValue::Type::Array || prims_v->array_value.empty()) {
            continue;
        }
        std::string mesh_name;
        if (!TryGetString(mesh, "name", &mesh_name)) {
            mesh_name = "Mesh" + std::to_string(mesh_i);
        }
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

        for (std::size_t prim_i = 0U; prim_i < prims_v->array_value.size(); ++prim_i) {
            const auto& prim = prims_v->array_value[prim_i];
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
            const bool multi_primitive = prims_v->array_value.size() > 1U;

            MeshRenderPayload mesh_payload;
            mesh_payload.name = multi_primitive
                ? (mesh_name + "#p" + std::to_string(prim_i))
                : mesh_name;
            mesh_payload.vertex_stride = 12U;
            std::uint32_t vtx_count = 0U;
            std::string read_error;
            if (!ExtractPositions(bin_chunk.bytes, accessors, views, pos_accessor, &mesh_payload.vertex_blob, &vtx_count, &read_error)) {
                pkg.warnings.push_back("W_PAYLOAD: VRM_POSITION_READ_FAILED: mesh=" + mesh_payload.name + ", detail=" + read_error);
                continue;
            }
            if (mesh_i < mesh_has_node_transform.size() && mesh_has_node_transform[mesh_i]) {
                if (ApplyPositionTransformToVertexBlob(
                        &mesh_payload.vertex_blob,
                        mesh_payload.vertex_stride,
                        mesh_node_transforms[mesh_i])) {
                    mesh_node_transform_applied[mesh_i] = true;
                } else {
                    pkg.warnings.push_back(
                        "W_NODE: VRM_NODE_TRANSFORM_INVALID: mesh=" + mesh_payload.name + ", action=skipped");
                    pkg.warning_codes.push_back("VRM_NODE_TRANSFORM_INVALID");
                }
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
                    pkg.warnings.push_back("W_PAYLOAD: VRM_TEXCOORD0_READ_FAILED: mesh=" + mesh_payload.name + ", detail=" + read_error);
                }
            }

            std::size_t idx_accessor = std::numeric_limits<std::size_t>::max();
            if (TryGetIndex(prim, "indices", &idx_accessor)) {
                if (!ExtractIndices(bin_chunk.bytes, accessors, views, idx_accessor, &mesh_payload.indices, &read_error)) {
                    pkg.warnings.push_back("W_PAYLOAD: VRM_INDEX_READ_FAILED: mesh=" + mesh_payload.name + ", detail=" + read_error);
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

            if (mesh_i < mesh_has_skin.size() && mesh_has_skin[mesh_i]) {
                ++skinned_primitive_count;
                bool emitted = false;
                const auto skin_index = mesh_skin_index[mesh_i];
                if (skin_index < skin_defs.size() && skin_defs[skin_index].valid) {
                    const auto* joints_v = FindKey(*attrs_v, "JOINTS_0");
                    const auto* weights_v = FindKey(*attrs_v, "WEIGHTS_0");
                    if (joints_v != nullptr && joints_v->type == JsonValue::Type::Number &&
                        weights_v != nullptr && weights_v->type == JsonValue::Type::Number) {
                        const std::size_t joints_accessor =
                            static_cast<std::size_t>(static_cast<std::uint32_t>(joints_v->number_value));
                        const std::size_t weights_accessor =
                            static_cast<std::size_t>(static_cast<std::uint32_t>(weights_v->number_value));
                        std::vector<std::uint8_t> skin_weight_blob;
                        std::string skin_error;
                        if (ExtractSkinWeightBlob(
                                bin_chunk.bytes,
                                accessors,
                                views,
                                joints_accessor,
                                weights_accessor,
                                vtx_count,
                                &skin_weight_blob,
                                &skin_error)) {
                            SkinRenderPayload skin_payload;
                            skin_payload.mesh_name = mesh_payload.name;
                            skin_payload.bone_indices = skin_defs[skin_index].joints;
                            skin_payload.bind_poses_16xn = skin_defs[skin_index].bind_poses_16xn;
                            skin_payload.skin_weight_blob = std::move(skin_weight_blob);
                            pkg.skin_payloads.push_back(std::move(skin_payload));

                            SkeletonRenderPayload skeleton_payload;
                            skeleton_payload.mesh_name = mesh_payload.name;
                            skeleton_payload.bone_matrices_16xn.reserve(skin_defs[skin_index].joints.size() * 16U);
                            const bool has_node_transform_applied =
                                mesh_i < mesh_node_transform_applied.size() && mesh_node_transform_applied[mesh_i];
                            std::array<float, 16U> mesh_global_for_skin = MakeIdentityMatrix4x4();
                            std::array<float, 16U> mesh_inv_for_skin = MakeIdentityMatrix4x4();
                            if (!has_node_transform_applied &&
                                mesh_i < mesh_node_transforms.size() &&
                                mesh_i < mesh_node_transform_conflict.size() &&
                                !mesh_node_transform_conflict[mesh_i]) {
                                mesh_global_for_skin = mesh_node_transforms[mesh_i];
                                if (!TryInvertMatrix4x4(mesh_global_for_skin, &mesh_inv_for_skin)) {
                                    mesh_inv_for_skin = MakeIdentityMatrix4x4();
                                    pkg.warnings.push_back(
                                        "W_SKIN: VRM_MESH_GLOBAL_INVERSE_FAILED: mesh=" + mesh_payload.name);
                                }
                            }
                            for (const auto joint_index : skin_defs[skin_index].joints) {
                                std::array<float, 16U> bone_m = MakeIdentityMatrix4x4();
                                if (joint_index >= 0 &&
                                    static_cast<std::size_t>(joint_index) < node_global_transforms.size()) {
                                    bone_m = node_global_transforms[static_cast<std::size_t>(joint_index)];
                                }
                                if (!has_node_transform_applied) {
                                    // Fallback path for conflict/no-transform cases keeps mesh-space conversion.
                                    bone_m = MulMatrix4x4(mesh_inv_for_skin, bone_m);
                                }
                                skeleton_payload.bone_matrices_16xn.insert(
                                    skeleton_payload.bone_matrices_16xn.end(),
                                    bone_m.begin(),
                                    bone_m.end());
                            }
                            pkg.skeleton_payloads.push_back(std::move(skeleton_payload));

                            SkeletonRigPayload rig_payload;
                            rig_payload.mesh_name = mesh_payload.name;
                            rig_payload.bones.reserve(skin_defs[skin_index].joints.size());
                            std::unordered_map<std::int32_t, std::size_t> joint_index_to_local;
                            joint_index_to_local.reserve(skin_defs[skin_index].joints.size());
                            for (std::size_t ji = 0U; ji < skin_defs[skin_index].joints.size(); ++ji) {
                                joint_index_to_local[skin_defs[skin_index].joints[ji]] = ji;
                            }
                            for (std::size_t ji = 0U; ji < skin_defs[skin_index].joints.size(); ++ji) {
                                const auto joint_index = skin_defs[skin_index].joints[ji];
                                SkeletonRigBonePayload bone;
                                bone.bone_name = "Bone_" + std::to_string(joint_index);
                                bone.local_matrix_16.assign(16U, 0.0f);
                                bone.local_matrix_16[0U] = 1.0f;
                                bone.local_matrix_16[5U] = 1.0f;
                                bone.local_matrix_16[10U] = 1.0f;
                                bone.local_matrix_16[15U] = 1.0f;
                                if (joint_index >= 0 &&
                                    nodes_v != nullptr &&
                                    nodes_v->type == JsonValue::Type::Array &&
                                    static_cast<std::size_t>(joint_index) < nodes_v->array_value.size()) {
                                    const auto& node = nodes_v->array_value[static_cast<std::size_t>(joint_index)];
                                    if (node.type == JsonValue::Type::Object) {
                                        std::string node_name;
                                        if (TryGetString(node, "name", &node_name) && !node_name.empty()) {
                                            bone.bone_name = node_name;
                                        }
                                        const auto local_matrix = ReadNodeLocalMatrixOrIdentity(node);
                                        bone.local_matrix_16.assign(local_matrix.begin(), local_matrix.end());
                                    }
                                    auto humanoid_it = node_to_humanoid.find(static_cast<std::size_t>(joint_index));
                                    if (humanoid_it != node_to_humanoid.end()) {
                                        bone.humanoid_id = humanoid_it->second;
                                    }
                                    if (static_cast<std::size_t>(joint_index) < node_parent_indices.size()) {
                                        const auto parent_global = node_parent_indices[static_cast<std::size_t>(joint_index)];
                                        bone.parent_index = -1;
                                        if (parent_global != std::numeric_limits<std::size_t>::max()) {
                                            const auto parent_it = joint_index_to_local.find(static_cast<std::int32_t>(parent_global));
                                            if (parent_it != joint_index_to_local.end()) {
                                                bone.parent_index = static_cast<std::int32_t>(parent_it->second);
                                            }
                                        }
                                    }
                                }
                                rig_payload.bones.push_back(std::move(bone));
                            }
                            pkg.skeleton_rig_payloads.push_back(std::move(rig_payload));
                            emitted = true;
                            ++skinned_payload_emitted;
                        } else {
                            pkg.warnings.push_back(
                                "W_SKIN: VRM_SKIN_WEIGHT_READ_FAILED: mesh=" + mesh_payload.name + ", detail=" + skin_error);
                            pkg.warning_codes.push_back("VRM_SKIN_WEIGHT_READ_FAILED");
                        }
                    } else {
                        pkg.warnings.push_back(
                            "W_SKIN: VRM_SKIN_ATTRIBUTES_MISSING: mesh=" + mesh_payload.name);
                        pkg.warning_codes.push_back("VRM_SKIN_ATTRIBUTES_MISSING");
                    }
                } else {
                    pkg.warnings.push_back(
                        "W_SKIN: VRM_SKIN_DEF_MISSING: mesh=" + mesh_payload.name);
                    pkg.warning_codes.push_back("VRM_SKIN_DEF_MISSING");
                }
                if (!emitted) {
                    ++skinned_payload_failed;
                }
            }

            const auto* targets_v = FindKey(prim, "targets");
            if (prim_i == 0U && targets_v != nullptr && targets_v->type == JsonValue::Type::Array) {
                BlendShapeRenderPayload blendshape_payload;
                blendshape_payload.mesh_name = mesh_name;
                for (std::size_t target_i = 0U; target_i < targets_v->array_value.size(); ++target_i) {
                    const auto& target = targets_v->array_value[target_i];
                    if (target.type != JsonValue::Type::Object) {
                        continue;
                    }
                    const auto* pos_acc = FindKey(target, "POSITION");
                    if (pos_acc == nullptr || pos_acc->type != JsonValue::Type::Number) {
                        continue;
                    }
                    const std::size_t target_pos_accessor =
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
                            bin_chunk.bytes,
                            accessors,
                            views,
                            target_pos_accessor,
                            &frame.delta_vertices,
                            vtx_count,
                            &read_error)) {
                        pkg.warnings.push_back("W_PAYLOAD: VRM_BLENDSHAPE_READ_FAILED: mesh=" + mesh_name + ", detail=" + read_error);
                        continue;
                    }
                    blendshape_payload.frames.push_back(std::move(frame));
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
            }

            pkg.mesh_payloads.push_back(std::move(mesh_payload));
            pkg.meshes.push_back(
                MeshAssetSummary{
                    pkg.mesh_payloads.back().name,
                    vtx_count,
                    static_cast<std::uint32_t>(pkg.mesh_payloads.back().indices.size())});
            ++mesh_added;
        }
    }

    pkg.parser_stage = "payload";
    std::uint32_t transformed_mesh_count = 0U;
    for (std::size_t i = 0U; i < mesh_node_transform_applied.size(); ++i) {
        if (mesh_node_transform_applied[i]) {
            ++transformed_mesh_count;
        }
    }
    if (transformed_mesh_count > 0U) {
        std::string sample_applied_mesh = "unknown";
        for (std::size_t mesh_i = 0U; mesh_i < mesh_node_transform_applied.size(); ++mesh_i) {
            if (mesh_node_transform_applied[mesh_i] &&
                mesh_i < mesh_names_by_index.size()) {
                sample_applied_mesh = mesh_names_by_index[mesh_i];
                break;
            }
        }
        pkg.warnings.push_back(
            "W_NODE: VRM_NODE_TRANSFORM_APPLIED: meshes=" + std::to_string(transformed_mesh_count) +
            ", sampleMesh=" + sample_applied_mesh);
        pkg.warning_codes.push_back("VRM_NODE_TRANSFORM_APPLIED");
    }
    pkg.warnings.push_back("W_NODE: VRM_NODE_TRANSFORM_BASIS: global");
    if (skinned_primitive_count > 0U) {
        std::uint32_t skinned_transform_injected_mesh_count = 0U;
        std::uint32_t skinned_transform_conflict_mesh_count = 0U;
        for (std::size_t mesh_i = 0U; mesh_i < mesh_has_skin.size(); ++mesh_i) {
            if (!mesh_has_skin[mesh_i]) {
                continue;
            }
            if (mesh_i < mesh_node_transform_conflict.size() && mesh_node_transform_conflict[mesh_i]) {
                ++skinned_transform_conflict_mesh_count;
            } else if (mesh_i < mesh_node_transform_applied.size() && mesh_node_transform_applied[mesh_i]) {
                ++skinned_transform_injected_mesh_count;
            }
        }
        pkg.warnings.push_back(
            "W_SKIN: VRM_SKIN_PAYLOAD_STATUS: skinnedPrimitives=" + std::to_string(skinned_primitive_count) +
            ", emitted=" + std::to_string(skinned_payload_emitted) +
            ", failed=" + std::to_string(skinned_payload_failed));
        pkg.warnings.push_back(
            "W_SKIN: VRM_SKIN_NODE_TRANSFORM_STATUS: injectedMeshes=" +
            std::to_string(skinned_transform_injected_mesh_count) +
            ", conflictMeshes=" + std::to_string(skinned_transform_conflict_mesh_count));
        pkg.warnings.push_back("W_SKIN: VRM_SKIN_SPACE: mesh");
        pkg.warnings.push_back("W_SKIN: VRM_SKINNING_CONVENTION: globalJoint*inverseBind");
        if (skinned_transform_conflict_mesh_count > 0U) {
            pkg.warnings.push_back(
                "W_NODE: VRM_NODE_TRANSFORM_SKIN_FALLBACK: meshes=" +
                std::to_string(skinned_transform_conflict_mesh_count));
            pkg.warning_codes.push_back("VRM_NODE_TRANSFORM_SKIN_FALLBACK");
        }
        if (skinned_payload_failed > 0U) {
            pkg.warning_codes.push_back("VRM_SKIN_PAYLOAD_PARTIAL");
        }
    }
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
        MaterialDiagnosticsEntry default_material_diag;
        default_material_diag.material_name = "Default";
        pkg.material_diagnostics.push_back(std::move(default_material_diag));
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
    ParseVrmSpringBonePayloads(root, nodes_v, &pkg.physics_colliders, &pkg.springbone_payloads);
    if (pkg.springbone_summary.present) {
        pkg.warnings.push_back(
            "W_SPRINGBONE: parsed springbone metadata spring=" + std::to_string(pkg.springbone_summary.spring_count) +
            ", joint=" + std::to_string(pkg.springbone_summary.joint_count) +
            ", collider=" + std::to_string(pkg.springbone_summary.collider_count));
        if (!pkg.springbone_payloads.empty()) {
            pkg.warnings.push_back(
                "W_SPRINGBONE: extracted spring payloads spring=" + std::to_string(pkg.springbone_payloads.size()) +
                ", colliders=" + std::to_string(pkg.physics_colliders.size()));
            pkg.warnings.push_back("W_SPRINGBONE: runtime simulation payload-ready");
        } else {
            pkg.warnings.push_back("W_SPRINGBONE: metadata present but runtime payload extraction is partial");
            pkg.missing_features.push_back("SpringBone runtime simulation");
        }
    } else {
        pkg.missing_features.push_back("SpringBone metadata");
    }
    bool has_mtoon_binding = false;
    bool has_matcap_binding = false;
    bool has_matcap_declared = false;
    bool has_outline_binding = false;
    bool has_uv_anim_binding = false;
    std::uint32_t unresolved_matcap_materials = 0U;
    for (const auto& m : parsed_materials) {
        if (m.has_mtoon_binding) {
            has_mtoon_binding = true;
        }
        has_matcap_declared = has_matcap_declared || m.matcap_declared;
        has_matcap_binding = has_matcap_binding || m.has_matcap_binding;
        has_outline_binding = has_outline_binding || m.has_outline_binding;
        has_uv_anim_binding = has_uv_anim_binding || m.has_uv_anim_binding;
        if (m.matcap_declared && !m.has_matcap_binding) {
            ++unresolved_matcap_materials;
        }
    }
    if (!has_mtoon_binding) {
        pkg.missing_features.push_back("MToon advanced parameter binding");
    } else {
        pkg.warnings.push_back("W_MTOON: applied core material parameter binding");
        if (!has_outline_binding) {
            pkg.missing_features.push_back("MToon outline");
        }
        if (!has_uv_anim_binding) {
            pkg.missing_features.push_back("MToon uv animation");
        }
        if (has_matcap_declared && !has_matcap_binding) {
            pkg.warnings.push_back(
                "W_MTOON: VRM_MTOON_MATCAP_UNRESOLVED: materials=" + std::to_string(unresolved_matcap_materials));
            pkg.warning_codes.push_back("VRM_MTOON_MATCAP_UNRESOLVED");
        }
    }

    pkg.parser_stage = "runtime-ready";
    if (pkg.compat_level != AvatarCompatLevel::Partial) {
        pkg.compat_level = AvatarCompatLevel::Full;
        pkg.primary_error_code = "NONE";
    }
    pkg.warnings.push_back("W_STAGE: runtime-ready");
    return core::Result<AvatarPackage>::Ok(pkg);
}

}  // namespace vsfclone::avatar
