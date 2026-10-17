#include "debug/DebugSession.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>

#include <algorithm>

RowsModel::RowsModel(const QStringList &roles, QObject *parent)
    : QAbstractListModel(parent)
    , roles_(roles) {}

int RowsModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : rows_.size();
}

QVariant RowsModel::data(const QModelIndex &index, int role) const {
    const int r = index.row();
    const int k = role - Qt::UserRole - 1;
    if (!index.isValid() || r >= rows_.size() || k < 0 || k >= roles_.size())
        return {};
    return rows_[r].value(roles_[k]);
}

QHash<int, QByteArray> RowsModel::roleNames() const {
    QHash<int, QByteArray> h;
    for (int i = 0; i < roles_.size(); ++i)
        h.insert(Qt::UserRole + 1 + i, roles_[i].toUtf8());
    return h;
}

void RowsModel::setRows(const QVector<QVariantMap> &rows) {
    if (rows.size() == rows_.size()) {
        for (int i = 0; i < rows.size(); ++i) {
            if (rows[i] != rows_[i]) {
                rows_[i] = rows[i];
                emit dataChanged(index(i), index(i));
            }
        }
        return;
    }
    beginResetModel();
    rows_ = rows;
    endResetModel();
    emit countChanged();
}

namespace {

const QStringList kVarRoles{"name", "value", "type", "depth", "expandable", "expanded", "changed", "header", "error"};

} // namespace

DebugSession::DebugSession(QObject *parent)
    : QObject(parent)
    , variables_(kVarRoles)
    , watchModel_(kVarRoles)
    , frames_({"level", "func", "where", "inProject", "file", "line", "addr"})
    , bpModel_({"label", "state", "error", "file", "line"}) {
    logPath_ = qEnvironmentVariable("MK_EMUL_GDB_LOG");
    gdb_.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&gdb_, &QProcess::readyReadStandardOutput, this, &DebugSession::onOutput);
    connect(&gdb_, &QProcess::readyReadStandardError, this, [this] {
        const QString err = QString::fromUtf8(gdb_.readAllStandardError()).trimmed();
        if (!err.isEmpty())
            setMessage(err.section('\n', -1));
    });
    connect(&gdb_, &QProcess::finished, this, [this] {
        if (state_ != QLatin1String("off")) {
            connected_ = false;
            pending_.clear();
            setState(QStringLiteral("off"));
            setMessage(QStringLiteral("gdb exited"));
        }
    });
}

DebugSession::~DebugSession() {
    blockSignals(true); // the owner is being destroyed too: no notifications from here on
    stop();
}

void DebugSession::setState(const QString &s) {
    if (state_ == s)
        return;
    state_ = s;
    emit stateChanged();
}

void DebugSession::setMessage(const QString &m) {
    if (message_ == m)
        return;
    message_ = m;
    emit messageChanged();
}

QString DebugSession::absSource(const QString &rel) const {
    return sourceRoot_ + "/" + rel;
}

QString DebugSession::toProjectFile(const QString &fullname) const {
    if (sourceRoot_.isEmpty() || fullname.isEmpty())
        return {};
    const QString path = QDir::cleanPath(QDir::fromNativeSeparators(fullname));
    const QString root = sourceRoot_ + "/";
    if (!path.startsWith(root, Qt::CaseInsensitive))
        return {};
    return path.mid(root.size());
}

void DebugSession::command(const QString &cmd, std::function<void(const MiRecord &)> done) {
    if (gdb_.state() != QProcess::Running)
        return;
    const int token = nextToken_++;
    if (done)
        pending_.insert(token, std::move(done));
    const QByteArray line = QByteArray::number(token) + cmd.toUtf8();
    logLine("->", line);
    gdb_.write(line + '\n');
}

void DebugSession::logLine(const char *dir, const QByteArray &line) {
    if (logPath_.isEmpty())
        return;
    QFile f(logPath_);
    if (f.open(QIODevice::Append))
        f.write(QByteArray(dir) + ' ' + line + '\n');
}

void DebugSession::exec(const QString &cmd) {
    command(cmd, [this](const MiRecord &r) {
        if (r.cls == QLatin1String("error"))
            setMessage(r.value.text("msg"));
    });
}

void DebugSession::start(const QString &gdb, const QString &elf, quint16 port, const QString &sourceRoot,
                         const QStringList &skipDirs) {
    stop();
    sourceRoot_ = sourceRoot.isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(sourceRoot));
    setMessage({});
    if (gdb.isEmpty() || !QFileInfo::exists(gdb)) {
        setMessage(QStringLiteral("arm-none-eabi-gdb not found: debugging is off"));
        return;
    }
    ++gen_;
    connected_ = false;
    setState(QStringLiteral("starting"));
    gdb_.start(gdb, {"--interpreter=mi3", "-q", "-nx"});
    if (!gdb_.waitForStarted(5000)) {
        setState(QStringLiteral("off"));
        setMessage("Cannot start gdb: " + gdb_.errorString());
        return;
    }
    command("-gdb-set mi-async on");
    command("-gdb-set confirm off");
    command("-gdb-set width 0");
    // Breakpoints stay in the machine while it is stopped (the disassembly panel shows them).
    command("-gdb-set breakpoint always-inserted on");
    command("-gdb-set remotetimeout 5");
    command(QStringLiteral("-gdb-set output-radix %1").arg(hex_ ? 16 : 10));
    // Step Into stays in the project: library sources (HAL, FreeRTOS) are stepped over.
    for (const QString &dir : skipDirs) {
        QString glob = QDir::cleanPath(QDir::fromNativeSeparators(dir));
        for (int depth = 0; depth < 5; ++depth) {
            glob += "/*";
            command("-interpreter-exec console " + miQuote("skip -gfi " + glob));
        }
    }
    command("-file-exec-and-symbols " + miQuote(QDir::fromNativeSeparators(elf)), [this](const MiRecord &r) {
        if (r.cls == QLatin1String("error"))
            setMessage(r.value.text("msg"));
    });
    // Global and static variables of the project files, for the Variables panel.
    command("-symbol-info-variables", [this](const MiRecord &r) {
        globals_.clear();
        for (const auto &file : r.value["symbols"]["debug"].items) {
            const QString rel = toProjectFile(file.second.text("fullname"));
            if (rel.isEmpty())
                continue;
            const QString base = QFileInfo(file.second.text("filename")).fileName();
            for (const auto &sym : file.second["symbols"].items) {
                const QString name = sym.second.text("name");
                if (name.isEmpty() || name.startsWith("__"))
                    continue;
                globals_.push_back({name, "'" + base + "'::" + name});
            }
        }
        std::sort(globals_.begin(), globals_.end());
    });
    // A gdb that never connects (port taken, hung) must not leave the UI waiting forever.
    const quint64 startGen = gen_;
    QTimer::singleShot(15000, this, [this, startGen] {
        if (!connected_ && state_ == QLatin1String("starting") && gen_ == startGen) {
            stop();
            setMessage(QStringLiteral("gdb did not connect to the emulator within 15 s: debugging is off"));
        }
    });
    command(QStringLiteral("-target-select remote localhost:%1").arg(port), [this](const MiRecord &r) {
        if (r.cls == QLatin1String("error")) {
            setMessage("gdb cannot connect: " + r.value.text("msg"));
            stop();
            return;
        }
        connected_ = true;
        setState(QStringLiteral("stopped"));
        for (int i = 0; i < bps_.size(); ++i)
            insertBreakpoint(i);
        ++gen_;
        refresh();
    });
}

void DebugSession::stop() {
    if (gdb_.state() != QProcess::NotRunning) {
        state_ = QStringLiteral("off"); // the finished handler must not report "gdb exited"
        gdb_.write("-gdb-exit\n");
        if (!gdb_.waitForFinished(1500)) {
            gdb_.kill();
            gdb_.waitForFinished(1000);
        }
    }
    outBuf_.clear();
    pending_.clear();
    afterStop_.clear();
    resumeAfterStop_ = false;
    interruptSent_ = false;
    connected_ = false;
    ++gen_;
    varobjs_.clear();
    vars_.clear();
    watchVars_.clear();
    globals_.clear();
    for (Breakpoint &b : bps_) {
        b.number = -1;
        b.actualLine = 0;
        if (b.state == QLatin1String("ok"))
            b.state = QStringLiteral("pending");
    }
    publishBreakpoints();
    publishVariables();
    frames_.setRows({});
    currentFile_.clear();
    currentLine_ = -1;
    location_.clear();
    emit locationChanged();
    state_ = QStringLiteral("off");
    emit stateChanged();
    emit addressBreakpointsChanged();
}

void DebugSession::targetChanged() {
    if (!connected_ || state_ != QLatin1String("stopped"))
        return; // a running target reports its stop to gdb by itself
    command("-interpreter-exec console " + miQuote("maintenance flush register-cache"));
    ++gen_;
    selectedFrame_ = 0;
    refresh();
}

void DebugSession::onOutput() {
    outBuf_ += gdb_.readAllStandardOutput();
    for (;;) {
        const int nl = outBuf_.indexOf('\n');
        if (nl < 0)
            break;
        logLine("<-", outBuf_.left(nl));
        const QString line = QString::fromUtf8(outBuf_.left(nl));
        outBuf_.remove(0, nl + 1);
        handleRecord(parseMiRecord(line));
    }
}

void DebugSession::handleRecord(const MiRecord &r) {
    switch (r.type) {
    case MiRecord::Result: {
        auto cb = pending_.take(r.token);
        if (cb)
            cb(r);
        else if (r.cls == QLatin1String("error"))
            setMessage(r.value.text("msg"));
        break;
    }
    case MiRecord::Exec:
        if (r.cls == QLatin1String("running")) {
            ++gen_;
            setState(QStringLiteral("running"));
            if (currentLine_ != -1) {
                currentLine_ = -1;
                emit locationChanged();
            }
        } else if (r.cls == QLatin1String("stopped")) {
            onStopped(r);
        }
        break;
    default:
        break;
    }
}

void DebugSession::onStopped(const MiRecord &r) {
    interruptSent_ = false;
    setState(QStringLiteral("stopped"));
    ++gen_;
    if (!afterStop_.isEmpty()) {
        // Stopped only to change breakpoints while running: apply them and continue.
        const auto fns = afterStop_;
        afterStop_.clear();
        for (const auto &fn : fns)
            fn();
        if (resumeAfterStop_) {
            resumeAfterStop_ = false;
            exec("-exec-continue");
            return;
        }
    }
    const QString reason = r.value.text("reason");
    const MiValue &f = r.value["frame"];
    const QString where = f.has("line") ? QFileInfo(f.text("file")).fileName() + ":" + f.text("line") : f.text("addr");
    if (reason == QLatin1String("breakpoint-hit"))
        setMessage("Breakpoint " + r.value.text("bkptno") + " at " + where);
    else if (reason == QLatin1String("end-stepping-range"))
        setMessage("Step: " + where);
    else if (reason == QLatin1String("function-finished"))
        setMessage("Step Out: " + where +
                   (r.value.has("return-value") ? ", returned " + r.value.text("return-value") : QString()));
    else if (reason == QLatin1String("signal-received") && r.value.text("signal-name") == QLatin1String("SIGINT"))
        setMessage("Paused at " + where);
    else
        setMessage("Stopped at " + where);
    selectedFrame_ = 0;
    refresh();
}

void DebugSession::refresh() {
    const quint64 gen = gen_;
    command("-stack-list-frames 0 63", [this, gen](const MiRecord &r) {
        if (gen != gen_)
            return;
        QVector<QVariantMap> rows;
        for (const auto &it : r.value["stack"].items) {
            const MiValue &f = it.second;
            QVariantMap m;
            const QString rel = toProjectFile(f.text("fullname"));
            const QString file = f.has("file") ? QFileInfo(f.text("file")).fileName() : QString();
            m["level"] = f.text("level").toInt();
            m["func"] = f.has("func") ? f.text("func") : QStringLiteral("??");
            m["where"] = f.has("line") ? file + ":" + f.text("line") : f.text("addr");
            m["inProject"] = !rel.isEmpty();
            m["file"] = rel;
            m["line"] = f.has("line") ? f.text("line").toInt() : -1;
            m["addr"] = f.text("addr");
            rows.push_back(m);
        }
        frames_.setRows(rows);
        if (selectedFrame_ >= rows.size())
            selectedFrame_ = 0;
        if (!rows.isEmpty()) {
            const QVariantMap &f = rows[selectedFrame_];
            currentFile_ = f["file"].toString();
            currentLine_ = currentFile_.isEmpty() ? -1 : f["line"].toInt();
            location_ = f["func"].toString() + "() at " + f["where"].toString() +
                        (f["inProject"].toBool() ? QString() : QStringLiteral(" (not in the project)"));
        } else {
            currentFile_.clear();
            currentLine_ = -1;
            location_.clear();
        }
        emit locationChanged();
        command(QStringLiteral("-stack-select-frame %1").arg(selectedFrame_));
        loadVariables(gen);
    });
}

void DebugSession::deleteVarobjs() {
    for (const QString &v : varobjs_)
        command("-var-delete " + v);
    varobjs_.clear();
}

void DebugSession::loadVariables(quint64 gen) {
    deleteVarobjs();
    lastValues_ = values_;
    values_.clear();
    changed_.clear();
    vars_.clear();
    watchVars_.clear();
    varsPending_ = 0;
    const QString frameOpt = QStringLiteral("--thread 1 --frame %1 ").arg(selectedFrame_);
    command("-stack-list-variables " + frameOpt + "--no-values", [this, gen, frameOpt](const MiRecord &r) {
        if (gen != gen_)
            return;
        auto header = [](const QString &title) {
            VarNode h;
            h.header = true;
            h.name = title;
            h.key = "h/" + title;
            return h;
        };
        QStringList locals;
        for (const auto &it : r.value["variables"].items)
            locals << it.second.text("name");
        vars_.push_back(header(locals.isEmpty() ? QStringLiteral("Locals (none)") : QStringLiteral("Locals")));
        QVector<QPair<int, QString>> create;
        for (const QString &name : locals) {
            VarNode n;
            n.name = name;
            n.key = "l/" + name;
            create.push_back({vars_.size(), name});
            vars_.push_back(n);
        }
        if (!globals_.isEmpty()) {
            vars_.push_back(header(QStringLiteral("Globals")));
            for (const auto &g : globals_) {
                VarNode n;
                n.name = g.first;
                n.key = "g/" + g.second;
                create.push_back({vars_.size(), g.second});
                vars_.push_back(n);
            }
        }
        for (const QString &expr : watchExprs_) {
            VarNode n;
            n.name = expr;
            n.key = "w/" + expr;
            watchVars_.push_back(n);
        }
        // All nodes exist before the first reply arrives: the indices stay valid.
        for (const auto &c : create)
            createVar(&vars_, c.first, c.second, gen, false, frameOpt);
        for (int i = 0; i < watchVars_.size(); ++i)
            createVar(&watchVars_, i, watchVars_[i].name, gen, true, frameOpt);
        if (varsPending_ == 0)
            publishVariables();
    });
}

void DebugSession::createVar(QVector<VarNode> *list, int index, const QString &expr, quint64 gen, bool watch,
                             const QString &frameOpt) {
    ++varsPending_;
    command("-var-create " + frameOpt + "- * " + miQuote(expr), [this, list, index, gen, watch](const MiRecord &r) {
        if (gen != gen_) {
            if (r.cls == QLatin1String("done"))
                command("-var-delete " + r.value.text("name"));
            return;
        }
        VarNode &n = (*list)[index];
        if (r.cls == QLatin1String("error")) {
            n.error = true;
            n.value = r.value.text("msg");
        } else {
            n.varobj = r.value.text("name");
            varobjs_ << n.varobj;
            n.value = r.value.text("value");
            n.type = r.value.text("type");
            n.numchild = r.value.text("numchild").toInt();
            if (n.numchild > 0 && expanded_.contains(n.key)) {
                n.expanded = true;
                loadChildren(watch, n.varobj, gen);
            }
        }
        values_[n.key] = n.value;
        if (lastValues_.contains(n.key) && lastValues_[n.key] != n.value)
            changed_.insert(n.key);
        if (--varsPending_ == 0)
            publishVariables();
    });
}

DebugSession::VarNode *DebugSession::findNode(QVector<VarNode> &list, const QString &varobj) {
    for (VarNode &n : list) {
        if (n.varobj == varobj)
            return &n;
        if (VarNode *c = findNode(n.children, varobj))
            return c;
    }
    return nullptr;
}

void DebugSession::loadChildren(bool watch, const QString &varobj, quint64 gen) {
    ++varsPending_;
    command("-var-list-children --all-values " + varobj + " 0 200", [this, watch, varobj, gen](const MiRecord &r) {
        if (gen != gen_)
            return;
        VarNode *n = findNode(watch ? watchVars_ : vars_, varobj);
        if (n && r.cls == QLatin1String("done")) {
            const bool array = n->type.endsWith(']');
            n->children.clear();
            n->loaded = true;
            for (const auto &it : r.value["children"].items) {
                const MiValue &c = it.second;
                VarNode k;
                k.varobj = c.text("name");
                k.name = array ? "[" + c.text("exp") + "]" : c.text("exp");
                k.key = n->key + "." + c.text("exp");
                k.value = c.text("value");
                k.type = c.text("type");
                k.numchild = c.text("numchild").toInt();
                values_[k.key] = k.value;
                if (lastValues_.contains(k.key) && lastValues_[k.key] != k.value)
                    changed_.insert(k.key);
                n->children.push_back(k);
            }
            if (r.value.text("has_more") == QLatin1String("1")) {
                VarNode more;
                more.name = QStringLiteral("…");
                more.value = QStringLiteral("(first 200 elements shown)");
                more.key = n->key + ".…";
                n->children.push_back(more);
            }
            // Re-open what was expanded before this stop. The children vector is complete,
            // so the nodes stay in place until the replies arrive.
            for (VarNode &k : n->children) {
                if (k.numchild > 0 && expanded_.contains(k.key)) {
                    k.expanded = true;
                    loadChildren(watch, k.varobj, gen);
                }
            }
        }
        if (--varsPending_ == 0)
            publishVariables();
    });
}

void DebugSession::flatten(const QVector<VarNode> &list, int depth, bool watch, QVector<QVariantMap> *out) {
    for (const VarNode &n : list) {
        QVariantMap m;
        m["name"] = n.name;
        m["value"] = n.value;
        m["type"] = n.type;
        m["depth"] = depth;
        m["expandable"] = n.numchild > 0;
        m["expanded"] = n.expanded;
        m["changed"] = changed_.contains(n.key);
        m["header"] = n.header;
        m["error"] = n.error;
        m["varobj"] = n.varobj; // not a role: used by toggleExpand / setValue
        out->push_back(m);
        if (n.expanded)
            flatten(n.children, depth + 1, watch, out);
    }
}

void DebugSession::publishVariables() {
    QVector<QVariantMap> v;
    QVector<QVariantMap> w;
    flatten(vars_, 0, false, &v);
    flatten(watchVars_, 0, true, &w);
    variables_.setRows(v);
    watchModel_.setRows(w);
    emit refreshed();
}

void DebugSession::toggleExpand(bool watch, int row) {
    const RowsModel &model = watch ? watchModel_ : variables_;
    if (row < 0 || row >= model.rows().size() || state_ != QLatin1String("stopped"))
        return;
    VarNode *n = findNode(watch ? watchVars_ : vars_, model.rows()[row].value("varobj").toString());
    if (!n || n->numchild == 0 || n->varobj.isEmpty())
        return;
    n->expanded = !n->expanded;
    if (n->expanded)
        expanded_.insert(n->key);
    else
        expanded_.remove(n->key);
    if (n->expanded && !n->loaded)
        loadChildren(watch, n->varobj, gen_);
    else
        publishVariables();
}

void DebugSession::addWatch(const QString &expression) {
    const QString e = expression.trimmed();
    if (e.isEmpty() || watchExprs_.contains(e))
        return;
    watchExprs_ << e;
    if (state_ == QLatin1String("stopped")) {
        ++gen_;
        refresh();
    } else {
        VarNode n;
        n.name = e;
        n.key = "w/" + e;
        n.value = active() ? QStringLiteral("(running)") : QStringLiteral("(no debug session)");
        watchVars_.push_back(n);
        publishVariables();
    }
}

void DebugSession::removeWatch(int row) {
    if (row < 0 || row >= watchModel_.rows().size())
        return;
    // Rows include expanded children: find the top-level expression of this row.
    for (int r = row; r >= 0; --r) {
        if (watchModel_.rows()[r].value("depth").toInt() == 0) {
            watchExprs_.removeAll(watchModel_.rows()[r].value("name").toString());
            break;
        }
    }
    if (state_ == QLatin1String("stopped")) {
        ++gen_;
        refresh();
    } else {
        watchVars_.erase(std::remove_if(watchVars_.begin(), watchVars_.end(),
                                        [this](const VarNode &n) { return !watchExprs_.contains(n.name); }),
                         watchVars_.end());
        publishVariables();
    }
}

void DebugSession::setValue(bool watch, int row, const QString &value) {
    const RowsModel &model = watch ? watchModel_ : variables_;
    if (row < 0 || row >= model.rows().size() || state_ != QLatin1String("stopped"))
        return;
    const QString varobj = model.rows()[row].value("varobj").toString();
    if (varobj.isEmpty() || value.trimmed().isEmpty())
        return;
    command("-var-assign " + varobj + " " + miQuote(value.trimmed()), [this](const MiRecord &r) {
        if (r.cls == QLatin1String("error"))
            setMessage(r.value.text("msg"));
        ++gen_;
        refresh(); // other variables may depend on it
    });
}

void DebugSession::setHex(bool on) {
    if (hex_ == on)
        return;
    hex_ = on;
    emit hexChanged();
    command(QStringLiteral("-gdb-set output-radix %1").arg(on ? 16 : 10));
    if (state_ == QLatin1String("stopped")) {
        values_.clear(); // a different radix is not a changed value
        ++gen_;
        refresh();
    }
}

void DebugSession::selectFrame(int level) {
    if (state_ != QLatin1String("stopped") || level < 0 || level >= frames_.rows().size() || level == selectedFrame_)
        return;
    selectedFrame_ = level;
    lastValues_.clear();
    values_.clear(); // other function: nothing is "changed"
    ++gen_;
    refresh();
}

// ---- run control -------------------------------------------------------------------------

void DebugSession::continueRun() {
    if (state_ == QLatin1String("stopped") || state_ == QLatin1String("starting"))
        exec("-exec-continue");
}

void DebugSession::interrupt() {
    if (state_ == QLatin1String("running") && !interruptSent_) {
        interruptSent_ = true;
        exec("-exec-interrupt");
    }
}

void DebugSession::stepOver() {
    if (state_ == QLatin1String("stopped") || state_ == QLatin1String("starting"))
        exec("-exec-next");
}

void DebugSession::stepInto() {
    if (state_ == QLatin1String("stopped") || state_ == QLatin1String("starting"))
        exec("-exec-step");
}

void DebugSession::stepOut() {
    if (state_ == QLatin1String("stopped"))
        exec("-exec-finish");
}

void DebugSession::stepInstruction() {
    if (state_ == QLatin1String("stopped") || state_ == QLatin1String("starting"))
        exec("-exec-step-instruction");
}

void DebugSession::whenStopped(std::function<void()> fn) {
    if (!connected_)
        return; // inserted when the session connects
    if (state_ == QLatin1String("stopped")) {
        fn();
        return;
    }
    // All-stop remote target: gdb cannot change breakpoints while it runs. Stop, apply, go on.
    afterStop_.push_back(std::move(fn));
    resumeAfterStop_ = true;
    if (!interruptSent_) {
        interruptSent_ = true;
        exec("-exec-interrupt");
    }
}

// ---- breakpoints -------------------------------------------------------------------------

QList<quint32> DebugSession::addressBreakpoints() const {
    QList<quint32> out;
    for (const Breakpoint &b : bps_) {
        if (b.file.isEmpty())
            out << b.address;
    }
    return out;
}

void DebugSession::insertBreakpoint(int index) {
    if (index < 0 || index >= bps_.size())
        return;
    const Breakpoint key = bps_[index];
    const QString cmd = key.file.isEmpty()
                            ? QStringLiteral("-break-insert *0x%1").arg(key.address, 8, 16, QChar('0'))
                            : QStringLiteral("-break-insert --source %1 --line %2").arg(miQuote(absSource(key.file))).arg(key.line);
    command(cmd, [this, key](const MiRecord &r) {
        int i = 0;
        while (i < bps_.size() && !(bps_[i].file == key.file && bps_[i].line == key.line && bps_[i].address == key.address))
            ++i;
        if (i == bps_.size()) {
            // Removed while the insert was on its way.
            if (r.cls == QLatin1String("done"))
                command("-break-delete " + r.value["bkpt"].text("number"));
            return;
        }
        Breakpoint &b = bps_[i];
        if (r.cls == QLatin1String("error")) {
            b.state = QStringLiteral("error");
            b.error = r.value.text("msg");
        } else {
            const MiValue &bk = r.value["bkpt"];
            b.number = bk.text("number").toInt();
            QString line = bk.text("line");
            if (line.isEmpty() && !bk["locations"].items.isEmpty())
                line = bk["locations"].items.first().second.text("line");
            b.actualLine = line.toInt();
            b.state = QStringLiteral("ok");
            b.error.clear();
        }
        publishBreakpoints();
    });
}

void DebugSession::publishBreakpoints() {
    QVector<QVariantMap> rows;
    for (const Breakpoint &b : bps_) {
        QVariantMap m;
        const int line = b.actualLine > 0 ? b.actualLine : b.line;
        m["label"] = b.file.isEmpty() ? QStringLiteral("0x%1").arg(b.address, 8, 16, QChar('0')).toUpper().replace("0X", "0x")
                                      : b.file + ":" + QString::number(line);
        m["state"] = b.state;
        m["error"] = b.error;
        m["file"] = b.file;
        m["line"] = line;
        rows.push_back(m);
    }
    bpModel_.setRows(rows);
    ++bpVersion_;
    emit breakpointsChanged();
}

QVariantList DebugSession::breakpointLines(const QString &file) const {
    QVariantList out;
    for (const Breakpoint &b : bps_) {
        if (b.file != file || b.file.isEmpty())
            continue;
        QVariantMap m;
        m["line"] = b.actualLine > 0 ? b.actualLine : b.line;
        m["state"] = b.state;
        out << m;
    }
    return out;
}

void DebugSession::toggleBreakpoint(const QString &file, int line) {
    if (file.isEmpty() || line <= 0)
        return;
    for (int i = 0; i < bps_.size(); ++i) {
        if (bps_[i].file == file && (bps_[i].line == line || bps_[i].actualLine == line)) {
            removeBreakpoint(i);
            return;
        }
    }
    Breakpoint b;
    b.file = file;
    b.line = line;
    b.state = QStringLiteral("pending");
    bps_.push_back(b);
    publishBreakpoints();
    whenStopped([this, b] { insertBreakpoint(bps_.indexOf(b)); });
}

bool DebugSession::addAddressBreakpoint(const QString &text) {
    bool ok = false;
    const quint32 addr = text.trimmed().toUInt(&ok, 0);
    if (!ok) {
        setMessage("Invalid address: \"" + text.trimmed() + "\" (use hex like 0x08000200)");
        return false;
    }
    for (const Breakpoint &b : bps_) {
        if (b.file.isEmpty() && b.address == (addr & ~1u))
            return true;
    }
    Breakpoint b;
    b.address = addr & ~1u;
    b.state = connected_ ? QStringLiteral("pending") : QStringLiteral("ok");
    bps_.push_back(b);
    publishBreakpoints();
    if (connected_)
        whenStopped([this, b] { insertBreakpoint(bps_.indexOf(b)); });
    else
        emit addressBreakpointsChanged();
    return true;
}

void DebugSession::removeBreakpoint(int index) {
    if (index < 0 || index >= bps_.size())
        return;
    const Breakpoint b = bps_.takeAt(index);
    publishBreakpoints();
    if (connected_ && b.number > 0)
        whenStopped([this, b] { command(QStringLiteral("-break-delete %1").arg(b.number)); });
    if (!connected_ && b.file.isEmpty())
        emit addressBreakpointsChanged();
}

void DebugSession::clearBreakpoints() {
    while (!bps_.isEmpty())
        removeBreakpoint(bps_.size() - 1);
}
