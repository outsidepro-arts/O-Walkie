namespace OWalkie.Desktop.Wpf.Models;

public sealed class ConnectionProfile
{
    public string Name { get; set; } = "Default local relay";
    public string Host { get; set; } = "192.168.100.2";
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
