#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDebug>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFileDialog>
#include <QTimer>
#include "logmanager.h"
#include <QImageReader>
#include <QThread>

// 定义重试常量
const int MAX_RETRIES = 5;
const int RETRY_DELAY_MS = 2000;
QString mainFolderPath = "E:/AIR/小长ISAR/实时数据回传/data";

QString ipAddress = "127.0.0.1";
quint16 port = 65432;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->ipAddressLineEdit->setPlaceholderText("请输入 IP 地址");
    ui->portLineEdit->setPlaceholderText("请输入端口号");
    ui->pathLineEdit->setPlaceholderText("请输入监控文件夹路径"); // ✅ 设置路径编辑框占位符
    ui->pathLineEdit->setText(mainFolderPath); // ✅ 将硬编码路径设为默认值

    connect(&LogManager::instance(), &LogManager::logMessage, this, &MainWindow::onLogMessage);

    // 实例化两个文件系统监视器
    m_mainWatcher = new QFileSystemWatcher(this);
    m_subWatcher = new QFileSystemWatcher(this);

    // 连接主文件夹监视器信号
    connect(m_mainWatcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::onMainDirectoryChanged);
    // 连接子文件夹监视器信号
    connect(m_subWatcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::onSubdirectoryChanged);

    updateStatistics();

    QImageReader::setAllocationLimit(1024);

    qRegisterMetaType<qint64>("qint64");

    // 实例化用于发送消息的 socket
    m_messageSocket = new QTcpSocket(this);
    // 连接消息 socket 的信号到处理函数
    connect(m_messageSocket, &QTcpSocket::connected, this, &MainWindow::onSocketConnected);
    connect(m_messageSocket, &QTcpSocket::disconnected, this, &MainWindow::onSocketDisconnected);
    connect(m_messageSocket, &QTcpSocket::readyRead, this, &MainWindow::onSocketReadyRead);
    connect(m_messageSocket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred), this, &MainWindow::onSocketError);
}

MainWindow::~MainWindow()
{
    delete ui;
}

// 新增：发送消息按钮的槽函数
void MainWindow::on_sendMessageButton_clicked()
{
    QString message = ui->messageLineEdit->text().trimmed(); // 获取并清理消息
    if (!message.isEmpty()) {
        // 如果 socket 未连接，则尝试连接
        if (m_messageSocket->state() == QAbstractSocket::UnconnectedState) {
            m_messageSocket->connectToHost(ipAddress, port);
            m_messageSocket->waitForConnected(3000); // 等待连接建立，超时3秒
        }

        // 确保连接成功后再发送消息
        if (m_messageSocket->state() == QAbstractSocket::ConnectedState) {
            handleMessageTransfer(message);
            ui->messageLineEdit->clear(); // 发送后清空输入框
        } else {
            QMessageBox::warning(this, "警告", "无法连接到服务器，请检查IP和端口。");
            qDebug() << "无法连接到服务器：" << m_messageSocket->errorString();
        }
    }
}

// 新增：处理消息传输的函数
void MainWindow::handleMessageTransfer(const QString& message)
{
    QByteArray data = message.toUtf8();
    // 可以在消息前添加一个特定的标识符，以区分消息和文件
    QByteArray messageHeader = "MSG:";
    m_messageSocket->write(messageHeader + data);
    qDebug() << "发送消息到服务器：" << message;
}

// 新增：处理消息 socket 的信号槽函数
void MainWindow::onSocketConnected()
{
    qDebug() << "消息 Socket 已连接到服务器。";
}

void MainWindow::onSocketDisconnected()
{
    qDebug() << "消息 Socket 已从服务器断开。";
}

void MainWindow::onSocketReadyRead()
{
    // 读取服务器响应
    QByteArray response = m_messageSocket->readAll();
    qDebug() << "收到服务器消息响应：" << QString(response);
}

void MainWindow::onSocketError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError);
    qDebug() << "消息 Socket 发生错误：" << m_messageSocket->errorString();
}

// “开始监控”按钮的槽函数
void MainWindow::on_pushButton_clicked()
{
    ipAddress = ui->ipAddressLineEdit->text();
    port = ui->portLineEdit->text().toUShort();

    if (ipAddress.isEmpty() || port == 0) {
        qDebug() << "请正确填写IP地址与端口号！";
        QMessageBox::warning(this, "警告", "请正确填写IP地址与端口号！");
        return;
    }

    mainFolderPath = ui->pathLineEdit->text();
    if (!QDir(mainFolderPath).exists()) {
        qDebug() << "错误：指定的监控路径不存在：" << mainFolderPath;
        QMessageBox::warning(this, "警告", "指定的监控文件夹不存在。");
        return;
    }

    // 清除旧的监控路径
    m_mainWatcher->removePaths(m_mainWatcher->directories());
    m_subWatcher->removePaths(m_subWatcher->directories());

    // 添加主文件夹监控
    if (!m_mainWatcher->directories().contains(mainFolderPath)) {
        m_mainWatcher->addPath(mainFolderPath);
        qDebug() << "已成功添加主监控路径：" << mainFolderPath;
    }

    // 检查并设置最新的子文件夹进行监控
    QDir mainDir(mainFolderPath);
    QStringList subDirs = mainDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    if (!subDirs.isEmpty()) {
        QString latestSubDir = mainDir.filePath(subDirs.first());
        m_subWatcher->addPath(latestSubDir);
        qDebug() << "已成功添加子监控路径：" << latestSubDir;

        // 立即扫描最新的子文件夹中的所有TIF文件
        QDir subDir(latestSubDir);
        QStringList tifFiles = subDir.entryList(QStringList("*.tif"), QDir::Files | QDir::NoDotAndDotDot);
        for (const QString &fileName : tifFiles) {
            QString filePath = subDir.filePath(fileName);
            if (!m_fileStatus.contains(filePath)) {
                m_fileStatus[filePath] = Pending;
                processAndTransferFile(filePath); // 直接处理并传输
            }
        }
    }
    updateStatistics();
}

// “停止监控”按钮的槽函数
void MainWindow::on_stopButton_clicked()
{
    if (m_mainWatcher->directories().isEmpty() && m_subWatcher->directories().isEmpty()) {
        return;
    }
    qDebug() << "停止监控文件夹...";
    m_mainWatcher->removePaths(m_mainWatcher->directories());
    m_subWatcher->removePaths(m_subWatcher->directories());
}

// ✅ 新增：浏览按钮的槽函数
void MainWindow::on_browseButton_clicked()
{
    QString selectedDir = QFileDialog::getExistingDirectory(this, "选择监控文件夹", ui->pathLineEdit->text());
    if (!selectedDir.isEmpty()) {
        ui->pathLineEdit->setText(selectedDir);
    }
}

// 新增：主文件夹内容变化处理（检测新子文件夹）
void MainWindow::onMainDirectoryChanged(const QString &path)
{
    QDir dir(path);
    // 获取所有子文件夹，按时间倒序排列，以找到最新的
    QStringList subDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    if (!subDirs.isEmpty()) {
        QString latestSubDir = dir.filePath(subDirs.first());

        // 检查是否正在监控该路径，如果没有，则更新监控路径
        if (!m_subWatcher->directories().contains(latestSubDir)) {
            // 先移除旧的监控
            m_subWatcher->removePaths(m_subWatcher->directories());
            // 添加新的子文件夹监控
            m_subWatcher->addPath(latestSubDir);
            qDebug() << "主文件夹检测到新子目录，已切换子监控路径到：" << latestSubDir;
        }
    }
}

// 新增：子文件夹内容变化处理（检测新的TIF文件）
void MainWindow::onSubdirectoryChanged(const QString &path)
{
    QDir dir(path);
    QStringList newFiles = dir.entryList(QStringList("*.tif"), QDir::Files | QDir::NoDotAndDotDot);

    for (const QString &fileName : newFiles) {
        QString filePath = dir.filePath(fileName);
        if (!m_fileStatus.contains(filePath)) {
            m_fileStatus[filePath] = Pending; // 标记为待处理
            qDebug() << "检测到新文件，正在处理：" << filePath;
            processAndTransferFile(filePath);
        }
    }
}

void MainWindow::processAndTransferFile(const QString &filePath)
{
    // 检查文件是否为TIF格式
    if (QFileInfo(filePath).suffix().toLower() != "tif") {
        qDebug() << "文件" << filePath << "不是TIF格式，跳过处理。";
        return;
    }

    // 尝试打开文件，直到成功或达到最大重试次数
    QFile file(filePath);
    int retries = 0;
    while (!file.open(QIODevice::ReadOnly) && retries < 50) {
        qDebug() << "等待文件释放... 重试次数：" << retries + 1;
        file.close();
        QThread::msleep(200); // 等待 200 毫秒
        retries++;
    }
    file.close();

    if (retries >= 50) {
        qDebug() << "文件" << filePath << "长时间被占用，放弃处理。";
        m_fileStatus[filePath] = Failure;
        updateStatistics();
        return;
    }

    // ... 后续的转换逻辑 ...
    QFileInfo fileInfo(filePath);
    QString sourceDir = fileInfo.absolutePath();
    QString jpgDir = sourceDir + "/jpg";
    QString jpgPath = jpgDir + "/" + fileInfo.baseName() + ".jpg";

    if (convertTiffToJpg(filePath, jpgPath)) {
        qDebug() << "TIF文件" << filePath << "已成功转换为JPG并保存到" << jpgPath;
        m_pendingFiles.enqueue(jpgPath);
        startFileTransfer();
    } else {
        qDebug() << "TIF文件" << filePath << "转换失败，放弃传输。";
        m_fileStatus[filePath] = Failure;
        updateStatistics();
    }
}

// 新增：TIF 到 JPG 转换函数
/*
 * @brief 将单个 TIFF 文件转换为 JPG 格式。
 * @param inputPath 源 TIFF 文件的绝对路径。
 * @param outputPath 目标 JPG 文件的绝对路径。
 * @return 转换成功返回 true，否则返回 false。
 */
bool MainWindow::convertTiffToJpg(const QString &inputPath, const QString &outputPath)
{
    // 使用 QFileInfo 检查输入文件是否存在
    QFileInfo fileInfo(inputPath);
    if (!fileInfo.exists()) {
        qDebug() << "源文件不存在:" << inputPath;
        return false;
    }

    QImage image;
    // 尝试加载 TIFF 图像
    if (!image.load(inputPath)) {
        qDebug() << "无法加载图像:" << inputPath << "。请检查文件是否损坏或缺少图像插件。";
        // 你可以进一步检查QImageReader::supportedImageFormats()来调试
        // qDebug() << "支持的图像格式：" << QImageReader::supportedImageFormats();
        return false;
    }

    // 检查目标目录是否存在，如果不存在则创建
    QDir destinationDir(QFileInfo(outputPath).absolutePath());
    if (!destinationDir.exists()) {
        if (!destinationDir.mkpath(".")) {
            qDebug() << "无法创建目标目录:" << destinationDir.absolutePath();
            return false;
        }
    }

    // 尝试将图像保存为 JPG 格式，质量为80
    if (!image.save(outputPath, "JPG", 80)) {
        qDebug() << "保存 JPG 文件失败:" << outputPath;
        return false;
    }

    qDebug() << "成功转换:" << inputPath << " -> " << outputPath;
    return true;
}


// 封装了文件传输和重试逻辑的槽函数
void MainWindow::startFileTransfer()
{
    // 如果正在发送文件或者队列为空，则不执行任何操作
    if (m_isSendingFile || m_pendingFiles.isEmpty()) {
        return;
    }

    m_isSendingFile = true; // 标记正在发送文件
    QString filePath = m_pendingFiles.dequeue(); // 从队列中取出下一个文件

    // 获取当前文件的重试次数
    int currentRetry = m_fileRetries.value(filePath, 0);

    // 检查是否超出最大重试次数
    if (currentRetry >= MAX_RETRIES) {
        qDebug() << "\033[31m文件" << QFileInfo(filePath).fileName() << "传输失败：已达到最大重试次数，放弃传输。\033[0m";
        m_fileRetries.remove(filePath); // 清除重试记录
        m_fileStatus[filePath] = Failure; // 将文件状态标记为失败
        updateStatistics(); // 更新统计数据
        m_isSendingFile = false; // 传输结束
        startFileTransfer(); // 尝试发送下一个文件
        return;
    }

    // 为每个文件创建一个独立的连接
    QTcpSocket* socket = new QTcpSocket(this);
    QFile* file = new QFile(filePath);

    // 尝试打开文件
    if (!file->open(QIODevice::ReadOnly)) {
        m_fileRetries.insert(filePath, currentRetry + 1); // 增加重试次数
        delete file;
        socket->deleteLater();
        m_isSendingFile = false; // 传输结束
        QTimer::singleShot(RETRY_DELAY_MS, [this, filePath](){
            // 将文件重新放回队列，等待下次发送
            m_pendingFiles.enqueue(filePath);
            startFileTransfer();
        });
        return;
    }

    // 记录文件大小和启动计时器
    m_fileSize = file->size();
    m_bytesWrittenTotal = 0;
    m_speedTimer.start();

    // 成功打开文件，设置连接信号
    connect(socket, &QTcpSocket::connected, this, [=]() {
        if (ui->label_currentFile) {
            ui->label_currentFile->setText(QString("正在发送: %1").arg(QFileInfo(filePath).fileName()));
        }

        // 构造并发送文件头
        QByteArray fileNameBytes = QFileInfo(*file).fileName().toUtf8();
        qint32 fileNameLength = fileNameBytes.size();
        qint64 fileSize = file->size();
        QByteArray fileSizeHeader = QString("%1").arg(fileSize, 16, 10, QChar(' ')).toUtf8();

        QByteArray outputBlock;
        outputBlock.append(reinterpret_cast<const char*>(&fileNameLength), sizeof(qint32));
        outputBlock.append(fileNameBytes);
        outputBlock.append(fileSizeHeader);

        socket->write(outputBlock);
    });

    // 新增：更新进度条和速度
    connect(socket, &QTcpSocket::bytesWritten, this, [=](qint64 bytes) {
        m_bytesWrittenTotal += bytes;
        qint64 totalBytes = m_fileSize;

        if (totalBytes > 0) {
            int percentage = (static_cast<double>(m_bytesWrittenTotal) / totalBytes) * 100;
            if (ui->progressBar) {
                ui->progressBar->setValue(percentage);
            }

            double elapsedTime = m_speedTimer.elapsed() / 1000.0;
            if (elapsedTime > 0) {
                double speed = m_bytesWrittenTotal / (1024.0 * 1024.0 * elapsedTime);
                if (ui->label_speed) {
                    ui->label_speed->setText(QString("%1 MB/s").arg(speed, 0, 'f', 2));
                }
            }
        }

        if (file->pos() < totalBytes) {
            qint64 bytesToWrite = totalBytes - file->pos();
            qint64 chunk = qMin(bytesToWrite, (qint64)64 * 1024);
            QByteArray outputBlock = file->read(chunk);
            socket->write(outputBlock);
        }
    });

    connect(socket, &QTcpSocket::readyRead, this, [=]() {
        QByteArray response = socket->readAll();
        if (response == "SUCCESS") {
            qDebug() << "\033[32m服务器确认文件" << file->fileName() << "接收成功。\033[0m";
            m_fileRetries.remove(filePath); // 成功后清除重试记录
            m_fileStatus[filePath] = Success; // 将文件状态标记为成功
            updateStatistics(); // 更新统计数据
        } else if (response == "FAILURE") {
            qDebug() << "\033[31m服务器返回失败，文件" << file->fileName() << "未成功接收。\033[0m";
            m_fileStatus[filePath] = Failure; // 将文件状态标记为失败
            updateStatistics();
        } else {
            qDebug() << "接收到未知服务器响应：" << response;
        }
        // 无论成功与否，收到服务器响应后都断开连接
        socket->disconnectFromHost();
    });

    // 处理连接错误并加入重试机制
    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred), this, [=](QAbstractSocket::SocketError socketError) {
        Q_UNUSED(socketError);
        // 如果是连接拒绝，并且没有达到最大重试次数，则进行重试
        if (socketError == QAbstractSocket::ConnectionRefusedError && m_fileRetries.value(filePath, 0) < MAX_RETRIES) {
            m_fileRetries.insert(filePath, m_fileRetries.value(filePath, 0) + 1); // 增加重试次数
            file->close();
            delete file;
            socket->deleteLater();
            m_isSendingFile = false; // 传输结束
            QTimer::singleShot(RETRY_DELAY_MS, [this, filePath](){
                // 将文件重新放回队列，等待下次发送
                m_pendingFiles.enqueue(filePath);
                startFileTransfer();
            });
        } else {
            qDebug() << "\033[31m连接错误：" << socket->errorString() << "，已达到最大重试次数，放弃传输。\033[0m";
            file->close();
            delete file;
            socket->disconnectFromHost();
        }
    });

    connect(socket, &QTcpSocket::disconnected, this, [=]() {
        // 文件发送完成后，重置进度显示
        if (ui->progressBar) {
            ui->progressBar->setValue(0);
        }
        if (ui->label_currentFile) {
            ui->label_currentFile->setText("无文件发送");
        }
        if (ui->label_speed) {
            ui->label_speed->setText("0.00 MB/s");
        }
        delete file;
        socket->deleteLater();
        m_isSendingFile = false; // 传输结束
        startFileTransfer(); // 尝试发送下一个文件
    });

    // 修正：确保客户端连接的端口与服务器监听的端口一致
    socket->connectToHost(ipAddress, port);
}

// 接收日志消息的槽函数
void MainWindow::onLogMessage(const QString &message)
{
    if (ui->textEdit_Log) {
        ui->textEdit_Log->append(message);
    }
}

// 更新统计标签的槽函数
void MainWindow::updateStatistics()
{
    int totalFiles = m_fileStatus.size();
    int successFiles = 0;
    int failedFiles = 0;

    for (FileStatus status : m_fileStatus.values()) {
        if (status == Success) {
            successFiles++;
        } else if (status == Failure) {
            failedFiles++;
        }
    }

    if (ui->label_total) {
        ui->label_total->setText(QString("总文件数：%1").arg(totalFiles));
    }
    if (ui->label_success) {
        ui->label_success->setText(QString("成功发送：%1").arg(successFiles));
    }
    if (ui->label_failed) {
        ui->label_failed->setText(QString("发送失败：%1").arg(failedFiles));
    }
}
