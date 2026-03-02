#include "vsfclone/avatar/avatar_loader_facade.h"

#include "vrm_loader.h"
#include "vxavatar_loader.h"
#include "vsfavatar_loader.h"

namespace vsfclone::avatar {

AvatarLoaderFacade::AvatarLoaderFacade() {
    loaders_.push_back(std::make_unique<VrmLoader>());
    loaders_.push_back(std::make_unique<VxAvatarLoader>());
    loaders_.push_back(std::make_unique<VsfAvatarLoader>());
}

core::Result<AvatarPackage> AvatarLoaderFacade::Load(const std::string& path) const {
    for (const auto& loader : loaders_) {
        if (!loader->CanLoadPath(path)) {
            continue;
        }
        return loader->Load(path);
    }
    return core::Result<AvatarPackage>::Fail("unsupported file extension");
}

}  // namespace vsfclone::avatar
