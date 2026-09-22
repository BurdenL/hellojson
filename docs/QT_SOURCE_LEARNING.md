# 跟着 HelloJson 源码学习 Qt

这份指南面向掌握 C++ 类、指针和基本容器，刚开始学习 Qt Widgets 的读者。目标是读懂一个真实桌面应用如何启动、响应操作、管理文档、展示树和执行后台任务。先读界面主线，再读大文件实现；JSON 解析算法可以放到最后。

文档依据当前源码整理。函数名比行号更适合长期定位：在 IDE 中搜索下文的 `类名::函数名`，再跳转定义。本文的“练习”均为学习建议，尚未修改进项目。

## 1. 先认识项目地图

```text
main.cpp                     创建 QApplication，显示窗口，进入事件循环
  MainWindow                 菜单、工具栏、标签页、对话框、操作路由
    JsonTab                  单个文档，普通编辑与大文件模式切换
      普通模式
        BoundedEditor 文本编辑与输入容量保护
        JsonTreeModel        向 QTreeView 提供节点数据
          JsonIndex          JSON 解析与原文位置索引
        JsonDetailsModel     向 QTableView 提供选中节点的详情
      大文件模式
        LargeFileView        分页文本、树和任务交互
        LargeTreeModel       按需树模型
        LargeFileTasks       后台搜索、复制、导出、格式化等任务
        FileJsonSource       分块读取和缓存
```

这是职责关系图，不代表每个对象都按图中的层级设置 QObject 父对象。具体生命周期以构造函数、parent 和析构函数为准。

| 目录或文件 | 阅读用途 |
| --- | --- |
| [src](../src) | 应用实现 |
| [CMakeLists.txt](../CMakeLists.txt) | 编译目标、Qt 模块、自动生成、翻译和测试 |
| [mainwindow.ui](../src/mainwindow.ui) | Qt Designer 保存的界面描述 |
| [assets/icons.qrc](../assets/icons.qrc) | 内嵌图标资源清单 |
| [i18n](../i18n) | 可编辑的 TS 翻译文件 |
| [tests/tst_json.cpp](../tests/tst_json.cpp) | 学习预期行为和自动化验证 |
| [CODE_STRUCTURE.md](CODE_STRUCTURE.md) | 文件职责速查 |

`mainwindow_actions.cpp`、`mainwindow_documents.cpp` 等文件实现的是同一个 `MainWindow` 类，不是多个子类。按功能拆分 cpp 不会额外创建对象；声明仍集中在 `mainwindow.h`。

## 2. 建议学习顺序

每次选择一行，用 30～90 分钟完成阅读、断点观察和一个练习。线程与模型章节可分多次完成。

| 顺序 | 主题 | 阅读入口 | 完成标志 |
| --- | --- | --- | --- |
| 1 | 启动与构建 | main.cpp、CMakeLists.txt | 能解释窗口显示后程序为何没有退出 |
| 2 | 控件、布局、所有权 | MainWindow / JsonTab 构造函数 | 能找到编辑器、树、标签页的创建位置 |
| 3 | 信号槽与 QAction | mainwindow.cpp、mainwindow_actions.cpp | 能追踪一次格式化按钮点击 |
| 4 | 文档生命周期 | mainwindow_documents.cpp、jsontab.cpp | 能解释保存失败为何阻止关闭 |
| 5 | Model/View | jsontreemodel.h / .cpp | 能手动推导一个节点的 index 和 parent |
| 6 | 搜索与事件 | mainwindow_search.cpp、jsontab_search.cpp | 能解释防抖和事件过滤器的作用 |
| 7 | 翻译、设置与资源 | mainwindow_localization.cpp、assets、cmake | 能为新增按钮补充翻译和持久化选项 |
| 8 | 分页、线程与取消 | jsondatasource、largefiletasks、largetreemodel | 能解释为何过期结果不会覆盖新页面 |
| 9 | 测试与边界 | tests/tst_json.cpp | 能为一次行为改动增加有意义的测试 |

## 3. 第一站：程序怎样启动

阅读 [main.cpp](../src/main.cpp) 和 [CMakeLists.txt](../CMakeLists.txt)。

启动过程是：创建 `QApplication` → 设置应用信息和图标 → 创建 `MainWindow` → `show()` → `QApplication::exec()`。

`show()` 请求显示窗口；`exec()` 进入事件循环，持续处理键盘、鼠标、绘制、定时器和排队的信号。界面程序的后续行为主要由事件驱动，不是在 main 中写一个不断轮询按钮的循环。

`MainWindow w` 是栈对象，在 main 离开作用域时析构。窗口构造函数中的 `ui->setupUi(this)` 创建 UI 文件描述的控件。`ui_mainwindow.h` 是构建产物，不应手工修改；界面变化应写入 `.ui` 或自己的 cpp。

构建配置中的三个自动步骤：

| 配置 | 作用 | 项目例子 |
| --- | --- | --- |
| AUTOMOC | 为带元对象声明的类生成所需代码 | Q_OBJECT、信号槽 |
| AUTOUIC | 将 .ui 转成界面构建代码 | ui_mainwindow.h |
| AUTORCC | 将 .qrc 中的文件打包成资源 | `:/icons/hellojson-32.png` |

`Q_OBJECT` 支持类的信号、元对象信息等机制；不要因为没有看到信号函数的普通定义就认为代码缺失。它也不意味着任何成员函数都自动成为槽。

**观察：** 在 main、MainWindow 构造函数和一个按钮槽中设置断点。启动时走过前两者，点击按钮才进入槽。

**练习：** 修改窗口标题并重新构建，判断应该修改 `.ui`、构造函数还是语言刷新函数。切换语言后再检查标题是否保留。

## 4. 第二站：控件、布局和对象生命周期

阅读 [mainwindow.cpp](../src/mainwindow.cpp)、[jsontab.cpp](../src/jsontab.cpp) 的构造函数，以及对应头文件。

- `QMainWindow` 提供菜单栏、工具栏、状态栏和中央区域。
- `QTabWidget` 管理多个文档页，当前页通过 `currentTab()` 获取。
- `QSplitter` 允许拖动分隔位置；普通文本和树区域通过它组织。
- `QStackedWidget` 在普通模式与大文件页面之间切换。
- `QTreeView` 展示层级节点，`QTableView` 展示详情。

布局管理器负责控件的位置和大小。不要把全部界面改成固定坐标；字体、翻译长度、窗口大小和 DPI 都会改变所需空间。本项目树视图收起按钮还涉及 `positionTreeButton()` 和事件过滤器，可在掌握普通布局后再读。

QObject 父对象通常会销毁其子对象，因此 `new SomeWidget(parent)` 不一定需要手写 delete。但要区分：**有 parent 的 Qt 对象、普通 C++ 对象、非拥有指针和后台线程对象**。

`currentTab()` 返回现有对象指针，调用者并未获得所有权。`removeWidget()` / 移除标签页也不能简单理解为已经销毁页面，应继续查看关闭流程是否显式释放对象。`Ui::MainWindow` 辅助对象则由窗口析构函数处理。

**观察：** 在 `createTab()`、`onTabCloseRequested()` 和 JsonTab 的模式切换处设断点，比较“切换当前标签”和“关闭标签”的区别。

**练习：** 添加一个只展示当前文件路径的 QLabel，交由布局管理；打开文件和切换标签时更新它。

## 5. 第三站：用信号槽追踪一次操作

阅读 [mainwindow_actions.cpp](../src/mainwindow_actions.cpp) 和 MainWindow 构造函数里的 `connect()`。

以源码中的连接形式为例：

```cpp
connect(ui->formatButton, &QPushButton::clicked,
        this, &MainWindow::onFormatClicked);
```

四个参数依次是发送者、信号、接收者和处理函数。点击按钮产生信号，窗口执行槽。这里采用带类型检查的成员函数指针语法。

`QAction` 表示一个操作，可放进菜单、工具栏并绑定快捷键。业务行为应复用同一条处理路径，避免菜单“保存”和快捷键“保存”实现不同逻辑。

本项目也大量使用带 `this` 上下文的 lambda 连接。上下文对象销毁后连接会断开；但捕获其他裸指针并不延长它们的寿命。阅读 lambda 时同时检查捕获列表和上下文参数。

**追踪主线：** 格式化按钮 → `onFormatClicked()` → `formatCurrentDocument()` → 当前 JsonTab 的 `formatJson()`。查看大文件模式在哪里改变处理路径，以及失败如何反馈。

**练习：** 给已有操作增加一个菜单入口，复用现有槽，不复制业务实现。验证鼠标和快捷键结果一致。

## 6. 第四站：打开、修改、保存与关闭

阅读 [mainwindow_documents.cpp](../src/mainwindow_documents.cpp) 和 JsonTab 的 `openFile()`、`isModified()`、`saveFile()`。

```text
打开：onOpenFile → openFileWithMode → JsonTab::openFile
保存：onSaveFile → saveTab → JsonTab::saveFile → QSaveFile::commit
退出：closeEvent → 逐页 confirmDiscard → 保存成功或允许丢弃 → accept
                                      └ 取消或保存失败 → ignore
```

窗口负责询问用户和选择路径，JsonTab 负责文档数据与写入。这让“对话框取消”和“磁盘写入失败”都能用返回值传回上层。

`QTextDocument` 管理编辑内容和撤销状态。`isModified()` 表示相对已保存状态有无变化；`m_hasValidDocument` 表示当前文本是否已有有效 JSON 索引，两者不能混用。无效 JSON 也可能包含必须保存的用户输入。

普通文本变化会清除旧模型和搜索位置。读 `textChanged` 的连接时，注意这是防止界面继续使用旧索引，而不是每次输入都立即完整解析。

`QSaveFile` 先写临时内容，成功 `commit()` 后替换目标。本项目禁用直接写入回退，只有提交成功才更新路径和修改标记。安全保存不等于自动备份或崩溃恢复。

**练习：** 输入文本 → 保存 → 修改 → 撤销回保存位置，观察修改标记。再尝试关闭未保存标签，分别选择保存、丢弃和取消。

**配套测试：** `safeSaveAndModification()`、`unsavedProtection()`。

## 7. 第五站：理解 QTreeView + QAbstractItemModel

先读 [jsontreemodel.h](../src/jsontreemodel.h)，再读 cpp 的基础接口，最后读 [jsonindex.h](../src/jsonindex.h)。

视图向模型询问“某父节点有几行”“这个位置显示什么”，模型再从索引和原始字节中取得数据。普通模式保留完整文档和节点索引；使用 QTreeView 本身并不能保证大文件内存有界。

| 接口 | 应回答的问题 |
| --- | --- |
| rowCount(parent) | 这个父节点下有几行？ |
| columnCount(parent) | 有几列？本模型是三列 |
| index(row, column, parent) | 这个位置对应哪个 QModelIndex？ |
| parent(child) | 子节点的父索引是什么？ |
| data(index, role) | 该单元格在指定角色下提供什么数据？ |
| headerData(...) | 表头显示什么？ |

`QModelIndex` 是模型位置的句柄，不是 JSON 数据本身。普通模型通过 `createIndex()` 把节点 ID 放入 `internalId()`，再用 ID 查询扁平节点数组。不要把 ID 当内存地址，也不要把另一个模型的索引传进来使用。

本项目还有一个容易读错的约定：**无效 parent 表示不可见根，模型在其下显示一个 JSON 根节点**。因此解析成功后 `rowCount({})` 为 1；要取 JSON 对象成员，需要继续查询这个可见根的子节点。

用下面的内容调试：

```json
{"name":"Qt","items":[1,2],"enabled":true}
```

观察 `index(0, 0, {})` 得到 JSON 根；随后根下有三个成员。展开 items 后，再观察数组元素的 `parent()` 是否返回 items 的第 0 列索引。`data()` 可能被频繁调用，不适合在每次调用时读取整个文件或执行重解析。

模型结构变化要通知视图：整体替换使用 `beginResetModel()` / `endResetModel()`，插入行使用 `beginInsertRows()` / `endInsertRows()`，数据变化通常使用 `dataChanged`。必须按相应通知约定包住修改过程。模型重置后不要继续使用旧 QModelIndex；QPersistentModelIndex 也不能跨模型重置永久有效。

`JsonDetailsModel` 提供同一数据的表格展示。阅读其 `select()` 和模型重置信号连接，理解主树变化时如何清理旧选择。

**练习：** 为树单元格增加 `Qt::ToolTipRole`，显示节点路径。复用已有 `path()`，避免修改解析器；用 `modelAndCopy()` 的风格验证角色返回值。

## 8. 第六站：搜索、定时器和事件过滤器

阅读 [mainwindow_search.cpp](../src/mainwindow_search.cpp)、[jsontab_search.cpp](../src/jsontab_search.cpp) 和 [boundededitor.cpp](../src/boundededitor.cpp)。

搜索输入通过 QTimer 延迟触发，连续输入会重新计时，减少重复搜索。这是防抖；QTimer 本身并不会把工作搬到后台。如果超时槽耗时过长，界面仍会卡顿。

`eventFilter()` 在目标对象正常处理事件前观察或拦截事件。返回 true 通常表示已经处理，不再交给目标；不处理的事件应沿正常路径继续。追踪键盘事件时，同时查看快捷键、事件过滤器和控件自身处理，避免只看一个槽。

BoundedEditor 把输入容量检查放在编辑控件边界，覆盖粘贴、拖放、键盘和输入法等路径。只在菜单“粘贴”槽里检查容量是不够的。

**练习：** 快速输入搜索词，给 `performSearch()` 设置断点或计数日志，观察触发次数；再比较树节点搜索和原文搜索的状态变量。

## 9. 第七站：翻译、资源与持久化

阅读 [mainwindow_localization.cpp](../src/mainwindow_localization.cpp)、[uistrings.h](../src/uistrings.h) 和 [PlatformResources.cmake](../cmake/PlatformResources.cmake)。

`tr()` 用源文与翻译上下文寻找译文。TS 是翻译源文件，QM 是运行时目录；`QTranslator` 加载 QM 并安装到应用。拆分类名或改变上下文时，原有译文可能匹配不到。

`.ui` 控件通过 `retranslateUi()` 更新，动态创建的按钮、模型表头和标签也需要刷新。切换语言并不保证所有手写字符串自动更新。本项目的 `trMain()` 明确保留 MainWindow 上下文，这是项目兼容既有翻译的设计。

`:/icons/...`、`:/i18n/...` 是 Qt 资源路径，与运行时工作目录无关；磁盘相对路径则会受当前目录影响。Qt 内嵌窗口图标与系统桌面图标是两层事情：Windows 用 ICO 资源，macOS 用 ICNS，Linux 用 desktop 文件和图标主题安装目录。

QSettings 存储语言、窗口几何、目录和布局偏好。本项目显式使用 IniFormat + UserScope，不能据“运行在 Windows”就推断它一定写注册表。偏好设置也不包含未保存的文档内容。

**练习：** 给一个新增菜单项补齐简体和繁体译文；切换语言后不重启检查文字，再重启确认语言偏好恢复。

## 10. 第八站：大文件、线程与取消

按 [jsondatasource.h](../src/jsondatasource.h) → [largefiletasks.h](../src/largefiletasks.h) → [largefiletasks.cpp](../src/largefiletasks.cpp) → [largetreemodel.cpp](../src/largetreemodel.cpp) 的顺序读。先理解任务接口，不必先读流式解析器的全部细节。

### 有界读取

FileJsonSource 使用 64 KiB 块和最多 8 块缓存；单次读取也有限制。512 KiB 是一个数据源的块缓存预算，不是整个应用的总内存。视图字符串、节点、任务和多个标签页还会占用内存。

文件偏移按 UTF-8 字节计，QString 和 QTextCursor 的位置按 UTF-16 代码单元计。“中”占 3 个 UTF-8 字节、1 个 UTF-16 代码单元；很多 emoji 占 4 个 UTF-8 字节、2 个 UTF-16 代码单元。不能直接交换这些坐标。分页还必须处理多字节字符跨块的情况。

### worker 对象与线程

```text
界面线程：LargeFileTasks::start → emit requested
                                    ↓ 排队传递
工作线程：LargeTaskWorker::run → 分块处理 → emit finished
                                              ↓ 排队传递
界面线程：检查 generation → emit completed → 更新界面
```

QThread 对象本身与它管理的执行线程不是同一概念。项目创建 worker 后调用 `moveToThread()`，通过信号请求工作。直接写 `worker->run(request)` 仍是普通 C++ 调用，不会自动切换执行线程。

默认 AutoConnection 会根据发射时所在的线程与接收对象的线程归属决定调用方式；跨线程结果在接收线程事件循环中处理。自定义请求/结果使用 `Q_DECLARE_METATYPE` 和 `qRegisterMetaType` 支持排队传递。worker 不应访问 QWidget。

### 取消与生命周期

请求携带 generation，界面取消或替换任务时递增共享原子计数。worker 在处理过程中检查代次，界面收到结果后也检查代次。因此已经排队的旧结果不会被当作新结果显示。

这是协作式取消：计数变化不会强行打断正在进行的一次系统读取。`quit()` 也不会立即终止正在运行的耗时函数。阅读析构函数时，重点看为什么不在界面线程同步等待，以及 `finished → deleteLater` 如何安排 worker 和线程对象清理。

### 按需树加载

LargeTreeModel 的 `canFetchMore()` / `fetchMore()` 表达“是否还能加载”和“发起下一批”。扫描在后台进行，结果到达后 `accept()` 在模型所属线程插入节点。配合分页、缓存预算和临时索引，才实现有界加载。

**练习：** 对较大的测试文件启动搜索后立即取消或关闭标签，在 start、run、结果回调处记录 generation 和当前线程，观察旧结果怎样被丢弃。不要在 worker 中添加直接更新控件的代码。

**配套测试：** `utf8Pages()`、`boundedSources()`、`largeTreeCancellation()`、`largeTaskCancellation()`。

## 11. 用测试帮助理解源码

阅读 [tst_json.h](../tests/tst_json.h) 的用例目录，再只读自己正在学习的用例实现。`QVERIFY` 验证条件，`QCOMPARE` 比较实际和预期；带 `_data()` 的函数提供多组输入。异步测试应等待可观察条件，不宜依靠随意增加固定休眠。

已有构建目录可执行：

```sh
cmake --build out/build/release
ctest --test-dir out/build/release --output-on-failure
```

首次配置和各平台依赖见 [跨平台说明](CROSS_PLATFORM.md)。CTest 通过项目包装脚本运行测试；诊断信息可查看构建目录的 `test-results.txt`。界面测试使用 offscreen，不能代替真实桌面的字体、输入法和系统对话框验收。

推荐练习顺序：先运行原有用例 → 写下改动的预期行为 → 修改代码 → 执行相关测试 → 手动检查界面。只改文档、注释或资源清单时，不必为了增加数量而编写重复实现的测试。

## 12. 读完后应能回答的问题

1. MainWindow、JsonTab、模型和解析器各负责什么？
2. 为什么按钮点击后会执行槽？为什么一个耗时槽会让界面卡顿？
3. 为什么移除标签和销毁标签对象需要分别确认？
4. 为什么修改状态与 JSON 是否有效必须分开？
5. 为什么无效 parent 在普通模型中还能有一行？
6. 为什么模型更新前后需要 begin/end 通知？
7. 为什么 QTreeView 不会自动解决 1GB 文件的内存问题？
8. 为什么移动 worker 到线程后，仍不能直接调用它来实现异步？
9. 为什么任务取消和结果接收两端都检查 generation？
10. 为什么有了 PNG 和翻译文件，还需要资源嵌入与平台部署？

遇到问题时，按“用户操作 → connect → 槽 → 数据变化 → 通知 → 界面刷新”记录调用链。一次读通一条链，比从头到尾连续阅读所有 cpp 更容易形成整体认识。
