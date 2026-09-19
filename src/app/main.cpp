#include "ui/bridge/EmulatorController.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <QtGlobal>

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("mk_emul"));
    app.setOrganizationName(QStringLiteral("mk_emul"));
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    EmulatorController controller;

    if (argc >= 2 && QString::fromUtf8(argv[1]) == QLatin1String("--headless")) {
        // Worker initializes asynchronously; wait via local event loop in GUI-less mode.
        int steps = 40000;
        if (argc >= 3)
            steps = QString::fromUtf8(argv[2]).toInt();
        QObject::connect(controller.boardModel(), &BoardModel::ledsChanged, &app, [&]() {
            // keep alive
        });
        QTimer timer;
        int n = 0;
        QObject::connect(&timer, &QTimer::timeout, &app, [&]() {
            if (n == 0)
                controller.run();
            ++n;
            if (n > steps / 200) {
                controller.pause();
                const auto leds = (controller.boardModel()->led0() ? 1 : 0) | (controller.boardModel()->led1() ? 2 : 0)
                                  | (controller.boardModel()->led2() ? 4 : 0) | (controller.boardModel()->led3() ? 8 : 0);
                fprintf(stdout, "status=%s leds=0x%X pc=%s\n", qPrintable(controller.status()), leds,
                        qPrintable(controller.pcText()));
                app.quit();
            }
        });
        timer.start(20);
        return app.exec();
    }

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));
    engine.rootContext()->setContextProperty(QStringLiteral("emulator"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("registerModel"), controller.registerModel());
    engine.rootContext()->setContextProperty(QStringLiteral("memoryModel"), controller.memoryModel());
    engine.rootContext()->setContextProperty(QStringLiteral("disasmModel"), controller.disasmModel());
    engine.rootContext()->setContextProperty(QStringLiteral("periphModel"), controller.periphModel());
    engine.rootContext()->setContextProperty(QStringLiteral("boardModel"), controller.boardModel());
    engine.rootContext()->setContextProperty(QStringLiteral("uartModel"), controller.uartModel());
    engine.rootContext()->setContextProperty(QStringLiteral("projectModel"), controller.projectModel());

    const QStringList qmlCandidates = {
        QStringLiteral("qrc:/MkEmul/Main.qml"),
        QStringLiteral("qrc:/qt/qml/MkEmul/Main.qml"),
        QStringLiteral("qrc:/Main.qml"),
    };
    for (const QString &path : qmlCandidates) {
        engine.load(QUrl(path));
        if (!engine.rootObjects().isEmpty())
            break;
    }
    if (engine.rootObjects().isEmpty()) {
        qCritical("Failed to load QML UI from qrc");
        return -1;
    }
    return app.exec();
}
