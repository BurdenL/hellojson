#include "jsontreemodel.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>

static QString label(const char *text)
{
    return QCoreApplication::translate("MainWindow", text);
}

JsonTreeModel::JsonTreeModel(QObject *parent) : QAbstractItemModel(parent) {}

bool JsonTreeModel::setJson(const QByteArray &source)
{
    beginResetModel();
    m_source = source;
    m_valid = m_index.build(m_source);
    m_children.clear();
    m_starts.clear();
    if (m_valid) {
        const auto &nodes = m_index.nodes();
        m_starts.resize(nodes.size());
        uint32_t total = 0;
        for (int i = 1; i < nodes.size(); ++i) {
            m_starts[i] = total;
            total += nodes[i].childCount;
        }
        m_children.resize(total);
        for (int i = 2; i < nodes.size(); ++i)
            m_children[m_starts[nodes[i].parentIdx] + nodes[i].row] = uint32_t(i);
    }
    endResetModel();
    return m_valid;
}

void JsonTreeModel::clear()
{
    beginResetModel();
    m_valid = false;
    m_source.clear();
    m_index = JsonIndex();
    m_children.clear();
    m_starts.clear();
    endResetModel();
}

void JsonTreeModel::retranslate()
{
    emit headerDataChanged(Qt::Horizontal, 0, 2);
    // Layout notification preserves persistent indexes and tree expansion.
    emit layoutAboutToBeChanged();
    emit layoutChanged();
}

const IndexNode *JsonTreeModel::node(const QModelIndex &idx) const
{
    if (!m_valid || !idx.isValid() || idx.model() != this ||
        idx.internalId() == 0 || idx.internalId() >= quintptr(m_index.nodes().size()))
        return nullptr;
    return &m_index.nodes()[int(idx.internalId())];
}

QModelIndex JsonTreeModel::indexForId(uint32_t id, int column) const
{
    if (!m_valid || id == 0 || id >= uint32_t(m_index.nodes().size()) ||
        column < 0 || column >= 3) return {};
    return createIndex(int(m_index.nodes()[id].row), column, quintptr(id));
}

QModelIndex JsonTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (row < 0 || column < 0 || column >= 3 || row >= rowCount(parent)) return {};
    if (!parent.isValid()) return indexForId(1, column);
    return indexForId(m_children[m_starts[int(parent.internalId())] + row], column);
}

QModelIndex JsonTreeModel::parent(const QModelIndex &child) const
{
    const auto *n = node(child);
    return n ? indexForId(n->parentIdx) : QModelIndex();
}

int JsonTreeModel::rowCount(const QModelIndex &parent) const
{
    if (!m_valid) return 0;
    if (!parent.isValid()) return 1;
    const auto *n = node(parent);
    return n && parent.column() == 0 ? int(n->childCount) : 0;
}

QString JsonTreeModel::quote(const QString &text)
{
    QByteArray array = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(array.mid(1, array.size() - 2));
}

QString JsonTreeModel::unescape(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 == text.size()) { out += text[i]; continue; }
        QChar c = text[++i];
        if (c == 'u' && i + 4 < text.size()) {
            bool ok;
            ushort code = text.mid(i + 1, 4).toUShort(&ok, 16);
            if (ok) { out += QChar(code); i += 4; continue; }
        }
        if (c == 'n') out += '\n';
        else if (c == 'r') out += '\r';
        else if (c == 't') out += '\t';
        else if (c == 'b') out += QChar('\b');
        else if (c == 'f') out += QChar('\f');
        else if (c == '\\' || c == '"' || c == '/' || c == '\'') out += c;
        else { out += '\\'; out += c; }
    }
    return out;
}

QString JsonTreeModel::key(const QModelIndex &idx) const
{
    const auto *n = node(idx);
    if (!n) return {};
    if (!n->parentIdx) return label("(root)");
    if (!n->keyLen) return QStringLiteral("[%1]").arg(n->row);
    return unescape(QString::fromUtf8(m_source.constData() + n->keyOffset + 1,
                                    int(n->keyLen) - 2));
}

QString JsonTreeModel::value(const QModelIndex &idx) const
{
    const auto *n = node(idx);
    if (!n) return {};
    if (n->type == IndexNode::Object) return label("{%1 members}").arg(n->childCount);
    if (n->type == IndexNode::Array) return label("[%1 elements]").arg(n->childCount);
    QString raw = QString::fromUtf8(m_source.constData() + n->valueOffset, int(n->valueLen));
    return n->type == IndexNode::String ? unescape(raw.mid(1, raw.size() - 2)) : raw;
}

QVariant JsonTreeModel::data(const QModelIndex &idx, int role) const
{
    const auto *n = node(idx);
    if (!n || (role != Qt::DisplayRole && role != Qt::ToolTipRole)) return {};
    if (idx.column() == 0) return key(idx);
    if (idx.column() == 1) {
        static const char *names[] = {"Object", "Array", "String", "Number", "Boolean", "Null"};
        return label(names[n->type]);
    }
    QString result = value(idx);
    return role == Qt::DisplayRole && result.size() > 512 ? result.left(512) + QChar(0x2026) : result;
}

QVariant JsonTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 || section > 2)
        return {};
    static const char *names[] = {"Key", "Type", "Value"};
    return label(names[section]);
}

QString JsonTreeModel::path(const QModelIndex &idx) const
{
    QStringList segments;
    QModelIndex current = idx;
    static const QRegularExpression identifier(QStringLiteral("^[A-Za-z_$][A-Za-z0-9_$]*$"));
    while (const auto *n = node(current)) {
        if (!n->parentIdx) break;
        if (!n->keyLen) segments.prepend(QStringLiteral("[%1]").arg(n->row));
        else {
            QString k = key(current);
            segments.prepend(identifier.match(k).hasMatch() ? "." + k : "[" + quote(k) + "]");
        }
        current = parent(current);
    }
    return "$" + segments.join(QString());
}

QByteArray JsonTreeModel::format(const QByteArray &input, bool pretty)
{
    QByteArray out;
    out.reserve(input.size());
    int depth = 0;
    bool string = false, escape = false;
    char previous = 0;
    auto newline = [&] { out += '\n'; out += QByteArray(depth * 4, ' '); };
    for (int i = 0; i < input.size(); ++i) {
        char c = input[i];
        if (string) {
            out += c;
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') string = false;
            continue;
        }
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
        if (c == '"') { string = true; out += c; }
        else if (c == '{' || c == '[') {
            out += c; ++depth;
            int next = i + 1;
            while (next < input.size() && QByteArray(" \r\n\t").contains(input[next])) ++next;
            if (pretty && next < input.size() && input[next] != '}' && input[next] != ']') newline();
        } else if (c == '}' || c == ']') {
            --depth;
            if (pretty && previous != '{' && previous != '[') newline();
            out += c;
        } else if (c == ',') { out += c; if (pretty) newline(); }
        else if (c == ':') { out += c; if (pretty) out += ' '; }
        else out += c;
        previous = c;
    }
    return out;
}

QByteArray JsonTreeModel::json(const QModelIndex &idx, bool pretty) const
{
    const auto *n = node(idx);
    return n ? format(m_source.mid(n->valueOffset, n->valueLen), pretty) : QByteArray();
}

QString JsonTreeModel::similarValues(const QModelIndex &idx) const
{
    QModelIndex p = parent(idx), grandparent = parent(p);
    if (!grandparent.isValid()) return {};
    QStringList values;
    const QString wanted = key(idx);
    for (int r = 0; r < rowCount(grandparent); ++r) {
        QModelIndex sibling = index(r, 0, grandparent);
        for (int c = 0; c < rowCount(sibling); ++c) {
            QModelIndex child = index(c, 0, sibling);
            if (key(child) == wanted) values += value(child);
        }
    }
    return values.join('\n');
}

QVector<uint32_t> JsonTreeModel::findNodes(const QString &query) const
{
    QVector<uint32_t> result;
    if (!m_valid || query.isEmpty()) return result;
    for (int i = 1; i < m_index.nodes().size(); ++i) {
        QModelIndex idx = indexForId(uint32_t(i));
        if (key(idx).contains(query, Qt::CaseInsensitive) ||
            value(idx).contains(query, Qt::CaseInsensitive)) result.append(uint32_t(i));
    }
    return result;
}

QModelIndex JsonTreeModel::indexAtOffset(int offset) const
{
    if (!m_valid || offset < 0) return {};
    const auto &nodes = m_index.nodes();
    auto it = std::upper_bound(nodes.cbegin() + 1, nodes.cend(), uint32_t(offset),
        [](uint32_t pos, const IndexNode &n) {
            return pos < (n.keyLen ? n.keyOffset : n.valueOffset);
        });
    if (it == nodes.cbegin() + 1) return {};
    uint32_t id = uint32_t((it - nodes.cbegin()) - 1);
    while (id && uint32_t(offset) >= nodes[id].valueOffset + nodes[id].valueLen)
        id = nodes[id].parentIdx;
    return indexForId(id);
}

JsonDetailsModel::JsonDetailsModel(JsonTreeModel *source, QObject *parent)
    : QAbstractTableModel(parent), m_source(source)
{
    connect(source, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        beginResetModel(); m_selected = {};
    });
    connect(source, &QAbstractItemModel::modelReset, this, [this] { endResetModel(); });
    connect(source, &QAbstractItemModel::layoutChanged, this, [this] {
        emit headerDataChanged(Qt::Horizontal, 0, 1);
        if (rowCount()) emit dataChanged(index(0, 0), index(rowCount() - 1, 1));
    });
}
void JsonDetailsModel::select(const QModelIndex &idx)
{
    beginResetModel(); m_selected = idx.sibling(idx.row(), 0); endResetModel();
}
int JsonDetailsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_selected.isValid()) return 0;
    const auto *n = m_source->node(m_selected);
    return n ? (n->isContainer() ? int(n->childCount) : 1) : 0;
}
QModelIndex JsonDetailsModel::sourceIndex(int row) const
{
    if (row < 0 || row >= rowCount()) return {};
    return m_source->node(m_selected)->isContainer()
        ? m_source->index(row, 0, m_selected) : m_selected;
}
QVariant JsonDetailsModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.column() > 1) return {};
    auto source = sourceIndex(idx.row());
    return m_source->data(source.sibling(source.row(), idx.column() == 0 ? 0 : 2), role);
}
QVariant JsonDetailsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    return m_source->headerData(section == 0 ? 0 : 2, orientation, role);
}
