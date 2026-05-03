package ru.outsidepro_arts.owalkie.model

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import ru.outsidepro_arts.owalkie.R

data class RogerPoint(
    val freqHz: Double,
    val durationMs: Int,
)

data class RogerPattern(
    val id: String,
    val name: String,
    val points: List<RogerPoint>,
    val builtIn: Boolean,
    /** Only used for calling signals; not stored in Roger custom JSON (see [CallingPatternStore]). */
    val repeatCount: Int? = null,
)

/** Expand stored calling pattern (base `points` × `repeatCount`) for PCM/stream generation. */
fun RogerPattern.expandedCallingPoints(): List<RogerPoint> {
    val r = repeatCount?.coerceAtLeast(1) ?: 1
    if (r <= 1) return points
    return List(r) { points }.flatten()
}

class RogerPatternStore(context: Context) {
    private val appContext = context.applicationContext
    private val prefs = context.getSharedPreferences("roger_patterns", Context.MODE_PRIVATE)
    private val customKey = "custom_items"
    private val selectedKey = "selected_id"

    fun getAllPatterns(): List<RogerPattern> {
        return builtInPatterns() + loadCustomPatterns()
    }

    fun getSelectedPattern(): RogerPattern {
        val selectedId = prefs.getString(selectedKey, null)
        return getAllPatterns().firstOrNull { it.id == selectedId } ?: builtInPatterns().first()
    }

    fun setSelectedPattern(patternId: String) {
        prefs.edit().putString(selectedKey, patternId).apply()
    }

    fun saveCustomPattern(name: String, points: List<RogerPoint>): RogerPattern {
        val cleanedName = name.trim()
        val custom = loadCustomPatterns().toMutableList()
        val existingIdx = custom.indexOfFirst { it.name.equals(cleanedName, ignoreCase = true) }
        val pattern = RogerPattern(
            id = if (existingIdx >= 0) custom[existingIdx].id else "custom_${System.currentTimeMillis()}",
            name = cleanedName,
            points = points,
            builtIn = false,
        )
        if (existingIdx >= 0) {
            custom[existingIdx] = pattern
        } else {
            custom += pattern
        }
        saveCustomPatterns(custom)
        setSelectedPattern(pattern.id)
        return pattern
    }

    fun updateCustomPattern(patternId: String, name: String, points: List<RogerPoint>): Boolean {
        val cleanedName = name.trim()
        if (cleanedName.isEmpty() || points.isEmpty()) return false
        val custom = loadCustomPatterns().toMutableList()
        val idx = custom.indexOfFirst { it.id == patternId }
        if (idx < 0) return false
        custom[idx] = custom[idx].copy(name = cleanedName, points = points)
        saveCustomPatterns(custom)
        setSelectedPattern(patternId)
        return true
    }

    fun deleteCustomPattern(patternId: String): Boolean {
        val custom = loadCustomPatterns().toMutableList()
        val removed = custom.removeAll { it.id == patternId }
        if (!removed) return false
        saveCustomPatterns(custom)
        val selectedId = prefs.getString(selectedKey, null)
        if (selectedId == patternId) {
            setSelectedPattern(builtInPatterns().first().id)
        }
        return true
    }

    private fun loadCustomPatterns(): List<RogerPattern> {
        val raw = prefs.getString(customKey, null) ?: return emptyList()
        return runCatching {
            val arr = JSONArray(raw)
            val out = ArrayList<RogerPattern>()
            for (i in 0 until arr.length()) {
                val o = arr.optJSONObject(i) ?: continue
                val id = o.optString("id", "") ?: continue
                val name = o.optString("name", "") ?: continue
                if (o.optBoolean("builtIn", false)) continue
                val ptsArr = o.optJSONArray("points") ?: continue
                val pts = ArrayList<RogerPoint>(ptsArr.length())
                for (j in 0 until ptsArr.length()) {
                    val pt = ptsArr.optJSONObject(j) ?: continue
                    val freq = pt.optDouble("freqHz", Double.NaN)
                    val dur = pt.optInt("durationMs", -1)
                    if (freq.isNaN() || dur <= 0 || freq < 0.0) continue
                    pts += RogerPoint(freqHz = freq, durationMs = dur)
                }
                if (id.isEmpty() || pts.isEmpty()) continue
                out += RogerPattern(id = id, name = name, points = pts, builtIn = false, repeatCount = null)
            }
            out.filter { !it.builtIn && it.points.isNotEmpty() }
        }.getOrDefault(emptyList())
    }

    private fun saveCustomPatterns(items: List<RogerPattern>) {
        val arr = JSONArray()
        for (p in items) {
            val o = JSONObject()
            o.put("id", p.id)
            o.put("name", p.name)
            o.put("builtIn", p.builtIn)
            val pts = JSONArray()
            for (pt in p.points) {
                pts.put(
                    JSONObject().apply {
                        put("freqHz", pt.freqHz)
                        put("durationMs", pt.durationMs)
                    },
                )
            }
            o.put("points", pts)
            arr.put(o)
        }
        prefs.edit().putString(customKey, arr.toString()).apply()
    }

    private fun builtInPatterns(): List<RogerPattern> {
        return listOf(
            RogerPattern(
                id = "none",
                name = appContext.getString(R.string.roger_no_signal_option),
                builtIn = true,
                points = emptyList(),
            ),
            RogerPattern(
                id = "variant_1",
                name = "Вариант 1",
                builtIn = true,
                points = listOf(
                    RogerPoint(freqHz = 890.0, durationMs = 20),
                    RogerPoint(freqHz = 670.0, durationMs = 20),
                    RogerPoint(freqHz = 890.0, durationMs = 45),
                    RogerPoint(freqHz = 1000.0, durationMs = 28),
                ),
            ),
            RogerPattern(
                id = "variant_2",
                name = "Вариант 2",
                builtIn = true,
                points = listOf(
                    RogerPoint(freqHz = 1000.0, durationMs = 88),
                    RogerPoint(freqHz = 800.0, durationMs = 64),
                ),
            ),
            RogerPattern(
                id = "variant_3",
                name = "Вариант 3",
                builtIn = true,
                points = listOf(
                    RogerPoint(freqHz = 1330.0, durationMs = 68),
                    RogerPoint(freqHz = 1600.0, durationMs = 56),
                ),
            ),
        )
    }
}
