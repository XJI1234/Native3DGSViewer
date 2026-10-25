# preview.4 Release evidence

Native tag `v0.2.2` was preserved. The three Web preview.4 assets were uploaded to its existing GitHub Release, then the three preview.3 Web assets were deleted. Existing Windows, Android, Cloud assets and the Cloud standalone SHA256 asset were preserved. No new standalone SHA256 assets were uploaded.

All downloaded ZIPs passed `ZipFile.testzip()`, every entry in each internal `MANIFEST.json` matched its recorded SHA-256, and all versions were `0.2.2-preview.4`.

| Asset | Bytes | Downloaded SHA-256 | Source commit |
| --- | ---: | --- | --- |
| `Native3DGS-SDK-0.2.2-Web-WebGPU-preview.4.zip` | 996,233 | `7155065593376060f781851ab0621f8e77eee85645072f406c241528b3b871af` | [`4fa4a30`](https://github.com/XJI1234/Native3DGSViewer/commit/4fa4a30f50eebe3f3cfc22f54a3bc036c693ea6e) |
| `Native3DGS-Template-0.2.2-Web-React-preview.4.zip` | 523,675 | `940c64227adb78be03ef65e0cabb535412250627c621b9f0ae50440e42b838b9` | [`38ff52f`](https://github.com/XJI1234/Native3DGSViewer/commit/38ff52ffc0daaf1c7908c0c519afbd57a0b1ba84) |
| `Native3DGS-Template-0.2.2-Web-Vue-preview.4.zip` | 524,023 | `52a9dda663b30ece38c4b6bc168f0c415f7a4756d90c6f0273c4b675d24b7a4e` | [`38ff52f`](https://github.com/XJI1234/Native3DGSViewer/commit/38ff52ffc0daaf1c7908c0c519afbd57a0b1ba84) |

The React and Vue archives embed the exact SDK tarball SHA recorded by the SDK archive manifest. The SDK archive was built from commit `4fa4a30f50eebe3f3cfc22f54a3bc036c693ea6e`; the template archives were built from commit `38ff52ffc0daaf1c7908c0c519afbd57a0b1ba84`. Release page: [v0.2.2](https://github.com/XJI1234/Native3DGSViewer/releases/tag/v0.2.2).
