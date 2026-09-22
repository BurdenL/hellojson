# 代码阅读指南

如果你希望通过本项目学习 Qt，请先读 [跟着 HelloJson 源码学习 Qt](QT_SOURCE_LEARNING.md)，再用本文查找文件职责。

本次整理保持现有交互、保存格式、翻译上下文和容量限制不变。
拆分后的 mainwindow_*.cpp 仍实现同一个 MainWindow，jsontab_*.cpp 仍实现同一个 JsonTab。
它们是按职责组织的编译单元，没有额外控制器实例或重复状态。

## 建议阅读顺序

1. `src/main.cpp`：Qt 进程入口与发布启动检查。
2. `src/mainwindow.h`、`src/mainwindow.cpp`：窗口组合、标签页生命周期、编辑操作路由。
3. `src/mainwindow_documents.cpp`：打开、保存、未保存确认、退出。
4. `src/jsontab.h`、`src/jsontab.cpp`：单个文档的编辑状态和普通／大文件模式切换。
5. `src/jsontreemodel.cpp`、`src/jsonindex.cpp`：普通 JSON 模型与索引。
6. `src/largefileview.cpp`、`src/largetreemodel.cpp`、`src/largefiletasks.cpp`：大文件异步流程。

## 文件职责

| 文件 | 负责什么 |
| --- | --- |
| mainwindow_actions.cpp | 创建与分组 QAction、菜单和工具栏，动态文字刷新 |
| mainwindow_dialogs.cpp | Unicode 转换、关于、许可和开源说明对话框 |
| mainwindow_documents.cpp | 路径选择、保存结果、关闭前确认；不直接写文件 |
| mainwindow_localization.cpp | 设置持久化、翻译加载和语言切换 |
| mainwindow_search.cpp | 搜索栏、防抖、快捷键、结果计数 |
| jsontab.cpp | 普通编辑器布局、模式切换、格式化、修改标记和原子保存 |
| jsontab_search.cpp | 文本／节点匹配、结果位置、树与原文定位 |
| jsontab_contextmenu.cpp | 树和详情表共用的节点复制、定位与展开操作 |
| boundededitor.h / .cpp | 粘贴、键盘、拖放、输入法的普通编辑容量检查 |
| editorlimits.h | 普通模式容量预算及单位，修改限制从这里开始 |
| uistrings.h | 普通编辑界面的共享翻译入口，保留 MainWindow 上下文 |
| jsondatasource.h / .cpp | 内存／文件数据源、UTF-8 分页和有界缓存 |
| jsonindex.h / .cpp | 普通模式连续节点索引，不创建界面对象 |
| jsontreemodel.h / .cpp | 普通树／详情表模型、格式化和节点查询 |
| largefileview.cpp / largefileactions.cpp | 大文件分页视图、任务入口与结果展示 |
| largetreeindex.cpp | 流式扫描、直接子节点批次与临时磁盘索引 |
| largetreemodel.cpp | 按需树节点、分页与常驻节点预算 |
| largefiletasks.cpp | 搜索、定位、复制、导出及流式格式化作业 |

## 三条重要数据流程

**保存与关闭**

`onSaveFile → saveTab → JsonTab::saveFile → QSaveFile::commit`。
窗口负责交互，标签页负责写入；仅提交成功后更新文件路径和已保存位置。
关闭标签和退出窗口都经过 `confirmDiscard`。取消对话框、取消路径选择、写入失败
均返回 false，上层必须保留文档。修改标记属于 QTextDocument 撤销栈；
它与 `m_hasValidDocument`（当前文本是否有有效 JSON 索引）是两回事。

**普通编辑与树模型**

原始 UTF-8 文本 → JsonIndex 偏移索引 → JsonTreeModel → QTreeView。
任何文本编辑会使旧索引和搜索位置失效。复制读取原始区间，不使用截断预览。
输入容量按 UTF-16 代码单元计算；文件和格式化输出按 UTF-8 字节计算，不能混用。
普通模式预算见 editorlimits.h；大文件缓存预算仍留在各自的数据源／模型类。

**大文件后台任务**

界面发出请求 → worker 分块读取 → 有界结果回传 → 界面验证代次后展示。
每个标签的分页、树扫描和操作任务使用独立工作线程。generation 递增表示旧请求
已经取消或被替换；结果必须检查代次。工作线程拥有自己的文件和缓存，不能访问
界面控件。关闭标签不在 UI 线程等待慢磁盘；后台通过代次检查停止并清理临时资源。
文件偏移是 64 位 UTF-8 字节范围 [start, end)，不是 QString 的字符位置。

## 构建与验证

CMake 的 EDITOR_SOURCES 由应用、测试和性能程序共享；WINDOW_SOURCES
由应用和界面测试共享。新增实现文件时修改对应列表，避免只在某个目标中生效。
翻译仍嵌入可执行文件，拆分 cpp 不改变 TS 上下文。
MSVC / Ninja 的依赖前缀探测必须保留，防止头文件修改后产生新旧对象混用。

```powershell
cmake --build out/build/release
ctest --test-dir out/build/release --output-on-failure
```

文件保存与关闭测试在 tests/tst_json.cpp，压力验收在 tests/stress_largefile.cpp。
发布入口为 scripts/package_windows.ps1；验证流程见 RELEASE_READINESS.md。
