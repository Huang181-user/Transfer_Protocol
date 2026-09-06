plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "com.example.transfer_server"
    compileSdk = 35

    sourceSets {
        getByName("main") {
            jniLibs.srcDirs("src/main/cpp/libsodium/lib", "src/main/cpp/msquic/lib")
        }
    }

    ndkVersion = "28.2.13676358"
    defaultConfig {
        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++20"
                // 🔥 KHOÁ CỨNG CHỈ BUILD ARM64-V8A ĐỂ TRÁNH LỖI THIẾU FILE MSQUIC
                abiFilters += listOf("arm64-v8a")
            }
        }
        // 🔥 ĐỒNG THỜI KHÓA LUÔN APK ĐẦU RA CHỈ CHỨA ARM64-V8A
        ndk {
            abiFilters += listOf("arm64-v8a")
        }
        applicationId = "com.example.transfer_server"
        minSdk = 35
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1+"
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    buildFeatures {
        compose = true
    }
}

dependencies {
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.graphics)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    testImplementation(libs.junit)
    androidTestImplementation(platform(libs.androidx.compose.bom))
    androidTestImplementation(libs.androidx.compose.ui.test.junit4)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(libs.androidx.junit)
    debugImplementation(libs.androidx.compose.ui.test.manifest)
    debugImplementation(libs.androidx.compose.ui.tooling)

    implementation("com.github.mwiede:jsch:0.2.17")
    implementation("com.squareup.okhttp3:okhttp-android:5.0.0-alpha.11")
}

layout.buildDirectory.set(File("/tmp/android_builds/transfer_server_app"))
