package ru.outsidepro_arts.owalkie.model

import android.content.Context
import android.media.MediaRecorder
import android.os.Build
import ru.outsidepro_arts.owalkie.R

class MicrophoneConfigStore(private val context: Context) {
    data class MicrophoneOption(
        val id: String,
        val title: String,
        val audioSource: Int,
        val preferBluetooth: Boolean,
    )

    fun getAvailableOptions(): List<MicrophoneOption> {
        val options = mutableListOf(
            MicrophoneOption(
                id = ID_MIC,
                title = context.getString(R.string.microphone_option_mic),
                audioSource = MediaRecorder.AudioSource.MIC,
                preferBluetooth = false,
            ),
            MicrophoneOption(
                id = ID_DEFAULT,
                title = context.getString(R.string.microphone_option_default),
                audioSource = MediaRecorder.AudioSource.DEFAULT,
                preferBluetooth = false,
            ),
            MicrophoneOption(
                id = ID_CAMCORDER,
                title = context.getString(R.string.microphone_option_camcorder),
                audioSource = MediaRecorder.AudioSource.CAMCORDER,
                preferBluetooth = false,
            ),
            MicrophoneOption(
                id = ID_VOICE_RECOGNITION,
                title = context.getString(R.string.microphone_option_voice_recognition),
                audioSource = MediaRecorder.AudioSource.VOICE_RECOGNITION,
                preferBluetooth = false,
            ),
            MicrophoneOption(
                id = ID_VOICE_COMMUNICATION,
                title = context.getString(R.string.microphone_option_voice_communication),
                audioSource = MediaRecorder.AudioSource.VOICE_COMMUNICATION,
                preferBluetooth = false,
            ),
        )

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            options += MicrophoneOption(
                id = ID_UNPROCESSED,
                title = context.getString(R.string.microphone_option_unprocessed),
                audioSource = MediaRecorder.AudioSource.UNPROCESSED,
                preferBluetooth = false,
            )
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            options += MicrophoneOption(
                id = ID_VOICE_PERFORMANCE,
                title = context.getString(R.string.microphone_option_voice_performance),
                audioSource = MediaRecorder.AudioSource.VOICE_PERFORMANCE,
                preferBluetooth = false,
            )
        }

        options += MicrophoneOption(
            id = ID_BLUETOOTH_HEADSET,
            title = context.getString(R.string.microphone_option_bluetooth_headset),
            audioSource = MediaRecorder.AudioSource.VOICE_COMMUNICATION,
            preferBluetooth = true,
        )
        return options
    }

    fun getSelectedOption(): MicrophoneOption {
        val selectedId = prefs().getString(KEY_MICROPHONE_ID, ID_MIC) ?: ID_MIC
        val options = getAvailableOptions()
        return options.firstOrNull { it.id == selectedId } ?: options.first()
    }

    fun setSelectedOption(id: String) {
        prefs().edit().putString(KEY_MICROPHONE_ID, id).apply()
    }

    private fun prefs() = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)

    companion object {
        private const val PREF_NAME = "microphone_config"
        private const val KEY_MICROPHONE_ID = "microphone_id"

        const val ID_MIC = "mic"
        const val ID_DEFAULT = "default"
        const val ID_CAMCORDER = "camcorder"
        const val ID_VOICE_RECOGNITION = "voice_recognition"
        const val ID_VOICE_COMMUNICATION = "voice_communication"
        const val ID_UNPROCESSED = "unprocessed"
        const val ID_VOICE_PERFORMANCE = "voice_performance"
        const val ID_BLUETOOTH_HEADSET = "bluetooth_headset"
    }
}
