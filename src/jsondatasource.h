#pragma once

#include <QByteArray>
#include <QFile>
#include <QDateTime>
#include <QVector>
#include <QMetaType>
#include <utility>

// Bounded byte access shared by memory-backed documents and paged files.
// Instances are owned and used on one thread; all positions are 64-bit.
class JsonDataSource
{
public:
    static constexpr qint64 BlockBytes = 64 * 1024;
    static constexpr qint64 MaxReadBytes = 1024 * 1024;
    static constexpr qint64 LargeFileThreshold = 32 * 1024 * 1024;
    virtual ~JsonDataSource() = default;
    virtual qint64 size() const = 0;
    virtual QByteArray read(qint64 offset, qint64 length, QString *error) = 0;
    virtual qint64 residentBytes() const = 0;
};

class MemoryJsonSource final : public JsonDataSource
{
public:
    explicit MemoryJsonSource(QByteArray bytes) : m_bytes(std::move(bytes)) {}
    qint64 size() const override { return m_bytes.size(); }
    QByteArray read(qint64 offset, qint64 length, QString *error) override;
    qint64 residentBytes() const override { return m_bytes.size(); }
private:
    QByteArray m_bytes;
};

class FileJsonSource final : public JsonDataSource
{
public:
    static constexpr int CacheBlocks = 8; // 512 KiB per open file, independent of file size.
    bool open(const QString &path, QString *error);
    qint64 size() const override { return m_size; }
    QByteArray read(qint64 offset, qint64 length, QString *error) override;
    qint64 residentBytes() const override;
    qint64 bytesRead() const { return m_bytesRead; }
private:
    struct Block { qint64 offset; QByteArray bytes; };
    QFile m_file;
    qint64 m_size = 0;
    qint64 m_bytesRead = 0;
    QDateTime m_modified;
    QVector<Block> m_cache;
};

struct JsonTextPage
{
    QString text;
    QString error;
    qint64 start = 0;
    qint64 end = 0; // exclusive
    qint64 total = 0;
    qint64 page = 0;
    qint64 pages = 1;
    qint64 cacheBytes = 0;
    qint64 bytesRead = 0;
    quint64 request = 0;
};
Q_DECLARE_METATYPE(JsonTextPage)

// Pages meet at UTF-8 character boundaries, including a 4-byte character
// crossing a nominal 64 KiB boundary. JSON tokens need not fit in one page.
JsonTextPage readJsonTextPage(JsonDataSource &source, qint64 page);

// Editable mode deliberately rejects documents above the size threshold.
QString readSmallJsonText(JsonDataSource &source, QString *error);
