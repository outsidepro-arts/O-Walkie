package com.owalkie.app

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
import android.view.Menu
import android.view.MenuItem
import android.view.MotionEvent
import android.widget.AdapterView
import android.widget.ArrayAdapter
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat.RECEIVER_NOT_EXPORTED
import androidx.core.content.ContextCompat
import com.owalkie.app.databinding.ActivityMainBinding
import com.owalkie.app.model.ServerProfile
import com.owalkie.app.model.ServerStore

class MainActivity : ComponentActivity() {
    companion object {
        const val ACTION_OPEN_BATTERY_SETTINGS = "com.owalkie.app.action.OPEN_BATTERY_SETTINGS"
    }

    private lateinit var binding: ActivityMainBinding
    private lateinit var serverStore: ServerStore
    private lateinit var uiSignalPlayer: UiSignalPlayer
    private var transmitting = false
    private var selectedServerIndex = 0
    private val servers = mutableListOf<ServerProfile>()
    private var wsConnected = false
    private var wsConnecting = false
    private var receiverRegistered = false
    private var repeaterModeEnabled = false

    private val statusReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent?.action != WalkieService.ACTION_STATUS) return
            val signal = intent.getIntExtra(WalkieService.EXTRA_SIGNAL, 0).coerceIn(0, 255)
            val prevConnected = wsConnected
            val prevConnecting = wsConnecting
            wsConnected = intent.getBooleanExtra(WalkieService.EXTRA_WS_CONNECTED, false)
            wsConnecting = intent.getBooleanExtra(WalkieService.EXTRA_WS_CONNECTING, false)
            val udpReady = intent.getBooleanExtra(WalkieService.EXTRA_UDP_READY, false)
            val signalPercent = ((signal / 255.0) * 100.0).toInt().coerceIn(0, 100)

            if (!prevConnected && wsConnected) {
                uiSignalPlayer.playConnected()
            } else if (prevConnecting && !wsConnecting && !wsConnected) {
                uiSignalPlayer.playConnectionError()
            }

            binding.signalBar.max = 100
            binding.signalBar.progress = signalPercent
            binding.signalText.text = getString(R.string.signal_quality_format_percent, signalPercent)
            binding.signalBar.contentDescription = getString(R.string.signal_quality_format_percent, signalPercent)
            val netStatus = getString(
                R.string.net_status_format,
                if (wsConnected) getString(R.string.net_state_connected) else if (wsConnecting) getString(R.string.net_state_reconnect) else getString(R.string.net_state_disconnected),
                if (udpReady) getString(R.string.net_state_ready) else getString(R.string.net_state_reinit),
            )
            binding.netText.text = netStatus
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
        serverStore = ServerStore(this)
        uiSignalPlayer = UiSignalPlayer(this)

        initServerProfilesUi()
        requestRuntimePermissions()
        updateBatteryOptimizationUi()
        if (intent?.action == ACTION_OPEN_BATTERY_SETTINGS) {
            openBatteryOptimizationSettings()
        }

        binding.pttButton.setOnTouchListener { _, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    startTransmitUi()
                    true
                }

                MotionEvent.ACTION_UP,
                MotionEvent.ACTION_CANCEL -> {
                    stopTransmitUi()
                    true
                }

                else -> false
            }
        }

        binding.callButton.setOnClickListener {
            if (!wsConnected) return@setOnClickListener
            sendServiceAction(WalkieService.ACTION_CALL_SIGNAL)
        }

        updateConnectButtonLabel()
        updatePttAvailability()
    }

    override fun onStart() {
        super.onStart()
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
        updateBatteryOptimizationUi()
    }

    override fun onStop() {
        super.onStop()
        if (receiverRegistered) {
            unregisterReceiver(statusReceiver)
            receiverRegistered = false
        }
    }

    override fun onDestroy() {
        super.onDestroy()
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

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        menuInflater.inflate(R.menu.main_overflow_menu, menu)
        return true
    }

    override fun onPrepareOptionsMenu(menu: Menu): Boolean {
        menu.findItem(R.id.action_repeater_mode)?.isChecked = repeaterModeEnabled
        menu.findItem(R.id.action_background_mode)?.title = getString(
            R.string.menu_background_mode_format,
            getString(
                if (isBackgroundModeActive()) {
                    R.string.menu_background_status_active
                } else {
                    R.string.menu_background_status_inactive
                },
            ),
        )
        return super.onPrepareOptionsMenu(menu)
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        return when (item.itemId) {
            R.id.action_repeater_mode -> {
                repeaterModeEnabled = !item.isChecked
                item.isChecked = repeaterModeEnabled
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

            else -> super.onOptionsItemSelected(item)
        }
    }

    private fun startTransmitUi() {
        if (transmitting) return
        if (!wsConnected) return
        transmitting = true
        binding.statusText.text = getString(R.string.status_tx)
        uiSignalPlayer.playPttPress()
        sendServiceAction(WalkieService.ACTION_PTT_PRESS)
        binding.callButton.isEnabled = false
        updatePttLabel()
    }

    private fun stopTransmitUi() {
        if (!transmitting) return
        transmitting = false
        binding.statusText.text = getString(R.string.status_idle)
        sendServiceAction(WalkieService.ACTION_PTT_RELEASE)
        binding.callButton.isEnabled = wsConnected
        updatePttLabel()
    }

    private fun updatePttLabel() {
        if (!binding.pttButton.isEnabled) {
            binding.pttButton.text = getString(R.string.ptt_unavailable)
            binding.pttButton.contentDescription = getString(R.string.ptt_unavailable)
            return
        }
        binding.pttButton.text = getString(R.string.ptt_hold)
        binding.pttButton.contentDescription = getString(R.string.ptt_hold_accessibility_hint)
    }

    private fun startWalkieService(profile: ServerProfile) {
        val intent = Intent(this, WalkieService::class.java).apply {
            action = WalkieService.ACTION_START
            putExtra(WalkieService.EXTRA_SERVER_HOST, profile.host)
            putExtra(WalkieService.EXTRA_WS_PORT, profile.wsPort)
            putExtra(WalkieService.EXTRA_UDP_PORT, profile.udpPort)
            putExtra(WalkieService.EXTRA_CHANNEL, profile.channel)
            putExtra(WalkieService.EXTRA_REPEATER_ENABLED, repeaterModeEnabled)
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

    private fun sendRepeaterModeAction(enabled: Boolean) {
        val intent = Intent(this, WalkieService::class.java).apply {
            action = WalkieService.ACTION_SET_REPEATER
            putExtra(WalkieService.EXTRA_REPEATER_ENABLED, enabled)
        }
        startService(intent)
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
        invalidateOptionsMenu()
    }

    private fun hasAudioPermission(): Boolean {
        return ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED
    }

    private fun initServerProfilesUi() {
        servers.clear()
        servers.addAll(serverStore.load())
        if (servers.isEmpty()) {
            servers += ServerProfile(
                name = getString(R.string.default_server_name),
                host = "192.168.100.2",
                wsPort = 5500,
                udpPort = 5505,
                channel = "global",
            )
            serverStore.save(servers)
        } else if (servers.size == 1 && servers.first().host == "10.0.2.2" && servers.first().wsPort == 8080 && servers.first().udpPort == 5000) {
            servers[0] = servers.first().copy(
                host = "192.168.100.2",
                wsPort = 5500,
                udpPort = 5505,
            )
            serverStore.save(servers)
        }
        refreshServerSpinner()
        bindServerButtons()
        loadServerToInputs(servers.first())
    }

    private fun refreshServerSpinner() {
        val names = servers.map { it.name }
        val adapter = ArrayAdapter(this, android.R.layout.simple_spinner_item, names)
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        binding.serverSpinner.adapter = adapter
        binding.serverSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: android.view.View?, position: Int, id: Long) {
                selectedServerIndex = position
                loadServerToInputs(servers[position])
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
            binding.serverSpinner.setSelection(selectedServerIndex)
            binding.root.announceForAccessibility(getString(R.string.saved_server_announcement))
        }

        binding.deleteServerButton.setOnClickListener {
            if (servers.size <= 1) return@setOnClickListener
            val idx = binding.serverSpinner.selectedItemPosition.coerceIn(0, servers.lastIndex)
            servers.removeAt(idx)
            selectedServerIndex = (idx - 1).coerceAtLeast(0)
            serverStore.save(servers)
            refreshServerSpinner()
            binding.serverSpinner.setSelection(selectedServerIndex)
            loadServerToInputs(servers[selectedServerIndex])
            binding.root.announceForAccessibility(getString(R.string.deleted_server_announcement))
        }

        binding.connectServerButton.setOnClickListener {
            if (wsConnecting) {
                sendServiceAction(WalkieService.ACTION_CANCEL_CONNECT)
                wsConnecting = false
                wsConnected = false
                updateConnectButtonLabel()
                updatePttAvailability()
                return@setOnClickListener
            }
            val profile = collectServerFromInputs() ?: return@setOnClickListener
            if (!hasAudioPermission()) {
                requestRuntimePermissions()
                return@setOnClickListener
            }
            startWalkieService(profile)
            wsConnecting = true
            wsConnected = false
            updateConnectButtonLabel()
            updatePttAvailability()
            binding.root.announceForAccessibility(getString(R.string.connected_server_announcement))
        }
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
        when {
            wsConnecting -> binding.connectServerButton.text = getString(R.string.connect_cancel)
            wsConnected -> binding.connectServerButton.text = getString(R.string.connect_reconnect)
            else -> binding.connectServerButton.text = getString(R.string.connect_server)
        }
    }

    private fun updatePttAvailability() {
        val enabled = wsConnected
        binding.pttButton.isEnabled = enabled
        binding.callButton.isEnabled = enabled && !transmitting
        binding.pttButton.alpha = if (enabled) 1.0f else 0.5f
        binding.callButton.alpha = if (enabled) 1.0f else 0.5f
        if (!enabled && transmitting) {
            stopTransmitUi()
        }
        updatePttLabel()
    }
}

