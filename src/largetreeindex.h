#pragma once
#include "jsondatasource.h"
#include <QVector>
#include <QTemporaryFile>
#include <QHash>
#include <QQueue>
#include <functional>
#include <atomic>
#include <memory>

struct LargeTreeRecord
{
    enum Type { Object, Array, String, Number, Boolean, Null };
    Type type = Null;
    int depth = 0;
    // UTF-8 文件字节范围 [start, end)，不是 QString 的字符索引。
    qint64 start = 0, end = 0, row = 0, keyStart = -1;
    QString key, preview; // bounded previews, never complete large values
    bool container() const { return type == Object || type == Array; }
};
struct LargeTreeRequest
{
    QString path;
    LargeTreeRecord parent;
    qint64 firstRow = 0;
    qint64 resumeOffset = -1;
    quint64 generation = 0;
    bool root = false;
};
struct LargeTreeBatch
{
    QVector<LargeTreeRecord> records;
    qint64 nextOffset = -1;
    bool done = false;
    bool cancelled = false;
    QString error;
    quint64 generation = 0;
    qint64 sourceCacheBytes = 0, diskBytes = 0;
    bool cacheHit = false;
    QString cachePath;
};
Q_DECLARE_METATYPE(LargeTreeRequest)
Q_DECLARE_METATYPE(LargeTreeBatch)

class LargeTreeScanner
{
public:
    static constexpr int BatchSize = 64;
    LargeTreeScanner(JsonDataSource &source, std::function<bool()> cancelled,
                     std::function<void(qint64)> progress = {});
    LargeTreeBatch root(bool shallow = false);
    LargeTreeBatch children(const LargeTreeRecord &parent, qint64 firstRow, qint64 resumeOffset);
    LargeTreeBatch transform(QIODevice &output, bool pretty);
    LargeTreeBatch locate(qint64 offset);
private:
    char peek();
    void advance();
    void whitespace();
    bool fail(const char *message);
    bool string();
    bool number();
    bool value(int depth, LargeTreeRecord::Type *type = nullptr);
    QString preview(qint64 start, qint64 end, bool quoted);
    bool finishRoot();
    void writeByte(char byte);
    void flushOutput();
    JsonDataSource &m_source;
    std::function<bool()> m_cancelled;
    std::function<void(qint64)> m_progress;
    QByteArray m_buffer;
    qint64 m_bufferStart = -1, m_pos = 0;
    QString m_error;
    bool m_stopped = false;
    QIODevice *m_output = nullptr;
    QByteArray m_outputBuffer;
    bool m_pretty = false, m_inString = false, m_escape = false;
    int m_indent = 0;
    char m_previous = 0;
};

class LargeTreeWorker : public QObject
{
    Q_OBJECT
public:
    static constexpr int CachePages = 128;
    static constexpr qint64 DiskBudget = 16 * 1024 * 1024;
    explicit LargeTreeWorker(std::shared_ptr<std::atomic<quint64>> generation);
public slots:
    void scan(const LargeTreeRequest &request);
signals:
    void ready(const LargeTreeBatch &batch);
    void progress(quint64 generation, qint64 offset);
private:
    struct Cached { qint64 offset; int length; };
    std::shared_ptr<std::atomic<quint64>> m_generation;
    QString m_path;
    std::unique_ptr<FileJsonSource> m_source;
    std::unique_ptr<QTemporaryFile> m_disk;
    QHash<QString, Cached> m_pages;
    QQueue<QString> m_order;
};
