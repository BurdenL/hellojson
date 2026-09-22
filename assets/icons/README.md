# HelloJson 图标

深蓝圆角底、白色花括号、青绿色冒号。不依赖字体或文字，适合小尺寸识别。

- hellojson.svg：矢量母版，可无限缩放。
- hellojson.ico：16、20、24、32、40、48、64、128、256 像素，多尺寸 Windows 图标。
- hellojson-*.png：以上尺寸及 512、1024 像素透明背景图片。
- 应用使用内嵌 PNG 自动选择适合高 DPI 的尺寸；Windows EXE 使用 ICO 资源。

macOS 使用 hellojson.icns，包含 128、256、512、1024 像素 PNG 表示。
Linux 安装 PNG/SVG 到 hicolor 图标主题目录。

修改 SVG 后，用 scripts/generate_icons.cpp 重新生成 PNG、ICO 和 ICNS。
该工具依赖 Qt Core、Gui、Svg；普通应用无需新增 SVG 链接依赖。
图标为本项目原创矢量资源，遵循项目 LICENSE。
