#pragma once

#include "animiq/avatar/i_avatar_loader.h"

namespace animiq::avatar {

class VrmLoader final : public IAvatarLoader {
  public:
    bool CanLoadPath(const std::string& path) const override;
    bool CanLoadBytes(const std::vector<std::uint8_t>& head) const override;
    core::Result<AvatarPackage> Load(const std::string& path) const override;
};

}  // namespace animiq::avatar
