#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class JsonTab;
class QLineEdit;
class QLabel;
class QTranslator;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void changeEvent(QEvent *event) override;
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
    JsonTab *currentTab() const;
    JsonTab *createTab(const QString &title);
    void ensurePlusTab();
    void switchLanguage(const QString &locale);
    void loadTranslation(const QString &locale);
    QString translationFilePath(const QString &locale) const;
    void updateTabStates();
    void showFindBar(bool visible);
    void performSearch();

    Ui::MainWindow *ui;
    QTranslator *m_translator = nullptr;
    QString m_currentLocale;

    // Find bar widgets
    QWidget     *m_findBar = nullptr;
    QLineEdit   *m_findEdit = nullptr;
    QLabel      *m_findCountLabel = nullptr;
    QString      m_lastSearchText;
};

#endif // MAINWINDOW_H
