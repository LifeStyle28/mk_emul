#include "ui/bridge/CHighlighter.h"

CHighlighter::CHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent) {
    QTextCharFormat kw;
    kw.setForeground(QColor("#569CD6"));
    QStringList keywords = {
        "auto",     "break",   "case",     "char",   "const",      "continue", "default",  "do",
        "double",   "else",    "enum",     "extern", "float",      "for",      "goto",     "if",
        "inline",   "int",     "long",     "register","restrict",  "return",   "short",    "signed",
        "sizeof",   "static",  "struct",   "switch", "typedef",    "union",    "unsigned", "void",
        "volatile", "while",   "uint8_t",  "uint16_t","uint32_t",  "int32_t",  "bool",     "true",
        "false",    "include", "define",   "ifdef",  "ifndef",     "endif",    "pragma"};
    for (const auto &k : keywords) {
        Rule r;
        r.pattern = QRegularExpression(QStringLiteral("\\b%1\\b").arg(k));
        r.format = kw;
        rules_.push_back(r);
    }
    QTextCharFormat num;
    num.setForeground(QColor("#B5CEA8"));
    rules_.push_back({QRegularExpression(QStringLiteral("\\b0x[0-9A-Fa-f]+\\b|\\b\\d+\\b")), num});
    QTextCharFormat str;
    str.setForeground(QColor("#CE9178"));
    rules_.push_back({QRegularExpression(QStringLiteral("\"[^\"]*\"")), str});
    QTextCharFormat pre;
    pre.setForeground(QColor("#C586C0"));
    rules_.push_back({QRegularExpression(QStringLiteral("^\\s*#.*")), pre});
    commentFormat_.setForeground(QColor("#6A9955"));
}

void CHighlighter::highlightBlock(const QString &text) {
    for (const auto &r : rules_) {
        auto it = r.pattern.globalMatch(text);
        while (it.hasNext()) {
            auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), r.format);
        }
    }
    static const QRegularExpression commentRe(QStringLiteral("//[^\n]*"));
    auto it = commentRe.globalMatch(text);
    while (it.hasNext()) {
        auto m = it.next();
        setFormat(m.capturedStart(), m.capturedLength(), commentFormat_);
    }
}
