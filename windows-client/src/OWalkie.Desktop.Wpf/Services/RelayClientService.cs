using System.Buffers.Binary;
using System.Net;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using OWalkie.Desktop.Wpf.Models;

namespace OWalkie.Desktop.Wpf.Services;

public sealed class RelayClientService
{
    private ClientWebSocket? _webSocket;
    private UdpClient? _udpClient;
    private CancellationTokenSource? _connectionCts;
    private Task? _wsReceiveTask;
    private Task? _udpReceiveTask;
    private ConnectionProfile? _activeProfile;
    private int _sessionId;
    private int _packetMs = 20;
    private int _seq;
    private int _connected;
    private string _udpHost = string.Empty;
    private int _udpPort;

    public bool IsConnected { get; private set; }
    public int PacketMs => _packetMs;

    public event EventHandler<bool>? ConnectionStateChanged;
    public event EventHandler<int>? PacketMsChanged;
    public event EventHandler<byte[]>? OpusFrameReceived;
    public event EventHandler<string>? StatusMessage;

    public async Task ConnectAsync(ConnectionProfile profile, bool repeaterEnabled = false, CancellationToken cancellationToken = default)
    {
        if (IsConnected)
        {
            return;
        }

        if (!TryParseServerEndpoint(profile.Host, profile.WsPort, out var wsHost, out var wsPort, out var wsSecure))
        {
            throw new ArgumentException("Invalid server host/port value.");
        }

        _activeProfile = profile;
        _connectionCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        _webSocket = new ClientWebSocket();
        _udpClient = new UdpClient(0);
        _sessionId = 0;
        _packetMs = 20;
        _seq = 0;
        _udpHost = wsHost;
        _udpPort = profile.UdpPort;

        var wsUri = new UriBuilder(wsSecure ? "wss" : "ws", wsHost, wsPort, "/ws").Uri;
        await _webSocket.ConnectAsync(wsUri, _connectionCts.Token);
        IsConnected = true;
        Interlocked.Exchange(ref _connected, 1);
        ConnectionStateChanged?.Invoke(this, true);
        StatusMessage?.Invoke(this, $"WS connected: {wsUri}");

        _udpReceiveTask = Task.Run(() => UdpReceiveLoopAsync(_connectionCts.Token), _connectionCts.Token);
        await SendWsJsonAsync(new
        {
            type = "join",
            channel = profile.Channel,
        }, _connectionCts.Token);
        await SendWsJsonAsync(new
        {
            type = "udp_hello",
            udpPort = ((IPEndPoint)_udpClient.Client.LocalEndPoint!).Port,
        }, _connectionCts.Token);
        await SendWsJsonAsync(new
        {
            type = "repeater_mode",
            enabled = repeaterEnabled,
        }, _connectionCts.Token);

        _wsReceiveTask = Task.Run(() => WsReceiveLoopAsync(_connectionCts.Token), _connectionCts.Token);
    }

    public async Task DisconnectAsync(CancellationToken cancellationToken = default)
    {
        if (!IsConnected)
        {
            return;
        }

        IsConnected = false;
        Interlocked.Exchange(ref _connected, 0);
        _connectionCts?.Cancel();

        if (_webSocket != null)
        {
            try
            {
                if (_webSocket.State == WebSocketState.Open)
                {
                    await _webSocket.CloseAsync(WebSocketCloseStatus.NormalClosure, "client disconnect", cancellationToken);
                }
            }
            catch
            {
                // Ignore shutdown errors.
            }
        }

        _udpClient?.Dispose();
        _webSocket?.Dispose();
        _udpClient = null;
        _webSocket = null;
        _connectionCts?.Dispose();
        _connectionCts = null;
        _wsReceiveTask = null;
        _udpReceiveTask = null;

        IsConnected = false;
        Interlocked.Exchange(ref _connected, 0);
        ConnectionStateChanged?.Invoke(this, false);
        StatusMessage?.Invoke(this, "Disconnected.");
    }

    public async Task SendOpusFrameAsync(byte[] opusBytes, byte signalStrength, CancellationToken cancellationToken = default)
    {
        if (opusBytes.Length == 0 || _udpClient == null || _activeProfile == null || Interlocked.CompareExchange(ref _connected, 0, 0) == 0)
        {
            return;
        }

        var seq = Interlocked.Increment(ref _seq);
        var payload = new byte[9 + opusBytes.Length];
        BinaryPrimitives.WriteInt32BigEndian(payload.AsSpan(0, 4), _sessionId);
        BinaryPrimitives.WriteInt32BigEndian(payload.AsSpan(4, 4), seq);
        payload[8] = signalStrength;
        opusBytes.CopyTo(payload.AsSpan(9));

        cancellationToken.ThrowIfCancellationRequested();
        await _udpClient.SendAsync(payload, payload.Length, _udpHost, _udpPort);
    }

    private static bool TryParseServerEndpoint(string rawHost, int fallbackWsPort, out string host, out int wsPort, out bool wsSecure)
    {
        host = string.Empty;
        wsPort = fallbackWsPort;
        wsSecure = false;

        var value = (rawHost ?? string.Empty).Trim();
        if (string.IsNullOrWhiteSpace(value))
        {
            return false;
        }

        if (Uri.TryCreate(value, UriKind.Absolute, out var absoluteUri))
        {
            var scheme = absoluteUri.Scheme.ToLowerInvariant();
            if (scheme is "ws" or "wss" or "http" or "https")
            {
                host = absoluteUri.Host;
                wsPort = absoluteUri.IsDefaultPort ? fallbackWsPort : absoluteUri.Port;
                wsSecure = scheme is "wss" or "https";
                return !string.IsNullOrWhiteSpace(host) && wsPort is > 0 and <= 65535;
            }
        }

        value = value.Split('/', '?', '#')[0].Trim();
        if (string.IsNullOrWhiteSpace(value))
        {
            return false;
        }

        var atIndex = value.LastIndexOf('@');
        if (atIndex >= 0 && atIndex < value.Length - 1)
        {
            value = value[(atIndex + 1)..];
        }

        if (value.StartsWith("[", StringComparison.Ordinal))
        {
            var close = value.IndexOf(']');
            if (close <= 1)
            {
                return false;
            }

            host = value[1..close].Trim();
            var rest = value[(close + 1)..];
            if (rest.StartsWith(":", StringComparison.Ordinal) && !int.TryParse(rest[1..], out wsPort))
            {
                return false;
            }
        }
        else
        {
            var firstColon = value.IndexOf(':');
            var lastColon = value.LastIndexOf(':');
            if (firstColon >= 0 && firstColon == lastColon)
            {
                var maybePort = value[(lastColon + 1)..];
                if (int.TryParse(maybePort, out var parsedPort))
                {
                    wsPort = parsedPort;
                    value = value[..lastColon];
                }
            }

            host = value.Trim();
        }

        return !string.IsNullOrWhiteSpace(host) && wsPort is > 0 and <= 65535;
    }

    private async Task SendWsJsonAsync<T>(T payload, CancellationToken cancellationToken)
    {
        if (_webSocket == null || _webSocket.State != WebSocketState.Open)
        {
            return;
        }

        var json = JsonSerializer.Serialize(payload);
        var bytes = Encoding.UTF8.GetBytes(json);
        await _webSocket.SendAsync(bytes, WebSocketMessageType.Text, true, cancellationToken);
    }

    private async Task WsReceiveLoopAsync(CancellationToken cancellationToken)
    {
        if (_webSocket == null)
        {
            return;
        }

        var buffer = new byte[4096];
        var builder = new StringBuilder();
        try
        {
            while (!cancellationToken.IsCancellationRequested && _webSocket.State == WebSocketState.Open)
            {
                var result = await _webSocket.ReceiveAsync(buffer, cancellationToken);
                if (result.MessageType == WebSocketMessageType.Close)
                {
                    break;
                }
                if (result.MessageType != WebSocketMessageType.Text)
                {
                    continue;
                }

                builder.Append(Encoding.UTF8.GetString(buffer, 0, result.Count));
                if (!result.EndOfMessage)
                {
                    continue;
                }

                var message = builder.ToString();
                builder.Clear();
                HandleWsMessage(message);
            }
        }
        catch (Exception ex)
        {
            StatusMessage?.Invoke(this, $"WS loop ended: {ex.Message}");
        }
        finally
        {
            if (IsConnected)
            {
                await DisconnectAsync(CancellationToken.None);
            }
        }
    }

    private async Task UdpReceiveLoopAsync(CancellationToken cancellationToken)
    {
        if (_udpClient == null)
        {
            return;
        }

        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                var result = await _udpClient.ReceiveAsync(cancellationToken);
                if (result.Buffer.Length <= 9)
                {
                    continue;
                }

                var opus = new byte[result.Buffer.Length - 9];
                Buffer.BlockCopy(result.Buffer, 9, opus, 0, opus.Length);
                OpusFrameReceived?.Invoke(this, opus);
            }
        }
        catch (OperationCanceledException)
        {
            // Normal shutdown.
        }
        catch (Exception ex)
        {
            StatusMessage?.Invoke(this, $"UDP loop ended: {ex.Message}");
        }
    }

    private void HandleWsMessage(string message)
    {
        try
        {
            using var doc = JsonDocument.Parse(message);
            var root = doc.RootElement;
            if (!root.TryGetProperty("type", out var typeNode))
            {
                return;
            }

            var type = typeNode.GetString() ?? string.Empty;
            if (type == "welcome")
            {
                if (root.TryGetProperty("sessionId", out var sessionNode) && sessionNode.TryGetInt32(out var sid))
                {
                    _sessionId = sid;
                }
                if (root.TryGetProperty("packetMs", out var packetNode) && packetNode.TryGetInt32(out var packet))
                {
                    _packetMs = packet is 10 or 20 or 40 or 60 ? packet : 20;
                    PacketMsChanged?.Invoke(this, _packetMs);
                }
                StatusMessage?.Invoke(this, $"Welcome received. sessionId={_sessionId}, packetMs={_packetMs}");
            }
        }
        catch
        {
            // Ignore malformed server events.
        }
    }
}
