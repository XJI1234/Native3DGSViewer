#include <native3dgs/android_engine.h>

int main(void)
{
    gs_android_config_t config = {0};
    config.struct_size = sizeof(config);
    config.api_version = GS_ANDROID_API_VERSION;
    gs_android_engine_t *engine = 0;
    if (gs_android_create(&config, &engine) != GS_ANDROID_OK) return 1;
    gs_android_snapshot_t snapshot = {0};
    snapshot.struct_size = sizeof(snapshot);
    int result = gs_android_get_snapshot(engine, &snapshot) == GS_ANDROID_OK &&
                 snapshot.phase == GS_ANDROID_EMPTY ? 0 : 2;
    gs_android_destroy(engine);
    return result;
}
