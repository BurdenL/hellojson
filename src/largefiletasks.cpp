#include "largefiletasks.h"
#include <QThread>
#include <QFileInfo>
#include <QSaveFile>
#include <QElapsedTimer>
#include <QCoreApplication>

static QString taskText(const char *text)
{
    return QCoreApplication::translate("LargeFile", text);
}

LargeTaskWorker::LargeTaskWorker(std::shared_ptr<std::atomic<quint64>> generation)
    : m_generation(std::move(generation)) {}

void LargeTaskWorker::run(const LargeTaskRequest &request)
{
    auto cancelled = [&] { return request.generation != m_generation->load(); };
    if (cancelled()) return;
    LargeTaskResult result;
    result.kind = request.kind; result.generation = request.generation; result.output = request.output;
    FileJsonSource source;
    if (!source.open(request.path, &result.error)) { emit finished(result); return; }
    const QFileInfo input(request.path);
    if (request.expectedSize >= 0 && (input.size() != request.expectedSize ||
        input.lastModified() != request.expectedModified)) {
        result.error = taskText("The file changed on disk. Reopen it to continue.");
        emit finished(result); return;
    }
    QElapsedTimer clock; clock.start();
    auto progress = [&](qint64 offset) {
        if (clock.elapsed() >= 100) {
            emit this->progress(request.generation, offset, source.size()); clock.restart();
        }
    };
    if (request.kind == LargeTaskKind::Search) {
        const QByteArray needle = request.query.toUtf8();
        if (needle.isEmpty() || needle.size() > 4096 || request.start < 0 || request.start > source.size()) {
            result.error = taskText("Enter 1–4096 UTF-8 bytes to search.");
        } else {
            QByteArray carry;
            QVector<qint64> hits;
            int count = 0;
            qint64 position = request.start;
            while (position < source.size() && result.error.isEmpty() && !cancelled()) {
                auto bytes = source.read(position, qMin(JsonDataSource::BlockBytes, source.size() - position), &result.error);
                if (!result.error.isEmpty()) break;
                auto window = carry + bytes;
                int from = 0;
                for (;;) {
                    const auto found = window.indexOf(needle, from);
                    if (found < 0 || cancelled()) break;
                    const qint64 offset = position - carry.size() + found;
                    hits.append(offset);
                    ++count;
                    result.nextOffset = offset + 1;
                    if (hits.size() == 32) { emit matches(request.generation, hits); hits.clear(); }
                    if (count == SearchLimit) { result.more = result.nextOffset < source.size(); break; }
                    from = int(found) + 1;
                }
                if (count == SearchLimit) break;
                carry = window.right(needle.size() - 1);
                position += bytes.size();
                progress(position);
            }
            if (!hits.isEmpty() && !cancelled() && result.error.isEmpty()) emit matches(request.generation, hits);
            if (count < SearchLimit) result.nextOffset = source.size();
        }
    } else if (request.kind == LargeTaskKind::Locate) {
        if (request.start < 0 || request.start > source.size()) result.error = taskText("Invalid or oversized read request.");
        else {
            LargeTreeScanner scanner(source, cancelled, progress);
            auto located = scanner.locate(request.start);
            result.error = located.error;
            result.path = std::move(located.records);
        }
    } else if (request.kind == LargeTaskKind::Copy) {
        if (request.start < 0 || request.end < request.start || request.end > source.size() ||
            request.end - request.start > CopyLimit)
            result.error = taskText("This node is too large to copy. Export it to a file.");
        else result.copied = source.read(request.start, request.end - request.start, &result.error);
    } else {
        const bool raw = request.kind == LargeTaskKind::Export;
        const QFileInfo destination(request.output);
        auto samePath = [](const QString &left, const QString &right) {
#ifdef Q_OS_WIN
            constexpr auto sensitivity = Qt::CaseInsensitive;
#else
            constexpr auto sensitivity = Qt::CaseSensitive;
#endif
            return !left.isEmpty() && !right.isEmpty() && left.compare(right, sensitivity) == 0;
        };
        if (request.output.isEmpty() || samePath(destination.absoluteFilePath(), input.absoluteFilePath()) ||
            samePath(destination.canonicalFilePath(), input.canonicalFilePath())) {
            result.error = taskText("Choose an output file different from the source.");
        } else if (raw && (request.start < 0 || request.end < request.start || request.end > source.size())) {
            result.error = taskText("Invalid or oversized read request.");
        } else {
            QSaveFile output(request.output);
            output.setDirectWriteFallback(false);
            if (!output.open(QIODevice::WriteOnly)) result.error = output.errorString();
            else {
                if (raw) {
                    for (qint64 position = request.start; position < request.end && !cancelled();) {
                        auto bytes = source.read(position, qMin(JsonDataSource::BlockBytes, request.end - position), &result.error);
                        if (!result.error.isEmpty()) break;
                        if (output.write(bytes) != bytes.size()) { result.error = output.errorString(); break; }
                        position += bytes.size(); progress(position);
                    }
                } else {
                    LargeTreeScanner scanner(source, cancelled, progress);
                    auto transformed = scanner.transform(output, request.kind == LargeTaskKind::Format);
                    result.error = transformed.error;
                }
                if (result.error.isEmpty() && !cancelled()) source.read(0, 0, &result.error);
                if (result.error.isEmpty() && !cancelled()) {
                    if (!output.commit()) result.error = output.errorString();
                } else output.cancelWriting();
            }
        }
    }
    if (!cancelled() && result.error.isEmpty() &&
        (request.kind == LargeTaskKind::Search || request.kind == LargeTaskKind::Copy || request.kind == LargeTaskKind::Locate))
        source.read(0, 0, &result.error);
    if (!cancelled()) emit finished(result);
}

LargeFileTasks::LargeFileTasks(QObject *parent) : QObject(parent), m_thread(new QThread),
    m_generation(std::make_shared<std::atomic<quint64>>(0))
{
    qRegisterMetaType<LargeTaskRequest>();
    qRegisterMetaType<LargeTaskResult>();
    qRegisterMetaType<QVector<qint64>>();
    auto *worker = new LargeTaskWorker(m_generation);
    worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(this, &LargeFileTasks::requested, worker, &LargeTaskWorker::run);
    connect(worker, &LargeTaskWorker::finished, this, [this](const LargeTaskResult &result) {
        if (result.generation != m_generation->load()) return;
        m_busy = false;
        emit busyChanged(false);
        emit completed(result);
    });
    connect(worker, &LargeTaskWorker::matches, this, [this](quint64 generation, const QVector<qint64> &offsets) {
        if (generation == m_generation->load()) emit matches(offsets);
    });
    connect(worker, &LargeTaskWorker::progress, this, [this](quint64 generation, qint64 position, qint64 total) {
        if (generation == m_generation->load()) emit progress(position, total);
    });
    m_thread->start();
}
LargeFileTasks::~LargeFileTasks()
{
    ++*m_generation;
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->quit();
}
void LargeFileTasks::start(LargeTaskRequest request)
{
    request.generation = ++*m_generation;
    m_busy = true; emit busyChanged(true);
    emit requested(request);
}
void LargeFileTasks::cancel()
{
    ++*m_generation;
    m_busy = false; emit busyChanged(false);
}
