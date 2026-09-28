plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "org.native3dgs.sdk"
    compileSdk = 35
    ndkVersion = "27.2.12479018"
    defaultConfig {
        minSdk = 29
        consumerProguardFiles("consumer-rules.pro")
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_shared")
                targets += listOf("gs_android_decoder")
            }
        }
    }
    testOptions.targetSdk = 35
    buildTypes {
        getByName("debug") { ndk { abiFilters += listOf("x86_64", "arm64-v8a") } }
        getByName("release") { ndk { abiFilters += listOf("arm64-v8a") } }
        create("benchmark") {
            initWith(getByName("debug"))
            externalNativeBuild {
                cmake { arguments += "-DCMAKE_BUILD_TYPE=RelWithDebInfo" }
            }
        }
    }
    externalNativeBuild {
        cmake { path = file("../CMakeLists.txt"); version = "3.22.1" }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
    }
    kotlinOptions { jvmTarget = "21" }
}

dependencies {
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
}
