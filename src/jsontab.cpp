#include "jsontab.h"
#include "editorlimits.h"
#include "uistrings.h"
#include "boundededitor.h"
#include <QSaveFile>
#include "jsonhighlighter.h"
#include "jsontreemodel.h"
#include "largefileview.h"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QSignalBlocker>
#include <QEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTreeView>
#include <QTableView>
#include <QStackedWidget>
#include <QFileInfo>
#include <QVBoxLayout>

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
    m_splitter->setObjectName("editorSplitter");

    // ── Left: text editor ───────────────────────────────────────────────
    auto *editor = new BoundedEditor(this);
    editor->rejected = [this] { emit protectionMessage(trMain("Editing limit reached. Save the text to a file and use large-file mode.")); };
    m_inputEdit = editor;
    connect(editor->document(), &QTextDocument::modificationChanged, this, [this] { emit contentChanged(); });
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
    m_showTreeBtn->setObjectName("showJsonTreeButton");
    m_showTreeBtn->setFixedSize(22, 44);
    m_showTreeBtn->setFlat(true);
    m_showTreeBtn->setToolTip(trMain("Show JSON tree"));
    m_showTreeBtn->setVisible(false);
    m_showTreeBtn->setStyleSheet(
        "QPushButton { background: rgba(200,200,200,100); border: 1px solid #aaa; "
        "border-top-right-radius: 0; border-bottom-right-radius: 0; }");
    connect(m_showTreeBtn, &QPushButton::clicked, this,
            [this] { setTreeVisible(true); });

    m_inputEdit->installEventFilter(this);
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
    m_inputEdit->document()->setModified(false);
    emit contentChanged();
    return true;
}

void JsonTab::setText(const QString &text)
{
    if (isLargeFile()) return;
    if (text.size() > BoundedEditor::CharacterLimit) {
        emit protectionMessage(trMain("Editing limit reached. Save the text to a file and use large-file mode."));
        return;
    }
    m_inputEdit->setPlainText(text);
    m_inputEdit->document()->setModified(true);
}

void JsonTab::clear()
{
    if (isLargeFile()) return;
    const bool changed = !text().isEmpty() || isModified();
    if (!text().isEmpty()) replaceText(QString());
    m_inputEdit->document()->setModified(changed);
    m_model->clear();
    m_hasValidDocument = false;
    clearFind();
    setTreeVisible(false);
}


void JsonTab::setTreeVisible(bool visible)
{
    if (isLargeFile()) return;
    const bool wasVisible = !m_treeContainer->isHidden();
    if (wasVisible && !visible && m_treeContainer->isVisible()) m_savedTreeState = m_splitter->saveState();
    m_treeContainer->setVisible(visible);
    m_showTreeBtn->setVisible(!visible);

    // Position the ◀ overlay at the right edge of the text editor
    if (!visible && m_inputEdit) {
        positionTreeButton();
        m_showTreeBtn->raise();
    }

    if (visible && !wasVisible) {
        const int extent = m_splitter->orientation() == Qt::Horizontal
            ? m_splitter->width() : m_splitter->height();
        if (m_savedTreeState.isEmpty() || !m_splitter->restoreState(m_savedTreeState))
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

void JsonTab::positionTreeButton()
{
    m_showTreeBtn->move(qMax(0, m_inputEdit->width() - m_showTreeBtn->width()),
                        qMax(0, (m_inputEdit->height() - m_showTreeBtn->height()) / 2));
}

bool JsonTab::eventFilter(QObject *watched, QEvent *event)
{
    // Splitter relayout can resize the editor without resizing the tab itself.
    if (watched == m_inputEdit && event->type() == QEvent::Resize)
        positionTreeButton();
    return QWidget::eventFilter(watched, event);
}

void JsonTab::formatJson(bool compressed)
{
    if (isLargeFile()) return;
    clearFind();
    m_operationError.clear();
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
    const QByteArray formatted = JsonTreeModel::format(input, !compressed, EditorLimits::FormattedBytes);
    if (formatted.isEmpty() && !input.isEmpty()) {
        m_model->clear();
        m_operationError = trMain("Formatted output exceeds the editing limit. Use large-file mode to format to a file.");
        emit protectionMessage(m_operationError);
        return;
    }
    const QString output = QString::fromUtf8(formatted);
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
    if (!m_operationError.isEmpty()) return m_operationError;
    return trMain("JSON Parse Error at offset %1: %2")
        .arg(m_model->errorOffset()).arg(QApplication::translate("JsonErrors", m_model->errorMessage().toUtf8().constData()));
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
    m_savedTreeState.clear();
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

bool JsonTab::isModified() const
{
    return !isLargeFile() && m_inputEdit->document()->isModified();
}

bool JsonTab::saveFile(const QString &path, QString *error)
{
    if (error) error->clear();
    if (isLargeFile()) { if (error) *error = trMain("Large-file mode is read-only."); return false; }
    // 不退回直接写入：提交前的失败必须保留已有目标文件。
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    const QByteArray bytes = text().toUtf8();
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        if (error) *error = output.errorString();
        return false;
    }
    // 只有 commit 成功才能更新文件关联，并将当前撤销位置标为已保存。
    m_filePath = QFileInfo(path).absoluteFilePath();
    m_inputEdit->document()->setModified(false);
    emit contentChanged();
    return true;
}

QByteArray JsonTab::viewState() const
{
    // 隐藏树后 splitter 的即时尺寸不再代表用户最后设置的分栏比例。
    if (!isTreeVisible() && !m_savedTreeState.isEmpty()) return m_savedTreeState;
    return m_splitter->saveState();
}
void JsonTab::restoreViewState(const QByteArray &state)
{
    if (!state.isEmpty() && m_splitter->restoreState(state)) m_savedTreeState = state;
}
