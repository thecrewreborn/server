plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val serverVersion: String = run {
    val py = rootProject.file("../tcreborn/version.py")
    val m = Regex("VERSION\\s*=\\s*\"([^\"]+)\"").find(if (py.exists()) py.readText() else "")
    val plain = rootProject.file("../native/VERSION")
    m?.groupValues?.get(1) ?: if (plain.exists()) plain.readText().trim() else "0.0.0"
}

fun versionCodeOf(v: String): Int {
    val p = v.split(".").map { it.takeWhile(Char::isDigit).toIntOrNull() ?: 0 } + listOf(0, 0, 0)
    return p[0] * 10000 + p[1] * 100 + p[2]
}

val releaseKeystore: String? = System.getenv("TCR_ANDROID_KEYSTORE")?.takeIf { it.isNotEmpty() }
val releasePassword: String? = System.getenv("TCR_ANDROID_KEYSTORE_PASSWORD")

android {
    namespace = "org.thecrewreborn.server"
    compileSdk = 34

    defaultConfig {
        applicationId = "org.thecrewreborn.server"
        minSdk = 28
        targetSdk = 34
        versionCode = versionCodeOf(serverVersion)
        versionName = serverVersion
    }

    signingConfigs {
        if (releaseKeystore != null) {
            create("release") {
                storeFile = file(releaseKeystore)
                storeType = "pkcs12"
                storePassword = releasePassword
                keyAlias = "thecrewreborn"
                keyPassword = releasePassword
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.findByName("release") ?: signingConfigs.getByName("debug")
        }
    }

    sourceSets["main"].jniLibs.srcDirs("jni")
    sourceSets["main"].assets.srcDirs(layout.buildDirectory.dir("generated/serverAssets"))

    packaging {
        jniLibs {
            useLegacyPackaging = true
            keepDebugSymbols += "**/libtcreborn.so"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
}

val serverAssets by tasks.registering(Copy::class) {
    from(rootProject.file("../config.example.ini"))
    into(layout.buildDirectory.dir("generated/serverAssets"))
}
tasks.named("preBuild") { dependsOn(serverAssets) }
