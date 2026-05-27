// Top-level build file where you can add configuration options common to all sub-projects/modules.
plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.kotlin.compose) apply false
}
// HACK: Ép Gradle đẻ file build ra ổ cục bộ (/tmp) để né lỗi I/O của ổ mạng NFS
layout.buildDirectory.set(File("/tmp/android_builds/${project.name}"))
