# 初步跨平台支持

本轮增加原生构建入口，未在 Windows 上交叉编译 macOS 或 Linux。
当前使用 Windows + Qt 6.11.1 验证；macOS、Linux 尚需对应系统实测。
建议使用 Qt 6，安装 Widgets、LinguistTools 和测试组件；旧 Qt 5 分支不在本次验收范围内。

## 资源与平台行为

| 项目 | Windows | macOS | Linux |
| --- | --- | --- | --- |
| 窗口图标 | 内嵌多尺寸 PNG | 内嵌多尺寸 PNG | 内嵌多尺寸 PNG |
| 系统图标 | EXE 内嵌 ICO | .app 内含 ICNS | hicolor PNG / SVG |
| 桌面集成 | 现有 EXE | 固定 bundle ID、关于/退出菜单角色 | .desktop 与窗口 desktopFileName 匹配 |
| 翻译 | 内嵌应用及可用的 Qt 中文目录 | 同左 | 同左 |
| 发布入口 | package_windows.ps1 | package_unix.sh，部署 Qt 到 .app | package_unix.sh，依赖系统 Qt |

设置继续使用 QSettings 的系统用户目录，文件操作继续使用 Qt 路径接口和 QSaveFile。
快捷键使用 Qt 的平台映射，macOS 上 Ctrl 快捷键映射为 Command。
Linux 桌面入口只启动程序，暂不声明 JSON 文件关联；命令行文件打开和 macOS Finder 文件打开事件尚未接入。

## 本机构建和安装

安装 C++17 编译器、CMake 和对应系统/架构的 Qt 6。macOS 还需 Xcode 命令行工具。

```sh
cmake -S . -B out/build/native -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt -DBUILD_TESTING=ON
cmake --build out/build/native --parallel
ctest --test-dir out/build/native --output-on-failure
cmake --install out/build/native --prefix "$HOME/.local"
```

Linux 安装后确认 `$HOME/.local/bin` 在 PATH 中，桌面环境可能需重新登录才显示图标。
macOS 安装产物为 `hellojson.app`；单纯 install 尚未复制 Qt，分发请使用下面的脚本。

```sh
QT_ROOT=/path/to/Qt bash scripts/package_unix.sh
```

脚本先构建并执行测试，再安装到独立暂存目录、启动冒烟检查并生成压缩包。
macOS 使用 Qt 自带 macdeployqt；产物尚未进行开发者签名和公证。
Linux 包包含 bin/share，安装到用户前缀即可；不是自包含 AppImage，需要兼容的 Qt 库、平台插件及系统图形依赖。
Linux 应在计划支持的最旧发行版上构建，以免依赖过新的 glibc。

## 目标平台验收仍需完成

- macOS Intel / Apple Silicon 分别构建，检查菜单、中文、Retina 图标、文件对话框和打包后的 Qt 插件。
- Linux 在 X11 / Wayland 检查图标、剪贴板、输入法、对话框及桌面启动；干净环境核对运行库。
- 三个平台回归未保存保护、安全保存、大文件分页、任务取消和退出。
- 现有压力测试的进程内存/句柄统计主要针对 Windows，非 Windows 的空统计不能作为 512MB 验收证据；需要补齐平台采样后重新测量。
- macOS 签名、公证和 Linux 自包含打包留待正式发布阶段处理。
