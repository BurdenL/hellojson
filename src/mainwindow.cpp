#include "mainwindow.h"
#include "editorlimits.h"
#include "ui_mainwindow.h"
#include "jsontab.h"

#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QShortcut>
#include <QLocale>
#include <QTranslator>
#include <QComboBox>
#include <QSettings>
#include <QStyle>
#include <QTimer>
#include <QMenu>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setupActions();

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
    m_searchTimer->setInterval(EditorLimits::SearchDelayMs);
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

void MainWindow::ensurePlusTab()
{
    if (ui->tabWidget->cornerWidget()) return;
    auto *button = new QPushButton("+", ui->tabWidget);
    button->setFixedWidth(28);
    button->setToolTip(tr("New Tab"));
    connect(button, &QPushButton::clicked, this, &MainWindow::onNewTab);
    ui->tabWidget->setCornerWidget(button);
}


void MainWindow::onFormatClicked()
{
    formatCurrentDocument(false);
}

void MainWindow::onCompressClicked()
{
    formatCurrentDocument(true);
}

void MainWindow::formatCurrentDocument(bool compressed)
{
    JsonTab *tab = currentTab();
    if (!tab || tab->isLargeFile()) return;
    if (tab->text().trimmed().isEmpty()) {
        ui->statusbar->showMessage(tr("Input is empty"), 3000);
        return;
    }
    tab->formatJson(compressed);
    if (tab->hasDocument()) {
        ui->statusbar->showMessage(compressed ? tr("JSON compressed successfully")
                                             : tr("JSON formatted successfully"), 3000);
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

