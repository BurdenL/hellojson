#include "largetreemodel.h"
#include <QThread>
#include <QCoreApplication>

static QString label(const char *text)
{
    return QCoreApplication::translate("LargeFile", text);
}
LargeTreeModel::LargeTreeModel(QObject *parent) : QAbstractItemModel(parent),
    m_thread(new QThread), m_generation(std::make_shared<std::atomic<quint64>>(0))
{
    qRegisterMetaType<LargeTreeRequest>();
    qRegisterMetaType<LargeTreeBatch>();
    auto *worker = new LargeTreeWorker(m_generation);
    worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(this, &LargeTreeModel::scanRequested, worker, &LargeTreeWorker::scan);
    connect(worker, &LargeTreeWorker::ready, this, &LargeTreeModel::accept);
    connect(worker, &LargeTreeWorker::progress, this, [this](quint64 generation, qint64 offset) {
        if (generation == m_generation->load())
            emit statusChanged(label("Scanning tree… byte %1").arg(offset));
    });
    m_thread->start();
}
LargeTreeModel::~LargeTreeModel()
{
    ++*m_generation;
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->quit();
}
void LargeTreeModel::openFile(const QString &path)
{
    cancel();
    beginResetModel();
    m_root.reset(); m_resident = 0; m_lastBatch = {}; m_path = path;
    endResetModel();
    m_paused = false;
    submit(nullptr, true);
}
LargeTreeModel::Node *LargeTreeModel::node(const QModelIndex &idx) const
{
    return idx.isValid() && idx.model() == this ? static_cast<Node *>(idx.internalPointer()) : nullptr;
}
QModelIndex LargeTreeModel::indexFor(Node *n, int column) const
{
    return n ? createIndex(n->row, column, n) : QModelIndex();
}
QModelIndex LargeTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (row < 0 || row >= rowCount(parent) || column < 0 || column > 2) return {};
    return indexFor(parent.isValid() ? node(parent)->children[size_t(row)].get() : m_root.get(), column);
}
QModelIndex LargeTreeModel::parent(const QModelIndex &idx) const
{
    auto *n = node(idx);
    return n ? indexFor(n->parent) : QModelIndex();
}
int LargeTreeModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid()) return m_root ? 1 : 0;
    auto *n = node(parent);
    return n && parent.column() == 0 ? int(n->children.size()) : 0;
}
const LargeTreeRecord *LargeTreeModel::record(const QModelIndex &idx) const
{
    auto *n = node(idx);
    return n ? &n->record : nullptr;
}
QVariant LargeTreeModel::data(const QModelIndex &idx, int role) const
{
    const auto *r = record(idx);
    if (!r || (role != Qt::DisplayRole && role != Qt::ToolTipRole)) return {};
    if (idx.column() == 0) return r->key;
    if (idx.column() == 2) return r->preview;
    static const char *names[] = {"Object", "Array", "String", "Number", "Boolean", "Null"};
    return QCoreApplication::translate("MainWindow", names[r->type]);
}
QVariant LargeTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 || section > 2) return {};
    static const char *names[] = {"Key", "Type", "Value"};
    return QCoreApplication::translate("MainWindow", names[section]);
}
bool LargeTreeModel::hasChildren(const QModelIndex &parent) const
{
    if (!parent.isValid()) return bool(m_root);
    auto *n = node(parent);
    return n && parent.column() == 0 && n->record.container() && (!n->done || !n->children.empty());
}
bool LargeTreeModel::canFetchMore(const QModelIndex &idx) const
{
    auto *n = node(idx);
    return n && idx.column() == 0 && n->record.container() && !m_busy && !m_paused &&
           !n->done && !n->manualFetch && n->children.size() < WindowSize &&
           m_resident + LargeTreeScanner::BatchSize <= ResidentLimit;
}
void LargeTreeModel::submit(Node *n, bool root)
{
    LargeTreeRequest request;
    request.path = m_path;
    request.root = root;
    request.generation = ++*m_generation;
    if (n) {
        request.parent = n->record;
        request.firstRow = n->firstRow + qint64(n->children.size());
        request.resumeOffset = n->nextOffset;
    }
    m_pending = n;
    m_busy = true;
    emit stateChanged();
    emit statusChanged(label("Scanning tree…"));
    emit scanRequested(request);
}
void LargeTreeModel::fetchMore(const QModelIndex &idx)
{
    if (canFetchMore(idx)) submit(node(idx), false);
}
void LargeTreeModel::requestMore(const QModelIndex &idx)
{
    if (m_busy) return;
    m_paused = false;
    if (!m_root) { submit(nullptr, true); return; }
    if (m_resident + LargeTreeScanner::BatchSize > ResidentLimit) {
        emit statusChanged(label("Tree memory limit reached. Collapse another branch to continue."));
        return;
    }
    if (auto *n = node(idx)) n->manualFetch = false;
    fetchMore(idx);
}
void LargeTreeModel::accept(const LargeTreeBatch &batch)
{
    if (batch.generation != m_generation->load()) return;
    m_lastBatch = batch;
    auto *target = m_pending;
    m_pending = nullptr;
    // Keep fetchMore disabled during begin/endInsertRows notifications.
    if (!batch.error.isEmpty()) {
        m_paused = true;
        emit statusChanged(batch.error);
    } else if (!target) {
        beginInsertRows({}, 0, 0);
        m_root = std::make_unique<Node>();
        if (!batch.records.isEmpty()) m_root->record = batch.records.first();
        m_resident = 1;
        endInsertRows();
        emit statusChanged(label("Expand a node to scan its children."));
    } else {
        const int start = int(target->children.size());
        if (!batch.records.isEmpty()) {
            beginInsertRows(indexFor(target), start, start + batch.records.size() - 1);
            for (const auto &record : batch.records) {
                auto child = std::make_unique<Node>();
                child->record = record; child->row = int(target->children.size()); child->parent = target;
                target->children.push_back(std::move(child));
                ++m_resident;
            }
            endInsertRows();
        }
        target->nextOffset = batch.nextOffset;
        target->done = batch.done;
        emit statusChanged(label("Children %1–%2 · Resident nodes %3 / %4")
            .arg(target->firstRow + (target->children.empty() ? 0 : 1))
            .arg(target->firstRow + qint64(target->children.size()))
            .arg(m_resident).arg(ResidentLimit));
    }
    m_busy = false;
    emit stateChanged();
    emit batchLoaded();
}
void LargeTreeModel::cancel()
{
    ++*m_generation;
    m_pending = nullptr; m_busy = false; m_paused = true;
    emit statusChanged(label("Tree scan cancelled. Use Load More to resume."));
    emit stateChanged();
}
int LargeTreeModel::count(Node *n) const
{
    int result = 1;
    for (const auto &child : n->children) result += count(child.get());
    return result;
}
void LargeTreeModel::releaseChildren(const QModelIndex &idx)
{
    auto *n = node(idx);
    if (!n) return;
    cancel();
    if (!n->children.empty()) {
        beginRemoveRows(indexFor(n), 0, int(n->children.size()) - 1);
        for (const auto &child : n->children) m_resident -= count(child.get());
        n->children.clear();
        endRemoveRows();
    }
    n->nextOffset = -1; n->done = false; n->manualFetch = false;
    emit stateChanged();
}
void LargeTreeModel::changePage(const QModelIndex &idx, bool next)
{
    auto *n = node(idx);
    if (!n || !n->record.container() || (next && !canNextPage(idx)) ||
        (!next && n->firstRow == 0)) return;
    const qint64 resume = next ? n->nextOffset : -1;
    const qint64 first = next ? n->firstRow + qint64(n->children.size()) : qMax(qint64(0), n->firstRow - WindowSize);
    releaseChildren(idx);
    n->firstRow = first; n->nextOffset = resume;
    requestMore(idx);
}
qint64 LargeTreeModel::firstRow(const QModelIndex &idx) const
{
    auto *n = node(idx); return n ? n->firstRow : 0;
}
bool LargeTreeModel::canNextPage(const QModelIndex &idx) const
{
    auto *n = node(idx);
    return n && !n->done && n->children.size() == WindowSize && !m_busy;
}
bool LargeTreeModel::hasMore(const QModelIndex &idx) const
{
    auto *n = node(idx);
    return n && n->record.container() && !n->done && n->children.size() < WindowSize;
}
void LargeTreeModel::retranslate()
{
    emit headerDataChanged(Qt::Horizontal, 0, 2);
    emit layoutAboutToBeChanged();
    emit layoutChanged();
}

QModelIndex LargeTreeModel::revealPath(const QVector<LargeTreeRecord> &path)
{
    if (path.isEmpty() || path.size() > 514) return {};
    cancel();
    beginResetModel();
    m_root = std::make_unique<Node>();
    m_root->record = path.first();
    m_root->manualFetch = true;
    Node *parent = m_root.get();
    for (int i = 1; i < path.size(); ++i) {
        auto child = std::make_unique<Node>();
        child->record = path[i]; child->parent = parent;
        child->manualFetch = true;
        parent->firstRow = path[i].row;
        parent->children.push_back(std::move(child));
        parent = parent->children.back().get();
    }
    m_resident = path.size();
    endResetModel();
    emit stateChanged();
    emit statusChanged(label("Located node near byte %1. Load More to browse adjacent children.").arg(parent->record.start));
    return indexFor(parent);
}
