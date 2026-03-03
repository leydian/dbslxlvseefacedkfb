#include "vsfclone/avatar/avatar_loader_facade.h"

#include <cstdint>
#include <fstream>
#include <vector>

#include "vrm_loader.h"
#include "vxavatar_loader.h"
#include "vxa2_loader.h"
#include "xav2_loader.h"
#include "vsfavatar_loader.h"

namespace vsfclone::avatar {

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

}  // namespace

AvatarLoaderFacade::AvatarLoaderFacade() {
    loaders_.push_back(std::make_unique<VrmLoader>());
    loaders_.push_back(std::make_unique<VxAvatarLoader>());
    loaders_.push_back(std::make_unique<Vxa2Loader>());
    loaders_.push_back(std::make_unique<Xav2Loader>());
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
        if (auto* xav2_loader = dynamic_cast<Xav2Loader*>(loader.get()); xav2_loader != nullptr) {
            return xav2_loader->Load(path, options.xav2_unknown_section_policy);
        }
        return loader->Load(path);
    }

    const auto head = ReadHeadBytes(path, 16U);
    if (head.empty()) {
        return core::Result<AvatarPackage>::Fail("unsupported file extension and failed to read file signature");
    }
    for (const auto& loader : loaders_) {
        if (!loader->CanLoadBytes(head)) {
            continue;
        }
        if (auto* xav2_loader = dynamic_cast<Xav2Loader*>(loader.get()); xav2_loader != nullptr) {
            return xav2_loader->Load(path, options.xav2_unknown_section_policy);
        }
        return loader->Load(path);
    }
    return core::Result<AvatarPackage>::Fail("unsupported file extension or signature");
}

}  // namespace vsfclone::avatar
