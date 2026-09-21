#include "largefileview.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QMenu>
#include <QTreeView>
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QApplication>
#include <QClipboard>
#include <QSignalBlocker>

static QString actionText(const char *text)
{
    return QCoreApplication::translate("LargeFile", text);
}

void LargeFileView::setupOperations(QVBoxLayout *layout)
{
    m_tasks = new LargeFileTasks(this);
    m_query = new QLineEdit(this);
    m_query->setObjectName("largeSearchQuery");
    m_query->setMaxLength(4096);
    m_search = new QPushButton(this); m_search->setObjectName("largeSearch");
    m_searchMore = new QPushButton(this); m_searchMore->setObjectName("largeSearchMore");
    m_cancelTask = new QPushButton(this); m_cancelTask->setObjectName("largeCancelTask");
    m_locate = new QPushButton(this); m_locate->setObjectName("largeLocate");
    m_matches = new QComboBox(this); m_matches->setObjectName("largeMatches");
    m_matches->setMinimumContentsLength(12);
    m_matches->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_fileOperations = new QToolButton(this);
    auto *fileMenu = new QMenu(m_fileOperations);
    m_formatFile = fileMenu->addAction(QString());
    m_compactFile = fileMenu->addAction(QString());
    m_fileOperations->setMenu(fileMenu);
    m_fileOperations->setPopupMode(QToolButton::InstantPopup);
    auto *bar = new QHBoxLayout;
    bar->addWidget(m_query, 1);
    bar->addWidget(m_search); bar->addWidget(m_searchMore); bar->addWidget(m_matches);
    bar->addWidget(m_locate); bar->addWidget(m_fileOperations); bar->addWidget(m_cancelTask);
    layout->insertLayout(2, bar);
    m_taskStatus = new QLabel(this); m_taskStatus->setObjectName("largeTaskStatus");
    m_taskStatus->setTextFormat(Qt::PlainText); m_taskStatus->setWordWrap(true);
    layout->insertWidget(3, m_taskStatus);
    connect(m_search, &QPushButton::clicked, this, [this] { searchFile(false); });
    connect(m_query, &QLineEdit::returnPressed, this, [this] { if (!m_tasks->busy()) searchFile(false); });
    connect(m_query, &QLineEdit::textChanged, this, &LargeFileView::updateTaskControls);
    connect(m_searchMore, &QPushButton::clicked, this, [this] { searchFile(true); });
    connect(m_cancelTask, &QPushButton::clicked, this, [this] {
        m_tasks->cancel();
        m_searchHasMore = false;
        m_taskStatus->setText(actionText("Task cancelled."));
        updateTaskControls();
    });
    connect(m_locate, &QPushButton::clicked, this, &LargeFileView::locateCursor);
    connect(m_formatFile, &QAction::triggered, this, [this] { saveTransformed(false); });
    connect(m_compactFile, &QAction::triggered, this, [this] { saveTransformed(true); });
    connect(m_matches, qOverload<int>(&QComboBox::activated), this, [this](int row) {
        goToByte(m_matches->itemData(row).toLongLong());
    });
    connect(m_tasks, &LargeFileTasks::busyChanged, this, &LargeFileView::updateTaskControls);
    connect(m_tasks, &LargeFileTasks::progress, this, [this](qint64 position, qint64 total) {
        m_taskStatus->setText(actionText("Working… byte %1 / %2").arg(position).arg(total));
    });
    connect(m_tasks, &LargeFileTasks::matches, this, [this](const QVector<qint64> &offsets) {
        for (const qint64 offset : offsets)
            if (m_matches->count() < LargeTaskWorker::SearchLimit)
                m_matches->addItem(actionText("Byte %1").arg(offset), offset);
        m_taskStatus->setText(actionText("Found %1 matches in this batch…").arg(m_matches->count()));
    });
    connect(m_tasks, &LargeFileTasks::completed, this, [this](const LargeTaskResult &result) {
        if (!result.error.isEmpty()) {
            m_taskStatus->setText(result.error);
            if (result.kind == LargeTaskKind::Search) m_matches->clear();
        } else if (result.kind == LargeTaskKind::Search) {
            m_searchNext = result.nextOffset; m_searchHasMore = result.more;
            m_taskStatus->setText(result.more
                ? actionText("Found %1 matches. Continue Search for the next batch.").arg(m_matches->count())
                : actionText("Search finished. %1 matches in this batch.").arg(m_matches->count()));
        } else if (result.kind == LargeTaskKind::Copy) {
            QApplication::clipboard()->setText(QString::fromUtf8(result.copied));
            m_taskStatus->setText(actionText("Node JSON copied."));
        } else if (result.kind == LargeTaskKind::Locate) {
            QSignalBlocker blocker(m_tree);
            const auto selected = m_treeModel->revealPath(result.path);
            for (auto parent = selected.parent(); parent.isValid(); parent = parent.parent()) m_tree->expand(parent);
            m_tree->setCurrentIndex(selected); m_tree->scrollTo(selected);
            m_taskStatus->setText(actionText("Cursor located in tree."));
        } else m_taskStatus->setText(actionText("Saved: %1").arg(result.output));
        updateTaskControls();
    });
    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        if (const auto *record = m_treeModel->record(index)) goToByte(record->start);
    });
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        const auto index = m_tree->indexAt(point);
        if (!index.isValid()) return;
        m_tree->setCurrentIndex(index);
        QMenu menu(this);
        auto *locate = menu.addAction(actionText("Locate Node in Text"));
        auto *copy = menu.addAction(actionText("Copy Node JSON (up to 1 MiB)"));
        auto *exportNode = menu.addAction(actionText("Export Node to File…"));
        const auto *record = m_treeModel->record(index);
        copy->setEnabled(!m_tasks->busy() && record && record->end - record->start <= LargeTaskWorker::CopyLimit);
        exportNode->setEnabled(!m_tasks->busy());
        // Store offsets, not a QModelIndex across the nested menu event loop.
        const qint64 start = record ? record->start : 0;
        connect(locate, &QAction::triggered, this, [this, start] { goToByte(start); });
        connect(copy, &QAction::triggered, this, [this] { nodeTask(LargeTaskKind::Copy); });
        connect(exportNode, &QAction::triggered, this, [this] { nodeTask(LargeTaskKind::Export); });
        menu.exec(m_tree->viewport()->mapToGlobal(point));
    });
}

void LargeFileView::updateTaskControls()
{
    const bool idle = !m_tasks->busy();
    m_search->setEnabled(idle && !m_path.isEmpty() && !m_query->text().isEmpty());
    m_searchMore->setEnabled(idle && m_searchHasMore && m_query->text() == m_searchQuery);
    m_cancelTask->setEnabled(!idle);
    m_locate->setEnabled(idle && !m_loading && m_page.error.isEmpty() && !m_path.isEmpty());
    m_fileOperations->setEnabled(idle && !m_path.isEmpty());
}

void LargeFileView::startTask(LargeTaskRequest request)
{
    request.path = m_path;
    request.expectedSize = m_fileSize; request.expectedModified = m_fileModified;
    m_taskStatus->setText(actionText("Working…"));
    m_tasks->start(std::move(request));
}

void LargeFileView::searchFile(bool more)
{
    if (m_tasks->busy()) return;
    LargeTaskRequest request; request.kind = LargeTaskKind::Search;
    request.query = more ? m_searchQuery : m_query->text();
    request.start = more ? m_searchNext : 0;
    m_searchQuery = request.query;
    m_searchHasMore = false;
    m_matches->clear();
    startTask(request);
}

qint64 LargeFileView::cursorByteOffset() const
{
    const int position = m_text->textCursor().position();
    int documentPosition = 0, originalPosition = 0;
    while (originalPosition < m_page.text.size() && documentPosition < position) {
        if (m_page.text[originalPosition] == '\r' && originalPosition + 1 < m_page.text.size() &&
            m_page.text[originalPosition + 1] == '\n') ++originalPosition;
        ++originalPosition; ++documentPosition;
    }
    if (originalPosition > 0 && m_page.text[originalPosition - 1].isHighSurrogate()) --originalPosition;
    return m_page.start + m_page.text.left(originalPosition).toUtf8().size();
}

void LargeFileView::locateCursor()
{
    if (m_loading || !m_page.error.isEmpty() || m_tasks->busy()) return;
    LargeTaskRequest request; request.kind = LargeTaskKind::Locate;
    request.start = cursorByteOffset();
    startTask(request);
}

void LargeFileView::nodeTask(LargeTaskKind kind)
{
    if (m_tasks->busy()) return;
    const auto *record = m_treeModel->record(m_tree->currentIndex());
    if (!record) return;
    LargeTaskRequest request; request.kind = kind;
    request.start = record->start; request.end = record->end;
    if (kind == LargeTaskKind::Export) {
        request.output = QFileDialog::getSaveFileName(this, actionText("Export Node to File…"),
            QFileInfo(m_path).absolutePath() + "/node.json");
        if (request.output.isEmpty()) return;
    }
    startTask(request);
}

void LargeFileView::saveTransformed(bool compact)
{
    if (m_tasks->busy()) return;
    LargeTaskRequest request;
    request.kind = compact ? LargeTaskKind::Compact : LargeTaskKind::Format;
    const QFileInfo info(m_path);
    request.output = QFileDialog::getSaveFileName(this, compact ? actionText("Compress to File…") : actionText("Format to File…"),
        info.absolutePath() + "/" + info.completeBaseName() + (compact ? ".compact.json" : ".formatted.json"));
    if (!request.output.isEmpty()) startTask(request);
}
