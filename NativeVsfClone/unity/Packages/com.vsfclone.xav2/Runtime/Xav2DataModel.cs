using System;
using System.Collections.Generic;

namespace VsfClone.Xav2.Runtime
{
    [Serializable]
    public sealed class Xav2Manifest
    {
        public string avatarId = string.Empty;
        public string displayName = string.Empty;
        public string sourceExt = ".vrm";
        public List<string> meshRefs = new();
        public List<string> materialRefs = new();
        public List<string> textureRefs = new();
        public List<string> strictShaderSet = new();
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
    }
}
