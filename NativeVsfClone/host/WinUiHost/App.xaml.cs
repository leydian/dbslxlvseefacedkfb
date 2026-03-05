using System;
using System.Linq;
using HostCore;
using Microsoft.UI.Xaml;

namespace WinUiHost;

public partial class App : Application
{
    private Window? _window;

    public App()
    {
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        var commandArgs = Environment.GetCommandLineArgs();
        if (commandArgs.Length > 1 && string.Equals(commandArgs[1], "--thumbnail-worker", StringComparison.Ordinal))
        {
            var exitCode = AvatarThumbnailWorker.Run(commandArgs.Skip(2).ToArray());
            Environment.Exit(exitCode);
            return;
        }

        _window = new MainWindow();
        _window.Activate();
    }
}
