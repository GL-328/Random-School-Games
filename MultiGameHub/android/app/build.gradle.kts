// The native code (all game and app logic) is built by the shared CMake project
// in ../../CMakeLists.txt. Java is limited to the SDL glue and PlatformBridge.
plugins {
    id("com.android.application")
}

val appVersion = rootProject.file("../VERSION").readText().trim()
val (vMajor, vMinor, vPatch) = appVersion.split(".").map { it.toInt() }

android {
    namespace = "com.gl328.multigamehub"
    compileSdk = 35
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "com.gl328.multigamehub"
        minSdk = 21
        targetSdk = 35
        versionCode = vMajor * 10000 + vMinor * 100 + vPatch
        versionName = appVersion
        ndk {
            abiFilters += "arm64-v8a"
        }
        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_static",
                    "-DMGH_BUILD_TESTS=OFF",
                    "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON",
                )
                targets += "main"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "3.22.1+"
        }
    }

    sourceSets["main"].java.srcDirs("src/main/java", "../../third_party/SDL/android-project/app/src/main/java")
    sourceSets["main"].assets.srcDirs("../../assets")

    signingConfigs {
        // Release signing comes from environment variables (GitHub Actions secrets).
        // Every release MUST use the same key, otherwise in-app updates cannot install.
        create("release") {
            val ks = System.getenv("MGH_KEYSTORE_FILE")
            if (ks != null && file(ks).exists()) {
                storeFile = file(ks)
                storePassword = System.getenv("MGH_KEYSTORE_PASSWORD")
                keyAlias = System.getenv("MGH_KEY_ALIAS")
                keyPassword = System.getenv("MGH_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            val rel = signingConfigs.getByName("release")
            signingConfig = if (rel.storeFile != null) rel else signingConfigs.getByName("debug")
            externalNativeBuild { cmake { arguments += "-DCMAKE_BUILD_TYPE=MinSizeRel" } }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }

    packaging {
        jniLibs.useLegacyPackaging = false  // keep libmain.so uncompressed & page aligned
    }
    lint {
        abortOnError = false
    }
}
