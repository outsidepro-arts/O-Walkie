package ru.outsidepro_arts.owalkie.flutter

import android.content.Context
import android.media.AudioManager

/**
 * Audio output profile registry for Android.
 * Maps profile IDs to AudioManager mode + BT SCO routing.
 * AAudio usage/contentType and capture usage are handled via FFI in Dart.
 */
object AudioOutputProfileRegistry {
    data class Option(
        val id: String,
        val title: String,
        val audioManagerMode: Int,
        val enableBtSco: Boolean,
    )

    fun listOptions(context: Context): List<Option> {
        return listOf(
            Option(ID_MEDIA, context.getString(R.string.audio_output_media), MODE_NORMAL, false),
            Option(ID_VOICE_CALL, context.getString(R.string.audio_output_voice_call), MODE_IN_COMMUNICATION, false),
            Option(ID_VOICE_CALL_BT, context.getString(R.string.audio_output_voice_call_bt), MODE_IN_COMMUNICATION, true),
            Option(ID_GAME, context.getString(R.string.audio_output_game), MODE_NORMAL, false),
            Option(ID_RAW, context.getString(R.string.audio_output_raw), MODE_NORMAL, false),
            Option(ID_NOTIFICATION, context.getString(R.string.audio_output_notification), MODE_NORMAL, false),
        )
    }

    fun applyProfile(context: Context, id: String) {
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        when (id) {
            ID_VOICE_CALL -> {
                audioManager.mode = AudioManager.MODE_IN_COMMUNICATION
                @Suppress("DEPRECATION")
                audioManager.isSpeakerphoneOn = true
                AudioRouteHelper.disableBluetoothInputRoute(audioManager)
            }
            ID_VOICE_CALL_BT -> {
                audioManager.mode = AudioManager.MODE_IN_COMMUNICATION
                @Suppress("DEPRECATION")
                audioManager.isSpeakerphoneOn = false
                AudioRouteHelper.enableBluetoothInputRoute(audioManager)
            }
            else -> {
                audioManager.mode = AudioManager.MODE_NORMAL
                @Suppress("DEPRECATION")
                audioManager.isSpeakerphoneOn = false
                AudioRouteHelper.disableBluetoothInputRoute(audioManager)
            }
        }
    }

    const val ID_MEDIA = "media"
    const val ID_VOICE_CALL = "voice_call"
    const val ID_VOICE_CALL_BT = "voice_call_bt"
    const val ID_GAME = "game"
    const val ID_RAW = "raw"
    const val ID_NOTIFICATION = "notification"

    private const val MODE_NORMAL = 0
    private const val MODE_IN_COMMUNICATION = 3
}