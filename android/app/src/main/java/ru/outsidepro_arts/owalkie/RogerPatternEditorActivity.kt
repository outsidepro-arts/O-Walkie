package ru.outsidepro_arts.owalkie

import android.content.Context
import android.content.Intent
import android.app.AlertDialog
import android.os.Bundle
import android.text.InputType
import android.widget.Button
import android.widget.EditText
import android.widget.ListView
import android.widget.TextView
import android.widget.Toast
import androidx.activity.ComponentActivity
import ru.outsidepro_arts.owalkie.model.CallingPatternStore
import ru.outsidepro_arts.owalkie.model.RogerPatternStore
import ru.outsidepro_arts.owalkie.model.RogerPoint

class RogerPatternEditorActivity : ComponentActivity() {
    companion object {
        private const val MAX_ROGER_DURATION_MS = 1000
        private const val MAX_CALLING_DURATION_MS = 5000
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
    private lateinit var playPatternButton: Button
    private lateinit var saveButton: Button
    private lateinit var cancelButton: Button
    private lateinit var repeatCountLabel: TextView
    private lateinit var repeatCountInput: EditText
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
        playPatternButton = findViewById(R.id.playPatternButton)
        saveButton = findViewById(R.id.savePatternButton)
        cancelButton = findViewById(R.id.cancelPatternButton)
        repeatCountLabel = findViewById(R.id.repeatCountLabel)
        repeatCountInput = findViewById(R.id.repeatCountInput)
        val isCalling = signalKind == SIGNAL_KIND_CALLING
        repeatCountLabel.visibility = if (isCalling) android.view.View.VISIBLE else android.view.View.GONE
        repeatCountInput.visibility = if (isCalling) android.view.View.VISIBLE else android.view.View.GONE
        if (isCalling) {
            repeatCountInput.setText("1")
        }

        addPointButton.setOnClickListener { showAddPointDialog() }
        playPatternButton.setOnClickListener {
            SignalPreviewPlayer.playPattern(buildSignalPointsForPlayback())
        }
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
                if (freq == null || duration == null || freq < 0.0 || duration <= 0) {
                    Toast.makeText(this, R.string.roger_point_invalid, Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                val repeatCount = resolveRepeatCountOrNull(showToastOnError = true) ?: return@setPositiveButton
                val totalDurationMs = (points.sumOf { it.durationMs } + duration) * repeatCount
                if (totalDurationMs > maxSignalDurationMs()) {
                    Toast.makeText(this, R.string.roger_points_total_too_long, Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                points += RogerPoint(freqHz = freq, durationMs = duration)
                refreshPointsList()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun refreshPointsList() {
        if (points.isEmpty()) {
            pointsList.adapter = object : android.widget.BaseAdapter() {
                override fun getCount(): Int = 1
                override fun getItem(position: Int): Any = getString(R.string.roger_points_empty)
                override fun getItemId(position: Int): Long = 0
                override fun getView(position: Int, convertView: android.view.View?, parent: android.view.ViewGroup): android.view.View {
                    val tv = (convertView as? TextView) ?: TextView(this@RogerPatternEditorActivity).apply {
                        setPadding(16, 16, 16, 16)
                    }
                    tv.text = getString(R.string.roger_points_empty)
                    return tv
                }
            }
            return
        }

        pointsList.adapter = object : android.widget.BaseAdapter() {
            override fun getCount(): Int = points.size
            override fun getItem(position: Int): Any = points[position]
            override fun getItemId(position: Int): Long = position.toLong()
            override fun getView(position: Int, convertView: android.view.View?, parent: android.view.ViewGroup): android.view.View {
                val row = convertView ?: layoutInflater.inflate(R.layout.item_signal_point, parent, false)
                val label = row.findViewById<TextView>(R.id.pointLabelText)
                val delete = row.findViewById<Button>(R.id.deletePointButton)
                val point = points[position]
                val toneLabel = if (point.freqHz <= 0.0) {
                    getString(R.string.roger_point_pause)
                } else {
                    getString(R.string.roger_point_hz_format, point.freqHz.toInt())
                }
                label.text = "${position + 1}. $toneLabel / ${point.durationMs} ms"
                delete.setOnClickListener {
                    if (position in points.indices) {
                        points.removeAt(position)
                        refreshPointsList()
                    }
                }
                return row
            }
        }
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
        val repeatCount = resolveRepeatCountOrNull(showToastOnError = false) ?: return
        val totalDurationMs = points.sumOf { it.durationMs } * repeatCount
        if (totalDurationMs > maxSignalDurationMs()) {
            signalNameInput.error = getString(R.string.roger_points_total_too_long)
            return
        }
        if (signalKind == SIGNAL_KIND_CALLING) {
            callingPatternStore.saveCustomPattern(name, buildRepeatedPoints(repeatCount))
        } else {
            rogerPatternStore.saveCustomPattern(name, points.toList())
        }
        setResult(RESULT_OK)
        finish()
    }

    private fun maxSignalDurationMs(): Int {
        return if (signalKind == SIGNAL_KIND_CALLING) MAX_CALLING_DURATION_MS else MAX_ROGER_DURATION_MS
    }

    private fun resolveRepeatCountOrNull(showToastOnError: Boolean): Int? {
        if (signalKind != SIGNAL_KIND_CALLING) return 1
        val repeats = repeatCountInput.text?.toString()?.trim()?.toIntOrNull()
        if (repeats == null || repeats <= 0) {
            repeatCountInput.error = getString(R.string.calling_repeat_count_invalid)
            if (showToastOnError) {
                Toast.makeText(this, R.string.calling_repeat_count_invalid, Toast.LENGTH_SHORT).show()
            }
            return null
        }
        return repeats
    }

    private fun buildRepeatedPoints(repeatCount: Int): List<RogerPoint> {
        if (repeatCount <= 1) return points.toList()
        val out = ArrayList<RogerPoint>(points.size * repeatCount)
        repeat(repeatCount) { out.addAll(points) }
        return out
    }

    private fun buildSignalPointsForPlayback(): List<RogerPoint> {
        val repeatCount = resolveRepeatCountOrNull(showToastOnError = true) ?: return points
        return buildRepeatedPoints(repeatCount)
    }
}
