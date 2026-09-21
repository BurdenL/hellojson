#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <QFontDatabase>
#include <QTreeView>
#include "jsontab.h"
#include "largefileview.h"
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

struct Memory { double privateMiB = 0; double peakWorkingMiB = 0; };
int stressLargeFile(QApplication &app, const QString &path, const QString &report);
int faultLargeFile(QApplication &app, const QString &path, const QString &report);
static Memory memory()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters)))
        return {counters.PrivateUsage / 1048576.0, counters.PeakWorkingSetSize / 1048576.0};
#endif
    return {};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
    app.setFont(QFont("Microsoft YaHei", 10));
#endif
    const auto args = app.arguments();
    if (args.size() == 4 && args[1] == "stress") return stressLargeFile(app, args[2], args[3]);
    if (args.size() == 4 && args[1] == "fault") return faultLargeFile(app, args[2], args[3]);
    if (args.size() != 4 || (args[1] != "legacy" && args[1] != "paged" && args[1] != "tree" && args[1] != "tasks")) return 2;
    const bool legacy = args[1] == "legacy";
    const QString path = args[2], report = args[3];
    // Baseline is intentionally capped: do not reproduce an OOM with a 1 GiB
    // document just to demonstrate that the old approach is unbounded.
    if (legacy && QFileInfo(path).size() > 8 * 1024 * 1024) return 3;
    JsonTab tab;
    tab.resize(1200, 780);
    tab.show();
    app.processEvents();
    const auto before = memory();
    QElapsedTimer heartbeatClock;
    heartbeatClock.start();
    qint64 maxGap = 0;
    QTimer heartbeat;
    heartbeat.setInterval(1);
    QObject::connect(&heartbeat, &QTimer::timeout, [&] {
        maxGap = qMax(maxGap, heartbeatClock.restart());
    });
    heartbeat.start();
    QElapsedTimer clock;
    clock.start();
    QString error;
    qint64 firstMs = 0, maxNavigationMs = 0;
    JsonTextPage finalPage;
    QJsonObject treeResult;
    QJsonObject taskResult;
    auto awaitPage = [](LargeFileView *view) {
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(view, &LargeFileView::pageLoaded, &loop, &QEventLoop::quit);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(15000);
        if (view->isLoading()) loop.exec();
        return !view->isLoading() && view->currentPage().error.isEmpty();
    };
    if (legacy) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return 4;
        QTextStream stream(&file);
        const QString fullText = stream.readAll();
        tab.setText(fullText);
        app.processEvents();
        firstMs = clock.elapsed();
    } else {
        if (!tab.openFile(path, JsonTab::OpenMode::LargeFile, &error)) return 5;
        auto *view = tab.findChild<LargeFileView *>();
        if (!view || !awaitPage(view)) return 6;
        app.processEvents();
        firstMs = clock.elapsed();
        for (int i = 0; i < 16; ++i) {
            clock.restart();
            view->goToByte((QFileInfo(path).size() - 1) * i / 15);
            if (!awaitPage(view)) return 7;
            app.processEvents();
            maxNavigationMs = qMax(maxNavigationMs, clock.elapsed());
        }
        finalPage = view->currentPage();
        if (args[1] == "tree") {
            auto *tree = view->findChild<QTreeView *>("largeFileTree");
            auto *model = qobject_cast<LargeTreeModel *>(tree->model());
            auto awaitTree = [&] {
                QEventLoop loop;
                QTimer timeout;
                timeout.setSingleShot(true);
                QObject::connect(model, &LargeTreeModel::batchLoaded, &loop, &QEventLoop::quit);
                QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
                timeout.start(15000);
                // A visible QTreeView can request the next batch immediately
                // when the preceding batch arrives.
                while (model->busy() && timeout.isActive()) loop.exec();
                return !model->busy();
            };
            if (!awaitTree() || model->rowCount() != 1) return 9;
            auto root = model->index(0, 0);
            tree->expand(root);
            qint64 maxTreeMs = 0;
            for (int page = 0; page < 20; ++page) {
                clock.restart();
                if (page) model->changePage(root, true);
                if (!awaitTree()) return 10;
                while (model->canFetchMore(root)) {
                    model->fetchMore(root);
                    if (!awaitTree()) return 10;
                }
                app.processEvents();
                if (model->rowCount(root) != LargeTreeModel::WindowSize) return 11;
                maxTreeMs = qMax(maxTreeMs, clock.elapsed());
            }
            model->changePage(root, false);
            if (!awaitTree()) return 10;
            treeResult = {
                {"pages_visited", 20}, {"max_tree_page_ms", double(maxTreeMs)},
                {"resident_nodes", model->residentNodes()},
                {"index_disk_bytes", double(model->diskBytes())},
                {"back_page_cache_hit", model->lastCacheHit()}
            };
            model->releaseChildren(root);
            treeResult["nodes_after_collapse"] = model->residentNodes();
        }
        if (args[1] == "tasks") {
            auto *tasks = view->findChild<LargeFileTasks *>();
            LargeTaskResult task;
            auto run = [&](LargeTaskRequest request, const QString &name) {
                const QFileInfo info(request.path);
                request.expectedSize = info.size(); request.expectedModified = info.lastModified();
                QEventLoop loop;
                QTimer timeout;
                timeout.setSingleShot(true);
                bool completed = false;
                QObject::connect(tasks, &LargeFileTasks::completed, &loop, [&](const LargeTaskResult &result) {
                    task = result; completed = true; loop.quit();
                });
                QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
                QElapsedTimer elapsed; elapsed.start();
                timeout.start(600000);
                tasks->start(request);
                loop.exec();
                taskResult[name + "_ms"] = double(elapsed.elapsed());
                QFile checkpoint(report + ".progress");
                if (checkpoint.open(QIODevice::WriteOnly))
                    checkpoint.write(QJsonDocument(taskResult).toJson());
                return completed && task.error.isEmpty();
            };
            LargeTaskRequest request;
            request.path = path; request.kind = LargeTaskKind::Search;
            request.query = "__hellojson_benchmark_absent__";
            if (!run(request, "full_search")) return 12;
            request.kind = LargeTaskKind::Locate;
            request.start = qMin(QFileInfo(path).size() - 1, qint64(20 * 65536 + 100));
            if (!run(request, "locate")) return 13;
            taskResult["located_depth"] = task.path.size();
            request.kind = LargeTaskKind::Export; request.start = 0; request.end = QFileInfo(path).size();
            // Unique scratch outputs alongside the report, cleaned after success.
            request.output = report + ".export.json";
            if (QFileInfo::exists(request.output) || !run(request, "export")) return 14;
            if (QFileInfo(request.output).size() != QFileInfo(path).size()) return 15;
            QFile::remove(request.output);
            request.kind = LargeTaskKind::Format; request.output = report + ".formatted.json";
            if (QFileInfo::exists(request.output) || !run(request, "format")) return 16;
            const QString formatted = request.output;
            taskResult["formatted_bytes"] = double(QFileInfo(formatted).size());
            request.path = formatted; request.kind = LargeTaskKind::Compact; request.output = report + ".compact.json";
            if (QFileInfo::exists(request.output) || !run(request, "compress")) return 17;
            taskResult["compressed_bytes"] = double(QFileInfo(request.output).size());
            QFile::remove(formatted); QFile::remove(request.output);
        }
        tab.grab().save(QFileInfo(report).absolutePath() + "/paged-benchmark.png");
    }
    app.processEvents();
    maxGap = qMax(maxGap, heartbeatClock.elapsed());
    const auto after = memory();
    QJsonObject result{
        {"mode", args[1]}, {"file_bytes", double(QFileInfo(path).size())},
        {"first_display_ms", double(firstMs)}, {"max_navigation_ms", double(maxNavigationMs)},
        {"max_event_loop_gap_ms", double(maxGap)},
        {"private_mib", after.privateMiB}, {"private_delta_mib", after.privateMiB - before.privateMiB},
        {"peak_working_set_mib", after.peakWorkingMiB},
        {"cached_file_bytes", double(finalPage.cacheBytes)}, {"disk_bytes_read", double(finalPage.bytesRead)},
        {"page_characters", finalPage.text.size()}
    };
    if (!treeResult.isEmpty()) result["tree"] = treeResult;
    if (!taskResult.isEmpty()) result["tasks"] = taskResult;
    QFile output(report);
    if (!output.open(QIODevice::WriteOnly)) return 8;
    output.write(QJsonDocument(result).toJson());
    return 0;
}
