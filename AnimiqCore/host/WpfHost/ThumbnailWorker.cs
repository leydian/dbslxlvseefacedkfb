using HostCore;

namespace WpfHost;

internal static class ThumbnailWorker
{
    public static int Run(string[] args) => AvatarThumbnailWorker.Run(args);
}
