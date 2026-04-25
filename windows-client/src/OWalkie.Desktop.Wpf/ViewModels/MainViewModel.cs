using System.ComponentModel;
using System.Collections.ObjectModel;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Input;
using OWalkie.Desktop.Wpf.Helpers;
using OWalkie.Desktop.Wpf.Models;
using OWalkie.Desktop.Wpf.Services;

namespace OWalkie.Desktop.Wpf.ViewModels;

public sealed class MainViewModel : INotifyPropertyChanged
{
    private readonly SettingsService _settingsService;
    private readonly RelayClientService _relayClientService;
    private readonly AudioEngineService _audioEngineService;
    private AppSettings _settings;
    private readonly ObservableCollection<ConnectionProfile> _profiles = new();

    private bool _isConnecting;
    private bool _isAwaitingHardwarePtt;
    private string _statusText = "Status: Idle";
    private int _signalPercent;
    private string _profileName = string.Empty;
    private string _host = string.Empty;
    private int _wsPort;
    private int _udpPort;
    private string _channel = string.Empty;
    private string _hardwarePttStatusText = "Hardware PTT: not assigned";
    private ConnectionProfile? _selectedProfile;

    public MainViewModel(SettingsService settingsService, RelayClientService relayClientService, AudioEngineService audioEngineService)
    {
        _settingsService = settingsService;
        _relayClientService = relayClientService;
        _audioEngineService = audioEngineService;
        _settings = _settingsService.Load();

        ConnectCommand = new RelayCommand(ToggleConnection, () => !_isConnecting);
        StartPttCommand = new RelayCommand(StartPtt, () => IsConnected && !_audioEngineService.IsTransmitting);
        StopPttCommand = new RelayCommand(StopPtt, () => _audioEngineService.IsTransmitting);
        SaveProfileCommand = new RelayCommand(SaveProfile);
        DeleteProfileCommand = new RelayCommand(DeleteSelectedProfile, () => Profiles.Count > 1 && SelectedProfile != null);
        AssignHardwarePttCommand = new RelayCommand(BeginHardwarePttAssignment);
        ClearHardwarePttCommand = new RelayCommand(ClearHardwarePttAssignment);

        _audioEngineService.Initialize(_relayClientService);
        LoadFromSettings();
        _relayClientService.ConnectionStateChanged += OnConnectionStateChanged;
        _relayClientService.StatusMessage += OnStatusMessage;
        _relayClientService.PacketMsChanged += OnPacketMsChanged;
        _audioEngineService.OutputLevelChanged += OnOutputLevelChanged;
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public RelayCommand ConnectCommand { get; }
    public RelayCommand StartPttCommand { get; }
    public RelayCommand StopPttCommand { get; }
    public RelayCommand SaveProfileCommand { get; }
    public RelayCommand DeleteProfileCommand { get; }
    public RelayCommand AssignHardwarePttCommand { get; }
    public RelayCommand ClearHardwarePttCommand { get; }

    public ObservableCollection<ConnectionProfile> Profiles => _profiles;

    public ConnectionProfile? SelectedProfile
    {
        get => _selectedProfile;
        set
        {
            if (SetField(ref _selectedProfile, value))
            {
                if (value != null)
                {
                    ProfileName = value.Name;
                    Host = value.Host;
                    WsPort = value.WsPort;
                    UdpPort = value.UdpPort;
                    Channel = value.Channel;
                }
                DeleteProfileCommand.RaiseCanExecuteChanged();
            }
        }
    }

    public string ProfileName
    {
        get => _profileName;
        set => SetField(ref _profileName, value);
    }

    public string Host
    {
        get => _host;
        set => SetField(ref _host, value);
    }

    public int WsPort
    {
        get => _wsPort;
        set => SetField(ref _wsPort, value);
    }

    public int UdpPort
    {
        get => _udpPort;
        set => SetField(ref _udpPort, value);
    }

    public string Channel
    {
        get => _channel;
        set => SetField(ref _channel, value);
    }

    public string HardwarePttStatusText
    {
        get => _hardwarePttStatusText;
        private set => SetField(ref _hardwarePttStatusText, value);
    }

    public bool IsAwaitingHardwarePtt
    {
        get => _isAwaitingHardwarePtt;
        private set => SetField(ref _isAwaitingHardwarePtt, value);
    }

    public bool IsConnected => _relayClientService.IsConnected;

    public string ConnectButtonText => IsConnected ? "Disconnect" : (_isConnecting ? "Connecting..." : "Connect");

    public string StatusText
    {
        get => _statusText;
        private set => SetField(ref _statusText, value);
    }

    public int SignalPercent
    {
        get => _signalPercent;
        set => SetField(ref _signalPercent, value);
    }

    private void LoadFromSettings()
    {
        _settings = _settingsService.Load();
        Profiles.Clear();
        foreach (var profile in _settings.Profiles)
        {
            Profiles.Add(profile.Clone());
        }

        if (Profiles.Count == 0)
        {
            Profiles.Add(new ConnectionProfile());
        }

        var selected = Profiles.FirstOrDefault(p =>
            p.Name.Equals(_settings.ActiveProfile.Name, StringComparison.OrdinalIgnoreCase)) ?? Profiles[0];
        SelectedProfile = selected;
        UpdateHardwarePttStatus();
    }

    public void ReloadSettingsFromDisk()
    {
        LoadFromSettings();
        StatusText = "Settings reloaded";
    }

    private async void ToggleConnection()
    {
        _isConnecting = true;
        NotifyConnectionState();
        try
        {
            if (_relayClientService.IsConnected)
            {
                await _audioEngineService.StopTransmitAsync();
                await _relayClientService.DisconnectAsync();
                StatusText = "Status: Disconnected";
                return;
            }

            SaveCurrentConnectionProfile();
            await _relayClientService.ConnectAsync(_settings.ActiveProfile, _settings.RepeaterEnabled);
            StatusText = "Status: Connected";
        }
        catch (Exception ex)
        {
            StatusText = $"Connect failed: {ex.Message}";
        }
        finally
        {
            _isConnecting = false;
            NotifyConnectionState();
        }
    }

    private async void StartPtt()
    {
        await _audioEngineService.StartTransmitAsync();
        StatusText = "Status: Transmitting";
        StartPttCommand.RaiseCanExecuteChanged();
        StopPttCommand.RaiseCanExecuteChanged();
    }

    private async void StopPtt()
    {
        await _audioEngineService.StopTransmitAsync();
        StatusText = IsConnected ? "Status: Connected" : "Status: Idle";
        StartPttCommand.RaiseCanExecuteChanged();
        StopPttCommand.RaiseCanExecuteChanged();
    }

    private void SaveCurrentConnectionProfile()
    {
        var updated = new ConnectionProfile
        {
            Name = string.IsNullOrWhiteSpace(ProfileName) ? "Default local relay" : ProfileName.Trim(),
            Host = Host,
            WsPort = WsPort,
            UdpPort = UdpPort,
            Channel = Channel,
        };
        _settings.ActiveProfile = updated.Clone();
        _settings.Profiles = Profiles.Select(p => p.Clone()).ToList();
        _settingsService.Save(_settings);
    }

    private void OnConnectionStateChanged(object? sender, bool connected)
    {
        StatusText = connected ? "Status: Connected" : "Status: Disconnected";
        NotifyConnectionState();
    }

    private void OnStatusMessage(object? sender, string status)
    {
        RunOnUi(() => StatusText = status);
    }

    private void OnPacketMsChanged(object? sender, int packetMs)
    {
        RunOnUi(() => StatusText = $"Status: Connected (packet {packetMs} ms)");
    }

    private void OnOutputLevelChanged(object? sender, int levelPercent)
    {
        RunOnUi(() => SignalPercent = levelPercent);
    }

    public bool HandlePreviewKeyDown(Key key, bool isRepeat)
    {
        if (IsAwaitingHardwarePtt)
        {
            var vkey = KeyInterop.VirtualKeyFromKey(key);
            if (vkey > 0)
            {
                _settings.HardwarePttKeyCode = vkey;
                _settingsService.Save(_settings);
                UpdateHardwarePttStatus();
                IsAwaitingHardwarePtt = false;
                StatusText = $"Hardware PTT assigned: {KeyToDisplayName(key)}";
            }
            return true;
        }

        if (IsAssignedHardwarePttKey(key))
        {
            if (!isRepeat && StartPttCommand.CanExecute(null))
            {
                StartPttCommand.Execute(null);
            }
            return true;
        }

        return false;
    }

    public bool HandlePreviewKeyUp(Key key)
    {
        if (IsAssignedHardwarePttKey(key))
        {
            if (StopPttCommand.CanExecute(null))
            {
                StopPttCommand.Execute(null);
            }
            return true;
        }
        return false;
    }

    private bool IsAssignedHardwarePttKey(Key key)
    {
        return _settings.HardwarePttKeyCode > 0 &&
            KeyInterop.VirtualKeyFromKey(key) == _settings.HardwarePttKeyCode;
    }

    private void BeginHardwarePttAssignment()
    {
        IsAwaitingHardwarePtt = true;
        StatusText = "Press a key to assign hardware PTT";
    }

    private void ClearHardwarePttAssignment()
    {
        _settings.HardwarePttKeyCode = 0;
        _settingsService.Save(_settings);
        UpdateHardwarePttStatus();
        IsAwaitingHardwarePtt = false;
        StatusText = "Hardware PTT assignment cleared";
    }

    private void SaveProfile()
    {
        var normalizedName = string.IsNullOrWhiteSpace(ProfileName) ? "Default local relay" : ProfileName.Trim();
        var existing = Profiles.FirstOrDefault(p => p.Name.Equals(normalizedName, StringComparison.OrdinalIgnoreCase));
        if (existing == null)
        {
            existing = new ConnectionProfile();
            Profiles.Add(existing);
        }

        existing.Name = normalizedName;
        existing.Host = Host.Trim();
        existing.WsPort = WsPort is > 0 and <= 65535 ? WsPort : 5500;
        existing.UdpPort = UdpPort is > 0 and <= 65535 ? UdpPort : 5505;
        existing.Channel = string.IsNullOrWhiteSpace(Channel) ? "global" : Channel.Trim();
        SelectedProfile = existing;

        _settings.ActiveProfile = existing.Clone();
        _settings.Profiles = Profiles.Select(p => p.Clone()).ToList();
        _settingsService.Save(_settings);
        DeleteProfileCommand.RaiseCanExecuteChanged();
        StatusText = $"Profile '{existing.Name}' saved";
    }

    private void DeleteSelectedProfile()
    {
        if (SelectedProfile == null || Profiles.Count <= 1)
        {
            return;
        }

        var toDelete = SelectedProfile;
        var nextIndex = Math.Max(0, Profiles.IndexOf(toDelete) - 1);
        Profiles.Remove(toDelete);
        SelectedProfile = Profiles[nextIndex];

        if (SelectedProfile != null)
        {
            _settings.ActiveProfile = SelectedProfile.Clone();
        }
        _settings.Profiles = Profiles.Select(p => p.Clone()).ToList();
        _settingsService.Save(_settings);
        DeleteProfileCommand.RaiseCanExecuteChanged();
        StatusText = "Profile deleted";
    }

    private void UpdateHardwarePttStatus()
    {
        HardwarePttStatusText = _settings.HardwarePttKeyCode > 0
            ? $"Hardware PTT: {KeyToDisplayName(KeyInterop.KeyFromVirtualKey(_settings.HardwarePttKeyCode))}"
            : "Hardware PTT: not assigned";
    }

    private static string KeyToDisplayName(Key key)
    {
        var raw = key.ToString();
        if (string.IsNullOrWhiteSpace(raw))
        {
            return "Unknown";
        }

        return raw;
    }

    private static void RunOnUi(Action action)
    {
        if (Application.Current.Dispatcher.CheckAccess())
        {
            action();
            return;
        }

        Application.Current.Dispatcher.Invoke(action);
    }

    private void NotifyConnectionState()
    {
        OnPropertyChanged(nameof(IsConnected));
        OnPropertyChanged(nameof(ConnectButtonText));
        ConnectCommand.RaiseCanExecuteChanged();
        StartPttCommand.RaiseCanExecuteChanged();
        StopPttCommand.RaiseCanExecuteChanged();
        DeleteProfileCommand.RaiseCanExecuteChanged();
    }

    private bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        OnPropertyChanged(propertyName);
        return true;
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}
