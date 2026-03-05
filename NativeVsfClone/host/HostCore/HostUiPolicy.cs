namespace HostCore;

public sealed record HostUiAvailability(
    bool InitializeEnabled,
    bool ShutdownEnabled,
    bool BrowseAvatarEnabled,
    bool LoadEnabled,
    bool UnloadEnabled,
    bool StartSpoutEnabled,
    bool StopSpoutEnabled,
    bool StartOscEnabled,
    bool StopOscEnabled,
    bool RenderControlsEnabled,
    bool ManualCameraMode);

public sealed record HostUiStatusText(
    string SessionText,
    string AvatarText,
    string RenderText,
    string OutputText,
    string BusyText)
{
    public string QuickStatusText => $"Session={SessionText} | Avatar={AvatarText} | Outputs={OutputText}";
}

public static class HostUiPolicy
{
    public static HostUiAvailability EvaluateAvailability(
        HostSessionState session,
        OutputState outputs,
        HostOperationState operation,
        HostValidationState validation,
        RenderUiState renderState,
        bool isManualCameraSelected)
    {
        var hasAvatar = session.ActiveAvatarHandle.HasValue;
        var isBusy = operation.IsBusy;
        var renderControlsEnabled = session.IsInitialized && !isBusy;
        var manualCameraMode = isManualCameraSelected || renderState.CameraMode == RenderCameraMode.Manual;

        return new HostUiAvailability(
            InitializeEnabled: !session.IsInitialized && !isBusy,
            ShutdownEnabled: session.IsInitialized && !isBusy,
            BrowseAvatarEnabled: session.IsInitialized && !isBusy,
            LoadEnabled: session.IsInitialized && !isBusy && validation.AvatarPathValid,
            UnloadEnabled: session.IsInitialized && hasAvatar && !isBusy,
            StartSpoutEnabled: session.IsInitialized && hasAvatar && !outputs.SpoutActive && !isBusy,
            StopSpoutEnabled: outputs.SpoutActive && !isBusy,
            StartOscEnabled: session.IsInitialized && hasAvatar && !outputs.OscActive && !isBusy && validation.OscBindPortValid && validation.OscPublishAddressValid,
            StopOscEnabled: outputs.OscActive && !isBusy,
            RenderControlsEnabled: renderControlsEnabled,
            ManualCameraMode: manualCameraMode);
    }

    public static HostUiStatusText BuildStatusText(
        HostSessionState session,
        OutputState outputs,
        HostOperationState operation)
    {
        var sessionText = session.IsInitialized ? "Initialized" : "Stopped";
        var avatarText = session.ActiveAvatarHandle.HasValue ? "Loaded" : "None";
        var outputText = $"Spout={(outputs.SpoutActive ? "On" : "Off")} OSC={(outputs.OscActive ? "On" : "Off")}";
        var busyText = operation.IsBusy ? operation.CurrentOperation : "Idle";
        var renderText = $"{session.LastRenderRc} {session.RenderWidthPx}x{session.RenderHeightPx}";

        return new HostUiStatusText(sessionText, avatarText, renderText, outputText, busyText);
    }
}
