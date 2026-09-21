#include "largefileview.h"
#include <QThread>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QRegularExpressionValidator>
#include <QFontDatabase>
#include <QCoreApplication>
#include <QTreeView>
#include <QSplitter>
#include <QHeaderView>
#include <QFileInfo>
#include <QComboBox>
#include <QToolButton>
#include <QAction>

static QString caption(const char *text)
{
    return QCoreApplication::translate("LargeFile", text);
}

PageReader::PageReader(std::shared_ptr<std::atomic<quint64>> latest) : m_latest(std::move(latest)) {}

void PageReader::load(const QString &path, qint64 page, quint64 request, bool reopen)
{
    if (request != m_latest->load()) return;
    JsonTextPage result;
    result.request = request;
    if (reopen || path != m_path) {
        m_path.clear();
        m_source = std::make_unique<FileJsonSource>();
        if (!m_source->open(path, &result.error)) { emit ready(result); return; }
        const auto prefix = m_source->read(0, 4, &result.error);
        if (prefix.startsWith("\xFF\xFE") || prefix.startsWith("\xFE\xFF") ||
            prefix.startsWith(QByteArray::fromHex("0000feff"))) {
            result.error = caption("Large-file mode supports UTF-8 files. Convert UTF-16/32 files to UTF-8 first.");
        }
        if (!result.error.isEmpty()) { emit ready(result); return; }
        m_path = path;
    }
    if (request != m_latest->load()) return;
    result = readJsonTextPage(*m_source, page);
    result.request = request;
    result.bytesRead = m_source->bytesRead();
    if (request == m_latest->load()) emit ready(result);
}

LargeFileView::LargeFileView(QWidget *parent)
    : QWidget(parent), m_thread(new QThread),
      m_latest(std::make_shared<std::atomic<quint64>>(0))
{
    qRegisterMetaType<JsonTextPage>();
    setObjectName("largeFileView");
    auto *layout = new QVBoxLayout(this);
    m_notice = new QLabel(this);
    m_notice->setWordWrap(true);
    layout->addWidget(m_notice);
    auto *bar = new QHBoxLayout;
    m_first = new QPushButton(this);
    m_previous = new QPushButton(this);
    m_next = new QPushButton(this);
    m_last = new QPushButton(this);
    m_jump = new QPushButton(this);
    m_jump->setObjectName("pageJump");
    m_next->setObjectName("nextPage");
    m_previous->setObjectName("previousPage");
    m_offset = new QLineEdit(this);
    m_offset->setObjectName("byteOffset");
    m_offset->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{1,19}"), m_offset));
    m_offset->setMaximumWidth(180);
    m_jumpLabel = new QLabel(this);
    for (auto *button : {m_first, m_previous, m_next, m_last}) bar->addWidget(button);
    bar->addStretch();
    bar->addWidget(m_jumpLabel);
    bar->addWidget(m_offset);
    bar->addWidget(m_jump);
    layout->addLayout(bar);
    m_text = new QPlainTextEdit(this);
    m_text->setObjectName("pagedText");
    m_text->setReadOnly(true);
    m_text->setUndoRedoEnabled(false);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_text->setWordWrapMode(QTextOption::WrapAnywhere);
    setFocusProxy(m_text);
    auto *splitter = new QSplitter(this);
    splitter->addWidget(m_text);
    auto *treePanel = new QWidget(splitter);
    auto *treeLayout = new QVBoxLayout(treePanel);
    treeLayout->setContentsMargins(0, 0, 0, 0);
    auto *treeBar = new QHBoxLayout;
    m_moreChildren = new QPushButton(treePanel);
    m_previousChildren = new QPushButton(treePanel);
    m_nextChildren = new QPushButton(treePanel);
    m_cancelScan = new QPushButton(treePanel);
    for (auto *button : {m_moreChildren, m_previousChildren, m_nextChildren, m_cancelScan})
        treeBar->addWidget(button);
    treeLayout->addLayout(treeBar);
    m_treeModel = new LargeTreeModel(this);
    m_tree = new QTreeView(treePanel);
    m_tree->setObjectName("largeFileTree");
    m_tree->setModel(m_treeModel);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setColumnWidth(0, 180);
    m_tree->setColumnWidth(1, 80);
    treeLayout->addWidget(m_tree);
    m_treeStatus = new QLabel(treePanel);
    m_treeStatus->setTextFormat(Qt::PlainText);
    m_treeStatus->setWordWrap(true);
    treeLayout->addWidget(m_treeStatus);
    splitter->addWidget(treePanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);
    connect(m_tree, &QTreeView::expanded, m_treeModel, &LargeTreeModel::requestMore);
    connect(m_tree, &QTreeView::collapsed, m_treeModel, &LargeTreeModel::releaseChildren);
    connect(m_treeModel, &LargeTreeModel::statusChanged, m_treeStatus, &QLabel::setText);
    connect(m_treeModel, &LargeTreeModel::stateChanged, this, &LargeFileView::updateTreeControls);
    connect(m_treeModel, &LargeTreeModel::batchLoaded, this, [this] {
        if (!m_tree->currentIndex().isValid()) m_tree->setCurrentIndex(m_treeModel->index(0, 0));
    });
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &LargeFileView::updateTreeControls);
    connect(m_moreChildren, &QPushButton::clicked, this, [this] {
        const auto container = activeContainer();
        m_tree->expand(container);
        m_treeModel->requestMore(container);
    });
    connect(m_cancelScan, &QPushButton::clicked, m_treeModel, &LargeTreeModel::cancel);
    auto changeChildren = [this](bool next) {
        const auto container = activeContainer();
        m_tree->setCurrentIndex(container);
        m_treeModel->changePage(container, next);
    };
    connect(m_previousChildren, &QPushButton::clicked, this, [changeChildren] { changeChildren(false); });
    connect(m_nextChildren, &QPushButton::clicked, this, [changeChildren] { changeChildren(true); });
    setupOperations(layout);
    m_status = new QLabel(this);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    connect(m_first, &QPushButton::clicked, this, [this] { load(0); });
    connect(m_previous, &QPushButton::clicked, this, [this] { load(m_page.page - 1); });
    connect(m_next, &QPushButton::clicked, this, [this] { load(m_page.page + 1); });
    connect(m_last, &QPushButton::clicked, this, [this] { load(m_page.pages - 1); });
    auto jump = [this] {
        bool ok = false;
        qint64 offset = m_offset->text().toLongLong(&ok);
        if (ok && offset >= 0 && offset < qMax(qint64(1), m_page.total)) goToByte(offset);
        else m_status->setText(caption("Enter a byte offset between 0 and %1.").arg(qMax(qint64(0), m_page.total - 1)));
    };
    connect(m_jump, &QPushButton::clicked, this, jump);
    connect(m_offset, &QLineEdit::returnPressed, this, jump);
    auto *reader = new PageReader(m_latest);
    reader->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, reader, &QObject::deleteLater);
    connect(this, &LargeFileView::requestPage, reader, &PageReader::load);
    connect(reader, &PageReader::ready, this, [this](const JsonTextPage &page) {
        if (page.request != m_latest->load()) return;
        m_loading = false;
        m_page = page;
        m_text->setPlainText(page.error.isEmpty() ? page.text : QString());
        if (page.error.isEmpty() && m_requestedOffset >= page.start) {
            QByteArray utf8 = page.text.toUtf8();
            int at = int(qMin(m_requestedOffset - page.start, qint64(utf8.size())));
            while (at > 0 && at < utf8.size() &&
                   (static_cast<unsigned char>(utf8[at]) & 0xC0) == 0x80) --at;
            QString prefix = QString::fromUtf8(utf8.constData(), at);
            // QTextDocument normalizes CRLF to one paragraph separator.
            prefix.replace("\r\n", "\n").replace('\r', '\n');
            QTextCursor cursor(m_text->document());
            cursor.setPosition(prefix.size());
            m_text->setTextCursor(cursor);
            m_text->ensureCursorVisible();
        }
        m_offset->setText(QString::number(m_requestedOffset >= 0 ? m_requestedOffset : page.start));
        updateControls();
        updateTaskControls();
        emit pageLoaded();
    });
    m_thread->start();
    retranslate();
}

LargeFileView::~LargeFileView()
{
    ++*m_latest;
    // No UI wait on slow storage. The worker owns its QFile and can finish its
    // current bounded read safely after this widget has gone away.
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->quit();
}

void LargeFileView::openFile(const QString &path)
{
    m_tasks->cancel();
    m_searchHasMore = false; m_searchNext = 0; m_searchQuery.clear();
    m_matches->clear(); m_taskStatus->clear();
    m_path = path;
    const QFileInfo info(path);
    m_fileSize = info.size(); m_fileModified = info.lastModified();
    m_page = {};
    m_text->clear();
    load(0, true);
    m_treeModel->openFile(path);
}

void LargeFileView::goToByte(qint64 offset)
{
    if (offset < 0 || offset >= qMax(qint64(1), m_page.total)) return;
    load(offset / JsonDataSource::BlockBytes);
    m_requestedOffset = offset;
}

void LargeFileView::load(qint64 page, bool reopen)
{
    m_loading = true;
    m_requestedOffset = -1;
    const quint64 request = ++*m_latest;
    updateControls();
    updateTaskControls();
    emit requestPage(m_path, page, request, reopen);
}

void LargeFileView::retranslate()
{
    m_notice->setText(caption("Large file · Read-only UTF-8. Search raw text; export or format to a separate file. Tree previews do not validate the whole document."));
    m_query->setPlaceholderText(caption("Search raw text (case-sensitive)"));
    m_search->setText(caption("Search File"));
    m_searchMore->setText(caption("Continue Search"));
    m_cancelTask->setText(caption("Cancel Task"));
    m_locate->setText(caption("Locate Cursor in Tree"));
    m_fileOperations->setText(caption("File Operations"));
    m_formatFile->setText(caption("Format to File…"));
    m_compactFile->setText(caption("Compress to File…"));
    m_moreChildren->setText(caption("Load More"));
    m_previousChildren->setText(caption("Previous Children"));
    m_nextChildren->setText(caption("Next Children"));
    m_cancelScan->setText(caption("Cancel Scan"));
    m_treeModel->retranslate();
    updateTreeControls();
    m_first->setText(caption("First"));
    m_previous->setText(caption("Previous"));
    m_next->setText(caption("Next"));
    m_last->setText(caption("Last"));
    m_jumpLabel->setText(caption("Byte offset (0-based):"));
    m_jump->setText(caption("Go"));
    updateControls();
}

QModelIndex LargeFileView::activeContainer() const
{
    auto index = m_tree->currentIndex().siblingAtColumn(0);
    if (!index.isValid()) return m_treeModel->index(0, 0);
    const auto *record = m_treeModel->record(index);
    return record && record->container() ? index : index.parent();
}

void LargeFileView::updateTreeControls()
{
    const auto index = activeContainer();
    const bool idle = !m_treeModel->busy();
    m_moreChildren->setEnabled(idle && (!m_treeModel->rowCount() ||
        m_treeModel->hasMore(index)));
    m_previousChildren->setEnabled(idle && m_treeModel->firstRow(index) > 0);
    m_nextChildren->setEnabled(m_treeModel->canNextPage(index));
    m_cancelScan->setEnabled(!idle);
}

void LargeFileView::updateControls()
{
    const bool ready = !m_loading && m_page.error.isEmpty();
    m_first->setEnabled(ready && m_page.page > 0);
    m_previous->setEnabled(ready && m_page.page > 0);
    m_next->setEnabled(ready && m_page.page + 1 < m_page.pages);
    m_last->setEnabled(ready && m_page.page + 1 < m_page.pages);
    m_jump->setEnabled(ready);
    m_offset->setEnabled(ready);
    if (m_loading) m_status->setText(caption("Loading page…"));
    else if (!m_page.error.isEmpty()) m_status->setText(m_page.error);
    else m_status->setText(caption("Page %1 / %2 · Bytes [%3, %4) / %5 · Cache %6 KiB")
        .arg(m_page.page + 1).arg(m_page.pages).arg(m_page.start).arg(m_page.end)
        .arg(m_page.total).arg(m_page.cacheBytes / 1024));
}
