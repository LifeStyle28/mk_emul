#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QFileSystemWatcher>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVector>

class RegisterModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { NameRole = Qt::UserRole + 1, ValueRole };
    explicit RegisterModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void update(const QVector<QPair<QString, QString>> &rows);

private:
    QVector<QPair<QString, QString>> rows_;
};

class MemoryModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(quint32 address READ address WRITE setAddress NOTIFY addressChanged)
public:
    enum Roles { AddrRole = Qt::UserRole + 1, HexRole, AsciiRole };
    static constexpr int kMemoryBytes = 256; // one requestMemory() window
    // 8, not 16: a 16-byte row (47 hex + 16 ASCII chars) does not fit the Memory panel width,
    // the hex column overflowed under the ASCII one.
    static constexpr int kMemoryBytesPerRow = 8;
    explicit MemoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    quint32 address() const { return address_; }
    void setAddress(quint32 addr);
    void setBytes(quint32 addr, const QByteArray &data);

signals:
    void addressChanged();

private:
    quint32 address_ = 0x20000000;
    QByteArray data_;
};

class DisasmModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { AddrRole = Qt::UserRole + 1, BytesRole, TextRole, CurrentRole, BreakRole };
    explicit DisasmModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    struct Row {
        quint32 addr = 0;
        QString bytes;
        QString text;
        bool current = false;
        bool breakpoint = false;
    };
    void setRows(QVector<Row> rows);

private:
    QVector<Row> rows_;
};

class PeriphModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { NameRole = Qt::UserRole + 1, ValueRole };
    explicit PeriphModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void update(const QVector<QPair<QString, QString>> &rows);

private:
    QVector<QPair<QString, QString>> rows_;
};

class BoardModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool led0 READ led0 NOTIFY ledsChanged)
    Q_PROPERTY(bool led1 READ led1 NOTIFY ledsChanged)
    Q_PROPERTY(bool led2 READ led2 NOTIFY ledsChanged)
    Q_PROPERTY(bool led3 READ led3 NOTIFY ledsChanged)
    // Brightness 0..1 of each LED (PWM duty when driven by TIM4).
    Q_PROPERTY(QVariantList ledLevels READ ledLevels NOTIFY ledsChanged)
    Q_PROPERTY(bool button READ button WRITE setButton NOTIFY buttonChanged)
    Q_PROPERTY(int analog READ analog WRITE setAnalog NOTIFY analogChanged)
public:
    explicit BoardModel(QObject *parent = nullptr);

    bool led0() const { return leds_ & 1; }
    bool led1() const { return leds_ & 2; }
    bool led2() const { return leds_ & 4; }
    bool led3() const { return leds_ & 8; }
    QVariantList ledLevels() const;
    bool button() const { return button_; }
    int analog() const { return analog_; }

    void setLeds(quint32 mask, const float levels[4]);
    void setButton(bool pressed);
    void setAnalog(int value);

signals:
    void ledsChanged();
    void buttonChanged();
    void analogChanged();
    void buttonPressed(bool pressed);
    void analogMoved(int value);

private:
    quint32 leds_ = 0;
    float levels_[4]{};
    bool button_ = false;
    int analog_ = 2048;
};

class UartModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text NOTIFY textChanged)
public:
    explicit UartModel(QObject *parent = nullptr);
    QString text() const { return text_; }
    Q_INVOKABLE void send(const QString &line);
    Q_INVOKABLE void clear();
    void appendBytes(const QByteArray &bytes);

signals:
    void textChanged();
    void sendRequested(const QString &line);

private:
    QString text_;
};

class ProjectModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(QString currentContent READ currentContent WRITE setCurrentContent NOTIFY currentContentChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentIndexChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectPathChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectPathChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(QStringList recentProjects READ recentProjects NOTIFY recentProjectsChanged)
    Q_PROPERTY(QStringList examples READ examples CONSTANT)
public:
    enum Roles { NameRole = Qt::UserRole + 1, ContentRole, DirtyRole, PathRole };
    explicit ProjectModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int currentIndex() const { return current_; }
    void setCurrentIndex(int idx);
    QString currentContent() const;
    QString currentName() const;
    QString projectPath() const { return projectPath_; }
    QString projectName() const;
    bool dirty() const;
    QStringList recentProjects() const { return recent_; }
    QStringList examples() const; // folder names in examples/

    void setCurrentContent(const QString &text);
    // The last opened project (settings), or examples/blinky.
    void loadDefaultProject(const QString &examplesDir);
    QVector<QPair<QString, QString>> files() const;

    Q_INVOKABLE void openFolder(const QUrl &url);
    Q_INVOKABLE void openPath(const QString &path);
    Q_INVOKABLE void openExample(const QString &name);
    // New project in an (empty) folder: main.c with a register-level LED blink.
    Q_INVOKABLE bool newProject(const QUrl &folder);
    Q_INVOKABLE void clearRecentProjects();
    Q_INVOKABLE int indexOfName(const QString &relativeName) const;
    // Programmatic edits (Clock Configuration): the editor shows them, they are saved at once.
    void setFileContent(int index, const QString &text);
    bool writeFile(const QString &relativeName, const QString &text); // creates or overwrites
    Q_INVOKABLE bool addFile(const QString &relativeName);
    Q_INVOKABLE bool removeFile(int index);
    Q_INVOKABLE bool renameFile(int index, const QString &newName);
    Q_INVOKABLE bool duplicateFile(int index);
    Q_INVOKABLE void revealInExplorer(int index);
    Q_INVOKABLE QString nameAt(int index) const;
    Q_INVOKABLE void saveAll();

signals:
    void currentIndexChanged();
    void currentContentChanged();
    void projectPathChanged();
    void dirtyChanged();
    void recentProjectsChanged();

private:
    struct ProjectFile {
        QString path;
        QString name;
        QString content;
        bool dirty = false;
    };

    void loadFromDirectory(const QString &root);
    void collectFiles(const QString &dir, const QString &root, QStringList *out) const;
    void watchAll();
    void flushSaves();
    void reloadFromDisk(const QString &path);
    int indexOfPath(const QString &path) const;
    QString sanitizedRelativeName(const QString &name) const;

    QVector<ProjectFile> files_;
    int current_ = 0;
    QString projectPath_;
    QFileSystemWatcher *watcher_ = nullptr;
    QTimer *saveTimer_ = nullptr;
    QSet<QString> writing_;
    QString examplesDir_;
    QStringList recent_;
};
