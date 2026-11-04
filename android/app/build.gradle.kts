plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Caminhos entregues pelo workflow (pasta do XenonRecomp e pasta generated/ do pipeline).
// Sem eles, o aplicativo compila sem o codigo recompilado.
val bo1XenonRecomp: String = (project.findProperty("bo1.xenonrecomp") as String?) ?: ""
val bo1Generated: String = (project.findProperty("bo1.generated") as String?) ?: ""

android {
    namespace = "com.bo1recomp"
    compileSdk = 34
    ndkVersion = "27.3.13750724"

    defaultConfig {
        applicationId = "com.bo1recomp.launcher"
        minSdk = 29
        targetSdk = 34
        versionCode = 2
        versionName = "0.2.0"

        ndk {
            abiFilters += "arm64-v8a"
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DBO1_XENONRECOMP_DIR=$bo1XenonRecomp",
                    "-DBO1_GENERATED_DIR=$bo1Generated",
                    "-DANDROID_STL=c++_static"
                )
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
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
