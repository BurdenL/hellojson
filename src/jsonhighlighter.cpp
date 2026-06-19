#include "jsonhighlighter.h"

JsonHighlighter::JsonHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    addStringRules();
    addNumberRule();
    addKeywordRule();
}

void JsonHighlighter::addStringRules()
{
    // ── String values (double-quoted) ────────────────────────────────────
    m_stringFormat.setForeground(QColor(0x1A, 0x6B, 0xBD));  // blue
    HighlightRule stringRule;
    stringRule.pattern = QRegularExpression(
        R"("[^"\\]*(?:\\.[^"\\]*)*")");
    stringRule.format = m_stringFormat;
    m_rules.append(stringRule);
}

void JsonHighlighter::addNumberRule()
{
    // ── Numbers: integers, floats, scientific notation ──────────────────
    QTextCharFormat numberFormat;
    numberFormat.setForeground(QColor(0xB7, 0x1C, 0x1C));    // dark red

    HighlightRule numberRule;
    numberRule.pattern = QRegularExpression(
        R"(\b-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?\b)");
    numberRule.format = numberFormat;
    m_rules.append(numberRule);
}

void JsonHighlighter::addKeywordRule()
{
    // ── Boolean / null ──────────────────────────────────────────────────
    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(QColor(0x8B, 0x00, 0x8B));    // dark magenta
    keywordFormat.setFontWeight(QFont::Bold);

    HighlightRule keywordRule;
    keywordRule.pattern = QRegularExpression(
        R"(\b(?:true|false|null)\b)");
    keywordRule.format = keywordFormat;
    m_rules.append(keywordRule);
}

void JsonHighlighter::highlightBlock(const QString &text)
{
    for (const HighlightRule &rule : m_rules) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(),
                      match.capturedLength(),
                      rule.format);
        }
    }
}
