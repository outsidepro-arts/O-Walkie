package ru.outsidepro_arts.owalkie.flutter

import android.Manifest
import android.content.pm.PackageManager
import android.media.AudioManager
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {
    private var pendingMicResult: MethodChannel.Result? = null

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, CHANNEL).setMethodCallHandler { call, result ->
            when (call.method) {
                "hasMicrophonePermission" -> result.success(hasMicPermission())
                "requestMicrophonePermission" -> requestMicPermission(result)
                "prepareAudioSession" -> {
                    val audioManager = getSystemService(AUDIO_SERVICE) as AudioManager
                    audioManager.mode = AudioManager.MODE_IN_COMMUNICATION
                    @Suppress("DEPRECATION")
                    audioManager.isSpeakerphoneOn = true
                    result.success(true)
                }
                "releaseAudioSession" -> {
                    val audioManager = getSystemService(AUDIO_SERVICE) as AudioManager
                    audioManager.mode = AudioManager.MODE_NORMAL
                    result.success(null)
                }
                else -> result.notImplemented()
            }
        }
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray,
    ) {
        if (requestCode == MIC_REQUEST) {
            val granted =
                grantResults.isNotEmpty() && grantResults[0] == PackageManager.PERMISSION_GRANTED
            pendingMicResult?.success(granted)
            pendingMicResult = null
        }
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
    }

    private fun hasMicPermission(): Boolean {
        return ContextCompat.checkSelfPermission(
            this,
            Manifest.permission.RECORD_AUDIO,
        ) == PackageManager.PERMISSION_GRANTED
    }

    private fun requestMicPermission(result: MethodChannel.Result) {
        if (hasMicPermission()) {
            result.success(true)
            return
        }
        if (pendingMicResult != null) {
            result.error("busy", "Microphone permission request already in progress", null)
            return
        }
        pendingMicResult = result
        ActivityCompat.requestPermissions(
            this,
            arrayOf(Manifest.permission.RECORD_AUDIO),
            MIC_REQUEST,
        )
    }

    companion object {
        private const val CHANNEL = "ru.outsidepro_arts.owalkie.flutter/platform"
        private const val MIC_REQUEST = 1001
    }
}
