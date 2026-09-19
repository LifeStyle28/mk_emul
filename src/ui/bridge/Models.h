#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QFileSystemWatcher>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QUrl>
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
    Q_PROPERTY(bool button READ button WRITE setButton NOTIFY buttonChanged)
    Q_PROPERTY(int analog READ analog WRITE setAnalog NOTIFY analogChanged)
public:
    explicit BoardModel(QObject *parent = nullptr);

    bool led0() const { return leds_ & 1; }
    bool led1() const { return leds_ & 2; }
    bool led2() const { return leds_ & 4; }
    bool led3() const { return leds_ & 8; }
    bool button() const { return button_; }
    int analog() const { return analog_; }

    void setLeds(quint32 mask);
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
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
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
    bool dirty() const;

    void setCurrentContent(const QString &text);
    void loadDefaultProject(const QString &examplesDir);
    QVector<QPair<QString, QString>> files() const;

    Q_INVOKABLE void openFolder(const QUrl &url);
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
};
