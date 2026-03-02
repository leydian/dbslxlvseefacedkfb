#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vsfclone/avatar/avatar_package.h"
#include "vsfclone/core/result.h"

namespace vsfclone::avatar {

class IAvatarLoader {
  public:
    virtual ~IAvatarLoader() = default;
    virtual bool CanLoadPath(const std::string& path) const = 0;
    virtual bool CanLoadBytes(const std::vector<std::uint8_t>& head) const = 0;
    virtual core::Result<AvatarPackage> Load(const std::string& path) const = 0;
};

}  // namespace vsfclone::avatar
