package ru.outsidepro_arts.owalkie

import android.app.AlertDialog
import android.content.ComponentName
import android.net.Uri
import android.provider.Settings
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
import ru.outsidepro_arts.owalkie.model.BluetoothHeadsetRouteStore
import ru.outsidepro_arts.owalkie.model.MicrophoneConfigStore
import ru.outsidepro_arts.owalkie.model.PttHardwareKeyStore
import ru.outsidepro_arts.owalkie.model.RogerPattern
import ru.outsidepro_arts.owalkie.model.RogerPatternStore

class SettingsActivity : ComponentActivity() {
    private lateinit var rogerPatternStore: RogerPatternStore
    private lateinit var callingPatternStore: CallingPatternStore
    private lateinit var microphoneConfigStore: MicrophoneConfigStore
    private lateinit var bluetoothHeadsetRouteStore: BluetoothHeadsetRouteStore
    private lateinit var pttHardwareKeyStore: PttHardwareKeyStore
    private lateinit var hardwarePttRow: View
    private lateinit var hardwarePttStatusText: TextView
    private lateinit var microphoneSpinner: Spinner
    private lateinit var useBluetoothHeadsetCheckBox: CheckBox
    private lateinit var rogerSpinner: Spinner
    private lateinit var callingSpinner: Spinner
    private lateinit var customRogerButton: Button
    private lateinit var customCallingButton: Button
    private lateinit var playRogerButton: Button
    private lateinit var playCallingButton: Button
    private lateinit var deleteRogerButton: Button
    private lateinit var deleteCallingButton: Button
    private val microphoneOptions = mutableListOf<MicrophoneConfigStore.MicrophoneOption>()
    private val rogerPatterns = mutableListOf<RogerPattern>()
    private val callingPatterns = mutableListOf<RogerPattern>()

    private val customPatternEditorLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
            refreshPatterns()
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)
        title = getString(R.string.menu_settings)
        rogerPatternStore = RogerPatternStore(this)
        callingPatternStore = CallingPatternStore(this)
        microphoneConfigStore = MicrophoneConfigStore(this)
        bluetoothHeadsetRouteStore = BluetoothHeadsetRouteStore(this)
        pttHardwareKeyStore = PttHardwareKeyStore(this)

        hardwarePttRow = findViewById(R.id.hardwarePttRow)
        hardwarePttStatusText = findViewById(R.id.hardwarePttStatusText)
        microphoneSpinner = findViewById(R.id.microphoneSpinner)
        useBluetoothHeadsetCheckBox = findViewById(R.id.useBluetoothHeadsetCheckBox)
        rogerSpinner = findViewById(R.id.rogerPatternSpinner)
        callingSpinner = findViewById(R.id.callingPatternSpinner)
        customRogerButton = findViewById(R.id.customRogerButton)
        customCallingButton = findViewById(R.id.customCallingButton)
        playRogerButton = findViewById(R.id.playRogerButton)
        playCallingButton = findViewById(R.id.playCallingButton)
        deleteRogerButton = findViewById(R.id.deleteRogerButton)
        deleteCallingButton = findViewById(R.id.deleteCallingButton)

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
            SignalPreviewPlayer.playPattern(pattern.points)
        }
        deleteRogerButton.setOnClickListener { confirmDeleteSelectedRogerPattern() }
        deleteCallingButton.setOnClickListener { confirmDeleteSelectedCallingPattern() }
        hardwarePttRow.setOnClickListener {
            showHardwarePttAssignmentDialog()
        }
        useBluetoothHeadsetCheckBox.setOnCheckedChangeListener { _, isChecked ->
            bluetoothHeadsetRouteStore.setEnabled(isChecked)
            sendBluetoothHeadsetModeToService(isChecked)
        }

        refreshPatterns()
    }

    override fun onResume() {
        super.onResume()
        refreshPatterns()
    }

    private fun refreshPatterns() {
        refreshHardwarePttStatus()
        refreshMicrophoneOptions()
        refreshBluetoothHeadsetToggle()
        refreshRogerPatterns()
        refreshCallingPatterns()
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

    private fun refreshHardwarePttStatus() {
        val binding = pttHardwareKeyStore.getBinding()
        hardwarePttStatusText.text = if (!binding.isAssigned()) {
            getString(R.string.hardware_ptt_status_unassigned)
        } else {
            getString(R.string.hardware_ptt_status_assigned_format, bindingToDisplayName(binding))
        }
    }

    private fun showHardwarePttAssignmentDialog() {
        val dialogView = layoutInflater.inflate(R.layout.dialog_ptt_hardware_key, null)
        val valueText = dialogView.findViewById<TextView>(R.id.pttKeyDialogValueText)
        val backgroundCheckBox = dialogView.findViewById<CheckBox>(R.id.pttKeyDialogBackgroundCheckBox)
        var pendingBinding = pttHardwareKeyStore.getBinding()
        valueText.text = if (!pendingBinding.isAssigned()) {
            getString(R.string.hardware_ptt_dialog_waiting)
        } else {
            bindingToDisplayName(pendingBinding)
        }
        backgroundCheckBox.isChecked = pendingBinding.handleInBackground
        AlertDialog.Builder(this)
            .setTitle(R.string.hardware_ptt_dialog_title)
            .setView(dialogView)
            .setNeutralButton(R.string.common_reset) { _, _ ->
                pttHardwareKeyStore.clearBinding()
                refreshHardwarePttStatus()
            }
            .setPositiveButton(R.string.common_ok) { _, _ ->
                val bindingToSave = pendingBinding.copy(handleInBackground = backgroundCheckBox.isChecked)
                pttHardwareKeyStore.setBinding(bindingToSave)
                refreshHardwarePttStatus()
                if (bindingToSave.handleInBackground && !isPttAccessibilityEnabled()) {
                    showAccessibilityRequiredDialog()
                }
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
                        handleInBackground = backgroundCheckBox.isChecked,
                    )
                    valueText.text = bindingToDisplayName(pendingBinding)
                    true
                }
            }
            .show()
    }

    private fun isPttAccessibilityEnabled(): Boolean {
        val enabledServices = Settings.Secure.getString(contentResolver, Settings.Secure.ENABLED_ACCESSIBILITY_SERVICES)
            ?: return false
        val myService = ComponentName(this, PttAccessibilityService::class.java).flattenToString()
        return enabledServices.split(':').any { it.equals(myService, ignoreCase = true) }
    }

    private fun showAccessibilityRequiredDialog() {
        AlertDialog.Builder(this)
            .setMessage(R.string.hardware_ptt_accessibility_required_question)
            .setPositiveButton(R.string.hardware_ptt_open_accessibility_settings) { _, _ ->
                openAccessibilityServiceSettings()
            }
            .setNegativeButton(R.string.roger_cancel, null)
            .show()
    }

    private fun openAccessibilityServiceSettings() {
        val component = ComponentName(this, PttAccessibilityService::class.java).flattenToString()
        val detailsIntent = Intent(Settings.ACTION_ACCESSIBILITY_DETAILS_SETTINGS).apply {
            putExtra(Intent.EXTRA_COMPONENT_NAME, component)
            putExtra("android.provider.extra.ACCESSIBILITY_SERVICE_COMPONENT_NAME", component)
            data = Uri.parse("package:$packageName")
        }
        val opened = runCatching {
            startActivity(detailsIntent)
            true
        }.getOrElse { false }
        if (!opened) {
            runCatching { startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS)) }
        }
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
        return if (binding.handleInBackground) {
            "$keyLabel (${getString(R.string.hardware_ptt_background_suffix)})"
        } else {
            keyLabel
        }
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
                microphoneOptions.getOrNull(position)?.let { microphoneConfigStore.setSelectedOption(it.id) }
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
        deleteRogerButton.visibility = if (selectedRoger.builtIn) View.GONE else View.VISIBLE
        deleteCallingButton.visibility = if (selectedCalling.builtIn) View.GONE else View.VISIBLE
    }

    private fun confirmDeleteSelectedRogerPattern() {
        val selected = rogerPatterns.getOrNull(rogerSpinner.selectedItemPosition) ?: return
        if (selected.builtIn) return
        AlertDialog.Builder(this)
            .setMessage(R.string.delete_signal_confirm)
            .setPositiveButton(R.string.common_ok) { _, _ ->
                rogerPatternStore.deleteCustomPattern(selected.id)
                refreshRogerPatterns()
            }
            .setNegativeButton(R.string.roger_cancel, null)
            .show()
    }

    private fun confirmDeleteSelectedCallingPattern() {
        val selected = callingPatterns.getOrNull(callingSpinner.selectedItemPosition) ?: return
        if (selected.builtIn) return
        AlertDialog.Builder(this)
            .setMessage(R.string.delete_signal_confirm)
            .setPositiveButton(R.string.common_ok) { _, _ ->
                callingPatternStore.deleteCustomPattern(selected.id)
                refreshCallingPatterns()
            }
            .setNegativeButton(R.string.roger_cancel, null)
            .show()
    }
}
