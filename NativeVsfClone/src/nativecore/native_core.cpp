#include "vsfclone/nativecore/api.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "vsfclone/avatar/avatar_loader_facade.h"
#include "vsfclone/avatar/avatar_package.h"
#include "vsfclone/osc/osc_endpoint.h"
#include "vsfclone/stream/spout_sender.h"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <dxgi.h>
#include <wincodec.h>
#endif

namespace vsfclone::nativecore {

namespace {

using vsfclone::avatar::AvatarCompatLevel;
using vsfclone::avatar::AvatarPackage;
using vsfclone::avatar::AvatarSourceType;

#if defined(_WIN32)
struct WindowRenderState {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* device_context = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct GpuMeshResource {
    ID3D11Buffer* vertex_buffer = nullptr;
    ID3D11Buffer* index_buffer = nullptr;
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
    std::int32_t material_index = -1;
    DirectX::XMFLOAT3 center = {0.0f, 0.0f, 0.0f};
    std::uint32_t vertex_stride = 12;
    DirectX::XMFLOAT3 bounds_min = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 bounds_max = {0.0f, 0.0f, 0.0f};
};

struct GpuMaterialResource {
    std::string alpha_mode = "OPAQUE";
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
    ID3D11ShaderResourceView* base_color_srv = nullptr;
};

struct RendererResources {
    ID3D11Device* device = nullptr;
    ID3D11VertexShader* vertex_shader = nullptr;
    ID3D11PixelShader* pixel_shader = nullptr;
    ID3D11InputLayout* input_layout = nullptr;
    ID3D11Buffer* constant_buffer = nullptr;
    ID3D11RasterizerState* raster_cull_back = nullptr;
    ID3D11RasterizerState* raster_cull_none = nullptr;
    ID3D11DepthStencilState* depth_write = nullptr;
    ID3D11DepthStencilState* depth_read = nullptr;
    ID3D11BlendState* blend_opaque = nullptr;
    ID3D11BlendState* blend_alpha = nullptr;
    ID3D11SamplerState* linear_sampler = nullptr;
    ID3D11Texture2D* depth_texture = nullptr;
    ID3D11DepthStencilView* depth_dsv = nullptr;
    std::uint32_t depth_width = 0;
    std::uint32_t depth_height = 0;
    std::unordered_map<std::uint64_t, std::vector<GpuMeshResource>> avatar_meshes;
    std::unordered_map<std::uint64_t, std::vector<GpuMaterialResource>> avatar_materials;
};
#endif

struct CoreState {
    bool initialized = false;
    std::uint64_t next_avatar_handle = 1;
    std::unordered_map<std::uint64_t, AvatarPackage> avatars;
    std::unordered_set<std::uint64_t> render_ready_avatars;
    avatar::AvatarLoaderFacade loader;
    stream::SpoutSender spout;
    osc::OscEndpoint osc;
    NcTrackingFrame latest_tracking {};
    float last_frame_ms = 0.0f;

#if defined(_WIN32)
    std::unordered_map<void*, WindowRenderState> window_targets;
    RendererResources renderer;
#endif

    NcResultCode last_error_code = NC_OK;
    std::string last_error_subsystem = "none";
    std::string last_error_message;
    bool last_error_recoverable = true;
};

CoreState g_state;
std::mutex g_mutex;

void CopyString(char* dst, std::size_t dst_size, const std::string& src) {
    if (dst == nullptr || dst_size == 0U) {
        return;
    }
    const std::size_t count = std::min(dst_size - 1U, src.size());
    std::memcpy(dst, src.data(), count);
    dst[count] = '\0';
}

void SetError(NcResultCode code, const char* subsystem, const std::string& message, bool recoverable) {
    g_state.last_error_code = code;
    g_state.last_error_subsystem = subsystem != nullptr ? subsystem : "unknown";
    g_state.last_error_message = message;
    g_state.last_error_recoverable = recoverable;
}

void ClearError() {
    SetError(NC_OK, "none", "", true);
}

NcAvatarFormatHint ToFormatHint(AvatarSourceType source_type) {
    switch (source_type) {
        case AvatarSourceType::Vrm:
            return NC_AVATAR_FORMAT_VRM;
        case AvatarSourceType::VxAvatar:
            return NC_AVATAR_FORMAT_VXAVATAR;
        case AvatarSourceType::Vxa2:
            return NC_AVATAR_FORMAT_VXA2;
        case AvatarSourceType::Xav2:
            return NC_AVATAR_FORMAT_XAV2;
        case AvatarSourceType::VsfAvatar:
            return NC_AVATAR_FORMAT_VSFAVATAR;
        default:
            return NC_AVATAR_FORMAT_AUTO;
    }
}

NcCompatLevel ToCompatLevel(AvatarCompatLevel compat_level) {
    switch (compat_level) {
        case AvatarCompatLevel::Full:
            return NC_COMPAT_FULL;
        case AvatarCompatLevel::Partial:
            return NC_COMPAT_PARTIAL;
        case AvatarCompatLevel::Failed:
            return NC_COMPAT_FAILED;
        default:
            return NC_COMPAT_UNKNOWN;
    }
}

void FillAvatarInfo(const AvatarPackage& pkg, std::uint64_t handle, NcAvatarInfo* out_info) {
    if (out_info == nullptr) {
        return;
    }

    std::memset(out_info, 0, sizeof(*out_info));
    out_info->handle = handle;
    out_info->detected_format = ToFormatHint(pkg.source_type);
    out_info->compat_level = ToCompatLevel(pkg.compat_level);
    out_info->mesh_count = static_cast<std::uint32_t>(pkg.meshes.size());
    out_info->material_count = static_cast<std::uint32_t>(pkg.materials.size());
    out_info->mesh_payload_count = static_cast<std::uint32_t>(pkg.mesh_payloads.size());
    out_info->material_payload_count = static_cast<std::uint32_t>(pkg.material_payloads.size());
    out_info->texture_payload_count = static_cast<std::uint32_t>(pkg.texture_payloads.size());
    out_info->format_section_count = pkg.format_section_count;
    out_info->format_decoded_section_count = pkg.format_decoded_section_count;
    out_info->format_unknown_section_count = pkg.format_unknown_section_count;
    out_info->warning_count = static_cast<std::uint32_t>(pkg.warnings.size());
    out_info->missing_feature_count = static_cast<std::uint32_t>(pkg.missing_features.size());
    out_info->expression_count = static_cast<std::uint32_t>(pkg.expressions.size());
    if (out_info->detected_format == NC_AVATAR_FORMAT_VRM && out_info->expression_count == 0U) {
        out_info->expression_count = 3U;
    }
    out_info->last_render_draw_calls = pkg.last_render_draw_calls;
    CopyString(out_info->display_name, sizeof(out_info->display_name), pkg.display_name);
    CopyString(out_info->source_path, sizeof(out_info->source_path), pkg.source_path);
    CopyString(
        out_info->parser_stage,
        sizeof(out_info->parser_stage),
        pkg.parser_stage.empty() ? "unknown" : pkg.parser_stage);
    CopyString(
        out_info->primary_error_code,
        sizeof(out_info->primary_error_code),
        pkg.primary_error_code.empty() ? "NONE" : pkg.primary_error_code);
    const std::string expression_summary =
        (!pkg.last_expression_summary.empty())
            ? pkg.last_expression_summary
            : (out_info->detected_format == NC_AVATAR_FORMAT_VRM ? "blink=0.000000, aa=0.000000, joy=0.000000" : "");
    CopyString(
        out_info->last_expression_summary,
        sizeof(out_info->last_expression_summary),
        expression_summary);
    if (!pkg.warnings.empty()) {
        CopyString(out_info->last_warning, sizeof(out_info->last_warning), pkg.warnings.back());
    }
    if (!pkg.missing_features.empty()) {
        CopyString(out_info->last_missing_feature, sizeof(out_info->last_missing_feature), pkg.missing_features.back());
    }
}

bool EnsureInitialized() {
    if (g_state.initialized) {
        return true;
    }
    SetError(NC_ERROR_NOT_INITIALIZED, "runtime", "native core is not initialized", true);
    return false;
}

#if defined(_WIN32)
void ReleaseWindowState(WindowRenderState* state) {
    if (state == nullptr) {
        return;
    }
    if (state->rtv != nullptr) {
        state->rtv->Release();
        state->rtv = nullptr;
    }
    if (state->swap_chain != nullptr) {
        state->swap_chain->Release();
        state->swap_chain = nullptr;
    }
    if (state->device_context != nullptr) {
        state->device_context->Release();
        state->device_context = nullptr;
    }
    if (state->device != nullptr) {
        state->device->Release();
        state->device = nullptr;
    }
    state->width = 0;
    state->height = 0;
}

void ReleaseGpuMeshResource(GpuMeshResource* mesh) {
    if (mesh == nullptr) {
        return;
    }
    if (mesh->vertex_buffer != nullptr) {
        mesh->vertex_buffer->Release();
        mesh->vertex_buffer = nullptr;
    }
    if (mesh->index_buffer != nullptr) {
        mesh->index_buffer->Release();
        mesh->index_buffer = nullptr;
    }
    mesh->vertex_count = 0;
    mesh->index_count = 0;
    mesh->material_index = -1;
    mesh->center = {0.0f, 0.0f, 0.0f};
    mesh->bounds_min = {0.0f, 0.0f, 0.0f};
    mesh->bounds_max = {0.0f, 0.0f, 0.0f};
}

void ReleaseGpuMaterialResource(GpuMaterialResource* material) {
    if (material == nullptr) {
        return;
    }
    if (material->base_color_srv != nullptr) {
        material->base_color_srv->Release();
        material->base_color_srv = nullptr;
    }
    material->alpha_mode = "OPAQUE";
    material->alpha_cutoff = 0.5f;
    material->double_sided = false;
}

void ResetRendererResources(RendererResources* renderer) {
    if (renderer == nullptr) {
        return;
    }
    for (auto& [_, meshes] : renderer->avatar_meshes) {
        for (auto& mesh : meshes) {
            ReleaseGpuMeshResource(&mesh);
        }
    }
    renderer->avatar_meshes.clear();
    for (auto& [_, materials] : renderer->avatar_materials) {
        for (auto& material : materials) {
            ReleaseGpuMaterialResource(&material);
        }
    }
    renderer->avatar_materials.clear();
    if (renderer->depth_dsv != nullptr) {
        renderer->depth_dsv->Release();
        renderer->depth_dsv = nullptr;
    }
    if (renderer->depth_texture != nullptr) {
        renderer->depth_texture->Release();
        renderer->depth_texture = nullptr;
    }
    if (renderer->blend_alpha != nullptr) {
        renderer->blend_alpha->Release();
        renderer->blend_alpha = nullptr;
    }
    if (renderer->linear_sampler != nullptr) {
        renderer->linear_sampler->Release();
        renderer->linear_sampler = nullptr;
    }
    if (renderer->blend_opaque != nullptr) {
        renderer->blend_opaque->Release();
        renderer->blend_opaque = nullptr;
    }
    if (renderer->depth_read != nullptr) {
        renderer->depth_read->Release();
        renderer->depth_read = nullptr;
    }
    if (renderer->depth_write != nullptr) {
        renderer->depth_write->Release();
        renderer->depth_write = nullptr;
    }
    if (renderer->raster_cull_none != nullptr) {
        renderer->raster_cull_none->Release();
        renderer->raster_cull_none = nullptr;
    }
    if (renderer->raster_cull_back != nullptr) {
        renderer->raster_cull_back->Release();
        renderer->raster_cull_back = nullptr;
    }
    if (renderer->constant_buffer != nullptr) {
        renderer->constant_buffer->Release();
        renderer->constant_buffer = nullptr;
    }
    if (renderer->input_layout != nullptr) {
        renderer->input_layout->Release();
        renderer->input_layout = nullptr;
    }
    if (renderer->pixel_shader != nullptr) {
        renderer->pixel_shader->Release();
        renderer->pixel_shader = nullptr;
    }
    if (renderer->vertex_shader != nullptr) {
        renderer->vertex_shader->Release();
        renderer->vertex_shader = nullptr;
    }
    if (renderer->device != nullptr) {
        renderer->device->Release();
        renderer->device = nullptr;
    }
    renderer->depth_width = 0U;
    renderer->depth_height = 0U;
}

bool EnsureDepthResources(RendererResources* renderer, ID3D11Device* device, std::uint32_t width, std::uint32_t height) {
    if (renderer == nullptr || device == nullptr || width == 0U || height == 0U) {
        return false;
    }
    if (renderer->depth_texture != nullptr && renderer->depth_dsv != nullptr &&
        renderer->depth_width == width && renderer->depth_height == height) {
        return true;
    }
    if (renderer->depth_dsv != nullptr) {
        renderer->depth_dsv->Release();
        renderer->depth_dsv = nullptr;
    }
    if (renderer->depth_texture != nullptr) {
        renderer->depth_texture->Release();
        renderer->depth_texture = nullptr;
    }

    D3D11_TEXTURE2D_DESC depth_desc {};
    depth_desc.Width = width;
    depth_desc.Height = height;
    depth_desc.MipLevels = 1U;
    depth_desc.ArraySize = 1U;
    depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depth_desc.SampleDesc.Count = 1U;
    depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(device->CreateTexture2D(&depth_desc, nullptr, &renderer->depth_texture)) || renderer->depth_texture == nullptr) {
        return false;
    }
    if (FAILED(device->CreateDepthStencilView(renderer->depth_texture, nullptr, &renderer->depth_dsv)) || renderer->depth_dsv == nullptr) {
        return false;
    }
    renderer->depth_width = width;
    renderer->depth_height = height;
    return true;
}

bool EnsurePipelineResources(RendererResources* renderer, ID3D11Device* device) {
    if (renderer == nullptr || device == nullptr) {
        return false;
    }
    if (renderer->device != nullptr && renderer->device != device) {
        ResetRendererResources(renderer);
    }
    if (renderer->device == nullptr) {
        renderer->device = device;
        renderer->device->AddRef();
    }
    if (renderer->vertex_shader != nullptr && renderer->pixel_shader != nullptr &&
        renderer->input_layout != nullptr && renderer->constant_buffer != nullptr &&
        renderer->raster_cull_back != nullptr && renderer->raster_cull_none != nullptr &&
        renderer->depth_write != nullptr && renderer->depth_read != nullptr &&
        renderer->blend_opaque != nullptr && renderer->blend_alpha != nullptr &&
        renderer->linear_sampler != nullptr) {
        return true;
    }

    constexpr char kVertexShaderSrc[] =
        "cbuffer SceneCB : register(b0) {\n"
        "  float4x4 world_view_proj;\n"
        "  float4 base_color;\n"
        "  float alpha_cutoff;\n"
        "  float alpha_mode_mask;\n"
        "  float has_texture;\n"
        "  float _pad;\n"
        "};\n"
        "struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; };\n"
        "struct VSOut { float4 pos : SV_POSITION; float4 color : COLOR0; float2 uv : TEXCOORD0; };\n"
        "VSOut main(VSIn i) {\n"
        "  VSOut o;\n"
        "  o.pos = mul(float4(i.pos, 1.0), world_view_proj);\n"
        "  o.color = base_color;\n"
        "  o.uv = i.uv;\n"
        "  return o;\n"
        "}\n";
    constexpr char kPixelShaderSrc[] =
        "cbuffer SceneCB : register(b0) {\n"
        "  float4x4 world_view_proj;\n"
        "  float4 base_color;\n"
        "  float alpha_cutoff;\n"
        "  float alpha_mode_mask;\n"
        "  float has_texture;\n"
        "  float _pad;\n"
        "};\n"
        "Texture2D tex0 : register(t0);\n"
        "SamplerState samp0 : register(s0);\n"
        "float4 main(float4 pos : SV_POSITION, float4 color : COLOR0, float2 uv : TEXCOORD0) : SV_TARGET {\n"
        "  float4 out_color = color;\n"
        "  if (has_texture > 0.5) {\n"
        "    float4 texel = tex0.Sample(samp0, uv);\n"
        "    out_color.rgb *= texel.rgb;\n"
        "    if (alpha_mode_mask > 0.5) {\n"
        "      out_color.a *= texel.a;\n"
        "    }\n"
        "  }\n"
        "  if (alpha_mode_mask > 0.5 && out_color.a < alpha_cutoff) {\n"
        "    clip(-1.0);\n"
        "  }\n"
        "  return out_color;\n"
        "}\n";

    ID3DBlob* vs_blob = nullptr;
    ID3DBlob* ps_blob = nullptr;
    ID3DBlob* err_blob = nullptr;
    HRESULT hr = D3DCompile(
        kVertexShaderSrc,
        sizeof(kVertexShaderSrc) - 1U,
        nullptr,
        nullptr,
        nullptr,
        "main",
        "vs_5_0",
        0U,
        0U,
        &vs_blob,
        &err_blob);
    if (FAILED(hr) || vs_blob == nullptr) {
        if (err_blob != nullptr) {
            err_blob->Release();
        }
        return false;
    }
    if (err_blob != nullptr) {
        err_blob->Release();
        err_blob = nullptr;
    }
    hr = D3DCompile(
        kPixelShaderSrc,
        sizeof(kPixelShaderSrc) - 1U,
        nullptr,
        nullptr,
        nullptr,
        "main",
        "ps_5_0",
        0U,
        0U,
        &ps_blob,
        &err_blob);
    if (FAILED(hr) || ps_blob == nullptr) {
        if (vs_blob != nullptr) {
            vs_blob->Release();
        }
        if (err_blob != nullptr) {
            err_blob->Release();
        }
        return false;
    }
    if (err_blob != nullptr) {
        err_blob->Release();
        err_blob = nullptr;
    }

    hr = device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &renderer->vertex_shader);
    if (FAILED(hr) || renderer->vertex_shader == nullptr) {
        vs_blob->Release();
        ps_blob->Release();
        return false;
    }
    hr = device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &renderer->pixel_shader);
    if (FAILED(hr) || renderer->pixel_shader == nullptr) {
        vs_blob->Release();
        ps_blob->Release();
        return false;
    }

    const D3D11_INPUT_ELEMENT_DESC input_desc[] = {
        {"POSITION", 0U, DXGI_FORMAT_R32G32B32_FLOAT, 0U, 0U, D3D11_INPUT_PER_VERTEX_DATA, 0U},
        {"TEXCOORD", 0U, DXGI_FORMAT_R32G32_FLOAT, 0U, 12U, D3D11_INPUT_PER_VERTEX_DATA, 0U},
    };
    hr = device->CreateInputLayout(
        input_desc,
        2U,
        vs_blob->GetBufferPointer(),
        vs_blob->GetBufferSize(),
        &renderer->input_layout);
    vs_blob->Release();
    ps_blob->Release();
    if (FAILED(hr) || renderer->input_layout == nullptr) {
        return false;
    }

    struct alignas(16) SceneConstants {
        float world_view_proj[16];
        float base_color[4];
        float alpha_cutoff;
        float alpha_mode_mask;
        float has_texture;
        float _pad;
    };
    D3D11_BUFFER_DESC cb_desc {};
    cb_desc.ByteWidth = static_cast<UINT>(sizeof(SceneConstants));
    cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&cb_desc, nullptr, &renderer->constant_buffer);
    if (FAILED(hr) || renderer->constant_buffer == nullptr) {
        return false;
    }

    D3D11_RASTERIZER_DESC raster_desc {};
    raster_desc.FillMode = D3D11_FILL_SOLID;
    raster_desc.CullMode = D3D11_CULL_BACK;
    raster_desc.DepthClipEnable = TRUE;
    hr = device->CreateRasterizerState(&raster_desc, &renderer->raster_cull_back);
    if (FAILED(hr) || renderer->raster_cull_back == nullptr) {
        return false;
    }
    raster_desc.CullMode = D3D11_CULL_NONE;
    hr = device->CreateRasterizerState(&raster_desc, &renderer->raster_cull_none);
    if (FAILED(hr) || renderer->raster_cull_none == nullptr) {
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC depth_desc {};
    depth_desc.DepthEnable = TRUE;
    depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth_desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    hr = device->CreateDepthStencilState(&depth_desc, &renderer->depth_write);
    if (FAILED(hr) || renderer->depth_write == nullptr) {
        return false;
    }
    depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    hr = device->CreateDepthStencilState(&depth_desc, &renderer->depth_read);
    if (FAILED(hr) || renderer->depth_read == nullptr) {
        return false;
    }

    D3D11_BLEND_DESC blend_desc {};
    blend_desc.RenderTarget[0].BlendEnable = FALSE;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = device->CreateBlendState(&blend_desc, &renderer->blend_opaque);
    if (FAILED(hr) || renderer->blend_opaque == nullptr) {
        return false;
    }

    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    hr = device->CreateBlendState(&blend_desc, &renderer->blend_alpha);
    if (FAILED(hr) || renderer->blend_alpha == nullptr) {
        return false;
    }
    D3D11_SAMPLER_DESC sampler_desc {};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.MaxAnisotropy = 1U;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler_desc.MinLOD = 0.0f;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&sampler_desc, &renderer->linear_sampler);
    if (FAILED(hr) || renderer->linear_sampler == nullptr) {
        return false;
    }
    return true;
}

bool BuildGpuMeshForPayload(const avatar::MeshRenderPayload& payload, ID3D11Device* device, GpuMeshResource* out_mesh) {
    if (device == nullptr || out_mesh == nullptr || payload.vertex_blob.empty() || payload.indices.empty()) {
        return false;
    }
    const std::uint32_t src_stride = payload.vertex_stride >= 12U ? payload.vertex_stride : 12U;
    if ((payload.vertex_blob.size() % src_stride) != 0U) {
        return false;
    }
    const std::uint32_t vertex_count = static_cast<std::uint32_t>(payload.vertex_blob.size() / src_stride);
    const std::uint32_t index_count = static_cast<std::uint32_t>(payload.indices.size());

    std::vector<std::uint8_t> gpu_vertex_blob;
    gpu_vertex_blob.reserve(static_cast<std::size_t>(vertex_count) * 20U);
    const auto* src = payload.vertex_blob.data();
    for (std::uint32_t i = 0U; i < vertex_count; ++i) {
        const std::size_t base = static_cast<std::size_t>(i) * src_stride;
        gpu_vertex_blob.insert(gpu_vertex_blob.end(), src + base, src + base + 12U);
        if (src_stride >= 20U) {
            gpu_vertex_blob.insert(gpu_vertex_blob.end(), src + base + 12U, src + base + 20U);
        } else {
            const std::array<float, 2U> uv_zero = {0.0f, 0.0f};
            const auto* uv_bytes = reinterpret_cast<const std::uint8_t*>(uv_zero.data());
            gpu_vertex_blob.insert(gpu_vertex_blob.end(), uv_bytes, uv_bytes + 8U);
        }
    }

    D3D11_BUFFER_DESC vb_desc {};
    vb_desc.ByteWidth = static_cast<UINT>(gpu_vertex_blob.size());
    vb_desc.Usage = D3D11_USAGE_DEFAULT;
    vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vb_data {};
    vb_data.pSysMem = gpu_vertex_blob.data();

    D3D11_BUFFER_DESC ib_desc {};
    ib_desc.ByteWidth = static_cast<UINT>(payload.indices.size() * sizeof(std::uint32_t));
    ib_desc.Usage = D3D11_USAGE_DEFAULT;
    ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ib_data {};
    ib_data.pSysMem = payload.indices.data();

    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    if (FAILED(device->CreateBuffer(&vb_desc, &vb_data, &vb)) || vb == nullptr) {
        return false;
    }
    if (FAILED(device->CreateBuffer(&ib_desc, &ib_data, &ib)) || ib == nullptr) {
        vb->Release();
        return false;
    }

    DirectX::XMFLOAT3 center = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 bmin = {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()};
    DirectX::XMFLOAT3 bmax = {
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max()};
    const auto* bytes = payload.vertex_blob.data();
    for (std::uint32_t i = 0U; i < vertex_count; ++i) {
        const std::size_t base = static_cast<std::size_t>(i) * src_stride;
        float px = 0.0f;
        float py = 0.0f;
        float pz = 0.0f;
        std::memcpy(&px, bytes + base, sizeof(float));
        std::memcpy(&py, bytes + base + 4U, sizeof(float));
        std::memcpy(&pz, bytes + base + 8U, sizeof(float));
        center.x += px;
        center.y += py;
        center.z += pz;
        bmin.x = std::min(bmin.x, px);
        bmin.y = std::min(bmin.y, py);
        bmin.z = std::min(bmin.z, pz);
        bmax.x = std::max(bmax.x, px);
        bmax.y = std::max(bmax.y, py);
        bmax.z = std::max(bmax.z, pz);
    }
    const float inv_count = vertex_count > 0U ? (1.0f / static_cast<float>(vertex_count)) : 1.0f;
    center.x *= inv_count;
    center.y *= inv_count;
    center.z *= inv_count;

    out_mesh->vertex_buffer = vb;
    out_mesh->index_buffer = ib;
    out_mesh->vertex_count = vertex_count;
    out_mesh->index_count = index_count;
    out_mesh->material_index = payload.material_index;
    out_mesh->center = center;
    out_mesh->vertex_stride = 20U;
    out_mesh->bounds_min = bmin;
    out_mesh->bounds_max = bmax;
    return true;
}

bool EnsureAvatarGpuMeshes(RendererResources* renderer, const AvatarPackage& avatar_pkg, std::uint64_t handle, ID3D11Device* device) {
    if (renderer == nullptr || device == nullptr) {
        return false;
    }
    if (renderer->avatar_meshes.find(handle) != renderer->avatar_meshes.end()) {
        return true;
    }
    std::vector<GpuMeshResource> meshes;
    meshes.reserve(avatar_pkg.mesh_payloads.size());
    for (const auto& payload : avatar_pkg.mesh_payloads) {
        GpuMeshResource mesh {};
        if (!BuildGpuMeshForPayload(payload, device, &mesh)) {
            for (auto& created : meshes) {
                ReleaseGpuMeshResource(&created);
            }
            return false;
        }
        meshes.push_back(mesh);
    }
    renderer->avatar_meshes[handle] = std::move(meshes);
    return true;
}

bool DecodeImageWithWic(
    const std::vector<std::uint8_t>& encoded,
    std::vector<std::uint8_t>* out_rgba,
    std::uint32_t* out_width,
    std::uint32_t* out_height) {
    if (encoded.empty() || out_rgba == nullptr || out_width == nullptr || out_height == nullptr) {
        return false;
    }
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_initialized = SUCCEEDED(hr);

    IWICImagingFactory* factory = nullptr;
    hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr) || factory == nullptr) {
        if (com_initialized) {
            CoUninitialize();
        }
        return false;
    }
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    bool ok = false;
    do {
        hr = factory->CreateStream(&stream);
        if (FAILED(hr) || stream == nullptr) {
            break;
        }
        hr = stream->InitializeFromMemory(const_cast<BYTE*>(encoded.data()), static_cast<DWORD>(encoded.size()));
        if (FAILED(hr)) {
            break;
        }
        hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
        if (FAILED(hr) || decoder == nullptr) {
            break;
        }
        hr = decoder->GetFrame(0U, &frame);
        if (FAILED(hr) || frame == nullptr) {
            break;
        }
        hr = frame->GetSize(reinterpret_cast<UINT*>(out_width), reinterpret_cast<UINT*>(out_height));
        if (FAILED(hr) || *out_width == 0U || *out_height == 0U) {
            break;
        }
        hr = factory->CreateFormatConverter(&converter);
        if (FAILED(hr) || converter == nullptr) {
            break;
        }
        hr = converter->Initialize(
            frame,
            GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) {
            break;
        }
        out_rgba->assign(static_cast<std::size_t>(*out_width) * static_cast<std::size_t>(*out_height) * 4U, 0U);
        hr = converter->CopyPixels(
            nullptr,
            (*out_width) * 4U,
            static_cast<UINT>(out_rgba->size()),
            out_rgba->data());
        if (FAILED(hr)) {
            out_rgba->clear();
            break;
        }
        ok = true;
    } while (false);

    if (converter != nullptr) {
        converter->Release();
    }
    if (frame != nullptr) {
        frame->Release();
    }
    if (decoder != nullptr) {
        decoder->Release();
    }
    if (stream != nullptr) {
        stream->Release();
    }
    if (factory != nullptr) {
        factory->Release();
    }
    if (com_initialized) {
        CoUninitialize();
    }
    return ok;
}

ID3D11ShaderResourceView* CreateTextureSrvFromPayload(ID3D11Device* device, const avatar::TextureRenderPayload* payload) {
    if (device == nullptr || payload == nullptr || payload->bytes.empty()) {
        return nullptr;
    }
    std::vector<std::uint8_t> bgra;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    if (!DecodeImageWithWic(payload->bytes, &bgra, &width, &height)) {
        return nullptr;
    }
    D3D11_TEXTURE2D_DESC tex_desc {};
    tex_desc.Width = width;
    tex_desc.Height = height;
    tex_desc.MipLevels = 1U;
    tex_desc.ArraySize = 1U;
    tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    tex_desc.SampleDesc.Count = 1U;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA tex_data {};
    tex_data.pSysMem = bgra.data();
    tex_data.SysMemPitch = width * 4U;
    ID3D11Texture2D* texture = nullptr;
    if (FAILED(device->CreateTexture2D(&tex_desc, &tex_data, &texture)) || texture == nullptr) {
        return nullptr;
    }
    ID3D11ShaderResourceView* srv = nullptr;
    const HRESULT hr = device->CreateShaderResourceView(texture, nullptr, &srv);
    texture->Release();
    if (FAILED(hr)) {
        return nullptr;
    }
    return srv;
}

bool EnsureAvatarGpuMaterials(RendererResources* renderer, const AvatarPackage& avatar_pkg, std::uint64_t handle, ID3D11Device* device) {
    if (renderer == nullptr || device == nullptr) {
        return false;
    }
    if (renderer->avatar_materials.find(handle) != renderer->avatar_materials.end()) {
        return true;
    }
    std::vector<GpuMaterialResource> materials;
    materials.reserve(std::max<std::size_t>(avatar_pkg.material_payloads.size(), 1U));
    for (const auto& payload : avatar_pkg.material_payloads) {
        GpuMaterialResource material {};
        material.alpha_mode = payload.alpha_mode.empty() ? "OPAQUE" : payload.alpha_mode;
        material.alpha_cutoff = payload.alpha_cutoff;
        material.double_sided = payload.double_sided;
        if (!payload.base_color_texture_name.empty()) {
            const auto tex_it = std::find_if(
                avatar_pkg.texture_payloads.begin(),
                avatar_pkg.texture_payloads.end(),
                [&](const avatar::TextureRenderPayload& t) { return t.name == payload.base_color_texture_name; });
            if (tex_it != avatar_pkg.texture_payloads.end()) {
                material.base_color_srv = CreateTextureSrvFromPayload(device, &(*tex_it));
            }
        }
        materials.push_back(std::move(material));
    }
    if (materials.empty()) {
        materials.push_back(GpuMaterialResource{});
    }
    renderer->avatar_materials[handle] = std::move(materials);
    return true;
}

DirectX::XMMATRIX ComputeViewMatrix() {
    using namespace DirectX;
    const XMVECTOR eye = XMVectorSet(0.35f, 0.22f, 3.2f, 1.0f);
    const XMVECTOR at = XMVectorSet(0.0f, 0.05f, 0.0f, 1.0f);
    const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    return XMMatrixLookAtRH(eye, at, up);
}

DirectX::XMMATRIX ComputeProjectionMatrix(std::uint32_t width, std::uint32_t height) {
    using namespace DirectX;
    const float aspect = static_cast<float>(width) / static_cast<float>(std::max<std::uint32_t>(height, 1U));
    return XMMatrixPerspectiveFovRH(XM_PIDIV4, aspect, 0.01f, 100.0f);
}

bool CaptureRtvBgra(
    ID3D11Device* device,
    ID3D11DeviceContext* device_ctx,
    ID3D11RenderTargetView* rtv,
    std::uint32_t width,
    std::uint32_t height,
    std::vector<std::uint8_t>* out_pixels) {
    if (device == nullptr || device_ctx == nullptr || rtv == nullptr || out_pixels == nullptr || width == 0U || height == 0U) {
        return false;
    }

    ID3D11Resource* rtv_resource = nullptr;
    rtv->GetResource(&rtv_resource);
    if (rtv_resource == nullptr) {
        return false;
    }

    ID3D11Texture2D* src_texture = nullptr;
    const HRESULT src_hr = rtv_resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&src_texture));
    rtv_resource->Release();
    if (FAILED(src_hr) || src_texture == nullptr) {
        return false;
    }

    D3D11_TEXTURE2D_DESC src_desc {};
    src_texture->GetDesc(&src_desc);
    if (src_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && src_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
        src_texture->Release();
        return false;
    }

    D3D11_TEXTURE2D_DESC staging_desc = src_desc;
    staging_desc.BindFlags = 0U;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.MiscFlags = 0U;
    staging_desc.SampleDesc.Count = 1U;
    staging_desc.SampleDesc.Quality = 0U;

    ID3D11Texture2D* staging_texture = nullptr;
    const HRESULT create_hr = device->CreateTexture2D(&staging_desc, nullptr, &staging_texture);
    if (FAILED(create_hr) || staging_texture == nullptr) {
        src_texture->Release();
        return false;
    }

    device_ctx->CopyResource(staging_texture, src_texture);

    D3D11_MAPPED_SUBRESOURCE mapped {};
    const HRESULT map_hr = device_ctx->Map(staging_texture, 0U, D3D11_MAP_READ, 0U, &mapped);
    if (FAILED(map_hr)) {
        staging_texture->Release();
        src_texture->Release();
        return false;
    }

    out_pixels->assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U, 0U);
    auto* dst = out_pixels->data();
    const auto* src = static_cast<const std::uint8_t*>(mapped.pData);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* row_src = src + static_cast<std::size_t>(mapped.RowPitch) * y;
        std::uint8_t* row_dst = dst + static_cast<std::size_t>(width) * 4U * y;
        std::memcpy(row_dst, row_src, static_cast<std::size_t>(width) * 4U);
    }

    device_ctx->Unmap(staging_texture, 0U);
    staging_texture->Release();
    src_texture->Release();
    return true;
}
#endif

void PublishTrackingFrame() {
    if (!g_state.osc.IsBound()) {
        return;
    }
    g_state.osc.Publish("/VsfClone/Tracking/BlinkL", g_state.latest_tracking.blink_l);
    g_state.osc.Publish("/VsfClone/Tracking/BlinkR", g_state.latest_tracking.blink_r);
    g_state.osc.Publish("/VsfClone/Tracking/MouthOpen", g_state.latest_tracking.mouth_open);
    g_state.osc.Publish("/VsfClone/Tracking/HeadPosX", g_state.latest_tracking.head_pos[0]);
    g_state.osc.Publish("/VsfClone/Tracking/HeadPosY", g_state.latest_tracking.head_pos[1]);
    g_state.osc.Publish("/VsfClone/Tracking/HeadPosZ", g_state.latest_tracking.head_pos[2]);
}

NcResultCode RenderFrameLocked(const NcRenderContext* ctx) {
    if (!EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (ctx == nullptr || ctx->width == 0U || ctx->height == 0U) {
        SetError(NC_ERROR_INVALID_ARGUMENT, "render", "render context/size is invalid", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (ctx->d3d11_device == nullptr || ctx->d3d11_device_context == nullptr || ctx->d3d11_rtv == nullptr) {
        SetError(
            NC_ERROR_INVALID_ARGUMENT,
            "render",
            "d3d11_device/d3d11_device_context/d3d11_rtv must be non-null",
            true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (g_state.render_ready_avatars.empty()) {
        SetError(NC_ERROR_UNSUPPORTED, "render", "no avatar has render resources", true);
        return NC_ERROR_UNSUPPORTED;
    }
    const auto frame_begin = std::chrono::steady_clock::now();

#if defined(_WIN32)
    auto* device = reinterpret_cast<ID3D11Device*>(ctx->d3d11_device);
    auto* device_ctx = reinterpret_cast<ID3D11DeviceContext*>(ctx->d3d11_device_context);
    auto* rtv = reinterpret_cast<ID3D11RenderTargetView*>(ctx->d3d11_rtv);
    if (device == nullptr || device_ctx == nullptr || rtv == nullptr) {
        SetError(
            NC_ERROR_INVALID_ARGUMENT,
            "render",
            "invalid d3d11 context pointers",
            true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    auto& renderer = g_state.renderer;
    if (!EnsurePipelineResources(&renderer, device)) {
        SetError(NC_ERROR_INTERNAL, "render", "failed to initialize D3D11 pipeline resources", true);
        return NC_ERROR_INTERNAL;
    }
    if (!EnsureDepthResources(&renderer, device, ctx->width, ctx->height)) {
        SetError(NC_ERROR_INTERNAL, "render", "failed to initialize depth-stencil resources", true);
        return NC_ERROR_INTERNAL;
    }

    const float clear_color[4] = {0.08f, 0.12f, 0.18f, 1.0f};
    device_ctx->OMSetRenderTargets(1, &rtv, renderer.depth_dsv);
    device_ctx->ClearRenderTargetView(rtv, clear_color);
    device_ctx->ClearDepthStencilView(renderer.depth_dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0U);

    D3D11_VIEWPORT viewport {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(ctx->width);
    viewport.Height = static_cast<float>(ctx->height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    device_ctx->RSSetViewports(1U, &viewport);

    device_ctx->IASetInputLayout(renderer.input_layout);
    device_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    device_ctx->VSSetShader(renderer.vertex_shader, nullptr, 0U);
    device_ctx->PSSetShader(renderer.pixel_shader, nullptr, 0U);
    device_ctx->VSSetConstantBuffers(0U, 1U, &renderer.constant_buffer);
    device_ctx->PSSetConstantBuffers(0U, 1U, &renderer.constant_buffer);

    struct DrawItem {
        std::uint64_t handle = 0U;
        std::size_t mesh_index = 0U;
        const AvatarPackage* pkg = nullptr;
        GpuMeshResource* mesh = nullptr;
        GpuMaterialResource* material = nullptr;
        DirectX::XMMATRIX world = DirectX::XMMatrixIdentity();
        float view_z = 0.0f;
        bool is_blend = false;
    };
    std::vector<DrawItem> opaque_draws;
    std::vector<DrawItem> blend_draws;
    std::uint32_t frame_draw_calls = 0U;
    const auto view = ComputeViewMatrix();
    const auto proj = ComputeProjectionMatrix(ctx->width, ctx->height);
    std::uint32_t avatar_slot = 0U;
    for (const auto handle : g_state.render_ready_avatars) {
        auto it = g_state.avatars.find(handle);
        if (it == g_state.avatars.end()) {
            continue;
        }
        if (!EnsureAvatarGpuMeshes(&renderer, it->second, handle, device)) {
            SetError(NC_ERROR_INTERNAL, "render", "failed to upload mesh payloads to GPU", true);
            return NC_ERROR_INTERNAL;
        }
        if (!EnsureAvatarGpuMaterials(&renderer, it->second, handle, device)) {
            SetError(NC_ERROR_INTERNAL, "render", "failed to create material GPU resources", true);
            return NC_ERROR_INTERNAL;
        }
        auto mesh_it = renderer.avatar_meshes.find(handle);
        if (mesh_it == renderer.avatar_meshes.end()) {
            continue;
        }
        auto material_it = renderer.avatar_materials.find(handle);
        if (material_it == renderer.avatar_materials.end()) {
            continue;
        }
        DirectX::XMFLOAT3 avatar_bmin = {
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
        DirectX::XMFLOAT3 avatar_bmax = {
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max()};
        for (const auto& m : mesh_it->second) {
            avatar_bmin.x = std::min(avatar_bmin.x, m.bounds_min.x);
            avatar_bmin.y = std::min(avatar_bmin.y, m.bounds_min.y);
            avatar_bmin.z = std::min(avatar_bmin.z, m.bounds_min.z);
            avatar_bmax.x = std::max(avatar_bmax.x, m.bounds_max.x);
            avatar_bmax.y = std::max(avatar_bmax.y, m.bounds_max.y);
            avatar_bmax.z = std::max(avatar_bmax.z, m.bounds_max.z);
        }
        const float extent_x = std::max(avatar_bmax.x - avatar_bmin.x, 0.0001f);
        const float extent_y = std::max(avatar_bmax.y - avatar_bmin.y, 0.0001f);
        const float extent_z = std::max(avatar_bmax.z - avatar_bmin.z, 0.0001f);
        const float max_extent = std::max(extent_x, std::max(extent_y, extent_z));
        const float fit_scale = 1.4f / max_extent;
        const float cx = (avatar_bmin.x + avatar_bmax.x) * 0.5f;
        const float cy = (avatar_bmin.y + avatar_bmax.y) * 0.5f;
        const float cz = (avatar_bmin.z + avatar_bmax.z) * 0.5f;
        const float x_offset = static_cast<float>(avatar_slot) * 1.8f;
        const float preview_yaw = DirectX::XM_PI + DirectX::XMConvertToRadians(12.0f);
        const auto world =
            DirectX::XMMatrixTranslation(-cx, -cy, -cz) *
            DirectX::XMMatrixRotationY(preview_yaw) *
            DirectX::XMMatrixScaling(fit_scale, fit_scale, fit_scale) *
            DirectX::XMMatrixTranslation(x_offset, 0.0f, 0.0f);
        ++avatar_slot;
        for (std::size_t mesh_index = 0U; mesh_index < mesh_it->second.size(); ++mesh_index) {
            auto& mesh = mesh_it->second[mesh_index];
            std::size_t material_index = 0U;
            if (mesh.material_index >= 0) {
                material_index = static_cast<std::size_t>(mesh.material_index);
            } else if (mesh_index < material_it->second.size()) {
                material_index = mesh_index;
            }
            if (material_index >= material_it->second.size()) {
                material_index = 0U;
            }
            auto* material = &material_it->second[material_index];
            std::string alpha_mode = "OPAQUE";
            if (!material->alpha_mode.empty()) {
                alpha_mode = material->alpha_mode;
            }
            std::transform(alpha_mode.begin(), alpha_mode.end(), alpha_mode.begin(), [](unsigned char c) {
                return static_cast<char>(std::toupper(c));
            });
            DrawItem item;
            item.handle = handle;
            item.mesh_index = mesh_index;
            item.pkg = &it->second;
            item.mesh = &mesh;
            item.material = material;
            item.world = world;
            item.is_blend = (alpha_mode == "BLEND");
            const auto center = DirectX::XMVectorSet(mesh.center.x, mesh.center.y, mesh.center.z, 1.0f);
            const auto center_view = DirectX::XMVector3TransformCoord(DirectX::XMVector3TransformCoord(center, world), view);
            item.view_z = DirectX::XMVectorGetZ(center_view);
            if (item.is_blend) {
                blend_draws.push_back(item);
            } else {
                opaque_draws.push_back(item);
            }
        }
    }
    std::sort(blend_draws.begin(), blend_draws.end(), [](const DrawItem& a, const DrawItem& b) {
        return a.view_z > b.view_z;
    });

    struct alignas(16) SceneConstants {
        float world_view_proj[16];
        float base_color[4];
        float alpha_cutoff;
        float alpha_mode_mask;
        float has_texture;
        float _pad;
    };
    auto draw_pass = [&](const DrawItem& item) {
        if (item.mesh == nullptr || item.mesh->vertex_buffer == nullptr || item.mesh->index_buffer == nullptr || item.pkg == nullptr) {
            return;
        }
        std::string alpha_mode = "OPAQUE";
        float alpha_cutoff = 0.5f;
        bool double_sided = false;
        ID3D11ShaderResourceView* srv = nullptr;
        if (item.material != nullptr) {
            if (!item.material->alpha_mode.empty()) {
                alpha_mode = item.material->alpha_mode;
            }
            alpha_cutoff = item.material->alpha_cutoff;
            double_sided = item.material->double_sided;
            srv = item.material->base_color_srv;
        }
        std::transform(alpha_mode.begin(), alpha_mode.end(), alpha_mode.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        const bool is_mask = (alpha_mode == "MASK");
        const bool is_blend = (alpha_mode == "BLEND");
        const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        if (is_blend) {
            device_ctx->OMSetBlendState(renderer.blend_alpha, blend_factor, 0xFFFFFFFFU);
            device_ctx->OMSetDepthStencilState(renderer.depth_read, 0U);
        } else {
            device_ctx->OMSetBlendState(renderer.blend_opaque, blend_factor, 0xFFFFFFFFU);
            device_ctx->OMSetDepthStencilState(renderer.depth_write, 0U);
        }
        (void)double_sided;
        device_ctx->RSSetState(renderer.raster_cull_none);

        const UINT stride = item.mesh->vertex_stride;
        const UINT offset = 0U;
        device_ctx->IASetVertexBuffers(0U, 1U, &item.mesh->vertex_buffer, &stride, &offset);
        device_ctx->IASetIndexBuffer(item.mesh->index_buffer, DXGI_FORMAT_R32_UINT, 0U);
        device_ctx->PSSetSamplers(0U, 1U, &renderer.linear_sampler);
        if (srv != nullptr) {
            device_ctx->PSSetShaderResources(0U, 1U, &srv);
        } else {
            ID3D11ShaderResourceView* null_srv = nullptr;
            device_ctx->PSSetShaderResources(0U, 1U, &null_srv);
        }

        const auto world_view_proj = item.world * view * proj;
        const auto world_view_proj_t = DirectX::XMMatrixTranspose(world_view_proj);
        SceneConstants cb {};
        DirectX::XMFLOAT4X4 wvp_store {};
        DirectX::XMStoreFloat4x4(&wvp_store, world_view_proj_t);
        std::memcpy(cb.world_view_proj, &wvp_store, sizeof(cb.world_view_proj));
        cb.base_color[0] = 1.0f;
        cb.base_color[1] = 0.96f;
        cb.base_color[2] = 0.92f;
        cb.base_color[3] = is_blend ? 0.65f : 1.0f;
        cb.alpha_cutoff = alpha_cutoff;
        cb.alpha_mode_mask = is_mask ? 1.0f : 0.0f;
        cb.has_texture = srv != nullptr ? 1.0f : 0.0f;

        D3D11_MAPPED_SUBRESOURCE mapped {};
        if (SUCCEEDED(device_ctx->Map(renderer.constant_buffer, 0U, D3D11_MAP_WRITE_DISCARD, 0U, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            device_ctx->Unmap(renderer.constant_buffer, 0U);
        }
        device_ctx->DrawIndexed(item.mesh->index_count, 0U, 0);
        ID3D11ShaderResourceView* null_srv = nullptr;
        device_ctx->PSSetShaderResources(0U, 1U, &null_srv);
        ++frame_draw_calls;
    };
    for (const auto& item : opaque_draws) {
        draw_pass(item);
    }
    for (const auto& item : blend_draws) {
        draw_pass(item);
    }

    if (g_state.spout.IsActive()) {
        std::vector<std::uint8_t> pixels;
        if (CaptureRtvBgra(device, device_ctx, rtv, ctx->width, ctx->height, &pixels)) {
            g_state.spout.SubmitFrame(pixels.data(), static_cast<std::uint32_t>(pixels.size()));
        }
    }
#endif

    PublishTrackingFrame();
    for (const auto handle : g_state.render_ready_avatars) {
        auto it = g_state.avatars.find(handle);
        if (it == g_state.avatars.end()) {
            continue;
        }
        it->second.last_render_draw_calls = frame_draw_calls;
    }
    if (frame_draw_calls == 0U) {
        SetError(NC_ERROR_UNSUPPORTED, "render", "render-ready avatars produced zero draw calls", true);
        return NC_ERROR_UNSUPPORTED;
    }

    const auto frame_end = std::chrono::steady_clock::now();
    g_state.last_frame_ms = static_cast<float>(
        std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_begin).count()) /
        1000.0f;

    ClearError();
    return NC_OK;
}

}  // namespace

}  // namespace vsfclone::nativecore

NcResultCode nc_initialize(const NcInitOptions* options) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);

    if (options == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "runtime", "options must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    vsfclone::nativecore::g_state.initialized = true;
    vsfclone::nativecore::g_state.next_avatar_handle = 1;
    vsfclone::nativecore::g_state.avatars.clear();
    vsfclone::nativecore::g_state.render_ready_avatars.clear();
    vsfclone::nativecore::g_state.last_frame_ms = 0.0f;
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_shutdown(void) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);

    if (!vsfclone::nativecore::g_state.initialized) {
        return NC_OK;
    }

    vsfclone::nativecore::g_state.spout.Stop();
    vsfclone::nativecore::g_state.osc.Close();
#if defined(_WIN32)
    for (auto& [_, state] : vsfclone::nativecore::g_state.window_targets) {
        vsfclone::nativecore::ReleaseWindowState(&state);
    }
    vsfclone::nativecore::g_state.window_targets.clear();
    vsfclone::nativecore::ResetRendererResources(&vsfclone::nativecore::g_state.renderer);
#endif
    vsfclone::nativecore::g_state.avatars.clear();
    vsfclone::nativecore::g_state.render_ready_avatars.clear();
    vsfclone::nativecore::g_state.initialized = false;
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_load_avatar(const NcAvatarLoadRequest* request, NcAvatarHandle* out_handle, NcAvatarInfo* out_info) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (request == nullptr || out_handle == nullptr || request->path == nullptr || request->path[0] == '\0') {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "request/path/out_handle must be valid", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    auto loaded = vsfclone::nativecore::g_state.loader.Load(request->path);
    if (!loaded.ok) {
        vsfclone::nativecore::SetError(NC_ERROR_IO, "avatar", loaded.error, true);
        return NC_ERROR_IO;
    }
    if (loaded.value.source_type == vsfclone::avatar::AvatarSourceType::Vrm && loaded.value.expressions.empty()) {
        loaded.value.expressions.push_back({"blink", "blink", 0.0f, 0.0f});
        loaded.value.expressions.push_back({"aa", "viseme_aa", 0.0f, 0.0f});
        loaded.value.expressions.push_back({"joy", "joy", 0.0f, 0.0f});
        loaded.value.warnings.push_back("W_VRM_EXPRESSION_FALLBACK: runtime injected blink/aa/joy expression defaults");
    }

    const std::uint64_t handle = vsfclone::nativecore::g_state.next_avatar_handle++;
    vsfclone::nativecore::g_state.avatars[handle] = loaded.value;
    *out_handle = handle;
    vsfclone::nativecore::FillAvatarInfo(loaded.value, handle, out_info);
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_unload_avatar(NcAvatarHandle handle) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }

    auto it = vsfclone::nativecore::g_state.avatars.find(handle);
    if (it == vsfclone::nativecore::g_state.avatars.end()) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    vsfclone::nativecore::g_state.render_ready_avatars.erase(handle);
#if defined(_WIN32)
    auto mesh_it = vsfclone::nativecore::g_state.renderer.avatar_meshes.find(handle);
    if (mesh_it != vsfclone::nativecore::g_state.renderer.avatar_meshes.end()) {
        for (auto& mesh : mesh_it->second) {
            vsfclone::nativecore::ReleaseGpuMeshResource(&mesh);
        }
        vsfclone::nativecore::g_state.renderer.avatar_meshes.erase(mesh_it);
    }
    auto material_it = vsfclone::nativecore::g_state.renderer.avatar_materials.find(handle);
    if (material_it != vsfclone::nativecore::g_state.renderer.avatar_materials.end()) {
        for (auto& material : material_it->second) {
            vsfclone::nativecore::ReleaseGpuMaterialResource(&material);
        }
        vsfclone::nativecore::g_state.renderer.avatar_materials.erase(material_it);
    }
#endif
    vsfclone::nativecore::g_state.avatars.erase(it);
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_get_avatar_info(NcAvatarHandle handle, NcAvatarInfo* out_info) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (out_info == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "out_info must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    auto it = vsfclone::nativecore::g_state.avatars.find(handle);
    if (it == vsfclone::nativecore::g_state.avatars.end()) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "avatar", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    vsfclone::nativecore::FillAvatarInfo(it->second, handle, out_info);
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_set_tracking_frame(const NcTrackingFrame* frame) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (frame == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "tracking", "frame must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    vsfclone::nativecore::g_state.latest_tracking = *frame;
    const float blink_avg = std::max(0.0f, std::min(1.0f, (frame->blink_l + frame->blink_r) * 0.5f));
    const float mouth_open = std::max(0.0f, std::min(1.0f, frame->mouth_open));
    for (auto& [handle, pkg] : vsfclone::nativecore::g_state.avatars) {
        (void)handle;
        if (pkg.expressions.empty()) {
            continue;
        }
        std::string summary;
        std::size_t shown = 0U;
        for (auto& expr : pkg.expressions) {
            float weight = std::max(0.0f, std::min(1.0f, expr.default_weight));
            if (expr.mapping_kind == "blink") {
                weight = blink_avg;
            } else if (expr.mapping_kind == "viseme_aa") {
                weight = mouth_open;
            } else if (expr.mapping_kind == "joy") {
                weight = std::max(0.0f, std::min(1.0f, mouth_open * 0.7f));
            }
            expr.runtime_weight = weight;
            if (shown < 3U) {
                if (!summary.empty()) {
                    summary += ", ";
                }
                summary += expr.name + "=" + std::to_string(weight);
                ++shown;
            }
        }
        pkg.last_expression_summary = summary;
    }
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_create_render_resources(NcAvatarHandle handle) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }

    auto it = vsfclone::nativecore::g_state.avatars.find(handle);
    if (it == vsfclone::nativecore::g_state.avatars.end()) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (it->second.mesh_payloads.empty()) {
        vsfclone::nativecore::SetError(
            NC_ERROR_UNSUPPORTED,
            "render",
            "avatar has no renderable mesh payloads",
            true);
        return NC_ERROR_UNSUPPORTED;
    }

    vsfclone::nativecore::g_state.render_ready_avatars.insert(handle);
#if defined(_WIN32)
    auto mesh_it = vsfclone::nativecore::g_state.renderer.avatar_meshes.find(handle);
    if (mesh_it != vsfclone::nativecore::g_state.renderer.avatar_meshes.end()) {
        for (auto& mesh : mesh_it->second) {
            vsfclone::nativecore::ReleaseGpuMeshResource(&mesh);
        }
        vsfclone::nativecore::g_state.renderer.avatar_meshes.erase(mesh_it);
    }
    auto material_it = vsfclone::nativecore::g_state.renderer.avatar_materials.find(handle);
    if (material_it != vsfclone::nativecore::g_state.renderer.avatar_materials.end()) {
        for (auto& material : material_it->second) {
            vsfclone::nativecore::ReleaseGpuMaterialResource(&material);
        }
        vsfclone::nativecore::g_state.renderer.avatar_materials.erase(material_it);
    }
#endif
    it->second.last_render_draw_calls = 0U;
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_destroy_render_resources(NcAvatarHandle handle) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }

    auto it = vsfclone::nativecore::g_state.avatars.find(handle);
    if (it == vsfclone::nativecore::g_state.avatars.end()) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "unknown avatar handle", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    vsfclone::nativecore::g_state.render_ready_avatars.erase(handle);
#if defined(_WIN32)
    auto mesh_it = vsfclone::nativecore::g_state.renderer.avatar_meshes.find(handle);
    if (mesh_it != vsfclone::nativecore::g_state.renderer.avatar_meshes.end()) {
        for (auto& mesh : mesh_it->second) {
            vsfclone::nativecore::ReleaseGpuMeshResource(&mesh);
        }
        vsfclone::nativecore::g_state.renderer.avatar_meshes.erase(mesh_it);
    }
    auto material_it = vsfclone::nativecore::g_state.renderer.avatar_materials.find(handle);
    if (material_it != vsfclone::nativecore::g_state.renderer.avatar_materials.end()) {
        for (auto& material : material_it->second) {
            vsfclone::nativecore::ReleaseGpuMaterialResource(&material);
        }
        vsfclone::nativecore::g_state.renderer.avatar_materials.erase(material_it);
    }
#endif
    it->second.last_render_draw_calls = 0U;
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_render_frame(const NcRenderContext* ctx) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    return vsfclone::nativecore::RenderFrameLocked(ctx);
}

NcResultCode nc_create_window_render_target(const NcWindowRenderTarget* target) {
#if !defined(_WIN32)
    (void)target;
    return NC_ERROR_UNSUPPORTED;
#else
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (target == nullptr || target->hwnd == nullptr || target->width == 0U || target->height == 0U) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "window target is invalid", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    auto& state = vsfclone::nativecore::g_state.window_targets[target->hwnd];
    vsfclone::nativecore::ReleaseWindowState(&state);

    DXGI_SWAP_CHAIN_DESC swap_desc {};
    swap_desc.BufferDesc.Width = target->width;
    swap_desc.BufferDesc.Height = target->height;
    swap_desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swap_desc.SampleDesc.Count = 1U;
    swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.BufferCount = 2U;
    swap_desc.OutputWindow = static_cast<HWND>(target->hwnd);
    swap_desc.Windowed = TRUE;
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL feature_levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};
    D3D_FEATURE_LEVEL selected_level = D3D_FEATURE_LEVEL_11_0;

    const UINT device_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        device_flags,
        feature_levels,
        static_cast<UINT>(std::size(feature_levels)),
        D3D11_SDK_VERSION,
        &swap_desc,
        &state.swap_chain,
        &state.device,
        &selected_level,
        &state.device_context);
    if (FAILED(hr)) {
        vsfclone::nativecore::g_state.window_targets.erase(target->hwnd);
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "render", "failed to create d3d11 device/swapchain", true);
        return NC_ERROR_INTERNAL;
    }

    ID3D11Texture2D* backbuffer = nullptr;
    HRESULT bb_hr = state.swap_chain->GetBuffer(0U, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuffer));
    if (FAILED(bb_hr) || backbuffer == nullptr) {
        vsfclone::nativecore::ReleaseWindowState(&state);
        vsfclone::nativecore::g_state.window_targets.erase(target->hwnd);
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "render", "failed to fetch swapchain backbuffer", true);
        return NC_ERROR_INTERNAL;
    }

    HRESULT rtv_hr = state.device->CreateRenderTargetView(backbuffer, nullptr, &state.rtv);
    backbuffer->Release();
    if (FAILED(rtv_hr) || state.rtv == nullptr) {
        vsfclone::nativecore::ReleaseWindowState(&state);
        vsfclone::nativecore::g_state.window_targets.erase(target->hwnd);
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "render", "failed to create render target view", true);
        return NC_ERROR_INTERNAL;
    }

    state.width = target->width;
    state.height = target->height;

    vsfclone::nativecore::ClearError();
    return NC_OK;
#endif
}

NcResultCode nc_resize_window_render_target(const NcWindowRenderTarget* target) {
#if !defined(_WIN32)
    (void)target;
    return NC_ERROR_UNSUPPORTED;
#else
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (target == nullptr || target->hwnd == nullptr || target->width == 0U || target->height == 0U) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "window target is invalid", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    auto it = vsfclone::nativecore::g_state.window_targets.find(target->hwnd);
    if (it == vsfclone::nativecore::g_state.window_targets.end()) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "unknown window render target", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    auto& state = it->second;
    if (state.swap_chain == nullptr || state.device == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "render", "window render target is not initialized", true);
        return NC_ERROR_INTERNAL;
    }

    if (state.rtv != nullptr) {
        state.rtv->Release();
        state.rtv = nullptr;
    }

    HRESULT resize_hr = state.swap_chain->ResizeBuffers(0U, target->width, target->height, DXGI_FORMAT_UNKNOWN, 0U);
    if (FAILED(resize_hr)) {
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "render", "swapchain resize failed", true);
        return NC_ERROR_INTERNAL;
    }

    ID3D11Texture2D* backbuffer = nullptr;
    HRESULT bb_hr = state.swap_chain->GetBuffer(0U, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuffer));
    if (FAILED(bb_hr) || backbuffer == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "render", "failed to fetch resized backbuffer", true);
        return NC_ERROR_INTERNAL;
    }

    HRESULT rtv_hr = state.device->CreateRenderTargetView(backbuffer, nullptr, &state.rtv);
    backbuffer->Release();
    if (FAILED(rtv_hr) || state.rtv == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "render", "failed to create resized render target view", true);
        return NC_ERROR_INTERNAL;
    }

    state.width = target->width;
    state.height = target->height;
    vsfclone::nativecore::ClearError();
    return NC_OK;
#endif
}

NcResultCode nc_destroy_window_render_target(void* hwnd) {
#if !defined(_WIN32)
    (void)hwnd;
    return NC_ERROR_UNSUPPORTED;
#else
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (hwnd == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "hwnd must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    auto it = vsfclone::nativecore::g_state.window_targets.find(hwnd);
    if (it == vsfclone::nativecore::g_state.window_targets.end()) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "unknown window render target", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    vsfclone::nativecore::ReleaseWindowState(&it->second);
    vsfclone::nativecore::g_state.window_targets.erase(it);
    vsfclone::nativecore::ClearError();
    return NC_OK;
#endif
}

NcResultCode nc_render_frame_to_window(void* hwnd, float delta_time_seconds) {
#if !defined(_WIN32)
    (void)hwnd;
    (void)delta_time_seconds;
    return NC_ERROR_UNSUPPORTED;
#else
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (hwnd == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "hwnd must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    auto it = vsfclone::nativecore::g_state.window_targets.find(hwnd);
    if (it == vsfclone::nativecore::g_state.window_targets.end()) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "render", "unknown window render target", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    auto& state = it->second;
    if (state.device == nullptr || state.device_context == nullptr || state.rtv == nullptr || state.swap_chain == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "render", "window render target is incomplete", true);
        return NC_ERROR_INTERNAL;
    }

    NcRenderContext ctx {};
    ctx.hwnd = hwnd;
    ctx.d3d11_device = state.device;
    ctx.d3d11_device_context = state.device_context;
    ctx.d3d11_rtv = state.rtv;
    ctx.width = state.width;
    ctx.height = state.height;
    ctx.delta_time_seconds = delta_time_seconds;

    NcResultCode rc = vsfclone::nativecore::RenderFrameLocked(&ctx);
    if (rc != NC_OK) {
        return rc;
    }

    const HRESULT present_hr = state.swap_chain->Present(1U, 0U);
    if (FAILED(present_hr)) {
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "render", "swapchain present failed", true);
        return NC_ERROR_INTERNAL;
    }

    vsfclone::nativecore::ClearError();
    return NC_OK;
#endif
}

NcResultCode nc_start_spout(const NcSpoutOptions* options) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (options == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "spout", "options must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    vsfclone::stream::StreamConfig cfg;
    cfg.width = options->width > 0U ? options->width : 1920U;
    cfg.height = options->height > 0U ? options->height : 1080U;
    cfg.fps = options->fps > 0U ? options->fps : 60U;
    cfg.channel_name = (options->channel_name != nullptr && options->channel_name[0] != '\0') ? options->channel_name : "VsfClone";

    if (!vsfclone::nativecore::g_state.spout.Start(cfg)) {
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "spout", "failed to start spout output", true);
        return NC_ERROR_INTERNAL;
    }
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_stop_spout(void) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    vsfclone::nativecore::g_state.spout.Stop();
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_start_osc(const NcOscOptions* options) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (options == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "osc", "options must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    const std::uint16_t port = options->bind_port > 0U ? options->bind_port : 39539U;
    if (!vsfclone::nativecore::g_state.osc.SetDestination(
            (options->publish_address != nullptr && options->publish_address[0] != '\0')
                ? options->publish_address
                : "127.0.0.1:39539")) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "osc", "invalid publish_address format", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (!vsfclone::nativecore::g_state.osc.Bind(port)) {
        vsfclone::nativecore::SetError(NC_ERROR_INTERNAL, "osc", "failed to bind osc endpoint", true);
        return NC_ERROR_INTERNAL;
    }
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_stop_osc(void) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    vsfclone::nativecore::g_state.osc.Close();
    vsfclone::nativecore::ClearError();
    return NC_OK;
}

NcResultCode nc_get_last_error(NcErrorInfo* out_error) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (out_error == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }

    std::memset(out_error, 0, sizeof(*out_error));
    out_error->code = vsfclone::nativecore::g_state.last_error_code;
    vsfclone::nativecore::CopyString(out_error->subsystem, sizeof(out_error->subsystem), vsfclone::nativecore::g_state.last_error_subsystem);
    vsfclone::nativecore::CopyString(out_error->message, sizeof(out_error->message), vsfclone::nativecore::g_state.last_error_message);
    out_error->recoverable = vsfclone::nativecore::g_state.last_error_recoverable ? 1U : 0U;
    return NC_OK;
}

NcResultCode nc_get_runtime_stats(NcRuntimeStats* out_stats) {
    std::lock_guard<std::mutex> lock(vsfclone::nativecore::g_mutex);
    if (!vsfclone::nativecore::EnsureInitialized()) {
        return NC_ERROR_NOT_INITIALIZED;
    }
    if (out_stats == nullptr) {
        vsfclone::nativecore::SetError(NC_ERROR_INVALID_ARGUMENT, "runtime", "out_stats must not be null", true);
        return NC_ERROR_INVALID_ARGUMENT;
    }

    std::memset(out_stats, 0, sizeof(*out_stats));
    out_stats->render_ready_avatar_count = static_cast<std::uint32_t>(vsfclone::nativecore::g_state.render_ready_avatars.size());
    out_stats->spout_active = vsfclone::nativecore::g_state.spout.IsActive() ? 1U : 0U;
    out_stats->osc_active = vsfclone::nativecore::g_state.osc.IsBound() ? 1U : 0U;
    out_stats->last_frame_ms = vsfclone::nativecore::g_state.last_frame_ms;
    vsfclone::nativecore::ClearError();
    return NC_OK;
}
