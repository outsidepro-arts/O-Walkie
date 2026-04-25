package ru.outsidepro_arts.owalkie.model

data class ServerProfile(
    val name: String,
    val host: String,
    val wsPort: Int,
    val udpPort: Int,
    val channel: String,
)
