#pragma once

#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

class CHighlighter;
class QQuickTextDocument;

// Builds the editor project with arm-none-eabi-gcc against the emulator's device files:
// startup, linker script and system file from deviceDir. A project with stm32f4xx_hal_conf.h
// is built with the bundled STM32CubeF4 HAL (deviceDir/cube), one with FreeRTOSConfig.h with
// the bundled FreeRTOS kernel (deviceDir/freertos). Library objects are cached in workDir/lib
// and rebuilt only when the project headers or flags change.
class ArmGccBuilder : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString toolchainPath READ toolchainPath NOTIFY toolchainPathChanged)
public:
    explicit ArmGccBuilder(QObject *parent = nullptr);

    QString toolchainPath() const { return gccPath_; }
    QString findToolchain() const;
    // Thread-safe apart from toolchainPath: the UI runs it on a worker thread.
    bool compile(const QVector<QPair<QString, QString>> &files, const QString &workDir,
                 const QString &deviceDir, QString *elfOut, QString *log);

    // HAL sources (file names in deviceDir/cube/hal/Src) for the modules enabled in a
    // stm32f4xx_hal_conf.h. Public for tests.
    static QStringList halSourcesFor(const QString &halConf, const QStringList &available);

signals:
    void toolchainPathChanged();

private:
    QString gccPath_;
};
