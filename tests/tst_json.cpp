#include <QtTest>
#include <QAbstractItemModelTester>
#include <QPlainTextEdit>
#include <QTreeView>
#include <QTableView>
#include <QTabWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QFontDatabase>
#include <QMenu>
#include <QTimer>
#include <QClipboard>
#include <QTemporaryDir>
#include <QFile>
#include <QPushButton>
#include <QBuffer>
#include <QSettings>
#include <QSplitter>
#include <QMessageBox>
#include "boundededitor.h"
#include "jsondatasource.h"
#include "largefileview.h"
#include "jsonindex.h"
#include "jsontreemodel.h"
#include "jsontab.h"
#include "mainwindow.h"

#include "tst_json.h"
void JsonTests::parse_data()
    {
        QTest::addColumn<QByteArray>("input");
        QTest::addColumn<bool>("valid");
        const QList<QByteArray> good = {
            "{}", "[]", R"({"a":[1],"b":{"x":[]},"c":{}})",
            R"([{},[],[[]],{"a":null},true,false,-1.2e+30])",
            "123", "null", R"("hello")", R"({"":"", "a":"\\\"\u4e2d"})",
            R"({"n":123456789012345678901234567890,"n":1e999})"
        };
        int i = 0;
        for (const auto &s : good) QTest::newRow(qPrintable(QString("good%1").arg(i++))) << s << true;
        const QList<QByteArray> bad = {
            "", "[", "{", "{\"a\":", "[1,", "[1,]", "{\"a\":1,}", "[1 2]",
            R"({"a" 1})", R"({"a":})", "true false", "01", "-", "1.", "1e+",
            R"("\x")", R"("\u00xz")", QByteArray("\"a\nb\""), "[}", R"({"a":[1})",
            QByteArray::fromHex("22eda08022"), QByteArray::fromHex("22f490808022")
        };
        i = 0;
        for (const auto &s : bad) QTest::newRow(qPrintable(QString("bad%1").arg(i++))) << s << false;
    }
void JsonTests::parse()
    {
        QFETCH(QByteArray, input);
        QFETCH(bool, valid);
        JsonIndex index;
        QCOMPARE(index.build(input), valid);
        if (!valid) { QVERIFY(index.errorOffset() >= 0); QVERIFY(!index.errorMessage().isEmpty()); }
    }
void JsonTests::modelAndCopy()
    {
        JsonTreeModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        JsonDetailsModel details(&model);
        QAbstractItemModelTester detailTester(&details, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVERIFY(model.setJson(R"({"items":[{"a.b":12345678901234567890},{"a.b":0.1234567890123456789}],"":"", "nil":null})"));
        auto root = model.index(0, 0);
        auto items = model.index(0, 0, root);
        auto first = model.index(0, 0, items);
        auto number = model.index(0, 0, first);
        QCOMPARE(model.path(number), QString(R"($.items[0]["a.b"])"));
        QCOMPARE(model.value(number), QString("12345678901234567890"));
        QCOMPARE(model.json(number), QByteArray("12345678901234567890"));
        QCOMPARE(model.similarValues(number), QString("12345678901234567890\n0.1234567890123456789"));
        QCOMPARE(model.key(model.index(1, 0, root)), QString());
        QCOMPARE(model.path(model.index(1, 0, root)), QString(R"($[""])"));
        QCOMPARE(model.value(model.index(2, 0, root)), QString("null"));
        details.select(first);
        QCOMPARE(details.rowCount(), 1);
        QCOMPARE(details.data(details.index(0, 0)).toString(), QString("a.b"));
        details.select(number);
        QCOMPARE(details.data(details.index(0, 1)).toString(), QString("12345678901234567890"));
        QCOMPARE(model.findNodes("a.b").size(), 2);
        QCOMPARE(model.indexAtOffset(model.node(number)->valueOffset), number);
        model.clear();
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(details.rowCount(), 0);
        QVERIFY(!model.setJson("["));
        QVERIFY(model.setJson("[[],{}]"));
    }
void JsonTests::losslessFormatting()
    {
        const QByteArray source = R"({"z":12345678901234567890,"a":1.234567890123456789e-30,"z":null,"s":"a,[] \"\n","empty":{}})";
        auto pretty = JsonTreeModel::format(source, true);
        JsonIndex index;
        QVERIFY(index.build(pretty));
        QCOMPARE(JsonTreeModel::format(pretty, false), source);
        QCOMPARE(JsonTreeModel::format("[]", true), QByteArray("[]"));
        QCOMPARE(JsonTreeModel::unescape(R"(\u4e2d\u6587\n\\)"), QString::fromUtf8("中文\n\\"));
        QCOMPARE(JsonTreeModel::quote("a\"b\n"), QString(R"("a\"b\n")"));
    }
void JsonTests::generatedDocuments()
    {
        QRandomGenerator rng(1234);
        for (int run = 0; run < 100; ++run) {
            QJsonArray array;
            for (int row = 0; row < 20; ++row)
                array.append(QJsonObject{{"key", QString::number(rng.generate())},
                    {"n", double(rng.generate()) / 7}, {"null", QJsonValue()},
                    {"nested", QJsonArray{true, QString::fromUtf8("中文😀"), QJsonObject{}}}});
            QByteArray bytes = QJsonDocument(array).toJson();
            JsonTreeModel model;
            QVERIFY(model.setJson(bytes));
            auto root = model.index(0, 0);
            QCOMPARE(model.rowCount(root), 20);
            QCOMPARE(QJsonDocument::fromJson(model.json(root)), QJsonDocument(array));
            for (int r = 0; r < 20; ++r)
                QCOMPARE(model.parent(model.index(r, 0, root)), root);
        }
    }
void JsonTests::largeArrayAndDepth()
    {
        QByteArray bytes = "[";
        for (int i = 0; i < 100000; ++i) {
            if (i) bytes += ',';
            bytes += QByteArray::number(i);
        }
        bytes += ']';
        QElapsedTimer timer; timer.start();
        JsonTreeModel model;
        QVERIFY(model.setJson(bytes));
        auto root = model.index(0, 0);
        QCOMPARE(model.rowCount(root), 100000);
        QCOMPARE(model.value(model.index(99999, 0, root)), QString("99999"));
        qInfo() << "100,000 values indexed in" << timer.elapsed() << "ms";
        JsonIndex index;
        QVERIFY(index.build(QByteArray(512, '[') + "0" + QByteArray(512, ']')));
        QVERIFY(!index.build(QByteArray(513, '[') + "0" + QByteArray(513, ']')));
    }
void JsonTests::tabIntegration()
    {
        JsonTab tab;
        auto *edit = tab.findChild<QPlainTextEdit *>();
        auto *tree = tab.findChild<QTreeView *>("jsonTree");
        auto *details = tab.findChild<QTableView *>("jsonDetails");
        QVERIFY(edit && tree && details);
        tab.setText(R"({"items":[{"name":"one"},{"name":"two"}]})");
        tab.formatJson(true);
        QVERIFY(tab.hasDocument());
        QString compact = tab.text();
        tab.refreshLanguage();
        QCOMPARE(tab.text(), compact);
        tab.setNodeSearch(true);
        tab.findText("name");
        QCOMPARE(tab.matchCount(), 2);
        QCOMPARE(tab.currentMatchIndex(), 0);
        QVERIFY(tab.findNext("name"));
        QCOMPARE(tab.currentMatchIndex(), 1);
        QCOMPARE(details->model()->rowCount(), 1);
        QCOMPARE(edit->textCursor().selectedText(), QString(R"("two")"));
        tab.findNext("name");
        QCOMPARE(tab.currentMatchIndex(), 0);
        tab.findPrev("name");
        QCOMPARE(tab.currentMatchIndex(), 1);
        tab.setText("{");
        QVERIFY(!tab.hasDocument());
        tab.formatJson(false);
        QVERIFY(!tab.hasDocument());
        QCOMPARE(tree->model()->rowCount(), 0);
        QVERIFY(!tab.parseError().isEmpty());
        tab.setText("[1,2]");
        tab.formatJson(false);
        QVERIFY(tab.hasDocument());
        edit->undo();
        QCOMPARE(tab.text(), QString("[1,2]"));
        QVERIFY(!tab.hasDocument());
        tab.clear();
        QCOMPARE(tab.matchCount(), 0);
    }
void JsonTests::windowIntegration()
    {
        MainWindow window;
        window.show();
        window.activateWindow();
        QApplication::processEvents();
        auto *tabs = window.findChild<QTabWidget *>();
        QVERIFY(tabs);
        QVERIFY(!window.findChild<QAction *>("actionCloseTab")->isEnabled());
        QVERIFY(!window.findChild<QAction *>("actionNextTab")->isEnabled());
        QCOMPARE(window.findChild<QAction *>("actionFind")->shortcut(), QKeySequence("Ctrl+F"));
        QCOMPARE(window.findChild<QAction *>("actionFormat")->shortcut(), QKeySequence("Ctrl+Alt+F"));
        auto *tab = qobject_cast<JsonTab *>(tabs->currentWidget());
        QVERIFY(tab);
        tab->setText(R"({"one":1,"two":2})");
        QVERIFY(QMetaObject::invokeMethod(&window, "onCompressClicked"));
        QVERIFY(tab->hasDocument());
        const QString compressed = tab->text();
        QVERIFY(QMetaObject::invokeMethod(&window, "onFindToggled"));
        auto *mode = window.findChild<QComboBox *>("searchMode");
        auto *search = window.findChild<QLineEdit *>("searchText");
        mode->setCurrentIndex(1);
        search->setFocus();
        QApplication::processEvents();
        QTest::keyClicks(search, "one");
        QCOMPARE(search->text(), QString("one"));
        QTRY_COMPARE(tab->matchCount(), 1);
        QVERIFY(search->hasFocus());
        QVERIFY(QMetaObject::invokeMethod(&window, "onFindClose"));
        for (auto *action : window.findChildren<QAction *>())
            if (action->objectName() == "actionChinese") action->trigger();
        QCOMPARE(tab->text(), compressed);
        QVERIFY(QMetaObject::invokeMethod(&window, "onNewTab"));
        auto *second = qobject_cast<JsonTab *>(tabs->currentWidget());
        QVERIFY(second && second != tab);
        QVERIFY(window.findChild<QAction *>("actionNextTab")->isEnabled());
        window.findChild<QAction *>("actionNextTab")->trigger();
        QCOMPARE(tabs->currentWidget(), tab);
        window.findChild<QAction *>("actionPrevTab")->trigger();
        QCOMPARE(tabs->currentWidget(), second);
        second->setText(R"({"another":true})");
        QVERIFY(QMetaObject::invokeMethod(&window, "onFormatClicked"));
        QVERIFY(second->hasDocument());
        tabs->setCurrentWidget(tab);
        QCOMPARE(tab->text(), compressed);
        tabs->tabBar()->moveTab(0, 1);
        QCOMPARE(tabs->currentWidget(), tab);
        QTimer::singleShot(0, [] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->button(QMessageBox::Discard)->click(); });
        QVERIFY(QMetaObject::invokeMethod(&window, "onCloseTab"));
        QCOMPARE(tabs->count(), 1);
        second->setText(QString::fromUtf8(R"({
            "项目": "Hello JSON",
            "users": [{"name":"Alice","id":12345678901234567890},{"name":"Bob","id":2}],
            "enabled":true, "empty":null, "special.key":"中文和 Unicode"
        })"));
        second->formatJson(false);
        second->expandAll();
        window.resize(1200, 780);
        QApplication::processEvents();
        QVERIFY(window.grab().save(QCoreApplication::applicationDirPath() + "/ui-smoke.png"));
    }

void JsonTests::clipboardActions()
{
    // Reset the translator installed by the preceding window test: it is owned
    // by that window and Qt removes it on destruction.
    JsonTab tab;
    tab.resize(1000, 650);
    tab.show();
    tab.setText(R"({"items":[{"a.b":12345678901234567890},{"a.b":2}]})");
    tab.formatJson(false);
    tab.expandAll();
    auto *tree = tab.findChild<QTreeView *>("jsonTree");
    auto *model = qobject_cast<JsonTreeModel *>(tree->model());
    auto root = model->index(0, 0);
    auto item = model->index(0, 0, model->index(0, 0, model->index(0, 0, root)));
    tree->setCurrentIndex(item);
    QApplication::processEvents();
    const QStringList expected = {
        "12345678901234567890", "a.b", R"($.items[0]["a.b"])",
        R"("a.b": 12345678901234567890)", "12345678901234567890",
        "12345678901234567890\n2", R"("a.b","12345678901234567890")",
        "12345678901234567890"
    };
    for (int action = 0; action < 8; ++action) {
        bool clicked = false;
        QTimer::singleShot(0, &tab, [action, &clicked] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu) return;
            clicked = true;
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                              menu->actionGeometry(menu->actions()[action]).center());
        });
        QVERIFY(QMetaObject::invokeMethod(&tab, "onTreeContextMenu",
                 Q_ARG(QPoint, tree->visualRect(item).center())));
        QVERIFY(clicked);
        QCOMPARE(QApplication::clipboard()->text(), expected[action]);
    }
}
void JsonTests::utf8Pages_data()
{
    QTest::addColumn<QByteArray>("bytes");
    for (const QByteArray codepoint : {QByteArray::fromHex("c3a9"), QByteArray::fromHex("e4b8ad"),
                                      QByteArray::fromHex("f09f9880")}) {
        for (int shift = 1; shift < codepoint.size(); ++shift) {
            const QByteArray text = QByteArray(JsonDataSource::BlockBytes - shift, 'a') + codepoint +
                QByteArray(JsonDataSource::BlockBytes, 'b') + codepoint;
            QTest::newRow(qPrintable(QString("%1-%2").arg(codepoint.size()).arg(shift))) << text;
        }
    }
    QTest::newRow("bom") << (QByteArray::fromHex("efbbbf") + QByteArray(JsonDataSource::BlockBytes, 'a'));
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("exact") << QByteArray(JsonDataSource::BlockBytes, 'a');
    QTest::newRow("long-string") << (QByteArray("\"") + QByteArray(5 * JsonDataSource::BlockBytes, 'x') + '"');
    QTest::newRow("crlf") << (QByteArray(JsonDataSource::BlockBytes - 1, 'a') + "\r\nb");
}

void JsonTests::utf8Pages()
{
    QFETCH(QByteArray, bytes);
    MemoryJsonSource source(bytes);
    JsonTextPage first = readJsonTextPage(source, 0);
    QByteArray joined;
    qint64 previousEnd = first.start;
    for (qint64 page = 0; page < first.pages; ++page) {
        auto result = readJsonTextPage(source, page);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.start, previousEnd);
        QVERIFY(result.end - result.start <= JsonDataSource::BlockBytes + 3);
        QVERIFY(!result.text.contains(QChar::ReplacementCharacter));
        joined += result.text.toUtf8();
        previousEnd = result.end;
    }
    if (bytes.startsWith(QByteArray::fromHex("efbbbf"))) bytes.remove(0, 3);
    QCOMPARE(joined, bytes);
    QCOMPARE(readJsonTextPage(source, -1).page, qint64(0));
    QCOMPARE(readJsonTextPage(source, 999999).page, first.pages - 1);
}

void JsonTests::boundedSources()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("pages.json");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    for (int i = 0; i < 32; ++i)
        QCOMPARE(file.write(QByteArray(JsonDataSource::BlockBytes, char('a' + i % 26))), JsonDataSource::BlockBytes);
    file.close();
    FileJsonSource source;
    QString error;
    QVERIFY(source.open(path, &error));
    for (int i = 0; i < 32; ++i) {
        auto page = readJsonTextPage(source, i);
        QVERIFY(page.error.isEmpty());
        QVERIFY(source.residentBytes() <= FileJsonSource::CacheBlocks * JsonDataSource::BlockBytes);
        QCOMPARE(page.text[0], QChar('a' + i % 26));
    }
    const qint64 read = source.bytesRead();
    QVERIFY(!source.read(31 * JsonDataSource::BlockBytes, 1, &error).isEmpty());
    QCOMPARE(source.bytesRead(), read);
    QVERIFY(source.read(-1, 1, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(source.read(0, JsonDataSource::MaxReadBytes + 1, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(file.open(QIODevice::Append));
    file.write("changed");
    file.close();
    QVERIFY(!readJsonTextPage(source, 0).error.isEmpty());
    QCOMPARE(source.residentBytes(), qint64(0));
    QVERIFY(source.open(path, &error));
    QVERIFY(readJsonTextPage(source, 0).error.isEmpty());

    class HugeSource : public JsonDataSource {
    public:
        qint64 size() const override { return (qint64(1) << 33) + 7; }
        qint64 residentBytes() const override { return 0; }
        QByteArray read(qint64 offset, qint64 length, QString *error) override {
            if (error) error->clear();
            return QByteArray(int(qMin(size() - offset, length)), 'x');
        }
    } huge;
    auto last = readJsonTextPage(huge, 9999999999);
    QCOMPARE(last.end, huge.size());
    QCOMPARE(last.text, QString("xxxxxxx"));
    QVERIFY(readSmallJsonText(huge, &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void JsonTests::largeFileIntegration()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("large.json");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    // Sparse whitespace is sufficient for the automatic-mode boundary test.
    QVERIFY(file.resize(JsonDataSource::LargeFileThreshold + 1));
    file.close();
    MainWindow window;
    window.resize(1200, 780);
    window.show();
    auto *tabs = window.findChild<QTabWidget *>();
    auto *tab = qobject_cast<JsonTab *>(tabs->currentWidget());
    QString error;
    QVERIFY(tab->openFile(path, JsonTab::OpenMode::Automatic, &error));
    QVERIFY(tab->isLargeFile());
    auto *view = tab->findChild<LargeFileView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(!view->isLoading(), 5000);
    QVERIFY(view->currentPage().error.isEmpty());
    QVERIFY(view->currentPage().bytesRead <= 3 * JsonDataSource::BlockBytes);
    QVERIFY(tab->text().isEmpty());
    tab->formatJson(false);
    tab->setText("{}");
    QVERIFY(tab->isLargeFile());
    QVERIFY(!tab->hasDocument());
    for (const char *name : {"actionFormat", "actionCompress", "actionSave"})
        QVERIFY(!window.findChild<QAction *>(name)->isEnabled());
    auto *editor = view->findChild<QPlainTextEdit *>("pagedText");
    QVERIFY(editor->isReadOnly());
    QVERIFY(!editor->isUndoRedoEnabled());
    view->goToByte(JsonDataSource::LargeFileThreshold);
    QTRY_VERIFY_WITH_TIMEOUT(!view->isLoading(), 5000);
    QCOMPARE(view->currentPage().page, view->currentPage().pages - 1);
    QVERIFY(editor->toPlainText().size() <= JsonDataSource::BlockBytes + 3);

    const QString small = dir.filePath("small.json");
    QFile smallFile(small);
    QVERIFY(smallFile.open(QIODevice::WriteOnly));
    const auto smallBytes = QString::fromUtf8("{\r\n\"message\":\"hello\",\r\n\"unicode\":\"中文😀\"\r\n}").toUtf8();
    smallFile.write(smallBytes);
    smallFile.close();
    QVERIFY(tab->openFile(small, JsonTab::OpenMode::LargeFile, &error));
    QTRY_VERIFY_WITH_TIMEOUT(!view->isLoading(), 5000);
    QVERIFY(editor->toPlainText().contains("hello"));
    view->goToByte(smallBytes.indexOf("hello"));
    QTRY_VERIFY_WITH_TIMEOUT(!view->isLoading(), 5000);
    QCOMPARE(editor->textCursor().position(), editor->toPlainText().indexOf("hello"));
    auto *tree = view->findChild<QTreeView *>("largeFileTree");
    auto *treeModel = qobject_cast<LargeTreeModel *>(tree->model());
    QTRY_VERIFY(!treeModel->busy());
    tree->expand(treeModel->index(0, 0));
    QTRY_VERIFY(!treeModel->busy());
    QCOMPARE(treeModel->rowCount(treeModel->index(0, 0)), 2);
    auto *query = view->findChild<QLineEdit *>("largeSearchQuery");
    auto *tasks = view->findChild<LargeFileTasks *>();
    auto *matches = view->findChild<QComboBox *>("largeMatches");
    QVERIFY(window.findChild<QAction *>("actionFind")->isEnabled());
    window.findChild<QAction *>("actionFind")->trigger();
    QCOMPARE(window.focusWidget(), query);
    query->setText("hello");
    view->findChild<QPushButton *>("largeSearch")->click();
    QTRY_VERIFY(!tasks->busy());
    QCOMPARE(matches->count(), 1);
    QCOMPARE(matches->itemData(0).toLongLong(), qint64(smallBytes.indexOf("hello")));
    // Locate a cursor after CRLF, including the original UTF-8 byte mapping.
    view->goToByte(smallBytes.indexOf("hello"));
    QTRY_VERIFY(!view->isLoading());
    view->findChild<QPushButton *>("largeLocate")->click();
    QTRY_VERIFY(!tasks->busy());
    QCOMPARE(treeModel->record(tree->currentIndex())->key, QString("message"));
    window.grab().save(QCoreApplication::applicationDirPath() + "/large-file-ui.png");
    // Multiple in-flight requests must never show a result for an older file.
    view->openFile(path);
    view->openFile(small);
    QTRY_VERIFY_WITH_TIMEOUT(!view->isLoading(), 5000);
    QVERIFY(view->currentPage().text.contains("hello"));
    QVERIFY(smallFile.open(QIODevice::Append));
    smallFile.write(" ");
    smallFile.close();
    view->goToByte(0);
    QTRY_VERIFY_WITH_TIMEOUT(!view->isLoading(), 5000);
    QVERIFY(!view->currentPage().error.isEmpty());
    view->openFile(small);
    QTRY_VERIFY_WITH_TIMEOUT(!view->isLoading(), 5000);
    QVERIFY(view->currentPage().error.isEmpty());
    const QString utf16 = dir.filePath("utf16.json");
    QFile utf16File(utf16);
    QVERIFY(utf16File.open(QIODevice::WriteOnly));
    utf16File.write(QByteArray::fromHex("fffe7b007d00"));
    utf16File.close();
    view->openFile(utf16);
    QTRY_VERIFY_WITH_TIMEOUT(!view->isLoading(), 5000);
    QVERIFY(!view->currentPage().error.isEmpty());
    QVERIFY(editor->toPlainText().isEmpty());
    QVERIFY(tab->openFile(small, JsonTab::OpenMode::Automatic, &error));
    QVERIFY(!tab->isLargeFile());
    QVERIFY(tab->text().contains("hello"));
    QVERIFY(window.findChild<QAction *>("actionFormat")->isEnabled());
    tab->formatJson(false);
    QVERIFY(tab->hasDocument());
}

void JsonTests::largeTreeParse_data() { parse_data(); }
void JsonTests::largeTreeParse()
{
    QFETCH(QByteArray, input);
    QFETCH(bool, valid);
    MemoryJsonSource source(input);
    LargeTreeScanner scanner(source, [] { return false; });
    auto result = scanner.root();
    if (result.error.isEmpty() && !result.records.isEmpty() && result.records[0].container()) {
        LargeTreeScanner children(source, [] { return false; });
        result = children.children(result.records[0], 0, -1);
    }
    QCOMPARE(result.error.isEmpty(), valid);
}

void JsonTests::largeTreeScanner()
{
    const QByteArray bytes = QByteArray::fromHex("efbbbf") +
        R"({"a":[{"deep":["str",true,null]}],"a":12345678901234567890,"":"\u4e2d"})";
    MemoryJsonSource source(bytes);
    LargeTreeScanner rootScanner(source, [] { return false; });
    auto root = rootScanner.root();
    QCOMPARE(root.records[0].start, qint64(3));
    LargeTreeScanner children(source, [] { return false; });
    auto batch = children.children(root.records[0], 0, -1);
    QVERIFY2(batch.error.isEmpty(), qPrintable(batch.error));
    QVERIFY(batch.done);
    QCOMPARE(batch.records.size(), 3);
    QCOMPARE(batch.records[1].preview, QString("12345678901234567890"));
    QCOMPARE(batch.records[2].key, QString());
    QCOMPARE(batch.records[2].preview, QString::fromUtf8("中"));
    QCOMPARE(bytes.mid(batch.records[0].start, batch.records[0].end - batch.records[0].start),
             QByteArray(R"([{"deep":["str",true,null]}])"));
    QByteArray longText = "[\"" + QByteArray(65532, 'a') + QString::fromUtf8("😀").toUtf8() + "\"]";
    MemoryJsonSource longSource(longText);
    LargeTreeRecord array; array.type = LargeTreeRecord::Array; array.key = "$"; array.row = -1;
    LargeTreeScanner longScanner(longSource, [] { return false; });
    auto longBatch = longScanner.children(array, 0, -1);
    QVERIFY(longBatch.error.isEmpty());
    QCOMPARE(longBatch.records[0].end, qint64(longText.size() - 1));
    QVERIFY(longBatch.records[0].preview.size() <= 129);
    MemoryJsonSource scalar(longText.mid(1, longText.size() - 2));
    int shallowChecks = 0;
    LargeTreeScanner scalarScanner(scalar, [&] { return ++shallowChecks > 3; });
    auto scalarRoot = scalarScanner.root(true);
    QVERIFY(!scalarRoot.cancelled);
    QCOMPARE(scalarRoot.records[0].type, LargeTreeRecord::String);
    QVERIFY(scalarRoot.records[0].preview.size() <= 129);
    int checks = 0;
    LargeTreeScanner stopped(longSource, [&checks] { return ++checks > 3; });
    auto cancelled = stopped.children(array, 0, -1);
    QVERIFY(cancelled.cancelled);
    QVERIFY(cancelled.records.isEmpty());
    MemoryJsonSource deep(QByteArray(514, '[') + "0" + QByteArray(514, ']'));
    LargeTreeScanner deepScanner(deep, [] { return false; });
    QVERIFY(!deepScanner.children(array, 0, -1).error.isEmpty());
    class WideSource : public JsonDataSource {
    public:
        qint64 base = qint64(1) << 33;
        qint64 size() const override { return base + 3; }
        qint64 residentBytes() const override { return 0; }
        QByteArray read(qint64 offset, qint64 length, QString *error) override {
            if (error) error->clear();
            return QByteArray("42]").mid(int(offset - base), int(length));
        }
    } wide;
    array.key = "nested"; array.row = 0;
    LargeTreeScanner wideScanner(wide, [] { return false; });
    auto wideBatch = wideScanner.children(array, qint64(1) << 32, wide.base);
    QVERIFY(wideBatch.error.isEmpty());
    QCOMPARE(wideBatch.records[0].start, wide.base);
    QCOMPARE(wideBatch.records[0].row, qint64(1) << 32);
    QCOMPARE(wideBatch.records[0].preview, QString("42"));
}

void JsonTests::largeTreeModel()
{
    QTemporaryDir directory;
    QFile file(directory.filePath("tree.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("[");
    for (int i = 0; i < 1000; ++i) {
        if (i) file.write(",");
        file.write(QByteArray::number(i));
    }
    file.write("]");
    file.close();
    auto model = std::make_unique<LargeTreeModel>();
    QAbstractItemModelTester tester(model.get(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    tester.setUseFetchMore(false);
    model->openFile(file.fileName());
    QTRY_VERIFY(!model->busy());
    QCOMPARE(model->rowCount(), 1);
    const auto root = model->index(0, 0);
    QCOMPARE(model->rowCount(root), 0);
    for (int i = 0; i < 4; ++i) {
        QVERIFY(model->canFetchMore(root));
        model->fetchMore(root);
        QTRY_VERIFY(!model->busy());
        QCOMPARE(model->rowCount(root), (i + 1) * LargeTreeScanner::BatchSize);
    }
    QVERIFY(!model->canFetchMore(root));
    QCOMPARE(model->residentNodes(), 257);
    QVERIFY(model->canNextPage(root));
    const QString cache = model->temporaryIndexPath();
    QVERIFY(QFileInfo::exists(cache));
    model->changePage(root, true);
    QTRY_VERIFY(!model->busy());
    QCOMPARE(model->firstRow(root), qint64(256));
    QCOMPARE(model->data(model->index(0, 2, root)).toString(), QString("256"));
    model->changePage(root, false);
    QTRY_VERIFY(!model->busy());
    QVERIFY(model->lastCacheHit());
    QCOMPARE(model->data(model->index(0, 2, root)).toString(), QString("0"));
    model->releaseChildren(root);
    QCOMPARE(model->residentNodes(), 1);
    model->requestMore(root);
    model->cancel();
    QTest::qWait(30);
    QCOMPARE(model->rowCount(root), 0);
    model->requestMore(root);
    QTRY_VERIFY(!model->busy());
    QCOMPARE(model->rowCount(root), 64);
    QVERIFY(model->diskBytes() <= LargeTreeWorker::DiskBudget);
    // Cached indexes must not be used after an external edit.
    QVERIFY(file.open(QIODevice::Append));
    file.write(" ");
    file.close();
    QSignalSpy errors(model.get(), &LargeTreeModel::statusChanged);
    model->releaseChildren(root);
    model->requestMore(root);
    QTRY_VERIFY(!model->busy());
    QCOMPARE(model->rowCount(root), 0);
    QVERIFY(errors.last()[0].toString().contains("changed"));
    // Many expanded branches still obey the total resident node budget.
    QFile branches(directory.filePath("branches.json"));
    QVERIFY(branches.open(QIODevice::WriteOnly));
    branches.write("[");
    for (int i = 0; i < 24; ++i) {
        if (i) branches.write(",");
        branches.write("[" + QByteArray("0,").repeated(255) + "0]");
    }
    branches.write("]");
    branches.close();
    model->openFile(file.fileName());
    model->openFile(branches.fileName()); // stale result must be discarded
    QTRY_VERIFY(!model->busy());
    auto branchRoot = model->index(0, 0);
    model->requestMore(branchRoot);
    QTRY_VERIFY(!model->busy());
    QCOMPARE(model->rowCount(branchRoot), 24);
    for (int i = 0; i < 24; ++i) {
        const auto branch = model->index(i, 0, branchRoot);
        while (model->canFetchMore(branch)) {
            model->fetchMore(branch);
            QTRY_VERIFY(!model->busy());
            QVERIFY(model->residentNodes() <= LargeTreeModel::ResidentLimit);
        }
    }
    QVERIFY(model->residentNodes() > LargeTreeModel::ResidentLimit - 64);
    model->releaseChildren(model->index(0, 0, branchRoot));
    const auto last = model->index(23, 0, branchRoot);
    model->requestMore(last);
    QTRY_VERIFY(!model->busy());
    QCOMPARE(model->rowCount(last), 64);
    const QString finalCache = model->temporaryIndexPath();
    QVERIFY(!QFileInfo::exists(cache));
    model.reset();
    QTRY_VERIFY(!QFileInfo::exists(finalCache));
}

void JsonTests::largeTreeCache()
{
    QTemporaryDir directory;
    QFile file(directory.filePath("cache.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray entry = "\"" + QByteArray(128, 'k') + "\":\"" + QByteArray(128, 'v') + "\",";
    file.write("{");
    for (int i = 0; i < 520; ++i) file.write(entry.repeated(64));
    file.write("\"end\":null}");
    file.close();
    auto generation = std::make_shared<std::atomic<quint64>>(1);
    LargeTreeWorker worker(generation);
    LargeTreeBatch batch;
    connect(&worker, &LargeTreeWorker::ready, this, [&](const LargeTreeBatch &result) { batch = result; });
    LargeTreeRequest request;
    request.path = file.fileName(); request.root = true; request.generation = 1;
    worker.scan(request);
    QVERIFY(batch.error.isEmpty());
    request.parent = batch.records[0]; request.root = false;
    qint64 previousSize = 0;
    bool rotated = false;
    for (int i = 0; i < 520; ++i) {
        request.firstRow = i * 64;
        worker.scan(request);
        QVERIFY2(batch.error.isEmpty(), qPrintable(batch.error));
        QCOMPARE(batch.records.size(), 64);
        QVERIFY(batch.diskBytes <= LargeTreeWorker::DiskBudget);
        QVERIFY(batch.sourceCacheBytes <= 8 * JsonDataSource::BlockBytes);
        rotated |= batch.diskBytes < previousSize;
        previousSize = batch.diskBytes;
        request.resumeOffset = batch.nextOffset;
    }
    QVERIFY(rotated);
    request.firstRow = 0; request.resumeOffset = -1;
    worker.scan(request);
    QVERIFY(!batch.cacheHit); // evicted page is rebuilt by a bounded scan
    worker.scan(request);
    QVERIFY(batch.cacheHit);
}

void JsonTests::largeTreeCancellation()
{
    QTemporaryDir directory;
    QFile large(directory.filePath("long-child.json"));
    QVERIFY(large.open(QIODevice::WriteOnly));
    large.write("[\"");
    const QByteArray chunk(1024 * 1024, 'x');
    for (int i = 0; i < 64; ++i) large.write(chunk);
    large.write("\"]");
    large.close();
    QFile small(directory.filePath("small.json"));
    QVERIFY(small.open(QIODevice::WriteOnly));
    small.write("[42]");
    small.close();
    LargeTreeModel model;
    model.openFile(large.fileName());
    QTRY_VERIFY(!model.busy());
    const auto root = model.index(0, 0);
    bool cancelled = false;
    int ticks = 0;
    QTimer heartbeat;
    heartbeat.setInterval(1);
    connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; });
    connect(&model, &LargeTreeModel::statusChanged, this, [&](const QString &status) {
        if (!cancelled && (status.contains("byte ") || status.contains("字节位置"))) {
            cancelled = true;
            model.cancel();
        }
    });
    heartbeat.start();
    model.requestMore(root);
    QTRY_VERIFY_WITH_TIMEOUT(cancelled, 5000);
    QVERIFY(ticks > 0);
    QCOMPARE(model.rowCount(root), 0);
    QElapsedTimer resume; resume.start();
    model.openFile(small.fileName());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 2000);
    QVERIFY(resume.elapsed() < 2000);
    model.requestMore(model.index(0, 0));
    QTRY_VERIFY(!model.busy());
    QCOMPARE(model.data(model.index(0, 2, model.index(0, 0))).toString(), QString("42"));
}

void JsonTests::largeTransform_data() { parse_data(); }
void JsonTests::largeTransform()
{
    QFETCH(QByteArray, input);
    QFETCH(bool, valid);
    for (bool pretty : {false, true}) {
        MemoryJsonSource source(input);
        QBuffer output;
        QVERIFY(output.open(QIODevice::WriteOnly));
        LargeTreeScanner scanner(source, [] { return false; });
        const auto result = scanner.transform(output, pretty);
        QCOMPARE(result.error.isEmpty(), valid);
        QCOMPARE(result.done, valid);
        if (valid) {
            JsonIndex index;
            QVERIFY(index.build(output.data()));
            QCOMPARE(JsonTreeModel::format(output.data(), false), JsonTreeModel::format(input, false));
        }
    }
}

static bool writeFixture(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
static QByteArray readFixture(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void JsonTests::largeTaskSearch()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("search.json");
    const QByteArray unicode = QString::fromUtf8("中文😀").toUtf8();
    const QByteArray bytes = QByteArray(65534, 'x') + unicode + QByteArray(600, 'a');
    QVERIFY(writeFixture(path, bytes));
    auto generation = std::make_shared<std::atomic<quint64>>(1);
    LargeTaskWorker worker(generation);
    LargeTaskResult result;
    QVector<qint64> hits;
    connect(&worker, &LargeTaskWorker::finished, this, [&](const LargeTaskResult &r) { result = r; });
    connect(&worker, &LargeTaskWorker::matches, this, [&](quint64, const QVector<qint64> &batch) {
        QVERIFY(batch.size() <= 32); hits += batch;
    });
    LargeTaskRequest request;
    request.path = path; request.generation = 1; request.query = QString::fromUtf8(unicode);
    worker.run(request);
    QVERIFY(result.error.isEmpty());
    QCOMPARE(hits, QVector<qint64>{65534});
    request.query = "aa";
    int total = 0;
    qint64 previous = 65534 + unicode.size() - 1;
    do {
        hits.clear();
        worker.run(request);
        QVERIFY(result.error.isEmpty());
        QVERIFY(hits.size() <= LargeTaskWorker::SearchLimit);
        for (qint64 offset : hits) { QCOMPARE(offset, previous + 1); previous = offset; }
        total += hits.size();
        request.start = result.nextOffset;
    } while (result.more);
    QCOMPARE(total, 599);
    request.query = ""; request.start = 0;
    worker.run(request);
    QVERIFY(!result.error.isEmpty());
    // Cancellation after a delivered batch must suppress the final result.
    QSignalSpy completed(&worker, &LargeTaskWorker::finished);
    connect(&worker, &LargeTaskWorker::matches, this, [&](quint64, const QVector<qint64> &) { ++*generation; });
    request.query = "aa";
    worker.run(request);
    QCOMPARE(completed.size(), 0);
    request.query = QString(4097, 'a');
    request.generation = generation->load();
    result = {};
    worker.run(request);
    QCOMPARE(completed.size(), 1);
    QVERIFY(!result.error.isEmpty());
}

void JsonTests::largeTaskFiles()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("input.json"), output = dir.filePath("output.json");
    const QByteArray source = QByteArray::fromHex("efbbbf") +
        " \r\n" + R"({"n":123456789012345678901234567890,"n":1e999,"s":"a \" \\ \n \u4e2d","a":[[],{}]})";
    QVERIFY(writeFixture(path, source));
    auto generation = std::make_shared<std::atomic<quint64>>(1);
    LargeTaskWorker worker(generation);
    LargeTaskResult result;
    connect(&worker, &LargeTaskWorker::finished, this, [&](const LargeTaskResult &r) { result = r; });
    LargeTaskRequest request; request.path = path; request.output = output; request.generation = 1;
    request.kind = LargeTaskKind::Format;
    worker.run(request);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    auto formatted = readFixture(output);
    QVERIFY(formatted.contains('\n'));
    JsonIndex index; QVERIFY(index.build(formatted));
    request.path = output; request.output = dir.filePath("compact.json"); request.kind = LargeTaskKind::Compact;
    worker.run(request);
    QVERIFY(result.error.isEmpty());
    QCOMPARE(readFixture(request.output), JsonTreeModel::format(source.mid(3), false));
    request.path = path; request.kind = LargeTaskKind::Copy;
    request.start = source.indexOf("1234"); request.end = request.start + 30;
    worker.run(request);
    QCOMPARE(result.copied, QByteArray("123456789012345678901234567890"));
    request.kind = LargeTaskKind::Export; request.output = output;
    worker.run(request);
    QCOMPARE(readFixture(output), QByteArray("123456789012345678901234567890"));
    QVERIFY(result.error.isEmpty());
    // Validation failure must preserve an existing destination.
    QVERIFY(writeFixture(path, "[1,]"));
    QVERIFY(writeFixture(output, "keep"));
    request.kind = LargeTaskKind::Format;
    worker.run(request);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(readFixture(output), QByteArray("keep"));
    request.output = dir.filePath("new-invalid.json");
    worker.run(request);
    QVERIFY(!QFileInfo::exists(request.output));
    request.output = path;
    worker.run(request);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(readFixture(path), QByteArray("[1,]"));
#ifdef Q_OS_WIN
    QVERIFY(writeFixture(path, "[1]"));
    request.output = path.toUpper();
    worker.run(request);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(readFixture(path), QByteArray("[1]"));
#endif
    // Raw export is bounded even for a node over the clipboard limit.
    const QByteArray big = "\"" + QByteArray(2 * 1024 * 1024, 'x') + "\"";
    QVERIFY(writeFixture(path, big));
    request.output = output; request.start = 0; request.end = big.size(); request.kind = LargeTaskKind::Copy;
    worker.run(request);
    QVERIFY(!result.error.isEmpty());
    request.kind = LargeTaskKind::Export;
    worker.run(request);
    QVERIFY(result.error.isEmpty());
    QCOMPARE(readFixture(output), big);
    class BrokenOutput : public QIODevice {
    public:
        qint64 readData(char *, qint64) override { return -1; }
        qint64 writeData(const char *, qint64) override { setErrorString("Simulated disk failure"); return -1; }
    } broken;
    QVERIFY(broken.open(QIODevice::WriteOnly));
    MemoryJsonSource longSource(big);
    LargeTreeScanner failingWriter(longSource, [] { return false; });
    auto failed = failingWriter.transform(broken, true);
    QCOMPARE(failed.error, QString("Simulated disk failure"));
    QVERIFY(!failed.done);
    request.expectedSize = QFileInfo(path).size(); request.expectedModified = QFileInfo(path).lastModified();
    QVERIFY(writeFixture(path, "[]"));
    worker.run(request);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(readFixture(output), big);
}

void JsonTests::largeTaskLocate()
{
    QByteArray input = "{\"arr\":[";
    for (int i = 0; i < 700; ++i) {
        if (i) input += ',';
        input += "{\"a\":" + QByteArray::number(i) + "}";
    }
    input += "]}";
    const int offset = input.indexOf("650");
    MemoryJsonSource source(input);
    LargeTreeScanner scanner(source, [] { return false; });
    auto located = scanner.locate(offset);
    QVERIFY2(located.error.isEmpty(), qPrintable(located.error));
    QCOMPARE(located.records.size(), 4);
    QCOMPARE(located.records[2].row, qint64(650));
    QCOMPARE(located.records[3].preview, QString("650"));
    LargeTreeScanner keyScanner(source, [] { return false; });
    QCOMPARE(keyScanner.locate(offset - 3).records.last().key, QString("a"));
    QTemporaryDir dir;
    QVERIFY(writeFixture(dir.filePath("locate.json"), input));
    LargeTreeModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    tester.setUseFetchMore(false);
    model.openFile(dir.filePath("locate.json"));
    QTRY_VERIFY(!model.busy());
    const auto selected = model.revealPath(located.records);
    QCOMPARE(model.record(selected)->preview, QString("650"));
    QCOMPARE(model.residentNodes(), 4);
    const auto array = selected.parent().parent();
    QCOMPARE(model.firstRow(array), qint64(650));
    model.requestMore(array);
    QTRY_VERIFY(!model.busy());
    QVERIFY(!model.canFetchMore(model.index(0, 0)));
    QCOMPARE(model.record(model.index(1, 0, array))->row, qint64(651));
    model.changePage(array, false);
    QTRY_VERIFY(!model.busy());
    QCOMPARE(model.record(model.index(0, 0, array))->row, qint64(394));
}

void JsonTests::largeTaskCancellation()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("long.json"), output = dir.filePath("output.json");
    QFile source(path);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("[\"");
    const QByteArray chunk(1024 * 1024, 'x');
    for (int i = 0; i < 64; ++i) source.write(chunk);
    source.write("\"]"); source.close();
    QVERIFY(writeFixture(output, "original"));
    LargeFileTasks tasks;
    QSignalSpy completed(&tasks, &LargeFileTasks::completed);
    int ticks = 0;
    QTimer heartbeat;
    connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; });
    heartbeat.start(1);
    bool cancelled = false;
    connect(&tasks, &LargeFileTasks::progress, this, [&](qint64, qint64) {
        if (!cancelled) { cancelled = true; tasks.cancel(); }
    });
    LargeTaskRequest request;
    request.path = path; request.output = output; request.kind = LargeTaskKind::Format;
    tasks.start(request);
    QTRY_VERIFY_WITH_TIMEOUT(cancelled, 5000);
    QVERIFY(ticks > 0);
    // A following task proves cancellation has returned from the writer and
    // cleaned its temporary file; no cancelled completion may be delivered.
    request.kind = LargeTaskKind::Copy; request.start = 0; request.end = 2;
    tasks.start(request);
    QTRY_VERIFY_WITH_TIMEOUT(!tasks.busy(), 2000);
    QCOMPARE(completed.size(), 1);
    QCOMPARE(readFixture(output), QByteArray("original"));
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 2);
    // Closing the task owner while a write is active also discards the output.
    auto closing = std::make_unique<LargeFileTasks>();
    connect(closing.get(), &LargeFileTasks::progress, this, [&](qint64, qint64) { closing.reset(); });
    request.kind = LargeTaskKind::Format; request.output = dir.filePath("never-committed.json");
    closing->start(request);
    QTRY_VERIFY_WITH_TIMEOUT(!closing, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 2, 2000);
    QVERIFY(!QFileInfo::exists(request.output));
}

void JsonTests::largeDepthBoundary_data()
{
    QTest::addColumn<int>("layers");
    QTest::addColumn<bool>("valid");
    QTest::newRow("511-layers") << 511 << true;
    QTest::newRow("512-layers") << 512 << true;
    QTest::newRow("513-layers") << 513 << false;
    QTest::newRow("2048-layers") << 2048 << false;
}
void JsonTests::largeDepthBoundary()
{
    QFETCH(int, layers);
    QFETCH(bool, valid);
    MemoryJsonSource source(QByteArray(layers, '[') + "0" + QByteArray(layers, ']'));
    LargeTreeScanner rootScanner(source, [] { return false; });
    const auto root = rootScanner.root(true).records.first();
    LargeTreeScanner children(source, [] { return false; });
    auto indexed = children.children(root, 0, -1);
    QCOMPARE(indexed.error.isEmpty(), valid);
    QBuffer output; QVERIFY(output.open(QIODevice::WriteOnly));
    LargeTreeScanner formatter(source, [] { return false; });
    QCOMPARE(formatter.transform(output, false).error.isEmpty(), valid);
    if (valid) {
        LargeTreeScanner nextLevel(source, [] { return false; });
        auto nested = nextLevel.children(indexed.records.first(), 0, -1);
        QVERIFY(nested.error.isEmpty());
        QCOMPARE(nested.records.first().depth, 2);
    }
}

void JsonTests::largeModeSwitching()
{
    QTemporaryDir directory;
    const auto large = directory.filePath("large.json"), small = directory.filePath("small.json");
    QVERIFY(writeFixture(large, "[" + QByteArray("0,").repeated(300000) + "0]"));
    QVERIFY(writeFixture(small, "{\"editable\":123}"));
    JsonTab tab;
    for (int cycle = 0; cycle < 10; ++cycle) {
        QString error;
        QVERIFY(tab.openFile(large, JsonTab::OpenMode::LargeFile, &error));
        auto *view = tab.findChild<LargeFileView *>();
        auto *model = view->findChild<LargeTreeModel *>();
        QTRY_VERIFY(!view->isLoading() && !model->busy());
        const auto cache = model->temporaryIndexPath();
        LargeTaskRequest request; request.kind = LargeTaskKind::Format; request.path = large;
        request.output = directory.filePath(QString("cancelled-%1.json").arg(cycle));
        view->findChild<LargeFileTasks *>()->start(request);
        QVERIFY(tab.openFile(small, JsonTab::OpenMode::Automatic, &error));
        QVERIFY(!tab.isLargeFile());
        QCOMPARE(tab.text(), QString("{\"editable\":123}"));
        tab.formatJson(false);
        QVERIFY(tab.hasDocument());
        tab.findText("editable");
        QCOMPARE(tab.matchCount(), 1);
        QTRY_VERIFY(!QFileInfo::exists(cache));
        QVERIFY(!QFileInfo::exists(request.output));
    }
    QTRY_COMPARE(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden).size(), 2);
}

#ifdef _MSC_VER
#include <crtdbg.h>
#endif
int main(int argc, char **argv)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
    app.setFont(QFont("Microsoft YaHei", 10));
#endif
    QTemporaryDir settingsDirectory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    JsonTests tests;
    return QTest::qExec(&tests, argc, argv);
}

void JsonTests::collapsedTreeButtonPosition()
{
    JsonTab tab;
    tab.resize(900, 600);
    tab.show();
    auto *editor = tab.findChild<QPlainTextEdit *>();
    auto *button = tab.findChild<QPushButton *>("showJsonTreeButton");
    QVERIFY(editor && button);
    for (int layout = 0; layout < 2; ++layout) {
        for (int iteration = 0; iteration < 3; ++iteration) {
            tab.setTreeVisible(true);
            QApplication::processEvents();
            QVERIFY(!button->isVisible());
            const QSize unchangedTabSize = tab.size();
            tab.setTreeVisible(false);
            QTRY_VERIFY(button->isVisible());
            QTRY_COMPARE(button->x() + button->width(), editor->width());
            QTRY_COMPARE(button->y(), (editor->height() - button->height()) / 2);
            QCOMPARE(tab.size(), unchangedTabSize);
            tab.resize(tab.width() + 20, tab.height() + 10);
            QTRY_COMPARE(button->x() + button->width(), editor->width());
            QTest::mouseClick(button, Qt::LeftButton);
            QVERIFY(tab.isTreeVisible());
        }
        tab.toggleLayout();
    }
}

void JsonTests::languageSwitching()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QVERIFY(QFile::exists(":/i18n/hellojson_zh_CN.qm"));
    QVERIFY(QFile::exists(":/i18n/hellojson_zh_TW.qm"));
#endif
    MainWindow window;
    window.show();
    auto *tabs = window.findChild<QTabWidget *>();
    auto *tab = qobject_cast<JsonTab *>(tabs->currentWidget());
    tab->setText("{broken}");
    tab->formatJson(false);
    const QString original = tab->text();
    for (const auto &language : {QString("zh_CN"), QString("zh_TW"), QString("en"), QString("zh_TW")}) {
        const bool traditional = language == "zh_TW";
        const bool english = language == "en";
        auto *action = window.findChild<QAction *>(english ? "actionEnglish" :
                          traditional ? "actionTraditionalChinese" : "actionChinese");
        QVERIFY(action);
        action->trigger();
        QVERIFY(action->isChecked());
        QCOMPARE(tabs->tabText(0), english ? QString("Untitled") : QString::fromUtf8("未命名"));
        QCOMPARE(tab->text(), original);
        QCOMPARE(QCoreApplication::translate("MainWindow", "Paste"), english ? QString("Paste") :
                 traditional ? QString::fromUtf8("貼上") : QString::fromUtf8("粘贴"));
        QCOMPARE(QCoreApplication::translate("LargeFile", "Search File"), english ? QString("Search File") :
                 traditional ? QString::fromUtf8("搜尋檔案") : QString::fromUtf8("搜索文件"));
        if (!english) {
            QVERIFY(!tab->parseError().contains("Expected string key"));
            // Qt's standard editor context menu is translated too.
            auto *editor = tab->findChild<QPlainTextEdit *>();
            std::unique_ptr<QMenu> menu(editor->createStandardContextMenu());
            QVERIFY(!menu->actions().first()->text().contains("Undo"));
        }
        QApplication::processEvents();
    }
    tab->setProperty("defaultTitle", false);
    tabs->setTabText(0, QString::fromUtf8("自訂名稱"));
    window.findChild<QAction *>("actionChinese")->trigger();
    QCOMPARE(tabs->tabText(0), QString::fromUtf8("自訂名稱"));
    window.findChild<QAction *>("actionTraditionalChinese")->trigger();
    window.resize(1200, 780);
    QApplication::processEvents();
    QVERIFY(window.grab().save(QCoreApplication::applicationDirPath() + "/ui-zh-TW.png"));
}

void JsonTests::safeSaveAndModification()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("document.json");
    QVERIFY(writeFixture(path, "[1]"));
    JsonTab tab;
    QString error;
    QVERIFY(tab.openFile(path, JsonTab::OpenMode::Automatic, &error));
    QVERIFY(!tab.isModified());
    tab.setText("[2]");
    QVERIFY(tab.isModified());
    QVERIFY(!tab.saveFile(dir.path(), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(tab.isModified());
    QCOMPARE(readFixture(path), QByteArray("[1]"));
    QVERIFY(tab.saveFile(path, &error));
    QVERIFY(!tab.isModified());
    QCOMPARE(readFixture(path), QByteArray("[2]"));
    auto *editor = tab.findChild<QPlainTextEdit *>();
    editor->moveCursor(QTextCursor::End);
    editor->insertPlainText(" ");
    QVERIFY(tab.isModified());
    editor->undo();
    QVERIFY(!tab.isModified());
    tab.clear();
    QVERIFY(tab.isModified());
    QVERIFY(tab.saveFile(path, &error));
    QCOMPARE(QFileInfo(path).size(), qint64(0));
    QVERIFY(!tab.isModified());
}

void JsonTests::unsavedProtection()
{
    auto answer = [](QMessageBox::StandardButton button) {
        QTimer::singleShot(0, [button] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                box->button(button)->click();
        });
    };
    QTemporaryDir dir;
    const auto path = dir.filePath("save.json");
    QVERIFY(writeFixture(path, "{}"));
    MainWindow window;
    window.show();
    auto *tabs = window.findChild<QTabWidget *>();
    auto *tab = qobject_cast<JsonTab *>(tabs->currentWidget());
    QString error;
    QVERIFY(tab->openFile(path, JsonTab::OpenMode::Automatic, &error));
    tab->setText("[42]");
    QMetaObject::invokeMethod(&window, "onNewTab");
    tabs->setCurrentWidget(tab);
    answer(QMessageBox::Cancel);
    QMetaObject::invokeMethod(&window, "onCloseTab");
    QCOMPARE(tabs->count(), 2);
    QCOMPARE(tab->text(), QString("[42]"));
    answer(QMessageBox::Save);
    QMetaObject::invokeMethod(&window, "onCloseTab");
    QCOMPARE(tabs->count(), 1);
    QCOMPARE(readFixture(path), QByteArray("[42]"));
    tab = qobject_cast<JsonTab *>(tabs->currentWidget());
    tab->setText("unsaved");
    answer(QMessageBox::Cancel);
    QVERIFY(!window.close());
    QVERIFY(window.isVisible());
    answer(QMessageBox::Discard);
    QVERIFY(window.close());
}

void JsonTests::editingBudgets()
{
    JsonTab tab;
    tab.show();
    tab.setText("keep");
    QSignalSpy rejected(&tab, &JsonTab::protectionMessage);
    QApplication::clipboard()->setText(QString(BoundedEditor::CharacterLimit + 1, 'x'));
    tab.paste();
    QCOMPARE(tab.text(), QString("keep"));
    QCOMPARE(rejected.size(), 1);
    auto *editor = tab.findChild<QPlainTextEdit *>();
    QTest::keyClick(editor, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(tab.text(), QString("keep"));
    tab.setText(QString(15000, 'a'));
    tab.findText("a");
    QCOMPARE(tab.matchCount(), 5000);
    QVERIFY(tab.searchLimited());
    const QString source = QString(510, '[') + QString("0,").repeated(2000) + "0" + QString(510, ']');
    tab.setText(source);
    tab.formatJson(false);
    QCOMPARE(tab.text(), source);
    QVERIFY(rejected.size() >= 3);
    JsonIndex index;
    QVERIFY(!index.build("[" + QByteArray("0,").repeated(250001) + "0]"));
    QVERIFY(index.errorMessage().contains("Too many nodes"));
}

void JsonTests::settingsPersistence()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "HelloJson", "HelloJson");
    settings.clear();
    settings.sync();
    QByteArray layout;
    {
        MainWindow window;
        window.show();
        window.resize(1040, 720);
        window.findChild<QAction *>("actionTraditionalChinese")->trigger();
        auto *tab = window.findChild<JsonTab *>();
        tab->toggleLayout();
        layout = tab->viewState();
        QVERIFY(window.close());
    }
    settings.sync();
    QCOMPARE(settings.value("language").toString(), QString("zh_TW"));
    QVERIFY(!settings.value("geometry").toByteArray().isEmpty());
    MainWindow reopened;
    QVERIFY(reopened.findChild<QAction *>("actionTraditionalChinese")->isChecked());
    QCOMPARE(reopened.findChild<QSplitter *>("editorSplitter")->orientation(), Qt::Vertical);
    QCOMPARE(settings.value("editorLayout").toByteArray(), layout);
    settings.clear();
}
