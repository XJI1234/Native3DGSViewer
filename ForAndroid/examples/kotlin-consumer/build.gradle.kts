plugins {
    id("com.android.library") version "8.7.2"
    id("org.jetbrains.kotlin.android") version "2.0.21"
}
android {
    namespace = "org.native3dgs.consumer"
    compileSdk = 35
    defaultConfig { minSdk = 29 }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
    }
    kotlinOptions { jvmTarget = "21" }
}
dependencies {
    compileOnly(files("../../aar/native3dgs-sdk-release.aar"))
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")
}
