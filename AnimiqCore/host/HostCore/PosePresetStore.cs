using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace HostCore;

public sealed record PosePresetModel(
    string Name,
    List<PoseBoneUiOffset> Offsets,
    PoseFilterProfile FilterProfile,
    float PoseDeadbandDeg);

public sealed class PosePresetStoreModel
{
    public const int CurrentVersion = 1;

    public int Version { get; init; } = CurrentVersion;
    public List<PosePresetModel> Presets { get; init; } = new();
    public string? LastSelectedPresetName { get; set; }

    public static PosePresetStoreModel CreateDefault()
    {
        var zeroOffsets = BuildZeroOffsets();
        return new PosePresetStoreModel
        {
            Version = CurrentVersion,
            Presets = new List<PosePresetModel>
            {
                new("Stable Default", CloneOffsets(zeroOffsets), PoseFilterProfile.Stable, 0.9f),
                new("Balanced", CloneOffsets(zeroOffsets), PoseFilterProfile.Balanced, 0.55f),
                new("Reactive", CloneOffsets(zeroOffsets), PoseFilterProfile.Reactive, 0.25f),
            },
            LastSelectedPresetName = "Stable Default",
        };
    }

    private static List<PoseBoneUiOffset> BuildZeroOffsets()
    {
        return new List<PoseBoneUiOffset>
        {
            new(PoseBoneKind.Hips, 0.0f, 0.0f, 0.0f),
            new(PoseBoneKind.Spine, 0.0f, 0.0f, 0.0f),
            new(PoseBoneKind.Chest, 0.0f, 0.0f, 0.0f),
            new(PoseBoneKind.UpperChest, 0.0f, 0.0f, 0.0f),
            new(PoseBoneKind.Neck, 0.0f, 0.0f, 0.0f),
            new(PoseBoneKind.Head, 0.0f, 0.0f, 0.0f),
            new(PoseBoneKind.LeftUpperArm, 0.0f, 0.0f, 0.0f),
            new(PoseBoneKind.RightUpperArm, 0.0f, 0.0f, 0.0f),
            new(PoseBoneKind.LeftShoulder, 0.0f, 0.0f, 0.0f),
            new(PoseBoneKind.RightShoulder, 0.0f, 0.0f, 0.0f),
            new(PoseBoneKind.LeftLowerArm, 0.0f, 0.0f, 0.0f),
            new(PoseBoneKind.RightLowerArm, 0.0f, 0.0f, 0.0f),
            new(PoseBoneKind.LeftHand, 0.0f, 0.0f, 0.0f),
            new(PoseBoneKind.RightHand, 0.0f, 0.0f, 0.0f),
        };
    }

    internal static List<PoseBoneUiOffset> CloneOffsets(IEnumerable<PoseBoneUiOffset> src)
    {
        return src.Select(static p => new PoseBoneUiOffset(p.Bone, p.PitchDeg, p.YawDeg, p.RollDeg)).ToList();
    }
}

public sealed class PosePresetStore : IPosePresetStore
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = true,
    };

    private readonly string _storagePath;

    public PosePresetStore(string? storagePath = null)
    {
        var root = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        _storagePath = storagePath ??
            Path.Combine(root, "AnimiqHost", "pose_presets.json");
    }

    public PosePresetStoreModel Load()
    {
        if (!File.Exists(_storagePath))
        {
            return PosePresetStoreModel.CreateDefault();
        }

        try
        {
            var json = File.ReadAllText(_storagePath);
            var model = JsonSerializer.Deserialize<PosePresetStoreModel>(json, JsonOptions);
            if (model is null)
            {
                return PosePresetStoreModel.CreateDefault();
            }

            var normalized = Normalize(model);
            return normalized.Presets.Count == 0
                ? PosePresetStoreModel.CreateDefault()
                : normalized;
        }
        catch
        {
            TryBackupCorruptFile();
            return PosePresetStoreModel.CreateDefault();
        }
    }

    public void Save(PosePresetStoreModel store)
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

    private static PosePresetStoreModel Normalize(PosePresetStoreModel input)
    {
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var presets = new List<PosePresetModel>();
        foreach (var preset in input.Presets ?? new List<PosePresetModel>())
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

            var offsets = NormalizeOffsets(preset.Offsets);
            var filterProfile = Enum.IsDefined(typeof(PoseFilterProfile), preset.FilterProfile)
                ? preset.FilterProfile
                : PoseFilterProfile.Stable;

            presets.Add(new PosePresetModel(
                normalizedName,
                offsets,
                filterProfile,
                Clamp(preset.PoseDeadbandDeg, 0.0f, 3.0f)));
        }

        var lastSelected = input.LastSelectedPresetName;
        if (string.IsNullOrWhiteSpace(lastSelected) ||
            presets.All(p => !string.Equals(p.Name, lastSelected, StringComparison.OrdinalIgnoreCase)))
        {
            lastSelected = presets.Count > 0 ? presets[0].Name : null;
        }

        return new PosePresetStoreModel
        {
            Version = PosePresetStoreModel.CurrentVersion,
            Presets = presets,
            LastSelectedPresetName = lastSelected,
        };
    }

    private static List<PoseBoneUiOffset> NormalizeOffsets(IEnumerable<PoseBoneUiOffset>? source)
    {
        static bool IsArmLinkedBone(PoseBoneKind bone)
        {
            return bone == PoseBoneKind.LeftShoulder ||
                bone == PoseBoneKind.RightShoulder ||
                bone == PoseBoneKind.LeftLowerArm ||
                bone == PoseBoneKind.RightLowerArm ||
                bone == PoseBoneKind.LeftHand ||
                bone == PoseBoneKind.RightHand;
        }

        var map = (source ?? Enumerable.Empty<PoseBoneUiOffset>())
            .GroupBy(x => x.Bone)
            .ToDictionary(
                g => g.Key,
                g => g.Last(),
                EqualityComparer<PoseBoneKind>.Default);

        var allBones = new[]
        {
            PoseBoneKind.Hips,
            PoseBoneKind.Spine,
            PoseBoneKind.Chest,
            PoseBoneKind.UpperChest,
            PoseBoneKind.Neck,
            PoseBoneKind.Head,
            PoseBoneKind.LeftUpperArm,
            PoseBoneKind.RightUpperArm,
            PoseBoneKind.LeftShoulder,
            PoseBoneKind.RightShoulder,
            PoseBoneKind.LeftLowerArm,
            PoseBoneKind.RightLowerArm,
            PoseBoneKind.LeftHand,
            PoseBoneKind.RightHand,
        };

        var output = new List<PoseBoneUiOffset>(allBones.Length);
        foreach (var bone in allBones)
        {
            if (!map.TryGetValue(bone, out var src))
            {
                output.Add(new PoseBoneUiOffset(bone, 0.0f, 0.0f, 0.0f));
                continue;
            }

            output.Add(new PoseBoneUiOffset(
                bone,
                IsArmLinkedBone(bone) ? 0.0f : Clamp(src.PitchDeg, -180.0f, 180.0f),
                Clamp(src.YawDeg, -180.0f, 180.0f),
                Clamp(src.RollDeg, -180.0f, 180.0f)));
        }

        return output;
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
            // Best-effort backup only.
        }
    }

    private static float Clamp(float value, float min, float max)
    {
        return Math.Min(max, Math.Max(min, value));
    }
}
