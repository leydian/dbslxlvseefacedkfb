#pragma once

#include <memory>
#include <vector>

#include "animiq/avatar/avatar_package.h"
#include "animiq/avatar/i_avatar_loader.h"

namespace animiq::avatar {

struct AvatarLoadOptions {
    AvatarSourceType forced_source_type = AvatarSourceType::Unknown;
    MiqUnknownSectionPolicy miq_unknown_section_policy = MiqUnknownSectionPolicy::Warn;
};

class AvatarLoaderFacade {
  public:
    AvatarLoaderFacade();
    core::Result<AvatarPackage> Load(const std::string& path) const;
    core::Result<AvatarPackage> Load(const std::string& path, const AvatarLoadOptions& options) const;

  private:
    std::vector<std::unique_ptr<IAvatarLoader>> loaders_;
};

}  // namespace animiq::avatar
