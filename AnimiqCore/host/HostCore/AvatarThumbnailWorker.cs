using System.Diagnostics;
using System.Globalization;
using System.Security.Cryptography;
using System.Text;

namespace HostCore;

public static class AvatarThumbnailWorker
{
    private const int ThumbnailCacheMaxEntries = 256;
    private const long ThumbnailCacheMaxBytes = 128L * 1024L * 1024L;

    public static int Run(string[] args)
    {
        if (!TryGetArg(args, "--avatar", out var avatarPath) ||
            !TryGetArg(args, "--output", out var outputPath))
        {
            return 2;
        }

        if (!File.Exists(avatarPath))
        {
            return 3;
        }

        var width = ParsePositiveInt(args, "--width", 256);
        var height = ParsePositiveInt(args, "--height", 256);
        var previousPlaceholderPolicy = Environment.GetEnvironmentVariable("VSF_ALLOW_VSF_PLACEHOLDER_RENDER");

        try
        {
            // Thumbnail worker is preview-only; allow placeholder payload render here.
            Environment.SetEnvironmentVariable("VSF_ALLOW_VSF_PLACEHOLDER_RENDER", "1");
            var outputDir = Path.GetDirectoryName(outputPath);
            if (!string.IsNullOrWhiteSpace(outputDir))
            {
                Directory.CreateDirectory(outputDir);
            }
        }
        catch
        {
            return 4;
        }

        var init = new NcInitOptions
        {
            ApiVersion = 1,
            Reserved = 0,
        };
        var initRc = NativeCoreInterop.nc_initialize(ref init);
        if (initRc != NcResultCode.Ok)
        {
            return 10;
        }

        ulong handle = 0;
        try
        {
            var loadRequest = new NcAvatarLoadRequest
            {
                Path = avatarPath,
                FormatHint = NcAvatarFormatHint.Auto,
                ShaderProfile = 0,
                FallbackPolicy = 0,
            };
            var loadRc = NativeCoreInterop.nc_load_avatar(ref loadRequest, out handle, out _);
            if (loadRc != NcResultCode.Ok)
            {
                return 11;
            }

            var createRc = NativeCoreInterop.nc_create_render_resources(handle);
            if (createRc != NcResultCode.Ok)
            {
                return 12;
            }

            var thumbnailRequest = new NcThumbnailRequest
            {
                Handle = handle,
                OutputPath = outputPath,
                Width = (uint)width,
                Height = (uint)height,
                DeltaTimeSeconds = 1.0f / 60.0f,
            };
            var thumbnailRc = NativeCoreInterop.nc_render_avatar_thumbnail_png(ref thumbnailRequest);
            if (thumbnailRc != NcResultCode.Ok)
            {
                return 13;
            }

            return 0;
        }
        finally
        {
            Environment.SetEnvironmentVariable("VSF_ALLOW_VSF_PLACEHOLDER_RENDER", previousPlaceholderPolicy);
            if (handle != 0)
            {
                _ = NativeCoreInterop.nc_destroy_render_resources(handle);
                _ = NativeCoreInterop.nc_unload_avatar(handle);
            }
            _ = NativeCoreInterop.nc_shutdown();
        }
    }

    public static string BuildThumbnailPath(string avatarPath)
    {
        var root = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "AnimiqHost",
            "thumbnails");
        Directory.CreateDirectory(root);
        PruneThumbnailCache(root, ThumbnailCacheMaxEntries, ThumbnailCacheMaxBytes);
        var hash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(avatarPath.ToLowerInvariant())));
        return Path.Combine(root, $"{hash}.png");
    }

    private static void PruneThumbnailCache(string root, int maxEntries, long maxBytes)
    {
        try
        {
            var files = new DirectoryInfo(root)
                .GetFiles("*.png", SearchOption.TopDirectoryOnly)
                .OrderByDescending(file => file.LastWriteTimeUtc)
                .ToList();
            if (files.Count == 0)
            {
                return;
            }

            var totalBytes = files.Sum(file => file.Length);
            for (var i = files.Count - 1; i >= 0; i--)
            {
                var overEntryCap = files.Count > maxEntries;
                var overSizeCap = totalBytes > maxBytes;
                if (!overEntryCap && !overSizeCap)
                {
                    break;
                }

                var target = files[i];
                totalBytes -= target.Length;
                files.RemoveAt(i);
                target.Delete();
            }
        }
        catch
        {
            // Cache cleanup is best-effort; failures should not block thumbnail generation.
        }
    }

    public static async Task<bool> RunWorkerProcessAsync(
        string workerExecutablePath,
        AvatarThumbnailJob job,
        TimeSpan timeout,
        CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrWhiteSpace(workerExecutablePath) || !File.Exists(workerExecutablePath))
        {
            return false;
        }

        try
        {
            var psi = new ProcessStartInfo(workerExecutablePath)
            {
                UseShellExecute = false,
                CreateNoWindow = true,
            };
            psi.ArgumentList.Add("--thumbnail-worker");
            psi.ArgumentList.Add("--avatar");
            psi.ArgumentList.Add(job.AvatarPath);
            psi.ArgumentList.Add("--output");
            psi.ArgumentList.Add(job.ThumbnailPath);
            psi.ArgumentList.Add("--width");
            psi.ArgumentList.Add("256");
            psi.ArgumentList.Add("--height");
            psi.ArgumentList.Add("256");

            using var process = Process.Start(psi);
            if (process is null)
            {
                return false;
            }

            using var timeoutCts = new CancellationTokenSource(timeout);
            using var linkedCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, timeoutCts.Token);
            await process.WaitForExitAsync(linkedCts.Token);
            return process.ExitCode == 0;
        }
        catch
        {
            return false;
        }
    }

    private static int ParsePositiveInt(string[] args, string key, int fallback)
    {
        if (!TryGetArg(args, key, out var raw))
        {
            return fallback;
        }
        if (!int.TryParse(raw, NumberStyles.Integer, CultureInfo.InvariantCulture, out var value))
        {
            return fallback;
        }
        return Math.Clamp(value, 64, 1024);
    }

    private static bool TryGetArg(string[] args, string key, out string value)
    {
        for (var i = 0; i < args.Length - 1; i++)
        {
            if (string.Equals(args[i], key, StringComparison.Ordinal))
            {
                value = args[i + 1].Trim();
                return !string.IsNullOrWhiteSpace(value);
            }
        }

        value = string.Empty;
        return false;
    }
}
