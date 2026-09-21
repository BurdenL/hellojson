#include "largetreeindex.h"
#include "jsontreemodel.h"
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QCoreApplication>

static QString indexText(const char *text)
{
    return QCoreApplication::translate("LargeFile", text);
}

LargeTreeScanner::LargeTreeScanner(JsonDataSource &source, std::function<bool()> cancelled,
                                   std::function<void(qint64)> progress)
    : m_source(source), m_cancelled(std::move(cancelled)), m_progress(std::move(progress)) {}

bool LargeTreeScanner::fail(const char *message)
{
    if (m_error.isEmpty()) m_error = indexText("Byte %1: %2").arg(m_pos).arg(indexText(message));
    return false;
}

char LargeTreeScanner::peek()
{
    if (!m_error.isEmpty() || m_stopped) return '\0';
    if (m_pos >= m_source.size()) return '\0';
    if (m_bufferStart < 0 || m_pos < m_bufferStart || m_pos >= m_bufferStart + m_buffer.size()) {
        if (m_cancelled()) { m_stopped = true; return '\0'; }
        m_bufferStart = m_pos;
        m_buffer = m_source.read(m_pos, qMin(JsonDataSource::BlockBytes, m_source.size() - m_pos), &m_error);
        if (m_progress) m_progress(m_pos);
        if (m_buffer.isEmpty()) return '\0';
    }
    return m_buffer[int(m_pos - m_bufferStart)];
}
void LargeTreeScanner::advance()
{
    if (m_output && !m_stopped && m_error.isEmpty()) writeByte(peek());
    ++m_pos;
    if ((m_pos & 4095) == 0 && m_cancelled()) m_stopped = true;
}
void LargeTreeScanner::whitespace()
{
    for (;;) {
        const char c = peek();
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        advance();
    }
}
bool LargeTreeScanner::string()
{
    if (peek() != '"') return fail("Expected string");
    advance();
    for (;;) {
        unsigned char c = static_cast<unsigned char>(peek());
        if (c == '"') { advance(); return true; }
        if (c < 0x20) return fail("Unterminated string or control character");
        if (c == '\\') {
            advance();
            char e = peek(); advance();
            if (e == 'u') {
                for (int i = 0; i < 4; ++i) {
                    char h = peek();
                    if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F')))
                        return fail("Invalid Unicode escape");
                    advance();
                }
            } else if (e != '"' && e != '\\' && e != '/' && e != 'b' && e != 'f' &&
                       e != 'n' && e != 'r' && e != 't') return fail("Invalid escape");
        } else if (c >= 0x80) {
            int length = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 :
                         c >= 0xf0 && c <= 0xf4 ? 4 : 0;
            if (!length) return fail("Invalid UTF-8");
            advance();
            for (int i = 1; i < length; ++i) {
                auto next = static_cast<unsigned char>(peek());
                if (next < 0x80 || next > 0xbf ||
                    (i == 1 && ((c == 0xe0 && next < 0xa0) || (c == 0xed && next > 0x9f) ||
                                (c == 0xf0 && next < 0x90) || (c == 0xf4 && next > 0x8f))))
                    return fail("Invalid UTF-8");
                advance();
            }
        } else advance();
    }
}
bool LargeTreeScanner::number()
{
    if (peek() == '-') advance();
    char c = peek();
    if (c == '0') advance();
    else {
        if (c < '1' || c > '9') return fail("Invalid number");
        do { advance(); c = peek(); } while (c >= '0' && c <= '9');
    }
    if (peek() == '.') {
        advance(); c = peek();
        if (c < '0' || c > '9') return fail("Expected fraction digits");
        do { advance(); c = peek(); } while (c >= '0' && c <= '9');
    }
    if (peek() == 'e' || peek() == 'E') {
        advance();
        if (peek() == '+' || peek() == '-') advance();
        c = peek();
        if (c < '0' || c > '9') return fail("Expected exponent digits");
        do { advance(); c = peek(); } while (c >= '0' && c <= '9');
    }
    return !m_stopped && m_error.isEmpty();
}
bool LargeTreeScanner::value(int depth, LargeTreeRecord::Type *type)
{
    if (depth > 512) return fail("Maximum nesting depth (512) exceeded");
    whitespace();
    const char c = peek();
    if (c == '{' || c == '[') {
        if (type) *type = c == '{' ? LargeTreeRecord::Object : LargeTreeRecord::Array;
        const char closing = c == '{' ? '}' : ']';
        advance(); whitespace();
        if (peek() == closing) { advance(); return true; }
        for (;;) {
            if (c == '{') {
                if (!string()) return false;
                whitespace();
                if (peek() != ':') return fail("Expected colon");
                advance();
            }
            if (!value(depth + 1)) return false;
            whitespace();
            char separator = peek();
            if (separator == closing) { advance(); return true; }
            if (separator != ',') return fail("Expected comma or closing bracket");
            advance(); whitespace();
        }
    }
    if (c == '"') {
        if (type) *type = LargeTreeRecord::String;
        return string();
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        if (type) *type = LargeTreeRecord::Number;
        return number();
    }
    const char *word = c == 't' ? "true" : c == 'f' ? "false" : c == 'n' ? "null" : nullptr;
    if (!word) return fail("Expected JSON value");
    if (type) *type = c == 'n' ? LargeTreeRecord::Null : LargeTreeRecord::Boolean;
    while (*word) {
        if (peek() != *word++) return fail("Invalid literal");
        advance();
    }
    return true;
}
QString LargeTreeScanner::preview(qint64 start, qint64 end, bool quoted)
{
    if (quoted) { ++start; --end; }
    const qint64 length = qMax(qint64(0), end - start);
    auto bytes = m_source.read(start, qMin(qint64(129), length), &m_error);
    int kept = int(qMin(qint64(128), length));
    if (bytes.size() > kept) {
        while (kept > 0 && (static_cast<unsigned char>(bytes[kept]) & 0xc0) == 0x80) --kept;
        if (quoted) {
            for (int i = 0; i < kept; ++i) {
                if (bytes[i] != '\\') continue;
                const int size = i + 1 < kept && bytes[i + 1] == 'u' ? 6 : 2;
                if (i + size > kept) { kept = i; break; }
                i += size - 1;
            }
        }
    }
    QString text = QString::fromUtf8(bytes.constData(), qMin(kept, int(bytes.size())));
    if (quoted) text = JsonTreeModel::unescape(text);
    if (!text.isEmpty() && text.back().isHighSurrogate()) text.chop(1);
    if (length > 128) text += QChar(0x2026);
    return text;
}
bool LargeTreeScanner::finishRoot()
{
    whitespace();
    return m_pos == m_source.size() || fail("Trailing data after root value");
}
LargeTreeBatch LargeTreeScanner::root(bool shallow)
{
    LargeTreeBatch result;
    const auto prefix = m_source.read(0, qMin(qint64(3), m_source.size()), &m_error);
    if (prefix == QByteArray::fromHex("efbbbf")) m_pos = 3;
    whitespace();
    LargeTreeRecord root;
    root.key = "$";
    root.row = -1;
    root.start = m_pos;
    root.keyStart = m_pos;
    root.end = m_source.size();
    const char c = peek();
    if (c == '{' || c == '[') {
        root.type = c == '{' ? LargeTreeRecord::Object : LargeTreeRecord::Array;
        root.preview = c == '{' ? "{…}" : "[…]";
        result.records.append(root);
    } else if (shallow && (c == '"' || c == '-' || (c >= '0' && c <= '9') ||
                          c == 't' || c == 'f' || c == 'n')) {
        // Opening a giant scalar must not trigger a full-file scan either.
        // This is only a type hint/raw preview, not a validation result.
        root.type = c == '"' ? LargeTreeRecord::String : c == 'n' ? LargeTreeRecord::Null :
                    c == 't' || c == 'f' ? LargeTreeRecord::Boolean : LargeTreeRecord::Number;
        root.preview = preview(root.start, root.end, false);
        result.records.append(root);
    } else if (value(0, &root.type)) {
        root.end = m_pos;
        if (finishRoot()) {
            root.preview = preview(root.start, root.end, root.type == LargeTreeRecord::String);
            result.records.append(root);
        }
    }
    result.error = m_error;
    result.cancelled = m_stopped || m_cancelled();
    return result;
}
LargeTreeBatch LargeTreeScanner::children(const LargeTreeRecord &parent, qint64 firstRow, qint64 resumeOffset)
{
    LargeTreeBatch result;
    if (!parent.container()) { result.done = true; return result; }
    const bool object = parent.type == LargeTreeRecord::Object;
    const char closing = object ? '}' : ']';
    qint64 row = resumeOffset >= 0 ? firstRow : 0;
    m_pos = resumeOffset >= 0 ? resumeOffset : parent.start + 1;
    bool afterComma = row > 0;
    for (;;) {
        whitespace();
        if (peek() == closing) {
            if (afterComma) { fail("Trailing comma"); break; }
            advance();
            if (parent.key == "$" && parent.row == -1 && !finishRoot()) break;
            result.done = true;
            result.nextOffset = m_pos;
            break;
        }
        LargeTreeRecord record;
        record.depth = parent.depth + 1;
        record.row = row;
        record.keyStart = m_pos;
        if (object) {
            const qint64 start = m_pos;
            if (!string()) break;
            if (row >= firstRow) record.key = preview(start, m_pos, true);
            whitespace();
            if (peek() != ':') { fail("Expected colon"); break; }
            advance(); whitespace();
        } else record.key = QString("[%1]").arg(row);
        record.start = m_pos;
        if (!value(record.depth, &record.type)) break;
        record.end = m_pos;
        if (row >= firstRow) {
            record.preview = record.container() ? (record.type == LargeTreeRecord::Object ? "{…}" : "[…]")
                : preview(record.start, record.end, record.type == LargeTreeRecord::String);
            result.records.append(record);
        }
        whitespace();
        if (peek() == closing) {
            afterComma = false;
            continue;
        }
        if (peek() != ',') { fail("Expected comma or closing bracket"); break; }
        advance();
        ++row;
        afterComma = true;
        if (result.records.size() == BatchSize) {
            result.nextOffset = m_pos;
            break;
        }
        if (!m_error.isEmpty() || m_stopped) break;
    }
    result.error = m_error;
    result.cancelled = m_stopped || m_cancelled();
    if (!result.error.isEmpty() || result.cancelled) result.records.clear();
    return result;
}

void LargeTreeScanner::flushOutput()
{
    if (!m_error.isEmpty() || m_stopped || m_outputBuffer.isEmpty()) return;
    if (m_output->write(m_outputBuffer) != m_outputBuffer.size())
        m_error = m_output->errorString();
    m_outputBuffer.clear();
}

void LargeTreeScanner::writeByte(char c)
{
    auto newline = [&] {
        if (m_pretty) { m_outputBuffer += '\n'; m_outputBuffer += QByteArray(qMax(0, m_indent) * 2, ' '); }
    };
    if (m_inString) {
        m_outputBuffer += c;
        if (m_escape) m_escape = false;
        else if (c == '\\') m_escape = true;
        else if (c == '"') { m_inString = false; m_previous = '"'; }
    } else if (c != ' ' && c != '\r' && c != '\n' && c != '\t') {
        if (c == '}' || c == ']') {
            --m_indent;
            if (m_previous != '{' && m_previous != '[') newline();
        } else if (m_previous == '{' || m_previous == '[') newline();
        m_outputBuffer += c;
        if (c == '{' || c == '[') ++m_indent;
        else if (c == ',') newline();
        else if (c == ':' && m_pretty) m_outputBuffer += ' ';
        else if (c == '"') m_inString = true;
        m_previous = c;
    }
    if (m_outputBuffer.size() >= JsonDataSource::BlockBytes) flushOutput();
}

LargeTreeBatch LargeTreeScanner::transform(QIODevice &output, bool pretty)
{
    m_output = &output; m_pretty = pretty;
    if (m_source.read(0, qMin(qint64(3), m_source.size()), &m_error) == QByteArray::fromHex("efbbbf")) m_pos = 3;
    const bool valid = value(0) && finishRoot();
    if (valid && !m_stopped && !m_cancelled()) {
        if (pretty) m_outputBuffer += '\n';
        flushOutput();
    }
    LargeTreeBatch result;
    result.error = m_error;
    result.cancelled = m_stopped || m_cancelled();
    result.done = valid && result.error.isEmpty() && !result.cancelled;
    m_output = nullptr;
    return result;
}

LargeTreeBatch LargeTreeScanner::locate(qint64 offset)
{
    auto result = root(true);
    if (!result.error.isEmpty() || result.cancelled || result.records.isEmpty()) return result;
    for (int depth = 0; depth <= 512; ++depth) {
        const auto parent = result.records.last();
        if (!parent.container()) return result;
        qint64 row = 0, resume = -1;
        bool found = false;
        for (;;) {
            auto batch = children(parent, row, resume);
            if (!batch.error.isEmpty() || batch.cancelled) return batch;
            for (const auto &record : batch.records) {
                if (offset >= record.keyStart && offset < record.end) {
                    result.records.append(record); found = true; break;
                }
                if (record.keyStart > offset) return result;
            }
            if (found) break;
            if (batch.done) return result;
            row += batch.records.size(); resume = batch.nextOffset;
        }
    }
    result.error = indexText("Maximum nesting depth (512) exceeded");
    result.records.clear();
    return result;
}

LargeTreeWorker::LargeTreeWorker(std::shared_ptr<std::atomic<quint64>> generation)
    : m_generation(std::move(generation)) {}

void LargeTreeWorker::scan(const LargeTreeRequest &request)
{
    auto cancelled = [&] { return request.generation != m_generation->load(); };
    if (cancelled()) return;
    LargeTreeBatch result;
    result.generation = request.generation;
    if (request.root || request.path != m_path) {
        m_pages.clear(); m_order.clear(); m_disk.reset(); m_path.clear();
        m_source = std::make_unique<FileJsonSource>();
        if (!m_source->open(request.path, &result.error)) { emit ready(result); return; }
        m_disk = std::make_unique<QTemporaryFile>(QDir::tempPath() + "/hellojson-tree-XXXXXX.idx");
        if (!m_disk->open()) { result.error = m_disk->errorString(); emit ready(result); return; }
        m_path = request.path;
    }
    m_source->read(0, 0, &result.error); // validate file identity before cache hits too
    if (!result.error.isEmpty()) { emit ready(result); return; }
    const QString key = QString("%1:%2").arg(request.parent.start).arg(request.firstRow);
    if (!request.root && m_pages.contains(key)) {
        const Cached entry = m_pages.value(key);
        m_disk->seek(entry.offset);
        QByteArray bytes = m_disk->read(entry.length);
        QDataStream in(bytes);
        int count = 0; in >> count >> result.nextOffset >> result.done;
        if (count < 0 || count > LargeTreeScanner::BatchSize)
            result.error = indexText("Invalid temporary tree index.");
        for (int i = 0; i < count && result.error.isEmpty(); ++i) {
            LargeTreeRecord record;
            int type;
            in >> type >> record.depth >> record.start >> record.end >> record.row >> record.keyStart >> record.key >> record.preview;
            if (type < LargeTreeRecord::Object || type > LargeTreeRecord::Null) {
                result.error = indexText("Invalid temporary tree index."); break;
            }
            record.type = LargeTreeRecord::Type(type);
            result.records.append(record);
        }
        if (in.status() != QDataStream::Ok) result.error = indexText("Unable to read temporary tree index.");
        result.cacheHit = true;
    } else {
        QElapsedTimer progressClock; progressClock.start();
        LargeTreeScanner scanner(*m_source, cancelled, [&](qint64 offset) {
            if (progressClock.elapsed() >= 100) {
                emit progress(request.generation, offset); progressClock.restart();
            }
        });
        result = request.root ? scanner.root(true) :
                 scanner.children(request.parent, request.firstRow, request.resumeOffset);
        result.generation = request.generation;
        if (result.cancelled || cancelled()) return;
        if (!request.root && result.error.isEmpty()) {
            QByteArray bytes;
            QDataStream out(&bytes, QIODevice::WriteOnly);
            out << int(result.records.size()) << result.nextOffset << result.done;
            for (const auto &record : result.records)
                out << int(record.type) << record.depth << record.start << record.end << record.row << record.keyStart << record.key << record.preview;
            if (m_disk->size() + bytes.size() > DiskBudget) {
                if (!m_disk->resize(0)) {
                    result.error = m_disk->errorString(); emit ready(result); return;
                }
                m_pages.clear(); m_order.clear();
            }
            const qint64 position = m_disk->size();
            m_disk->seek(position);
            if (m_disk->write(bytes) != bytes.size() || !m_disk->flush()) result.error = m_disk->errorString();
            else {
                if (m_pages.size() == CachePages) m_pages.remove(m_order.dequeue());
                m_pages.insert(key, Cached{position, int(bytes.size())});
                m_order.enqueue(key);
            }
        }
    }
    result.sourceCacheBytes = m_source->residentBytes();
    result.diskBytes = m_disk->size();
    result.cachePath = m_disk->fileName();
    if (!cancelled()) emit ready(result);
}
