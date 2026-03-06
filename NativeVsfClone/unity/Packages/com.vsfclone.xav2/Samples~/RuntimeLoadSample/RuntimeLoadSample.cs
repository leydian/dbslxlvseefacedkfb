using System;
using UnityEngine;
using VsfClone.Xav2.Runtime;

namespace VsfClone.Xav2.Samples
{
    public sealed class RuntimeLoadSample : MonoBehaviour
    {
        [Tooltip("Absolute path to a .xav2 file on disk.")]
        public string Xav2Path = string.Empty;

        [Tooltip("Enable strict validation when loading.")]
        public bool StrictValidation;

        [Tooltip("Unknown section handling policy.")]
        public Xav2UnknownSectionPolicy UnknownSectionPolicy = Xav2UnknownSectionPolicy.Warn;

        [ContextMenu("Load XAV2")]
        public void LoadNow()
        {
            if (string.IsNullOrWhiteSpace(Xav2Path))
            {
                Debug.LogError("[RuntimeLoadSample] Xav2Path is empty.");
                return;
            }

            var options = new Xav2LoadOptions
            {
                StrictValidation = StrictValidation,
                UnknownSectionPolicy = UnknownSectionPolicy
            };

            if (Xav2RuntimeLoader.TryLoad(Xav2Path, out var payload, out var diagnostics, options))
            {
                Debug.Log(
                    $"[RuntimeLoadSample] Success avatarId={payload.Manifest.avatarId}, " +
                    $"meshes={payload.Meshes.Count}, materials={payload.Materials.Count}, textures={payload.Textures.Count}");
                return;
            }

            Debug.LogError(
                $"[RuntimeLoadSample] Fail code={diagnostics.ErrorCode}, stage={diagnostics.ParserStage}, " +
                $"message={diagnostics.ErrorMessage}");
            if (diagnostics.Warnings.Count > 0)
            {
                Debug.LogWarning($"[RuntimeLoadSample] warnings={string.Join(", ", diagnostics.Warnings)}");
            }
        }
    }
}
