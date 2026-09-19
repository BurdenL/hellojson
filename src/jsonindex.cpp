#include "jsonindex.h"
#include <cstring>
#include <limits>

bool JsonIndex::build(const QByteArray &utf8json)
{
    m_nodes.clear();
    m_nodes.append(IndexNode{});
    m_errorOffset = -1;
    m_errorMsg.clear();
    m_pos = 0;
    if (utf8json.size() > std::numeric_limits<int>::max()) {
        m_errorMsg = QStringLiteral("Document exceeds supported size");
        m_errorOffset = 0;
        return false;
    }
    m_src = utf8json.constData();
    m_len = int(utf8json.size());
    struct Frame { uint32_t id; bool first = true; };
    QVector<Frame> stack;
    bool rootRead = false;
    auto fail = [this](const char *message) {
        m_errorOffset = m_pos;
        m_errorMsg = QString::fromLatin1(message);
        return false;
    };
    for (;;) {
        skipWhitespace();
        uint32_t parent = 0, keyOff = 0, keyLen = 0;
        if (stack.isEmpty()) {
            if (rootRead)
                return m_pos == m_len || fail("Trailing data after root value");
        } else {
            const uint32_t id = stack.last().id;
            const bool object = m_nodes[id].type == IndexNode::Object;
            const char close = object ? '}' : ']';
            if (m_pos >= m_len) return fail("Unterminated container");
            if (m_src[m_pos] == close) {
                ++m_pos;
                m_nodes[id].valueLen = uint32_t(m_pos) - m_nodes[id].valueOffset;
                stack.removeLast();
                continue;
            }
            if (!stack.last().first) {
                if (m_src[m_pos] != ',') return fail("Expected comma or closing bracket");
                ++m_pos;
                skipWhitespace();
                if (m_pos >= m_len || m_src[m_pos] == close)
                    return fail("Expected value after comma");
            }
            stack.last().first = false;
            parent = id;
            if (object) {
                if (m_pos >= m_len || m_src[m_pos] != '"')
                    return fail("Expected string key");
                if (!scanString(keyOff, keyLen)) return false;
                skipWhitespace();
                if (m_pos >= m_len || m_src[m_pos] != ':')
                    return fail("Expected colon after key");
                ++m_pos;
                skipWhitespace();
            }
        }
        if (m_pos >= m_len) return fail("Expected value");
        const uint32_t start = uint32_t(m_pos);
        uint32_t len = 0, unused = 0;
        const char c = m_src[m_pos];
        IndexNode::Type type;
        if (c == '{' || c == '[') {
            type = c == '{' ? IndexNode::Object : IndexNode::Array;
            ++m_pos;
        } else if (c == '"') {
            type = IndexNode::String;
            if (!scanString(unused, len)) return false;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            type = IndexNode::Number;
            if (!scanNumber(unused, len)) return false;
        } else if (c == 't' || c == 'f' || c == 'n') {
            type = c == 'n' ? IndexNode::Null : IndexNode::Bool;
            const char *word = c == 't' ? "true" : c == 'f' ? "false" : "null";
            len = c == 'f' ? 5 : 4;
            if (!scanKeyword(word, int(len))) return false;
        } else {
            return fail("Unexpected character");
        }
        uint32_t id = addNode(type, parent, keyOff, keyLen, start, len);
        rootRead = true;
        if (type == IndexNode::Object || type == IndexNode::Array) {
            if (stack.size() >= 512) return fail("Maximum nesting depth (512) exceeded");
            stack.append(Frame{id, true});
        }
    }
}
void JsonIndex::skipWhitespace()
{
    while (m_pos < m_len) {
        char c = m_src[m_pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            ++m_pos;
        else
            break;
    }
}

bool JsonIndex::scanString(uint32_t &outStart, uint32_t &outLen)
{
    if (m_src[m_pos] != '"') {
        m_errorOffset = m_pos;
        m_errorMsg = QStringLiteral("Expected '\"'");
        return false;
    }

    int start = m_pos;
    ++m_pos;

    while (m_pos < m_len) {
        char c = m_src[m_pos];
        if (c == '"') {
            ++m_pos;
            outStart = (uint32_t)start;
            outLen   = (uint32_t)(m_pos - start);
            return true;
        }
        if (c == '\\') {
            ++m_pos;
            if (m_pos >= m_len) break;
            const char escape = m_src[m_pos++];
            if (escape == 'u') {
                for (int i = 0; i < 4; ++i) {
                    if (m_pos >= m_len ||
                        !((m_src[m_pos] >= '0' && m_src[m_pos] <= '9') ||
                          (m_src[m_pos] >= 'a' && m_src[m_pos] <= 'f') ||
                          (m_src[m_pos] >= 'A' && m_src[m_pos] <= 'F'))) {
                        m_errorOffset = m_pos;
                        m_errorMsg = QStringLiteral("Invalid Unicode escape");
                        return false;
                    }
                    ++m_pos;
                }
            } else if (!std::strchr("\"\\/bfnrt", escape) || escape == '\0') {
                m_errorOffset = m_pos - 1;
                m_errorMsg = QStringLiteral("Invalid string escape");
                return false;
            }
        } else if (static_cast<unsigned char>(c) < 0x20) {
            m_errorOffset = m_pos;
            m_errorMsg = QStringLiteral("Control character in string");
            return false;
        } else if (static_cast<unsigned char>(c) >= 0x80) {
            const unsigned char first = static_cast<unsigned char>(c);
            int count = first >= 0xC2 && first <= 0xDF ? 2 :
                        first >= 0xE0 && first <= 0xEF ? 3 :
                        first >= 0xF0 && first <= 0xF4 ? 4 : 0;
            bool valid = count && m_pos + count <= m_len;
            for (int i = 1; valid && i < count; ++i) {
                unsigned char next = static_cast<unsigned char>(m_src[m_pos + i]);
                valid = next >= 0x80 && next <= 0xBF;
                if (i == 1)
                    valid = valid && !(first == 0xE0 && next < 0xA0) &&
                            !(first == 0xED && next > 0x9F) &&
                            !(first == 0xF0 && next < 0x90) &&
                            !(first == 0xF4 && next > 0x8F);
            }
            if (!valid) {
                m_errorOffset = m_pos;
                m_errorMsg = QStringLiteral("Invalid UTF-8 in string");
                return false;
            }
            m_pos += count;
        } else {
            ++m_pos;
        }
    }

    m_errorOffset = start;
    m_errorMsg = QStringLiteral("Unterminated string");
    return false;
}

bool JsonIndex::scanNumber(uint32_t &outStart, uint32_t &outLen)
{
    int start = m_pos;

    if (m_src[m_pos] == '-')
        ++m_pos;

    if (m_pos >= m_len) {
        m_errorOffset = m_pos;
        m_errorMsg = QStringLiteral("Unexpected end in number");
        return false;
    }

    if (m_src[m_pos] == '0') {
        ++m_pos;
    } else if (m_src[m_pos] >= '1' && m_src[m_pos] <= '9') {
        while (m_pos < m_len && m_src[m_pos] >= '0' && m_src[m_pos] <= '9')
            ++m_pos;
    } else {
        m_errorOffset = m_pos;
        m_errorMsg = QStringLiteral("Invalid number");
        return false;
    }

    if (m_pos < m_len && m_src[m_pos] == '.') {
        ++m_pos;
        if (m_pos >= m_len || m_src[m_pos] < '0' || m_src[m_pos] > '9') {
            m_errorOffset = m_pos;
            m_errorMsg = QStringLiteral("Expected digit after decimal point");
            return false;
        }
        while (m_pos < m_len && m_src[m_pos] >= '0' && m_src[m_pos] <= '9')
            ++m_pos;
    }

    if (m_pos < m_len && (m_src[m_pos] == 'e' || m_src[m_pos] == 'E')) {
        ++m_pos;
        if (m_pos < m_len && (m_src[m_pos] == '+' || m_src[m_pos] == '-'))
            ++m_pos;
        if (m_pos >= m_len || m_src[m_pos] < '0' || m_src[m_pos] > '9') {
            m_errorOffset = m_pos;
            m_errorMsg = QStringLiteral("Expected digit in exponent");
            return false;
        }
        while (m_pos < m_len && m_src[m_pos] >= '0' && m_src[m_pos] <= '9')
            ++m_pos;
    }

    outStart = (uint32_t)start;
    outLen   = (uint32_t)(m_pos - start);
    return true;
}

bool JsonIndex::scanKeyword(const char *word, int len)
{
    if (m_pos + len > m_len) {
        m_errorOffset = m_pos;
        m_errorMsg = QStringLiteral("Unexpected end of input");
        return false;
    }
    if (std::memcmp(m_src + m_pos, word, (size_t)len) != 0) {
        m_errorOffset = m_pos;
        m_errorMsg = QStringLiteral("Invalid keyword");
        return false;
    }
    m_pos += len;
    return true;
}

// ── Node management ──────────────────────────────────────────────────────────

uint32_t JsonIndex::addNode(IndexNode::Type type, uint32_t parentIdx,
                             uint32_t keyOff, uint32_t keyLen,
                             uint32_t valOff, uint32_t valLen)
{
    IndexNode node;
    node.type        = type;
    node.keyOffset   = keyOff;
    node.keyLen      = keyLen;
    node.valueOffset = valOff;
    node.valueLen    = valLen;
    node.parentIdx   = parentIdx;
    node.childCount  = 0;
    node.firstChild  = 0;

    uint32_t idx = (uint32_t)m_nodes.size();

    if (parentIdx > 0 && parentIdx < (uint32_t)m_nodes.size()) {
        IndexNode &parent = m_nodes[parentIdx];
        node.row = parent.childCount;
        if (parent.childCount == 0)
            parent.firstChild = idx;
        parent.childCount++;
    }

    m_nodes.append(node);
    return idx;
}
