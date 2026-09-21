#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "jsontab.h"
#include "jsontreemodel.h"

#include <QActionGroup>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QTextBrowser>
#include <QFrame>
#include <QPushButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QTabBar>
#include <QEvent>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLocale>
#include <QMessageBox>
#include <QTextStream>
#include <QTranslator>
#include <QComboBox>
#include <QInputDialog>
#include <QPlainTextEdit>
#include <QToolBar>
#include <QMenu>
#include <QSignalBlocker>
#include <QCloseEvent>
#include <QSettings>
#include <QStyle>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setupHiJsonActions();

    // ── Language actions: mutually exclusive group ────────────────────────
    QActionGroup *langGroup = new QActionGroup(this);
    langGroup->setExclusive(true);
    langGroup->addAction(ui->actionEnglish);
    langGroup->addAction(ui->actionChinese);
    langGroup->addAction(ui->actionTraditionalChinese);
    connect(langGroup, &QActionGroup::triggered,
            this, &MainWindow::onLanguageChanged);

    // ── Buttons ───────────────────────────────────────────────────────────
    connect(ui->formatButton, &QPushButton::clicked,
            this, &MainWindow::onFormatClicked);
    connect(ui->compressButton, &QPushButton::clicked,
            this, &MainWindow::onCompressClicked);
    connect(ui->clearButton, &QPushButton::clicked,
            this, &MainWindow::onClearClicked);

    // Toggle tree button
    m_toggleTreeBtn = new QPushButton(tr("Tree ▶"), this);
    m_toggleTreeBtn->setMinimumHeight(32);
    m_toggleTreeBtn->setToolTip(tr("Show / hide JSON tree view"));
    m_toggleTreeBtn->setEnabled(false);
    connect(m_toggleTreeBtn, &QPushButton::clicked,
            this, &MainWindow::onToggleTree);
    ui->buttonLayout->addWidget(m_toggleTreeBtn);

    // ── Menu actions ──────────────────────────────────────────────────────
    connect(ui->actionFormat, &QAction::triggered,
            this, &MainWindow::onFormatClicked);
    connect(ui->actionCompress, &QAction::triggered,
            this, &MainWindow::onCompressClicked);
    connect(ui->actionClear, &QAction::triggered,
            this, &MainWindow::onClearClicked);
    connect(ui->actionOpen, &QAction::triggered,
            this, &MainWindow::onOpenFile);
    connect(ui->actionSave, &QAction::triggered,
            this, &MainWindow::onSaveFile);
    connect(ui->actionExit, &QAction::triggered,
            this, &QMainWindow::close);
    connect(ui->actionNewTab, &QAction::triggered,
            this, &MainWindow::onNewTab);
    connect(ui->actionCloseTab, &QAction::triggered,
            this, &MainWindow::onCloseTab);
    connect(ui->actionNextTab, &QAction::triggered,
            this, &MainWindow::onNextTab);
    connect(ui->actionPrevTab, &QAction::triggered,
            this, &MainWindow::onPrevTab);
    connect(ui->actionExpandAll, &QAction::triggered,
            this, &MainWindow::onExpandAll);
    connect(ui->actionCollapseAll, &QAction::triggered,
            this, &MainWindow::onCollapseAll);

    // Toggle Tree — add action to View menu programmatically
    QAction *toggleTreeAction = new QAction(tr("&Tree View"), this);
    toggleTreeAction->setObjectName("toggleTreeAction");
    toggleTreeAction->setProperty("translationSource", "&Tree View");
    toggleTreeAction->setShortcut(QKeySequence(tr("Ctrl+Shift+T")));
    toggleTreeAction->setCheckable(true);
    connect(toggleTreeAction, &QAction::triggered,
            this, &MainWindow::onToggleTree);
    ui->menuView->addSeparator();
    ui->menuView->addAction(toggleTreeAction);
    setupMenuLayout();

    connect(ui->actionAbout, &QAction::triggered,
            this, &MainWindow::onAbout);
    connect(ui->actionOpenSource, &QAction::triggered,
            this, &MainWindow::onOpenSource);
    connect(ui->actionLicense, &QAction::triggered,
            this, &MainWindow::onLicense);
    connect(ui->actionFind, &QAction::triggered,
            this, &MainWindow::onFindToggled);

    // ── Tab widget ────────────────────────────────────────────────────────
    connect(ui->tabWidget, &QTabWidget::tabCloseRequested,
            this, &MainWindow::onTabCloseRequested);
    connect(ui->tabWidget, &QTabWidget::currentChanged,
            this, &MainWindow::onCurrentTabChanged);

    // ── Find bar (hidden by default) ─────────────────────────────────────
    m_findBar = new QWidget(this);
    m_findBar->setVisible(false);
    auto *findLayout = new QHBoxLayout(m_findBar);
    findLayout->setContentsMargins(4, 2, 4, 2);
    findLayout->setSpacing(4);

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(180);
    connect(m_searchTimer, &QTimer::timeout, this, &MainWindow::performSearch);
    m_findEdit = new QLineEdit(m_findBar);
    m_findEdit->setObjectName("searchText");
    m_findMode = new QComboBox(m_findBar);
    m_findMode->setObjectName("searchMode");
    m_findMode->addItems({tr("Text Search"), tr("Node Search")});
    connect(m_findMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        if (auto *tab = currentTab()) tab->setNodeSearch(m_findMode->currentIndex() == 1);
        performSearch();
    });
    m_findEdit->setPlaceholderText(tr("Find in JSON..."));
    m_findEdit->setClearButtonEnabled(true);
    m_findEdit->setMinimumWidth(200);
    connect(m_findEdit, &QLineEdit::textChanged,
            this, &MainWindow::onFindTextChanged);
    connect(m_findEdit, &QLineEdit::returnPressed,
            this, &MainWindow::onFindNext);

    auto *prevBtn = new QPushButton(tr("◂ Prev"), m_findBar);
    prevBtn->setProperty("translationSource", "◂ Prev");
    prevBtn->setFixedWidth(60);
    connect(prevBtn, &QPushButton::clicked, this, &MainWindow::onFindPrev);

    auto *nextBtn = new QPushButton(tr("Next ▸"), m_findBar);
    nextBtn->setProperty("translationSource", "Next ▸");
    nextBtn->setFixedWidth(60);
    connect(nextBtn, &QPushButton::clicked, this, &MainWindow::onFindNext);

    m_findCountLabel = new QLabel(m_findBar);
    m_findCountLabel->setMinimumWidth(60);

    auto *closeBtn = new QPushButton(tr("✕"), m_findBar);
    closeBtn->setFixedSize(24, 24);
    closeBtn->setFlat(true);
    connect(closeBtn, &QPushButton::clicked, this, &MainWindow::onFindClose);

    findLayout->addWidget(m_findMode);
    findLayout->addWidget(m_findEdit);
    findLayout->addWidget(prevBtn);
    findLayout->addWidget(nextBtn);
    findLayout->addWidget(m_findCountLabel);
    findLayout->addStretch();
    findLayout->addWidget(closeBtn);

    // Insert find bar between buttonLayout and tabWidget
    auto *centralLayout = qobject_cast<QVBoxLayout *>(ui->centralwidget->layout());
    int tabIdx = centralLayout->indexOf(ui->tabWidget);
    centralLayout->insertWidget(tabIdx, m_findBar);

    // Escape closes the find bar
    m_findEdit->installEventFilter(this);
    auto *escapeSearch = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escapeSearch, &QShortcut::activated, this, &MainWindow::onFindClose);

    // ── Create the initial tab ────────────────────────────────────────────
    createTab(tr("Untitled"), true);
    ensurePlusTab();
    updateTabStates();

    // ── Load initial language ─────────────────────────────────────────────
    m_translator = new QTranslator(this);
    m_qtTranslator = new QTranslator(this);
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "HelloJson", "HelloJson");
    m_lastDirectory = settings.value("lastDirectory", QDir::homePath()).toString();
    restoreGeometry(settings.value("geometry").toByteArray());
    if (auto *tab = currentTab()) tab->restoreViewState(settings.value("editorLayout").toByteArray());
    const QString savedLocale = settings.value("language").toString();
    QString systemLocale = QLocale::system().name();
    if (QStringList{"en", "zh_CN", "zh_TW"}.contains(savedLocale)) {
        switchLanguage(savedLocale);
        return;
    }
    if (QLocale::system().script() == QLocale::TraditionalHanScript)
        switchLanguage("zh_TW");
    else if (systemLocale.startsWith("zh"))
        switchLanguage("zh_CN");
    else
        switchLanguage("en");
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setupHiJsonActions()
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

void MainWindow::showUnicodeConverter()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Unicode / Escape Conversion"));
    dialog.resize(650, 450);
    auto *layout = new QVBoxLayout(&dialog);
    auto *source = new QPlainTextEdit(&dialog);
    source->setPlaceholderText(tr("Input escaped text"));
    auto *output = new QPlainTextEdit(&dialog);
    output->setReadOnly(true);
    auto *convert = new QPushButton(tr("Decode Escapes"), &dialog);
    auto *encode = new QPushButton(tr("Encode Unicode"), &dialog);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(convert);
    buttons->addWidget(encode);
    layout->addWidget(source);
    layout->addLayout(buttons);
    layout->addWidget(output);
    connect(convert, &QPushButton::clicked, &dialog, [source, output] {
        output->setPlainText(JsonTreeModel::unescape(source->toPlainText()));
    });
    connect(encode, &QPushButton::clicked, &dialog, [source, output] {
        QString encoded;
        for (QChar c : source->toPlainText()) {
            if (c.unicode() > 0x7f) encoded += QStringLiteral("\\u%1").arg(uint(c.unicode()), 4, 16, QChar('0'));
            else {
                QString quoted = JsonTreeModel::quote(QString(c));
                encoded += quoted.mid(1, quoted.size() - 2);
            }
        }
        output->setPlainText(encoded);
    });
    dialog.exec();
}

// ── Tab management ───────────────────────────────────────────────────────

JsonTab *MainWindow::currentTab() const
{
    auto *w = ui->tabWidget->currentWidget();
    return qobject_cast<JsonTab *>(w);
}

JsonTab *MainWindow::createTab(const QString &title, bool defaultTitle)
{
    auto *tab = new JsonTab(this);
    tab->setProperty("defaultTitle", defaultTitle);
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "HelloJson", "HelloJson");
    if (auto *previous = currentTab()) tab->restoreViewState(previous->viewState());
    else tab->restoreViewState(settings.value("editorLayout").toByteArray());
    connect(tab, &JsonTab::protectionMessage, this, [this](const QString &message) { ui->statusbar->showMessage(message, 15000); });
    // The add button lives outside the movable tabs.
    int cur = ui->tabWidget->currentIndex();
    int idx = ui->tabWidget->insertTab(cur + 1, tab, title);
    ui->tabWidget->setCurrentIndex(idx);
    connect(tab, &JsonTab::contentChanged, this, &MainWindow::updateTabStates);
    connect(tab, &JsonTab::treeVisibilityChanged,
            this, &MainWindow::onTreeVisibilityChanged);
    connect(tab, &JsonTab::searchResultsChanged, this, [this, tab] {
        if (tab == currentTab()) updateSearchCount();
    });
    return tab;
}

void MainWindow::onNewTab()
{
    createTab(tr("Untitled"), true);
}

void MainWindow::onTabCloseRequested(int index)
{
    if (index < 0 || index >= ui->tabWidget->count()) return;
    if (ui->tabWidget->count() <= 1) return;

    QWidget *w = ui->tabWidget->widget(index);
    if (!confirmDiscard(qobject_cast<JsonTab *>(w))) return;
    ui->tabWidget->removeTab(index);
    delete w;
    ensurePlusTab();
    updateTabStates();
}

void MainWindow::onCurrentTabChanged(int index)
{
    Q_UNUSED(index);
    updateTabStates();
    // Sync toggle-tree button text with current tab's tree state
    JsonTab *tab = currentTab();
    if (tab)
        onTreeVisibilityChanged(tab->isTreeVisible());
    if (m_findBar && m_findBar->isVisible()) performSearch();
}

void MainWindow::updateTabStates()
{
    JsonTab *tab = currentTab();
    for (int i = 0; i < ui->tabWidget->count(); ++i) {
        auto *document = qobject_cast<JsonTab *>(ui->tabWidget->widget(i));
        const bool modified = document && document->isModified();
        ui->tabWidget->setTabIcon(i, modified ? style()->standardIcon(QStyle::SP_DialogSaveButton) : QIcon());
        ui->tabWidget->setTabToolTip(i, modified ? tr("Unsaved changes") : document->filePath());
    }
    bool hasTab = tab && !tab->isLargeFile();
    ui->formatButton->setEnabled(hasTab);
    ui->compressButton->setEnabled(hasTab);
    ui->clearButton->setEnabled(hasTab);
    ui->actionFormat->setEnabled(hasTab);
    ui->actionCompress->setEnabled(hasTab);
    ui->actionClear->setEnabled(hasTab);
    ui->actionSave->setEnabled(hasTab);
    ui->actionFind->setEnabled(tab != nullptr);
    ui->actionExpandAll->setEnabled(hasTab);
    ui->actionCollapseAll->setEnabled(hasTab);
    if (auto *action = findChild<QAction *>("toggleTreeAction")) action->setEnabled(hasTab);
    for (auto *action : findChildren<QAction *>())
        if (action->property("requiresEditable").toBool()) action->setEnabled(hasTab);
    for (auto *action : findChildren<QAction *>())
        if (action->property("requiresTab").toBool()) action->setEnabled(tab != nullptr);
    const bool multipleTabs = ui->tabWidget->count() > 1;
    ui->actionCloseTab->setEnabled(tab && multipleTabs);
    ui->actionNextTab->setEnabled(multipleTabs);
    ui->actionPrevTab->setEnabled(multipleTabs);
    if (m_findBar && !hasTab) showFindBar(false);

    if (m_toggleTreeBtn)
        m_toggleTreeBtn->setEnabled(hasTab);
}

void MainWindow::onCloseTab()
{
    int idx = ui->tabWidget->currentIndex();
    if (idx < 0 || ui->tabWidget->count() <= 1) return;
    onTabCloseRequested(idx);
}

void MainWindow::onNextTab()
{
    int count = ui->tabWidget->count();
    if (count <= 1) return;
    int lastReal = count - 1;
    int cur = ui->tabWidget->currentIndex();
    int next = (cur >= lastReal) ? 0 : cur + 1;
    ui->tabWidget->setCurrentIndex(next);
}

void MainWindow::onPrevTab()
{
    int count = ui->tabWidget->count();
    if (count <= 1) return;
    int lastReal = count - 1;
    int cur = ui->tabWidget->currentIndex();
    int prev = (cur <= 0) ? lastReal : cur - 1;
    ui->tabWidget->setCurrentIndex(prev);
}

void MainWindow::onExpandAll()
{
    JsonTab *tab = currentTab();
    if (tab) tab->expandAll();
}

void MainWindow::onCollapseAll()
{
    JsonTab *tab = currentTab();
    if (tab) tab->collapseAll();
}

void MainWindow::onToggleTree()
{
    JsonTab *tab = currentTab();
    if (!tab) return;
    tab->setTreeVisible(!tab->isTreeVisible());
}

void MainWindow::onTreeVisibilityChanged(bool visible)
{
    if (m_toggleTreeBtn) {
        m_toggleTreeBtn->setText(visible ? tr("Tree ▼") : tr("Tree ▶"));
    }
    // Also update the menu action check state
    if (auto *action = findChild<QAction *>("toggleTreeAction"))
        action->setChecked(visible);
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About Hello JSON"),
        tr("<h3>Hello JSON v0.1</h3>"
           "<p>A lightweight JSON formatter and viewer built with Qt.</p>"
           "<p>Features:<br>"
           "&bull; JSON format &amp; compress<br>"
           "&bull; Tree structure viewer<br>"
           "&bull; Multi-tab editing<br>"
           "&bull; Text search<br>"
           "&bull; Copy value / path</p>"
           "<p style='margin-top:12pt'>"
           "<b>Developer:</b> BurdenL<br>"
           "<b>GitHub:</b> <a href='https://github.com/BurdenL/hellojson'>"
           "github.com/BurdenL/hellojson</a></p>"));
}

void MainWindow::onOpenSource()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Open Source Notice"));
    dlg.resize(520, 360);
    auto *layout = new QVBoxLayout(&dlg);

    auto *browser = new QTextBrowser(&dlg);
    browser->setOpenExternalLinks(true);
    browser->setHtml(
        tr("<h3>Open Source Software Notice</h3>"
           "<p>Hello JSON is built with the following open source components:</p>"
           "<p><b>Qt</b> &mdash; Cross-platform application framework<br>"
           "Version: 6.x &nbsp;|&nbsp; "
           "<a href='https://www.qt.io/licensing/'>License</a><br>"
           "Qt is available under the LGPL v3 / GPL v2 / Commercial license.</p>"
           "<p><b>GCC / MinGW-w64</b><br>"
           "C++ compiler toolchain</p>"
           "<p>Full source code of this application is available at:<br>"
           "<a href='https://github.com/burden/hellojson'>"
           "github.com/burden/hellojson</a></p>"));
    browser->setReadOnly(true);
    layout->addWidget(browser);

    auto *closeBtn = new QPushButton(tr("Close"), &dlg);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    layout->addWidget(closeBtn);

    dlg.exec();
}

void MainWindow::onLicense()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("License"));
    dlg.resize(520, 420);
    auto *layout = new QVBoxLayout(&dlg);

    auto *browser = new QTextBrowser(&dlg);
    browser->setReadOnly(true);
    browser->setPlainText(
        "MIT License\n\n"
        "Copyright (c) 2025 Hello JSON Contributors\n\n"
        "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
        "of this software and associated documentation files (the \"Software\"), to deal\n"
        "in the Software without restriction, including without limitation the rights\n"
        "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n"
        "copies of the Software, and to permit persons to whom the Software is\n"
        "furnished to do so, subject to the following conditions:\n\n"
        "The above copyright notice and this permission notice shall be included in\n"
        "all copies or substantial portions of the Software.\n\n"
        "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
        "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
        "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
        "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
        "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n"
        "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN\n"
        "THE SOFTWARE.\n");
    layout->addWidget(browser);

    auto *closeBtn = new QPushButton(tr("Close"), &dlg);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    layout->addWidget(closeBtn);

    dlg.exec();
}

// A corner button cannot be dragged into the document tabs.
void MainWindow::ensurePlusTab()
{
    if (ui->tabWidget->cornerWidget()) return;
    auto *button = new QPushButton("+", ui->tabWidget);
    button->setFixedWidth(28);
    button->setToolTip(tr("New Tab"));
    connect(button, &QPushButton::clicked, this, &MainWindow::onNewTab);
    ui->tabWidget->setCornerWidget(button);
}

// ── Button / Menu slots ──────────────────────────────────────────────────

void MainWindow::onFormatClicked()
{
    JsonTab *tab = currentTab();
    if (!tab || tab->isLargeFile()) return;

    const QString input = tab->text().trimmed();
    if (input.isEmpty()) {
        ui->statusbar->showMessage(tr("Input is empty"), 3000);
        return;
    }

    tab->formatJson(false);

    if (tab->hasDocument()) {
        ui->statusbar->showMessage(
            tr("JSON formatted successfully"), 3000);
    } else {
        ui->statusbar->showMessage(tab->parseError(), 5000);
    }
}

void MainWindow::onCompressClicked()
{
    JsonTab *tab = currentTab();
    if (!tab || tab->isLargeFile()) return;

    const QString input = tab->text().trimmed();
    if (input.isEmpty()) {
        ui->statusbar->showMessage(tr("Input is empty"), 3000);
        return;
    }

    tab->formatJson(true);

    if (tab->hasDocument()) {
        ui->statusbar->showMessage(
            tr("JSON compressed successfully"), 3000);
    } else {
        ui->statusbar->showMessage(tab->parseError(), 5000);
    }
}

void MainWindow::onClearClicked()
{
    JsonTab *tab = currentTab();
    if (!tab) return;
    tab->clear();
    ui->statusbar->clearMessage();
}

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

void MainWindow::persistSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "HelloJson", "HelloJson");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("language", m_currentLocale);
    settings.setValue("lastDirectory", m_lastDirectory);
    if (auto *tab = currentTab()) settings.setValue("editorLayout", tab->viewState());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    for (int i = 0; i < ui->tabWidget->count(); ++i)
        if (!confirmDiscard(qobject_cast<JsonTab *>(ui->tabWidget->widget(i)))) { event->ignore(); return; }
    persistSettings();
    event->accept();
}

// ── Language switching ───────────────────────────────────────────────────

QString MainWindow::translationFilePath(const QString &locale) const
{
    const QString fileName = "hellojson_" + locale + ".qm";

    QStringList searchDirs;
    searchDirs << ":/i18n";
    searchDirs << QApplication::applicationDirPath();
    searchDirs << QDir::currentPath();

    for (const QString &dir : searchDirs) {
        QString fullPath = dir + "/" + fileName;
        if (QFile::exists(fullPath))
            return fullPath;
    }
    return fileName;
}

void MainWindow::loadTranslation(const QString &locale)
{
    qApp->removeTranslator(m_qtTranslator);
    if (locale != "en") {
        const QString name = "qtbase_" + locale + ".qm";
        if (m_qtTranslator->load(":/i18n/" + name) ||
            m_qtTranslator->load(QApplication::applicationDirPath() + "/" + name))
            qApp->installTranslator(m_qtTranslator);
    }
    if (locale == "en") {
        qApp->removeTranslator(m_translator);
        return;
    }

    QString path = translationFilePath(locale);
    if (m_translator->load(path)) {
        qApp->installTranslator(m_translator);
    } else {
        qWarning() << "Failed to load translation for locale:" << locale
                   << " tried path:" << path;
    }
}

void MainWindow::switchLanguage(const QString &locale)
{
    m_currentLocale = locale;
    qApp->removeTranslator(m_translator);
    loadTranslation(locale);

    ui->actionEnglish->setChecked(locale == "en");
    ui->actionChinese->setChecked(locale == "zh_CN");
    ui->actionTraditionalChinese->setChecked(locale == "zh_TW");

    ui->retranslateUi(this);

    // Refresh all tabs' tree labels in the new language
    for (int i = 0; i < ui->tabWidget->count(); ++i) {
        auto *tab = qobject_cast<JsonTab *>(ui->tabWidget->widget(i));
        if (tab) {
            tab->refreshLanguage();
            if (tab->property("defaultTitle").toBool())
                ui->tabWidget->setTabText(i, tr("Untitled"));
        }
    }
    refreshDynamicTexts();
    QSettings(QSettings::IniFormat, QSettings::UserScope, "HelloJson", "HelloJson").setValue("language", locale);
}

void MainWindow::onLanguageChanged()
{
    if (ui->actionTraditionalChinese->isChecked())
        switchLanguage("zh_TW");
    else if (ui->actionChinese->isChecked())
        switchLanguage("zh_CN");
    else
        switchLanguage("en");
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
    QMainWindow::changeEvent(event);
}

// ── Search ───────────────────────────────────────────────────────────────

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

