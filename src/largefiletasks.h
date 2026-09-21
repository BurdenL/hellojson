#pragma once
#include "largetreeindex.h"
#include <QDateTime>

enum class LargeTaskKind { Search, Copy, Export, Format, Compact, Locate };
struct LargeTaskRequest
{
    LargeTaskKind kind = LargeTaskKind::Search;
    QString path, output, query;
    qint64 start = 0, end = 0;
    qint64 expectedSize = -1;
    QDateTime expectedModified;
    quint64 generation = 0;
};
struct LargeTaskResult
{
    LargeTaskKind kind = LargeTaskKind::Search;
    quint64 generation = 0;
    QString error, output;
    QByteArray copied;
    QVector<LargeTreeRecord> path;
    qint64 nextOffset = 0;
    bool more = false;
};
Q_DECLARE_METATYPE(LargeTaskRequest)
Q_DECLARE_METATYPE(LargeTaskResult)

class LargeTaskWorker : public QObject
{
    Q_OBJECT
public:
    static constexpr qint64 CopyLimit = 1024 * 1024;
    static constexpr int SearchLimit = 128;
    explicit LargeTaskWorker(std::shared_ptr<std::atomic<quint64>> generation);
public slots:
    void run(const LargeTaskRequest &request);
signals:
    void finished(const LargeTaskResult &result);
    void matches(quint64 generation, const QVector<qint64> &offsets);
    void progress(quint64 generation, qint64 position, qint64 total);
private:
    std::shared_ptr<std::atomic<quint64>> m_generation;
};

class QThread;
class LargeFileTasks : public QObject
{
    Q_OBJECT
public:
    explicit LargeFileTasks(QObject *parent = nullptr);
    ~LargeFileTasks() override;
    // Observe lifetime only; ownership remains internal (deleted after finishing).
    QThread *workerThreadForDiagnostics() const { return m_thread; }
    void start(LargeTaskRequest request);
    void cancel();
    bool busy() const { return m_busy; }
signals:
    void requested(const LargeTaskRequest &request);
    void completed(const LargeTaskResult &result);
    void matches(const QVector<qint64> &offsets);
    void progress(qint64 position, qint64 total);
    void busyChanged(bool busy);
private:
    QThread *m_thread;
    std::shared_ptr<std::atomic<quint64>> m_generation;
    bool m_busy = false;
};
