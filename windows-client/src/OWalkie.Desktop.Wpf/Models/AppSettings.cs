namespace OWalkie.Desktop.Wpf.Models;

public sealed class AppSettings
{
    public ConnectionProfile ActiveProfile { get; set; } = new();
    public List<ConnectionProfile> Profiles { get; set; } = new()
    {
        new ConnectionProfile(),
    };
    public string MicrophoneBackendId { get; set; } = "default";
    public int HardwarePttKeyCode { get; set; }
    public string RogerPresetId { get; set; } = "roger_variant_1";
    public string CallingPresetId { get; set; } = "calling_variant_1";
    public bool RepeaterEnabled { get; set; }
}
