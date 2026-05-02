package ru.outsidepro_arts.owalkie

import android.Manifest
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.PowerManager
import android.provider.Settings
import android.view.KeyEvent
import android.view.MotionEvent
import android.widget.SeekBar
import android.widget.PopupMenu
import android.widget.AdapterView
import android.widget.ArrayAdapter
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat.RECEIVER_NOT_EXPORTED
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import org.json.JSONObject
import ru.outsidepro_arts.owalkie.databinding.ActivityMainBinding
import ru.outsidepro_arts.owalkie.model.PttHardwareKeyStore
import ru.outsidepro_arts.owalkie.model.BluetoothHeadsetRouteStore
import ru.outsidepro_arts.owalkie.model.RxVolumeStore
import ru.outsidepro_arts.owalkie.model.ServerProfile
import ru.outsidepro_arts.owalkie.model.ServerStore

class MainActivity : ComponentActivity() {
    companion object {
        const val ACTION_OPEN_BATTERY_SETTINGS = "ru.outsidepro_arts.owalkie.action.OPEN_BATTERY_SETTINGS"
        private const val SCAN_INTERVAL_MS = 10_000L
        private const val SCAN_QUERY_TIMEOUT_MS = 4_000L
    }

    private lateinit var binding: ActivityMainBinding
    private lateinit var serverStore: ServerStore
    private lateinit var pttHardwareKeyStore: PttHardwareKeyStore
    private lateinit var bluetoothHeadsetRouteStore: BluetoothHeadsetRouteStore
    private lateinit var rxVolumeStore: RxVolumeStore
    private lateinit var uiSignalPlayer: UiSignalPlayer
    private var transmitting = false
    private var callActive = false
    private var pttToggleModeEnabled = false
    private var selectedServerIndex = 0
    private val servers = mutableListOf<ServerProfile>()
    private var wsConnected = false
    private var wsConnecting = false
    private var udpReady = false
    private var receiverRegistered = false
    private var repeaterModeEnabled = false
    private var connectionDetailsExpanded = false
    private var lastSignalPercent = 0
    private var protocolIncompatible = false
    private var busyModeEnabled = false
    private var busyRxActive = false
    private var pttBurstPressBlocked = false
    private var rxActive = false
    private var userRequestedConnection = false
    private var suppressSpinnerReconnect = false
    private var skipNextConnectedTone = false
    /** Hold-to-talk: finger is down; release must send PTT_RELEASE even if STATUS has not arrived yet. */
    private var holdPttFingerDown = false
    private var scanJob: Job? = null
    private val scanScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private val scanClient = OkHttpClient.Builder().retryOnConnectionFailure(true).build()

    private val statusReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent?.action != WalkieService.ACTION_STATUS) return
            val signal = intent.getIntExtra(WalkieService.EXTRA_SIGNAL, 0).coerceIn(0, 255)
            val prevConnected = wsConnected
            val prevConnecting = wsConnecting
            wsConnected = intent.getBooleanExtra(WalkieService.EXTRA_WS_CONNECTED, false)
            wsConnecting = intent.getBooleanExtra(WalkieService.EXTRA_WS_CONNECTING, false)
            transmitting = intent.getBooleanExtra(WalkieService.EXTRA_TX_ACTIVE, transmitting)
            callActive = intent.getBooleanExtra(WalkieService.EXTRA_CALL_ACTIVE, callActive)
            udpReady = intent.getBooleanExtra(WalkieService.EXTRA_UDP_READY, false)
            val prevProtocolIncompatible = protocolIncompatible
            protocolIncompatible = intent.getBooleanExtra(WalkieService.EXTRA_PROTOCOL_ERROR, false)
            busyModeEnabled = intent.getBooleanExtra(WalkieService.EXTRA_BUSY_MODE, false)
            busyRxActive = intent.getBooleanExtra(WalkieService.EXTRA_BUSY_RX_ACTIVE, false)
            pttBurstPressBlocked = intent.getBooleanExtra(WalkieService.EXTRA_PTT_BURST_PRESS_BLOCKED, false)
            rxActive = intent.getBooleanExtra(WalkieService.EXTRA_RX_ACTIVE, false)
            val signalPercent = ((signal / 255.0) * 100.0).toInt().coerceIn(0, 100)
            lastSignalPercent = signalPercent

            if (!prevConnected && wsConnected) {
                if (skipNextConnectedTone) {
                    skipNextConnectedTone = false
                } else {
                    uiSignalPlayer.playConnected()
                }
            } else if (!prevProtocolIncompatible && protocolIncompatible) {
                uiSignalPlayer.playConnectionError()
            } else if (userRequestedConnection && prevConnecting && !wsConnecting && !wsConnected) {
                uiSignalPlayer.playConnectionError()
            } else if (userRequestedConnection && prevConnected && !wsConnected) {
                uiSignalPlayer.playConnectionError()
            }

            updateStatusChips()
            updateConnectButtonLabel()
            updatePttAvailability()
        }
    }

    private val permissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
            // Connection is user-driven from the Connect button after permissions are granted.
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
        // Activity recreation on orientation change should not look like a fresh connect event.
        skipNextConnectedTone = savedInstanceState != null
        serverStore = ServerStore(this)
        pttHardwareKeyStore = PttHardwareKeyStore(this)
        bluetoothHeadsetRouteStore = BluetoothHeadsetRouteStore(this)
        rxVolumeStore = RxVolumeStore(this)
        uiSignalPlayer = UiSignalPlayer(this)

        initServerProfilesUi()
        requestRuntimePermissions()
        updateBatteryOptimizationUi()
        if (intent?.action == ACTION_OPEN_BATTERY_SETTINGS) {
            openBatteryOptimizationSettings()
        }

        binding.pttButton.setOnTouchListener { _, event ->
            if (pttToggleModeEnabled) {
                when (event.actionMasked) {
                    MotionEvent.ACTION_UP -> {
                        toggleTransmitUi()
                        true
                    }
                    MotionEvent.ACTION_DOWN, MotionEvent.ACTION_CANCEL -> true
                    else -> false
                }
            } else {
                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN -> {
                        holdPttFingerDown = true
                        startTransmitUi()
                        true
                    }

                    MotionEvent.ACTION_UP,
                    MotionEvent.ACTION_CANCEL -> {
                        endHoldPttGesture()
                        true
                    }

                    else -> false
                }
            }
        }
        binding.pttButton.setOnClickListener {
            if (pttToggleModeEnabled) {
                toggleTransmitUi()
            }
        }

        binding.callButton.setOnClickListener {
            if (!wsConnected) return@setOnClickListener
            sendServiceAction(WalkieService.ACTION_CALL_SIGNAL)
        }

        binding.toggleConnectionDetailsButton.setOnClickListener {
            connectionDetailsExpanded = !connectionDetailsExpanded
            updateConnectionDetailsUi()
        }

        binding.previousServerButton.setOnClickListener {
            moveSelectedServer(-1)
        }

        binding.nextServerButton.setOnClickListener {
            moveSelectedServer(1)
        }
        binding.moveServerUpButton.setOnClickListener {
            moveServerInList(-1)
        }
        binding.moveServerDownButton.setOnClickListener {
            moveServerInList(1)
        }

        binding.compactConnectButton.setOnClickListener {
            handleConnectAction()
        }
        binding.scanButton.setOnClickListener {
            toggleScanning()
        }
        binding.moreButton.setOnClickListener {
            showMoreMenu()
        }
        initRxVolumeUi()
        refreshPttToggleModeSetting()

        updateConnectionDetailsUi()
        updateConnectButtonLabel()
        updatePttAvailability()
        updateStatusChips()
        updateScanButtonLabel()
    }

    override fun onStart() {
        super.onStart()
        sendActivityFocusState(true)
        updateBatteryOptimizationUi()
        if (!receiverRegistered) {
            ContextCompat.registerReceiver(
                this,
                statusReceiver,
                IntentFilter(WalkieService.ACTION_STATUS),
                RECEIVER_NOT_EXPORTED,
            )
            receiverRegistered = true
        }
    }

    override fun onResume() {
        super.onResume()
        refreshPttToggleModeSetting()
        updateBatteryOptimizationUi()
    }

    override fun onStop() {
        super.onStop()
        // If touch sequence is interrupted (app backgrounded/system overlay), ACTION_UP may never arrive.
        // Force TX release to prevent stuck PTT state.
        if (holdPttFingerDown) {
            holdPttFingerDown = false
            sendServiceAction(WalkieService.ACTION_PTT_RELEASE)
            binding.callButton.isEnabled = wsConnected
            updatePttLabel()
            updateStatusChips()
        } else {
            stopTransmitUi()
        }
        // Orientation recreation is not a real background transition; avoid toggling
        // service focus state which can disturb active network/session timing.
        if (!isChangingConfigurations) {
            sendActivityFocusState(false)
        }
        if (receiverRegistered) {
            unregisterReceiver(statusReceiver)
            receiverRegistered = false
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        stopScanning(announce = false)
        scanScope.cancel()
        scanClient.dispatcher.executorService.shutdown()
        if (::uiSignalPlayer.isInitialized) {
            uiSignalPlayer.release()
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        if (intent.action == ACTION_OPEN_BATTERY_SETTINGS) {
            openBatteryOptimizationSettings()
        }
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (pttHardwareKeyStore.matches(event)) {
            when (event.action) {
                KeyEvent.ACTION_DOWN -> {
                    sendHardwarePttKeyEventToService(event)
                    return true
                }

                KeyEvent.ACTION_UP -> {
                    sendHardwarePttKeyEventToService(event)
                    return true
                }
            }
        }
        return super.dispatchKeyEvent(event)
    }

    private fun sendHardwarePttKeyEventToService(event: KeyEvent) {
        val intent = Intent(this, WalkieService::class.java).apply {
            action = WalkieService.ACTION_HARDWARE_PTT_KEY
            putExtra(WalkieService.EXTRA_HW_KEY_ACTION, event.action)
            putExtra(WalkieService.EXTRA_HW_KEY_REPEAT, event.repeatCount)
            putExtra(WalkieService.EXTRA_HW_KEY_CODE, event.keyCode)
            putExtra(WalkieService.EXTRA_HW_SCAN_CODE, event.scanCode)
        }
        startService(intent)
    }

    private fun handleMenuAction(itemId: Int): Boolean {
        return when (itemId) {
            R.id.action_repeater_mode -> {
                repeaterModeEnabled = !repeaterModeEnabled
                sendRepeaterModeAction(repeaterModeEnabled)
                true
            }

            R.id.action_background_mode -> {
                requestBatteryOptimizationExemption()
                true
            }

            R.id.action_settings -> {
                startActivity(Intent(this, SettingsActivity::class.java))
                true
            }

            R.id.action_exit -> {
                requestAppExit()
                true
            }

            else -> false
        }
    }

    private fun showMoreMenu() {
        val popup = PopupMenu(this, binding.moreButton)
        popup.menuInflater.inflate(R.menu.main_overflow_menu, popup.menu)
        popup.menu.findItem(R.id.action_repeater_mode)?.isChecked = repeaterModeEnabled
        popup.menu.findItem(R.id.action_background_mode)?.title = getString(
            R.string.menu_background_mode_format,
            getString(
                if (isBackgroundModeActive()) {
                    R.string.menu_background_status_active
                } else {
                    R.string.menu_background_status_inactive
                },
            ),
        )
        popup.setOnMenuItemClickListener { menuItem ->
            handleMenuAction(menuItem.itemId)
        }
        popup.show()
    }

    private fun startTransmitUi() {
        if (transmitting) return
        if (!wsConnected) return
        sendServiceAction(WalkieService.ACTION_PTT_PRESS)
        binding.callButton.isEnabled = false
        updatePttLabel()
        updateStatusChips()
    }

    private fun endHoldPttGesture() {
        if (!holdPttFingerDown) return
        holdPttFingerDown = false
        sendServiceAction(WalkieService.ACTION_PTT_RELEASE)
        binding.callButton.isEnabled = wsConnected
        updatePttLabel()
        updateStatusChips()
    }

    private fun toggleTransmitUi() {
        if (transmitting) {
            stopTransmitUi()
        } else {
            startTransmitUi()
        }
    }

    private fun stopTransmitUi() {
        if (!transmitting) return
        sendServiceAction(WalkieService.ACTION_PTT_RELEASE)
        binding.callButton.isEnabled = wsConnected
        updatePttLabel()
        updateStatusChips()
    }

    private fun updatePttLabel() {
        if (!binding.pttButton.isEnabled) {
            binding.pttButton.text = getString(R.string.ptt_unavailable)
            binding.pttButton.contentDescription = getString(R.string.ptt_unavailable)
            return
        }
        if (pttToggleModeEnabled) {
            binding.pttButton.text = getString(if (transmitting) R.string.ptt_stop_talking else R.string.ptt_start_talking)
            binding.pttButton.contentDescription = getString(R.string.ptt_toggle_accessibility_hint)
        } else {
            binding.pttButton.text = getString(R.string.ptt_hold)
            binding.pttButton.contentDescription = getString(R.string.ptt_hold_accessibility_hint)
        }
    }

    private fun refreshPttToggleModeSetting() {
        val enabled = pttHardwareKeyStore.isToggleModeEnabled()
        if (pttToggleModeEnabled == enabled) return
        pttToggleModeEnabled = enabled
        if (!pttToggleModeEnabled && transmitting) {
            stopTransmitUi()
        }
        updatePttLabel()
    }

    private fun startWalkieService(profile: ServerProfile) {
        val intent = Intent(this, WalkieService::class.java).apply {
            action = WalkieService.ACTION_START
            putExtra(WalkieService.EXTRA_SERVER_HOST, profile.host)
            putExtra(WalkieService.EXTRA_WS_PORT, profile.wsPort)
            putExtra(WalkieService.EXTRA_UDP_PORT, profile.udpPort)
            putExtra(WalkieService.EXTRA_CHANNEL, profile.channel)
            putExtra(WalkieService.EXTRA_REPEATER_ENABLED, repeaterModeEnabled)
            putExtra(WalkieService.EXTRA_RX_VOLUME_PERCENT, rxVolumeStore.getPercent())
            putExtra(WalkieService.EXTRA_USE_BLUETOOTH_HEADSET, bluetoothHeadsetRouteStore.isEnabled())
        }
        ContextCompat.startForegroundService(this, intent)
    }

    private fun sendServiceAction(action: String) {
        val intent = Intent(this, WalkieService::class.java).apply {
            this.action = action
        }
        if (action == WalkieService.ACTION_START) {
            ContextCompat.startForegroundService(this, intent)
        } else {
            startService(intent)
        }
    }

    private fun sendActivityFocusState(focused: Boolean) {
        val intent = Intent(this, WalkieService::class.java).apply {
            action = WalkieService.ACTION_SET_ACTIVITY_FOCUS
            putExtra(WalkieService.EXTRA_ACTIVITY_FOCUSED, focused)
        }
        startService(intent)
    }

    private fun sendRepeaterModeAction(enabled: Boolean) {
        val intent = Intent(this, WalkieService::class.java).apply {
            action = WalkieService.ACTION_SET_REPEATER
            putExtra(WalkieService.EXTRA_REPEATER_ENABLED, enabled)
        }
        startService(intent)
    }

    private fun sendRxVolumeAction(percent: Int) {
        if (!wsConnected && !wsConnecting) return
        val intent = Intent(this, WalkieService::class.java).apply {
            action = WalkieService.ACTION_SET_RX_VOLUME
            putExtra(WalkieService.EXTRA_RX_VOLUME_PERCENT, percent)
        }
        startService(intent)
    }

    private fun initRxVolumeUi() {
        val current = rxVolumeStore.getPercent()
        applyRxVolumeUi(current)
        binding.rxVolumeSeekBar.progress = current
        binding.rxVolumeSeekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                val safe = progress.coerceIn(RxVolumeStore.MIN_RX_VOLUME_PERCENT, RxVolumeStore.MAX_RX_VOLUME_PERCENT)
                applyRxVolumeUi(safe)
                if (fromUser) {
                    rxVolumeStore.setPercent(safe)
                    sendRxVolumeAction(safe)
                }
            }

            override fun onStartTrackingTouch(seekBar: SeekBar?) = Unit

            override fun onStopTrackingTouch(seekBar: SeekBar?) {
                val safe = (seekBar?.progress ?: RxVolumeStore.DEFAULT_RX_VOLUME_PERCENT)
                    .coerceIn(RxVolumeStore.MIN_RX_VOLUME_PERCENT, RxVolumeStore.MAX_RX_VOLUME_PERCENT)
                binding.rxVolumeSeekBar.announceForAccessibility(getString(R.string.rx_volume_accessibility, safe))
            }
        })
    }

    private fun applyRxVolumeUi(percent: Int) {
        binding.rxVolumeValueText.text = getString(R.string.rx_volume_value, percent)
        val description = getString(R.string.rx_volume_accessibility, percent)
        binding.rxVolumeSeekBar.contentDescription = description
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            binding.rxVolumeSeekBar.stateDescription = description
        }
    }

    private fun requestAppExit() {
        sendServiceAction(WalkieService.ACTION_EXIT_APP)
        finishAffinity()
    }

    private fun requestRuntimePermissions() {
        val needed = buildList {
            add(Manifest.permission.RECORD_AUDIO)
            add(Manifest.permission.READ_PHONE_STATE)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                add(Manifest.permission.BLUETOOTH_CONNECT)
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                add(Manifest.permission.POST_NOTIFICATIONS)
            }
        }.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (needed.isNotEmpty()) {
            permissionLauncher.launch(needed.toTypedArray())
        }
    }

    private fun requestBatteryOptimizationExemption() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) return
        if (isIgnoringBatteryOptimizations()) {
            openBatteryOptimizationSettings()
            return
        }
        val intent = Intent(
            Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
            Uri.parse("package:$packageName"),
        )
        runCatching { startActivity(intent) }
            .onFailure { openBatteryOptimizationSettings() }
    }

    private fun openBatteryOptimizationSettings() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) return
        runCatching {
            startActivity(Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS))
        }
    }

    private fun isIgnoringBatteryOptimizations(): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) return true
        val pm = getSystemService(Context.POWER_SERVICE) as? PowerManager ?: return false
        return pm.isIgnoringBatteryOptimizations(packageName)
    }

    private fun isBackgroundModeActive(): Boolean {
        return Build.VERSION.SDK_INT < Build.VERSION_CODES.M || isIgnoringBatteryOptimizations()
    }

    private fun updateBatteryOptimizationUi() {
        // Popup menu title is recalculated on each open.
    }

    private fun hasAudioPermission(): Boolean {
        return ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED
    }

    private fun initServerProfilesUi() {
        servers.clear()
        servers.addAll(serverStore.load())
        val lastSelectedName = serverStore.getLastSelectedName()
        selectedServerIndex = servers.indexOfFirst { it.name == lastSelectedName }.takeIf { it >= 0 } ?: 0
        refreshServerSpinner()
        bindServerButtons()
        if (servers.isEmpty()) {
            clearServerInputs()
            updateServerNavigationButtons()
        } else {
            applySelectedServerIndex(selectedServerIndex, announce = false)
        }
        updateServerNavigationButtons()
    }

    private fun refreshServerSpinner() {
        val names = servers.map { it.name }
        val adapter = ArrayAdapter(this, android.R.layout.simple_spinner_item, names)
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        binding.serverSpinner.adapter = adapter
        binding.serverSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: android.view.View?, position: Int, id: Long) {
                val previousIndex = selectedServerIndex
                applySelectedServerIndex(position, announce = false)
                if (!suppressSpinnerReconnect && position != previousIndex) {
                    reconnectToSelectedServerIfRequested()
                }
            }

            override fun onNothingSelected(parent: AdapterView<*>?) = Unit
        }
    }

    private fun bindServerButtons() {
        binding.saveServerButton.setOnClickListener {
            val profile = collectServerFromInputs() ?: return@setOnClickListener
            val existing = servers.indexOfFirst { it.name.equals(profile.name, ignoreCase = true) }
            if (existing >= 0) {
                servers[existing] = profile
                selectedServerIndex = existing
            } else {
                servers += profile
                selectedServerIndex = servers.lastIndex
            }
            serverStore.save(servers)
            refreshServerSpinner()
            applySelectedServerIndex(selectedServerIndex, announce = false)
            binding.root.announceForAccessibility(getString(R.string.saved_server_announcement))
        }

        binding.deleteServerButton.setOnClickListener {
            if (servers.isEmpty()) return@setOnClickListener
            val idx = binding.serverSpinner.selectedItemPosition.coerceIn(0, servers.lastIndex)
            servers.removeAt(idx)
            selectedServerIndex = (idx - 1).coerceAtLeast(0)
            serverStore.save(servers)
            refreshServerSpinner()
            if (servers.isEmpty()) {
                clearServerInputs()
                serverStore.setLastSelectedName("")
                updateServerNavigationButtons()
            } else {
                applySelectedServerIndex(selectedServerIndex, announce = false)
            }
            binding.root.announceForAccessibility(getString(R.string.deleted_server_announcement))
        }

        binding.connectServerButton.setOnClickListener {
            handleConnectAction()
        }
    }

    private fun handleConnectAction() {
        if (wsConnecting || wsConnected) {
            userRequestedConnection = false
            uiSignalPlayer.playManualDisconnect()
            sendServiceAction(WalkieService.ACTION_DISCONNECT_AND_STOP)
            wsConnecting = false
            wsConnected = false
            protocolIncompatible = false
            updateConnectButtonLabel()
            updatePttAvailability()
            updateStatusChips()
            return
        }
        val profile = collectServerFromInputs() ?: return
        if (!hasAudioPermission()) {
            requestRuntimePermissions()
            return
        }
        userRequestedConnection = true
        uiSignalPlayer.playManualConnectStart()
        startWalkieService(profile)
        wsConnecting = true
        wsConnected = false
        protocolIncompatible = false
        updateConnectButtonLabel()
        updatePttAvailability()
        updateStatusChips()
        binding.root.announceForAccessibility(getString(R.string.connected_server_announcement))
    }

    private fun loadServerToInputs(profile: ServerProfile) {
        binding.serverNameInput.setText(profile.name)
        binding.serverHostInput.setText(profile.host)
        binding.wsPortInput.setText(profile.wsPort.toString())
        binding.udpPortInput.setText(profile.udpPort.toString())
        binding.channelInput.setText(profile.channel)
    }

    private fun collectServerFromInputs(): ServerProfile? {
        val name = binding.serverNameInput.text?.toString()?.trim().orEmpty()
        val host = binding.serverHostInput.text?.toString()?.trim().orEmpty()
        val wsPort = binding.wsPortInput.text?.toString()?.trim()?.toIntOrNull() ?: -1
        val udpPort = binding.udpPortInput.text?.toString()?.trim()?.toIntOrNull() ?: -1
        val channel = binding.channelInput.text?.toString()?.trim().orEmpty()

        when {
            name.isBlank() -> {
                binding.serverNameInput.error = getString(R.string.validation_server_name)
                binding.serverNameInput.requestFocus()
                return null
            }

            host.isBlank() -> {
                binding.serverHostInput.error = getString(R.string.validation_server_host)
                binding.serverHostInput.requestFocus()
                return null
            }

            channel.isBlank() -> {
                binding.channelInput.error = getString(R.string.validation_channel)
                binding.channelInput.requestFocus()
                return null
            }

            wsPort !in 1..65535 || udpPort !in 1..65535 -> {
                binding.wsPortInput.error = getString(R.string.validation_port)
                binding.udpPortInput.error = getString(R.string.validation_port)
                return null
            }
        }

        return ServerProfile(
            name = name,
            host = host,
            wsPort = wsPort,
            udpPort = udpPort,
            channel = channel,
        )
    }

    private fun updateConnectButtonLabel() {
        val labelRes = when {
            wsConnecting -> R.string.connect_cancel
            wsConnected -> R.string.connect_disconnect
            else -> R.string.connect_server
        }
        val label = getString(labelRes)
        binding.connectServerButton.text = label
        binding.compactConnectButton.text = label
    }

    private fun toggleScanning() {
        if (scanJob?.isActive == true) {
            stopScanning(announce = true)
        } else {
            startScanning()
        }
    }

    private fun startScanning() {
        if (scanJob?.isActive == true) return
        if (servers.isEmpty()) return
        scanJob = scanScope.launch {
            binding.root.announceForAccessibility(getString(R.string.scan_started_announcement))
            updateScanButtonLabel()
            while (isActive) {
                if (servers.isEmpty()) {
                    delay(SCAN_INTERVAL_MS)
                    continue
                }
                val snapshot = servers.toList()
                var foundIndex = -1
                var foundProfile: ServerProfile? = null
                for (profile in snapshot) {
                    val active = queryServerActivity(profile)
                    if (active) {
                        foundProfile = profile
                        foundIndex = servers.indexOfFirst { it.name == profile.name && it.host == profile.host && it.channel == profile.channel }
                        if (foundIndex < 0) {
                            foundIndex = servers.indexOf(profile)
                        }
                        break
                    }
                }
                if (foundProfile != null) {
                    val idx = foundIndex.coerceAtLeast(0).coerceAtMost((servers.size - 1).coerceAtLeast(0))
                    if (servers.isNotEmpty()) {
                        applySelectedServerIndex(idx, announce = true)
                    }
                    val profile = foundProfile
                    binding.root.announceForAccessibility(getString(R.string.scan_found_server_announcement, profile.name))
                    connectToProfileFromScan(profile)
                    stopScanning(announce = false)
                    break
                }
                delay(SCAN_INTERVAL_MS)
            }
            updateScanButtonLabel()
        }
    }

    private fun stopScanning(announce: Boolean) {
        scanJob?.cancel()
        scanJob = null
        updateScanButtonLabel()
        if (announce) {
            binding.root.announceForAccessibility(getString(R.string.scan_stopped_announcement))
        }
    }

    private fun updateScanButtonLabel() {
        val labelRes = if (scanJob?.isActive == true) R.string.scan_stop else R.string.scan_start
        binding.scanButton.text = getString(labelRes)
        updateStatusChips()
    }

    private suspend fun connectToProfileFromScan(profile: ServerProfile) {
        userRequestedConnection = true
        sendServiceAction(WalkieService.ACTION_CANCEL_CONNECT)
        wsConnecting = false
        wsConnected = false
        protocolIncompatible = false
        updateConnectButtonLabel()
        updatePttAvailability()
        updateStatusChips()
        delay(150L)
        startWalkieService(profile)
        wsConnecting = true
        wsConnected = false
        protocolIncompatible = false
        updateConnectButtonLabel()
        updatePttAvailability()
        updateStatusChips()
    }

    private suspend fun queryServerActivity(profile: ServerProfile): Boolean = withContext(Dispatchers.IO) {
        val wsUrl = buildWsUrlForScan(profile) ?: return@withContext false
        val deferred = CompletableDeferred<Boolean>()
        val request = runCatching { Request.Builder().url(wsUrl).build() }.getOrNull() ?: return@withContext false
        val ws = scanClient.newWebSocket(request, object : WebSocketListener() {
            override fun onMessage(webSocket: WebSocket, text: String) {
                val parseResult = runCatching {
                    val obj = JSONObject(text)
                    when (obj.optString("type")) {
                        "welcome" -> {
                            webSocket.send("""{"type":"has_activity","channel":"${profile.channel}"}""")
                            null
                        }
                        "has_activity" -> obj.optBoolean("active", false)
                        else -> null
                    }
                }.getOrNull()
                if (parseResult != null) {
                    if (!deferred.isCompleted) deferred.complete(parseResult)
                    webSocket.close(1000, "done")
                }
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                if (!deferred.isCompleted) deferred.complete(false)
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                if (!deferred.isCompleted) deferred.complete(false)
            }
        })
        val result = withTimeoutOrNull(SCAN_QUERY_TIMEOUT_MS) { deferred.await() } ?: false
        runCatching { ws.cancel() }
        return@withContext result
    }

    private fun buildWsUrlForScan(profile: ServerProfile): String? {
        val endpoint = parseServerEndpointForScan(profile.host, profile.wsPort) ?: return null
        val hostPart = if (endpoint.host.contains(':') && !endpoint.host.startsWith("[")) {
            "[${endpoint.host}]"
        } else {
            endpoint.host
        }
        val scheme = if (endpoint.secure) "wss" else "ws"
        return "$scheme://$hostPart:${endpoint.port}/ws"
    }

    private data class ParsedScanEndpoint(
        val host: String,
        val port: Int,
        val secure: Boolean,
    )

    private fun parseServerEndpointForScan(rawHost: String, fallbackWsPort: Int): ParsedScanEndpoint? {
        var value = rawHost.trim()
        if (value.isBlank()) return null
        var secure = false
        val lowered = value.lowercase()
        when {
            lowered.startsWith("wss://") -> {
                secure = true
                value = value.substring(6)
            }
            lowered.startsWith("https://") -> {
                secure = true
                value = value.substring(8)
            }
            lowered.startsWith("ws://") -> value = value.substring(5)
            lowered.startsWith("http://") -> value = value.substring(7)
        }
        value = value.substringBefore('/').substringBefore('?').substringBefore('#').trim()
        value = value.substringAfterLast('@')
        if (value.isBlank()) return null
        var port = fallbackWsPort
        var host = value
        if (host.startsWith("[")) {
            val closing = host.indexOf(']')
            if (closing <= 0) return null
            val ipv6 = host.substring(1, closing).trim()
            val rest = host.substring(closing + 1)
            if (rest.startsWith(":")) {
                val parsedPort = rest.substring(1).toIntOrNull() ?: return null
                port = parsedPort
            }
            host = ipv6
        } else {
            val colonCount = host.count { it == ':' }
            if (colonCount == 1) {
                val idx = host.lastIndexOf(':')
                val maybePort = host.substring(idx + 1).toIntOrNull()
                if (maybePort != null) {
                    port = maybePort
                    host = host.substring(0, idx)
                }
            }
        }
        if (host.isBlank() || port !in 1..65535) return null
        return ParsedScanEndpoint(host = host, port = port, secure = secure)
    }

    private fun updatePttAvailability() {
        val blockedByBusyMode = busyModeEnabled && busyRxActive && !transmitting
        val enabled = wsConnected && !blockedByBusyMode && !pttBurstPressBlocked
        binding.pttButton.isEnabled = enabled
        binding.callButton.isEnabled = enabled && !transmitting
        binding.pttButton.alpha = if (enabled) 1.0f else 0.5f
        binding.callButton.alpha = if (enabled) 1.0f else 0.5f
        if (!enabled && transmitting) {
            stopTransmitUi()
        }
        updatePttLabel()
        updateServerNavigationButtons()
    }

    private fun updateStatusChips() {
        val connectionState = when {
            protocolIncompatible -> getString(R.string.connection_state_protocol_incompatible)
            callActive -> getString(R.string.connection_state_calling)
            transmitting -> getString(R.string.connection_state_transmitting)
            wsConnected && rxActive -> getString(R.string.connection_state_receiving)
            scanJob?.isActive == true -> getString(R.string.connection_state_scanning)
            wsConnecting -> getString(R.string.connection_state_connecting)
            wsConnected && udpReady -> getString(R.string.connection_state_connected)
            wsConnected -> getString(R.string.connection_state_partial)
            else -> getString(R.string.connection_state_disconnected)
        }
        binding.connectionStateChip.text = connectionState
        binding.signalStateChip.text = getString(R.string.signal_quality_format_percent, lastSignalPercent)
    }

    private fun updateConnectionDetailsUi() {
        binding.connectionDetailsContainer.visibility = if (connectionDetailsExpanded) android.view.View.VISIBLE else android.view.View.GONE
        binding.collapsedActionsContainer.visibility = if (connectionDetailsExpanded) android.view.View.GONE else android.view.View.VISIBLE
        binding.toggleConnectionDetailsButton.text = getString(
            if (connectionDetailsExpanded) R.string.collapse_connection_details else R.string.expand_connection_details,
        )
        updateServerNavigationButtons()
    }

    private fun updateServerNavigationButtons() {
        val canNavigate = !connectionDetailsExpanded && servers.isNotEmpty()
        val hasPrevious = canNavigate && selectedServerIndex > 0
        val hasNext = canNavigate && selectedServerIndex < servers.lastIndex
        binding.previousServerButton.isEnabled = hasPrevious
        binding.nextServerButton.isEnabled = hasNext
        binding.previousServerButton.alpha = if (hasPrevious) 1.0f else 0.5f
        binding.nextServerButton.alpha = if (hasNext) 1.0f else 0.5f

        val canReorder = connectionDetailsExpanded && servers.isNotEmpty()
        val canMoveUp = canReorder && selectedServerIndex > 0
        val canMoveDown = canReorder && selectedServerIndex < servers.lastIndex
        binding.moveServerUpButton.isEnabled = canMoveUp
        binding.moveServerDownButton.isEnabled = canMoveDown
        binding.moveServerUpButton.alpha = if (canMoveUp) 1.0f else 0.5f
        binding.moveServerDownButton.alpha = if (canMoveDown) 1.0f else 0.5f
    }

    private fun moveSelectedServer(offset: Int) {
        if (servers.isEmpty()) return
        val targetIndex = (selectedServerIndex + offset).coerceIn(0, servers.lastIndex)
        if (targetIndex == selectedServerIndex) return
        applySelectedServerIndex(targetIndex, announce = true)
        uiSignalPlayer.playSwitch()
        reconnectToSelectedServerIfRequested()
    }

    private fun moveServerInList(offset: Int) {
        if (servers.isEmpty()) return
        val from = selectedServerIndex.coerceIn(0, servers.lastIndex)
        val to = (from + offset).coerceIn(0, servers.lastIndex)
        if (from == to) return
        val item = servers.removeAt(from)
        servers.add(to, item)
        selectedServerIndex = to
        serverStore.save(servers)
        refreshServerSpinner()
        applySelectedServerIndex(selectedServerIndex, announce = true)
        reconnectToSelectedServerIfRequested()
    }

    private fun reconnectToSelectedServerIfRequested() {
        if (!userRequestedConnection) return
        val profile = servers.getOrNull(selectedServerIndex) ?: return

        // Explicit reconnect flow: disconnect current session first, then start selected one.
        sendServiceAction(WalkieService.ACTION_CANCEL_CONNECT)
        wsConnecting = false
        wsConnected = false
        protocolIncompatible = false
        updateConnectButtonLabel()
        updatePttAvailability()
        updateStatusChips()

        binding.root.postDelayed({
            startWalkieService(profile)
            wsConnecting = true
            wsConnected = false
            protocolIncompatible = false
            updateConnectButtonLabel()
            updatePttAvailability()
            updateStatusChips()
        }, 150L)
    }

    private fun applySelectedServerIndex(index: Int, announce: Boolean) {
        if (servers.isEmpty()) return
        val safeIndex = index.coerceIn(0, servers.lastIndex)
        selectedServerIndex = safeIndex
        val profile = servers[safeIndex]
        if (binding.serverSpinner.selectedItemPosition != safeIndex) {
            suppressSpinnerReconnect = true
            binding.serverSpinner.setSelection(safeIndex, false)
            suppressSpinnerReconnect = false
        }
        loadServerToInputs(profile)
        serverStore.setLastSelectedName(profile.name)
        updateServerNavigationButtons()
        if (announce) {
            binding.root.announceForAccessibility(profile.name)
        }
    }

    private fun clearServerInputs() {
        binding.serverNameInput.setText("")
        binding.serverHostInput.setText("")
        binding.wsPortInput.setText("")
        binding.udpPortInput.setText("")
        binding.channelInput.setText("")
    }

}

