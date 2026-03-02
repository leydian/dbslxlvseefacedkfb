#pragma once

#include <memory>
#include <vector>

#include "vsfclone/avatar/i_avatar_loader.h"

namespace vsfclone::avatar {

class AvatarLoaderFacade {
  public:
    AvatarLoaderFacade();
    core::Result<AvatarPackage> Load(const std::string& path) const;

  private:
    std::vector<std::unique_ptr<IAvatarLoader>> loaders_;
};

}  // namespace vsfclone::avatar

