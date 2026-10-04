# Third-party notices

Runtime native decoder sources are compiled from repository-pinned revisions:

| Dependency | Revision | License |
| --- | --- | --- |
| Niantic SPZ | 5bf2945de1a003cee07133b1e495fe9c6ffdc7e7 | MIT |
| zlib | 51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf | zlib |
| Zstandard | 794ea1b0afca0f020f4e57b6732332231fb23c70 | BSD-3-Clause option |

Their full license texts must be included in SDK assets/licenses by the packaging script. React/Vue are optional peers, supplied by the host. WebGPU typings are BSD-3-Clause. SparkJS and Three.js are development/reference dependencies, not bundled into the Web engine. Exact JavaScript dependencies and integrity fields are in pnpm-lock.yaml.

This file does not assign a license to the repository's own code. Public distribution terms require the repository owner's license decision; this task creates a local consumable SDK and does not publish npm packages.
