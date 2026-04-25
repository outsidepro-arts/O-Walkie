package com.owalkie.app

import android.os.Bundle
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.Spinner
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import com.owalkie.app.model.CallingPatternStore
import com.owalkie.app.model.RogerPattern
import com.owalkie.app.model.RogerPatternStore

class SettingsActivity : ComponentActivity() {
    private lateinit var rogerPatternStore: RogerPatternStore
    private lateinit var callingPatternStore: CallingPatternStore
    private lateinit var rogerSpinner: Spinner
    private lateinit var callingSpinner: Spinner
    private lateinit var customRogerButton: Button
    private lateinit var customCallingButton: Button
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

        rogerSpinner = findViewById(R.id.rogerPatternSpinner)
        callingSpinner = findViewById(R.id.callingPatternSpinner)
        customRogerButton = findViewById(R.id.customRogerButton)
        customCallingButton = findViewById(R.id.customCallingButton)

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

        refreshPatterns()
    }

    override fun onResume() {
        super.onResume()
        refreshPatterns()
    }

    private fun refreshPatterns() {
        refreshRogerPatterns()
        refreshCallingPatterns()
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
            }

            override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
        })
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
            }

            override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
        })
    }
}
