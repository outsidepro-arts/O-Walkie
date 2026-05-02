package ru.outsidepro_arts.owalkie.model

import android.content.Context
import com.google.gson.Gson
import com.google.gson.reflect.TypeToken
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
)

class RogerPatternStore(context: Context) {
    private val appContext = context.applicationContext
    private val prefs = context.getSharedPreferences("roger_patterns", Context.MODE_PRIVATE)
    private val gson = Gson()
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
            val listType = object : TypeToken<List<RogerPattern>>() {}.type
            gson.fromJson<List<RogerPattern>>(raw, listType).orEmpty()
                .filter { !it.builtIn && it.points.isNotEmpty() }
        }.getOrDefault(emptyList())
    }

    private fun saveCustomPatterns(items: List<RogerPattern>) {
        prefs.edit().putString(customKey, gson.toJson(items)).apply()
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
