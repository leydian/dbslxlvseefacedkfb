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
}
