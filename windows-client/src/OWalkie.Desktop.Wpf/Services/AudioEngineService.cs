using Concentus.Enums;
using Concentus.Structs;
using NAudio.CoreAudioApi;
using NAudio.Wave;
using System.Collections.Concurrent;
using System.Diagnostics;

namespace OWalkie.Desktop.Wpf.Services;

public sealed class AudioEngineService
{
    private const int TargetSampleRate = 8000;
    private const int Channels = 1;

    private readonly object _codecLock = new();
    private readonly object _captureLock = new();
    private readonly List<float> _captureMonoBuffer = new();
    private readonly ConcurrentQueue<short[]> _pcmQueue = new();
    private readonly SemaphoreSlim _pcmSignal = new(0);

    private RelayClientService? _relayClientService;
    private WasapiCapture? _capture;
    private IWaveIn? _captureFallback;
    private IWavePlayer? _playback;
    private BufferedWaveProvider? _playbackBuffer;
    private OpusEncoder? _encoder;
    private OpusDecoder? _decoder;
    private int _frameSamples = 160;
    private double _captureReadPos;
    private CancellationTokenSource? _senderLoopCts;
    private Task? _senderLoopTask;
    private int _queuedPcmFrames;
    private int _packetIntervalMs = 20;

    public bool IsTransmitting { get; private set; }
    public event EventHandler<int>? OutputLevelChanged;

    public void Initialize(RelayClientService relayClientService)
    {
        _relayClientService = relayClientService;
        _relayClientService.PacketMsChanged += OnPacketMsChanged;
        _relayClientService.OpusFrameReceived += OnOpusFrameReceived;
        EnsurePlaybackPipeline();
        EnsureCodec();
        EnsureSenderLoop();
    }

    public Task StartTransmitAsync(CancellationToken cancellationToken = default)
    {
        if (IsTransmitting)
        {
            return Task.CompletedTask;
        }

        EnsureCodec();
        StartCapture();
        IsTransmitting = true;
        return Task.CompletedTask;
    }

    public Task StopTransmitAsync(CancellationToken cancellationToken = default)
    {
        if (!IsTransmitting)
        {
            return Task.CompletedTask;
        }

        IsTransmitting = false;
        StopCapture();
        ClearTxQueue();
        return Task.CompletedTask;
    }

    private void OnPacketMsChanged(object? sender, int packetMs)
    {
        _frameSamples = TargetSampleRate * packetMs / 1000;
        _packetIntervalMs = packetMs is 10 or 20 or 40 or 60 ? packetMs : 20;
        EnsureCodec();
    }

    private void EnsureCodec()
    {
        lock (_codecLock)
        {
            _encoder = new OpusEncoder(TargetSampleRate, Channels, OpusApplication.OPUS_APPLICATION_VOIP);
            _encoder.Bitrate = 12000;
            _encoder.SignalType = OpusSignal.OPUS_SIGNAL_VOICE;
            _decoder = new OpusDecoder(TargetSampleRate, Channels);
        }
    }

    private void EnsurePlaybackPipeline()
    {
        if (_playback != null && _playbackBuffer != null)
        {
            return;
        }

        _playbackBuffer = new BufferedWaveProvider(new WaveFormat(TargetSampleRate, 16, Channels))
        {
            DiscardOnBufferOverflow = true,
            BufferDuration = TimeSpan.FromSeconds(2),
        };

        try
        {
            var renderDevice = new MMDeviceEnumerator().GetDefaultAudioEndpoint(DataFlow.Render, Role.Multimedia);
            var wasapiOut = new WasapiOut(renderDevice, AudioClientShareMode.Shared, true, 40);
            wasapiOut.Init(_playbackBuffer);
            wasapiOut.Play();
            _playback = wasapiOut;
        }
        catch
        {
            var fallback = new WaveOutEvent();
            fallback.Init(_playbackBuffer);
            fallback.Play();
            _playback = fallback;
        }
    }

    private void StartCapture()
    {
        lock (_captureLock)
        {
            if (_capture != null || _captureFallback != null)
            {
                return;
            }

            _captureMonoBuffer.Clear();
            _captureReadPos = 0;

            try
            {
                var fallback = new WaveInEvent
                {
                    BufferMilliseconds = 20,
                    NumberOfBuffers = 4,
                    WaveFormat = new WaveFormat(16000, 16, 1),
                };
                fallback.DataAvailable += CaptureOnDataAvailable;
                fallback.StartRecording();
                _captureFallback = fallback;
            }
            catch
            {
                try
                {
                    var captureDevice = new MMDeviceEnumerator().GetDefaultAudioEndpoint(DataFlow.Capture, Role.Communications);
                    _capture = new WasapiCapture(captureDevice);
                    _capture.DataAvailable += CaptureOnDataAvailable;
                    _capture.StartRecording();
                }
                catch
                {
                    // No usable capture backend available.
                }
            }
        }
    }

    private void StopCapture()
    {
        lock (_captureLock)
        {
            if (_capture != null)
            {
                _capture.DataAvailable -= CaptureOnDataAvailable;
                _capture.StopRecording();
                _capture.Dispose();
                _capture = null;
            }
            if (_captureFallback != null)
            {
                _captureFallback.DataAvailable -= CaptureOnDataAvailable;
                _captureFallback.StopRecording();
                _captureFallback.Dispose();
                _captureFallback = null;
            }
            _captureMonoBuffer.Clear();
            _captureReadPos = 0;
        }
    }

    private void CaptureOnDataAvailable(object? sender, WaveInEventArgs e)
    {
        if (!IsTransmitting || _relayClientService == null)
        {
            return;
        }

        var waveFormat = (sender as IWaveIn)?.WaveFormat ?? new WaveFormat(TargetSampleRate, 16, 1);
        var mono = ConvertToMonoSamples(e.Buffer, e.BytesRecorded, waveFormat);
        if (mono.Count == 0)
        {
            return;
        }

        lock (_captureLock)
        {
            _captureMonoBuffer.AddRange(mono);
            var sampleRate = Math.Max(1, waveFormat.SampleRate);
            var step = sampleRate / (double)TargetSampleRate;

            while (CanBuildFrame(step))
            {
                var pcm = BuildFrame(step);
                if (pcm.Length == 0)
                {
                    break;
                }
                EnqueuePcmFrame(pcm);
            }

            var consumedFloor = (int)_captureReadPos;
            if (consumedFloor > 1)
            {
                var drop = consumedFloor - 1;
                _captureMonoBuffer.RemoveRange(0, Math.Min(drop, _captureMonoBuffer.Count));
                _captureReadPos -= drop;
            }
        }
    }

    private bool CanBuildFrame(double step)
    {
        if (_captureMonoBuffer.Count < 2)
        {
            return false;
        }

        var lastRequired = _captureReadPos + ((_frameSamples - 1) * step);
        return lastRequired < (_captureMonoBuffer.Count - 1);
    }

    private short[] BuildFrame(double step)
    {
        var pcm = new short[_frameSamples];
        for (var i = 0; i < _frameSamples; i++)
        {
            var srcPos = _captureReadPos + (i * step);
            var left = (int)srcPos;
            var right = Math.Min(left + 1, _captureMonoBuffer.Count - 1);
            var frac = srcPos - left;
            var value = (_captureMonoBuffer[left] * (1.0 - frac)) + (_captureMonoBuffer[right] * frac);
            var sampleInt = (int)(value * short.MaxValue);
            pcm[i] = (short)Math.Clamp(sampleInt, short.MinValue, short.MaxValue);
        }
        _captureReadPos += _frameSamples * step;
        return pcm;
    }

    private byte[] EncodeFrame(short[] pcm)
    {
        lock (_codecLock)
        {
            if (_encoder == null)
            {
                return Array.Empty<byte>();
            }

            var outBuffer = new byte[512];
            var encodedBytes = _encoder.Encode(pcm, 0, _frameSamples, outBuffer, 0, outBuffer.Length);
            if (encodedBytes <= 0)
            {
                return Array.Empty<byte>();
            }

            var opus = new byte[encodedBytes];
            Buffer.BlockCopy(outBuffer, 0, opus, 0, encodedBytes);
            return opus;
        }
    }

    private void OnOpusFrameReceived(object? sender, byte[] opus)
    {
        if (_playbackBuffer == null || opus.Length == 0 || IsTransmitting)
        {
            return;
        }

        short[] pcmBuffer;
        int decodedSamples;
        lock (_codecLock)
        {
            if (_decoder == null)
            {
                return;
            }

            pcmBuffer = new short[_frameSamples * 2];
            decodedSamples = _decoder.Decode(opus, 0, opus.Length, pcmBuffer, 0, _frameSamples, false);
        }

        if (decodedSamples <= 0)
        {
            return;
        }

        var outBytes = new byte[decodedSamples * sizeof(short)];
        Buffer.BlockCopy(pcmBuffer, 0, outBytes, 0, outBytes.Length);
        _playbackBuffer.AddSamples(outBytes, 0, outBytes.Length);
        OutputLevelChanged?.Invoke(this, EstimateSignalPercent(pcmBuffer, decodedSamples));
    }

    private static int EstimateSignalPercent(short[] pcm, int count)
    {
        if (count <= 0)
        {
            return 0;
        }

        double sum = 0.0;
        for (var i = 0; i < count; i++)
        {
            var sample = pcm[i] / (double)short.MaxValue;
            sum += sample * sample;
        }

        var rms = Math.Sqrt(sum / count);
        return (int)Math.Clamp(rms * 220.0, 0.0, 100.0);
    }

    private static List<float> ConvertToMonoSamples(byte[] buffer, int bytesRecorded, WaveFormat format)
    {
        var channels = Math.Max(1, format.Channels);
        var result = new List<float>(bytesRecorded / Math.Max(2, format.BlockAlign));

        if (format.Encoding == WaveFormatEncoding.IeeeFloat && format.BitsPerSample == 32)
        {
            var frames = bytesRecorded / format.BlockAlign;
            for (var frame = 0; frame < frames; frame++)
            {
                var acc = 0.0f;
                for (var ch = 0; ch < channels; ch++)
                {
                    var offset = (frame * format.BlockAlign) + (ch * 4);
                    acc += BitConverter.ToSingle(buffer, offset);
                }
                result.Add(acc / channels);
            }
            return result;
        }

        if (format.BitsPerSample == 16)
        {
            var frames = bytesRecorded / format.BlockAlign;
            for (var frame = 0; frame < frames; frame++)
            {
                var acc = 0.0f;
                for (var ch = 0; ch < channels; ch++)
                {
                    var offset = (frame * format.BlockAlign) + (ch * 2);
                    var sample = BitConverter.ToInt16(buffer, offset) / (float)short.MaxValue;
                    acc += sample;
                }
                result.Add(acc / channels);
            }
        }

        return result;
    }

    private void EnsureSenderLoop()
    {
        if (_senderLoopTask is { IsCompleted: false })
        {
            return;
        }

        _senderLoopCts = new CancellationTokenSource();
        var token = _senderLoopCts.Token;
        _senderLoopTask = Task.Run(async () =>
        {
            var pacingClock = Stopwatch.StartNew();
            long nextSendAtMs = pacingClock.ElapsedMilliseconds;
            while (!token.IsCancellationRequested)
            {
                try
                {
                    await _pcmSignal.WaitAsync(token);
                }
                catch (OperationCanceledException)
                {
                    break;
                }

                if (!_pcmQueue.TryDequeue(out var pcm))
                {
                    continue;
                }

                Interlocked.Decrement(ref _queuedPcmFrames);
                if (!IsTransmitting || pcm.Length == 0 || _relayClientService == null)
                {
                    continue;
                }

                try
                {
                    var encoded = EncodeFrame(pcm);
                    if (encoded.Length > 0)
                    {
                        await _relayClientService.SendOpusFrameAsync(encoded, 255, token);
                    }
                }
                catch (OperationCanceledException)
                {
                    return;
                }
                catch
                {
                    // Ignore transient network send failures in sender loop.
                }

                nextSendAtMs += _packetIntervalMs;
                var now = pacingClock.ElapsedMilliseconds;
                var delayMs = nextSendAtMs - now;
                if (delayMs > 0)
                {
                    try
                    {
                        await Task.Delay((int)delayMs, token);
                    }
                    catch (OperationCanceledException)
                    {
                        return;
                    }
                }
                else if (delayMs < -(_packetIntervalMs * 3L))
                {
                    // If we're too late, resync pacing to "now" to avoid burst catch-up.
                    nextSendAtMs = now;
                }
            }
        }, token);
    }

    private void EnqueuePcmFrame(short[] frame)
    {
        // Keep bounded queue: on overload drop oldest PCM frames to preserve low latency.
        while (Volatile.Read(ref _queuedPcmFrames) >= 12 && _pcmQueue.TryDequeue(out _))
        {
            Interlocked.Decrement(ref _queuedPcmFrames);
        }

        _pcmQueue.Enqueue(frame);
        Interlocked.Increment(ref _queuedPcmFrames);
        _pcmSignal.Release();
    }

    private void ClearTxQueue()
    {
        while (_pcmQueue.TryDequeue(out _))
        {
            Interlocked.Decrement(ref _queuedPcmFrames);
        }
    }
}
