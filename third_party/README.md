# Pinned dependencies

Initialize with `git submodule update --init --recursive` before configuring CMake.

| Dependency | Revision | License | Use |
|---|---|---|---|
| miniply | `1a235c70390fadf789695c9ccbf285ae712416b3` | MIT (`LICENSE.md`) | Historical PLY parser reference; no longer linked by the streaming decoder |
| Niantic SPZ | `5bf2945de1a003cee07133b1e495fe9c6ffdc7e7` (v3.0.0) | MIT (`LICENSE`) | SPZ v1-v4 decoding |
| GoogleTest | `52eb8108c5bdec04579160ae17225d66034bd723` (v1.17.0) | BSD-3-Clause (`LICENSE`) | Unit and integration tests |
| zlib | `51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf` (v1.3.1) | zlib (`LICENSE`) | Legacy SPZ gzip and preflight |
| zstd | `794ea1b0afca0f020f4e57b6732332231fb23c70` (v1.5.6) | BSD-3-Clause/GPL-2.0 (`LICENSE`) | SPZ v4 streams, static library |
| FidelityFX Parallel Sort | `0c539948c8d196ae338d91efbc8ca495f1ea0d1d` | MIT (`LICENSE.txt`) | Stable GPU key/value radix, eight 4-bit passes, SM6 wave operations |

Both compression libraries are built from pinned submodules. Release packaging
must include their license notices.

Parallel Sort is consumed through its original header; the sample engine is not
built. `shaders/parallel_sort.hlsl` binds our resources to upstream kernels.
Key/value buffers are padded to complete 512-key blocks because upstream
prefetches the last block before masking its inactive entries. NVIDIA wave32
is tested; other wave sizes require hardware verification.
