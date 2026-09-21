#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "jsontab.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QCloseEvent>

// 窗口负责选择路径与询问用户；JsonTab 负责原子写入和修改标记。
// 所有关闭路径都通过 confirmDiscard，取消或保存失败必须保留文档。

void MainWindow::onOpenFile()
{
    openFileWithMode(false);
}

void MainWindow::openFileWithMode(bool largeFile)
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open JSON File"),
        m_lastDirectory,
        tr("JSON Files (*.json);;All Files (*)"));

    if (filePath.isEmpty())
        return;

    m_lastDirectory = QFileInfo(filePath).absolutePath();
    // Never reuse a paged tab: its text() deliberately does not return a page.
    JsonTab *tab = currentTab();
    if (!tab || tab->isLargeFile() || tab->isModified() || !tab->text().isEmpty()) {
        tab = createTab(QFileInfo(filePath).fileName());
    } else if (tab) {
        int idx = ui->tabWidget->currentIndex();
        tab->setProperty("defaultTitle", false);
        ui->tabWidget->setTabText(idx, QFileInfo(filePath).fileName());
    }

    QString error;
    if (!tab->openFile(filePath, largeFile ? JsonTab::OpenMode::LargeFile : JsonTab::OpenMode::Automatic, &error)) {
        QMessageBox::warning(this, tr("Error"), error);
        return;
    }
    updateTabStates();
    ui->statusbar->showMessage(
        tr("Loaded: %1").arg(filePath), 3000);
}

void MainWindow::onSaveFile()
{
    if (auto *tab = currentTab()) saveTab(tab);
}

bool MainWindow::saveTab(JsonTab *tab, bool saveAs)
{
    if (!tab || tab->isLargeFile()) return false;
    QString path = tab->filePath();
    if (saveAs || path.isEmpty())
        path = QFileDialog::getSaveFileName(this, tr("Save JSON File"),
            path.isEmpty() ? m_lastDirectory : path, tr("JSON Files (*.json);;All Files (*)"));
    if (path.isEmpty()) return false;
    QString error;
    if (!tab->saveFile(path, &error)) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot write file:\n%1").arg(error));
        return false;
    }
    m_lastDirectory = QFileInfo(path).absolutePath();
    tab->setProperty("defaultTitle", false);
    ui->tabWidget->setTabText(ui->tabWidget->indexOf(tab), QFileInfo(path).fileName());
    updateTabStates();
    ui->statusbar->showMessage(tr("Saved: %1").arg(path), 3000);
    return true;
}

bool MainWindow::confirmDiscard(JsonTab *tab)
{
    if (!tab || !tab->isModified()) return true;
    ui->tabWidget->setCurrentWidget(tab);
    const auto answer = QMessageBox::warning(this, tr("Unsaved changes"),
        tr("Save changes to %1 before closing?").arg(ui->tabWidget->tabText(ui->tabWidget->indexOf(tab))),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save) return saveTab(tab);
    return answer == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    for (int i = 0; i < ui->tabWidget->count(); ++i) {
        auto *tab = qobject_cast<JsonTab *>(ui->tabWidget->widget(i));
        if (!confirmDiscard(tab)) {
            event->ignore();
            return;
        }
    }
    persistSettings();
    event->accept();
}
