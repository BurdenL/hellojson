#include "jsondatasource.h"
#include <QFileInfo>
#include <QCoreApplication>
#include <algorithm>
#include <QTextStream>

static QString message(const char *text)
{
    return QCoreApplication::translate("LargeFile", text);
}

static bool validRange(qint64 size, qint64 offset, qint64 length, QString *error)
{
    if (error) error->clear();
    if (offset < 0 || offset > size || length < 0 || length > JsonDataSource::MaxReadBytes) {
        if (error) *error = message("Invalid or oversized read request.");
        return false;
    }
    return true;
}

QByteArray MemoryJsonSource::read(qint64 offset, qint64 length, QString *error)
{
    if (!validRange(size(), offset, length, error)) return {};
    return m_bytes.mid(int(offset), int(qMin(length, size() - offset)));
}

bool FileJsonSource::open(const QString &path, QString *error)
{
    if (error) error->clear();
    m_file.close();
    m_cache.clear();
    m_size = m_bytesRead = 0;
    m_file.setFileName(path);
    if (!QFileInfo(path).isFile() || !m_file.open(QIODevice::ReadOnly)) {
        if (error) *error = message("Cannot open file: %1").arg(path) + "\n" + m_file.errorString();
        return false;
    }
    m_size = m_file.size();
    m_modified = QFileInfo(m_file).lastModified();
    return true;
}

qint64 FileJsonSource::residentBytes() const
{
    qint64 size = 0;
    for (const auto &block : m_cache) size += block.bytes.size();
    return size;
}

QByteArray FileJsonSource::read(qint64 offset, qint64 length, QString *error)
{
    if (!validRange(m_size, offset, length, error)) return {};
    if (!m_file.isOpen()) {
        if (error) *error = message("No file is open.");
        return {};
    }
    // Never serve cached pages after the underlying document changes.
    const QFileInfo current(m_file.fileName());
    if (!current.exists() || current.size() != m_size || current.lastModified() != m_modified) {
        m_cache.clear();
        if (error) *error = message("The file changed on disk. Reopen it to continue.");
        return {};
    }
    length = qMin(length, m_size - offset);
    QByteArray result;
    result.reserve(int(length));
    while (length > 0) {
        const qint64 blockStart = offset / BlockBytes * BlockBytes;
        int found = -1;
        for (int i = 0; i < m_cache.size(); ++i)
            if (m_cache[i].offset == blockStart) { found = i; break; }
        if (found >= 0) {
            auto block = m_cache.takeAt(found);
            m_cache.append(std::move(block)); // least-recently-used first
        } else {
            if (!m_file.seek(blockStart)) {
                if (error) *error = m_file.errorString();
                return {};
            }
            const qint64 expected = qMin(BlockBytes, m_size - blockStart);
            QByteArray bytes = m_file.read(expected);
            m_bytesRead += bytes.size();
            if (bytes.size() != expected) {
                if (error) *error = message("Unable to read the requested page. The file may have changed.");
                return {};
            }
            if (m_cache.size() == CacheBlocks) m_cache.removeFirst();
            m_cache.append(Block{blockStart, std::move(bytes)});
        }
        const auto &bytes = m_cache.last().bytes;
        const int within = int(offset - blockStart);
        const int count = int(qMin(length, qint64(bytes.size() - within)));
        result.append(bytes.constData() + within, count);
        offset += count;
        length -= count;
    }
    return result;
}

JsonTextPage readJsonTextPage(JsonDataSource &source, qint64 page)
{
    JsonTextPage result;
    result.total = source.size();
    result.pages = qMax(qint64(1), result.total / JsonDataSource::BlockBytes +
                       (result.total % JsonDataSource::BlockBytes != 0));
    result.page = qBound(qint64(0), page, result.pages - 1);
    const qint64 nominalStart = result.page * JsonDataSource::BlockBytes;
    const qint64 nominalEnd = qMin(result.total, nominalStart + JsonDataSource::BlockBytes);
    const qint64 readStart = qMax(qint64(0), nominalStart - 3);
    const qint64 readEnd = qMin(result.total, nominalEnd + 3);
    const QByteArray bytes = source.read(readStart, readEnd - readStart, &result.error);
    if (!result.error.isEmpty()) return result;
    auto boundary = [&](qint64 pos) {
        int i = int(pos - readStart);
        int shifts = 0;
        while (i > 0 && i < bytes.size() && shifts < 3 &&
               (static_cast<unsigned char>(bytes[i]) & 0xC0) == 0x80) {
            --i; ++shifts;
        }
        return i;
    };
    int start = boundary(nominalStart);
    int end = nominalEnd == result.total ? bytes.size() : boundary(nominalEnd);
    result.end = readStart + end;
    if (result.page == 0 && bytes.startsWith("\xEF\xBB\xBF")) start += 3;
    result.start = readStart + start;
    result.text = QString::fromUtf8(bytes.constData() + start, end - start);
    result.cacheBytes = source.residentBytes();
    return result;
}

QString readSmallJsonText(JsonDataSource &source, QString *error)
{
    if (error) error->clear();
    if (source.size() > JsonDataSource::LargeFileThreshold) {
        if (error) *error = message("This file requires large-file mode.");
        return {};
    }
    QByteArray bytes;
    bytes.reserve(int(source.size()));
    for (qint64 offset = 0; offset < source.size(); offset += JsonDataSource::BlockBytes) {
        QString failure;
        auto block = source.read(offset, qMin(JsonDataSource::BlockBytes, source.size() - offset), &failure);
        if (!failure.isEmpty()) {
            if (error) *error = failure;
            return {};
        }
        bytes += block;
    }
    // Preserve the existing small-file BOM auto-detection (UTF-8/16/32).
    QTextStream stream(&bytes, QIODevice::ReadOnly);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    stream.setCodec("UTF-8");
#endif
    return stream.readAll();
}
