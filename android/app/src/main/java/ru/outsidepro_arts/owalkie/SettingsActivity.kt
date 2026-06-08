package ru.outsidepro_arts.owalkie

import android.app.AlertDialog
import android.content.ActivityNotFoundException
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.KeyEvent
import android.view.View
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.CheckBox
import android.widget.Spinner
import android.widget.TextView
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import ru.outsidepro_arts.owalkie.model.CallingPatternStore
import ru.outsidepro_arts.owalkie.model.expandedCallingPoints
import ru.outsidepro_arts.owalkie.model.BluetoothHeadsetRouteStore
import ru.outsidepro_arts.owalkie.model.ExternalControlStore
import ru.outsidepro_arts.owalkie.model.MicrophoneConfigStore
import ru.outsidepro_arts.owalkie.model.PhoneCallRelayPauseStore
import ru.outsidepro_arts.owalkie.model.PttHardwareKeyStore
import ru.outsidepro_arts.owalkie.model.RogerPattern
import ru.outsidepro_arts.owalkie.model.RogerPatternStore
import ru.outsidepro_arts.owalkie.model.ScreenOrientationStore
import ru.outsidepro_arts.owalkie.model.WarmMicRecorderStore

class SettingsActivity : ComponentActivity() {
    companion object {
        private const val CLIENT_PROTOCOL_VERSION = 2
        private const val PROJECT_GITHUB_URL = "https://github.com/outsidepro-arts/O-Walkie"
    }

    private lateinit var rogerPatternStore: RogerPatternStore
    private lateinit var callingPatternStore: CallingPatternStore
    private lateinit var microphoneConfigStore: MicrophoneConfigStore
    private lateinit var bluetoothHeadsetRouteStore: BluetoothHeadsetRouteStore
    private lateinit var pttHardwareKeyStore: PttHardwareKeyStore
    private lateinit var externalControlStore: ExternalControlStore
    private lateinit var phoneCallRelayPauseStore: PhoneCallRelayPauseStore
    private lateinit var warmMicRecorderStore: WarmMicRecorderStore
    private lateinit var screenOrientationStore: ScreenOrientationStore
    private lateinit var screenOrientationSpinner: Spinner
    private lateinit var hardwarePttRow: View
    private lateinit var hardwarePttStatusText: TextView
    private lateinit var microphoneSpinner: Spinner
    private lateinit var useBluetoothHeadsetCheckBox: CheckBox
    private lateinit var warmMicRecorderCheckBox: CheckBox
    private lateinit var pttToggleModeCheckBox: CheckBox
    private lateinit var mediaButtonPttCheckBox: CheckBox
    private lateinit var externalControlCheckBox: CheckBox
    private lateinit var pauseRelayDuringCellularCallCheckBox: CheckBox
    private lateinit var rogerSpinner: Spinner
    private lateinit var callingSpinner: Spinner
    private lateinit var customRogerButton: Button
    private lateinit var customCallingButton: Button
    private lateinit var playRogerButton: Button
    private lateinit var playCallingButton: Button
    private lateinit var editRogerButton: Button
    private lateinit var editCallingButton: Button
    private lateinit var versionInfoText: TextView
    private lateinit var openGithubButton: Button
    private val microphoneOptions = mutableListOf<MicrophoneConfigStore.MicrophoneOption>()
    private val rogerPatterns = mutableListOf<RogerPattern>()
    private val callingPatterns = mutableListOf<RogerPattern>()
    private val orientationModes = ScreenOrientationStore.Mode.entries.toList()
    private var suppressOrientationSpinnerCallback = false

    private val customPatternEditorLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
            refreshPatterns()
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        screenOrientationStore = ScreenOrientationStore(this)
        ScreenOrientationStore.applyTo(this)
        setContentView(R.layout.activity_settings)
        title = getString(R.string.menu_settings)
        rogerPatternStore = RogerPatternStore(this)
        callingPatternStore = CallingPatternStore(this)
        microphoneConfigStore = MicrophoneConfigStore(this)
        bluetoothHeadsetRouteStore = BluetoothHeadsetRouteStore(this)
        pttHardwareKeyStore = PttHardwareKeyStore(this)
        externalControlStore = ExternalControlStore(this)
        phoneCallRelayPauseStore = PhoneCallRelayPauseStore(this)
        warmMicRecorderStore = WarmMicRecorderStore(this)

        screenOrientationSpinner = findViewById(R.id.screenOrientationSpinner)
        initScreenOrientationSpinner()

        hardwarePttRow = findViewById(R.id.hardwarePttRow)
        hardwarePttStatusText = findViewById(R.id.hardwarePttStatusText)
        microphoneSpinner = findViewById(R.id.microphoneSpinner)
        useBluetoothHeadsetCheckBox = findViewById(R.id.useBluetoothHeadsetCheckBox)
        warmMicRecorderCheckBox = findViewById(R.id.warmMicRecorderCheckBox)
        pttToggleModeCheckBox = findViewById(R.id.pttToggleModeCheckBox)
        mediaButtonPttCheckBox = findViewById(R.id.mediaButtonPttCheckBox)
        externalControlCheckBox = findViewById(R.id.externalControlCheckBox)
        pauseRelayDuringCellularCallCheckBox = findViewById(R.id.pauseRelayDuringCellularCallCheckBox)
        rogerSpinner = findViewById(R.id.rogerPatternSpinner)
        callingSpinner = findViewById(R.id.callingPatternSpinner)
        customRogerButton = findViewById(R.id.customRogerButton)
        customCallingButton = findViewById(R.id.customCallingButton)
        playRogerButton = findViewById(R.id.playRogerButton)
        playCallingButton = findViewById(R.id.playCallingButton)
        editRogerButton = findViewById(R.id.editRogerButton)
        editCallingButton = findViewById(R.id.editCallingButton)
        versionInfoText = findViewById(R.id.settingsVersionInfoText)
        openGithubButton = findViewById(R.id.settingsOpenGithubButton)

        versionInfoText.text = getString(
            R.string.settings_version_info_format,
            currentClientVersionName(),
            CLIENT_PROTOCOL_VERSION,
        )
        openGithubButton.setOnClickListener {
            val intent = Intent(Intent.ACTION_VIEW, Uri.parse(PROJECT_GITHUB_URL))
            runCatching { startActivity(intent) }.onFailure {
                if (it is ActivityNotFoundException) {
                    android.widget.Toast.makeText(
                        this,
                        getString(R.string.settings_github_open_failed),
                        android.widget.Toast.LENGTH_SHORT,
                    ).show()
                }
            }
        }

        customRogerButton.setOnClickListener {
            customPatternEditorLauncher.launch(
                RogerPatternEditorActivity.intent(this, RogerPatternEditorActivity.SIGNAL_KIND_ROGER),
            )
        }
        customCallingButton.setOnClickListener {
            customPatternEditorLauncher.launch(
                RogerPatternEditorActivity.intent(this, RogerPatternEditorActivity.SIGNAL_KIND_CALLING),
            )
        }
        playRogerButton.setOnClickListener {
            val pattern = rogerPatterns.getOrNull(rogerSpinner.selectedItemPosition) ?: rogerPatternStore.getSelectedPattern()
            SignalPreviewPlayer.playPattern(pattern.points)
        }
        playCallingButton.setOnClickListener {
            val pattern = callingPatterns.getOrNull(callingSpinner.selectedItemPosition) ?: callingPatternStore.getSelectedPattern()
            SignalPreviewPlayer.playPattern(pattern.expandedCallingPoints())
        }
        editRogerButton.setOnClickListener {
            val p = rogerPatterns.getOrNull(rogerSpinner.selectedItemPosition) ?: return@setOnClickListener
            if (p.builtIn) return@setOnClickListener
            customPatternEditorLauncher.launch(
                RogerPatternEditorActivity.intentEdit(this, RogerPatternEditorActivity.SIGNAL_KIND_ROGER, p.id),
            )
        }
        editCallingButton.setOnClickListener {
            val p = callingPatterns.getOrNull(callingSpinner.selectedItemPosition) ?: return@setOnClickListener
            if (p.builtIn) return@setOnClickListener
            customPatternEditorLauncher.launch(
                RogerPatternEditorActivity.intentEdit(this, RogerPatternEditorActivity.SIGNAL_KIND_CALLING, p.id),
            )
        }
        hardwarePttRow.setOnClickListener {
            showHardwarePttAssignmentDialog()
        }
        useBluetoothHeadsetCheckBox.setOnCheckedChangeListener { _, isChecked ->
            bluetoothHeadsetRouteStore.setEnabled(isChecked)
            sendBluetoothHeadsetModeToService(isChecked)
        }
        warmMicRecorderCheckBox.setOnCheckedChangeListener { _, isChecked ->
            warmMicRecorderStore.setEnabled(isChecked)
            sendWarmMicRecorderModeToService(isChecked)
        }
        pttToggleModeCheckBox.setOnCheckedChangeListener { _, isChecked ->
            pttHardwareKeyStore.setToggleModeEnabled(isChecked)
            refreshMediaButtonPttCheckbox()
            notifyWalkieServicePttMediaSessionSync()
        }

        refreshPatterns()
    }

    override fun onResume() {
        super.onResume()
        refreshPatterns()
    }

    private fun refreshPatterns() {
        refreshScreenOrientationSpinner()
        refreshHardwarePttStatus()
        refreshPttToggleMode()
        refreshMediaButtonPttCheckbox()
        refreshExternalControlCheckbox()
        refreshPhoneCallRelayPauseToggle()
        refreshMicrophoneOptions()
        refreshBluetoothHeadsetToggle()
        refreshWarmMicRecorderToggle()
        refreshRogerPatterns()
        refreshCallingPatterns()
    }

    private fun currentClientVersionName(): String {
        return runCatching {
            @Suppress("DEPRECATION")
            val pkgInfo = packageManager.getPackageInfo(packageName, 0)
            pkgInfo.versionName ?: "dev"
        }.getOrDefault("dev")
    }

    private fun initScreenOrientationSpinner() {
        val adapter = ArrayAdapter(
            this,
            android.R.layout.simple_spinner_item,
            orientationModes.map { screenOrientationModeLabel(it) },
        )
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        screenOrientationSpinner.adapter = adapter
        screenOrientationSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
                if (suppressOrientationSpinnerCallback) return
                val mode = orientationModes[position]
                screenOrientationStore.setMode(mode)
                ScreenOrientationStore.applyTo(this@SettingsActivity, mode)
            }

            override fun onNothingSelected(parent: AdapterView<*>?) {}
        }
        refreshScreenOrientationSpinner()
    }

    private fun refreshScreenOrientationSpinner() {
        if (!::screenOrientationSpinner.isInitialized) return
        val idx = orientationModes.indexOf(screenOrientationStore.getMode()).coerceAtLeast(0)
        suppressOrientationSpinnerCallback = true
        screenOrientationSpinner.setSelection(idx)
        suppressOrientationSpinnerCallback = false
    }

    private fun screenOrientationModeLabel(mode: ScreenOrientationStore.Mode): String = when (mode) {
        ScreenOrientationStore.Mode.PORTRAIT -> getString(R.string.screen_orientation_portrait)
        ScreenOrientationStore.Mode.LANDSCAPE -> getString(R.string.screen_orientation_landscape)
        ScreenOrientationStore.Mode.FOLLOW_SYSTEM -> getString(R.string.screen_orientation_follow_system)
    }

    private fun refreshBluetoothHeadsetToggle() {
        val enabled = bluetoothHeadsetRouteStore.isEnabled()
        useBluetoothHeadsetCheckBox.setOnCheckedChangeListener(null)
        useBluetoothHeadsetCheckBox.isChecked = enabled
        useBluetoothHeadsetCheckBox.setOnCheckedChangeListener { _, isChecked ->
            bluetoothHeadsetRouteStore.setEnabled(isChecked)
            sendBluetoothHeadsetModeToService(isChecked)
        }
    }

    private fun refreshWarmMicRecorderToggle() {
        val enabled = warmMicRecorderStore.isEnabled()
        warmMicRecorderCheckBox.setOnCheckedChangeListener(null)
        warmMicRecorderCheckBox.isChecked = enabled
        warmMicRecorderCheckBox.setOnCheckedChangeListener { _, isChecked ->
            warmMicRecorderStore.setEnabled(isChecked)
            sendWarmMicRecorderModeToService(isChecked)
        }
    }

    private fun refreshHardwarePttStatus() {
        val binding = pttHardwareKeyStore.getBinding()
        hardwarePttStatusText.text = if (!binding.isAssigned()) {
            getString(R.string.hardware_ptt_status_unassigned)
        } else {
            getString(R.string.hardware_ptt_status_assigned_format, bindingToDisplayName(binding))
        }
    }

    private fun refreshPttToggleMode() {
        val enabled = pttHardwareKeyStore.isToggleModeEnabled()
        pttToggleModeCheckBox.setOnCheckedChangeListener(null)
        pttToggleModeCheckBox.isChecked = enabled
        pttToggleModeCheckBox.setOnCheckedChangeListener { _, isChecked ->
            pttHardwareKeyStore.setToggleModeEnabled(isChecked)
            refreshMediaButtonPttCheckbox()
            notifyWalkieServicePttMediaSessionSync()
        }
    }

    private fun refreshMediaButtonPttCheckbox() {
        val toggleOn = pttHardwareKeyStore.isToggleModeEnabled()
        mediaButtonPttCheckBox.setOnCheckedChangeListener(null)
        mediaButtonPttCheckBox.isEnabled = toggleOn
        mediaButtonPttCheckBox.isChecked = pttHardwareKeyStore.isMediaButtonPttEnabled()
        mediaButtonPttCheckBox.setOnCheckedChangeListener { _, isChecked ->
            if (!pttHardwareKeyStore.isToggleModeEnabled()) return@setOnCheckedChangeListener
            pttHardwareKeyStore.setMediaButtonPttEnabled(isChecked)
            notifyWalkieServicePttMediaSessionSync()
        }
    }

    private fun notifyWalkieServicePttMediaSessionSync() {
        val intent = android.content.Intent(this, WalkieService::class.java).apply {
            action = WalkieService.ACTION_SYNC_PTT_MEDIA_SESSION
        }
        startService(intent)
    }

    private fun refreshExternalControlCheckbox() {
        val enabled = externalControlStore.isEnabled()
        externalControlCheckBox.setOnCheckedChangeListener(null)
        externalControlCheckBox.isChecked = enabled
        externalControlCheckBox.setOnCheckedChangeListener { _, isChecked ->
            externalControlStore.setEnabled(isChecked)
        }
    }

    private fun refreshPhoneCallRelayPauseToggle() {
        val enabled = phoneCallRelayPauseStore.isPauseDuringCallEnabled()
        pauseRelayDuringCellularCallCheckBox.setOnCheckedChangeListener(null)
        pauseRelayDuringCellularCallCheckBox.isChecked = enabled
        pauseRelayDuringCellularCallCheckBox.setOnCheckedChangeListener { _, isChecked ->
            phoneCallRelayPauseStore.setPauseDuringCallEnabled(isChecked)
        }
    }

    private fun showHardwarePttAssignmentDialog() {
        val dialogView = layoutInflater.inflate(R.layout.dialog_ptt_hardware_key, null)
        val valueText = dialogView.findViewById<TextView>(R.id.pttKeyDialogValueText)
        var pendingBinding = pttHardwareKeyStore.getBinding()
        valueText.text = if (!pendingBinding.isAssigned()) {
            getString(R.string.hardware_ptt_dialog_waiting)
        } else {
            bindingToDisplayName(pendingBinding)
        }
        AlertDialog.Builder(this)
            .setTitle(R.string.hardware_ptt_dialog_title)
            .setView(dialogView)
            .setNeutralButton(R.string.common_reset) { _, _ ->
                pttHardwareKeyStore.clearBinding()
                refreshHardwarePttStatus()
            }
            .setPositiveButton(R.string.common_ok) { _, _ ->
                pttHardwareKeyStore.setBinding(pendingBinding)
                refreshHardwarePttStatus()
            }
            .setNegativeButton(R.string.roger_cancel, null)
            .create()
            .also { dialog ->
                dialog.setOnKeyListener { _, keyCode, event ->
                    if (event.action != KeyEvent.ACTION_DOWN) return@setOnKeyListener false
                    if (keyCode == KeyEvent.KEYCODE_BACK) return@setOnKeyListener false
                    pendingBinding = PttHardwareKeyStore.Binding(
                        keyCode = if (keyCode > KeyEvent.KEYCODE_UNKNOWN) keyCode else KeyEvent.KEYCODE_UNKNOWN,
                        scanCode = event.scanCode.coerceAtLeast(0),
                    )
                    valueText.text = bindingToDisplayName(pendingBinding)
                    true
                }
            }
            .show()
    }

    private fun keyCodeToDisplayName(keyCode: Int): String {
        val raw = KeyEvent.keyCodeToString(keyCode)
        return raw
            .removePrefix("KEYCODE_")
            .replace('_', ' ')
            .lowercase()
            .replaceFirstChar { if (it.isLowerCase()) it.titlecase() else it.toString() }
    }

    private fun bindingToDisplayName(binding: PttHardwareKeyStore.Binding): String {
        val keyLabel = when {
            binding.keyCode != KeyEvent.KEYCODE_UNKNOWN -> keyCodeToDisplayName(binding.keyCode)
            binding.scanCode > 0 -> getString(R.string.hardware_ptt_scan_only_format, binding.scanCode)
            else -> getString(R.string.hardware_ptt_unknown_key)
        }
        return keyLabel
    }

    private fun refreshMicrophoneOptions() {
        microphoneOptions.clear()
        microphoneOptions += microphoneConfigStore.getAvailableOptions()
        val titles = microphoneOptions.map { it.title }
        microphoneSpinner.adapter = ArrayAdapter(
            this,
            android.R.layout.simple_spinner_item,
            titles,
        ).also {
            it.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        }
        val selected = microphoneConfigStore.getSelectedOption().id
        val selectedIndex = microphoneOptions.indexOfFirst { it.id == selected }.coerceAtLeast(0)
        microphoneSpinner.setSelection(selectedIndex, false)
        microphoneSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: android.view.View?, position: Int, id: Long) {
                microphoneOptions.getOrNull(position)?.let {
                    microphoneConfigStore.setSelectedOption(it.id)
                    notifyWalkieServiceWarmMicSync()
                }
            }

            override fun onNothingSelected(parent: AdapterView<*>?) = Unit
        }
    }

    private fun sendBluetoothHeadsetModeToService(enabled: Boolean) {
        val intent = android.content.Intent(this, WalkieService::class.java).apply {
            action = WalkieService.ACTION_SET_BLUETOOTH_HEADSET_MODE
            putExtra(WalkieService.EXTRA_USE_BLUETOOTH_HEADSET, enabled)
        }
        startService(intent)
    }

    private fun sendWarmMicRecorderModeToService(enabled: Boolean) {
        val intent = android.content.Intent(this, WalkieService::class.java).apply {
            action = WalkieService.ACTION_SET_WARM_MIC_RECORDER
            putExtra(WalkieService.EXTRA_WARM_MIC_RECORDER_ENABLED, enabled)
        }
        startService(intent)
    }

    private fun notifyWalkieServiceWarmMicSync() {
        val intent = android.content.Intent(this, WalkieService::class.java).apply {
            action = WalkieService.ACTION_SYNC_WARM_MIC_RECORDER
        }
        startService(intent)
    }

    private fun refreshRogerPatterns() {
        rogerPatterns.clear()
        rogerPatterns += rogerPatternStore.getAllPatterns()
        val names = rogerPatterns.map { it.name }
        rogerSpinner.adapter = ArrayAdapter(
            this,
            android.R.layout.simple_spinner_item,
            names,
        ).also {
            it.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        }
        val selected = rogerPatternStore.getSelectedPattern().id
        val selectedIndex = rogerPatterns.indexOfFirst { it.id == selected }.coerceAtLeast(0)
        rogerSpinner.setSelection(selectedIndex, false)
        rogerSpinner.setOnItemSelectedListener(object : android.widget.AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: android.view.View?, position: Int, id: Long) {
                rogerPatterns.getOrNull(position)?.let { rogerPatternStore.setSelectedPattern(it.id) }
                updatePatternActionButtons()
            }

            override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
        })
        updatePatternActionButtons()
    }

    private fun refreshCallingPatterns() {
        callingPatterns.clear()
        callingPatterns += callingPatternStore.getAllPatterns()
        val names = callingPatterns.map { it.name }
        callingSpinner.adapter = ArrayAdapter(
            this,
            android.R.layout.simple_spinner_item,
            names,
        ).also {
            it.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        }
        val selected = callingPatternStore.getSelectedPattern().id
        val selectedIndex = callingPatterns.indexOfFirst { it.id == selected }.coerceAtLeast(0)
        callingSpinner.setSelection(selectedIndex, false)
        callingSpinner.setOnItemSelectedListener(object : android.widget.AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: android.view.View?, position: Int, id: Long) {
                callingPatterns.getOrNull(position)?.let { callingPatternStore.setSelectedPattern(it.id) }
                updatePatternActionButtons()
            }

            override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
        })
        updatePatternActionButtons()
    }

    private fun updatePatternActionButtons() {
        val selectedRoger = rogerPatterns.getOrNull(rogerSpinner.selectedItemPosition) ?: rogerPatternStore.getSelectedPattern()
        val selectedCalling = callingPatterns.getOrNull(callingSpinner.selectedItemPosition) ?: callingPatternStore.getSelectedPattern()
        editRogerButton.visibility = if (selectedRoger.builtIn) View.GONE else View.VISIBLE
        editCallingButton.visibility = if (selectedCalling.builtIn) View.GONE else View.VISIBLE
    }
}
