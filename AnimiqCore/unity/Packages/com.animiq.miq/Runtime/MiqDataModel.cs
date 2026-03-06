using System;
using System.Collections.Generic;

namespace Animiq.Miq.Runtime
{
    public enum MiqLoadErrorCode
    {
        None = 0,
        IoError,
        MagicMismatch,
        UnsupportedVersion,
        ManifestTruncated,
        ManifestInvalid,
        MissingRequiredManifestKeys,
        SectionHeaderTruncated,
        SectionTruncated,
        SectionSchemaInvalid,
        CompressionDecodeFailed,
        UnknownSectionNotAllowed,
        StrictValidationFailed,
        ParityContractViolation
    }

    public enum MiqUnknownSectionPolicy
    {
        Warn = 0,
        Ignore,
        Fail
    }

    public enum MiqShaderPolicy
    {
        WarnFallback = 0,
        Fail
    }

    /// <summary>
    /// Loader policy options for <see cref="MiqRuntimeLoader.TryLoad(string,out MiqAvatarPayload,out MiqLoadDiagnostics,MiqLoadOptions)"/>.
    /// </summary>
    public sealed class MiqLoadOptions
    {
        /// <summary>
        /// Enables strict payload validation. Default is <c>false</c>.
        /// </summary>
        public bool StrictValidation { get; set; }

        /// <summary>
        /// Unknown section handling policy. Default is <see cref="MiqUnknownSectionPolicy.Warn"/>.
        /// </summary>
        public MiqUnknownSectionPolicy UnknownSectionPolicy { get; set; } = MiqUnknownSectionPolicy.Warn;

        /// <summary>
        /// Shader compatibility policy. Default is <see cref="MiqShaderPolicy.WarnFallback"/>.
        /// </summary>
        public MiqShaderPolicy ShaderPolicy { get; set; } = MiqShaderPolicy.WarnFallback;
    }

    /// <summary>
    /// Structured diagnostics returned by runtime load APIs.
    /// </summary>
    public sealed class MiqLoadDiagnostics
    {
        public MiqLoadErrorCode ErrorCode { get; set; } = MiqLoadErrorCode.None;
        public string ErrorMessage { get; set; } = string.Empty;
        public string ParserStage { get; set; } = "header";
        public bool IsPartial { get; set; }
        public bool MigrationApplied { get; set; }
        public int SourceFormatVersion { get; set; }
        public string SourceMaterialParamEncoding { get; set; } = "legacy-json";
        public bool CriticalParityViolation { get; set; }
        public List<string> Warnings { get; } = new List<string>();
        public List<string> WarningCodes { get; } = new List<string>();
    }

    [Serializable]
    public sealed class MiqManifest
    {
        public uint schemaVersion = 1U;
        public string exporterVersion = "0.3.0";
        public uint physicsSchemaVersion = 1U;
        public string avatarId = string.Empty;
        public string displayName = string.Empty;
        public string sourceExt = ".vrm";
        public string physicsSource = "none";
        public List<string> meshRefs = new List<string>();
        public List<string> materialRefs = new List<string>();
        public List<string> textureRefs = new List<string>();
        public List<string> strictShaderSet = new List<string>();
        public string materialParamEncoding = "legacy-json";
        public bool hasSkinning;
        public bool hasBlendShapes;
        public bool hasSpringBones;
        public bool hasPhysBones;
    }

    [Serializable]
    public sealed class MiqTypedFloatParam
    {
        public string Id = string.Empty;
        public float Value;
    }

    [Serializable]
    public sealed class MiqTypedColorParam
    {
        public string Id = string.Empty;
        public float R;
        public float G;
        public float B;
        public float A = 1.0f;
    }

    [Serializable]
    public sealed class MiqTypedTextureParam
    {
        public string Slot = string.Empty;
        public string TextureRef = string.Empty;
    }

    public sealed class MiqBlendShapeFramePayload
    {
        public string Name = string.Empty;
        public float Weight;
        public byte[] DeltaVertices = Array.Empty<byte>();
        public byte[] DeltaNormals = Array.Empty<byte>();
        public byte[] DeltaTangents = Array.Empty<byte>();
    }

    public sealed class MiqSkinPayload
    {
        public string MeshName = string.Empty;
        public int[] BoneIndices = Array.Empty<int>();
        public float[] BindPoses16xN = Array.Empty<float>();
        public byte[] SkinWeightBlob = Array.Empty<byte>();
    }

    public sealed class MiqSkeletonPayload
    {
        public string MeshName = string.Empty;
        public float[] BoneMatrices16xN = Array.Empty<float>();
    }

    public sealed class MiqRigBonePayload
    {
        public string Name = string.Empty;
        public int ParentIndex = -1;
        public float[] LocalMatrix16 = Array.Empty<float>();
    }

    public sealed class MiqSkeletonRigPayload
    {
        public string MeshName = string.Empty;
        public List<MiqRigBonePayload> Bones = new List<MiqRigBonePayload>();
    }

    public sealed class MiqBlendShapePayload
    {
        public string MeshName = string.Empty;
        public List<MiqBlendShapeFramePayload> Frames = new List<MiqBlendShapeFramePayload>();
    }

    public sealed class MiqMeshPayload
    {
        public string Name = string.Empty;
        public uint VertexStride = 20U;
        public int MaterialIndex = -1;
        public byte[] VertexBlob = Array.Empty<byte>();
        public uint[] Indices = Array.Empty<uint>();
    }

    public sealed class MiqMaterialPayload
    {
        public string Name = string.Empty;
        public string ShaderName = "MToon (minimal)";
        public string ShaderVariant = "default";
        public string ShaderFamily = "legacy";
        public string KeywordSet = "[]";
        public string RenderState = "auto";
        public string PassFlags = "base";
        public string MaterialParamEncoding = "legacy-json";
        public ushort TypedSchemaVersion;
        public uint FeatureFlags;
        public string BaseColorTextureName = string.Empty;
        public string AlphaMode = "OPAQUE";
        public float AlphaCutoff = 0.5f;
        public bool DoubleSided;
        public string ShaderParamsJson = "{}";
        public List<MiqTypedFloatParam> TypedFloatParams = new List<MiqTypedFloatParam>();
        public List<MiqTypedColorParam> TypedColorParams = new List<MiqTypedColorParam>();
        public List<MiqTypedTextureParam> TypedTextureParams = new List<MiqTypedTextureParam>();
    }

    public sealed class MiqTexturePayload
    {
        public string Name = string.Empty;
        public byte[] Bytes = Array.Empty<byte>();
    }

    public enum MiqPhysicsColliderShape : byte
    {
        Sphere = 0,
        Capsule = 1,
        Plane = 2,
        Unknown = 255
    }

    public sealed class MiqPhysicsColliderPayload
    {
        public string Name = string.Empty;
        public string BonePath = string.Empty;
        public MiqPhysicsColliderShape Shape = MiqPhysicsColliderShape.Unknown;
        public float Radius;
        public float Height;
        public float[] LocalPosition = new float[3];
        public float[] LocalDirection = new float[3];
    }

    public sealed class MiqSpringBonePayload
    {
        public string Name = string.Empty;
        public string RootBonePath = string.Empty;
        public List<string> BonePaths = new List<string>();
        public List<string> ColliderRefs = new List<string>();
        public float Stiffness;
        public float Drag;
        public float Radius;
        public float[] Gravity = new float[3];
        public bool Enabled = true;
    }

    public sealed class MiqPhysBonePayload
    {
        public string Name = string.Empty;
        public string RootBonePath = string.Empty;
        public List<string> BonePaths = new List<string>();
        public List<string> ColliderRefs = new List<string>();
        public float Pull;
        public float Spring;
        public float Immobile;
        public float Radius;
        public float[] Gravity = new float[3];
        public bool Enabled = true;
    }

    public sealed class MiqAvatarPayload
    {
        public MiqManifest Manifest = new MiqManifest();
        public List<MiqMeshPayload> Meshes = new List<MiqMeshPayload>();
        public List<MiqMaterialPayload> Materials = new List<MiqMaterialPayload>();
        public List<MiqTexturePayload> Textures = new List<MiqTexturePayload>();
        public List<MiqSkinPayload> Skins = new List<MiqSkinPayload>();
        public List<MiqSkeletonPayload> Skeletons = new List<MiqSkeletonPayload>();
        public List<MiqSkeletonRigPayload> SkeletonRigs = new List<MiqSkeletonRigPayload>();
        public List<MiqBlendShapePayload> BlendShapes = new List<MiqBlendShapePayload>();
        public List<MiqPhysicsColliderPayload> PhysicsColliders = new List<MiqPhysicsColliderPayload>();
        public List<MiqSpringBonePayload> SpringBones = new List<MiqSpringBonePayload>();
        public List<MiqPhysBonePayload> PhysBones = new List<MiqPhysBonePayload>();
    }
}
