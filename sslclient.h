#ifndef SSLCLIENT_H
#define SSLCLIENT_H

#include <QSslSocket>
#include <QThread>

#define LOGTIME QDateTime::currentDateTime().toString( \
                "[""dd.MM.yyyy hh:mm:ss""]").toStdString().c_str()

class sslclient : public QThread
{
    Q_OBJECT

    QThread *m_thread;

    friend class vixdisklibsamplegui;

    QSslSocket *sslSocket;
    QString m_thumb;
    QString *m_host;
    int m_port;

public:
    sslclient(QObject *parent = 0);
    sslclient(const QString &host, const int &port, QObject *parent = 0);
    ~sslclient();
    void connectToHost();
    QString getThumb();

protected:
    virtual void run(QString &s);

public slots:


private slots:
    void setThumb();
    void socketError(const QAbstractSocket::SocketError &socketError);
    void sslError(const QList<QSslError> &errors);

signals:

};

#endif // SSLCLIENT_H
