#pragma once

#include "core/GdbStub.h"

#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>

// TCP transport for GdbStub on 127.0.0.1 (like "target remote :3333" to OpenOCD). Lives on the
// thread that owns the Machine; one gdb client at a time.
class GdbServer : public QObject {
    Q_OBJECT
public:
    GdbServer(Machine &machine, GdbStub::Target target, QObject *parent = nullptr);

    bool listen(quint16 port = 0); // 0: any free port
    quint16 port() const { return server_.serverPort(); }
    GdbStub &stub() { return stub_; }
    bool hasClient() const { return client_ != nullptr; }

signals:
    void clientChanged(bool connected);

private:
    void onNewConnection();

    QTcpServer server_;
    QPointer<QTcpSocket> client_;
    GdbStub stub_;
};
