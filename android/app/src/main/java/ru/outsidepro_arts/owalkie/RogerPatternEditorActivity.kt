package ru.outsidepro_arts.owalkie

import android.content.Context
import android.content.Intent
import android.app.AlertDialog
import android.os.Bundle
import android.text.InputType
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.ListView
import androidx.activity.ComponentActivity
import ru.outsidepro_arts.owalkie.model.CallingPatternStore
import ru.outsidepro_arts.owalkie.model.RogerPatternStore
import ru.outsidepro_arts.owalkie.model.RogerPoint

class RogerPatternEditorActivity : ComponentActivity() {
    companion object {
        const val EXTRA_SIGNAL_KIND = "signalKind"
        const val SIGNAL_KIND_ROGER = "roger"
        const val SIGNAL_KIND_CALLING = "calling"

        fun intent(context: Context, signalKind: String): Intent =
            Intent(context, RogerPatternEditorActivity::class.java).apply {
                putExtra(EXTRA_SIGNAL_KIND, signalKind)
            }
    }

    private lateinit var signalNameInput: EditText
    private lateinit var pointsList: ListView
    private lateinit var addPointButton: Button
    private lateinit var saveButton: Button
    private lateinit var cancelButton: Button
    private lateinit var rogerPatternStore: RogerPatternStore
    private lateinit var callingPatternStore: CallingPatternStore
    private var signalKind: String = SIGNAL_KIND_ROGER
    private val points = mutableListOf<RogerPoint>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_roger_pattern_editor)
        signalKind = intent.getStringExtra(EXTRA_SIGNAL_KIND) ?: SIGNAL_KIND_ROGER
        title = getString(
            if (signalKind == SIGNAL_KIND_CALLING) {
                R.string.call_custom_title
            } else {
                R.string.roger_custom_title
            },
        )
        rogerPatternStore = RogerPatternStore(this)
        callingPatternStore = CallingPatternStore(this)

        signalNameInput = findViewById(R.id.signalNameInput)
        pointsList = findViewById(R.id.pointsListView)
        addPointButton = findViewById(R.id.addPointButton)
        saveButton = findViewById(R.id.savePatternButton)
        cancelButton = findViewById(R.id.cancelPatternButton)

        addPointButton.setOnClickListener { showAddPointDialog() }
        saveButton.setOnClickListener { savePattern() }
        cancelButton.setOnClickListener { finish() }
        refreshPointsList()
    }

    private fun showAddPointDialog() {
        val freqInput = EditText(this).apply {
            hint = getString(R.string.roger_point_frequency_hint)
            inputType = InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_DECIMAL
        }
        val durationInput = EditText(this).apply {
            hint = getString(R.string.roger_point_duration_hint)
            inputType = InputType.TYPE_CLASS_NUMBER
        }
        val container = android.widget.LinearLayout(this).apply {
            orientation = android.widget.LinearLayout.VERTICAL
            setPadding(40, 20, 40, 0)
            addView(freqInput)
            addView(durationInput)
        }

        AlertDialog.Builder(this)
            .setTitle(R.string.roger_new_point_title)
            .setView(container)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                val freq = freqInput.text?.toString()?.trim()?.toDoubleOrNull()
                val duration = durationInput.text?.toString()?.trim()?.toIntOrNull()
                if (freq == null || duration == null || freq <= 0.0 || duration <= 0) {
                    return@setPositiveButton
                }
                points += RogerPoint(freqHz = freq, durationMs = duration)
                refreshPointsList()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun refreshPointsList() {
        val items = points.mapIndexed { index, point ->
            "${index + 1}. ${point.freqHz.toInt()} Hz / ${point.durationMs} ms"
        }
        pointsList.adapter = ArrayAdapter(
            this,
            android.R.layout.simple_list_item_1,
            items.ifEmpty { listOf(getString(R.string.roger_points_empty)) },
        )
    }

    private fun savePattern() {
        val name = signalNameInput.text?.toString()?.trim().orEmpty()
        if (name.isBlank()) {
            signalNameInput.error = getString(R.string.roger_name_required)
            signalNameInput.requestFocus()
            return
        }
        if (points.isEmpty()) {
            signalNameInput.error = getString(R.string.roger_points_required)
            return
        }
        if (signalKind == SIGNAL_KIND_CALLING) {
            callingPatternStore.saveCustomPattern(name, points.toList())
        } else {
            rogerPatternStore.saveCustomPattern(name, points.toList())
        }
        setResult(RESULT_OK)
        finish()
    }
}
