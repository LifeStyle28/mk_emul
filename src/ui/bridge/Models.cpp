#include "ui/bridge/Models.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <QUrl>

RegisterModel::RegisterModel(QObject *parent)
    : QAbstractListModel(parent) {}

int RegisterModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : rows_.size();
}

QVariant RegisterModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size())
        return {};
    if (role == NameRole)
        return rows_[index.row()].first;
    if (role == ValueRole)
        return rows_[index.row()].second;
    return {};
}

QHash<int, QByteArray> RegisterModel::roleNames() const {
    return {{NameRole, "name"}, {ValueRole, "value"}};
}

void RegisterModel::update(const QVector<QPair<QString, QString>> &rows) {
    if (rows_.size() != rows.size()) {
        beginResetModel();
        rows_ = rows;
        endResetModel();
        return;
    }
    rows_ = rows;
    emit dataChanged(index(0), index(rows_.size() - 1));
}

MemoryModel::MemoryModel(QObject *parent)
    : QAbstractListModel(parent)
    , data_(256, 0) {}

int MemoryModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : 16;
}

QVariant MemoryModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid())
        return {};
    const int row = index.row();
    const quint32 addr = address_ + static_cast<quint32>(row * 16);
    if (role == AddrRole)
        return QStringLiteral("0x") + QString("%1").arg(addr, 8, 16, QChar('0')).toUpper();
    QString hex;
    QString ascii;
    for (int i = 0; i < 16; ++i) {
        const int off = row * 16 + i;
        const auto b = static_cast<unsigned char>(off < data_.size() ? data_[off] : 0);
        hex += QString("%1 ").arg(b, 2, 16, QChar('0')).toUpper();
        ascii += (b >= 32 && b < 127) ? QChar(b) : QChar('.');
    }
    if (role == HexRole)
        return hex.trimmed();
    if (role == AsciiRole)
        return ascii;
    return {};
}

QHash<int, QByteArray> MemoryModel::roleNames() const {
    return {{AddrRole, "addr"}, {HexRole, "hex"}, {AsciiRole, "ascii"}};
}

void MemoryModel::setAddress(quint32 addr) {
    addr &= ~0xFu;
    if (addr == address_)
        return;
    address_ = addr;
    emit addressChanged();
    emit dataChanged(index(0), index(15));
}

void MemoryModel::setBytes(quint32 addr, const QByteArray &data) {
    address_ = addr & ~0xFu;
    data_ = data;
    emit addressChanged();
    beginResetModel();
    endResetModel();
}

DisasmModel::DisasmModel(QObject *parent)
    : QAbstractListModel(parent) {}

int DisasmModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : rows_.size();
}

QVariant DisasmModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size())
        return {};
    const auto &r = rows_[index.row()];
    switch (role) {
    case AddrRole:
        return QStringLiteral("0x") + QString("%1").arg(r.addr, 8, 16, QChar('0')).toUpper();
    case BytesRole:
        return r.bytes;
    case TextRole:
        return r.text;
    case CurrentRole:
        return r.current;
    case BreakRole:
        return r.breakpoint;
    default:
        return {};
    }
}

QHash<int, QByteArray> DisasmModel::roleNames() const {
    return {{AddrRole, "addr"}, {BytesRole, "bytes"}, {TextRole, "text"}, {CurrentRole, "current"}, {BreakRole, "isBreak"}};
}

void DisasmModel::setRows(QVector<Row> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

PeriphModel::PeriphModel(QObject *parent)
    : QAbstractListModel(parent) {}

int PeriphModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : rows_.size();
}

QVariant PeriphModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size())
        return {};
    if (role == NameRole)
        return rows_[index.row()].first;
    if (role == ValueRole)
        return rows_[index.row()].second;
    return {};
}

QHash<int, QByteArray> PeriphModel::roleNames() const {
    return {{NameRole, "name"}, {ValueRole, "value"}};
}

void PeriphModel::update(const QVector<QPair<QString, QString>> &rows) {
    if (rows_.size() != rows.size()) {
        beginResetModel();
        rows_ = rows;
        endResetModel();
        return;
    }
    rows_ = rows;
    if (!rows_.isEmpty())
        emit dataChanged(index(0), index(rows_.size() - 1));
}

BoardModel::BoardModel(QObject *parent)
    : QObject(parent) {}

void BoardModel::setLeds(quint32 mask) {
    if (leds_ == mask)
        return;
    leds_ = mask;
    emit ledsChanged();
}

void BoardModel::setButton(bool pressed) {
    if (button_ == pressed)
        return;
    button_ = pressed;
    emit buttonChanged();
    emit buttonPressed(pressed);
}

void BoardModel::setAnalog(int value) {
    value = qBound(0, value, 4095);
    if (analog_ == value)
        return;
    analog_ = value;
    emit analogChanged();
    emit analogMoved(value);
}

UartModel::UartModel(QObject *parent)
    : QObject(parent) {}

void UartModel::send(const QString &line) {
    emit sendRequested(line + "\r");
}

void UartModel::clear() {
    text_.clear();
    emit textChanged();
}

void UartModel::appendBytes(const QByteArray &bytes) {
    if (bytes.isEmpty())
        return;
    text_ += QString::fromLatin1(bytes);
    if (text_.size() > 80000)
        text_ = text_.right(60000);
    emit textChanged();
}

ProjectModel::ProjectModel(QObject *parent)
    : QAbstractListModel(parent)
    , watcher_(new QFileSystemWatcher(this))
    , saveTimer_(new QTimer(this)) {
    saveTimer_->setSingleShot(true);
    connect(saveTimer_, &QTimer::timeout, this, &ProjectModel::flushSaves);
    connect(watcher_, &QFileSystemWatcher::fileChanged, this, &ProjectModel::reloadFromDisk);
    files_.push_back({QString(), QStringLiteral("main.c"),
                      QStringLiteral("// STM32F407\nint main(void) { for(;;) {} }\n"), false});
}

int ProjectModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : files_.size();
}

QVariant ProjectModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= files_.size())
        return {};
    const auto &f = files_[index.row()];
    if (role == NameRole)
        return f.name;
    if (role == ContentRole)
        return f.content;
    if (role == DirtyRole)
        return f.dirty;
    if (role == PathRole)
        return f.path;
    return {};
}

QHash<int, QByteArray> ProjectModel::roleNames() const {
    return {{NameRole, "name"}, {ContentRole, "content"}, {DirtyRole, "dirty"}, {PathRole, "path"}};
}

void ProjectModel::setCurrentIndex(int idx) {
    if (idx < 0 || idx >= files_.size() || idx == current_)
        return;
    current_ = idx;
    emit currentIndexChanged();
    emit currentContentChanged();
}

QString ProjectModel::currentContent() const {
    if (current_ < 0 || current_ >= files_.size())
        return {};
    return files_[current_].content;
}

QString ProjectModel::currentName() const {
    if (current_ < 0 || current_ >= files_.size())
        return {};
    return files_[current_].name;
}

bool ProjectModel::dirty() const {
    for (const auto &f : files_) {
        if (f.dirty)
            return true;
    }
    return false;
}

void ProjectModel::setCurrentContent(const QString &text) {
    if (current_ < 0 || current_ >= files_.size())
        return;
    if (files_[current_].content == text)
        return;
    files_[current_].content = text;
    files_[current_].dirty = true;
    emit dataChanged(index(current_), index(current_));
    emit dirtyChanged();
    saveTimer_->start(200);
}

QVector<QPair<QString, QString>> ProjectModel::files() const {
    QVector<QPair<QString, QString>> out;
    out.reserve(files_.size());
    for (const auto &f : files_)
        out.push_back({f.name, f.content});
    return out;
}

void ProjectModel::loadDefaultProject(const QString &examplesDir) {
    const QString root = QDir(examplesDir + "/blinky").absolutePath();
    if (QDir(root).exists())
        loadFromDirectory(root);
}

void ProjectModel::openFolder(const QUrl &url) {
    const QString root = QDir(url.toLocalFile()).absolutePath();
    if (root.isEmpty() || !QDir(root).exists())
        return;
    loadFromDirectory(root);
}

void ProjectModel::collectFiles(const QString &dir, const QString &root, QStringList *out) const {
    if (out->size() > 200)
        return;
    QDir d(dir);
    const auto skip = QStringList{"build", "project_build", ".git", "cmake-build", "third_party", "node_modules",
                                  "_deps", ".vs"};
    for (const auto &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (skip.contains(sub, Qt::CaseInsensitive))
            continue;
        collectFiles(d.filePath(sub), root, out);
    }
    const QStringList filters = {"*.c", "*.h", "*.hpp", "*.s", "*.S", "*.inc", "*.ld", "*.txt"};
    for (const auto &n : d.entryList(filters, QDir::Files, QDir::Name))
        out->push_back(QDir(root).relativeFilePath(d.filePath(n)));
    for (const auto &n : d.entryList(QDir::Files, QDir::Name)) {
        if (!n.contains(QLatin1Char('.')))
            out->push_back(QDir(root).relativeFilePath(d.filePath(n)));
    }
}

void ProjectModel::loadFromDirectory(const QString &root) {
    QStringList rel;
    collectFiles(root, root, &rel);
    beginResetModel();
    files_.clear();
    const auto watched = watcher_->files();
    if (!watched.isEmpty())
        watcher_->removePaths(watched);
    for (const QString &name : rel) {
        const QString path = QDir(root).filePath(name);
        QFile f(path);
        QString content;
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            content = QString::fromUtf8(f.readAll());
        files_.push_back({QFileInfo(path).absoluteFilePath(), QDir::fromNativeSeparators(name), content, false});
    }
    if (files_.isEmpty()) {
        files_.push_back({QString(), QStringLiteral("main.c"),
                          QStringLiteral("// empty project\nint main(void) { for(;;) {} }\n"), true});
    }
    current_ = 0;
    projectPath_ = root;
    endResetModel();
    watchAll();
    emit projectPathChanged();
    emit currentIndexChanged();
    emit currentContentChanged();
    emit dirtyChanged();
}

void ProjectModel::watchAll() {
    QStringList paths;
    for (const auto &f : files_) {
        if (!f.path.isEmpty())
            paths << f.path;
    }
    if (!paths.isEmpty())
        watcher_->addPaths(paths);
}

bool ProjectModel::addFile(const QString &relativeName) {
    QString name = sanitizedRelativeName(relativeName);
    if (name.isEmpty() || projectPath_.isEmpty())
        return false;
    const QString path = QFileInfo(QDir(projectPath_).filePath(name)).absoluteFilePath();
    const int existing = indexOfPath(path);
    if (existing >= 0) {
        setCurrentIndex(existing);
        return true;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!QFile::exists(path)) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        f.write("\n");
    }
    QFile in(path);
    QString content;
    if (in.open(QIODevice::ReadOnly | QIODevice::Text))
        content = QString::fromUtf8(in.readAll());
    const int row = files_.size();
    beginInsertRows({}, row, row);
    files_.push_back({path, QDir(projectPath_).relativeFilePath(path), content, false});
    files_.back().name = QDir::fromNativeSeparators(files_.back().name);
    endInsertRows();
    watcher_->addPath(path);
    setCurrentIndex(row);
    return true;
}

QString ProjectModel::sanitizedRelativeName(const QString &name) const {
    QString n = QDir::fromNativeSeparators(name.trimmed());
    n.replace('\\', '/');
    if (n.isEmpty() || n.contains("..") || n.startsWith('/'))
        return {};
    return n;
}

bool ProjectModel::removeFile(int index) {
    if (index < 0 || index >= files_.size())
        return false;
    saveAll();
    const QString path = files_[index].path;
    if (!path.isEmpty()) {
        watcher_->removePath(path);
        if (QFile::exists(path) && !QFile::remove(path))
            return false;
    }
    beginRemoveRows({}, index, index);
    files_.removeAt(index);
    endRemoveRows();
    if (files_.isEmpty())
        current_ = -1;
    else if (current_ > index)
        --current_;
    else if (current_ == index)
        current_ = qMin(index, files_.size() - 1);
    emit currentIndexChanged();
    emit currentContentChanged();
    emit dirtyChanged();
    return true;
}

QString ProjectModel::nameAt(int index) const {
    if (index < 0 || index >= files_.size())
        return {};
    return files_[index].name;
}

void ProjectModel::revealInExplorer(int index) {
    if (index < 0 || index >= files_.size())
        return;
    const QString path = files_[index].path;
    if (path.isEmpty()) {
        if (!projectPath_.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(projectPath_));
        return;
    }
#ifdef Q_OS_WIN
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

bool ProjectModel::renameFile(int index, const QString &newName) {
    if (index < 0 || index >= files_.size() || projectPath_.isEmpty())
        return false;
    saveAll();
    auto &f = files_[index];
    QString want = sanitizedRelativeName(newName);
    if (want.isEmpty())
        return false;
    if (f.path.isEmpty()) {
        f.name = want.contains('/') ? QFileInfo(want).fileName() : want;
        emit dataChanged(this->index(index), this->index(index));
        if (index == current_)
            emit currentIndexChanged();
        return true;
    }
    if (!want.contains('/')) {
        const QString dir = QFileInfo(f.path).absolutePath();
        want = QDir::fromNativeSeparators(QDir(projectPath_).relativeFilePath(QDir(dir).filePath(want)));
    }
    const QString dest = QFileInfo(QDir(projectPath_).filePath(want)).absoluteFilePath();
    if (QFileInfo(f.path).absoluteFilePath() == dest)
        return true;
    if (QFile::exists(dest))
        return false;
    QDir().mkpath(QFileInfo(dest).absolutePath());
    if (!f.path.isEmpty()) {
        watcher_->removePath(f.path);
        if (QFile::exists(f.path) && !QFile::rename(f.path, dest)) {
            watcher_->addPath(f.path);
            return false;
        }
    }
    f.path = dest;
    f.name = QDir::fromNativeSeparators(QDir(projectPath_).relativeFilePath(dest));
    watcher_->addPath(dest);
    emit dataChanged(this->index(index), this->index(index));
    if (index == current_)
        emit currentIndexChanged();
    return true;
}

bool ProjectModel::duplicateFile(int index) {
    if (index < 0 || index >= files_.size() || projectPath_.isEmpty())
        return false;
    saveAll();
    const auto &src = files_[index];
    const QFileInfo info(src.path.isEmpty() ? src.name : src.path);
    QString stem = info.completeBaseName();
    if (stem.isEmpty())
        stem = info.fileName();
    const QString ext = info.suffix();
    const QString dir = info.absolutePath().isEmpty() ? projectPath_ : info.absolutePath();
    QString dest;
    for (int n = 1; n < 100; ++n) {
        const QString extra = n == 1 ? QStringLiteral("_copy") : QStringLiteral("_copy%1").arg(n);
        dest = dir + "/" + stem + extra + (ext.isEmpty() ? QString() : "." + ext);
        if (!QFile::exists(dest))
            break;
    }
    if (QFile::exists(dest))
        return false;
    if (!src.path.isEmpty()) {
        if (!QFile::copy(src.path, dest))
            return false;
    } else {
        QFile out(dest);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        out.write(src.content.toUtf8());
    }
    const QString rel = QDir::fromNativeSeparators(QDir(projectPath_).relativeFilePath(dest));
    return addFile(rel);
}

void ProjectModel::saveAll() {
    saveTimer_->stop();
    flushSaves();
}

void ProjectModel::flushSaves() {
    for (int i = 0; i < files_.size(); ++i) {
        auto &f = files_[i];
        if (!f.dirty || f.path.isEmpty())
            continue;
        QDir().mkpath(QFileInfo(f.path).absolutePath());
        const QString abs = QFileInfo(f.path).absoluteFilePath();
        writing_.insert(abs);
        QFile out(f.path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            writing_.remove(abs);
            continue;
        }
        out.write(f.content.toUtf8());
        out.close();
        f.dirty = false;
        emit dataChanged(index(i), index(i));
        if (!watcher_->files().contains(f.path) && QFile::exists(f.path))
            watcher_->addPath(f.path);
        QTimer::singleShot(100, this, [this, abs]() { writing_.remove(abs); });
    }
    emit dirtyChanged();
}

int ProjectModel::indexOfPath(const QString &path) const {
    const QString abs = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < files_.size(); ++i) {
        if (QFileInfo(files_[i].path).absoluteFilePath() == abs)
            return i;
    }
    return -1;
}

void ProjectModel::reloadFromDisk(const QString &path) {
    const QString abs = QFileInfo(path).absoluteFilePath();
    if (writing_.contains(abs) || writing_.contains(path)) {
        if (!watcher_->files().contains(path) && QFile::exists(path))
            watcher_->addPath(path);
        return;
    }
    const int i = indexOfPath(path);
    if (i < 0)
        return;
    if (!watcher_->files().contains(path) && QFile::exists(path))
        watcher_->addPath(path);
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QString content = QString::fromUtf8(in.readAll());
    if (files_[i].content == content)
        return;
    files_[i].content = content;
    files_[i].dirty = false;
    emit dataChanged(index(i), index(i));
    emit dirtyChanged();
    if (i == current_)
        emit currentContentChanged();
}
