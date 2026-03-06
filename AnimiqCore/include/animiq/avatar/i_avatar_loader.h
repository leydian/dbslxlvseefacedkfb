#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "animiq/avatar/avatar_package.h"
#include "animiq/core/result.h"

namespace animiq::avatar {

class IAvatarLoader {
  public:
    virtual ~IAvatarLoader() = default;
    virtual bool CanLoadPath(const std::string& path) const = 0;
    virtual bool CanLoadBytes(const std::vector<std::uint8_t>& head) const = 0;
    virtual core::Result<AvatarPackage> Load(const std::string& path) const = 0;
};

}  // namespace animiq::avatar
