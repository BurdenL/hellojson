#ifndef JSONHIGHLIGHTER_H
#define JSONHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QTextCharFormat>

class JsonHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit JsonHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct HighlightRule {
        QRegularExpression pattern;
        QTextCharFormat    format;
    };

    void addStringRules();
    void addNumberRule();
    void addKeywordRule();

    QVector<HighlightRule> m_rules;

    // Multi-line string state
    QTextCharFormat m_stringFormat;
};

#endif // JSONHIGHLIGHTER_H
