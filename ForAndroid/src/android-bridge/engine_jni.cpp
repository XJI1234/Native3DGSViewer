#include "native3dgs/android_engine.h"

#include <android/native_window_jni.h>
#include <jni.h>

namespace
{
gs_android_engine_t *engine(jlong value)
{
    return reinterpret_cast<gs_android_engine_t *>(value);
}
}

extern "C" JNIEXPORT jlong JNICALL
Java_org_native3dgs_sdk_EngineBridge_create(JNIEnv *env, jobject, jstring directory)
{
    if (!directory) return 0;
    const char *path = env->GetStringUTFChars(directory, nullptr);
    if (!path) return 0;
    gs_android_config_t config{sizeof(gs_android_config_t), GS_ANDROID_API_VERSION,
                               0, 0, path};
    gs_android_engine_t *result = nullptr;
    const auto status = gs_android_create(&config, &result);
    env->ReleaseStringUTFChars(directory, path);
    return status == GS_ANDROID_OK ? reinterpret_cast<jlong>(result) : 0;
}

extern "C" JNIEXPORT void JNICALL
Java_org_native3dgs_sdk_EngineBridge_destroy(JNIEnv *, jobject, jlong handle)
{
    gs_android_destroy(engine(handle));
}

extern "C" JNIEXPORT jlong JNICALL
Java_org_native3dgs_sdk_EngineBridge_openFd(JNIEnv *, jobject, jlong handle, jint fd,
                                            jboolean shared)
{
    uint64_t request = 0;
    const auto status = shared ? gs_android_open_shared_fd(engine(handle), fd, &request)
                               : gs_android_open_fd(engine(handle), fd, &request);
    return status == GS_ANDROID_OK ? static_cast<jlong>(request) : -static_cast<jlong>(status);
}

extern "C" JNIEXPORT jint JNICALL
Java_org_native3dgs_sdk_EngineBridge_cancel(JNIEnv *, jobject, jlong handle, jlong request)
{
    return gs_android_cancel(engine(handle), static_cast<uint64_t>(request));
}

extern "C" JNIEXPORT jint JNICALL
Java_org_native3dgs_sdk_EngineBridge_closeScene(JNIEnv *, jobject, jlong handle)
{
    return gs_android_close_scene(engine(handle));
}

extern "C" JNIEXPORT jint JNICALL
Java_org_native3dgs_sdk_EngineBridge_attach(JNIEnv *env, jobject, jlong handle,
                                              jobject surface, jlong generation)
{
    ANativeWindow *window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    if (!window) return GS_ANDROID_INVALID_ARGUMENT;
    const auto status = gs_android_attach_surface(engine(handle), window,
                                                   static_cast<uint64_t>(generation));
    ANativeWindow_release(window);
    return status;
}

extern "C" JNIEXPORT jint JNICALL
Java_org_native3dgs_sdk_EngineBridge_detach(JNIEnv *, jobject, jlong handle, jlong generation)
{
    return gs_android_detach_surface(engine(handle), static_cast<uint64_t>(generation));
}

extern "C" JNIEXPORT jint JNICALL
Java_org_native3dgs_sdk_EngineBridge_resize(JNIEnv *, jobject, jlong handle, jlong generation,
                                              jlong revision, jint width, jint height)
{
    return gs_android_resize(engine(handle), static_cast<uint64_t>(generation),
                             static_cast<uint64_t>(revision), static_cast<uint32_t>(width),
                             static_cast<uint32_t>(height));
}

extern "C" JNIEXPORT jint JNICALL
Java_org_native3dgs_sdk_EngineBridge_camera(JNIEnv *, jobject, jlong handle, jint action,
                                              jdouble x, jdouble y, jdouble z, jdouble seconds)
{
    return gs_android_camera(engine(handle), static_cast<gs_android_camera_action_t>(action),
                             x, y, z, seconds);
}

extern "C" JNIEXPORT jlongArray JNICALL
Java_org_native3dgs_sdk_EngineBridge_snapshot(JNIEnv *env, jobject, jlong handle)
{
    gs_android_snapshot_t snapshot{};
    snapshot.struct_size = sizeof(snapshot);
    if (gs_android_get_snapshot(engine(handle), &snapshot) != GS_ANDROID_OK) return nullptr;
    const jlong values[] = {snapshot.phase, static_cast<jlong>(snapshot.request_id),
        static_cast<jlong>(snapshot.active_request_id), static_cast<jlong>(snapshot.upload_ticket),
        static_cast<jlong>(snapshot.surface_generation), static_cast<jlong>(snapshot.scene_count),
        snapshot.error, static_cast<jlong>(snapshot.frames_presented),
        static_cast<jlong>(snapshot.last_frame_us)};
    auto result = env->NewLongArray(9);
    if (result) env->SetLongArrayRegion(result, 0, 9, values);
    return result;
}
