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
                "작업 중",
                $"{operation.CurrentOperation} 작업이 진행 중입니다. 완료될 때까지 기다려 주세요.");
        }

        if (!session.IsInitialized)
        {
            return new HostNextActionHint(
                HostNextRecommendedAction.InitializeSession,
                "1단계",
                "세션 시작 버튼을 눌러 주세요.");
        }

        if (!validation.AvatarPathValid)
        {
            return new HostNextActionHint(
                HostNextRecommendedAction.SelectAvatarFile,
                "2단계",
                "올바른 아바타 파일 경로를 선택해 주세요.");
        }

        if (!session.ActiveAvatarHandle.HasValue)
        {
            return new HostNextActionHint(
                HostNextRecommendedAction.LoadAvatar,
                "2단계",
                "선택한 아바타를 불러와 주세요.");
        }

        if (!outputs.SpoutActive && !outputs.OscActive)
        {
            return new HostNextActionHint(
                HostNextRecommendedAction.StartOutputs,
                "3단계",
                "출력을 시작해 방송 준비를 완료해 주세요.");
        }

        return new HostNextActionHint(
            HostNextRecommendedAction.Ready,
            "준비 완료",
            "방송 출력이 정상 실행 중입니다.");
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
                "잠시만 기다려 주세요",
                $"{operation.CurrentOperation} 작업을 진행 중입니다.",
                "현재 작업이 끝나야 다음 단계로 넘어갈 수 있어요.",
                "작업 완료 후 다시 시도",
                "현재 작업을 진행 중입니다.",
                "작업 진행 중",
                HostActionability.Blocked);
        }

        if (!session.IsInitialized)
        {
            return new HostOnboardingState(
                HostOnboardingStep.Initialize,
                HostPrimaryActionKind.InitializeSession,
                "1단계. 세션 시작",
                "먼저 세션 시작 버튼을 눌러 준비를 완료해 주세요.",
                string.Empty,
                string.Empty,
                "세션을 시작하면 아바타 불러오기가 열립니다.",
                string.Empty,
                HostActionability.Immediate);
        }

        if (!validation.AvatarPathValid)
        {
            return new HostOnboardingState(
                HostOnboardingStep.LoadAvatar,
                HostPrimaryActionKind.None,
                "2단계. 아바타 불러오기",
                "올바른 아바타 파일을 선택해 주세요.",
                validation.AvatarPathError,
                "정상 파일 경로 선택",
                "경로를 수정한 뒤 다시 불러오세요.",
                "아바타 경로를 확인해 주세요.",
                HostActionability.Blocked);
        }

        if (!session.ActiveAvatarHandle.HasValue)
        {
            return new HostOnboardingState(
                HostOnboardingStep.LoadAvatar,
                HostPrimaryActionKind.LoadAvatar,
                "2단계. 아바타 불러오기",
                "선택한 파일을 불러와 주세요.",
                string.Empty,
                string.Empty,
                "아바타를 불러오면 마지막 단계로 이동합니다.",
                string.Empty,
                HostActionability.Immediate);
        }

        if (!outputs.SpoutActive && !outputs.OscActive)
        {
            return new HostOnboardingState(
                HostOnboardingStep.StartOutput,
                HostPrimaryActionKind.StartOutput,
                "3단계. 출력 시작",
                "출력 시작을 누르면 방송 출력 준비가 완료됩니다.",
                string.Empty,
                string.Empty,
                "출력을 시작하면 준비가 완료됩니다.",
                string.Empty,
                HostActionability.Immediate);
        }

        return new HostOnboardingState(
            HostOnboardingStep.Ready,
            HostPrimaryActionKind.None,
            "준비 완료",
            "지금 바로 사용할 수 있어요.",
            string.Empty,
            string.Empty,
            "초기 설정이 모두 완료되었습니다.",
            string.Empty,
            HostActionability.Immediate);
    }
}
