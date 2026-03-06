using System;
using UnityEngine;
using Animiq.Miq.Runtime;

namespace Animiq.Miq.Samples
{
    public sealed class RuntimeLoadSample : MonoBehaviour
    {
        [Tooltip("Absolute path to a .miq file on disk.")]
        public string MiqPath = string.Empty;

        [Tooltip("Enable strict validation when loading.")]
        public bool StrictValidation;

        [Tooltip("Unknown section handling policy.")]
        public MiqUnknownSectionPolicy UnknownSectionPolicy = MiqUnknownSectionPolicy.Warn;

        [ContextMenu("Load MIQ")]
        public void LoadNow()
        {
            if (string.IsNullOrWhiteSpace(MiqPath))
            {
                Debug.LogError("[RuntimeLoadSample] MiqPath is empty.");
                return;
            }

            var options = new MiqLoadOptions
            {
                StrictValidation = StrictValidation,
                UnknownSectionPolicy = UnknownSectionPolicy
            };

            if (MiqRuntimeLoader.TryLoad(MiqPath, out var payload, out var diagnostics, options))
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
