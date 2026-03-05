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

public enum HostNextRecommendedAction
{
    WaitBusy,
    InitializeSession,
    SelectAvatarFile,
    LoadAvatar,
    StartOutputs,
    Ready,
}

public sealed record HostNextActionHint(
    HostNextRecommendedAction Action,
    string Title,
    string Instruction);

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

    public static HostNextActionHint BuildNextActionHint(
        HostSessionState session,
        OutputState outputs,
        HostOperationState operation,
        HostValidationState validation)
    {
        if (operation.IsBusy)
        {
            return new HostNextActionHint(
                HostNextRecommendedAction.WaitBusy,
                "Working",
                $"{operation.CurrentOperation} in progress. Wait for completion.");
        }

        if (!session.IsInitialized)
        {
            return new HostNextActionHint(
                HostNextRecommendedAction.InitializeSession,
                "Step 1",
                "Click Initialize to start the session.");
        }

        if (!validation.AvatarPathValid)
        {
            return new HostNextActionHint(
                HostNextRecommendedAction.SelectAvatarFile,
                "Step 2",
                "Select a valid avatar file path.");
        }

        if (!session.ActiveAvatarHandle.HasValue)
        {
            return new HostNextActionHint(
                HostNextRecommendedAction.LoadAvatar,
                "Step 2",
                "Click Load to import the selected avatar.");
        }

        if (!outputs.SpoutActive && !outputs.OscActive)
        {
            return new HostNextActionHint(
                HostNextRecommendedAction.StartOutputs,
                "Step 3",
                "Start Spout or OSC output for broadcast.");
        }

        return new HostNextActionHint(
            HostNextRecommendedAction.Ready,
            "Ready",
            "Broadcast pipeline is running.");
    }

    public static HostOnboardingState BuildOnboardingState(
        HostSessionState session,
        OutputState outputs,
        HostOperationState operation,
        HostValidationState validation)
    {
        if (operation.IsBusy)
        {
            return new HostOnboardingState(
                HostOnboardingStep.Blocked,
                HostPrimaryActionKind.None,
                "Working",
                $"{operation.CurrentOperation} in progress.",
                "Current operation must finish before the next step.",
                "Wait for completion");
        }

        if (!session.IsInitialized)
        {
            return new HostOnboardingState(
                HostOnboardingStep.Initialize,
                HostPrimaryActionKind.InitializeSession,
                "Step 1. Start Session",
                "Click Start Session to prepare camera and output runtime.",
                string.Empty,
                string.Empty);
        }

        if (!validation.AvatarPathValid)
        {
            return new HostOnboardingState(
                HostOnboardingStep.LoadAvatar,
                HostPrimaryActionKind.None,
                "Step 2. Load Avatar",
                "Pick a valid avatar file, then continue.",
                validation.AvatarPathError,
                "Select a valid avatar file");
        }

        if (!session.ActiveAvatarHandle.HasValue)
        {
            return new HostOnboardingState(
                HostOnboardingStep.LoadAvatar,
                HostPrimaryActionKind.LoadAvatar,
                "Step 2. Load Avatar",
                "Click Load Avatar to import the selected file.",
                string.Empty,
                string.Empty);
        }

        if (!outputs.SpoutActive && !outputs.OscActive)
        {
            return new HostOnboardingState(
                HostOnboardingStep.StartOutput,
                HostPrimaryActionKind.StartOutput,
                "Step 3. Start Broadcast",
                "Click Start Output. The app tries Spout first, then OSC if needed.",
                string.Empty,
                string.Empty);
        }

        return new HostOnboardingState(
            HostOnboardingStep.Ready,
            HostPrimaryActionKind.None,
            "Ready",
            "Broadcast output is running.",
            string.Empty,
            string.Empty);
    }
}
