import org.jetbrains.kotlin.gradle.dsl.JvmTarget
import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "ru.outsidepro_arts.owalkie"
    compileSdk = 35

    val keystoreFile = rootProject.file("keystore/release-keystore.properties")

    fun Properties.require(key: String): String =
        getProperty(key)?.takeIf { it.isNotBlank() }
            ?: throw GradleException("Missing or empty '$key' in ${keystoreFile.path}")

    val releaseSigningConfig = if (keystoreFile.exists()) {
        val props = Properties().apply {
            keystoreFile.inputStream().use { load(it) }
        }

        signingConfigs.create("release").apply {
            val storeFilePath = props.require("storeFile")
            storeFile = rootProject.file(storeFilePath).also {
                if (!it.exists()) {
                    throw GradleException("Keystore file not found: ${it.path}")
                }
            }

            storePassword = props.require("storePassword")
            keyAlias = props.require("keyAlias")
            keyPassword = props.require("keyPassword")
        }
    } else {
        logger.lifecycle("No release keystore config found, building unsigned release")
        null
    }

    defaultConfig {
        applicationId = "ru.outsidepro_arts.owalkie"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false

            releaseSigningConfig?.let {
                signingConfig = it
            }

            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        viewBinding = true
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(JvmTarget.JVM_17)
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.activity:activity-ktx:1.10.1")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.10.2")
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    implementation("com.google.code.gson:gson:2.13.1")
    implementation("eu.buney.kopus:kopus:1.6")
}