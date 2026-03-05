using System;
using System.Globalization;
using System.IO;
using HostCore;

namespace WpfHost;

internal static class ThumbnailWorker
{
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

        try
        {
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
            if (handle != 0)
            {
                _ = NativeCoreInterop.nc_destroy_render_resources(handle);
                _ = NativeCoreInterop.nc_unload_avatar(handle);
            }
            _ = NativeCoreInterop.nc_shutdown();
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
