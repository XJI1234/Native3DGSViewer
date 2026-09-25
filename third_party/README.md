# Pinned dependencies

Initialize with `git submodule update --init --recursive` before configuring CMake.

| Dependency | Revision | License | Use |
|---|---|---|---|
| miniply | `1a235c70390fadf789695c9ccbf285ae712416b3` | MIT (`LICENSE.md`) | PLY parsing; generated `FILE*` constructor adapter in CMake |
| Niantic SPZ | `5bf2945de1a003cee07133b1e495fe9c6ffdc7e7` (v3.0.0) | MIT (`LICENSE`) | SPZ v1-v4 decoding |
| GoogleTest | `52eb8108c5bdec04579160ae17225d66034bd723` (v1.17.0) | BSD-3-Clause (`LICENSE`) | Unit and integration tests |
| zlib | `51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf` (v1.3.1) | zlib (`LICENSE`) | Legacy SPZ gzip and preflight |
| zstd | `794ea1b0afca0f020f4e57b6732332231fb23c70` (v1.5.6) | BSD-3-Clause/GPL-2.0 (`LICENSE`) | SPZ v4 streams, static library |

Both compression libraries are built from pinned submodules. Release packaging
must include their license notices.
