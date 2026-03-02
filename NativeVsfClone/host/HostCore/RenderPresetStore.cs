using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace HostCore;

public sealed record RenderPresetModel(
    string Name,
    bool BroadcastMode,
    RenderCameraMode CameraMode,
    float FramingTarget,
    float Headroom,
    float YawDeg,
    float FovDeg,
    BackgroundPreset BackgroundPreset,
    bool ShowDebugOverlay,
    bool MirrorMode);

public sealed class RenderPresetStoreModel
{
    public const int CurrentVersion = 1;

    public int Version { get; init; } = CurrentVersion;
    public List<RenderPresetModel> Presets { get; init; } = new();
    public string? LastSelectedPresetName { get; set; }

    public static RenderPresetStoreModel CreateDefault()
    {
        return new RenderPresetStoreModel
        {
            Version = CurrentVersion,
            Presets = new List<RenderPresetModel>
            {
                new(
                    "Broadcast Default",
                    true,
                    RenderCameraMode.AutoFitBust,
                    0.72f,
                    0.10f,
                    0.0f,
                    35.0f,
                    BackgroundPreset.DarkBlue,
                    false,
                    false),
                new(
                    "Debug Default",
                    false,
                    RenderCameraMode.AutoFitBust,
                    0.72f,
                    0.10f,
                    0.0f,
                    35.0f,
                    BackgroundPreset.NeutralGray,
                    true,
                    false),
                new(
                    "Green Screen Tight",
                    true,
                    RenderCameraMode.AutoFitBust,
                    0.84f,
                    0.08f,
                    0.0f,
                    32.0f,
                    BackgroundPreset.GreenScreen,
                    false,
                    false),
            },
            LastSelectedPresetName = "Broadcast Default",
        };
    }
}

public sealed class RenderPresetStore : IRenderPresetStore
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = true,
    };

    private readonly string _storagePath;

    public RenderPresetStore(string? storagePath = null)
    {
        var root = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        _storagePath = storagePath ??
            Path.Combine(root, "VsfCloneHost", "render_presets.json");
    }

    public RenderPresetStoreModel Load()
    {
        if (!File.Exists(_storagePath))
        {
            return RenderPresetStoreModel.CreateDefault();
        }

        try
        {
            var json = File.ReadAllText(_storagePath);
            var model = JsonSerializer.Deserialize<RenderPresetStoreModel>(json, JsonOptions);
            if (model is null)
            {
                return RenderPresetStoreModel.CreateDefault();
            }

            var normalized = Normalize(model);
            if (normalized.Presets.Count == 0)
            {
                return RenderPresetStoreModel.CreateDefault();
            }

            return normalized;
        }
        catch
        {
            TryBackupCorruptFile();
            return RenderPresetStoreModel.CreateDefault();
        }
    }

    public void Save(RenderPresetStoreModel store)
    {
        var normalized = Normalize(store);
        var directory = Path.GetDirectoryName(_storagePath);
        if (!string.IsNullOrWhiteSpace(directory))
        {
            Directory.CreateDirectory(directory);
        }

        var json = JsonSerializer.Serialize(normalized, JsonOptions);
        File.WriteAllText(_storagePath, json);
    }

    private static RenderPresetStoreModel Normalize(RenderPresetStoreModel input)
    {
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var presets = new List<RenderPresetModel>();
        foreach (var preset in input.Presets ?? new List<RenderPresetModel>())
        {
            if (string.IsNullOrWhiteSpace(preset.Name))
            {
                continue;
            }

            var normalizedName = preset.Name.Trim();
            if (!names.Add(normalizedName))
            {
                continue;
            }

            presets.Add(preset with
            {
                Name = normalizedName,
                FramingTarget = Clamp(preset.FramingTarget, 0.35f, 0.95f),
                Headroom = Clamp(preset.Headroom, 0.0f, 0.50f),
                YawDeg = Clamp(preset.YawDeg, -45.0f, 45.0f),
                FovDeg = Clamp(preset.FovDeg, 20.0f, 70.0f),
            });
        }

        var lastSelected = input.LastSelectedPresetName;
        if (string.IsNullOrWhiteSpace(lastSelected) ||
            presets.All(p => !string.Equals(p.Name, lastSelected, StringComparison.OrdinalIgnoreCase)))
        {
            lastSelected = presets.Count > 0 ? presets[0].Name : null;
        }

        return new RenderPresetStoreModel
        {
            Version = RenderPresetStoreModel.CurrentVersion,
            Presets = presets,
            LastSelectedPresetName = lastSelected,
        };
    }

    private void TryBackupCorruptFile()
    {
        try
        {
            var backupPath = _storagePath + ".bak";
            File.Copy(_storagePath, backupPath, true);
        }
        catch
        {
            // Best-effort backup for diagnostics. Ignore backup failures.
        }
    }

    private static float Clamp(float value, float min, float max)
    {
        return Math.Min(max, Math.Max(min, value));
    }
}
