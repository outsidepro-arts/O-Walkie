package ru.outsidepro_arts.owalkie.model

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject

class CallingPatternStore(context: Context) {
    private val prefs = context.getSharedPreferences("calling_patterns", Context.MODE_PRIVATE)
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

    fun saveCustomPattern(name: String, points: List<RogerPoint>, repeatCount: Int): RogerPattern {
        val cleanedName = name.trim()
        val custom = loadCustomPatterns().toMutableList()
        val existingIdx = custom.indexOfFirst { it.name.equals(cleanedName, ignoreCase = true) }
        val rep = repeatCount.coerceAtLeast(1)
        val pattern = RogerPattern(
            id = if (existingIdx >= 0) custom[existingIdx].id else "custom_${System.currentTimeMillis()}",
            name = cleanedName,
            points = points,
            builtIn = false,
            repeatCount = rep,
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

    fun updateCustomPattern(patternId: String, name: String, points: List<RogerPoint>, repeatCount: Int): Boolean {
        val cleanedName = name.trim()
        if (cleanedName.isEmpty() || points.isEmpty()) return false
        val rep = repeatCount.coerceAtLeast(1)
        val custom = loadCustomPatterns().toMutableList()
        val idx = custom.indexOfFirst { it.id == patternId }
        if (idx < 0) return false
        custom[idx] = custom[idx].copy(name = cleanedName, points = points, repeatCount = rep)
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
                val o = arr.getJSONObject(i)
                val id = o.optString("id", "") ?: continue
                val name = o.optString("name", "") ?: continue
                val builtIn = o.optBoolean("builtIn", false)
                if (builtIn) continue
                val ptsArr = o.optJSONArray("points") ?: continue
                val pts = ArrayList<RogerPoint>(ptsArr.length())
                for (j in 0 until ptsArr.length()) {
                    val pt = ptsArr.optJSONObject(j) ?: continue
                    val freq = pt.optDouble("freqHz", Double.NaN)
                    val dur = pt.optInt("durationMs", -1)
                    if (freq.isNaN() || dur <= 0 || freq < 0.0) continue
                    pts += RogerPoint(freqHz = freq, durationMs = dur)
                }
                if (pts.isEmpty() || id.isEmpty()) continue
                val rc = if (o.has("repeatCount") && !o.isNull("repeatCount")) {
                    o.optInt("repeatCount", 1).coerceAtLeast(1)
                } else {
                    null
                }
                out += RogerPattern(
                    id = id,
                    name = name,
                    points = pts,
                    builtIn = false,
                    repeatCount = rc,
                )
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
            val rc = p.repeatCount?.coerceAtLeast(1) ?: 1
            o.put("repeatCount", rc)
            arr.put(o)
        }
        prefs.edit().putString(customKey, arr.toString()).apply()
    }

    private fun builtInPatterns(): List<RogerPattern> {
        val variant1Cycle = listOf(
            RogerPoint(freqHz = 2300.0, durationMs = 70),
            RogerPoint(freqHz = 1850.0, durationMs = 70),
            RogerPoint(freqHz = 1450.0, durationMs = 70),
        )
        val variant2Cycle = listOf(
            RogerPoint(freqHz = 1150.0, durationMs = 35),
            RogerPoint(freqHz = 1350.0, durationMs = 35),
            RogerPoint(freqHz = 1550.0, durationMs = 35),
            RogerPoint(freqHz = 1750.0, durationMs = 35),
            RogerPoint(freqHz = 1550.0, durationMs = 35),
            RogerPoint(freqHz = 1350.0, durationMs = 35),
        )
        val variant3Cycle = listOf(
            RogerPoint(freqHz = 2000.0, durationMs = 60),
            RogerPoint(freqHz = 1000.0, durationMs = 60),
        )

        return listOf(
            RogerPattern(
                id = "call_variant_1",
                name = "Вариант 1",
                builtIn = true,
                points = variant1Cycle,
                repeatCount = 9,
            ),
            RogerPattern(
                id = "call_variant_2",
                name = "Вариант 2",
                builtIn = true,
                points = variant2Cycle,
                repeatCount = 14,
            ),
            RogerPattern(
                id = "call_variant_3",
                name = "Вариант 3",
                builtIn = true,
                points = variant3Cycle,
                repeatCount = 32,
            ),
        )
    }
}
