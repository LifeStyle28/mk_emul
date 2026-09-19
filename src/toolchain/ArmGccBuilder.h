#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class CHighlighter;
class QQuickTextDocument;

class ArmGccBuilder : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString toolchainPath READ toolchainPath NOTIFY toolchainPathChanged)
public:
    explicit ArmGccBuilder(QObject *parent = nullptr);

    QString toolchainPath() const { return gccPath_; }
    QString findToolchain() const;
    bool compile(const QVector<QPair<QString, QString>> &files, const QString &workDir,
                 const QString &deviceDir, QString *elfOut, QString *log);

signals:
    void toolchainPathChanged();

private:
    QString gccPath_;
};
