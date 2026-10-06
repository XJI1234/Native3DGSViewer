#pragma once
#include "../../src/model-io/common/probe.h"
#ifdef __EMSCRIPTEN_PTHREADS__
#define GS_DECODER_LOCAL thread_local
#else
#define GS_DECODER_LOCAL
#endif
extern GS_DECODER_LOCAL gs::io::detail::SceneHeader *gs_output;
