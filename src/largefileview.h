#pragma once
#include "jsondatasource.h"
#include <QWidget>
#include <atomic>
#include <memory>

class QThread;
class QPlainTextEdit;
class QLabel;
class QLineEdit;
class QPushButton;

class PageReader : public QObject
{
    Q_OBJECT
public:
    explicit PageReader(std::shared_ptr<std::atomic<quint64>> latest);
public slots:
    void load(const QString &path, qint64 page, quint64 request);
signals:
    void ready(const JsonTextPage &page);
private:
    std::shared_ptr<std::atomic<quint64>> m_latest;
    QString m_path;
    std::unique_ptr<FileJsonSource> m_source;
};

class LargeFileView : public QWidget
{
    Q_OBJECT
public:
    explicit LargeFileView(QWidget *parent = nullptr);
    ~LargeFileView() override;
    void openFile(const QString &path);
    void goToByte(qint64 offset);
    void retranslate();
    bool isLoading() const { return m_loading; }
    const JsonTextPage &currentPage() const { return m_page; }
    QString filePath() const { return m_path; }
signals:
    void requestPage(const QString &path, qint64 page, quint64 request);
    void pageLoaded();
private:
    void load(qint64 page);
    void updateControls();
    QThread *m_thread;
    std::shared_ptr<std::atomic<quint64>> m_latest;
    QString m_path;
    JsonTextPage m_page;
    bool m_loading = false;
    qint64 m_requestedOffset = -1;
    QLabel *m_notice;
    QLabel *m_status;
    QLabel *m_jumpLabel;
    QPlainTextEdit *m_text;
    QLineEdit *m_offset;
    QPushButton *m_first;
    QPushButton *m_previous;
    QPushButton *m_next;
    QPushButton *m_last;
    QPushButton *m_jump;
};
