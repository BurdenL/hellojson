#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "jsontab.h"
#include <QAction>
#include <QMenu>
#include <QToolBar>
#include <QInputDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>

// 菜单与工具栏复用 QAction；这里只组织入口，不直接修改文档。

void MainWindow::setupActions()
{
    auto *saveAs = new QAction(tr("Save As..."), this);
    saveAs->setObjectName("actionSaveAs");
    saveAs->setProperty("translationSource", "Save As...");
    saveAs->setProperty("requiresEditable", true);
    saveAs->setShortcut(QKeySequence("Ctrl+Shift+S"));
    ui->menuFile->insertAction(ui->actionExit, saveAs);
    connect(saveAs, &QAction::triggered, this, [this] { if (auto *tab = currentTab()) saveTab(tab, true); });
    auto *largeOpen = ui->menuFile->addAction(tr("Open in Large-file Mode..."));
    largeOpen->setProperty("translationSource", "Open in Large-file Mode...");
    largeOpen->setObjectName("openLargeFile");
    largeOpen->setShortcut(QKeySequence("Ctrl+Shift+O"));
    connect(largeOpen, &QAction::triggered, this, [this] { openFileWithMode(true); });
    auto *toolsMenu = ui->menubar->addMenu(tr("Tools"));
    toolsMenu->setProperty("translationSource", "Tools");
    toolsMenu->setObjectName("menuTools");
    auto *toolbar = addToolBar(tr("JSON Tools"));
    toolbar->setObjectName("jsonTools");
    auto add = [this, toolsMenu](const char *source, const QKeySequence &shortcut,
                                auto callback) {
        auto *action = toolsMenu->addAction(tr(source));
        action->setProperty("translationSource", source);
        action->setProperty("requiresEditable", QByteArray(source) == "Paste" ||
                            QByteArray(source) == "Node Search" ||
                            QByteArray(source) == "Switch Layout" ||
                            QByteArray(source) == "Remove Newlines" ||
                            QByteArray(source) == "Remove Backslashes");
        action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, callback);
        return action;
    };
    toolbar->addAction(add("Paste", QKeySequence(), [this] {
        if (auto *tab = currentTab()) tab->paste();
    }));
    toolbar->addAction(add("Node Search", QKeySequence("Ctrl+Shift+N"), [this] {
        m_findMode->setCurrentIndex(1);
        showFindBar(true);
    }));
    toolbar->addAction(add("Switch Layout", QKeySequence("Ctrl+Alt+L"), [this] {
        if (auto *tab = currentTab()) tab->toggleLayout();
    }));
    add("Remove Newlines", QKeySequence(), [this] {
        if (auto *tab = currentTab()) tab->removeNewlines();
    });
    add("Remove Backslashes", QKeySequence(), [this] {
        if (auto *tab = currentTab()) tab->removeBackslashes();
    });
    add("Rename Tab", QKeySequence("F2"), [this] {
        if (!currentTab()) return;
        int index = ui->tabWidget->currentIndex();
        bool ok;
        QString title = QInputDialog::getText(this, tr("Rename Tab"), tr("Name"),
                                            QLineEdit::Normal, ui->tabWidget->tabText(index), &ok);
        if (ok && !title.trimmed().isEmpty()) {
            currentTab()->setProperty("defaultTitle", false);
            ui->tabWidget->setTabText(index, title);
        }
    });
    add("Rename Window", QKeySequence(), [this] {
        bool ok;
        QString title = QInputDialog::getText(this, tr("Rename Window"), tr("Name"),
                                            QLineEdit::Normal, windowTitle(), &ok);
        if (ok && !title.trimmed().isEmpty()) setWindowTitle(title);
    });
    add("Unicode / Escape Conversion", QKeySequence("Ctrl+Alt+U"),
        [this] { showUnicodeConverter(); });
}

void MainWindow::setupMenuLayout()
{
    auto *tools = findChild<QMenu *>("menuTools");
    auto actionFor = [this](const char *source) -> QAction * {
        for (auto *action : findChildren<QAction *>())
            if (action->property("translationSource").toByteArray() == source) return action;
        return nullptr;
    };
    auto move = [](QMenu *from, QMenu *to, QAction *action) {
        from->removeAction(action);
        to->addAction(action);
    };
    auto makeMenu = [this](const char *name, const char *source) {
        auto *menu = new QMenu(tr(source), this);
        menu->setObjectName(name);
        menu->setProperty("translationSource", source);
        return menu;
    };
    auto *search = makeMenu("menuSearch", "&Search");
    auto *tabs = makeMenu("menuTabs", "&Tabs");
    // Remove presentation entries only; actions retain their owners and connections.
    for (auto *action : ui->menuView->actions()) ui->menuView->removeAction(action);
    ui->menuFile->insertAction(ui->actionOpen, ui->actionNewTab);
    ui->menuFile->insertAction(ui->actionSave, actionFor("Open in Large-file Mode..."));
    ui->menuFile->insertSeparator(ui->actionSave);
    ui->menuFile->insertAction(ui->actionExit, ui->actionCloseTab);
    ui->menuFile->insertSeparator(ui->actionExit);
    auto *paste = actionFor("Paste");
    tools->removeAction(paste);
    ui->menuEdit->insertAction(ui->actionFormat, paste);
    ui->menuEdit->insertSeparator(ui->actionFormat);
    ui->menuEdit->removeAction(ui->actionFind);
    move(ui->menuEdit, search, ui->actionFind);
    move(tools, search, actionFor("Node Search"));
    auto *cleanup = makeMenu("menuCleanup", "Text Cleanup");
    move(tools, cleanup, actionFor("Remove Newlines"));
    move(tools, cleanup, actionFor("Remove Backslashes"));
    ui->menuEdit->addMenu(cleanup);
    ui->menuEdit->removeAction(ui->actionClear);
    ui->menuEdit->addSeparator();
    ui->menuEdit->addAction(ui->actionClear);
    ui->menuView->addAction(findChild<QAction *>("toggleTreeAction"));
    move(tools, ui->menuView, actionFor("Switch Layout"));
    ui->menuView->addSeparator();
    ui->menuView->addAction(ui->actionExpandAll);
    ui->menuView->addAction(ui->actionCollapseAll);
    ui->menuView->addSeparator();
    ui->menuView->addMenu(ui->menuLanguage);
    move(tools, tabs, actionFor("Rename Tab"));
    actionFor("Rename Tab")->setProperty("requiresTab", true);
    tabs->addSeparator();
    tabs->addAction(ui->actionNextTab);
    tabs->addAction(ui->actionPrevTab);
    tabs->addSeparator();
    move(tools, tabs, actionFor("Rename Window"));
    ui->menubar->clear();
    for (auto *menu : {ui->menuFile, ui->menuEdit, search, ui->menuView, tabs, tools, ui->menuHelp})
        ui->menubar->addMenu(menu);
}

void MainWindow::refreshDynamicTexts()
{
    for (auto *action : findChildren<QAction *>()) {
        QByteArray source = action->property("translationSource").toByteArray();
        if (!source.isEmpty()) action->setText(tr(source.constData()));
    }
    for (auto *menu : findChildren<QMenu *>()) {
        QByteArray source = menu->property("translationSource").toByteArray();
        if (!source.isEmpty()) menu->setTitle(tr(source.constData()));
    }
    for (auto *button : findChildren<QPushButton *>()) {
        QByteArray source = button->property("translationSource").toByteArray();
        if (!source.isEmpty()) button->setText(tr(source.constData()));
    }
    if (auto *toolbar = findChild<QToolBar *>("jsonTools")) toolbar->setWindowTitle(tr("JSON Tools"));
    if (ui->tabWidget->cornerWidget()) ui->tabWidget->cornerWidget()->setToolTip(tr("New Tab"));
    m_findMode->setItemText(0, tr("Text Search"));
    m_findMode->setItemText(1, tr("Node Search"));
    m_findEdit->setPlaceholderText(tr("Find in JSON..."));
    m_toggleTreeBtn->setToolTip(tr("Show / hide JSON tree view"));
    if (auto *tab = currentTab()) onTreeVisibilityChanged(tab->isTreeVisible());
    updateTabStates();
    updateSearchCount();
}
