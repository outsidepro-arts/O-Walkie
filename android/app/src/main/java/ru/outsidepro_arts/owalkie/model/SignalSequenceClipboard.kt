package ru.outsidepro_arts.owalkie.model

import org.json.JSONArray
import org.json.JSONObject

/**
 * Clipboard exchange format shared with Windows desktop client.
 *
 * ```
 * {
 *   "oWalkieSignalSequence": {
 *     "version": 1,
 *     "signal": { "name": "..." },
 *     "points": [ { "durationMs": ..., "freqHz": ... }, ... ],
 *     "repetitions": 4   // only when editing a calling signal (optional field)
 *   }
 * }
 * ```
 */
object SignalSequenceClipboard {
    const val MAGIC_KEY = "oWalkieSignalSequence"
    const val FORMAT_VERSION = 1

    data class Payload(
        val name: String,
        val points: List<RogerPoint>,
        /** Ignored for Roger UI; from optional `repetitions` when present. */
        val repeatCount: Int,
    )

    fun toJson(name: String, points: List<RogerPoint>, includeRepetitions: Boolean, repetitions: Int): String {
        val arr = JSONArray()
        for (p in points) {
            arr.put(
                JSONObject().apply {
                    put("durationMs", p.durationMs)
                    put("freqHz", p.freqHz)
                },
            )
        }
        val inner = JSONObject().apply {
            put("version", FORMAT_VERSION)
            put("signal", JSONObject().put("name", name))
            put("points", arr)
            if (includeRepetitions) {
                put("repetitions", repetitions.coerceIn(1, 500))
            }
        }
        return JSONObject().put(MAGIC_KEY, inner).toString()
    }

    fun parseFromText(fullText: String): Payload? {
        val trimmed = fullText.trim()
        val candidates = LinkedHashSet<String>()
        if (trimmed.isNotEmpty()) {
            candidates.add(trimmed)
        }
        var start = 0
        while (start < fullText.length) {
            val brace = fullText.indexOf('{', startIndex = start)
            if (brace < 0) break
            extractBalancedJsonObject(fullText, brace)?.let { candidates.add(it) }
            start = brace + 1
        }
        for (text in candidates) {
            parseNestedEnvelope(text)?.let { return it }
            parseLegacyFlatEnvelope(text)?.let { return it }
        }
        return null
    }

    /** Current format: `oWalkieSignalSequence` is an object. */
    private fun parseNestedEnvelope(jsonText: String): Payload? {
        return try {
            val root = JSONObject(jsonText)
            val envelope = root.optJSONObject(MAGIC_KEY) ?: return null
            val ver = (envelope.opt("version") as? Number)?.toInt() ?: return null
            if (ver != FORMAT_VERSION) {
                return null
            }
            val signalObj = envelope.optJSONObject("signal") ?: return null
            val name = signalObj.optString("name", "")
            val arr = envelope.optJSONArray("points") ?: return null
            val out = parsePointsArray(arr) ?: return null
            val repeatCount = parseRepetitionsField(envelope)
            Payload(name = name, points = out, repeatCount = repeatCount)
        } catch (_: Exception) {
            null
        }
    }

    /** Earlier dev-only flat layout (numeric magic + root-level fields). */
    private fun parseLegacyFlatEnvelope(jsonText: String): Payload? {
        return try {
            val root = JSONObject(jsonText)
            val magic = root.opt(MAGIC_KEY) ?: return null
            if (magic !is Number) {
                return null
            }
            val arr = root.optJSONArray("points") ?: return null
            val out = parsePointsArray(arr) ?: return null
            if (out.isEmpty()) {
                return null
            }
            val name = root.optString("name", "")
            val repeatCount = when {
                root.has("repeatCount") -> parseIntish(root.opt("repeatCount")) ?: 1
                root.has("repetitions") -> parseIntish(root.opt("repetitions")) ?: 1
                else -> 1
            }.coerceAtLeast(1)
            Payload(name = name, points = out, repeatCount = repeatCount)
        } catch (_: Exception) {
            null
        }
    }

    private fun parsePointsArray(arr: JSONArray): List<RogerPoint>? {
        val out = ArrayList<RogerPoint>(arr.length())
        for (i in 0 until arr.length()) {
            val o = arr.optJSONObject(i) ?: return null
            if (!o.has("freqHz") || !o.has("durationMs")) {
                return null
            }
            val freq = o.getDouble("freqHz")
            val dur = o.getInt("durationMs")
            if (freq < 0.0 || dur <= 0) {
                return null
            }
            out.add(RogerPoint(freqHz = freq, durationMs = dur))
        }
        return if (out.isEmpty()) null else out
    }

    private fun parseRepetitionsField(envelope: JSONObject): Int {
        if (!envelope.has("repetitions")) {
            return 1
        }
        return parseIntish(envelope.opt("repetitions"))?.coerceAtLeast(1) ?: 1
    }

    private fun parseIntish(raw: Any?): Int? {
        return when (raw) {
            null -> null
            is Number -> raw.toInt()
            is String -> raw.trim().toIntOrNull()
            else -> null
        }
    }

    private fun extractBalancedJsonObject(s: String, openIndex: Int): String? {
        if (openIndex !in s.indices || s[openIndex] != '{') return null
        var depth = 0
        var i = openIndex
        var inString = false
        var escape = false
        while (i < s.length) {
            val c = s[i]
            if (inString) {
                when {
                    escape -> escape = false
                    c == '\\' -> escape = true
                    c == '"' -> inString = false
                }
            } else {
                when (c) {
                    '"' -> inString = true
                    '{' -> depth++
                    '}' -> {
                        depth--
                        if (depth == 0) {
                            return s.substring(openIndex, i + 1)
                        }
                    }
                }
            }
            i++
        }
        return null
    }
}
