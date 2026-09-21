# Hello JSON

Files larger than 2 MiB now open in read-only paged mode automatically.
Ctrl+Shift+O forces this mode for any file. It displays approximately 64 KiB per
page with byte-offset navigation and an on-demand tree scanned in the background.
Tree batches contain 64 nodes, with 256 children per container page and at most
4096 resident nodes per tab. Scans can be cancelled; collapsing releases nodes.
Temporary indexes are capped at 16 MiB. Large-file mode also supports streaming
text search, tree/text navigation, bounded node copy, node export, and formatting
or compression to a separate file, with cancellation.
See [large-file mode and measurements](docs/LARGE_FILE_MODE.md).
Windows acceptance covers 9 large-file scenarios and 128 regression tests in both
Debug and Release. See [delivery and reproduction](docs/DELIVERY.md).

The tree now uses QTreeView and a flat QAbstractItemModel, with a linked
key/value table, node search, eight copy operations, subtree navigation,
tab renaming, layout switching, and Unicode/escape conversion.
Formatting preserves numeric literals, field order, and duplicate keys.
See [HiJson Java feature comparison](docs/HIJSON_PARITY.md) for the reference
version, supported operations, deliberate differences, limits, and test commands.

A lightweight JSON formatter and viewer built with Qt.

[中文](README_zh_CN.md)

## Features

- **Format & Compress** — Indented pretty-print or compact single-line output
- **Tree View** — Browse JSON structure in an expandable tree (Key / Type / Value)
- **Multi-Tab** — Open multiple JSON files simultaneously with browser-style tabs
- **Search** — Find text in JSON content with match highlighting and navigation
- **Context Menu** — Right-click any tree node to copy its value or JSON path
- **i18n** — Simplified Chinese, Traditional Chinese and English; switch under View → Language. Traditional Chinese systems (Taiwan / Hong Kong / Macao) are detected automatically
- **Syntax Highlighting** — JSON-aware color highlighting (strings, numbers, keywords)
- **Expand / Collapse All** — One-click toggle for large JSON files

## Screenshot

```
┌──────────────────────────────────────────────────────────┐
│ [Format] [Compress] [Clear]                             │
├──────────────────────────────────────────────────────────┤
│ ┌──────────────┬──────────────────────────────────────┐ │
│ │ {            │  Key      │ Type    │ Value          │ │
│ │   "store": { │  ▼ (root) │ Object  │ {1 members}    │ │
│ │     "book": [│    store  │ Object  │ {1 members}    │ │
│ │       {      │      book │ Array   │ [2 elements]   │ │
│ │         ...  │        [0]│ Object  │ {2 members}    │ │
│ │       }      │          │         │                │ │
│ │     ]        │          │         │                │ │
│ │   }          │          │         │                │ │
│ │ }            │          │         │                │ │
│ └──────────────┴──────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
```

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+Alt+F` | Format JSON |
| `Ctrl+Shift+C` | Compress JSON |
| `Ctrl+F` | Find in JSON |
| `Enter` / `Shift+Enter` | Next / Previous match |
| `Ctrl+O` | Open file |
| `Ctrl+S` | Save file |
| `Ctrl+T` | New tab |
| `Ctrl+W` | Close tab |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Next / Previous tab |
| `Ctrl+Shift+E` | Expand all tree nodes |
| `Ctrl+Shift+R` | Collapse all tree nodes |
| `Ctrl+L` | Clear |
| `Escape` | Close find bar |

## Building

### Prerequisites

- Qt 6.2+ (Widgets + LinguistTools)
- CMake 3.16+
- C++17 compiler (GCC / MinGW-w64 / MSVC / Clang)

### Setup

Set the `QTDIR` environment variable to your Qt installation path, or edit
`CMAKE_PREFIX_PATH` in your `CMakeUserPresets.json`.

See `CMakeUserPresets.json.example` for a template.

### Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### Deploy (Windows)

```bash
windeployqt hellojson.exe
```

## License

This project is licensed under the **MIT License** — see [LICENSE](LICENSE).

Qt is used under the **LGPL v3** — see [NOTICE](NOTICE) for third-party details.

[发布保护与一键验收](docs/RELEASE_READINESS.md)
