#include "model-io/fd_loader.h"
#include "render-core/surface_probe.h"
#include "render-core/scene_renderer.h"

#include <jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <unistd.h>
#include <array>

namespace
{
jobject decode(JNIEnv *env, jint fd, jlong available, jstring directory, jobject observer)
{
    if (!directory || !observer || available <= 0)
        return nullptr;
    const char *path = env->GetStringUTFChars(directory, nullptr);
    if (!path)
        return nullptr;
    gs::android::io::FdLoadRequest request{fd};
    try
    {
        request.temporary_directory = path;
    }
    catch (...)
    {
        env->ReleaseStringUTFChars(directory, path);
        return nullptr;
    }
    env->ReleaseStringUTFChars(directory, path);
    request.available_memory_bytes = static_cast<uint64_t>(available);
    jclass observer_class = env->GetObjectClass(observer);
    if (!observer_class)
        return nullptr;
    const jmethodID cancel = env->GetMethodID(observer_class, "cancelled", "()Z");
    const jmethodID progress = env->GetMethodID(observer_class, "progress", "(IJJ)V");
    env->DeleteLocalRef(observer_class);
    if (!cancel || !progress || env->ExceptionCheck())
        return nullptr;
    auto result = gs::android::io::load_shared_fd(request, [&] {
        if (env->ExceptionCheck())
            return true;
        const bool stopped = env->CallBooleanMethod(observer, cancel);
        return stopped || env->ExceptionCheck();
    }, [&](const gs::io::LoadProgress &value) {
        if (env->ExceptionCheck())
            return;
        env->CallVoidMethod(observer, progress, static_cast<jint>(value.stage),
                             static_cast<jlong>(value.bytesRead),
                             value.totalBytes ? static_cast<jlong>(*value.totalBytes) : -1);
    });
    if (env->ExceptionCheck())
        return nullptr;
    int transfer_fd = -1;
    int error = -1;
    int stage = static_cast<int>(gs::io::LoadStage::Ready);
    std::string diagnostic;
    if (auto *scene = std::get_if<gs::android::io::SharedScene>(&result))
        transfer_fd = scene->duplicate_fd();
    else
    {
        const auto &failure = std::get<gs::io::LoadError>(result);
        error = static_cast<int>(failure.code);
        stage = static_cast<int>(failure.stage);
        diagnostic = failure.diagnostic;
    }
    if (transfer_fd < 0 && error < 0)
    {
        error = static_cast<int>(gs::io::LoadErrorCode::IoFailure);
        diagnostic = "Shared scene descriptor duplication";
    }
    jclass result_class = env->FindClass("org/native3dgs/sdk/DecodeResult");
    jobject response = nullptr;
    if (result_class)
    {
        const auto constructor = env->GetMethodID(result_class, "<init>", "(IIILjava/lang/String;)V");
        jstring detail = env->NewStringUTF(diagnostic.c_str());
        if (constructor && detail && !env->ExceptionCheck())
            response = env->NewObject(result_class, constructor, transfer_fd, error, stage, detail);
        if (detail) env->DeleteLocalRef(detail);
        env->DeleteLocalRef(result_class);
    }
    if (!response && transfer_fd >= 0)
        close(transfer_fd);
    return response;
}
} // namespace

extern "C" JNIEXPORT jobject JNICALL
Java_org_native3dgs_sdk_NativeDecoder_decode(JNIEnv *env, jobject, jint fd,
                                             jlong available, jstring directory,
                                             jobject observer)
{
    try
    {
        return decode(env, fd, available, directory, observer);
    }
    catch (...)
    {
        if (!env->ExceptionCheck())
        {
            jclass error = env->FindClass("java/lang/IllegalStateException");
            if (error)
            {
                env->ThrowNew(error, "Native decoder boundary failure");
                env->DeleteLocalRef(error);
            }
        }
        return nullptr;
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_org_native3dgs_sdk_NativeDecoder_validateScene(JNIEnv *, jobject, jint fd, jlong limit)
{
    if (limit <= 0)
        return -1;
    try
    {
        const auto result = gs::android::io::import_shared_fd(fd, static_cast<uint64_t>(limit));
        if (const auto *scene = std::get_if<gs::SceneHandle>(&result))
            return static_cast<jlong>((*scene)->count);
    }
    catch (...) { }
    return -1;
}

extern "C" JNIEXPORT jint JNICALL
Java_org_native3dgs_sdk_NativeDecoder_probeSurface(JNIEnv *env, jobject, jobject surface,
                                                   jobject callback)
{
    ANativeWindow *window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    if (!window)
        return -1;
    jclass callback_class = callback ? env->GetObjectClass(callback) : nullptr;
    const jmethodID run = callback_class ? env->GetMethodID(callback_class, "run", "()V") : nullptr;
    if (callback_class) env->DeleteLocalRef(callback_class);
    if (!run || env->ExceptionCheck())
    {
        ANativeWindow_release(window);
        return -1;
    }
    const auto result = gs::android::render::probe_surface(window, [&] {
        env->CallVoidMethod(callback, run);
    });
    ANativeWindow_release(window);
    __android_log_print(ANDROID_LOG_INFO, "Native3DGS", "Surface presented=%d result=%d stage=%s",
                        result.presented, result.result, result.diagnostic.c_str());
    return result.presented ? 0 : result.result;
}

extern "C" JNIEXPORT jint JNICALL
Java_org_native3dgs_sdk_NativeDecoder_probeSplatSurface(JNIEnv *env, jobject, jobject surface,
                                                         jobject callback)
{
    ANativeWindow *window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    if (!window) return -1;
    jint result = -1;
    try
    {
        struct Fixture
        {
            std::array<float, 6> centers{0, 0, 0, 0, 0, -0.4f};
            std::array<float, 6> scales{0.35f, 0.35f, 0.35f,
                                         0.35f, 0.35f, 0.35f};
            std::array<float, 8> rotations{0, 0, 0, 1, 0, 0, 0, 1};
            std::array<float, 2> opacity{0.9f, 0.9f};
            std::array<float, 6> colors{1, 0, 0, 0, 0, 1};
        };
        auto values = std::make_shared<Fixture>();
        auto scene = std::make_shared<gs::SplatScene>();
        scene->count = 2;
        scene->bounds = {{-0.35, -0.35, -0.75}, {0.35, 0.35, 0.35}};
        scene->maxScale = 0.35;
        scene->centerLocal = values->centers;
        scene->scale = values->scales;
        scene->rotation = values->rotations;
        scene->opacity = values->opacity;
        scene->rgb0 = values->colors;
        scene->storage = values;
        gs::android::render::SceneRenderer renderer(window);
        std::string diagnostic;
        if (renderer.upload(scene, diagnostic))
        {
            gs::android::engine::CameraPose camera;
            camera.position = {0, 0, 3};
            const auto frame = renderer.render(camera);
            result = frame.status == gs::android::render::FrameStatus::Presented
                ? 0 : frame.platform_result ? frame.platform_result : -2;
            diagnostic = frame.diagnostic;
            if (result == 0 && callback)
            {
                jclass type = env->GetObjectClass(callback);
                const auto run = type ? env->GetMethodID(type, "run", "()V") : nullptr;
                if (run && !env->ExceptionCheck()) env->CallVoidMethod(callback, run);
                if (type) env->DeleteLocalRef(type);
            }
        }
        __android_log_print(ANDROID_LOG_INFO, "Native3DGS", "Splat frame=%d stage=%s",
                            result, diagnostic.c_str());
    }
    catch (const std::exception &error)
    {
        __android_log_print(ANDROID_LOG_ERROR, "Native3DGS", "Splat probe: %s", error.what());
    }
    ANativeWindow_release(window);
    return result;
}
