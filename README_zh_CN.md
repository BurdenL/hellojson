# Hello JSON

基于 Qt 的轻量级 JSON 格式化与查看工具。

[English](README.md)

## 功能

- **格式化与压缩** — 带缩进美化输出或单行紧凑输出
- **树形视图** — 以可展开的树形结构浏览 JSON（键 / 类型 / 值）
- **多标签页** — 支持同时打开多个 JSON 文件，浏览器风格标签
- **文本搜索** — 在 JSON 内容中查找，高亮所有匹配并支持导航
- **右键菜单** — 右键树节点复制其值或 JSON 路径
- **多语言** — 支持中文和英文，运行时即时切换
- **语法高亮** — JSON 感知的颜色高亮（字符串、数字、关键字）
- **展开/折叠全部** — 一键操作，方便处理大型 JSON 文件

## 界面

```
┌──────────────────────────────────────────────────────────┐
│ [格式化] [压缩] [清空]                                   │
├──────────────────────────────────────────────────────────┤
│ ┌──────────────┬──────────────────────────────────────┐ │
│ │ {            │  键       │ 类型   │ 值              │ │
│ │   "store": { │  ▼ (根)  │ 对象   │ {1 个成员}      │ │
│ │     "book": [│    store  │ 对象   │ {1 个成员}      │ │
│ │       {      │      book │ 数组   │ [2 个元素]      │ │
│ │         ...  │        [0]│ 对象   │ {2 个成员}      │ │
│ │       }      │          │        │                 │ │
│ │     ]        │          │        │                 │ │
│ │   }          │          │        │                 │ │
│ │ }            │          │        │                 │ │
│ └──────────────┴──────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
```

## 快捷键

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+F` | 格式化 JSON |
| `Ctrl+Shift+C` | 压缩 JSON |
| `Ctrl+Shift+F` | 搜索 JSON |
| `Enter` / `Shift+Enter` | 下一个 / 上一个匹配 |
| `Ctrl+O` | 打开文件 |
| `Ctrl+S` | 保存文件 |
| `Ctrl+T` | 新建标签页 |
| `Ctrl+W` | 关闭标签页 |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | 下一个 / 上一个标签页 |
| `Ctrl+Shift+E` | 展开全部节点 |
| `Ctrl+Shift+R` | 折叠全部节点 |
| `Ctrl+L` | 清空 |
| `Escape` | 关闭搜索栏 |

## 构建

### 依赖

- Qt 6.2+（Widgets + LinguistTools）
- CMake 3.16+
- C++17 编译器（GCC / MinGW-w64 / MSVC / Clang）

### 环境配置

设置 `QTDIR` 环境变量指向你的 Qt 安装路径，或在 `CMakeUserPresets.json` 中
修改 `CMAKE_PREFIX_PATH`。

参考 `CMakeUserPresets.json.example` 模板文件。

### 编译

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### 打包发布（Windows）

```bash
windeployqt hellojson.exe
```

## 许可证

本项目使用 **MIT License** — 详见 [LICENSE](LICENSE)。

Qt 基于 **LGPL v3** 使用 — 第三方声明详见 [NOTICE](NOTICE)。
