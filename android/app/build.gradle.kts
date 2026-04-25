import org.jetbrains.kotlin.gradle.dsl.JvmTarget
import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "ru.outsidepro_arts.owalkie"
    compileSdk = 35

    val releaseKeystoreFile = rootProject.file("keystore/release-keystore.properties")
    val releaseKeystoreProps = Properties().apply {
        if (releaseKeystoreFile.exists()) {
            releaseKeystoreFile.inputStream().use { load(it) }
        }
    }

    defaultConfig {
        applicationId = "ru.outsidepro_arts.owalkie"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"
    }

    signingConfigs {
        create("release") {
            val storeFilePath = releaseKeystoreProps.getProperty("storeFile")
                ?: error("Missing 'storeFile' in keystore/release-keystore.properties")
            storeFile = rootProject.file(storeFilePath)
            storePassword = releaseKeystoreProps.getProperty("storePassword")
                ?: error("Missing 'storePassword' in keystore/release-keystore.properties")
            keyAlias = releaseKeystoreProps.getProperty("keyAlias")
                ?: error("Missing 'keyAlias' in keystore/release-keystore.properties")
            keyPassword = releaseKeystoreProps.getProperty("keyPassword")
                ?: error("Missing 'keyPassword' in keystore/release-keystore.properties")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("release")
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
