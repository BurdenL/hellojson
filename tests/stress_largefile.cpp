#include "jsontab.h"
#include "largefileview.h"
#include <QApplication>
#include <QTabWidget>
#include <QTreeView>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QTimer>
#include <QPointer>
#include <QThread>
#include <functional>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#endif

static QJsonObject resources()
{
    QJsonObject result;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory))) {
        result["private_mib"] = memory.PrivateUsage / 1048576.0;
        result["working_set_mib"] = memory.WorkingSetSize / 1048576.0;
        result["peak_working_set_mib"] = memory.PeakWorkingSetSize / 1048576.0;
    }
    DWORD handles = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handles);
    result["handles"] = int(handles);
    int threads = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
        if (Thread32First(snapshot, &entry)) do {
            if (entry.th32OwnerProcessID == GetCurrentProcessId()) ++threads;
        } while (Thread32Next(snapshot, &entry));
        CloseHandle(snapshot);
    }
    result["threads"] = threads;
#endif
    return result;
}

static bool until(const std::function<bool()> &ready, int timeoutMs = 15000)
{
    if (ready()) return true;
    QEventLoop loop;
    QTimer poll, timeout;
    poll.setInterval(2); timeout.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (ready()) loop.quit(); });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(); timeout.start(timeoutMs);
    do { loop.exec(); } while (!ready() && timeout.isActive());
    return ready();
}

int stressLargeFile(QApplication &app, const QString &path, const QString &report)
{
    constexpr int TabCount = 8, Cycles = 3, Jumps = 4;
    QTabWidget tabs;
    tabs.resize(1200, 780); tabs.show(); app.processEvents();
    QTemporaryDir output;
    // Warm Qt's process-wide font/platform pools before comparing native
    // thread/handle counts; those pools are not owned by individual tabs.
    auto warmup = std::make_unique<JsonTab>(&tabs);
    tabs.addTab(warmup.get(), "warmup");
    QString warmupError;
    if (!warmup->openFile(path, JsonTab::OpenMode::LargeFile, &warmupError)) return 27;
    auto *warmView = warmup->findChild<LargeFileView *>();
    auto *warmModel = warmView->findChild<LargeTreeModel *>();
    if (!until([&] { return !warmView->isLoading() && !warmModel->busy(); })) return 28;
    const QString warmIndex = warmModel->temporaryIndexPath();
    tabs.removeTab(0); warmup.reset();
    QElapsedTimer settle; settle.start();
    if (!until([&] { return !QFileInfo::exists(warmIndex) && settle.elapsed() >= 250; })) return 29;
    const auto baseline = resources();
    QJsonArray cycles;
    QStringList indexes;
    QList<QPointer<QThread>> workers;
    QString failure;
    qint64 maxGap = 0, maxJumpMs = 0;
    int maxNodes = 0;
    qint64 maxTextCache = 0, maxIndexBytes = 0;
    QElapsedTimer gap; gap.start();
    QTimer heartbeat;
    heartbeat.setInterval(1);
    QObject::connect(&heartbeat, &QTimer::timeout, &tabs, [&] { maxGap = qMax(maxGap, gap.restart()); });
    heartbeat.start();
    auto require = [](bool ok, const QString &message) { if (!ok) throw message; };
    auto closeTabs = [&] {
        while (tabs.count()) {
            auto *widget = tabs.widget(0);
            tabs.removeTab(0); delete widget;
        }
    };
    auto clean = [&] {
        for (const auto &index : indexes) if (QFileInfo::exists(index)) return false;
        return QDir(output.path()).entryList(QDir::Files | QDir::Hidden).isEmpty();
    };
    auto treeReady = [&](LargeTreeModel *model) {
        require(until([&] { return !model->busy(); }), "Tree scan timed out");
    };
    auto fillWindow = [&](LargeTreeModel *model, const QModelIndex &root) {
        treeReady(model);
        while (model->hasMore(root)) {
            const int before = model->rowCount(root);
            model->requestMore(root); treeReady(model);
            require(model->rowCount(root) > before || !model->hasMore(root), "Tree scan made no progress");
            require(model->residentNodes() < LargeTreeModel::ResidentLimit, "Unexpected full tree budget");
        }
    };
    try {
        require(output.isValid(), "Cannot create temporary output directory");
        for (int cycle = 0; cycle < Cycles; ++cycle) {
            for (int i = 0; i < TabCount; ++i) {
                auto *tab = new JsonTab(&tabs);
                tabs.addTab(tab, QString::number(i)); tabs.setCurrentWidget(tab);
                QString error;
                require(tab->openFile(path, JsonTab::OpenMode::LargeFile, &error), error);
                auto *view = tab->findChild<LargeFileView *>();
                require(until([&] { return !view->isLoading(); }), "Initial page timed out");
                require(view->currentPage().error.isEmpty(), view->currentPage().error);
                auto *model = view->findChild<LargeTreeModel *>();
                treeReady(model);
                require(model->rowCount() == 1, "Root preview missing");
                indexes.append(model->temporaryIndexPath());
                workers.append(view->workerThreadForDiagnostics());
                workers.append(model->workerThreadForDiagnostics());
                workers.append(view->findChild<LargeFileTasks *>()->workerThreadForDiagnostics());
            }
            for (int jump = 0; jump < Jumps; ++jump) {
                qint64 aggregateCache = 0;
                for (int i = 0; i < TabCount; ++i) {
                    tabs.setCurrentIndex(i);
                    auto *view = tabs.widget(i)->findChild<LargeFileView *>();
                    QElapsedTimer navigation; navigation.start();
                    const qint64 position = (QFileInfo(path).size() - 1) * (jump * TabCount + i) / (Jumps * TabCount - 1);
                    view->goToByte(position);
                    require(until([&] { return !view->isLoading(); }), "Navigation timed out");
                    require(view->currentPage().error.isEmpty(), view->currentPage().error);
                    maxJumpMs = qMax(maxJumpMs, navigation.elapsed());
                    aggregateCache += view->currentPage().cacheBytes;
                    require(view->currentPage().cacheBytes <= 8 * JsonDataSource::BlockBytes, "Text cache exceeded budget");
                    auto *tree = view->findChild<QTreeView *>("largeFileTree");
                    auto *model = qobject_cast<LargeTreeModel *>(tree->model());
                    const auto root = model->index(0, 0);
                    if (model->record(root)->container()) {
                        tree->expand(root);
                        if (jump && model->canNextPage(root)) model->changePage(root, true);
                        fillWindow(model, root);
                        const auto child = model->index(0, 0, root);
                        if (model->record(child) && model->record(child)->container()) {
                            tree->expand(child); treeReady(model);
                            maxNodes = qMax(maxNodes, model->residentNodes());
                            tree->collapse(child);
                        }
                        maxNodes = qMax(maxNodes, model->residentNodes());
                        require(model->residentNodes() <= LargeTreeModel::ResidentLimit, "Resident nodes exceeded budget");
                        maxIndexBytes = qMax(maxIndexBytes, model->diskBytes());
                        require(model->diskBytes() <= LargeTreeWorker::DiskBudget, "Index exceeded budget");
                    }
                    auto *tasks = view->findChild<LargeFileTasks *>();
                    LargeTaskRequest search; search.path = path; search.query = QString::fromUtf8("中文");
                    QJsonObject searchStatus;
                    QMetaObject::Connection result = QObject::connect(tasks, &LargeFileTasks::completed, &tabs,
                        [&](const LargeTaskResult &r) { searchStatus["error"] = r.error; });
                    tasks->start(search);
                    require(until([&] { return !tasks->busy(); }), "Search timed out");
                    QObject::disconnect(result);
                    require(searchStatus.contains("error") && searchStatus["error"].toString().isEmpty(), "Search failed");
                }
                maxTextCache = qMax(maxTextCache, aggregateCache);
            }
            const auto loaded = resources();
            // Close all tabs while a real background writer is still active.
            auto *tasks = tabs.widget(0)->findChild<LargeFileTasks *>();
            bool started = false;
            QObject::connect(tasks, &LargeFileTasks::progress, &tabs, [&](qint64, qint64) { started = true; });
            LargeTaskRequest format; format.path = path; format.kind = LargeTaskKind::Format;
            format.output = output.filePath(QString("cancel-%1.json").arg(cycle));
            tasks->start(format);
            require(until([&] { return started; }, 10000), "Writer did not reach progress checkpoint");
            QElapsedTimer closing; closing.start();
            closeTabs();
            require(until(clean, 5000), "Temporary files survived closing tabs");
            require(until([&] {
                for (const auto &worker : workers) if (worker) return false;
                return true;
            }, 5000), "Worker threads survived closing tabs");
            const auto released = resources();
            cycles.append(QJsonObject{{"loaded", loaded}, {"closed", released}, {"close_cleanup_ms", double(closing.elapsed())}});
            require(released["peak_working_set_mib"].toDouble() < 512, "Peak working set exceeded 512 MiB");
            if (cycle) {
                const auto first = cycles.first().toObject()["closed"].toObject();
                require(released["private_mib"].toDouble() - first["private_mib"].toDouble() < 16,
                        "Closed-cycle private memory grew by more than 16 MiB");
                require(released["handles"].toInt() <= first["handles"].toInt() + 16, "Handle count did not stabilize");
            }
        }
    } catch (const QString &error) {
        failure = error;
        closeTabs(); until(clean, 5000);
    }
    QJsonObject result{
        {"file_bytes", double(QFileInfo(path).size())}, {"tabs_per_cycle", TabCount}, {"cycle_count", Cycles},
        {"jumps_per_tab_per_cycle", Jumps}, {"max_navigation_ms", double(maxJumpMs)},
        {"max_event_loop_gap_ms", double(maxGap)}, {"max_resident_nodes_per_tab", maxNodes},
        {"max_aggregate_text_cache_bytes", double(maxTextCache)}, {"max_index_bytes_per_tab", double(maxIndexBytes)},
        {"baseline_after_qt_warmup", baseline}, {"cycles", cycles}, {"temporary_files_removed", clean()},
        {"tracked_worker_count", workers.size()}, {"final_resources", resources()},
        {"passed", failure.isEmpty()}, {"error", failure}
    };
    QFile destination(report);
    if (!destination.open(QIODevice::WriteOnly)) return 20;
    destination.write(QJsonDocument(result).toJson());
    return failure.isEmpty() ? 0 : 21;
}

int faultLargeFile(QApplication &app, const QString &path, const QString &report)
{
    QTemporaryDir temporary;
    const QString destination = temporary.filePath("preserve.json");
    QFile original(destination);
    if (!original.open(QIODevice::WriteOnly)) return 22;
    original.write("original"); original.close();
    JsonTab tab; tab.resize(1200, 780); tab.show(); app.processEvents();
    QString failure;
    if (!tab.openFile(path, JsonTab::OpenMode::LargeFile, &failure)) return 23;
    auto *view = tab.findChild<LargeFileView *>();
    if (!until([&] { return !view->isLoading(); }) || !view->currentPage().error.isEmpty()) return 24;
    auto *tasks = view->findChild<LargeFileTasks *>();
    bool completed = false;
    LargeTaskResult result;
    QObject::connect(tasks, &LargeFileTasks::completed, &tab, [&](const LargeTaskResult &r) { result = r; completed = true; });
    LargeTaskRequest request; request.kind = LargeTaskKind::Format; request.path = path; request.output = destination;
    QElapsedTimer elapsed; elapsed.start();
    tasks->start(request);
    const bool returned = until([&] { return completed; }, 600000);
    const bool readable = original.open(QIODevice::ReadOnly);
    const bool preserved = readable && original.readAll() == "original"; original.close();
    const bool cleaned = QDir(temporary.path()).entryList(QDir::Files | QDir::Hidden).size() == 1;
    const auto memory = resources();
    const bool passed = returned && !result.error.isEmpty() && preserved && cleaned &&
                        memory["peak_working_set_mib"].toDouble() < 512;
    QJsonObject data{{"file_bytes", double(QFileInfo(path).size())}, {"validation_ms", double(elapsed.elapsed())},
        {"validation_error", result.error}, {"destination_preserved", preserved}, {"temporary_output_removed", cleaned},
        {"resources", memory}, {"passed", passed}};
    QFile output(report);
    if (!output.open(QIODevice::WriteOnly)) return 25;
    output.write(QJsonDocument(data).toJson());
    return passed ? 0 : 26;
}
