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
        private const val EXTRA_EDIT_PATTERN_ID = "editPatternId"
        const val SIGNAL_KIND_ROGER = "roger"
        const val SIGNAL_KIND_CALLING = "calling"

        fun intent(context: Context, signalKind: String): Intent =
            Intent(context, RogerPatternEditorActivity::class.java).apply {
                putExtra(EXTRA_SIGNAL_KIND, signalKind)
            }

        fun intentEdit(context: Context, signalKind: String, patternId: String): Intent =
            intent(context, signalKind).putExtra(EXTRA_EDIT_PATTERN_ID, patternId)
    }

    private lateinit var signalNameInput: EditText
    private lateinit var pointsList: ListView
    private lateinit var addPointButton: Button
    private lateinit var playPatternButton: Button
    private lateinit var deletePatternButton: Button
    private lateinit var saveButton: Button
    private lateinit var cancelButton: Button
    private lateinit var repeatCountLabel: TextView
    private lateinit var repeatCountInput: EditText
    private lateinit var rogerPatternStore: RogerPatternStore
    private lateinit var callingPatternStore: CallingPatternStore
    private var signalKind: String = SIGNAL_KIND_ROGER
    private var editPatternId: String? = null
    private val points = mutableListOf<RogerPoint>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_roger_pattern_editor)
        signalKind = intent.getStringExtra(EXTRA_SIGNAL_KIND) ?: SIGNAL_KIND_ROGER
        editPatternId = intent.getStringExtra(EXTRA_EDIT_PATTERN_ID)
        title = getString(
            when {
                editPatternId != null -> R.string.edit_signal_title
                signalKind == SIGNAL_KIND_CALLING -> R.string.call_custom_title
                else -> R.string.roger_custom_title
            },
        )
        rogerPatternStore = RogerPatternStore(this)
        callingPatternStore = CallingPatternStore(this)

        signalNameInput = findViewById(R.id.signalNameInput)
        pointsList = findViewById(R.id.pointsListView)
        pointsList.setItemsCanFocus(true)
        addPointButton = findViewById(R.id.addPointButton)
        playPatternButton = findViewById(R.id.playPatternButton)
        deletePatternButton = findViewById(R.id.deletePatternButton)
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

        applyEditModeIfNeeded()

        addPointButton.setOnClickListener { showSegmentDialog(editIndex = null) }
        playPatternButton.setOnClickListener {
            SignalPreviewPlayer.playPattern(buildSignalPointsForPlayback())
        }
        deletePatternButton.setOnClickListener { confirmDeleteEditedPattern() }
        saveButton.setOnClickListener { savePattern() }
        cancelButton.setOnClickListener { finish() }
        refreshPointsList()
    }

    private fun applyEditModeIfNeeded() {
        val id = editPatternId ?: run {
            deletePatternButton.visibility = android.view.View.GONE
            return
        }
        val pattern = when (signalKind) {
            SIGNAL_KIND_CALLING -> callingPatternStore.getAllPatterns().firstOrNull { it.id == id }
            else -> rogerPatternStore.getAllPatterns().firstOrNull { it.id == id }
        }
        if (pattern == null || pattern.builtIn) {
            editPatternId = null
            deletePatternButton.visibility = android.view.View.GONE
            return
        }
        signalNameInput.setText(pattern.name)
        points.clear()
        points.addAll(pattern.points)
        if (signalKind == SIGNAL_KIND_CALLING) {
            repeatCountInput.setText("1")
        }
        deletePatternButton.visibility = android.view.View.VISIBLE
    }

    private fun confirmDeleteEditedPattern() {
        val id = editPatternId ?: return
        AlertDialog.Builder(this)
            .setMessage(R.string.delete_signal_confirm)
            .setPositiveButton(R.string.common_ok) { _, _ ->
                when (signalKind) {
                    SIGNAL_KIND_CALLING -> callingPatternStore.deleteCustomPattern(id)
                    else -> rogerPatternStore.deleteCustomPattern(id)
                }
                setResult(RESULT_OK)
                finish()
            }
            .setNegativeButton(R.string.roger_cancel, null)
            .show()
    }

    private fun formatFreqForEdit(freqHz: Double): String {
        if (freqHz <= 0.0) return "0"
        val frac = kotlin.math.abs(freqHz % 1.0)
        if (frac < 1e-9 || frac > 1.0 - 1e-9) return freqHz.toInt().toString()
        return freqHz.toString()
    }

    private fun showSegmentDialog(editIndex: Int?) {
        if (editIndex != null && editIndex !in points.indices) return
        val freqInput = EditText(this).apply {
            hint = getString(R.string.roger_point_frequency_hint)
            inputType = InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_DECIMAL
        }
        val durationInput = EditText(this).apply {
            hint = getString(R.string.roger_point_duration_hint)
            inputType = InputType.TYPE_CLASS_NUMBER
        }
        if (editIndex != null) {
            val pt = points[editIndex]
            freqInput.setText(formatFreqForEdit(pt.freqHz))
            durationInput.setText(pt.durationMs.toString())
        }
        val container = android.widget.LinearLayout(this).apply {
            orientation = android.widget.LinearLayout.VERTICAL
            setPadding(40, 20, 40, 0)
            addView(freqInput)
            addView(durationInput)
        }
        val titleRes =
            if (editIndex == null) R.string.roger_new_segment_title else R.string.roger_edit_segment_title

        AlertDialog.Builder(this)
            .setTitle(titleRes)
            .setView(container)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                val freq = freqInput.text?.toString()?.trim()?.toDoubleOrNull()
                val duration = durationInput.text?.toString()?.trim()?.toIntOrNull()
                if (freq == null || duration == null || freq < 0.0 || duration <= 0) {
                    Toast.makeText(this, R.string.roger_point_invalid, Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                val repeatCount = resolveRepeatCountOrNull(showToastOnError = true) ?: return@setPositiveButton
                val oldDur = editIndex?.let { points[it].durationMs } ?: 0
                val baseSum = points.sumOf { it.durationMs } - oldDur
                val totalDurationMs = (baseSum + duration) * repeatCount
                if (totalDurationMs > maxSignalDurationMs()) {
                    Toast.makeText(this, R.string.roger_points_total_too_long, Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                if (editIndex == null) {
                    points += RogerPoint(freqHz = freq, durationMs = duration)
                } else if (editIndex in points.indices) {
                    points[editIndex] = RogerPoint(freqHz = freq, durationMs = duration)
                }
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
                val lineText = "${position + 1}. $toneLabel / ${point.durationMs} ms"
                label.text = lineText
                label.contentDescription =
                    getString(R.string.segment_row_a11y, position + 1, toneLabel, point.durationMs)
                label.tag = position
                label.setOnClickListener { v ->
                    val pos = v.tag as? Int ?: return@setOnClickListener
                    if (pos in points.indices) showSegmentDialog(editIndex = pos)
                }
                delete.tag = position
                delete.setOnClickListener { v ->
                    val pos = v.tag as? Int ?: return@setOnClickListener
                    if (pos in points.indices) {
                        points.removeAt(pos)
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
        val editId = editPatternId
        if (editId != null) {
            val ok = when (signalKind) {
                SIGNAL_KIND_CALLING -> callingPatternStore.updateCustomPattern(
                    editId,
                    name,
                    buildRepeatedPoints(repeatCount),
                )
                else -> rogerPatternStore.updateCustomPattern(editId, name, points.toList())
            }
            if (!ok) {
                Toast.makeText(this, R.string.roger_name_required, Toast.LENGTH_SHORT).show()
                return
            }
        } else {
            if (signalKind == SIGNAL_KIND_CALLING) {
                callingPatternStore.saveCustomPattern(name, buildRepeatedPoints(repeatCount))
            } else {
                rogerPatternStore.saveCustomPattern(name, points.toList())
            }
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
