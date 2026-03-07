using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace WpfHost;

public partial class CommandPalette : Window
{
    private sealed record PaletteCommand(
        string Title,
        string Shortcut,
        Action Execute,
        bool IsEnabled = true);

    private readonly MainWindow _host;
    private readonly List<PaletteCommand> _allCommands;

    public CommandPalette(MainWindow host)
    {
        InitializeComponent();
        _host = host;
        _allCommands = BuildCommands();
        PopulateList(string.Empty);
        Loaded += (_, _) => SearchTextBox.Focus();
    }

    private List<PaletteCommand> BuildCommands()
    {
        var ctrl = _host.Controller;
        var session = ctrl.SessionState;
        var outputs = ctrl.Outputs;
        var operation = ctrl.OperationState;

        return new List<PaletteCommand>
        {
            new("세션 시작", "Ctrl+2 → 세션",
                () => { Close(); _host.InvokeInitialize(); },
                !session.IsInitialized && !operation.IsBusy),
            new("아바타 파일 찾아보기", "Ctrl+O",
                () => { Close(); _host.InvokeBrowseAvatar(); },
                !operation.IsBusy),
            new("아바타 불러오기", "F5",
                () => { Close(); _host.InvokeLoadAvatar(); },
                session.IsInitialized && !operation.IsBusy),
            new("Spout 화면 출력 시작", "Ctrl+Shift+S",
                () => { Close(); _host.InvokeStartSpout(); },
                session.IsInitialized && !outputs.SpoutActive && !operation.IsBusy),
            new("Spout 화면 출력 중지", "Ctrl+Shift+S",
                () => { Close(); _host.InvokeStopSpout(); },
                session.IsInitialized && outputs.SpoutActive && !operation.IsBusy),
            new("OSC 모션 출력 시작", "Ctrl+Shift+O",
                () => { Close(); _host.InvokeStartOsc(); },
                session.IsInitialized && !outputs.OscActive && !operation.IsBusy),
            new("OSC 모션 출력 중지", "Ctrl+Shift+O",
                () => { Close(); _host.InvokeStopOsc(); },
                session.IsInitialized && outputs.OscActive && !operation.IsBusy),
            new("모델 창 분리 (PopOut)", "Ctrl+Shift+P",
                () => { Close(); _host.InvokePopOut(); }),
            new("진단 패널 토글", "Ctrl+D",
                () => { Close(); _host.InvokeDiagnosticsToggle(); }),
            new("세션/아바타 섹션으로 이동", "Ctrl+2",
                () => { Close(); _host.InvokeNavSection(2); }),
            new("렌더 설정 섹션으로 이동", "Ctrl+3",
                () => { Close(); _host.InvokeNavSection(3); }),
            new("출력 설정 섹션으로 이동", "Ctrl+4",
                () => { Close(); _host.InvokeNavSection(4); }),
            new("트래킹 섹션으로 이동", "Ctrl+5",
                () => { Close(); _host.InvokeNavSection(5); }),
            new("테마 전환 (라이트/다크)", "Ctrl+T",
                () => { Close(); _host.InvokeThemeToggle(); }),
            new("세션 종료", string.Empty,
                () => { Close(); _host.InvokeShutdown(); },
                session.IsInitialized && !operation.IsBusy),
        };
    }

    private void PopulateList(string filter)
    {
        CommandListBox.Items.Clear();
        var lower = filter.ToLowerInvariant().Trim();
        foreach (var cmd in _allCommands)
        {
            if (!string.IsNullOrEmpty(lower) &&
                !cmd.Title.Contains(lower, StringComparison.OrdinalIgnoreCase) &&
                !cmd.Shortcut.Contains(lower, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            var panel = new DockPanel();
            var shortcutText = new TextBlock
            {
                Text = cmd.Shortcut,
                FontSize = 11,
                Foreground = System.Windows.Media.Brushes.Gray,
                VerticalAlignment = VerticalAlignment.Center,
                Margin = new Thickness(4, 0, 0, 0),
            };
            DockPanel.SetDock(shortcutText, Dock.Right);
            panel.Children.Add(shortcutText);

            var titleText = new TextBlock
            {
                Text = cmd.Title,
                FontSize = 13,
                Foreground = cmd.IsEnabled
                    ? System.Windows.Media.Brushes.Black
                    : System.Windows.Media.Brushes.Gray,
                VerticalAlignment = VerticalAlignment.Center,
            };
            panel.Children.Add(titleText);

            var item = new ListBoxItem
            {
                Content = panel,
                Tag = cmd,
                IsEnabled = cmd.IsEnabled,
            };
            CommandListBox.Items.Add(item);
        }

        if (CommandListBox.Items.Count > 0)
        {
            CommandListBox.SelectedIndex = 0;
        }
    }

    private void ExecuteSelected()
    {
        if (CommandListBox.SelectedItem is ListBoxItem item && item.Tag is PaletteCommand cmd && cmd.IsEnabled)
        {
            cmd.Execute();
        }
    }

    private void SearchTextBox_TextChanged(object sender, TextChangedEventArgs e)
    {
        PopulateList(SearchTextBox.Text);
    }

    private void SearchTextBox_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Down)
        {
            if (CommandListBox.Items.Count > 0)
            {
                var next = Math.Min(CommandListBox.SelectedIndex + 1, CommandListBox.Items.Count - 1);
                CommandListBox.SelectedIndex = next;
                CommandListBox.ScrollIntoView(CommandListBox.SelectedItem);
            }
            e.Handled = true;
        }
        else if (e.Key == Key.Up)
        {
            if (CommandListBox.Items.Count > 0)
            {
                var prev = Math.Max(CommandListBox.SelectedIndex - 1, 0);
                CommandListBox.SelectedIndex = prev;
                CommandListBox.ScrollIntoView(CommandListBox.SelectedItem);
            }
            e.Handled = true;
        }
        else if (e.Key == Key.Enter)
        {
            ExecuteSelected();
            e.Handled = true;
        }
        else if (e.Key == Key.Escape)
        {
            Close();
            e.Handled = true;
        }
    }

    private void CommandListBox_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter)
        {
            ExecuteSelected();
            e.Handled = true;
        }
        else if (e.Key == Key.Escape)
        {
            Close();
            e.Handled = true;
        }
    }

    private void CommandListBox_MouseDoubleClick(object sender, MouseButtonEventArgs e)
    {
        ExecuteSelected();
    }

    private void Window_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Escape)
        {
            Close();
            e.Handled = true;
        }
    }

    private void Window_Deactivated(object sender, EventArgs e)
    {
        Close();
    }
}
