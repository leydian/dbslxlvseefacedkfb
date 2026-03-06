using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;

namespace HostCore;

internal static class Arkit52Channels
{
    public static readonly ReadOnlyCollection<string> CanonicalOrder = Array.AsReadOnly(
        new[]
        {
            "browDownLeft", "browDownRight", "browInnerUp", "browOuterUpLeft", "browOuterUpRight",
            "cheekPuff", "cheekSquintLeft", "cheekSquintRight", "eyeBlinkLeft", "eyeBlinkRight",
            "eyeLookDownLeft", "eyeLookDownRight", "eyeLookInLeft", "eyeLookInRight", "eyeLookOutLeft",
            "eyeLookOutRight", "eyeLookUpLeft", "eyeLookUpRight", "eyeSquintLeft", "eyeSquintRight",
            "eyeWideLeft", "eyeWideRight", "jawForward", "jawLeft", "jawOpen",
            "jawRight", "mouthClose", "mouthDimpleLeft", "mouthDimpleRight", "mouthFrownLeft",
            "mouthFrownRight", "mouthFunnel", "mouthLeft", "mouthLowerDownLeft", "mouthLowerDownRight",
            "mouthPressLeft", "mouthPressRight", "mouthPucker", "mouthRight", "mouthRollLower",
            "mouthRollUpper", "mouthShrugLower", "mouthShrugUpper", "mouthSmileLeft", "mouthSmileRight",
            "mouthStretchLeft", "mouthStretchRight", "mouthUpperUpLeft", "mouthUpperUpRight", "noseSneerLeft",
            "noseSneerRight", "tongueOut",
        });

    public static readonly ReadOnlyCollection<string> NormalizedOrder = Array.AsReadOnly(
        CanonicalOrder
            .Select(NormalizeKey)
            .ToArray());

    public static readonly IReadOnlySet<string> NormalizedSet =
        new HashSet<string>(NormalizedOrder, StringComparer.OrdinalIgnoreCase);

    public static readonly IReadOnlyDictionary<string, ReadOnlyCollection<string>> FallbackCandidatesByChannel =
        new Dictionary<string, ReadOnlyCollection<string>>(StringComparer.OrdinalIgnoreCase)
        {
            ["eyeblinkleft"] = ToNormalizedList("blinkl", "blinkleft", "eyecloseleft"),
            ["eyeblinkright"] = ToNormalizedList("blinkr", "blinkright", "eyecloseright"),
            ["jawopen"] = ToNormalizedList("mouthopen", "visemeaa", "aa"),
            ["mouthsmileleft"] = ToNormalizedList("smileleft", "smilel", "smile"),
            ["mouthsmileright"] = ToNormalizedList("smileright", "smiler", "smile"),
            ["browinnerup"] = ToNormalizedList("browup", "browsup", "innerbrowup"),
            ["mouthfunnel"] = ToNormalizedList("funnel", "moutho", "visemeo"),
            ["mouthpucker"] = ToNormalizedList("pucker", "mouthu", "visemeu"),
        };

    public static string NormalizeKey(string raw)
    {
        if (string.IsNullOrWhiteSpace(raw))
        {
            return string.Empty;
        }

        var sb = new System.Text.StringBuilder(raw.Length);
        foreach (var ch in raw)
        {
            if (char.IsLetterOrDigit(ch))
            {
                sb.Append(char.ToLowerInvariant(ch));
            }
        }
        return sb.ToString();
    }

    private static ReadOnlyCollection<string> ToNormalizedList(params string[] items)
    {
        return Array.AsReadOnly(
            items
                .Select(NormalizeKey)
                .Where(static s => !string.IsNullOrWhiteSpace(s))
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .ToArray());
    }
}
