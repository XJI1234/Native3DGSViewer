#pragma once

#include "../common/protocol.h"

#include <span>

namespace gs::io::detail
{

struct RawSplat
{
    const float *position;
    const float *logScale;
    const float *rotationXyzw;
    float logitAlpha;
    const float *dc;
    const float *sh;
    bool quantizedAlpha = false;
};

class SceneWriter
{
  public:
    SceneWriter(SceneHeader *header, bool rdf);
    bool write(uint64_t index, const RawSplat &raw);
    bool finish();

  private:
    SceneHeader *header_;
    bool rdf_;
    double min_[3]{INFINITY, INFINITY, INFINITY};
    double max_[3]{-INFINITY, -INFINITY, -INFINITY};
    double maxScale_ = 0;
};

bool validate_scene(const SceneHeader &header, uint64_t mappingBytes);

} // namespace gs::io::detail
