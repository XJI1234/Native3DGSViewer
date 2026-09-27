# Kotlin consumer

这是仅依赖 SDK 工件的 Android library 消费工程。打包后从本目录运行
`gradlew -p . assembleDebug`，可验证 AAR 公开 Kotlin API 能独立编译。此工程
产出 library，因此以 `compileOnly` 引入本地 AAR；实际 App 必须另外使用
`implementation(files("libs/native3dgs-sdk-release.aar"))` 提供运行时类、原生库和 Service。
真实应用使用 `ACTION_OPEN_DOCUMENT` 取得 `Uri`，将 `SurfaceView.holder.surface`
传给 `attach`，保存返回的 generation；尺寸变化时调用 `resize`，Surface 销毁时调用
`detach`，宿主退出时调用 `close` 或在后台协程调用 `closeAndWait`。
具体调用序列见 `docs/SDK-guide.md`。
