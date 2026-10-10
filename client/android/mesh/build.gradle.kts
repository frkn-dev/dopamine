plugins {
    id(libs.plugins.android.library.get().pluginId)
    id(libs.plugins.kotlin.android.get().pluginId)
    id("kotlin-parcelize")
}

kotlin {
    jvmToolchain(17)
}

tasks.withType<org.jetbrains.kotlin.gradle.tasks.KotlinCompile>().configureEach {
    kotlinOptions.freeCompilerArgs += "-opt-in=kotlin.ExperimentalStdlibApi"
}

android {
    namespace = "com.bitchat.android"
    // vendored southernstorm Noise stack is .java living next to the .kt files
    sourceSets["main"].java.srcDirs("src/main/kotlin")
}

dependencies {
    implementation(libs.androidx.core)
    implementation(libs.kotlinx.coroutines)
    implementation(libs.androidx.security.crypto)
    implementation(libs.androidx.lifecycle.process)
    implementation(libs.gson)
    implementation(libs.bouncycastle)
}
