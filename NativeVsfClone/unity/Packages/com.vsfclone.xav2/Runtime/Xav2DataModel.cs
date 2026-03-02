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
        StrictValidationFailed
    }

    public sealed class Xav2LoadOptions
    {
        public bool StrictValidation;
    }

    public sealed class Xav2LoadDiagnostics
    {
        public Xav2LoadErrorCode ErrorCode = Xav2LoadErrorCode.None;
        public string ErrorMessage = string.Empty;
        public string ParserStage = "header";
        public bool IsPartial;
        public List<string> Warnings = new();
    }

    [Serializable]
    public sealed class Xav2Manifest
    {
        public uint schemaVersion = 1U;
        public string exporterVersion = "0.3.0";
        public string avatarId = string.Empty;
        public string displayName = string.Empty;
        public string sourceExt = ".vrm";
        public List<string> meshRefs = new();
        public List<string> materialRefs = new();
        public List<string> textureRefs = new();
        public List<string> strictShaderSet = new();
        public bool hasSkinning;
        public bool hasBlendShapes;
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

    public sealed class Xav2BlendShapePayload
    {
        public string MeshName = string.Empty;
        public List<Xav2BlendShapeFramePayload> Frames = new();
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
        public string BaseColorTextureName = string.Empty;
        public string AlphaMode = "OPAQUE";
        public float AlphaCutoff = 0.5f;
        public bool DoubleSided;
        public string ShaderParamsJson = "{}";
    }

    public sealed class Xav2TexturePayload
    {
        public string Name = string.Empty;
        public byte[] Bytes = Array.Empty<byte>();
    }

    public sealed class Xav2AvatarPayload
    {
        public Xav2Manifest Manifest = new();
        public List<Xav2MeshPayload> Meshes = new();
        public List<Xav2MaterialPayload> Materials = new();
        public List<Xav2TexturePayload> Textures = new();
        public List<Xav2SkinPayload> Skins = new();
        public List<Xav2BlendShapePayload> BlendShapes = new();
    }
}
