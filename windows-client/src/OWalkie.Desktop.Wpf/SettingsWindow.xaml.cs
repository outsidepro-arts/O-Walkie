using System.Windows;
using System.Windows.Input;
using OWalkie.Desktop.Wpf.Models;
using OWalkie.Desktop.Wpf.Services;

namespace OWalkie.Desktop.Wpf;

public partial class SettingsWindow : Window
{
    private readonly SettingsService _settingsService;
    private readonly AppSettings _settings;
    private bool _awaitingHardwareKey;

    private readonly List<OptionItem> _microphoneOptions =
    [
        new("default", "System default"),
        new("mic", "Phone mic (MIC)"),
        new("camcorder", "Camcorder"),
        new("voice_recognition", "Voice recognition"),
        new("voice_communication", "Voice communication"),
        new("unprocessed", "Unprocessed"),
        new("voice_performance", "Voice performance"),
        new("bluetooth_headset", "Bluetooth headset mic"),
    ];

    private readonly List<OptionItem> _rogerOptions =
    [
        new("roger_variant_1", "Variant 1"),
        new("roger_variant_2", "Variant 2"),
        new("roger_variant_3", "Variant 3"),
        new("roger_custom", "Custom"),
    ];

    private readonly List<OptionItem> _callingOptions =
    [
        new("calling_variant_1", "Variant 1"),
        new("calling_variant_2", "Variant 2"),
        new("calling_variant_3", "Variant 3"),
        new("calling_custom", "Custom"),
    ];

    public SettingsWindow(SettingsService settingsService)
    {
        InitializeComponent();
        _settingsService = settingsService;
        _settings = _settingsService.Load();
        BindControls();
        RefreshHardwarePttStatus();
    }

    private void BindControls()
    {
        MicrophoneComboBox.ItemsSource = _microphoneOptions;
        RogerComboBox.ItemsSource = _rogerOptions;
        CallingComboBox.ItemsSource = _callingOptions;

        RepeaterCheckBox.IsChecked = _settings.RepeaterEnabled;
        SelectById(MicrophoneComboBox, _microphoneOptions, _settings.MicrophoneBackendId);
        SelectById(RogerComboBox, _rogerOptions, _settings.RogerPresetId);
        SelectById(CallingComboBox, _callingOptions, _settings.CallingPresetId);
    }

    private static void SelectById(System.Windows.Controls.ComboBox comboBox, List<OptionItem> options, string id)
    {
        comboBox.SelectedItem = options.FirstOrDefault(o => o.Id == id) ?? options.FirstOrDefault();
    }

    private void AssignButton_OnClick(object sender, RoutedEventArgs e)
    {
        _awaitingHardwareKey = true;
        HardwareKeyHintText.Text = "Press any key now to assign PTT.";
    }

    private void ClearButton_OnClick(object sender, RoutedEventArgs e)
    {
        _settings.HardwarePttKeyCode = 0;
        _awaitingHardwareKey = false;
        RefreshHardwarePttStatus();
        HardwareKeyHintText.Text = "Hardware key assignment cleared.";
    }

    private void SaveButton_OnClick(object sender, RoutedEventArgs e)
    {
        _settings.RepeaterEnabled = RepeaterCheckBox.IsChecked == true;
        _settings.MicrophoneBackendId = (MicrophoneComboBox.SelectedItem as OptionItem)?.Id ?? "default";
        _settings.RogerPresetId = (RogerComboBox.SelectedItem as OptionItem)?.Id ?? "roger_variant_1";
        _settings.CallingPresetId = (CallingComboBox.SelectedItem as OptionItem)?.Id ?? "calling_variant_1";

        _settingsService.Save(_settings);
        DialogResult = true;
        Close();
    }

    private void CancelButton_OnClick(object sender, RoutedEventArgs e)
    {
        DialogResult = false;
        Close();
    }

    private void Window_OnPreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (!_awaitingHardwareKey)
        {
            return;
        }

        if (e.Key == Key.Escape)
        {
            _awaitingHardwareKey = false;
            HardwareKeyHintText.Text = "Assignment cancelled.";
            e.Handled = true;
            return;
        }

        _settings.HardwarePttKeyCode = KeyInterop.VirtualKeyFromKey(e.Key);
        _awaitingHardwareKey = false;
        RefreshHardwarePttStatus();
        HardwareKeyHintText.Text = "Hardware key captured. Click Save to apply.";
        e.Handled = true;
    }

    private void RefreshHardwarePttStatus()
    {
        HardwareKeyStatusText.Text = _settings.HardwarePttKeyCode > 0
            ? $"Assigned: {KeyInterop.KeyFromVirtualKey(_settings.HardwarePttKeyCode)}"
            : "Not assigned";
    }

    private sealed class OptionItem(string id, string title)
    {
        public string Id { get; } = id;
        public string Title { get; } = title;
        public override string ToString() => Title;
    }
}
