using System;
using System.Linq;
using System.Windows;

namespace WpfHost;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        if (e.Args.Length > 0 && string.Equals(e.Args[0], "--thumbnail-worker", StringComparison.Ordinal))
        {
            var exitCode = ThumbnailWorker.Run(e.Args.Skip(1).ToArray());
            Shutdown(exitCode);
            return;
        }

        base.OnStartup(e);
        var window = new MainWindow();
        window.Show();
    }
}
