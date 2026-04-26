namespace OWalkie.Desktop.Wpf.Models;

public sealed class ConnectionProfile
{
    public string Name { get; set; } = string.Empty;
    public string Host { get; set; } = string.Empty;
    public int WsPort { get; set; } = 5500;
    public int UdpPort { get; set; } = 5505;
    public string Channel { get; set; } = "global";

    public ConnectionProfile Clone()
    {
        return new ConnectionProfile
        {
            Name = Name,
            Host = Host,
            WsPort = WsPort,
            UdpPort = UdpPort,
            Channel = Channel,
        };
    }

    public override string ToString() => Name;
}
