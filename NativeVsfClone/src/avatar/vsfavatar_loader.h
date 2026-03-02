#pragma once

#include "vsfclone/avatar/i_avatar_loader.h"
#include "vsfclone/vsf/unityfs_reader.h"

namespace vsfclone::avatar {

class VsfAvatarLoader final : public IAvatarLoader {
  public:
    bool CanLoadPath(const std::string& path) const override;
    core::Result<AvatarPackage> Load(const std::string& path) const override;

  private:
    core::Result<AvatarPackage> LoadInHouse(const std::string& path) const;
    core::Result<AvatarPackage> LoadViaSidecar(const std::string& path) const;
    vsf::UnityFsReader reader_ {};
};

}  // namespace vsfclone::avatar
