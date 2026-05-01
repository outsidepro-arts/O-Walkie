import org.jetbrains.kotlin.gradle.dsl.JvmTarget
import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val repoRoot = rootProject.projectDir.parentFile

fun gitDescribe(): String {
    return try {
        val proc = ProcessBuilder("git", "describe", "--tags", "--match", "v*", "--long", "--dirty")
            .directory(repoRoot)
            .redirectError(ProcessBuilder.Redirect.DISCARD)
            .start()
        val text = proc.inputStream.bufferedReader().use { it.readText().trim() }
        if (proc.waitFor() == 0 && text.isNotEmpty()) text else ""
    } catch (_: Exception) {
        ""
    }
}

fun stripLeadingV(s: String) = if (s.startsWith("v")) s.drop(1) else s

val describeVersionPattern =
    Regex("^v?(\\d+)\\.(\\d+)\\.(\\d+)-(\\d+)-g[0-9a-fA-F]+(-dirty)?$")

fun parseDescribeToVersionName(describe: String): String {
    if (describe.isBlank()) return "0.0.0-dev"
    return stripLeadingV(describe.trim())
}

/** Encodes vMAJOR.MINOR.PATCH with post-tag commit offset; keeps versionCode monotonic for typical semver releases. */
fun parseDescribeToVersionCode(describe: String): Int {
    val m = describeVersionPattern.matchEntire(describe.trim()) ?: return 1
    val major = m.groupValues[1].toInt()
    val minor = m.groupValues[2].toInt()
    val patch = m.groupValues[3].toInt()
    val offset = m.groupValues[4].toInt().coerceIn(0, 999)
    val code = major * 10_000_000 + minor * 100_000 + patch * 1_000 + offset
    return code.coerceIn(1, 2_100_000_000)
}

fun readVersionCodeFloor(): Int {
    val f = rootProject.file("owalkie-version.properties")
    if (!f.exists()) return 0
    val p = Properties().apply { f.inputStream().use { load(it) } }
    return p.getProperty("versionCodeFloor", "0").toIntOrNull()?.coerceAtLeast(0) ?: 0
}

val gitDescribeText = gitDescribe()
val owalkieVersionName: String = run {
    val prop = (project.findProperty("owalkie.versionName") as String?)?.trim()?.takeIf { it.isNotEmpty() }
    val env = System.getenv("OWALKIE_VERSION_NAME")?.trim()?.takeIf { it.isNotEmpty() }
    prop ?: env ?: parseDescribeToVersionName(gitDescribeText)
}
val owalkieVersionCode: Int = run {
    val prop = (project.findProperty("owalkie.versionCode") as String?)?.trim()?.toIntOrNull()
    val env = System.getenv("OWALKIE_VERSION_CODE")?.trim()?.toIntOrNull()
    val computed = parseDescribeToVersionCode(gitDescribeText)
    val floor = readVersionCodeFloor()
    (prop ?: env ?: maxOf(computed, floor)).coerceIn(1, 2_100_000_000)
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
        versionCode = owalkieVersionCode
        versionName = owalkieVersionName
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
