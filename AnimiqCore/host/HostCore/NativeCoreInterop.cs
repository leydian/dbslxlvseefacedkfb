using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace HostCore;

public enum NcResultCode : uint
{
    Ok = 0,
    NotInitialized = 1,
    InvalidArgument = 2,
    Io = 3,
    Unsupported = 4,
    Internal = 5,
}

public enum NcAvatarFormatHint : uint
{
    Auto = 0,
    Vrm = 1,
    VxAvatar = 2,
    VsfAvatar = 3,
    Vxa2 = 4,
    Miq = 5,
}

public enum NcCameraMode : uint
{
    AutoFitFull = 0,
    AutoFitBust = 1,
    Manual = 2,
}

public enum NcRenderQualityProfile : uint
{
    Default = 0,
    Balanced = 1,
    UltraParity = 2,
    FastFallback = 3,
}

public enum NcPoseBoneId : uint
{
    Unknown = 0,
    Hips = 1,
    Spine = 2,
    Chest = 3,
    UpperChest = 4,
    Neck = 5,
    Head = 6,
    LeftUpperArm = 7,
    RightUpperArm = 8,
    LeftShoulder = 9,
    RightShoulder = 10,
    LeftLowerArm = 11,
    RightLowerArm = 12,
    LeftHand = 13,
    RightHand = 14,
}

[StructLayout(LayoutKind.Sequential)]
public struct NcInitOptions
{
    public uint ApiVersion;
    public uint Reserved;
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
public struct NcAvatarLoadRequest
{
    public string Path;
    public NcAvatarFormatHint FormatHint;
    public uint ShaderProfile;
    public uint FallbackPolicy;
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
public struct NcAvatarInfo
{
    public ulong Handle;
    public NcAvatarFormatHint DetectedFormat;
    public uint CompatLevel;
    public uint MeshCount;
    public uint MaterialCount;
    public uint MeshPayloadCount;
    public uint MaterialPayloadCount;
    public uint TexturePayloadCount;
    public uint FormatSectionCount;
    public uint FormatDecodedSectionCount;
    public uint FormatUnknownSectionCount;
    public uint WarningCount;
    public uint WarningCodeCount;
    public uint CriticalWarningCount;
    public uint MaterialDiagCount;
    public uint OpaqueMaterialCount;
    public uint MaskMaterialCount;
    public uint BlendMaterialCount;
    public uint MissingFeatureCount;
    public uint ExpressionCount;
    public uint LastRenderDrawCalls;
    public uint SpringActiveChainCount;
    public uint SpringCorrectedChainCount;
    public uint SpringDisabledChainCount;
    public uint SpringUnsupportedColliderChainCount;
    public uint MtoonAdvancedParamMaterialCount;
    public uint MtoonFallbackMaterialCount;
    public float SpringAvgSubsteps;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
    public string DisplayName;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
    public string SourcePath;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
    public string ParserStage;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
    public string PrimaryErrorCode;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
    public string LastWarningCode;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 16)]
    public string LastWarningSeverity;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 16)]
    public string LastWarningCategory;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
    public string LastExpressionSummary;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
    public string LastWarning;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
    public string LastMaterialDiag;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
    public string LastRenderPassSummary;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
    public string LastMissingFeature;
    public float ParityScore;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
    public string VariantId;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
    public string ParityFallbackReason;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 16)]
    public string QualityMode;
    public uint FamilyBackendFallbackCount;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
    public string SelectedFamilyBackend;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
    public string ActivePasses;
    public uint MaterialParityMismatchCount;
    public uint TextureResolveAmbiguousCount;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
    public string MaterialParityLastMismatch;
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
public struct NcExpressionInfo
{
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
    public string Name;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
    public string MappingKind;
    public float DefaultWeight;
    public float RuntimeWeight;
    public uint BindCount;
}

[StructLayout(LayoutKind.Sequential)]
public struct NcSpringBoneInfo
{
    public uint Present;
    public uint SpringCount;
    public uint JointCount;
    public uint ColliderCount;
    public uint ColliderGroupCount;
    public uint ActiveChainCount;
    public uint CorrectedChainCount;
    public uint DisabledChainCount;
    public uint UnsupportedColliderChainCount;
    public float AvgSubsteps;
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
public struct NcAvatarRuntimeMetricsV2
{
    public uint SpringActiveChainCount;
    public uint SpringConstraintHitCount;
    public uint SpringDampingEventCount;
    public float SpringAvgOffsetMagnitude;
    public float SpringPeakOffsetMagnitude;
    public uint MtoonOutlineMaterialCount;
    public uint MtoonUvAnimMaterialCount;
    public uint MtoonMatcapMaterialCount;
    public uint MtoonBlendMaterialCount;
    public uint MtoonMaskMaterialCount;
    public float LastFrameMs;
    public float TargetFrameMs;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
    public string PhysicsSolver;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
    public string MtoonRuntimeMode;
    public int ContractPreviewYawDeg;
    public uint TransformConfidenceLevel;
    public uint IsVrmOriginMiq;
    public uint PreviewBoundsExcludedMeshCount;
    public uint PreviewHairCandidateMeshCount;
    public float PreviewHairHeadAlignmentScore;
}

[StructLayout(LayoutKind.Sequential)]
public struct NcTrackingFrame
{
    public float HeadPosX;
    public float HeadPosY;
    public float HeadPosZ;
    public float HeadRotX;
    public float HeadRotY;
    public float HeadRotZ;
    public float HeadRotW;
    public float EyeGazeLX;
    public float EyeGazeLY;
    public float EyeGazeLZ;
    public float EyeGazeRX;
    public float EyeGazeRY;
    public float EyeGazeRZ;
    public float BlinkL;
    public float BlinkR;
    public float MouthOpen;
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
public struct NcExpressionWeight
{
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
    public string Name;
    public float Weight;
}

[StructLayout(LayoutKind.Sequential)]
public struct NcSpoutOptions
{
    public uint Width;
    public uint Height;
    public uint Fps;
    [MarshalAs(UnmanagedType.LPStr)]
    public string ChannelName;
}

[StructLayout(LayoutKind.Sequential)]
public struct NcSpoutReceiverOptions
{
    [MarshalAs(UnmanagedType.LPStr)]
    public string ChannelName;
    public uint ForceLinear;
}

[StructLayout(LayoutKind.Sequential)]
public struct NcOscOptions
{
    public ushort BindPort;
    [MarshalAs(UnmanagedType.LPStr)]
    public string PublishAddress;
}

[StructLayout(LayoutKind.Sequential)]
public struct NcWindowRenderTarget
{
    public IntPtr Hwnd;
    public uint Width;
    public uint Height;
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
public struct NcThumbnailRequest
{
    public ulong Handle;
    [MarshalAs(UnmanagedType.LPStr)]
    public string OutputPath;
    public uint Width;
    public uint Height;
    public float DeltaTimeSeconds;
}

[StructLayout(LayoutKind.Sequential)]
public struct NcRenderQualityOptions
{
    public NcCameraMode CameraMode;
    public float FramingTarget;
    public float Headroom;
    public float YawDeg;
    public float FovDeg;
    public float BackgroundR;
    public float BackgroundG;
    public float BackgroundB;
    public float BackgroundA;
    public uint QualityProfile;
    public uint ShowDebugOverlay;
}

[StructLayout(LayoutKind.Sequential)]
public struct NcLightingOptions
{
    public float LightPositionX;
    public float LightPositionY;
    public float LightPositionZ;
    public float LightEulerPitchDeg;
    public float LightEulerYawDeg;
    public float LightEulerRollDeg;
    public float Intensity;
    public float Range;
    public float SpotAngleDeg;
    public float InnerSpotAngleDeg;
    public float ShadowStrength;
    public float ShadowBias;
    public float ShadowNormalBias;
    public float ShadowNearPlane;
    public uint ShadowResolution;
    public float AmbientIntensity;
    public uint EnableSunLight;
    public uint EnableShadow;
}

[StructLayout(LayoutKind.Sequential)]
public struct NcPoseBoneOffset
{
    public NcPoseBoneId BoneId;
    public float PitchDeg;
    public float YawDeg;
    public float RollDeg;
}

[StructLayout(LayoutKind.Sequential)]
public struct NcRuntimeStats
{
    public uint RenderReadyAvatarCount;
    public uint SpoutActive;
    public uint OscActive;
    public float LastFrameMs;
    public float GpuFrameMs;
    public float CpuFrameMs;
    public float MaterialResolveMs;
    public uint PassCount;
}

public enum NcSpoutBackendKind : uint
{
    Inactive = 0,
    LegacySharedMemory = 1,
    Spout2Gpu = 2,
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
public struct NcSpoutDiagnostics
{
    public uint BackendKind;
    public uint StrictMode;
    public ulong FallbackCount;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
    public string LastErrorCode;
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
public struct NcSpoutReceiverDiagnostics
{
    public uint Active;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
    public string ChannelName;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
    public string LastErrorCode;
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
public struct NcErrorInfo
{
    public NcResultCode Code;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
    public string Subsystem;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
    public string Message;
    public byte Recoverable;
}

public static class NativeCoreInterop
{
    private const string DllName = "nativecore.dll";
    private const int MaxModulePathChars = 1024;

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr GetModuleHandleW(string moduleName);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern uint GetModuleFileNameW(IntPtr module, StringBuilder fileName, int size);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_initialize(ref NcInitOptions options);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_shutdown();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern NcResultCode nc_load_avatar(
        ref NcAvatarLoadRequest request,
        out ulong outHandle,
        out NcAvatarInfo outInfo);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_unload_avatar(ulong handle);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_get_avatar_info(ulong handle, out NcAvatarInfo outInfo);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_get_expression_count(ulong handle, out uint outCount);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_get_expression_infos(
        ulong handle,
        [Out] NcExpressionInfo[] outInfos,
        uint capacity,
        out uint outWritten);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_get_springbone_info(ulong handle, out NcSpringBoneInfo outInfo);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_get_avatar_runtime_metrics_v2(ulong handle, out NcAvatarRuntimeMetricsV2 outInfo);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_set_tracking_frame(ref NcTrackingFrame frame);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_set_expression_weights([In] NcExpressionWeight[] weights, uint count);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_create_render_resources(ulong handle);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_destroy_render_resources(ulong handle);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_create_window_render_target(ref NcWindowRenderTarget target);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_resize_window_render_target(ref NcWindowRenderTarget target);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_destroy_window_render_target(IntPtr hwnd);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_render_frame_to_window(IntPtr hwnd, float deltaTimeSeconds);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern NcResultCode nc_render_avatar_thumbnail_png(ref NcThumbnailRequest request);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_set_render_quality_options(ref NcRenderQualityOptions options);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_get_render_quality_options(out NcRenderQualityOptions outOptions);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_set_lighting_options(ref NcLightingOptions options);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_get_lighting_options(out NcLightingOptions outOptions);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_set_pose_offsets([In] NcPoseBoneOffset[] offsets, uint count);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_clear_pose_offsets();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_start_spout(ref NcSpoutOptions options);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_stop_spout();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_start_spout_receiver(ref NcSpoutReceiverOptions options);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_stop_spout_receiver();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_start_osc(ref NcOscOptions options);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_stop_osc();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_get_last_error(out NcErrorInfo outError);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_get_runtime_stats(out NcRuntimeStats outStats);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_get_spout_diagnostics(out NcSpoutDiagnostics outDiag);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern NcResultCode nc_get_spout_receiver_diagnostics(out NcSpoutReceiverDiagnostics outDiag);

    public static string FormatLastError()
    {
        var rc = nc_get_last_error(out var err);
        if (rc != NcResultCode.Ok)
        {
            return $"last_error unavailable: {rc}";
        }

        var recoverable = err.Recoverable == 0 ? "fatal" : "recoverable";
        return $"{err.Code} [{err.Subsystem}] {err.Message} ({recoverable})";
    }

    public static NcRenderQualityOptions BuildBroadcastPreset()
    {
        return new NcRenderQualityOptions
        {
            CameraMode = NcCameraMode.AutoFitBust,
            FramingTarget = 0.80f,
            Headroom = 0.10f,
            YawDeg = 0.0f,
            FovDeg = 40.0f,
            BackgroundR = 0.55f,
            BackgroundG = 0.55f,
            BackgroundB = 0.55f,
            BackgroundA = 1.0f,
            QualityProfile = (uint)NcRenderQualityProfile.Balanced,
            ShowDebugOverlay = 0U,
        };
    }

    public static NcRenderQualityOptions BuildDebugPreset()
    {
        var options = BuildBroadcastPreset();
        options.ShowDebugOverlay = 1U;
        return options;
    }

    public static NcRenderQualityOptions BuildUltraParityPreset()
    {
        var options = BuildBroadcastPreset();
        options.QualityProfile = (uint)NcRenderQualityProfile.UltraParity;
        options.FovDeg = 42.0f;
        options.Headroom = 0.10f;
        options.ShowDebugOverlay = 0U;
        return options;
    }

    public static NcRenderQualityOptions BuildFastFallbackPreset()
    {
        var options = BuildBroadcastPreset();
        options.QualityProfile = (uint)NcRenderQualityProfile.FastFallback;
        options.FovDeg = 45.0f;
        options.Headroom = 0.12f;
        options.ShowDebugOverlay = 0U;
        return options;
    }

    public static NcLightingOptions BuildVsfRealtimeShadowPreset()
    {
        return new NcLightingOptions
        {
            LightPositionX = -0.72f,
            LightPositionY = 19.35f,
            LightPositionZ = 3.7f,
            LightEulerPitchDeg = 19.201f,
            LightEulerYawDeg = 175.0f,
            LightEulerRollDeg = 2.582f,
            Intensity = 12.5f,
            Range = 16.4f,
            SpotAngleDeg = 16.6f,
            InnerSpotAngleDeg = 0.0f,
            ShadowStrength = 1.0f,
            ShadowBias = 0.1f,
            ShadowNormalBias = 0.0f,
            ShadowNearPlane = 8.5f,
            ShadowResolution = 8192U,
            AmbientIntensity = 1.0f,
            EnableSunLight = 0U,
            EnableShadow = 1U,
        };
    }

    public static string FormatSpoutBackend(uint backendKind)
    {
        return backendKind switch
        {
            (uint)NcSpoutBackendKind.Spout2Gpu => "spout2-gpu",
            (uint)NcSpoutBackendKind.LegacySharedMemory => "legacy-shared-memory",
            _ => "inactive",
        };
    }

    public static string GetLoadedNativeCorePath()
    {
        var module = GetModuleHandleW(DllName);
        if (module == IntPtr.Zero)
        {
            return string.Empty;
        }

        var sb = new StringBuilder(MaxModulePathChars);
        var len = GetModuleFileNameW(module, sb, sb.Capacity);
        if (len == 0U)
        {
            return string.Empty;
        }

        return sb.ToString();
    }

    public static string GetLoadedNativeCoreTimestampUtc()
    {
        var path = GetLoadedNativeCorePath();
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
        {
            return string.Empty;
        }

        try
        {
            return File.GetLastWriteTimeUtc(path).ToString("O");
        }
        catch
        {
            return string.Empty;
        }
    }
}
