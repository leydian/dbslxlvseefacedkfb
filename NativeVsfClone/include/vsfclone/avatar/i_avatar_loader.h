#pragma once

#include <string>

#include "vsfclone/avatar/avatar_package.h"
#include "vsfclone/core/result.h"

namespace vsfclone::avatar {

class IAvatarLoader {
  public:
    virtual ~IAvatarLoader() = default;
    virtual bool CanLoadPath(const std::string& path) const = 0;
    virtual core::Result<AvatarPackage> Load(const std::string& path) const = 0;
};

}  // namespace vsfclone::avatar

