#include "jsontab.h"
#include "jsonhighlighter.h"

#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QJsonArray>
#include <QMenu>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

// Helper: translate using the MainWindow context so existing .ts entries work
static inline QString trMain(const char *source, const char *comment = nullptr)
{
    return QApplication::translate("MainWindow", source, comment);
}

JsonTab::JsonTab(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);

    m_inputEdit = new QPlainTextEdit(this);
    m_inputEdit->setPlaceholderText(trMain("Paste your JSON here..."));
    m_inputEdit->setTabStopDistance(20);
    m_inputEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    new JsonHighlighter(m_inputEdit->document());

    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setAlternatingRowColors(true);
    m_treeWidget->setRootIsDecorated(true);
    m_treeWidget->setHeaderLabels({
        trMain("Key"),
        trMain("Type"),
        trMain("Value")
    });
    m_treeWidget->header()->setStretchLastSection(true);
    m_treeWidget->setColumnWidth(0, 180);
    m_treeWidget->setColumnWidth(1, 80);
    m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_treeWidget, &QTreeWidget::customContextMenuRequested,
            this, &JsonTab::onTreeContextMenu);

    m_splitter->addWidget(m_inputEdit);
    m_splitter->addWidget(m_treeWidget);
    m_splitter->setSizes({500, 500});

    layout->addWidget(m_splitter);

    connect(m_inputEdit, &QPlainTextEdit::textChanged,
            this, &JsonTab::contentChanged);
}

QString JsonTab::text() const
{
    return m_inputEdit->toPlainText();
}

void JsonTab::setText(const QString &text)
{
    m_inputEdit->setPlainText(text);
}

void JsonTab::clear()
{
    m_inputEdit->clear();
    m_treeWidget->clear();
    m_hasValidDocument = false;
    m_lastDocument = QJsonDocument();
}

// ── Recursive tree population ────────────────────────────────────────────

void JsonTab::populateJsonTree(const QJsonValue &value,
                               QTreeWidgetItem *parent,
                               const QString &key)
{
    auto *item = new QTreeWidgetItem();

    if (value.isObject()) {
        QJsonObject obj = value.toObject();
        item->setText(0, key.isEmpty() ? trMain("(root)") : key);
        item->setText(1, trMain("Object"));
        item->setText(2, trMain("{%1 members}").arg(obj.size()));
        for (auto it = obj.begin(); it != obj.end(); ++it)
            populateJsonTree(it.value(), item, it.key());
    }
    else if (value.isArray()) {
        QJsonArray arr = value.toArray();
        item->setText(0, key.isEmpty() ? trMain("(root)") : key);
        item->setText(1, trMain("Array"));
        item->setText(2, trMain("[%1 elements]").arg(arr.size()));
        for (int i = 0; i < arr.size(); ++i)
            populateJsonTree(arr.at(i), item, trMain("[%1]").arg(i));
    }
    else if (value.isString()) {
        item->setText(0, key);
        item->setText(1, trMain("String"));
        item->setText(2, value.toString());
    }
    else if (value.isDouble()) {
        item->setText(0, key);
        item->setText(1, trMain("Number"));
        double d = value.toDouble();
        if (d == static_cast<qint64>(d))
            item->setText(2, QString::number(static_cast<qint64>(d)));
        else
            item->setText(2, QString::number(d, 'f'));
    }
    else if (value.isBool()) {
        item->setText(0, key);
        item->setText(1, trMain("Boolean"));
        item->setText(2, value.toBool() ? QStringLiteral("true")
                                        : QStringLiteral("false"));
    }
    else if (value.isNull()) {
        item->setText(0, key);
        item->setText(1, trMain("Null"));
        item->setText(2, QStringLiteral("null"));
    }
    else {
        item->setText(0, key);
        item->setText(1, trMain("Unknown"));
        item->setText(2, QStringLiteral("?"));
    }

    if (parent)
        parent->addChild(item);
    else
        m_treeWidget->addTopLevelItem(item);
}

void JsonTab::rebuildTree()
{
    m_treeWidget->clear();
    if (m_lastDocument.isObject())
        populateJsonTree(QJsonValue(m_lastDocument.object()), nullptr, QString());
    else if (m_lastDocument.isArray())
        populateJsonTree(QJsonValue(m_lastDocument.array()), nullptr, QString());

    if (m_treeWidget->topLevelItemCount() > 0)
        m_treeWidget->topLevelItem(0)->setExpanded(true);
}

// ── Format / Compress ────────────────────────────────────────────────────

void JsonTab::formatJson(bool compressed)
{
    const QString input = m_inputEdit->toPlainText().trimmed();

    if (input.isEmpty())
        return;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(input.toUtf8(), &parseError);

    if (parseError.error != QJsonParseError::NoError)
        return;

    m_lastDocument = doc;
    m_hasValidDocument = true;

    QJsonDocument::JsonFormat format = compressed
                                           ? QJsonDocument::Compact
                                           : QJsonDocument::Indented;
    // Block signals so setPlainText doesn't fire contentChanged
    m_inputEdit->blockSignals(true);
    m_inputEdit->setPlainText(QString::fromUtf8(doc.toJson(format)));
    m_inputEdit->blockSignals(false);

    rebuildTree();
}

// ── Tree helpers ─────────────────────────────────────────────────────────

void JsonTab::expandAll()
{
    m_treeWidget->expandAll();
}

void JsonTab::collapseAll()
{
    m_treeWidget->collapseAll();
}

// ── Search ───────────────────────────────────────────────────────────────

void JsonTab::findText(const QString &text)
{
    clearFind();
    if (text.isEmpty()) return;

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
    m_matchPositions.clear();
    m_currentMatch = -1;
    m_inputEdit->setExtraSelections({});
}

// ── Tree context menu ────────────────────────────────────────────────────

void JsonTab::onTreeContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_treeWidget->itemAt(pos);
    if (!item) return;

    QString value = item->text(2);  // Value column
    if (value.isEmpty()) return;

    // Build JSON path by walking up (stop before root node)
    QStringList segments;
    QTreeWidgetItem *cur = item;
    while (cur && cur->parent()) {           // skip root (no parent)
        segments.prepend(cur->text(0));      // Key column
        cur = cur->parent();
    }
    QString path = segments.isEmpty() ? "$" : ("$." + segments.join("."));

    QMenu menu(this);
    QAction *copyVal  = menu.addAction(trMain("Copy Value"));
    QAction *copyPath = menu.addAction(trMain("Copy Path"));
    QAction *chosen   = menu.exec(m_treeWidget->viewport()->mapToGlobal(pos));

    if (chosen == copyVal) {
        QApplication::clipboard()->setText(value);
    } else if (chosen == copyPath) {
        QApplication::clipboard()->setText(path);
    }
}

