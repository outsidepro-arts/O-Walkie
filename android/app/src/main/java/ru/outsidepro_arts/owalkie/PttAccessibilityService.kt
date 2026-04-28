package ru.outsidepro_arts.owalkie

import android.accessibilityservice.AccessibilityService
import android.accessibilityservice.AccessibilityServiceInfo
import android.view.KeyEvent
import android.view.accessibility.AccessibilityEvent
import android.content.Intent
import ru.outsidepro_arts.owalkie.model.PttHardwareKeyStore

class PttAccessibilityService : AccessibilityService() {
    private lateinit var keyStore: PttHardwareKeyStore

    override fun onCreate() {
        super.onCreate()
        keyStore = PttHardwareKeyStore(this)
    }

    override fun onServiceConnected() {
        super.onServiceConnected()
        val info = serviceInfo ?: return
        info.flags = info.flags or AccessibilityServiceInfo.FLAG_REQUEST_FILTER_KEY_EVENTS
        setServiceInfo(info)
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent?) = Unit

    override fun onInterrupt() = Unit

    override fun onKeyEvent(event: KeyEvent): Boolean {
        val binding = keyStore.getBinding()
        if (!binding.isAssigned() || !binding.handleInBackground) return false
        if (!keyStore.matches(event)) return false

        val serviceIntent = Intent(this, WalkieService::class.java).apply {
            action = WalkieService.ACTION_HARDWARE_PTT_KEY
            putExtra(WalkieService.EXTRA_HW_KEY_ACTION, event.action)
            putExtra(WalkieService.EXTRA_HW_KEY_REPEAT, event.repeatCount)
            putExtra(WalkieService.EXTRA_HW_KEY_CODE, event.keyCode)
            putExtra(WalkieService.EXTRA_HW_SCAN_CODE, event.scanCode)
            putExtra(WalkieService.EXTRA_HW_FROM_BACKGROUND, true)
        }
        startService(serviceIntent)
        return true
    }
}
