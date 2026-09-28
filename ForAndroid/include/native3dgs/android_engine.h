#ifndef NATIVE3DGS_ANDROID_ENGINE_H
#define NATIVE3DGS_ANDROID_ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GS_ANDROID_API_VERSION 2u

typedef struct gs_android_engine gs_android_engine_t;

typedef enum gs_android_result {
    GS_ANDROID_OK = 0,
    GS_ANDROID_INVALID_ARGUMENT = 1,
    GS_ANDROID_CLOSED = 2,
    GS_ANDROID_IO_ERROR = 3,
    GS_ANDROID_UNSUPPORTED = 4,
    GS_ANDROID_OUT_OF_MEMORY = 5
} gs_android_result_t;

typedef enum gs_android_phase {
    GS_ANDROID_EMPTY = 0,
    GS_ANDROID_LOADING = 1,
    GS_ANDROID_UPLOADING = 2,
    GS_ANDROID_READY = 3,
    GS_ANDROID_RECOVERING = 4,
    GS_ANDROID_FAILED = 5,
    GS_ANDROID_STOPPING = 6,
    GS_ANDROID_STOPPED = 7
} gs_android_phase_t;

typedef enum gs_android_camera_action {
    GS_ANDROID_ORBIT = 0,
    GS_ANDROID_PAN = 1,
    GS_ANDROID_DOLLY = 2,
    GS_ANDROID_LOOK = 3,
    GS_ANDROID_FLY = 4,
    GS_ANDROID_FIT = 5,
    GS_ANDROID_RESET = 6,
    GS_ANDROID_ORBIT_MODE = 7,
    GS_ANDROID_FLY_MODE = 8,
    GS_ANDROID_FLIP_AXES = 9
} gs_android_camera_action_t;

typedef enum gs_android_quality_mode {
    GS_ANDROID_QUALITY_FULL = 0,
    GS_ANDROID_QUALITY_MOBILE = 1
} gs_android_quality_mode_t;

typedef struct gs_android_config {
    uint32_t struct_size;
    uint32_t api_version;
    uint64_t max_input_bytes;
    uint64_t max_scene_bytes;
    const char *temporary_directory;
} gs_android_config_t;

typedef struct gs_android_snapshot {
    uint32_t struct_size;
    uint32_t phase;
    uint64_t request_id;
    uint64_t active_request_id;
    uint64_t upload_ticket;
    uint64_t surface_generation;
    uint64_t scene_count;
    uint32_t error;
    uint32_t reserved;
    uint64_t frames_presented;
    uint64_t last_frame_us;
    uint32_t quality_mode;
    uint32_t requested_scale_milli;
    uint32_t actual_scale_milli;
    uint32_t quality_reserved;
    uint64_t active_splats;
    uint64_t last_gpu_frame_id;
    uint64_t last_gpu_project_us;
    uint64_t last_gpu_sort_us;
    uint64_t last_gpu_draw_us;
    uint64_t dropped_frame_samples;
} gs_android_snapshot_t;

/* GPU timings describe gpu_frame_id, which may lag frame_id by one frame. */
typedef struct gs_android_frame_sample {
    uint64_t frame_id;
    uint64_t present_call_ns;
    uint64_t cpu_frame_us;
    uint64_t gpu_frame_id;
    uint64_t gpu_project_us;
    uint64_t gpu_sort_us;
    uint64_t gpu_draw_us;
    uint64_t submitted_splats;
    uint32_t width;
    uint32_t height;
} gs_android_frame_sample_t;

typedef struct gs_android_frame_sample_v2 {
    uint32_t struct_size;
    uint32_t quality_mode;
    gs_android_frame_sample_t frame;
    uint64_t source_splats;
    uint64_t active_splats;
    uint32_t requested_scale_milli;
    uint32_t actual_scale_milli;
} gs_android_frame_sample_v2_t;

uint32_t gs_android_api_version(void);
gs_android_result_t gs_android_create(const gs_android_config_t *config,
                                      gs_android_engine_t **out_engine);
void gs_android_destroy(gs_android_engine_t *engine);

/* On success the engine duplicates fd before returning. Caller retains its fd. */
gs_android_result_t gs_android_open_fd(gs_android_engine_t *engine, int fd,
                                       uint64_t *request_id);
/* Accepts the versioned read-only shared scene produced by the decoder Service. */
gs_android_result_t gs_android_open_shared_fd(gs_android_engine_t *engine, int fd,
                                              uint64_t *request_id);
gs_android_result_t gs_android_cancel(gs_android_engine_t *engine, uint64_t request_id);
gs_android_result_t gs_android_close_scene(gs_android_engine_t *engine);

/* window is ANativeWindow*. The engine acquires its own reference before returning. */
gs_android_result_t gs_android_attach_surface(gs_android_engine_t *engine, void *window,
                                              uint64_t generation);
gs_android_result_t gs_android_detach_surface(gs_android_engine_t *engine,
                                              uint64_t generation);
gs_android_result_t gs_android_resize(gs_android_engine_t *engine, uint64_t generation,
                                      uint64_t revision, uint32_t width, uint32_t height);
/* x/y are physical pixels for drag, steps for dolly, or local movement for fly;
   seconds is used only by fly. FLIP_AXES uses x as an integer bitmask 1/2/4. */
gs_android_result_t gs_android_camera(gs_android_engine_t *engine,
                                      gs_android_camera_action_t action,
                                      double x, double y, double z, double seconds);
/* display_width/height are the final SurfaceView size in physical pixels. */
gs_android_result_t gs_android_set_quality(gs_android_engine_t *engine,
    gs_android_quality_mode_t mode, uint32_t requested_scale_milli,
    uint32_t display_width, uint32_t display_height);
gs_android_result_t gs_android_get_snapshot(gs_android_engine_t *engine,
                                            gs_android_snapshot_t *snapshot);
/* Drains up to capacity samples without waiting for GPU work. */
gs_android_result_t gs_android_drain_frame_samples(gs_android_engine_t *engine,
    gs_android_frame_sample_t *samples, uint32_t capacity, uint32_t *count);
/* Writes at most sample_stride bytes per element; preserves the v1 drain ABI. */
gs_android_result_t gs_android_drain_frame_samples_v2(gs_android_engine_t *engine,
    void *samples, uint32_t sample_stride, uint32_t capacity, uint32_t *count);

#ifdef __cplusplus
}
#endif

#endif
