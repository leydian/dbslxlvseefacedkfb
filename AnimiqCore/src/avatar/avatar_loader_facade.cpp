#include "animiq/avatar/avatar_loader_facade.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "vrm_loader.h"
#include "miq_loader.h"
#include "vsfavatar_loader.h"

namespace animiq::avatar {

namespace {

std::vector<std::uint8_t> ReadHeadBytes(const std::string& path, std::size_t max_size) {
    std::vector<std::uint8_t> out;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return out;
    }
    out.assign(max_size, 0U);
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    const auto read_size = static_cast<std::size_t>(in.gcount());
    out.resize(read_size);
    return out;
}

const char* SourceTypeContractName(AvatarSourceType source_type) {
    switch (source_type) {
        case AvatarSourceType::Vrm:
            return "vrm";
        case AvatarSourceType::Miq:
            return "miq";
        default:
            return "other";
    }
}

bool IsContractManagedSource(AvatarSourceType source_type) {
    return source_type == AvatarSourceType::Vrm || source_type == AvatarSourceType::Miq;
}

void AppendWarningCodeUnique(AvatarPackage* pkg, const std::string& code) {
    if (pkg == nullptr || code.empty()) {
        return;
    }
    const auto it = std::find(pkg->warning_codes.begin(), pkg->warning_codes.end(), code);
    if (it == pkg->warning_codes.end()) {
        pkg->warning_codes.push_back(code);
    }
}

void ApplyRenderReadyContract(AvatarPackage* pkg) {
    if (pkg == nullptr || !IsContractManagedSource(pkg->source_type)) {
        return;
    }
    const std::string parser_stage = pkg->parser_stage.empty() ? "unknown" : pkg->parser_stage;
    const std::string primary_error = pkg->primary_error_code.empty() ? "NONE" : pkg->primary_error_code;
    pkg->warnings.push_back(
        "W_CONTRACT: AVATAR_RENDER_READY_V1: format=" + std::string(SourceTypeContractName(pkg->source_type)) +
        ", parser_stage=" + parser_stage +
        ", primary_error=" + primary_error +
        ", mesh_payloads=" + std::to_string(pkg->mesh_payloads.size()) +
        ", material_payloads=" + std::to_string(pkg->material_payloads.size()) +
        ", texture_payloads=" + std::to_string(pkg->texture_payloads.size()));
    AppendWarningCodeUnique(pkg, "AVATAR_RENDER_READY_V1_APPLIED");

    if (!pkg->mesh_payloads.empty()) {
        return;
    }

    if (pkg->parser_stage.empty()) {
        pkg->parser_stage = "payload";
    }
    if (pkg->primary_error_code.empty() || pkg->primary_error_code == "NONE") {
        pkg->primary_error_code = "AVATAR_RENDER_READY_MESH_PAYLOAD_MISSING";
    }
    pkg->compat_level = AvatarCompatLevel::Failed;
    pkg->warnings.push_back(
        "E_CONTRACT: AVATAR_RENDER_READY_MESH_PAYLOAD_MISSING: format=" +
        std::string(SourceTypeContractName(pkg->source_type)) +
        ", parser_stage=" + pkg->parser_stage +
        ", primary_error=" + pkg->primary_error_code);
    AppendWarningCodeUnique(pkg, "AVATAR_RENDER_READY_MESH_PAYLOAD_MISSING");
}

core::Result<AvatarPackage> FinalizeLoadResult(core::Result<AvatarPackage> result) {
    if (!result.ok) {
        return result;
    }
    ApplyRenderReadyContract(&result.value);
    return result;
}

}  // namespace

AvatarLoaderFacade::AvatarLoaderFacade() {
    loaders_.push_back(std::make_unique<VrmLoader>());
    loaders_.push_back(std::make_unique<MiqLoader>());
    loaders_.push_back(std::make_unique<VsfAvatarLoader>());
}

core::Result<AvatarPackage> AvatarLoaderFacade::Load(const std::string& path) const {
    return Load(path, AvatarLoadOptions {});
}

core::Result<AvatarPackage> AvatarLoaderFacade::Load(const std::string& path, const AvatarLoadOptions& options) const {
    for (const auto& loader : loaders_) {
        if (!loader->CanLoadPath(path)) {
            continue;
        }
        if (auto* miq_loader = dynamic_cast<MiqLoader*>(loader.get()); miq_loader != nullptr) {
            return FinalizeLoadResult(miq_loader->Load(path, options.miq_unknown_section_policy));
        }
        return FinalizeLoadResult(loader->Load(path));
    }

    const auto head = ReadHeadBytes(path, 16U);
    if (head.empty()) {
        return core::Result<AvatarPackage>::Fail("unsupported file extension and failed to read file signature");
    }
    for (const auto& loader : loaders_) {
        if (!loader->CanLoadBytes(head)) {
            continue;
        }
        if (auto* miq_loader = dynamic_cast<MiqLoader*>(loader.get()); miq_loader != nullptr) {
            return FinalizeLoadResult(miq_loader->Load(path, options.miq_unknown_section_policy));
        }
        return FinalizeLoadResult(loader->Load(path));
    }
    return core::Result<AvatarPackage>::Fail("unsupported file extension or signature");
}

}  // namespace animiq::avatar
