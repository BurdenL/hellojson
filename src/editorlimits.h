#pragma once

// 仅普通编辑模式使用这些预算；大文件的分页/磁盘缓存预算由对应模块管理。
// 字符数指 UTF-16 代码单元，字节数指 UTF-8，二者不能混用。
namespace EditorLimits {
inline constexpr int InputCharacters = 2 * 1024 * 1024;
inline constexpr int FileBytes = 2 * 1024 * 1024;
inline constexpr int FormattedBytes = 2 * 1024 * 1024;
inline constexpr int CopyFormattedBytes = 8 * 1024 * 1024;
inline constexpr int Nodes = 250000;
inline constexpr int SearchMatches = 5000;
inline constexpr int SearchDelayMs = 180;
}
