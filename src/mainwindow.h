#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class JsonTab;
class QLineEdit;
class QLabel;
class QPushButton;
class QTranslator;
class QComboBox;
class QTimer;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

// 窗口协调层：路由操作与用户确认，不负责 JSON 解析或后台文件扫描。
// 实现按 actions / documents / localization / search / dialogs 拆分。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onFormatClicked();
    void onCompressClicked();
    void onClearClicked();
    void onNewTab();
    void onCloseTab();
    void onNextTab();
    void onPrevTab();
    void onExpandAll();
    void onCollapseAll();
    void onToggleTree();
    void onTreeVisibilityChanged(bool visible);
    void onAbout();
    void onOpenSource();
    void onLicense();
    void onOpenFile();
    void onSaveFile();
    void onTabCloseRequested(int index);
    void onCurrentTabChanged(int index);
    void onLanguageChanged();

    // Search
    void onFindToggled();
    void onFindTextChanged(const QString &text);
    void onFindNext();
    void onFindPrev();
    void onFindClose();

private:
    // 文件生命周期：保存失败与取消都通过 false 传回关闭流程。
    bool saveTab(JsonTab *tab, bool saveAs = false);
    bool confirmDiscard(JsonTab *tab);
    void persistSettings();
    // 标签页由 Qt 父子关系管理，currentTab 返回非拥有指针。
    JsonTab *currentTab() const;
    JsonTab *createTab(const QString &title, bool defaultTitle = false);
    void ensurePlusTab();
    void switchLanguage(const QString &locale);
    void loadTranslation(const QString &locale);
    QString translationFilePath(const QString &locale) const;
    void updateTabStates();
    void showFindBar(bool visible);
    void performSearch();
    void setupActions();
    void setupMenuLayout();
    void refreshDynamicTexts();
    void updateSearchCount();
    void showUnicodeConverter();
    void formatCurrentDocument(bool compressed);
    void openFileWithMode(bool largeFile);

    Ui::MainWindow *ui;
    QTranslator *m_translator = nullptr;
    QTranslator *m_qtTranslator = nullptr;
    QString m_currentLocale;
    QString m_lastDirectory;

    // Toggle tree button
    QPushButton *m_toggleTreeBtn = nullptr;

    // Find bar widgets
    QWidget     *m_findBar = nullptr;
    QLineEdit   *m_findEdit = nullptr;
    QComboBox   *m_findMode = nullptr;
    QLabel      *m_findCountLabel = nullptr;
    QString      m_lastSearchText;
    QTimer *m_searchTimer = nullptr;
};

#endif // MAINWINDOW_H
