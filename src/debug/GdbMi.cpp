#include "debug/GdbMi.h"

namespace {

const MiValue kEmpty;

class Parser {
public:
    explicit Parser(const QString &s) : s_(s) {}

    bool atEnd() const { return i_ >= s_.size(); }
    QChar peek() const { return atEnd() ? QChar() : s_[i_]; }

    // C string with gdb escapes; \NNN octal escapes are bytes (UTF-8 text comes as octal).
    QString cstring() {
        QByteArray bytes;
        if (peek() != '"')
            return {};
        ++i_;
        while (!atEnd() && s_[i_] != '"') {
            QChar c = s_[i_++];
            if (c != '\\') {
                bytes += QString(c).toUtf8();
                continue;
            }
            if (atEnd())
                break;
            c = s_[i_++];
            switch (c.unicode()) {
            case 'n': bytes += '\n'; break;
            case 't': bytes += '\t'; break;
            case 'r': bytes += '\r'; break;
            case 'a': bytes += '\a'; break;
            case 'b': bytes += '\b'; break;
            case 'f': bytes += '\f'; break;
            case 'v': bytes += '\v'; break;
            case 'e': bytes += '\x1b'; break;
            default:
                if (c >= '0' && c <= '7') {
                    int v = c.unicode() - '0';
                    for (int k = 0; k < 2 && !atEnd() && s_[i_] >= '0' && s_[i_] <= '7'; ++k)
                        v = v * 8 + (s_[i_++].unicode() - '0');
                    bytes += static_cast<char>(v);
                } else {
                    bytes += QString(c).toUtf8();
                }
            }
        }
        ++i_; // closing quote
        return QString::fromUtf8(bytes);
    }

    QString name() {
        const int start = i_;
        while (!atEnd() && s_[i_] != '=' && s_[i_] != ',' && s_[i_] != '}' && s_[i_] != ']')
            ++i_;
        return s_.mid(start, i_ - start);
    }

    MiValue value() {
        MiValue v;
        const QChar c = peek();
        if (c == '"') {
            v.str = cstring();
        } else if (c == '{') {
            v.kind = MiValue::Tuple;
            ++i_;
            while (!atEnd() && peek() != '}') {
                const QString n = name();
                if (peek() == '=')
                    ++i_;
                v.items.push_back({n, value()});
                if (peek() == ',')
                    ++i_;
            }
            ++i_;
        } else if (c == '[') {
            v.kind = MiValue::List;
            ++i_;
            while (!atEnd() && peek() != ']') {
                if (peek() == '"' || peek() == '{' || peek() == '[') {
                    v.items.push_back({QString(), value()});
                } else {
                    const QString n = name();
                    if (peek() == '=')
                        ++i_;
                    v.items.push_back({n, value()});
                }
                if (peek() == ',')
                    ++i_;
            }
            ++i_;
        } else {
            ++i_; // malformed: skip a character so parsing always ends
        }
        return v;
    }

    // ,name=value,name=value... up to the end of the line
    MiValue results() {
        MiValue t;
        t.kind = MiValue::Tuple;
        while (!atEnd()) {
            if (peek() == ',')
                ++i_;
            const QString n = name();
            if (peek() != '=')
                break;
            ++i_;
            t.items.push_back({n, value()});
        }
        return t;
    }

    int pos() const { return i_; }
    void setPos(int p) { i_ = p; }

private:
    const QString &s_;
    int i_ = 0;
};

} // namespace

const MiValue &MiValue::operator[](const QString &name) const {
    for (const auto &it : items) {
        if (it.first == name)
            return it.second;
    }
    return kEmpty;
}

bool MiValue::has(const QString &name) const {
    for (const auto &it : items) {
        if (it.first == name)
            return true;
    }
    return false;
}

MiRecord parseMiRecord(const QString &rawLine) {
    MiRecord r;
    QString line = rawLine;
    while (line.endsWith('\r') || line.endsWith('\n'))
        line.chop(1);
    if (line.startsWith(QLatin1String("(gdb)"))) {
        r.type = MiRecord::Prompt;
        return r;
    }
    int i = 0;
    while (i < line.size() && line[i].isDigit())
        ++i;
    if (i > 0)
        r.token = line.left(i).toInt();
    if (i >= line.size())
        return r;
    const QChar kind = line[i];
    Parser p(line);
    p.setPos(i + 1);
    switch (kind.unicode()) {
    case '^': r.type = MiRecord::Result; break;
    case '*': r.type = MiRecord::Exec; break;
    case '+': r.type = MiRecord::Status; break;
    case '=': r.type = MiRecord::Notify; break;
    case '~': r.type = MiRecord::Console; break;
    case '@': r.type = MiRecord::Target; break;
    case '&': r.type = MiRecord::Log; break;
    default: return r;
    }
    if (r.type == MiRecord::Console || r.type == MiRecord::Target || r.type == MiRecord::Log) {
        r.value.str = p.cstring();
        return r;
    }
    const int start = p.pos();
    int end = line.indexOf(',', start);
    if (end < 0)
        end = line.size();
    r.cls = line.mid(start, end - start);
    p.setPos(end);
    r.value = p.results();
    return r;
}

QString miQuote(const QString &s) {
    QString out = "\"";
    for (const QChar c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + '"';
}
