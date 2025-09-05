#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFileSystemWatcher>
#include <QTcpSocket>
#include <QQueue>
#include <QMap>
#include <QElapsedTimer>
#include <QImage>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

// 定义文件状态枚举
enum FileStatus {
    Pending,
    Success,
    Failure
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_pushButton_clicked();
    void on_stopButton_clicked();
    void on_browseButton_clicked();
    void onLogMessage(const QString &message);
    void updateStatistics();

    void on_sendMessageButton_clicked();
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketError(QAbstractSocket::SocketError socketError);
    void handleMessageTransfer(const QString& message);

    // 新增：主文件夹监控槽函数
    void onMainDirectoryChanged(const QString &path);
    // 新增：子文件夹监控槽函数
    void onSubdirectoryChanged(const QString &path);

    // 新增：处理 TIF 转换和文件传输的函数
    void processAndTransferFile(const QString &filePath);

private:
    Ui::MainWindow *ui;
    QTcpSocket* m_messageSocket;

    QFileSystemWatcher* m_mainWatcher; // 新增：主文件夹监控器
    QFileSystemWatcher* m_subWatcher;  // 新增：子文件夹监控器

    QQueue<QString> m_pendingFiles; // 文件传输队列
    QMap<QString, FileStatus> m_fileStatus; // 文件状态映射
    QMap<QString, int> m_fileRetries; // 文件重试次数映射

    bool m_isSendingFile = false; // 传输状态标志
    qint64 m_fileSize;
    qint64 m_bytesWrittenTotal;
    QElapsedTimer m_speedTimer;

    // 传输逻辑函数
    void startFileTransfer();

    // 新增：TIF 到 JPG 转换函数
    bool convertTiffToJpg(const QString &inputPath, const QString &outputPath);
};
#endif // MAINWINDOW_H
