#include "jsontab.h"
#include "jsonhighlighter.h"
#include "jsontreemodel.h"
#include "largefileview.h"

#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QSignalBlocker>
#include <QEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QSplitter>
#include <QTreeView>
#include <QTableView>
#include <QScrollBar>
#include <QStackedWidget>
#include <QFileInfo>
#include <QVBoxLayout>

// Helper: translate using the MainWindow context so existing .ts entries work
static inline QString trMain(const char *source, const char *comment = nullptr)
{
    return QApplication::translate("MainWindow", source, comment);
}

JsonTab::JsonTab(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    m_pages = new QStackedWidget(this);
    outer->addWidget(m_pages);
    auto *editorPage = new QWidget(m_pages);
    m_pages->addWidget(editorPage);
    auto *layout = new QVBoxLayout(editorPage);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);

    // ── Left: text editor ───────────────────────────────────────────────
    m_inputEdit = new QPlainTextEdit(this);
    setFocusProxy(m_inputEdit);
    m_inputEdit->setPlaceholderText(trMain("Paste your JSON here..."));
    m_inputEdit->setTabStopDistance(20);
    m_inputEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    new JsonHighlighter(m_inputEdit->document());

    // ── Right: tree view inside a container with a hide button ──────────
    m_treeContainer = new QWidget(this);
    auto *treeLayout = new QVBoxLayout(m_treeContainer);
    treeLayout->setContentsMargins(0, 0, 0, 0);
    treeLayout->setSpacing(4);

    // Header row: "JSON Tree" label + hide (×) button
    auto *headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);
    m_treeLabel = new QLabel(trMain("JSON Tree"), m_treeContainer);
    QFont f = m_treeLabel->font();
    f.setBold(true);
    m_treeLabel->setFont(f);
    m_hideTreeBtn = new QPushButton(trMain("×"), m_treeContainer);  // ×
    m_hideTreeBtn->setFixedSize(22, 22);
    m_hideTreeBtn->setFlat(true);
    m_hideTreeBtn->setToolTip(trMain("Hide tree view"));
    connect(m_hideTreeBtn, &QPushButton::clicked, this,
            [this] { setTreeVisible(false); });
    headerRow->addWidget(m_treeLabel);
    headerRow->addStretch();
    headerRow->addWidget(m_hideTreeBtn);
    treeLayout->addLayout(headerRow);

    // Separator line
    auto *sep = new QFrame(m_treeContainer);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    treeLayout->addWidget(sep);


    m_model = new JsonTreeModel(this);
    m_treeView = new QTreeView(m_treeContainer);
    m_treeView->setObjectName("jsonTree");
    m_treeView->setModel(m_model);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeView->setColumnWidth(0, 180);
    m_treeView->setColumnWidth(1, 80);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_treeView, &QTreeView::customContextMenuRequested,
            this, &JsonTab::onTreeContextMenu);
    auto *detailsSplit = new QSplitter(Qt::Vertical, m_treeContainer);
    detailsSplit->addWidget(m_treeView);
    m_detailsModel = new JsonDetailsModel(m_model, this);
    m_detailsView = new QTableView(m_treeContainer);
    m_detailsView->setObjectName("jsonDetails");
    m_detailsView->setModel(m_detailsModel);
    m_detailsView->horizontalHeader()->setStretchLastSection(true);
    m_detailsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_detailsView->setAlternatingRowColors(true);
    m_detailsView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_detailsView, &QTableView::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto cell = m_detailsView->indexAt(pos);
        if (cell.isValid()) showContextMenu(m_detailsModel->sourceIndex(cell.row()),
                                           m_detailsView->viewport()->mapToGlobal(pos));
    });
    connect(m_detailsView, &QTableView::doubleClicked, this, [this](const QModelIndex &cell) {
        auto idx = m_detailsModel->sourceIndex(cell.row());
        m_treeView->setCurrentIndex(idx);
        showNode(idx);
    });
    detailsSplit->addWidget(m_detailsView);
    detailsSplit->setStretchFactor(0, 3);
    detailsSplit->setStretchFactor(1, 1);
    treeLayout->addWidget(detailsSplit);
    m_pathLabel = new QLabel(m_treeContainer);
    m_pathLabel->setTextFormat(Qt::PlainText);
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathLabel->setWordWrap(true);
    treeLayout->addWidget(m_pathLabel);
    connect(m_treeView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex &idx) {
        m_detailsModel->select(idx);
        m_pathLabel->setText(idx.isValid() ? m_model->path(idx) : QString());
    });
    connect(m_treeView, &QTreeView::doubleClicked, this, &JsonTab::showNode);

    // ── Show-tree button (overlay on text editor, visible when tree hidden)
    m_showTreeBtn = new QPushButton(trMain("◀"), m_inputEdit);
    m_showTreeBtn->setFixedSize(22, 44);
    m_showTreeBtn->setFlat(true);
    m_showTreeBtn->setToolTip(trMain("Show JSON tree"));
    m_showTreeBtn->setVisible(false);
    m_showTreeBtn->setStyleSheet(
        "QPushButton { background: rgba(200,200,200,100); border: 1px solid #aaa; "
        "border-top-right-radius: 0; border-bottom-right-radius: 0; }");
    connect(m_showTreeBtn, &QPushButton::clicked, this,
            [this] { setTreeVisible(true); });

    m_splitter->addWidget(m_inputEdit);
    m_splitter->addWidget(m_treeContainer);

    layout->addWidget(m_splitter);

    // ── Start with tree hidden ──────────────────────────────────────────
    setTreeVisible(false);

    connect(m_inputEdit, &QPlainTextEdit::textChanged,
            this, [this] {
        m_hasValidDocument = false;
        m_model->clear();
        m_pathLabel->clear();
        clearFind();
        emit contentChanged();
        emit searchResultsChanged();
    });
}

QString JsonTab::text() const
{
    // Never let existing save/format paths mistake one page for the document.
    if (isLargeFile()) return {};
    return m_inputEdit->toPlainText();
}

bool JsonTab::openFile(const QString &path, OpenMode mode, QString *error)
{
    if (error) error->clear();
    QFileInfo info(path);
    if (!info.isFile() || !info.isReadable()) {
        if (error) *error = trMain("Cannot open file:\n%1").arg(path);
        return false;
    }
    if (mode == OpenMode::LargeFile || info.size() > JsonDataSource::LargeFileThreshold) {
        // A page is never passed into JsonTreeModel or the editable document.
        if (!m_largeFileView) {
            m_inputEdit->clear();
            m_model->clear();
            m_hasValidDocument = false;
            clearFind();
            m_largeFileView = new LargeFileView(m_pages);
            m_pages->addWidget(m_largeFileView);
        }
        m_filePath = info.absoluteFilePath();
        m_pages->setCurrentWidget(m_largeFileView);
        setFocusProxy(m_largeFileView);
        m_largeFileView->openFile(m_filePath);
        emit contentChanged();
        emit treeVisibilityChanged(false);
        return true;
    }
    FileJsonSource source;
    QString failure;
    if (!source.open(path, &failure)) {
        if (error) *error = failure;
        return false;
    }
    const QString content = readSmallJsonText(source, &failure);
    if (!failure.isEmpty()) {
        if (error) *error = failure;
        return false;
    }
    if (m_largeFileView) {
        m_pages->removeWidget(m_largeFileView);
        delete m_largeFileView;
        m_largeFileView = nullptr;
    }
    m_pages->setCurrentIndex(0);
    setFocusProxy(m_inputEdit);
    m_filePath = info.absoluteFilePath();
    setText(content);
    emit contentChanged();
    return true;
}

void JsonTab::setText(const QString &text)
{
    if (isLargeFile()) return;
    m_inputEdit->setPlainText(text);
}

void JsonTab::clear()
{
    if (isLargeFile()) return;
    m_inputEdit->clear();
    m_model->clear();
    m_hasValidDocument = false;
    clearFind();
    setTreeVisible(false);
}

// ── Tree visibility ──────────────────────────────────────────────────────

void JsonTab::setTreeVisible(bool visible)
{
    if (isLargeFile()) return;
    const bool wasVisible = !m_treeContainer->isHidden();
    m_treeContainer->setVisible(visible);
    m_showTreeBtn->setVisible(!visible);

    // Position the ◀ overlay at the right edge of the text editor
    if (!visible && m_inputEdit) {
        m_showTreeBtn->move(m_inputEdit->width() - m_showTreeBtn->width(),
                            m_inputEdit->height() / 2 - m_showTreeBtn->height() / 2);
        m_showTreeBtn->raise();
    }

    if (visible && !wasVisible) {
        const int extent = m_splitter->orientation() == Qt::Horizontal
            ? m_splitter->width() : m_splitter->height();
        m_splitter->setSizes({extent * 3 / 5, extent * 2 / 5});
    }
    // When hidden: splitter has only one visible widget → editor fills 100%

    emit treeVisibilityChanged(visible);
}

bool JsonTab::isTreeVisible() const
{
    if (isLargeFile()) return false;
    return !m_treeContainer->isHidden();
}

void JsonTab::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Keep the ◀ overlay button at the top-right edge of the text editor
    if (m_showTreeBtn && m_inputEdit) {
        m_showTreeBtn->move(m_inputEdit->width() - m_showTreeBtn->width(),
                            m_inputEdit->height() / 2 - m_showTreeBtn->height() / 2);
    }
}


void JsonTab::formatJson(bool compressed)
{
    if (isLargeFile()) return;
    clearFind();
    m_hasValidDocument = false;
    const QByteArray input = m_inputEdit->toPlainText().toUtf8();
    if (!m_model->setJson(input)) {
        m_pathLabel->clear();
        QTextCursor cursor(m_inputEdit->document());
        cursor.setPosition(QString::fromUtf8(input.left(qMax(0, m_model->errorOffset()))).size());
        m_inputEdit->setTextCursor(cursor);
        emit searchResultsChanged();
        return;
    }
    const QString output = QString::fromUtf8(JsonTreeModel::format(input, !compressed));
    replaceText(output);
    m_hasValidDocument = m_model->setJson(output.toUtf8());
    m_treeView->expand(m_model->index(0, 0));
    m_treeView->setCurrentIndex(m_model->index(0, 0));
    setTreeVisible(true);
    if (!m_searchText.isEmpty()) findText(m_searchText);
    emit contentChanged();
    emit searchResultsChanged();
}

QString JsonTab::parseError() const
{
    return trMain("JSON Parse Error at offset %1: %2")
        .arg(m_model->errorOffset()).arg(m_model->errorMessage());
}

void JsonTab::replaceText(const QString &text)
{
    const QSignalBlocker blocker(m_inputEdit);
    QTextCursor cursor(m_inputEdit->document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(text);
    cursor.endEditBlock();
    cursor.setPosition(0);
    m_inputEdit->setTextCursor(cursor);
}

void JsonTab::refreshLanguage()
{
    if (m_largeFileView) m_largeFileView->retranslate();
    m_model->retranslate();
    m_treeLabel->setText(trMain("JSON Tree"));
    m_inputEdit->setPlaceholderText(trMain("Paste your JSON here..."));
    m_hideTreeBtn->setToolTip(trMain("Hide tree view"));
    m_showTreeBtn->setToolTip(trMain("Show JSON tree"));
}

void JsonTab::toggleLayout()
{
    if (isLargeFile()) return;
    m_splitter->setOrientation(m_splitter->orientation() == Qt::Horizontal ? Qt::Vertical : Qt::Horizontal);
}
void JsonTab::paste() { if (!isLargeFile()) m_inputEdit->paste(); }
void JsonTab::removeNewlines()
{
    if (isLargeFile()) return;
    QString content = text();
    content.remove('\n').remove('\r');
    replaceText(content);
    m_hasValidDocument = false;
    m_model->clear();
    clearFind();
    emit contentChanged();
    emit searchResultsChanged();
}
void JsonTab::removeBackslashes()
{
    if (isLargeFile()) return;
    QString content = text();
    content.remove('\\');
    replaceText(content);
    m_hasValidDocument = false;
    m_model->clear();
    clearFind();
    emit contentChanged();
    emit searchResultsChanged();
}

void JsonTab::setNodeSearch(bool enabled)
{
    clearFind();
    m_nodeSearch = enabled;
}
void JsonTab::selectNodeMatch(int position)
{
    if (position < 0 || position >= m_nodeMatches.size()) return;
    m_currentNodeMatch = position;
    auto idx = m_model->indexForId(m_nodeMatches[position]);
    setTreeVisible(true);
    for (auto parent = idx.parent(); parent.isValid(); parent = parent.parent())
        m_treeView->expand(parent);
    m_treeView->setCurrentIndex(idx);
    m_treeView->scrollTo(idx);
    showNode(idx);
}

void JsonTab::showNode(const QModelIndex &idx)
{
    const auto *n = m_model->node(idx);
    if (!n) return;
    QTextCursor cursor(m_inputEdit->document());
    const auto &source = m_model->source();
    const int start = QString::fromUtf8(source.constData(), int(n->valueOffset)).size();
    const int length = QString::fromUtf8(source.constData() + n->valueOffset, int(n->valueLen)).size();
    cursor.setPosition(start);
    cursor.setPosition(start + length, QTextCursor::KeepAnchor);
    m_inputEdit->setTextCursor(cursor);
    m_inputEdit->ensureCursorVisible();
}

// ── Tree helpers ─────────────────────────────────────────────────────────

void JsonTab::expandAll()
{
    m_treeView->expandAll();
}

void JsonTab::collapseAll()
{
    m_treeView->collapseAll();
}

// ── Search ───────────────────────────────────────────────────────────────

void JsonTab::findText(const QString &text)
{
    if (isLargeFile()) return;
    m_searchText = text;
    clearFind();
    if (text.isEmpty()) return;
    if (m_nodeSearch) {
        if (!m_hasValidDocument) {
            m_hasValidDocument = m_model->setJson(this->text().toUtf8());
            if (!m_hasValidDocument) return;
        }
        m_nodeMatches = m_model->findNodes(text);
        selectNodeMatch(0);
        return;
    }

    QTextDocument *doc = m_inputEdit->document();
    QTextCursor cursor(doc);

    QTextCharFormat highlightFmt;
    highlightFmt.setBackground(QColor(255, 255, 0));    // yellow
    highlightFmt.setForeground(Qt::black);

    QTextCharFormat activeFmt;
    activeFmt.setBackground(QColor(255, 165, 0));       // orange

    while (true) {
        cursor = doc->find(text, cursor);
        if (cursor.isNull()) break;
        m_matchPositions.append(cursor);
    }

    // Apply highlights
    QList<QTextEdit::ExtraSelection> extras;
    for (int i = 0; i < m_matchPositions.size(); ++i) {
        QTextEdit::ExtraSelection sel;
        sel.cursor = m_matchPositions[i];
        sel.format = (i == 0) ? activeFmt : highlightFmt;
        extras.append(sel);
    }
    m_inputEdit->setExtraSelections(extras);

    // Jump to first match
    if (!m_matchPositions.isEmpty()) {
        m_currentMatch = 0;
        m_inputEdit->setTextCursor(m_matchPositions[0]);
        m_inputEdit->ensureCursorVisible();
    }
}

bool JsonTab::findNext(const QString &text)
{
    if (isLargeFile()) return false;
    if (m_nodeSearch) {
        if (m_nodeMatches.isEmpty()) findText(text);
        else selectNodeMatch((m_currentNodeMatch + 1) % m_nodeMatches.size());
        return !m_nodeMatches.isEmpty();
    }
    if (m_matchPositions.isEmpty()) {
        findText(text);
        return !m_matchPositions.isEmpty();
    }

    int prev = m_currentMatch;
    m_currentMatch = (m_currentMatch + 1) % m_matchPositions.size();

    // Update highlight: prev → yellow, current → orange
    QList<QTextEdit::ExtraSelection> extras = m_inputEdit->extraSelections();
    if (prev >= 0 && prev < extras.size())
        extras[prev].format.setBackground(QColor(255, 255, 0));
    if (m_currentMatch >= 0 && m_currentMatch < extras.size())
        extras[m_currentMatch].format.setBackground(QColor(255, 165, 0));
    m_inputEdit->setExtraSelections(extras);

    m_inputEdit->setTextCursor(m_matchPositions[m_currentMatch]);
    m_inputEdit->ensureCursorVisible();
    return true;
}

bool JsonTab::findPrev(const QString &text)
{
    if (isLargeFile()) return false;
    if (m_nodeSearch) {
        if (m_nodeMatches.isEmpty()) findText(text);
        else selectNodeMatch((m_currentNodeMatch - 1 + m_nodeMatches.size()) % m_nodeMatches.size());
        return !m_nodeMatches.isEmpty();
    }
    if (m_matchPositions.isEmpty()) {
        findText(text);
        return !m_matchPositions.isEmpty();
    }

    int prev = m_currentMatch;
    m_currentMatch = (m_currentMatch - 1 + m_matchPositions.size()) % m_matchPositions.size();

    QList<QTextEdit::ExtraSelection> extras = m_inputEdit->extraSelections();
    if (prev >= 0 && prev < extras.size())
        extras[prev].format.setBackground(QColor(255, 255, 0));
    if (m_currentMatch >= 0 && m_currentMatch < extras.size())
        extras[m_currentMatch].format.setBackground(QColor(255, 165, 0));
    m_inputEdit->setExtraSelections(extras);

    m_inputEdit->setTextCursor(m_matchPositions[m_currentMatch]);
    m_inputEdit->ensureCursorVisible();
    return true;
}

void JsonTab::clearFind()
{
    m_nodeMatches.clear();
    m_currentNodeMatch = -1;
    m_matchPositions.clear();
    m_currentMatch = -1;
    m_inputEdit->setExtraSelections({});
}

// ── Tree context menu ────────────────────────────────────────────────────


void JsonTab::onTreeContextMenu(const QPoint &pos)
{
    showContextMenu(m_treeView->indexAt(pos), m_treeView->viewport()->mapToGlobal(pos));
}

void JsonTab::showContextMenu(const QModelIndex &idx, const QPoint &globalPos)
{
    if (!idx.isValid()) return;
    QMenu menu(this);
    const QStringList labels = {
        trMain("Copy Value"), trMain("Copy Key"), trMain("Copy Path"),
        trMain("Copy Key / Value"), trMain("Copy Node JSON"), trMain("Copy Similar Values"),
        trMain("Copy MAP Entry"), trMain("Copy Formatted Node JSON")
    };
    QList<QAction *> actions;
    for (const auto &label : labels) actions.append(menu.addAction(label));
    menu.addSeparator();
    auto *locate = menu.addAction(trMain("Locate in Text"));
    auto *expand = menu.addAction(trMain("Expand Subtree"));
    auto *collapse = menu.addAction(trMain("Collapse Subtree"));
    QAction *chosen = menu.exec(globalPos);
    if (!chosen) return;
    if (chosen == locate) { showNode(idx); return; }
    const auto root = idx.sibling(idx.row(), 0);
    if (chosen == expand) { m_treeView->expandRecursively(root); return; }
    if (chosen == collapse) {
        QList<QModelIndex> pending{root};
        while (!pending.isEmpty()) {
            auto current = pending.takeLast();
            if (!m_treeView->isExpanded(current)) continue;
            m_treeView->collapse(current);
            for (int r = 0; r < m_model->rowCount(current); ++r)
                pending.append(m_model->index(r, 0, current));
        }
        return;
    }
    QString result;
    switch (actions.indexOf(chosen)) {
    case 0:
        result = m_model->node(idx)->isContainer()
            ? QString::fromUtf8(m_model->json(idx)) : m_model->value(idx); break;
    case 1: result = m_model->key(idx); break;
    case 2: result = m_model->path(idx); break;
    case 3: result = JsonTreeModel::quote(m_model->key(idx)) + ": " + QString::fromUtf8(m_model->json(idx)); break;
    case 4: result = QString::fromUtf8(m_model->json(idx)); break;
    case 5: result = m_model->similarValues(idx); break;
    case 6: result = JsonTreeModel::quote(m_model->key(idx)) + "," + JsonTreeModel::quote(m_model->value(idx)); break;
    case 7: result = QString::fromUtf8(m_model->json(idx, true)); break;
    default: return;
    }
    QApplication::clipboard()->setText(result);
}
