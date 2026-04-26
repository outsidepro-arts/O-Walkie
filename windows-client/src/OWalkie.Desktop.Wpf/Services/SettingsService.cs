using System.IO;
using System.Text.Json;
using OWalkie.Desktop.Wpf.Models;

namespace OWalkie.Desktop.Wpf.Services;

public sealed class SettingsService
{
    private static readonly JsonSerializerOptions SerializerOptions = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
    };

    private readonly string _settingsPath;

    public SettingsService()
    {
        var appDataRoot = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        var appDirectory = Path.Combine(appDataRoot, "OWalkie", "WindowsClient");
        Directory.CreateDirectory(appDirectory);
        _settingsPath = Path.Combine(appDirectory, "settings.json");
    }

    public AppSettings Load()
    {
        var settings = new AppSettings();
        if (!File.Exists(_settingsPath))
        {
            return settings;
        }

        try
        {
            var raw = File.ReadAllText(_settingsPath);
            settings = JsonSerializer.Deserialize<AppSettings>(raw, SerializerOptions) ?? new AppSettings();
        }
        catch
        {
            settings = new AppSettings();
        }

        var activeName = settings.ActiveProfile.Name;
        var fromList = settings.Profiles.FirstOrDefault(p => p.Name.Equals(activeName, StringComparison.OrdinalIgnoreCase));
        settings.ActiveProfile = (fromList ?? settings.ActiveProfile).Clone();
        return settings;
    }

    public void Save(AppSettings settings)
    {
        var raw = JsonSerializer.Serialize(settings, SerializerOptions);
        File.WriteAllText(_settingsPath, raw);
    }
}
