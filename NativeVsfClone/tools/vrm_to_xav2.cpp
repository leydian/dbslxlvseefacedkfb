#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "vsfclone/avatar/avatar_loader_facade.h"
#include "vsfclone/avatar/avatar_package.h"

namespace fs = std::filesystem;

namespace {

constexpr std::uint16_t kSectionTextureBlob = 0x0002U;
constexpr std::uint16_t kSectionMaterialOverride = 0x0003U;
constexpr std::uint16_t kSectionMeshRenderPayload = 0x0011U;
constexpr std::uint16_t kSectionMaterialShaderParams = 0x0012U;

void WriteU16Le(std::ofstream* out, std::uint16_t value) {
    const std::array<std::uint8_t, 2> bytes = {
        static_cast<std::uint8_t>(value & 0xFFU),
        static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
    };
    out->write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void WriteU32Le(std::ofstream* out, std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes = {
        static_cast<std::uint8_t>(value & 0xFFU),
        static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((value >> 24U) & 0xFFU),
    };
    out->write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void WriteI32Le(std::ofstream* out, std::int32_t value) {
    WriteU32Le(out, static_cast<std::uint32_t>(value));
}

bool WriteSizedString(std::ofstream* out, const std::string& s) {
    if (s.size() > 0xFFFFU) {
        return false;
    }
    WriteU16Le(out, static_cast<std::uint16_t>(s.size()));
    if (!s.empty()) {
        out->write(s.data(), static_cast<std::streamsize>(s.size()));
    }
    return out->good();
}

std::string EscapeJson(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 8U);
    for (char c : input) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

std::string BuildManifest(const vsfclone::avatar::AvatarPackage& pkg) {
    std::string out = "{";
    out += "\"schemaVersion\":1,";
    out += "\"exporterVersion\":\"0.2.0\",";
    out += "\"avatarId\":\"" + EscapeJson(fs::path(pkg.source_path).stem().string()) + "\",";
    out += "\"displayName\":\"" + EscapeJson(pkg.display_name) + "\",";
    out += "\"sourceExt\":\".vrm\",";
    out += "\"meshRefs\":[";
    for (std::size_t i = 0; i < pkg.mesh_payloads.size(); ++i) {
        if (i > 0U) {
            out += ",";
        }
        out += "\"" + EscapeJson(pkg.mesh_payloads[i].name) + "\"";
    }
    out += "],\"materialRefs\":[";
    for (std::size_t i = 0; i < pkg.material_payloads.size(); ++i) {
        if (i > 0U) {
            out += ",";
        }
        out += "\"" + EscapeJson(pkg.material_payloads[i].name) + "\"";
    }
    out += "],\"textureRefs\":[";
    for (std::size_t i = 0; i < pkg.texture_payloads.size(); ++i) {
        if (i > 0U) {
            out += ",";
        }
        out += "\"" + EscapeJson(pkg.texture_payloads[i].name) + "\"";
    }
    out += "],\"strictShaderSet\":[\"lilToon\",\"Poiyomi\",\"potatoon\",\"realtoon\"],";
    out += "\"hasSkinning\":false,\"hasBlendShapes\":false}";
    return out;
}

std::vector<std::uint8_t> BuildMeshRenderSection(const vsfclone::avatar::MeshRenderPayload& mesh) {
    std::vector<std::uint8_t> payload;
    const auto append_u16 = [&](std::uint16_t value) {
        payload.push_back(static_cast<std::uint8_t>(value & 0xFFU));
        payload.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    };
    const auto append_u32 = [&](std::uint32_t value) {
        payload.push_back(static_cast<std::uint8_t>(value & 0xFFU));
        payload.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
        payload.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
        payload.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    };
    const auto append_i32 = [&](std::int32_t value) {
        append_u32(static_cast<std::uint32_t>(value));
    };
    append_u16(static_cast<std::uint16_t>(mesh.name.size()));
    payload.insert(payload.end(), mesh.name.begin(), mesh.name.end());
    append_u32(mesh.vertex_stride);
    append_i32(mesh.material_index);
    append_u32(static_cast<std::uint32_t>(mesh.vertex_blob.size()));
    payload.insert(payload.end(), mesh.vertex_blob.begin(), mesh.vertex_blob.end());
    append_u32(static_cast<std::uint32_t>(mesh.indices.size()));
    for (std::uint32_t idx : mesh.indices) {
        append_u32(idx);
    }
    return payload;
}

std::vector<std::uint8_t> BuildTextureSection(const vsfclone::avatar::TextureRenderPayload& tex) {
    std::vector<std::uint8_t> payload;
    const auto append_u16 = [&](std::uint16_t value) {
        payload.push_back(static_cast<std::uint8_t>(value & 0xFFU));
        payload.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    };
    const auto append_u32 = [&](std::uint32_t value) {
        payload.push_back(static_cast<std::uint8_t>(value & 0xFFU));
        payload.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
        payload.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
        payload.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    };
    append_u16(static_cast<std::uint16_t>(tex.name.size()));
    payload.insert(payload.end(), tex.name.begin(), tex.name.end());
    append_u32(static_cast<std::uint32_t>(tex.bytes.size()));
    payload.insert(payload.end(), tex.bytes.begin(), tex.bytes.end());
    return payload;
}

std::vector<std::uint8_t> BuildMaterialSection(const vsfclone::avatar::MaterialRenderPayload& mat) {
    std::vector<std::uint8_t> payload;
    const auto append_u16 = [&](std::uint16_t value) {
        payload.push_back(static_cast<std::uint8_t>(value & 0xFFU));
        payload.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    };
    const auto append_u32 = [&](std::uint32_t value) {
        payload.push_back(static_cast<std::uint8_t>(value & 0xFFU));
        payload.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
        payload.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
        payload.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    };
    auto append_sized = [&](const std::string& s) {
        append_u16(static_cast<std::uint16_t>(s.size()));
        payload.insert(payload.end(), s.begin(), s.end());
    };
    append_sized(mat.name);
    append_sized(mat.shader_name);
    append_sized(mat.shader_variant.empty() ? "default" : mat.shader_variant);
    append_sized(mat.base_color_texture_name);
    append_sized(mat.alpha_mode.empty() ? "OPAQUE" : mat.alpha_mode);
    std::uint32_t bits = 0U;
    static_assert(sizeof(bits) == sizeof(mat.alpha_cutoff));
    std::memcpy(&bits, &mat.alpha_cutoff, sizeof(bits));
    append_u32(bits);
    payload.push_back(mat.double_sided ? 1U : 0U);
    return payload;
}

std::vector<std::uint8_t> BuildMaterialParamsSection(const vsfclone::avatar::MaterialRenderPayload& mat) {
    std::vector<std::uint8_t> payload;
    const auto append_u16 = [&](std::uint16_t value) {
        payload.push_back(static_cast<std::uint8_t>(value & 0xFFU));
        payload.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    };
    auto append_sized = [&](const std::string& s) {
        append_u16(static_cast<std::uint16_t>(s.size()));
        payload.insert(payload.end(), s.begin(), s.end());
    };
    append_sized(mat.name);
    append_sized(mat.shader_params_json.empty() ? "{}" : mat.shader_params_json);
    return payload;
}

bool WriteSection(std::ofstream* out, std::uint16_t type, std::uint16_t flags, const std::vector<std::uint8_t>& payload) {
    WriteU16Le(out, type);
    WriteU16Le(out, flags);
    WriteU32Le(out, static_cast<std::uint32_t>(payload.size()));
    if (!payload.empty()) {
        out->write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    }
    return out->good();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: vrm_to_xav2 <input.vrm> <output.xav2>\n";
        return 1;
    }
    const std::string input_path = argv[1];
    const std::string output_path = argv[2];

    vsfclone::avatar::AvatarLoaderFacade facade;
    auto loaded = facade.Load(input_path);
    if (!loaded.ok) {
        std::cerr << "Load failed: " << loaded.error << "\n";
        return 1;
    }
    const auto& pkg = loaded.value;
    if (pkg.source_type != vsfclone::avatar::AvatarSourceType::Vrm) {
        std::cerr << "Input must be a .vrm file.\n";
        return 1;
    }
    if (pkg.compat_level == vsfclone::avatar::AvatarCompatLevel::Failed) {
        std::cerr << "VRM parse failed with code: " << pkg.primary_error_code << "\n";
        return 1;
    }

    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        std::cerr << "Could not open output file: " << output_path << "\n";
        return 1;
    }

    const std::string manifest = BuildManifest(pkg);
    out.write("XAV2", 4);
    WriteU16Le(&out, 1U);
    WriteU32Le(&out, static_cast<std::uint32_t>(manifest.size()));
    out.write(manifest.data(), static_cast<std::streamsize>(manifest.size()));

    for (const auto& mesh : pkg.mesh_payloads) {
        if (mesh.name.size() > 0xFFFFU || mesh.indices.size() > 0xFFFFFFFFU || mesh.vertex_blob.size() > 0xFFFFFFFFU) {
            std::cerr << "Mesh payload too large: " << mesh.name << "\n";
            return 1;
        }
        if (!WriteSection(&out, kSectionMeshRenderPayload, 0U, BuildMeshRenderSection(mesh))) {
            std::cerr << "Failed to write mesh section.\n";
            return 1;
        }
    }
    for (const auto& tex : pkg.texture_payloads) {
        if (tex.name.size() > 0xFFFFU || tex.bytes.size() > 0xFFFFFFFFU) {
            std::cerr << "Texture payload too large: " << tex.name << "\n";
            return 1;
        }
        if (!WriteSection(&out, kSectionTextureBlob, 0U, BuildTextureSection(tex))) {
            std::cerr << "Failed to write texture section.\n";
            return 1;
        }
    }
    for (const auto& mat : pkg.material_payloads) {
        if (mat.name.size() > 0xFFFFU || mat.shader_name.size() > 0xFFFFU || mat.base_color_texture_name.size() > 0xFFFFU ||
            mat.alpha_mode.size() > 0xFFFFU || mat.shader_params_json.size() > 0xFFFFU) {
            std::cerr << "Material payload too large: " << mat.name << "\n";
            return 1;
        }
        if (!WriteSection(&out, kSectionMaterialOverride, 0U, BuildMaterialSection(mat))) {
            std::cerr << "Failed to write material section.\n";
            return 1;
        }
        if (!WriteSection(&out, kSectionMaterialShaderParams, 0U, BuildMaterialParamsSection(mat))) {
            std::cerr << "Failed to write material params section.\n";
            return 1;
        }
    }

    if (!out.good()) {
        std::cerr << "Write failed.\n";
        return 1;
    }

    std::cout << "Wrote XAV2: " << output_path << "\n";
    std::cout << "Meshes=" << pkg.mesh_payloads.size() << ", Materials=" << pkg.material_payloads.size()
              << ", Textures=" << pkg.texture_payloads.size() << "\n";
    return 0;
}
