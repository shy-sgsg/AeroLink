#pragma once

// Qt基础类型
#include <QObject>
#include <QString>
#include <QQueue>
#include <QMap>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QFile>
#include <QTimer>
#include <QDebug>

// 业务通用类型
#include "package_sar_data.h"

// ===================== 业务通用类型 =====================
// 文件状态（主窗口和传输模块共用）
enum FileStatus {
    Pending,
    Success,
    Failure
};

// ===================== 单文件处理接口 =====================
struct ImageTransferResult {
    bool success;
    QString message;
};

// 单文件处理（TIF转JPG、AUX打包、TCP发送）
ImageTransferResult processAndTransferImage(const QString &filePath, const QString &ipAddress, quint16 port);

bool sendImage(const QString& tifPath, const QString& auxPath, const QString& ip, quint16 port);

// ===================== 高级批量传输类 =====================
// 支持信号/槽的批量传输工具
class SarPacketTransferManager : public QObject {
    Q_OBJECT

public:
    explicit SarPacketTransferManager(SarPacketizer* packetizer, QObject* parent = nullptr);
    void startTransfer(const QString& ip, quint16 port);

signals:
    void finished(bool success);

private slots:
    void onSocketConnected();
    void onBytesWritten(qint64 bytes);
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError socketError);

private:
    void sendNextPacket();

private:
    SarPacketizer* m_packetizer;
    QTcpSocket* m_socket;
    QString m_ip;
    quint16 m_port;
    size_t m_currentPacketIndex;
};
