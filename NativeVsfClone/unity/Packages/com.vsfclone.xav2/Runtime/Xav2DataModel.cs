using System;
using System.Collections.Generic;

namespace VsfClone.Xav2.Runtime
{
    public enum Xav2LoadErrorCode
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

    public enum Xav2UnknownSectionPolicy
    {
        Warn = 0,
        Ignore,
        Fail
    }

    public enum Xav2ShaderPolicy
    {
        WarnFallback = 0,
        Fail
    }

    /// <summary>
    /// Loader policy options for <see cref="Xav2RuntimeLoader.TryLoad(string,out Xav2AvatarPayload,out Xav2LoadDiagnostics,Xav2LoadOptions)"/>.
    /// </summary>
    public sealed class Xav2LoadOptions
    {
        /// <summary>
        /// Enables strict payload validation. Default is <c>false</c>.
        /// </summary>
        public bool StrictValidation { get; set; }

        /// <summary>
        /// Unknown section handling policy. Default is <see cref="Xav2UnknownSectionPolicy.Warn"/>.
        /// </summary>
        public Xav2UnknownSectionPolicy UnknownSectionPolicy { get; set; } = Xav2UnknownSectionPolicy.Warn;

        /// <summary>
        /// Shader compatibility policy. Default is <see cref="Xav2ShaderPolicy.WarnFallback"/>.
        /// </summary>
        public Xav2ShaderPolicy ShaderPolicy { get; set; } = Xav2ShaderPolicy.WarnFallback;
    }

    /// <summary>
    /// Structured diagnostics returned by runtime load APIs.
    /// </summary>
    public sealed class Xav2LoadDiagnostics
    {
        public Xav2LoadErrorCode ErrorCode { get; set; } = Xav2LoadErrorCode.None;
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
    public sealed class Xav2Manifest
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
    public sealed class Xav2TypedFloatParam
    {
        public string Id = string.Empty;
        public float Value;
    }

    [Serializable]
    public sealed class Xav2TypedColorParam
    {
        public string Id = string.Empty;
        public float R;
        public float G;
        public float B;
        public float A = 1.0f;
    }

    [Serializable]
    public sealed class Xav2TypedTextureParam
    {
        public string Slot = string.Empty;
        public string TextureRef = string.Empty;
    }

    public sealed class Xav2BlendShapeFramePayload
    {
        public string Name = string.Empty;
        public float Weight;
        public byte[] DeltaVertices = Array.Empty<byte>();
        public byte[] DeltaNormals = Array.Empty<byte>();
        public byte[] DeltaTangents = Array.Empty<byte>();
    }

    public sealed class Xav2SkinPayload
    {
        public string MeshName = string.Empty;
        public int[] BoneIndices = Array.Empty<int>();
        public float[] BindPoses16xN = Array.Empty<float>();
        public byte[] SkinWeightBlob = Array.Empty<byte>();
    }

    public sealed class Xav2SkeletonPayload
    {
        public string MeshName = string.Empty;
        public float[] BoneMatrices16xN = Array.Empty<float>();
    }

    public sealed class Xav2RigBonePayload
    {
        public string Name = string.Empty;
        public int ParentIndex = -1;
        public float[] LocalMatrix16 = Array.Empty<float>();
    }

    public sealed class Xav2SkeletonRigPayload
    {
        public string MeshName = string.Empty;
        public List<Xav2RigBonePayload> Bones = new List<Xav2RigBonePayload>();
    }

    public sealed class Xav2BlendShapePayload
    {
        public string MeshName = string.Empty;
        public List<Xav2BlendShapeFramePayload> Frames = new List<Xav2BlendShapeFramePayload>();
    }

    public sealed class Xav2MeshPayload
    {
        public string Name = string.Empty;
        public uint VertexStride = 20U;
        public int MaterialIndex = -1;
        public byte[] VertexBlob = Array.Empty<byte>();
        public uint[] Indices = Array.Empty<uint>();
    }

    public sealed class Xav2MaterialPayload
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
        public List<Xav2TypedFloatParam> TypedFloatParams = new List<Xav2TypedFloatParam>();
        public List<Xav2TypedColorParam> TypedColorParams = new List<Xav2TypedColorParam>();
        public List<Xav2TypedTextureParam> TypedTextureParams = new List<Xav2TypedTextureParam>();
    }

    public sealed class Xav2TexturePayload
    {
        public string Name = string.Empty;
        public byte[] Bytes = Array.Empty<byte>();
    }

    public enum Xav2PhysicsColliderShape : byte
    {
        Sphere = 0,
        Capsule = 1,
        Plane = 2,
        Unknown = 255
    }

    public sealed class Xav2PhysicsColliderPayload
    {
        public string Name = string.Empty;
        public string BonePath = string.Empty;
        public Xav2PhysicsColliderShape Shape = Xav2PhysicsColliderShape.Unknown;
        public float Radius;
        public float Height;
        public float[] LocalPosition = new float[3];
        public float[] LocalDirection = new float[3];
    }

    public sealed class Xav2SpringBonePayload
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

    public sealed class Xav2PhysBonePayload
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

    public sealed class Xav2AvatarPayload
    {
        public Xav2Manifest Manifest = new Xav2Manifest();
        public List<Xav2MeshPayload> Meshes = new List<Xav2MeshPayload>();
        public List<Xav2MaterialPayload> Materials = new List<Xav2MaterialPayload>();
        public List<Xav2TexturePayload> Textures = new List<Xav2TexturePayload>();
        public List<Xav2SkinPayload> Skins = new List<Xav2SkinPayload>();
        public List<Xav2SkeletonPayload> Skeletons = new List<Xav2SkeletonPayload>();
        public List<Xav2SkeletonRigPayload> SkeletonRigs = new List<Xav2SkeletonRigPayload>();
        public List<Xav2BlendShapePayload> BlendShapes = new List<Xav2BlendShapePayload>();
        public List<Xav2PhysicsColliderPayload> PhysicsColliders = new List<Xav2PhysicsColliderPayload>();
        public List<Xav2SpringBonePayload> SpringBones = new List<Xav2SpringBonePayload>();
        public List<Xav2PhysBonePayload> PhysBones = new List<Xav2PhysBonePayload>();
    }
}
