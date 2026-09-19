#ifndef JSONINDEX_H
#define JSONINDEX_H

#include <QByteArray>
#include <QString>
#include <QVector>
#include <stdint.h>

/// Flat source index; no string copies or heap allocation per node.
/// The entire JSON document is represented as a flat vector of these.
struct IndexNode {
    enum Type : uint8_t { Object, Array, String, Number, Bool, Null };

    Type     type = Null;
    uint32_t keyOffset   = 0;   // byte offset of key in source (0 = root / array element)
    uint32_t keyLen      = 0;
    uint32_t valueOffset = 0;   // byte offset of value text (or opening {/[)
    uint32_t valueLen    = 0;   // length of value text (includes quotes for strings)
    uint32_t parentIdx   = 0;   // parent node in m_nodes (0 = root / sentinel)
    uint32_t childCount  = 0;   // number of direct children
    uint32_t firstChild  = 0;   // index of first child node
    uint32_t row = 0;          // row within parent

    bool isContainer() const { return type == Object || type == Array; }
};

/// Streaming JSON scanner — builds a flat IndexNode tree in O(n) time
/// without constructing a DOM.
class JsonIndex
{
public:
    /// Parse UTF-8 JSON text into index nodes.
    /// Returns false on parse error; error offset and message are set.
    bool build(const QByteArray &utf8json);

    const QVector<IndexNode> &nodes() const { return m_nodes; }
    int rootIndex() const { return 1; }   // sentinel is always m_nodes[0]

    int  errorOffset() const { return m_errorOffset; }
    const QString &errorMessage() const { return m_errorMsg; }

private:
    // ── Scanner helpers ──────────────────────────────────────────────────
    void skipWhitespace();
    bool scanString(uint32_t &outStart, uint32_t &outLen);
    bool scanNumber(uint32_t &outStart, uint32_t &outLen);
    bool scanKeyword(const char *word, int len);

    uint32_t addNode(IndexNode::Type type, uint32_t parentIdx,
                     uint32_t keyOff, uint32_t keyLen,
                     uint32_t valOff, uint32_t valLen);

    // ── Data ─────────────────────────────────────────────────────────────
    const char *m_src    = nullptr;
    int         m_len    = 0;
    int         m_pos    = 0;

    // Sentinel at index 0 so that parentIdx=0 means "no parent"
    QVector<IndexNode> m_nodes{1};

    int         m_errorOffset = -1;
    QString     m_errorMsg;
};

#endif // JSONINDEX_H
