# HiJson Java 功能对照

参考：[nblookup/HiJson](https://github.com/nblookup/HiJson)，本机只读源码版本
2db12edec6d510b7b8d74bca75bed0691f8447a1。
核对文件为 src/hi/chyl/json/MainView.java 和 Kit.java。
本项目使用 Qt 独立实现这些交互，没有引入 Java 运行时或复制 Java 源码。

| 原版操作 | Qt 实现入口 |
| --- | --- |
| 格式化、清空、粘贴 | 顶部按钮、工具栏及编辑菜单 |
| JSON 树和选中节点 key/value 表 | QTreeView + JsonTreeModel；下方 QTableView + JsonDetailsModel |
| 文本查找、上一个/下一个 | Ctrl+Shift+F，搜索栏选择“文本查找”；Enter / Shift+Enter |
| 节点查找、展开并定位命中 | 工具栏“节点查找”或 Ctrl+Shift+N；匹配键名和值 |
| 复制键值、键名、路径、键名键值 | 树节点和键值表的右键菜单 |
| 复制节点内容、带格式节点内容 | “复制节点 JSON”及“复制格式化节点 JSON” |
| 复制同路径键值 | 对选中节点的祖父节点下，各兄弟容器中同名字段汇总，换行分隔 |
| 复制 MAP 式内容 | 输出转义后的 "key","value" |
| 新建、关闭、切换、修改标签名 | 标签栏、视图菜单，F2 重命名；至少保留一个标签 |
| 修改窗口标题 | 工具 → 修改窗口标题 |
| 横向/纵向布局切换 | 工具栏或 Ctrl+Alt+L |
| 清除换行、清除反斜杠 | 工具菜单；可用编辑器 Ctrl+Z 撤销 |
| Unicode / 转义转换 | 工具菜单或 Ctrl+Alt+U；独立输入/输出窗口 |
| 打开、保存文件 | 文件菜单，Ctrl+O / Ctrl+S |

另外保留压缩、树显示切换、中英文切换、语法高亮和全部展开/折叠。
新增双击节点定位原文、右键展开/折叠子树。对象字段顺序、重复键、
数字字面量、字符串转义原文均在格式化和压缩过程中保留。

## 明确的行为差异

- 接受严格 JSON，包括对象、数组及标量根节点。不会复刻 Fastjson 的宽松语法；
  不支持注释、单引号键、未加引号键、尾随逗号。非法输入定位到错误字符并给出 UTF-8 字节偏移。
- 路径使用 $ 根和方括号转义，例如 $.items[0]["a.b"]，避免原版路径拼接的歧义。
- 复制键名键值输出合法的 JSON 成员片段；复制容器值输出实际 JSON，而不是界面摘要。
- 查找不区分大小写；节点查找也能匹配容器键名，不限于叶节点。
- 转义转换支持 JSON 常用转义、Unicode 转义及转义单引号；未知/不完整转义保留原文。
  不实现 Java 八进制转义。原文编辑和转换窗口均不会自动删除合法 JSON 字符串里的转义。
- 窗口标题、布局和标签名目前不跨进程持久化；关闭标签/退出尚无未保存确认。
- 只以上述 Java 仓库为对照，不包含其他 HiJson 分支的 XML/MAP 转 JSON 等扩展。

## 模型结构与性能边界

JsonIndex 使用显式栈扫描 UTF-8，生成连续节点数组。每个节点保存类型、
键/值的原文区间、父节点及所在行。JsonTreeModel 再建立连续子节点 ID 数组，
让 index()、parent()、rowCount() 均直接索引，不扫描兄弟节点。
QModelIndex 保存稳定节点 ID；模型重置后旧索引失效。
树和详情表共享源数据，按视图请求解码显示内容，没有每节点 QWidget、
QTreeWidgetItem 或递归堆对象。长值显示截断，复制始终取完整内容。

当前仍会完整读取文本并同步解析；没有后台解析、磁盘分页或无限文件大小承诺。
文本编辑器、语法高亮、全量搜索以及“展开全部”仍有与输入大小相关的开销。
嵌套限制为 512 层，单份输入索引限制为 INT_MAX 字节；实际可用大小受内存影响。

## 验证

使用 Qt Test 和 QAbstractItemModelTester 检查树/表模型契约，
覆盖非法语法、嵌套空容器、根标量、Unicode、特殊键名、重复键、长整数和高精度小数、
格式化往返、10 万元素随机行访问、深度限制、搜索循环、八种右键复制、
多标签重排/关闭、语言切换不修改文档及格式化撤销。

在配置好编译器和 Qt 的终端中：

    cmake -S . -B out/build/check -DCMAKE_PREFIX_PATH="<Qt kit>" -DBUILD_TESTING=ON
    cmake --build out/build/check
    ctest --test-dir out/build/check --output-on-failure

Windows MSVC 请使用 Developer PowerShell；多配置生成器补充
--config Debug 和 ctest -C Debug。不构建测试时可设置 -DBUILD_TESTING=OFF。
