package com.owalkie.app

import android.Manifest
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.view.accessibility.AccessibilityManager
import android.os.Build
import android.os.Bundle
import android.view.MotionEvent
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat.RECEIVER_NOT_EXPORTED
import androidx.core.content.ContextCompat.registerReceiver
import androidx.core.content.ContextCompat
import android.widget.AdapterView
import android.widget.ArrayAdapter
import com.owalkie.app.databinding.ActivityMainBinding
import com.owalkie.app.model.ServerProfile
import com.owalkie.app.model.ServerStore

class MainActivity : ComponentActivity() {
    private lateinit var binding: ActivityMainBinding
    private lateinit var accessibilityManager: AccessibilityManager
    private lateinit var serverStore: ServerStore
    private var transmitting = false
    private var accessibilityToggleMode = false
    private var lastNetStatusText: String = ""
    private var selectedServerIndex = 0
    private val servers = mutableListOf<ServerProfile>()

    private val statusReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent?.action != WalkieService.ACTION_STATUS) return
            val signal = intent.getIntExtra(WalkieService.EXTRA_SIGNAL, 0).coerceIn(0, 255)
            val wsConnected = intent.getBooleanExtra(WalkieService.EXTRA_WS_CONNECTED, false)
            val udpReady = intent.getBooleanExtra(WalkieService.EXTRA_UDP_READY, false)

            binding.signalBar.max = 255
            binding.signalBar.progress = signal
            binding.signalText.text = getString(R.string.signal_quality_format, signal)
            binding.signalBar.contentDescription = getString(R.string.signal_quality_format, signal)
            val netStatus = getString(
                R.string.net_status_format,
                if (wsConnected) getString(R.string.net_state_connected) else getString(R.string.net_state_reconnect),
                if (udpReady) getString(R.string.net_state_ready) else getString(R.string.net_state_reinit),
            )
            binding.netText.text = netStatus
            if (lastNetStatusText != netStatus) {
                lastNetStatusText = netStatus
                binding.netText.announceForAccessibility(netStatus)
            }
        }
    }

    private val permissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {}

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
        accessibilityManager = getSystemService(ACCESSIBILITY_SERVICE) as AccessibilityManager
        serverStore = ServerStore(this)

        requestRuntimePermissions()
        initServerProfilesUi()
        refreshAccessibilityMode()

        binding.pttButton.setOnTouchListener { _, event ->
            if (accessibilityToggleMode) {
                // In TalkBack touch-exploration mode, tap-to-toggle is more reliable than hold-to-talk.
                return@setOnTouchListener false
            }
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

        binding.pttButton.setOnClickListener {
            if (!accessibilityToggleMode) return@setOnClickListener
            if (transmitting) {
                stopTransmitUi()
            } else {
                startTransmitUi()
            }
        }
    }

    override fun onStart() {
        super.onStart()
        refreshAccessibilityMode()
        registerReceiver(
            this,
            statusReceiver,
            IntentFilter(WalkieService.ACTION_STATUS),
            RECEIVER_NOT_EXPORTED,
        )
    }

    override fun onStop() {
        super.onStop()
        unregisterReceiver(statusReceiver)
    }

    private fun refreshAccessibilityMode() {
        accessibilityToggleMode =
            accessibilityManager.isEnabled && accessibilityManager.isTouchExplorationEnabled
        updatePttLabel()
    }

    private fun startTransmitUi() {
        if (transmitting) return
        transmitting = true
        binding.statusText.text = getString(R.string.status_tx)
        sendServiceAction(WalkieService.ACTION_PTT_PRESS)
        binding.statusText.announceForAccessibility(binding.statusText.text)
        updatePttLabel()
    }

    private fun stopTransmitUi() {
        if (!transmitting) return
        transmitting = false
        binding.statusText.text = getString(R.string.status_idle)
        sendServiceAction(WalkieService.ACTION_PTT_RELEASE)
        binding.statusText.announceForAccessibility(binding.statusText.text)
        updatePttLabel()
    }

    private fun updatePttLabel() {
        if (!accessibilityToggleMode) {
            binding.pttButton.text = getString(R.string.ptt_hold)
            binding.pttButton.contentDescription = getString(R.string.ptt_hold_accessibility_hint)
            return
        }
        if (transmitting) {
            binding.pttButton.text = getString(R.string.ptt_stop_talking)
            binding.pttButton.contentDescription = getString(R.string.ptt_stop_talking)
        } else {
            binding.pttButton.text = getString(R.string.ptt_start_talking)
            binding.pttButton.contentDescription = getString(R.string.ptt_start_talking)
        }
    }

    private fun startWalkieService(profile: ServerProfile) {
        val intent = Intent(this, WalkieService::class.java).apply {
            action = WalkieService.ACTION_START
            putExtra(WalkieService.EXTRA_SERVER_HOST, profile.host)
            putExtra(WalkieService.EXTRA_WS_PORT, profile.wsPort)
            putExtra(WalkieService.EXTRA_UDP_PORT, profile.udpPort)
            putExtra(WalkieService.EXTRA_CHANNEL, profile.channel)
        }
        ContextCompat.startForegroundService(this, intent)
    }

    private fun sendServiceAction(action: String) {
        val intent = Intent(this, WalkieService::class.java).apply {
            this.action = action
        }
        startService(intent)
    }

    private fun requestRuntimePermissions() {
        val needed = buildList {
            add(Manifest.permission.RECORD_AUDIO)
            add(Manifest.permission.READ_PHONE_STATE)
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

    private fun initServerProfilesUi() {
        servers.clear()
        servers.addAll(serverStore.load())
        if (servers.isEmpty()) {
            servers += ServerProfile(
                name = getString(R.string.default_server_name),
                host = "10.0.2.2",
                wsPort = 8080,
                udpPort = 5000,
                channel = "global",
            )
            serverStore.save(servers)
        }
        refreshServerSpinner()
        bindServerButtons()
        loadServerToInputs(servers.first())
        startWalkieService(servers.first())
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
            val profile = collectServerFromInputs() ?: return@setOnClickListener
            startWalkieService(profile)
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
}

