# Windows 交付说明

2026-09-21，Qt 6.11.1 / MSVC x64 Release。

- 本地交付包：`out/dist/HelloJson-windows-x64.zip`，旁边提供 SHA-256 校验文件。
- 解压整个目录后运行 `hellojson.exe`；保留 DLL、插件目录和翻译文件。
- 包含 Qt 与 MSVC 运行时 DLL，无需安装 Qt 开发环境。许可文本位于 `licenses`。
- 已排除开发环境 PATH，使用包内依赖通过 128 项测试及离屏启动检查。
  此检查不等同于在全新 Windows 机器上验证安装兼容性。
- 源码及测试未自动提交或发布；交付包对应本工作区当前实现。

大文件使用只读模式；超过 32 MiB 自动启用，也可用 Ctrl+Shift+O 手动选择。
支持分页、按需树、全文搜索、定位、导出，以及格式化/压缩到新文件。
仅支持 UTF-8；搜索为区分大小写的原始文本匹配，节点复制上限 1 MiB，嵌套上限 512 层。
其他说明、实测结果和复现方法见 [大文件模式](LARGE_FILE_MODE.md)。

验收：Debug / Release 各 128 项回归通过，9 组压力场景全部通过。
场景含 100 MiB / 1 GiB、8 标签反复打开与关闭、超长字符串、嵌套对象、
非法与截断输入、深度限制、取消清理及完整流式转换。
机器可读报告见 [large-file-acceptance.json](large-file-acceptance.json)。

## Windows 增量构建修复

2026-09-21 修复 MSVC 本地化 `/showIncludes` 输出与 Ninja 依赖前缀不匹配的问题。
旧构建曾漏记头文件依赖：MainWindow 增加字段后，main.cpp 未重新编译，
导致 Debug 退出时报变量 w 附近栈损坏。
现在配置时以 UTF-8 编译已知头文件并读取原始输出，检测 Ninja 使用的前缀。
检测失败会停止配置，避免继续产生不完整的依赖记录。

受影响的既有构建目录需要完整重建一次：

```powershell
cmake --build out/build/debug --clean-first
```

已验证头文件修改会触发入口重编译，实际 Debug 程序正常启动、关闭并返回 0。
