using System.Windows;
using System.Windows.Input;
using OWalkie.Desktop.Wpf.Services;
using OWalkie.Desktop.Wpf.ViewModels;

namespace OWalkie.Desktop.Wpf;

public partial class MainWindow : Window
{
    private readonly SettingsService _settingsService;
    private readonly MainViewModel _viewModel;

    public MainWindow()
    {
        InitializeComponent();
        _settingsService = new SettingsService();
        _viewModel = new MainViewModel(
            _settingsService,
            new RelayClientService(),
            new AudioEngineService());
        DataContext = _viewModel;
    }

    private void PttButton_OnMouseDown(object sender, MouseButtonEventArgs e)
    {
        if (_viewModel.StartPttCommand.CanExecute(null))
        {
            _viewModel.StartPttCommand.Execute(null);
        }
    }

    private void PttButton_OnMouseUp(object sender, MouseButtonEventArgs e)
    {
        if (_viewModel.StopPttCommand.CanExecute(null))
        {
            _viewModel.StopPttCommand.Execute(null);
        }
    }

    private void PttButton_OnLostMouseCapture(object sender, MouseEventArgs e)
    {
        if (_viewModel.StopPttCommand.CanExecute(null))
        {
            _viewModel.StopPttCommand.Execute(null);
        }
    }

    private void Window_OnPreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (_viewModel.HandlePreviewKeyDown(e.Key, e.IsRepeat))
        {
            e.Handled = true;
        }
    }

    private void Window_OnPreviewKeyUp(object sender, KeyEventArgs e)
    {
        if (_viewModel.HandlePreviewKeyUp(e.Key))
        {
            e.Handled = true;
        }
    }

    private void SettingsButton_OnClick(object sender, RoutedEventArgs e)
    {
        var dialog = new SettingsWindow(_settingsService)
        {
            Owner = this,
        };
        if (dialog.ShowDialog() == true)
        {
            _viewModel.ReloadSettingsFromDisk();
        }
    }
}