#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "jsontab.h"
#include <QKeyEvent>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QTimer>

// 搜索框防抖与快捷键归窗口管理；匹配及高亮由当前 JsonTab 管理。
// 大文件模式使用自己的后台搜索入口，不进入普通编辑器搜索。

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (obj == m_findEdit && (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
            && ke->modifiers().testFlag(Qt::ShiftModifier)) {
            onFindPrev();
            return true;
        }
        if (ke->key() == Qt::Key_Escape && m_findBar->isVisible()) {
            onFindClose();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::onFindToggled()
{
    if (auto *tab = currentTab(); tab && tab->isLargeFile()) {
        if (auto *query = tab->findChild<QLineEdit *>("largeSearchQuery")) {
            query->setFocus(); query->selectAll();
        }
        return;
    }
    showFindBar(!m_findBar->isVisible());
}

void MainWindow::showFindBar(bool visible)
{
    if (visible && currentTab() && currentTab()->isLargeFile()) return;
    m_findBar->setVisible(visible);
    if (visible) {
        m_findEdit->setFocus();
        m_findEdit->selectAll();
        // Re-search with current text if any
        if (!m_findEdit->text().isEmpty())
            performSearch();
    } else {
        // Clear highlights in current tab
        for (int i = 0; i < ui->tabWidget->count(); ++i)
            if (auto *tab = qobject_cast<JsonTab *>(ui->tabWidget->widget(i)))
                tab->findText(QString());
        m_findCountLabel->clear();
    }
}

void MainWindow::onFindClose()
{
    showFindBar(false);
    // Return focus to current tab's editor
    JsonTab *tab = currentTab();
    if (tab) tab->setFocus();
}

void MainWindow::onFindTextChanged(const QString &text)
{
    m_lastSearchText = text;
    if (text.isEmpty()) { m_searchTimer->stop(); performSearch(); }
    else m_searchTimer->start();
}

void MainWindow::performSearch()
{
    JsonTab *tab = currentTab();
    if (!tab) return;

    const QString &text = m_findEdit->text();
    tab->setNodeSearch(m_findMode->currentIndex() == 1);
    if (text.isEmpty()) {
        tab->findText(QString());
        m_findCountLabel->clear();
        return;
    }

    tab->findText(text);
    updateSearchCount();
}

void MainWindow::updateSearchCount()
{
    JsonTab *tab = currentTab();
    if (!tab || !m_findCountLabel) return;
    if (!m_findBar->isVisible() || m_findEdit->text().isEmpty()) {
        m_findCountLabel->clear();
        return;
    }
    if (tab->isNodeSearch() && !tab->hasDocument()) {
        m_findCountLabel->setText(tr("Format valid JSON first"));
        return;
    }
    int total = tab->matchCount();
    if (total > 0) {
        int cur = tab->currentMatchIndex() + 1;
        m_findCountLabel->setText(tab->searchLimited() ? tr("First %1 matches (limit reached)").arg(total) : tr("%1 / %2").arg(cur).arg(total));
    } else {
        m_findCountLabel->setText(tr("No matches"));
    }
}

void MainWindow::onFindNext()
{
    if (m_searchTimer->isActive()) { m_searchTimer->stop(); performSearch(); return; }
    JsonTab *tab = currentTab();
    if (!tab) return;

    const QString &text = m_findEdit->text();
    if (text.isEmpty()) return;

    tab->findNext(text);

    int total = tab->matchCount();
    if (total > 0) {
        int cur = tab->currentMatchIndex() + 1;
        m_findCountLabel->setText(tab->searchLimited() ? tr("First %1 matches (limit reached)").arg(total) : tr("%1 / %2").arg(cur).arg(total));
    }
}

void MainWindow::onFindPrev()
{
    if (m_searchTimer->isActive()) { m_searchTimer->stop(); performSearch(); return; }
    JsonTab *tab = currentTab();
    if (!tab) return;

    const QString &text = m_findEdit->text();
    if (text.isEmpty()) return;

    tab->findPrev(text);

    int total = tab->matchCount();
    if (total > 0) {
        int cur = tab->currentMatchIndex() + 1;
        m_findCountLabel->setText(tab->searchLimited() ? tr("First %1 matches (limit reached)").arg(total) : tr("%1 / %2").arg(cur).arg(total));
    }
}
