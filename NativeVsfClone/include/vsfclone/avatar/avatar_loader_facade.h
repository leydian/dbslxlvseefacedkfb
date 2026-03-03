#pragma once

#include <memory>
#include <vector>

#include "vsfclone/avatar/i_avatar_loader.h"

namespace vsfclone::avatar {

struct AvatarLoadOptions {
    Xav2UnknownSectionPolicy xav2_unknown_section_policy = Xav2UnknownSectionPolicy::Warn;
};

class AvatarLoaderFacade {
  public:
    AvatarLoaderFacade();
    core::Result<AvatarPackage> Load(const std::string& path) const;
    core::Result<AvatarPackage> Load(const std::string& path, const AvatarLoadOptions& options) const;

  private:
    std::vector<std::unique_ptr<IAvatarLoader>> loaders_;
};

}  // namespace vsfclone::avatar
