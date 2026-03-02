#pragma once

#include "vsfclone/avatar/i_avatar_loader.h"

namespace vsfclone::avatar {

class VrmLoader final : public IAvatarLoader {
  public:
    bool CanLoadPath(const std::string& path) const override;
    bool CanLoadBytes(const std::vector<std::uint8_t>& head) const override;
    core::Result<AvatarPackage> Load(const std::string& path) const override;
};

}  // namespace vsfclone::avatar
