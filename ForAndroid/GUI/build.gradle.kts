plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "org.native3dgs.viewer"
    compileSdk = 35

    defaultConfig {
        applicationId = "org.native3dgs.viewer"
        minSdk = 29
        targetSdk = 35
        versionCode = 2
        versionName = "0.2.1"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildTypes {
        getByName("debug") { ndk { abiFilters += listOf("x86_64", "arm64-v8a") } }
        getByName("release") { ndk { abiFilters += listOf("arm64-v8a") } }
        create("benchmark") {
            initWith(getByName("debug"))
            isDebuggable = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
    }
    kotlinOptions { jvmTarget = "21" }
}

androidComponents {
    onVariants(selector().withBuildType("benchmark")) { variant ->
        variant.outputs.forEach { it.versionCode.set(3) }
    }
}

dependencies {
    implementation(project(":sdk"))
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")
    testImplementation("junit:junit:4.13.2")
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
}
