using System.Buffers.Binary;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using OWalkie.Desktop.Wpf.Models;

namespace OWalkie.Desktop.Wpf.Services;

public sealed class RelayClientService
{
    public sealed record OpusConfig(int Bitrate, int Complexity, bool Fec, bool Dtx, string Application)
    {
        public static OpusConfig Default { get; } = new(12000, 5, true, false, "voip");
    }

    private const int ProtocolVersion = 2;
    private ClientWebSocket? _webSocket;
    private UdpClient? _udpClient;
    private CancellationTokenSource? _connectionCts;
    private Task? _wsReceiveTask;
    private Task? _udpReceiveTask;
    private Task? _udpKeepaliveTask;
    private ConnectionProfile? _activeProfile;
    private uint _sessionId;
    private int _sampleRate = 8000;
    private int _packetMs = 20;
    private OpusConfig _opusConfig = OpusConfig.Default;
    private int _seq;
    private int _connected;
    private string _udpHost = string.Empty;
    private int _udpPort;
    private bool _channelBound;
    private bool _requestedRepeaterEnabled;
    private bool _busyMode;
    private bool _busyRxActive;
    private long _busyLastRxTicks;
    private Task? _busyMonitorTask;

    public bool IsConnected { get; private set; }
    public int SampleRate => _sampleRate;
    public int PacketMs => _packetMs;
    public OpusConfig CurrentOpusConfig => _opusConfig;

    public event EventHandler<int>? SampleRateChanged;
    public event EventHandler<bool>? ConnectionStateChanged;
    public event EventHandler<int>? PacketMsChanged;
    public event EventHandler<OpusConfig>? OpusConfigChanged;
    public event EventHandler<byte[]>? OpusFrameReceived;
    public event EventHandler<string>? StatusMessage;
    public event EventHandler<bool>? BusyTransmitBlockedChanged;
    public event EventHandler? ForceTransmitStopRequested;

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
        _sampleRate = 8000;
        _packetMs = 20;
        _opusConfig = OpusConfig.Default;
        _seq = 0;
        _udpHost = wsHost;
        _udpPort = profile.UdpPort;
        _channelBound = false;
        _requestedRepeaterEnabled = repeaterEnabled;
        _busyMode = false;
        _busyRxActive = false;
        _busyLastRxTicks = 0;

        var wsUri = new UriBuilder(wsSecure ? "wss" : "ws", wsHost, wsPort, "/ws").Uri;
        await _webSocket.ConnectAsync(wsUri, _connectionCts.Token);
        IsConnected = true;
        Interlocked.Exchange(ref _connected, 1);
        ConnectionStateChanged?.Invoke(this, true);
        StatusMessage?.Invoke(this, $"WS connected: {wsUri}");

        _udpReceiveTask = Task.Run(() => UdpReceiveLoopAsync(_connectionCts.Token), _connectionCts.Token);
        _udpKeepaliveTask = Task.Run(() => UdpKeepaliveLoopAsync(_connectionCts.Token), _connectionCts.Token);
        _busyMonitorTask = Task.Run(() => BusyMonitorLoopAsync(_connectionCts.Token), _connectionCts.Token);

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
        _udpKeepaliveTask = null;
        _busyMonitorTask = null;
        _busyMode = false;
        SetBusyRxActive(false);

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
        BinaryPrimitives.WriteUInt32BigEndian(payload.AsSpan(0, 4), _sessionId);
        BinaryPrimitives.WriteInt32BigEndian(payload.AsSpan(4, 4), seq);
        payload[8] = signalStrength;
        opusBytes.CopyTo(payload.AsSpan(9));

        cancellationToken.ThrowIfCancellationRequested();
        await _udpClient.SendAsync(payload, payload.Length, _udpHost, _udpPort);
    }

    private async Task SendUdpKeepaliveAsync(CancellationToken cancellationToken)
    {
        if (_udpClient == null || Interlocked.CompareExchange(ref _connected, 0, 0) == 0 || _sessionId == 0)
        {
            return;
        }

        var payload = new byte[9];
        BinaryPrimitives.WriteUInt32BigEndian(payload.AsSpan(0, 4), _sessionId);
        BinaryPrimitives.WriteInt32BigEndian(payload.AsSpan(4, 4), 0);
        payload[8] = 255;
        cancellationToken.ThrowIfCancellationRequested();
        await _udpClient.SendAsync(payload, payload.Length, _udpHost, _udpPort);
    }

    private async Task UdpKeepaliveLoopAsync(CancellationToken cancellationToken)
    {
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                await SendUdpKeepaliveAsync(cancellationToken);
                await Task.Delay(TimeSpan.FromSeconds(2), cancellationToken);
            }
        }
        catch (OperationCanceledException)
        {
            // Normal shutdown.
        }
        catch (Exception ex)
        {
            StatusMessage?.Invoke(this, $"UDP keepalive ended: {ex.Message}");
        }
    }

    public Task SendTxEofAsync(CancellationToken cancellationToken = default)
    {
        return SendUdpTxEofBurstAsync(cancellationToken);
    }

    private async Task SendUdpTxEofBurstAsync(CancellationToken cancellationToken)
    {
        var scheduleMs = new[] { 0, 20, 60 };
        foreach (var delayMs in scheduleMs)
        {
            if (delayMs > 0)
            {
                await Task.Delay(delayMs, cancellationToken);
            }

            await SendUdpTxEofPacketAsync(cancellationToken);
        }
    }

    private async Task SendUdpTxEofPacketAsync(CancellationToken cancellationToken)
    {
        if (_udpClient == null || Interlocked.CompareExchange(ref _connected, 0, 0) == 0 || _sessionId == 0)
        {
            return;
        }

        var seq = Interlocked.Increment(ref _seq);
        var payload = new byte[9];
        BinaryPrimitives.WriteUInt32BigEndian(payload.AsSpan(0, 4), _sessionId);
        BinaryPrimitives.WriteInt32BigEndian(payload.AsSpan(4, 4), seq);
        payload[8] = 0; // signal=0 with empty payload marks UDP TX EOF on relay

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
                await HandleWsMessageAsync(message, cancellationToken);
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
                if (_busyMode)
                {
                    _busyLastRxTicks = Stopwatch.GetTimestamp();
                    SetBusyRxActive(true);
                }
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

    private async Task HandleWsMessageAsync(string message, CancellationToken cancellationToken)
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
                if (!root.TryGetProperty("protocolVersion", out var protocolNode) ||
                    !protocolNode.TryGetInt32(out var serverProtocol) ||
                    serverProtocol != ProtocolVersion)
                {
                    StatusMessage?.Invoke(this, "Protocol incompatible: server/client protocolVersion mismatch.");
                    await DisconnectAsync(CancellationToken.None);
                    return;
                }
                if (root.TryGetProperty("sessionId", out var sessionNode) && sessionNode.TryGetUInt32(out var sid))
                {
                    _sessionId = sid;
                }
                if (root.TryGetProperty("packetMs", out var packetNode) && packetNode.TryGetInt32(out var packet))
                {
                    _packetMs = packet is 10 or 20 or 40 or 60 ? packet : 20;
                    PacketMsChanged?.Invoke(this, _packetMs);
                }
                if (!root.TryGetProperty("sampleRate", out var sampleRateNode) || !sampleRateNode.TryGetInt32(out var sampleRate))
                {
                    StatusMessage?.Invoke(this, "Protocol incompatible: missing sampleRate in welcome.");
                    await DisconnectAsync(CancellationToken.None);
                    return;
                }
                if (sampleRate != NormalizeSampleRate(sampleRate))
                {
                    StatusMessage?.Invoke(this, "Protocol incompatible: unsupported sampleRate in welcome.");
                    await DisconnectAsync(CancellationToken.None);
                    return;
                }
                _sampleRate = sampleRate;
                SampleRateChanged?.Invoke(this, _sampleRate);
                _opusConfig = ParseOpusConfig(root);
                OpusConfigChanged?.Invoke(this, _opusConfig);
                _busyMode = root.TryGetProperty("busyMode", out var busyNode) && busyNode.ValueKind == JsonValueKind.True;
                if (!_busyMode)
                {
                    SetBusyRxActive(false);
                }
                if (!_channelBound && _activeProfile != null)
                {
                    var selectedChannel = (_activeProfile.Channel ?? string.Empty).Trim();
                    if (!string.IsNullOrWhiteSpace(selectedChannel))
                    {
                        await SendWsJsonAsync(new
                        {
                            type = "join",
                            channel = selectedChannel,
                        }, cancellationToken);
                        _channelBound = true;
                        await SendWsJsonAsync(new
                        {
                            type = "udp_hello",
                            udpPort = ((IPEndPoint)_udpClient!.Client.LocalEndPoint!).Port,
                        }, cancellationToken);
                        await SendWsJsonAsync(new
                        {
                            type = "repeater_mode",
                            enabled = _requestedRepeaterEnabled,
                        }, cancellationToken);
                    }
                }
                StatusMessage?.Invoke(this, $"Welcome received. sessionId={_sessionId}, packetMs={_packetMs}");
                return;
            }
            if (type == "tx_stop")
            {
                StatusMessage?.Invoke(this, "Server requested TX stop (transmit timeout).");
                ForceTransmitStopRequested?.Invoke(this, EventArgs.Empty);
            }
        }
        catch
        {
            // Ignore malformed server events.
        }
    }

    private async Task BusyMonitorLoopAsync(CancellationToken cancellationToken)
    {
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                if (_busyMode && _busyRxActive)
                {
                    var elapsed = Stopwatch.GetElapsedTime(_busyLastRxTicks);
                    var holdMs = Math.Max(_packetMs, 20) * 2;
                    if (elapsed > TimeSpan.FromMilliseconds(holdMs))
                    {
                        SetBusyRxActive(false);
                    }
                }
                else if (!_busyMode && _busyRxActive)
                {
                    SetBusyRxActive(false);
                }
                await Task.Delay(50, cancellationToken);
            }
        }
        catch (OperationCanceledException)
        {
            // Normal shutdown.
        }
    }

    private void SetBusyRxActive(bool active)
    {
        if (_busyRxActive == active)
        {
            return;
        }
        _busyRxActive = active;
        BusyTransmitBlockedChanged?.Invoke(this, _busyMode && _busyRxActive);
    }

    private static int NormalizeSampleRate(int value)
    {
        return value is 8000 or 12000 or 16000 or 24000 or 48000 ? value : 8000;
    }

    private static OpusConfig ParseOpusConfig(JsonElement root)
    {
        if (!root.TryGetProperty("opus", out var opusNode) || opusNode.ValueKind != JsonValueKind.Object)
        {
            return OpusConfig.Default;
        }

        var bitrate = opusNode.TryGetProperty("bitrate", out var bitrateNode) && bitrateNode.TryGetInt32(out var parsedBitrate)
            ? Math.Clamp(parsedBitrate, 6000, 510000)
            : OpusConfig.Default.Bitrate;
        var complexity = opusNode.TryGetProperty("complexity", out var complexityNode) && complexityNode.TryGetInt32(out var parsedComplexity)
            ? Math.Clamp(parsedComplexity, 0, 10)
            : OpusConfig.Default.Complexity;
        var fec = opusNode.TryGetProperty("fec", out var fecNode) && fecNode.ValueKind is JsonValueKind.True or JsonValueKind.False
            ? fecNode.GetBoolean()
            : OpusConfig.Default.Fec;
        var dtx = opusNode.TryGetProperty("dtx", out var dtxNode) && dtxNode.ValueKind is JsonValueKind.True or JsonValueKind.False
            ? dtxNode.GetBoolean()
            : OpusConfig.Default.Dtx;
        var application = opusNode.TryGetProperty("application", out var appNode) && appNode.ValueKind == JsonValueKind.String
            ? NormalizeApplication(appNode.GetString())
            : OpusConfig.Default.Application;
        return new OpusConfig(bitrate, complexity, fec, dtx, application);
    }

    private static string NormalizeApplication(string? value)
    {
        return (value ?? string.Empty).Trim().ToLowerInvariant() switch
        {
            "voip" => "voip",
            "audio" => "audio",
            "lowdelay" => "lowdelay",
            _ => OpusConfig.Default.Application,
        };
    }
}
