package ru.outsidepro_arts.owalkie.model

import android.content.Context
import com.google.gson.Gson
import com.google.gson.JsonParser

class ServerStore(context: Context) {
    private val prefs = context.getSharedPreferences("server_profiles", Context.MODE_PRIVATE)
    private val gson = Gson()
    private val key = "items"
    private val selectedNameKey = "selected_name"

    fun load(): MutableList<ServerProfile> {
        val raw = prefs.getString(key, null) ?: return mutableListOf()
        return runCatching {
            val root = JsonParser.parseString(raw)
            if (!root.isJsonArray) return@runCatching mutableListOf()
            root.asJsonArray.map { el ->
                val o = el.asJsonObject
                val port = when {
                    o.has("port") && !o.get("port").isJsonNull -> o.get("port").asInt
                    o.has("wsPort") && !o.get("wsPort").isJsonNull -> o.get("wsPort").asInt
                    o.has("udpPort") && !o.get("udpPort").isJsonNull -> o.get("udpPort").asInt
                    else -> 5500
                }
                ServerProfile(
                    name = if (o.has("name") && !o.get("name").isJsonNull) o.get("name").asString else "",
                    host = if (o.has("host") && !o.get("host").isJsonNull) o.get("host").asString else "",
                    port = port,
                    channel = if (o.has("channel") && !o.get("channel").isJsonNull) {
                        o.get("channel").asString
                    } else {
                        "global"
                    },
                )
            }.toMutableList()
        }.getOrDefault(mutableListOf())
    }

    fun save(items: List<ServerProfile>) {
        prefs.edit().putString(key, gson.toJson(items)).apply()
    }

    fun getLastSelectedName(): String? {
        return prefs.getString(selectedNameKey, null)
    }

    fun setLastSelectedName(name: String) {
        prefs.edit().putString(selectedNameKey, name).apply()
    }
}
