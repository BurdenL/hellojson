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
        QCOMPARE(tab->matchCount(), 1);
        QVERIFY(search->hasFocus());
        QVERIFY(QMetaObject::invokeMethod(&window, "onFindClose"));
        for (auto *action : window.findChildren<QAction *>())
            if (action->objectName() == "actionChinese") action->trigger();
        QCOMPARE(tab->text(), compressed);
        QVERIFY(QMetaObject::invokeMethod(&window, "onNewTab"));
        auto *second = qobject_cast<JsonTab *>(tabs->currentWidget());
        QVERIFY(second && second != tab);
        second->setText(R"({"another":true})");
        QVERIFY(QMetaObject::invokeMethod(&window, "onFormatClicked"));
        QVERIFY(second->hasDocument());
        tabs->setCurrentWidget(tab);
        QCOMPARE(tab->text(), compressed);
        tabs->tabBar()->moveTab(0, 1);
        QCOMPARE(tabs->currentWidget(), tab);
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
    JsonTests tests;
    return QTest::qExec(&tests, argc, argv);
}
