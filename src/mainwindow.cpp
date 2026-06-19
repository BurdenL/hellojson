#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "jsontab.h"

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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // ── Language actions: mutually exclusive group ────────────────────────
    QActionGroup *langGroup = new QActionGroup(this);
    langGroup->setExclusive(true);
    langGroup->addAction(ui->actionEnglish);
    langGroup->addAction(ui->actionChinese);
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
    toggleTreeAction->setShortcut(QKeySequence(tr("Ctrl+Shift+T")));
    toggleTreeAction->setCheckable(true);
    connect(toggleTreeAction, &QAction::triggered,
            this, &MainWindow::onToggleTree);
    ui->menuView->addSeparator();
    ui->menuView->addAction(toggleTreeAction);

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

    // "+" tab: clicking it creates a new tab before itself
    ui->tabWidget->tabBar()->setStyleSheet(
        "QTabBar::tab:last { max-width: 28px; font-weight: bold; }");
    connect(ui->tabWidget, &QTabWidget::tabBarClicked,
            this, [this](int index) {
        if (index == ui->tabWidget->count() - 1) {
            onNewTab();
        }
    });

    // ── Find bar (hidden by default) ─────────────────────────────────────
    m_findBar = new QWidget(this);
    m_findBar->setVisible(false);
    auto *findLayout = new QHBoxLayout(m_findBar);
    findLayout->setContentsMargins(4, 2, 4, 2);
    findLayout->setSpacing(4);

    m_findEdit = new QLineEdit(m_findBar);
    m_findEdit->setPlaceholderText(tr("Find in JSON..."));
    m_findEdit->setClearButtonEnabled(true);
    m_findEdit->setMinimumWidth(200);
    connect(m_findEdit, &QLineEdit::textChanged,
            this, &MainWindow::onFindTextChanged);
    connect(m_findEdit, &QLineEdit::returnPressed,
            this, &MainWindow::onFindNext);

    auto *prevBtn = new QPushButton(tr("◂ Prev"), m_findBar);
    prevBtn->setFixedWidth(60);
    connect(prevBtn, &QPushButton::clicked, this, &MainWindow::onFindPrev);

    auto *nextBtn = new QPushButton(tr("Next ▸"), m_findBar);
    nextBtn->setFixedWidth(60);
    connect(nextBtn, &QPushButton::clicked, this, &MainWindow::onFindNext);

    m_findCountLabel = new QLabel(m_findBar);
    m_findCountLabel->setMinimumWidth(60);

    auto *closeBtn = new QPushButton(tr("✕"), m_findBar);
    closeBtn->setFixedSize(24, 24);
    closeBtn->setFlat(true);
    connect(closeBtn, &QPushButton::clicked, this, &MainWindow::onFindClose);

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
    installEventFilter(this);

    // ── Create the initial tab ────────────────────────────────────────────
    createTab("Untitled");
    ensurePlusTab();
    updateTabStates();

    // ── Load initial language ─────────────────────────────────────────────
    m_translator = new QTranslator(this);
    QString systemLocale = QLocale::system().name();
    if (systemLocale.startsWith("zh"))
        switchLanguage("zh_CN");
    else
        switchLanguage("en");
}

MainWindow::~MainWindow()
{
    delete ui;
}

// ── Tab management ───────────────────────────────────────────────────────

JsonTab *MainWindow::currentTab() const
{
    auto *w = ui->tabWidget->currentWidget();
    return qobject_cast<JsonTab *>(w);
}

JsonTab *MainWindow::createTab(const QString &title)
{
    auto *tab = new JsonTab(this);
    // Insert after the current tab, before the "+" tab
    int cur = ui->tabWidget->currentIndex();
    int plusIdx = ui->tabWidget->count() - 1;
    int idx = (cur >= 0 && cur < plusIdx)
                  ? ui->tabWidget->insertTab(cur + 1, tab, title)
                  : ui->tabWidget->insertTab(plusIdx, tab, title);
    ui->tabWidget->setCurrentIndex(idx);
    connect(tab, &JsonTab::contentChanged, this, &MainWindow::updateTabStates);
    connect(tab, &JsonTab::treeVisibilityChanged,
            this, &MainWindow::onTreeVisibilityChanged);
    return tab;
}

void MainWindow::onNewTab()
{
    createTab("Untitled");
}

void MainWindow::onTabCloseRequested(int index)
{
    // Don't close the "+" tab itself
    if (index == ui->tabWidget->count() - 1) return;
    // Keep at least one real tab (total = 1 real + "+" = 2 minimum)
    if (ui->tabWidget->count() <= 2) return;

    QWidget *w = ui->tabWidget->widget(index);
    ui->tabWidget->removeTab(index);
    delete w;
    ensurePlusTab();
    updateTabStates();
}

void MainWindow::onCurrentTabChanged(int index)
{
    // If "+" tab selected, switch back to last real tab
    if (index == ui->tabWidget->count() - 1 && index > 0) {
        ui->tabWidget->setCurrentIndex(index - 1);
        return;
    }
    updateTabStates();
    // Sync toggle-tree button text with current tab's tree state
    JsonTab *tab = currentTab();
    if (tab)
        onTreeVisibilityChanged(tab->isTreeVisible());
}

void MainWindow::updateTabStates()
{
    JsonTab *tab = currentTab();
    bool hasTab = (tab != nullptr);
    ui->formatButton->setEnabled(hasTab);
    ui->compressButton->setEnabled(hasTab);
    ui->clearButton->setEnabled(hasTab);
    ui->actionFormat->setEnabled(hasTab);
    ui->actionCompress->setEnabled(hasTab);
    ui->actionClear->setEnabled(hasTab);

    if (m_toggleTreeBtn)
        m_toggleTreeBtn->setEnabled(hasTab);
}

void MainWindow::onCloseTab()
{
    int idx = ui->tabWidget->currentIndex();
    if (idx < 0 || idx == ui->tabWidget->count() - 1) return;  // "+" tab
    if (ui->tabWidget->count() <= 2) return;  // keep 1 real tab
    onTabCloseRequested(idx);
}

void MainWindow::onNextTab()
{
    int count = ui->tabWidget->count();
    if (count <= 2) return;  // only 1 real tab + "+"
    int lastReal = count - 2;
    int cur = ui->tabWidget->currentIndex();
    int next = (cur >= lastReal) ? 0 : cur + 1;
    ui->tabWidget->setCurrentIndex(next);
}

void MainWindow::onPrevTab()
{
    int count = ui->tabWidget->count();
    if (count <= 2) return;
    int lastReal = count - 2;
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
    QList<QAction *> actions = ui->menuView->actions();
    for (QAction *act : actions) {
        if (act->text().contains(tr("Tree View"))) {
            act->setChecked(visible);
            break;
        }
    }
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

// Ensure a "+" placeholder tab always exists at the end
void MainWindow::ensurePlusTab()
{
    int count = ui->tabWidget->count();
    // Remove stale "+" tab(s) from the end
    while (count > 0 && !qobject_cast<JsonTab *>(ui->tabWidget->widget(count - 1))) {
        ui->tabWidget->removeTab(count - 1);
        count--;
    }
    // Add a fresh "+" tab at the end
    int idx = ui->tabWidget->addTab(new QWidget(this), "+");
    // Hide close button on the "+" tab
    ui->tabWidget->tabBar()->setTabButton(idx, QTabBar::RightSide, nullptr);
    ui->tabWidget->tabBar()->setTabButton(idx, QTabBar::LeftSide, nullptr);
}

// ── Button / Menu slots ──────────────────────────────────────────────────

void MainWindow::onFormatClicked()
{
    JsonTab *tab = currentTab();
    if (!tab) return;

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
        // formatJson returned without success — parse error
        // Re-parse to get error details
        QJsonParseError err;
        QJsonDocument::fromJson(input.toUtf8(), &err);
        ui->statusbar->showMessage(
            tr("JSON Parse Error at offset %1: %2")
                .arg(err.offset)
                .arg(err.errorString()), 5000);
    }
}

void MainWindow::onCompressClicked()
{
    JsonTab *tab = currentTab();
    if (!tab) return;

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
        QJsonParseError err;
        QJsonDocument::fromJson(input.toUtf8(), &err);
        ui->statusbar->showMessage(
            tr("JSON Parse Error at offset %1: %2")
                .arg(err.offset)
                .arg(err.errorString()), 5000);
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
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open JSON File"),
        QString(),
        tr("JSON Files (*.json);;All Files (*)"));

    if (filePath.isEmpty())
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Cannot open file:\n%1").arg(filePath));
        return;
    }

    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();

    // If current tab is empty and untouched, reuse it; else create new
    JsonTab *tab = currentTab();
    if (tab && !tab->text().isEmpty()) {
        tab = createTab(QFileInfo(filePath).fileName());
    } else if (tab) {
        int idx = ui->tabWidget->currentIndex();
        ui->tabWidget->setTabText(idx, QFileInfo(filePath).fileName());
    }

    tab->setText(content);
    ui->statusbar->showMessage(
        tr("Loaded: %1").arg(filePath), 3000);
}

void MainWindow::onSaveFile()
{
    JsonTab *tab = currentTab();
    if (!tab) return;

    const QString output = tab->text();
    if (output.isEmpty()) {
        ui->statusbar->showMessage(tr("Nothing to save"), 3000);
        return;
    }

    const QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Save JSON File"),
        QString(),
        tr("JSON Files (*.json);;All Files (*)"));

    if (filePath.isEmpty())
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Cannot write file:\n%1").arg(filePath));
        return;
    }

    QTextStream stream(&file);
    stream << output;
    file.close();

    // Update tab title to saved filename
    int idx = ui->tabWidget->currentIndex();
    ui->tabWidget->setTabText(idx, QFileInfo(filePath).fileName());

    ui->statusbar->showMessage(
        tr("Saved: %1").arg(filePath), 3000);
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

    ui->retranslateUi(this);

    // Refresh all tabs' tree labels in the new language
    for (int i = 0; i < ui->tabWidget->count(); ++i) {
        auto *tab = qobject_cast<JsonTab *>(ui->tabWidget->widget(i));
        if (tab && tab->hasDocument()) {
            // Re-format the JSON to rebuild tree with translated type labels
            tab->formatJson(false);
        }
    }
}

void MainWindow::onLanguageChanged()
{
    if (ui->actionChinese->isChecked())
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
        if (ke->key() == Qt::Key_Escape && m_findBar->isVisible()) {
            onFindClose();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::onFindToggled()
{
    showFindBar(!m_findBar->isVisible());
}

void MainWindow::showFindBar(bool visible)
{
    m_findBar->setVisible(visible);
    if (visible) {
        m_findEdit->setFocus();
        m_findEdit->selectAll();
        // Re-search with current text if any
        if (!m_findEdit->text().isEmpty())
            performSearch();
    } else {
        // Clear highlights in current tab
        JsonTab *tab = currentTab();
        if (tab) tab->clearFind();
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
    performSearch();
}

void MainWindow::performSearch()
{
    JsonTab *tab = currentTab();
    if (!tab) return;

    const QString &text = m_findEdit->text();
    if (text.isEmpty()) {
        tab->clearFind();
        m_findCountLabel->clear();
        return;
    }

    tab->findText(text);

    int total = tab->matchCount();
    if (total > 0) {
        int cur = tab->currentMatchIndex() + 1;
        m_findCountLabel->setText(tr("%1 / %2").arg(cur).arg(total));
    } else {
        m_findCountLabel->setText(tr("No matches"));
    }
}

void MainWindow::onFindNext()
{
    JsonTab *tab = currentTab();
    if (!tab) return;

    const QString &text = m_findEdit->text();
    if (text.isEmpty()) return;

    tab->findNext(text);

    int total = tab->matchCount();
    if (total > 0) {
        int cur = tab->currentMatchIndex() + 1;
        m_findCountLabel->setText(tr("%1 / %2").arg(cur).arg(total));
    }
}

void MainWindow::onFindPrev()
{
    JsonTab *tab = currentTab();
    if (!tab) return;

    const QString &text = m_findEdit->text();
    if (text.isEmpty()) return;

    tab->findPrev(text);

    int total = tab->matchCount();
    if (total > 0) {
        int cur = tab->currentMatchIndex() + 1;
        m_findCountLabel->setText(tr("%1 / %2").arg(cur).arg(total));
    }
}

