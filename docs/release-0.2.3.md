# Native3DGS 0.2.3 preview

Windows x64 now selects between the original DX12 sorter and an optional CUDA sorter automatically. The updated Windows viewer installer and C++ SDK include the CUDA backend and its notices. Android, Cloud and Web packages reuse the existing builds unchanged and retain their original versioned filenames.

| Product | Release file | Build |
| --- | --- | --- |
| Local Windows viewer | `Native3DGSViewer-0.2.3-Windows-x64-Setup.exe` | New 0.2.3, includes optional CUDA |
| Windows C++ SDK | `Native3DGS-SDK-0.2.3-Windows-x64.zip` | New 0.2.3, includes optional CUDA |
| Android viewer | `Native3DGSViewer-0.2.2-Android-arm64-v8a-preview.apk` | Unchanged from v0.2.2 |
| Android SDK | `Native3DGS-SDK-0.2.2-Android-arm64-v8a.zip` | Unchanged from v0.2.2 |
| Cloud Windows viewer | `Native3DGSCloudViewer-0.2.2-Windows-x64-Setup.exe` | Unchanged from the v0.2.2 supplement |
| WebGPU SDK | `Native3DGS-SDK-0.2.2-Web-WebGPU-preview.4.zip` | Unchanged preview.4 |
| React source template | `Native3DGS-Template-0.2.2-Web-React-preview.4.zip` | Unchanged preview.4 |
| Vue source template | `Native3DGS-Template-0.2.2-Web-Vue-preview.4.zip` | Unchanged preview.4 |

## Windows changes

- Probe CUDA support on the actual DX12 adapter using LUID matching and a real kernel. Scenes below 2,000,000 rendered points retain DX12; larger scenes use full 32-bit stable CUB radix sorting with CUDA Graph when the provider is usable.
- Shared GPU buffers are created and mapped per scene; sorting arrays do not travel through CPU memory each frame. Graph capture reduces CPU submission overhead. Static views retain the existing projection/sort reuse.
- Show “已启用 CUDA 加速” in the upper-right viewport corner only after a frame completes for an active CUDA scene. Small scenes, cleared scenes and recovery hide the indicator.
- Retain DX12 when the optional DLL is absent or incompatible, the selected adapter is unsupported, or CUDA scene preparation fails. A CUDA submission failure disables CUDA for that renderer and restores the scene through DX12 device recovery. Diagnostic configurations using the D3D12 Debug Layer use DX12.
- Fix premature DeviceRestored notification while a retained scene is still being reuploaded.
- Include the optional CUDA DLL and notices in the Windows SDK; `native3dgs_deploy_runtime` deploys them when present. Fix CUDA component installation with Windows toolkit paths. SDK hosts need no CUDA headers, toolkit or CUDA link dependency.

`RenderStats` adds `cuda_available` and `cuda_sort_active`. C++ SDK consumers must rebuild using the matching 0.2.3 headers and static libraries. Existing DX12-only deployments remain supported.

## Verification and limits

The implementation passed 14/14 CTest groups, including installed SDK, and five isolated production CUDA integration tests with no skips. Tests cover scene switching, static reuse, picking, cancellation, clearing, preparation failure, submission failure and device recovery. A process without the CUDA DLL renders the complete two-million-point scene through DX12. Real desktop runs confirmed small-model DX12 and large-model CUDA selection and normal shutdown; badge state was checked through application logs, without pixel inspection.

On the RTX 3080 10GB test system, the short paired tests reduced complete GPU span by about 5% at 2.1 million points and 10% at 3.9 million points. Smaller scenes generally favor DX12; the 1.25-million-point region is near crossover. Interop variants and the tested stable parallel merge-sort replacement did not provide consistent gains and were removed. All 162 experimental outputs match their respective DX12 references byte for byte; 158 runs contribute valid timing after excluding one contaminated block.

These are local GPU diagnostics, not a cross-device FPS guarantee. The two-million-point threshold is calibrated on RTX 3080. The backend targets SM86 plus compatible PTX, with actual runtime probing; two-hour stability and a broader GPU/driver matrix remain open. Known Debug Layer external-fence handle retention remains unresolved. The installer is unsigned. Android remains a development-signed preview; unchanged Cloud and Web packages retain their previous requirements and limitations.

See [the optimization report](windows-cuda-adaptive-sort-2026-10-28.md), [SDK guide](SDK-guide.md), and [release verification](release-0.2.3-verification.md). Original reused assets and their source provenance remain documented in [v0.2.2](https://github.com/XJI1234/Native3DGSViewer/releases/tag/v0.2.2). No standalone SHA256 files are attached.
