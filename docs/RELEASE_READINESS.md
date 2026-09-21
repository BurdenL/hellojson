# 发布准备与验证

## 文档保护

编辑后标签显示未保存图标。关闭标签或退出时可选择保存、放弃或取消；
取消文件选择、保存失败都会保留标签并阻止关闭。
Ctrl+S 保存到当前文件；新文档首次保存、Ctrl+Shift+S 另存为会选择路径。
支持空文档，按 UTF-8 保存。使用 QSaveFile 原子提交，失败时保留旧目标，
仅成功提交后清除修改标记。清空支持撤销。

## 普通模式保护

- 文件超过 **2 MiB** 自动进入只读大文件模式（原阈值为 32 MiB）。
- 编辑／粘贴上限 **2,097,152 个 UTF-16 代码单元**，覆盖键盘粘贴、右键粘贴和拖放。
- 普通格式化输出最多 **2 MiB UTF-8 字节**；超限保留原文，提示使用大文件流式格式化。
- 普通索引最多 **250,000 节点**。大量短值也不会建立无限索引。
- 搜索最多保留 **5,000 条**结果，并标记达到上限；输入停止 180 ms 后执行搜索。
- 这些是同步普通模式的容量保护，不提供普通模式后台取消；大文件操作保持后台执行与取消。

## 设置

记忆语言、窗口位置与大小、打开/保存目录、编辑区横纵布局与分隔比例。
设置使用当前用户的 HelloJson/HelloJson.ini；不保存文档内容。
不恢复未保存文档；崩溃恢复与自动备份仍未实现。

## 一键发布

在 MSVC Developer PowerShell 中执行：

```powershell
./scripts/package_windows.ps1 -QtDirectory '<Qt 6 MSVC kit>'
```

脚本自动构建 Release、部署 Qt / MSVC 运行时、拷贝许可文本，
在隔离 PATH 的目录运行完整回归测试，并用真实 Windows 平台插件验证应用启动和正常退出。
只有验证通过才更新 out/dist/HelloJson-windows-x64.zip 和 SHA-256。
应用翻译内嵌，无需外部 qm。测试程序和离屏测试插件放在独立 qa 目录，不进入发布 ZIP。

## 干净 Windows 验收

每次发布产生 release-日期时间/verify-clean-windows.wsb，启用 Windows Sandbox 的机器
可双击执行；网络禁用，只映射本次发布目录，验收结果写入 sandbox-verification/result.json。
也可将整份 release-日期时间 目录复制到无 Qt / MSVC 开发环境的 Windows 虚拟机，执行：

```powershell
./verify_windows_package.ps1 -PackageDirectory ./HelloJson-windows-x64 -QaDirectory ./qa -ReportDirectory ./clean-verification -CleanMachine
```

验收包括完整自动化测试与实际应用启动退出。人工仍应核对高 DPI 显示、菜单操作和文件对话框。
当前开发机未提供 Windows Sandbox，因此真实干净机器验收状态为 **pending**；
本机隔离 PATH 测试通过不等同于干净系统兼容性验证。
测试使用独立临时设置目录，不改动用户偏好。runtime-随机目录保留供检查，可在验收后手动删除。
