#include "debug/GdbServer.h"

GdbServer::GdbServer(Machine &machine, GdbStub::Target target, QObject *parent)
    : QObject(parent)
    , stub_(
          machine,
          [this](const std::string &bytes) {
              if (client_)
                  client_->write(bytes.data(), static_cast<qint64>(bytes.size()));
          },
          std::move(target)) {
    connect(&server_, &QTcpServer::newConnection, this, &GdbServer::onNewConnection);
}

bool GdbServer::listen(quint16 port) {
    return server_.listen(QHostAddress::LocalHost, port);
}

void GdbServer::onNewConnection() {
    while (QTcpSocket *s = server_.nextPendingConnection()) {
        if (client_) {
            s->close(); // one debugger at a time
            s->deleteLater();
            continue;
        }
        client_ = s;
        s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        stub_.connected();
        connect(s, &QTcpSocket::readyRead, this, [this, s] {
            const QByteArray data = s->readAll();
            stub_.feed(data.constData(), static_cast<size_t>(data.size()));
        });
        connect(s, &QTcpSocket::disconnected, this, [this, s] {
            // gdb went away (new session, gdb killed): a running machine keeps running.
            if (client_ == s)
                client_ = nullptr;
            s->deleteLater();
            emit clientChanged(false);
        });
        emit clientChanged(true);
    }
}
