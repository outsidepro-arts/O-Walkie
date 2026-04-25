package ru.outsidepro_arts.owalkie.model

import android.content.Context
import com.google.gson.Gson
import com.google.gson.reflect.TypeToken

class ServerStore(context: Context) {
    private val prefs = context.getSharedPreferences("server_profiles", Context.MODE_PRIVATE)
    private val gson = Gson()
    private val key = "items"
    private val selectedNameKey = "selected_name"

    fun load(): MutableList<ServerProfile> {
        val raw = prefs.getString(key, null) ?: return mutableListOf()
        return runCatching {
            val listType = object : TypeToken<List<ServerProfile>>() {}.type
            gson.fromJson<List<ServerProfile>>(raw, listType).toMutableList()
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
