package ru.outsidepro_arts.owalkie.flutter

import android.content.Context
import android.os.Build
import ru.outsidepro_arts.owalkie.flutter.R

/**
 * Android microphone capture profile (Kotlin client [MicrophoneConfigStore] parity).
 * Maps to miniaudio [ma_aaudio_input_preset] values passed to native capture.
 */
object MicrophoneSourceRegistry {
    data class Option(
        val id: String,
        val title: String,
        /** miniaudio ma_aaudio_input_preset ordinal. */
        val inputPreset: Int,
    )

    fun listOptions(context: Context): List<Option> {
        val options = mutableListOf(
            Option(ID_MIC, context.getString(R.string.microphone_option_mic), PRESET_GENERIC),
            Option(ID_DEFAULT, context.getString(R.string.microphone_option_default), PRESET_DEFAULT),
            Option(ID_CAMCORDER, context.getString(R.string.microphone_option_camcorder), PRESET_CAMCORDER),
            Option(
                ID_VOICE_RECOGNITION,
                context.getString(R.string.microphone_option_voice_recognition),
                PRESET_VOICE_RECOGNITION,
            ),
            Option(
                ID_VOICE_COMMUNICATION,
                context.getString(R.string.microphone_option_voice_communication),
                PRESET_VOICE_COMMUNICATION,
            ),
        )
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            options += Option(
                ID_UNPROCESSED,
                context.getString(R.string.microphone_option_unprocessed),
                PRESET_UNPROCESSED,
            )
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            options += Option(
                ID_VOICE_PERFORMANCE,
                context.getString(R.string.microphone_option_voice_performance),
                PRESET_VOICE_PERFORMANCE,
            )
        }
        return options
    }

    fun presetForId(context: Context, id: String): Int {
        return listOptions(context).firstOrNull { it.id == id }?.inputPreset ?: PRESET_GENERIC
    }

    const val ID_MIC = "mic"
    const val ID_DEFAULT = "default"
    const val ID_CAMCORDER = "camcorder"
    const val ID_VOICE_RECOGNITION = "voice_recognition"
    const val ID_VOICE_COMMUNICATION = "voice_communication"
    const val ID_UNPROCESSED = "unprocessed"
    const val ID_VOICE_PERFORMANCE = "voice_performance"

    private const val PRESET_DEFAULT = 0
    private const val PRESET_GENERIC = 1
    private const val PRESET_CAMCORDER = 2
    private const val PRESET_VOICE_RECOGNITION = 3
    private const val PRESET_VOICE_COMMUNICATION = 4
    private const val PRESET_UNPROCESSED = 5
    private const val PRESET_VOICE_PERFORMANCE = 6
}
