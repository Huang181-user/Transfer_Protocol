plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "com.example.transfer_server"
    compileSdk {
        version = release(36) {
            minorApiLevel = 1
        }
    }

    defaultConfig {
        applicationId = "com.example.transfer_server"
        minSdk = 35
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
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
    // Thư viện JSch để chạy SFTP
    implementation("com.github.mwiede:jsch:0.2.17")

    // Bộ ba nguyên tử để chạy QUIC (HTTP/3)
    implementation("com.squareup.okhttp3:okhttp:5.0.0-alpha.11")
    implementation("com.google.android.gms:play-services-cronet:18.0.1")

    // FIX TẠI ĐÂY: okhttp-cronet đổi thành okhttp-android
    implementation("com.squareup.okhttp3:okhttp-android:5.0.0-alpha.11")
    implementation(files("libs/quicdroid.aar"))
}
// HACK: Ép cứng module app đẻ file build ra ổ cục bộ (/tmp) để né lỗi NFS
layout.buildDirectory.set(File("/tmp/android_builds/transfer_server_app"))
