#include <QTimer>
#include <QFileDialog>
#include <QElapsedTimer>
#include "image_utils.h"
#include "image_transfer.h"
#include "package_sar_data.h"
#include "AuxFileReader.h"
#include <QFileInfo>
#include <QDebug>
#include <QFileInfo>
#include <QFile>
#include <QDebug>
#include <QBuffer>
#include <QCoreApplication>

ImageTransferResult processAndTransferImage(const QString &filePath, const QString &ipAddress, quint16 port)
{
    ImageTransferResult result;
    result.success = false;

    const int MAX_AUX_RETRIES = 10;
    const int AUX_RETRY_DELAY_MS = 500;

    if (QFileInfo(filePath).suffix().toLower() != "tif") {
        result.message = QString("File %1 is not TIF, skip.").arg(filePath);
        qDebug() << result.message;
        return result;
    }

    if (!waitForFileRelease(filePath)) {
        result.message = QString("File %1 is locked for too long, give up processing.").arg(filePath);
        qDebug() << result.message;
        return result;
    }

    static QMap<QString, int> auxFileRetries;
    int currentRetryCount = auxFileRetries.value(filePath, 0);

    if (currentRetryCount >= MAX_AUX_RETRIES) {
        result.message = QString("Maximum retries reached for AUX file for TIF %1. Giving up.").arg(filePath);
        qDebug() << result.message;
        auxFileRetries.remove(filePath); // Clean up the retry count for this file
        return result;
    }

    QFileInfo fileInfo(filePath);
    QString sourceDir = fileInfo.absolutePath();
    QString jpgDir = sourceDir + "/jpg";
    QString jpgPath = jpgDir + "/" + fileInfo.baseName() + ".jpg";

    QDir dir(jpgDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    if (convertTiffToJpg(filePath, jpgPath)) {
        QString auxPath = sourceDir + "/" + fileInfo.baseName() + ".dat";
        QFileInfo auxInfo(auxPath);

        if (auxInfo.exists() && auxInfo.isFile()) {
            qDebug() << "Found AUX file. Starting transfer.";
            auxFileRetries.remove(filePath); // Remove from retry list
            if (sendImage(jpgPath, auxPath, ipAddress, port)) {
                result.success = true;
                result.message = QString("Package and send started successfully: %1 + %2").arg(jpgPath, auxPath);
            } else {
                result.message = QString("Package or send failed to start: %1 + %2").arg(jpgPath, auxPath);
            }
        } else {
            qDebug() << QString("No matching AUX file for TIF %1, waiting... (Attempt %2/%3)").arg(filePath).arg(currentRetryCount + 1).arg(MAX_AUX_RETRIES);
            auxFileRetries[filePath] = currentRetryCount + 1;
            QTimer::singleShot(AUX_RETRY_DELAY_MS, QCoreApplication::instance(), [=]() {
                processAndTransferImage(filePath, ipAddress, port);
            });
            result.message = QString("No matching AUX file, waiting: %1").arg(auxPath);
        }
    } else {
        result.message = QString("TIF file %1 convert failed, abandon transfer.").arg(filePath);
    }
    qDebug() << result.message;
    return result;
}

bool sendImage(const QString& imagePath, const QString& auxPath, const QString& ip, quint16 port) {
    // 1. 读取图像文件内容到 QByteArray
    QFile imageFile(imagePath);
    if (!imageFile.open(QIODevice::ReadOnly)) {
        qCritical() << "Failed to open image file:" << imagePath;
        return false;
    }
    QByteArray imageData = imageFile.readAll();

    // 2. 读取 AUX 文件并填充 AuxHeader
    AuxFileReader auxReader;
    if (!auxReader.read(auxPath)) {
        qCritical() << "Failed to open aux file:" << auxPath;
        return false;
    }
    AuxHeader auxHeader = auxReader.getHeader();

    // 3. 封装 SAR_DataInfo
    SAR_DataInfo dataInfo = createSarDataInfo(auxHeader);

    // 4. 将 QByteArray 转换为 std::vector<uint8_t>
    std::vector<uint8_t> imageDataVec(imageData.begin(), imageData.end());

    // 5. 使用 SarPacketizer 类来生成所有数据包
    SarPacketizer* packetizer = new SarPacketizer(dataInfo, imageDataVec, 1);
    qDebug() << "Generated" << packetizer->getTotalPackets() << "packets.";

    // 6. 创建新的 SarPacketTransferManager 并启动传输
    SarPacketTransferManager* transferManager = new SarPacketTransferManager(packetizer);
    QObject::connect(transferManager, &SarPacketTransferManager::finished, transferManager, [transferManager, packetizer](bool success) {
        qDebug() << "Transfer finished with success:" << success;
        delete packetizer;
        transferManager->deleteLater();
    });
    transferManager->startTransfer(ip, port);

    return true;
}

/**
 * @brief SarPacketTransferManager的构造函数
 * @param packetizer 负责提供数据包的打包器实例
 * @param parent 父QObject，用于自动内存管理
 */
SarPacketTransferManager::SarPacketTransferManager(SarPacketizer* packetizer, QObject* parent)
    : QObject(parent),
    m_packetizer(packetizer),
    m_socket(new QTcpSocket(this)), // m_socket作为SarPacketTransferManager的子对象，当父对象销毁时自动销毁
    m_currentPacketIndex(0)
{
    // 连接套接字的信号到对应的槽函数
    connect(m_socket, &QTcpSocket::connected, this, &SarPacketTransferManager::onSocketConnected);
    connect(m_socket, &QTcpSocket::bytesWritten, this, &SarPacketTransferManager::onBytesWritten);
    connect(m_socket, &QTcpSocket::disconnected, this, &SarPacketTransferManager::onSocketDisconnected);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred), this, &SarPacketTransferManager::onSocketError);
}

/**
 * @brief 启动数据传输
 * @param ip 目标主机的IP地址
 * @param port 目标主机的端口号
 */
void SarPacketTransferManager::startTransfer(const QString& ip, quint16 port)
{
    m_ip = ip;
    m_port = port;
    qDebug() << "Connecting to host:" << m_ip << "on port" << m_port;
    m_socket->connectToHost(m_ip, m_port);
}

/**
 * @brief 套接字成功连接时的槽函数
 * 启动第一个数据包的发送
 */
void SarPacketTransferManager::onSocketConnected()
{
    qDebug() << "Successfully connected to host. Starting packet transfer.";
    sendNextPacket();
}

/**
 * @brief 写入字节后的槽函数
 * 继续发送下一个数据包，直到所有包都发送完毕
 * @param bytes 已经写入套接字的字节数
 */
void SarPacketTransferManager::onBytesWritten(qint64 bytes)
{
    Q_UNUSED(bytes);
    sendNextPacket();
}

/**
 * @brief 套接字断开连接时的槽函数
 * 告知外部传输已完成
 */
void SarPacketTransferManager::onSocketDisconnected()
{
    qDebug() << "Disconnected from host.";
    // 通常在所有数据发送完毕后，我们期望断开连接，所以这里可以认为是成功
    emit finished(true);
}

/**
 * @brief 套接字发生错误时的槽函数
 * 告知外部传输失败
 * @param socketError 发生的错误类型
 */
void SarPacketTransferManager::onSocketError(QAbstractSocket::SocketError socketError)
{
    qWarning() << "Socket error:" << m_socket->errorString() << "Error code:" << socketError;
    emit finished(false);
}

/**
 * @brief 发送下一个数据包的私有辅助函数
 * 检查是否有更多数据包并写入套接字
 */
void SarPacketTransferManager::sendNextPacket()
{
    if (m_packetizer->hasNextPacket()) {
        std::vector<uint8_t> packetDataVec = m_packetizer->getNextPacket();
        QByteArray packetData(reinterpret_cast<const char*>(packetDataVec.data()), packetDataVec.size());

        qint64 bytesWritten = m_socket->write(packetData);
        if (bytesWritten == -1) {
            qWarning() << "Failed to write packet to socket:" << m_socket->errorString();
            emit finished(false);
            return;
        }
        qDebug() << "Sent packet" << m_currentPacketIndex + 1 << "of" << m_packetizer->getTotalPackets();
        m_currentPacketIndex++;
    } else {
        qDebug() << "All packets sent successfully. Disconnecting.";
        m_socket->disconnectFromHost();
    }
}
