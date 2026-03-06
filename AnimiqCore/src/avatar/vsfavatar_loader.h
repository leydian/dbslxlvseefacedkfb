#pragma once

#include "animiq/avatar/i_avatar_loader.h"
#include "animiq/vsf/unityfs_reader.h"

namespace animiq::avatar {

class VsfAvatarLoader final : public IAvatarLoader {
  public:
    bool CanLoadPath(const std::string& path) const override;
    bool CanLoadBytes(const std::vector<std::uint8_t>& head) const override;
    core::Result<AvatarPackage> Load(const std::string& path) const override;

  private:
    core::Result<AvatarPackage> LoadInHouse(const std::string& path) const;
    core::Result<AvatarPackage> LoadViaSidecar(const std::string& path) const;
    vsf::UnityFsReader reader_ {};
};

}  // namespace animiq::avatar
