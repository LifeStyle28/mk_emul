#pragma once

#include "debug/GdbMi.h"

#include <QAbstractListModel>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <functional>

// List model of QVariantMap rows (keys = role names), for the debugger panels.
class RowsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    explicit RowsModel(const QStringList &roles, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return rows_.size(); }
    const QVector<QVariantMap> &rows() const { return rows_; }
    // Same row count: rows are updated in place (the view keeps its scroll position).
    void setRows(const QVector<QVariantMap> &rows);

signals:
    void countChanged();

private:
    QStringList roles_;
    QVector<QVariantMap> rows_;
};

// The UI debugger: runs arm-none-eabi-gdb (MI) against the emulator's GDB server.
// Source breakpoints, Step Over / Into / Out, call stack, local and global variables and watch
// expressions all come from gdb and the ELF's debug info, as in STM32CubeIDE.
//
// Breakpoints are kept here (file + line of the project, or an address) and survive rebuilds:
// each new session inserts them again. Paths: the builder compiles project files from
// <work>/src/<relative path>, so gdb reports those; they map back to project files.
class DebugSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)          // off / starting / stopped / running
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY locationChanged) // project-relative, or ""
    Q_PROPERTY(int currentLine READ currentLine NOTIFY locationChanged)     // 1-based, -1 if none
    Q_PROPERTY(QString location READ location NOTIFY locationChanged)
    Q_PROPERTY(int selectedFrame READ selectedFrame NOTIFY locationChanged)
    Q_PROPERTY(bool hex READ hex WRITE setHex NOTIFY hexChanged)
    Q_PROPERTY(int breakpointsVersion READ breakpointsVersion NOTIFY breakpointsChanged)
    Q_PROPERTY(QObject *variables READ variables CONSTANT)
    Q_PROPERTY(QObject *watches READ watches CONSTANT)
    Q_PROPERTY(QObject *frames READ frames CONSTANT)
    Q_PROPERTY(QObject *breakpoints READ breakpoints CONSTANT)
public:
    explicit DebugSession(QObject *parent = nullptr);
    ~DebugSession() override;

    // Starts gdb on elf and connects it to the emulator's GDB server at port. sourceRoot is the
    // folder the project files were compiled from ("" for a firmware opened from disk);
    // skipDirs: library sources Step Into does not enter (bundled HAL, FreeRTOS, ...).
    void start(const QString &gdb, const QString &elf, quint16 port, const QString &sourceRoot,
               const QStringList &skipDirs = {});
    void stop();
    // The machine was reset or reloaded behind gdb's back: re-read registers and views.
    void targetChanged();

    QString state() const { return state_; }
    bool active() const { return state_ != QLatin1String("off"); }
    QString message() const { return message_; }
    QString currentFile() const { return currentFile_; }
    int currentLine() const { return currentLine_; }
    QString location() const { return location_; }
    int selectedFrame() const { return selectedFrame_; }
    bool hex() const { return hex_; }
    void setHex(bool on);
    int breakpointsVersion() const { return bpVersion_; }
    QObject *variables() { return &variables_; }
    QObject *watches() { return &watchModel_; }
    QObject *frames() { return &frames_; }
    QObject *breakpoints() { return &bpModel_; }

    // Address breakpoints that must be set directly on the machine while there is no session.
    QList<quint32> addressBreakpoints() const;

    Q_INVOKABLE void toggleBreakpoint(const QString &file, int line);
    Q_INVOKABLE QVariantList breakpointLines(const QString &file) const; // [{line, ok}]
    Q_INVOKABLE bool addAddressBreakpoint(const QString &text);
    Q_INVOKABLE void removeBreakpoint(int index);
    Q_INVOKABLE void clearBreakpoints();

    Q_INVOKABLE void continueRun();
    Q_INVOKABLE void interrupt();
    Q_INVOKABLE void stepOver();
    Q_INVOKABLE void stepInto();
    Q_INVOKABLE void stepOut();
    Q_INVOKABLE void stepInstruction();
    Q_INVOKABLE void selectFrame(int level);

    Q_INVOKABLE void toggleExpand(bool watch, int row);
    Q_INVOKABLE void addWatch(const QString &expression);
    Q_INVOKABLE void removeWatch(int row);
    Q_INVOKABLE void setValue(bool watch, int row, const QString &value);

    // For tests: runs an MI command, the callback gets the result record.
    void command(const QString &cmd, std::function<void(const MiRecord &)> done = {});

signals:
    void stateChanged();
    void messageChanged();
    void locationChanged();
    void hexChanged();
    void breakpointsChanged();
    // Address breakpoints changed while there is no session (the owner sets them on the machine).
    void addressBreakpointsChanged();
    void refreshed(); // views updated after a stop (tests wait for it)

private:
    struct Breakpoint {
        QString file; // project-relative; empty for an address breakpoint
        int line = 0; // requested line
        quint32 address = 0;
        int number = -1;     // gdb breakpoint number
        int actualLine = 0;  // where gdb put it (next line with code)
        QString state;       // pending / ok / error
        QString error;
        bool operator==(const Breakpoint &o) const { return file == o.file && line == o.line && address == o.address; }
    };
    struct VarNode {
        QString varobj; // gdb variable object
        QString key;    // section/path: keeps expansion and change marks between stops
        QString name;
        QString value;
        QString type;
        int numchild = 0;
        bool expanded = false;
        bool loaded = false;
        bool error = false;
        bool header = false;
        QVector<VarNode> children;
    };

    void setState(const QString &s);
    void setMessage(const QString &m);
    void onOutput();
    void handleRecord(const MiRecord &r);
    void onStopped(const MiRecord &r);
    void refresh();
    void loadVariables(quint64 gen);
    void createVar(QVector<VarNode> *list, int index, const QString &expr, quint64 gen, bool watch,
                   const QString &frameOpt);
    void loadChildren(bool watch, const QString &varobj, quint64 gen);
    VarNode *findNode(QVector<VarNode> &list, const QString &varobj);
    void publishVariables();
    void flatten(const QVector<VarNode> &list, int depth, bool watch, QVector<QVariantMap> *out);
    void deleteVarobjs();
    void insertBreakpoint(int index);
    void publishBreakpoints();
    // Runs fn while the target is stopped: at once, or after interrupting it (then continues).
    void whenStopped(std::function<void()> fn);
    void exec(const QString &cmd);
    QString toProjectFile(const QString &fullname) const;
    QString absSource(const QString &rel) const;

    void logLine(const char *dir, const QByteArray &line);

    QProcess gdb_;
    QByteArray outBuf_;
    QString logPath_; // MK_EMUL_GDB_LOG: MI traffic for troubleshooting
    int nextToken_ = 1;
    QHash<int, std::function<void(const MiRecord &)>> pending_;
    QString state_ = QStringLiteral("off");
    QString message_;
    QString sourceRoot_;
    QString currentFile_;
    int currentLine_ = -1;
    QString location_;
    int selectedFrame_ = 0;
    bool hex_ = false;
    quint64 gen_ = 0; // bumped on every stop/run: drops callbacks of an older refresh
    bool connected_ = false;

    QVector<Breakpoint> bps_;
    int bpVersion_ = 0;
    QVector<std::function<void()>> afterStop_;
    bool resumeAfterStop_ = false;
    bool interruptSent_ = false;

    QVector<QPair<QString, QString>> globals_; // display name, gdb expression
    QStringList watchExprs_;
    QVector<VarNode> vars_;
    QVector<VarNode> watchVars_;
    QSet<QString> expanded_;
    QHash<QString, QString> lastValues_;
    QHash<QString, QString> values_;
    QSet<QString> changed_;
    QStringList varobjs_; // roots to delete before the next refresh
    int varsPending_ = 0;

    RowsModel variables_;
    RowsModel watchModel_;
    RowsModel frames_;
    RowsModel bpModel_;
};
