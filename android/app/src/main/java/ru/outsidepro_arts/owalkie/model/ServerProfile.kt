package ru.outsidepro_arts.owalkie.model

data class ServerProfile(
    val name: String,
    val host: String,
    val port: Int,
    val channel: String,
)
