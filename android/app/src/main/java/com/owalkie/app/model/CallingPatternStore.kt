package com.owalkie.app.model

import android.content.Context
import com.google.gson.Gson
import com.google.gson.reflect.TypeToken

class CallingPatternStore(context: Context) {
    private val prefs = context.getSharedPreferences("calling_patterns", Context.MODE_PRIVATE)
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
            RogerPoint(freqHz = 2000.0, durationMs = 120),
            RogerPoint(freqHz = 1000.0, durationMs = 120),
        )

        return listOf(
            RogerPattern(
                id = "call_variant_1",
                name = "Вариант 1",
                builtIn = true,
                points = List(9) { variant1Cycle }.flatten(),
            ),
            RogerPattern(
                id = "call_variant_2",
                name = "Вариант 2",
                builtIn = true,
                points = List(14) { variant2Cycle }.flatten(),
            ),
            RogerPattern(
                id = "call_variant_3",
                name = "Вариант 3",
                builtIn = true,
                points = List(32) {
                    listOf(
                        RogerPoint(freqHz = 2000.0, durationMs = 60),
                        RogerPoint(freqHz = 1000.0, durationMs = 60),
                    )
                }.flatten(),
            ),
        )
    }
}
