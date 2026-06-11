import java.util.Properties

plugins {
    id("com.android.application")
    // The Flutter Gradle Plugin must be applied after the Android and Kotlin Gradle plugins.
    id("dev.flutter.flutter-gradle-plugin")
}

val repoRoot = rootProject.projectDir.parentFile.parentFile

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
    val f = repoRoot.resolve("android/owalkie-version.properties")
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

val releaseKeystoreFile = repoRoot.resolve("android/keystore/release-keystore.properties")
val releaseKeystoreProps = Properties().apply {
    if (releaseKeystoreFile.exists()) {
        releaseKeystoreFile.inputStream().use { load(it) }
    }
}

android {
    namespace = "ru.outsidepro_arts.owalkie.flutter"
    compileSdk = flutter.compileSdkVersion
    ndkVersion = flutter.ndkVersion

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    defaultConfig {
        applicationId = "ru.outsidepro_arts.owalkie.flutter"
        minSdk = flutter.minSdkVersion
        targetSdk = flutter.targetSdkVersion
        versionCode = owalkieVersionCode
        versionName = owalkieVersionName
        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
    }

    signingConfigs {
        if (releaseKeystoreFile.exists()) {
            create("release") {
                val storeFilePath = releaseKeystoreProps.getProperty("storeFile")
                    ?: error("Missing 'storeFile' in android/keystore/release-keystore.properties")
                storeFile = repoRoot.resolve("android").resolve(storeFilePath)
                storePassword = releaseKeystoreProps.getProperty("storePassword")
                    ?: error("Missing 'storePassword' in android/keystore/release-keystore.properties")
                keyAlias = releaseKeystoreProps.getProperty("keyAlias")
                    ?: error("Missing 'keyAlias' in android/keystore/release-keystore.properties")
                keyPassword = releaseKeystoreProps.getProperty("keyPassword")
                    ?: error("Missing 'keyPassword' in android/keystore/release-keystore.properties")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            isShrinkResources = false
            signingConfig = if (releaseKeystoreFile.exists()) {
                signingConfigs.getByName("release")
            } else {
                signingConfigs.getByName("debug")
            }
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget = org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17
    }
}

flutter {
    source = "../.."
}

dependencies {
    implementation("androidx.media:media:1.7.0")
}
