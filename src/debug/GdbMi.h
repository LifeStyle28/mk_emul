#pragma once

#include <QPair>
#include <QString>
#include <QVector>

// GDB/MI output (gdb --interpreter=mi3): one line = one record, e.g.
//   12^done,bkpt={number="1",addr="0x08000234",line="15"}
//   *stopped,reason="breakpoint-hit",frame={func="main",line="15",...}
//   ~"console text\n"
struct MiValue {
    enum Kind { String, Tuple, List };
    Kind kind = String;
    QString str;
    // Tuple: named fields. List: items; a list of results ([frame={..},frame={..}]) keeps the
    // names, a list of values ([".." , ".."]) has empty names.
    QVector<QPair<QString, MiValue>> items;

    const MiValue &operator[](const QString &name) const; // empty String if missing
    QString text(const QString &name) const { return (*this)[name].str; }
    bool has(const QString &name) const;
};

struct MiRecord {
    enum Type { Invalid, Result, Exec, Status, Notify, Console, Target, Log, Prompt };
    Type type = Invalid;
    int token = -1;
    QString cls;   // done / running / connected / error / exit, or stopped / running / ...
    MiValue value; // results as a tuple; Console/Target/Log: str is the text
};

MiRecord parseMiRecord(const QString &line);
// Quotes a string for an MI command argument ("C:/x y/main.c" -> "\"C:/x y/main.c\"").
QString miQuote(const QString &s);
