#include "sslclient.h"
#include <QRegExpValidator>
#include <QSslConfiguration>

sslclient::sslclient(QObject *parent/* = 0*/)
{
    m_thread = new QThread(this);
    sslSocket = new QSslSocket(this);
    m_host = new QString;
    m_port = 0;

    connect( sslSocket, SIGNAL(encrypted()), this, SLOT(setThumb()) );
    connect( sslSocket, SIGNAL(error(QAbstractSocket::SocketError)),
             this, SLOT(socketError(QAbstractSocket::SocketError)) );
    connect( sslSocket, SIGNAL(sslErrors(const QList<QSslError> &)),
             this, SLOT(sslError(const QList<QSslError> &)) );
}

sslclient::sslclient(const QString &host, const int &port, QObject *parent)
{
    m_thread = new QThread(this);
    sslSocket = new QSslSocket(this);
    m_host = new QString(host);
    m_port = port;

    connect( sslSocket, SIGNAL(encrypted()), this, SLOT(setThumb()) );
    connect( sslSocket, SIGNAL(error(QAbstractSocket::SocketError)),
             this, SLOT(socketError(QAbstractSocket::SocketError)) );
    connect( sslSocket, SIGNAL(sslErrors(const QList<QSslError> &)),
             this, SLOT(sslError(const QList<QSslError> &)) );

    QSslConfiguration conf = sslSocket->sslConfiguration();         //ignore certificate verification
    conf.setPeerVerifyMode(QSslSocket::VerifyNone);
    sslSocket->setSslConfiguration(conf);
}

sslclient::~sslclient()
{
    delete m_host;
}

void sslclient::connectToHost()
{
    sslSocket->connectToHostEncrypted( *m_host, m_port );

    if (sslSocket->waitForConnected(1000)) {                        //timeout for encrypted() signal
        qDebug() << LOGTIME << " "
                 << "Connection to " << *m_host << " established";
    }
    else {
        qDebug() << LOGTIME << " "
                 << "Connection to " << *m_host << " timed out";
    }
    if (sslSocket->waitForEncrypted(1000)) {
        qDebug() << LOGTIME << " "
                 << "SSL handshake with " << *m_host << " completed";
    }
    else {
        qDebug() << LOGTIME << " "
                 << "SSL handshake with " << *m_host << " failed";
    }
}


QString sslclient::getThumb()
{
    return m_thumb;
}

void sslclient::run(QString &s)
{
    m_thread->start();

    connectToHost();
    s = getThumb();

    m_thread->quit();
}

void sslclient::setThumb()
{
    QSslCertificate cert = sslSocket->peerCertificate();
    qDebug() << cert.toPem();

    QByteArray der = cert.toDer();                                 //get binary format of certificate

    QString hashFromDer = QString("%1")
            .arg(QString(QCryptographicHash::hash(der,
                         QCryptographicHash::Sha1).toHex()) );           //get sha1 from certificate

    QRegExp hashReg("^[0-9A-Fa-f]{40}$");                                //regexp to check the hash
    QRegExpValidator hashRegVal(hashReg);

    QString thumb;
    int pos = 0;
    if (hashRegVal.validate(hashFromDer,pos) == QValidator::Acceptable) {
        for (int i = 0; i < hashFromDer.size(); ++i)
        {
            (!i || i%2 == 0 || i == hashFromDer.size()-1) ?
                        QTextStream(&thumb) << hashFromDer[i] :
                        QTextStream(&thumb) << hashFromDer[i] << ":";  //delimit every even hash char
        }                                                              // with ":"
    }
    qDebug() << LOGTIME << " "
             << "Thumbprint: " << thumb;

    m_thumb = thumb;
}

void sslclient::socketError(const QAbstractSocket::SocketError &socketError)
{
    switch (socketError) {
    case QAbstractSocket::RemoteHostClosedError:
        break;
    case QAbstractSocket::HostNotFoundError:
        qDebug() << LOGTIME << " "
                 << "Socket Error: "
                 << "The host " << *m_host
                 << " was not found. Please check the "
                 << "host name and port settings.";
        break;
    case QAbstractSocket::ConnectionRefusedError:
        qDebug()  << LOGTIME << " "
                  << "Socket Error: "
                  << "The connection was refused by the peer. "
                  << "Make sure the server is running, "
                  << "and check that the host name and port "
                  << "settings are correct.";
        break;
    default:
        qDebug() << LOGTIME << " "
                 << "SSL Error: " << sslSocket->errorString();
    }
}

void sslclient::sslError(const QList<QSslError> &errors)
{
    foreach( const QSslError &error, errors ) {
        if(error.error() != QSslError::NoError)
            qDebug() << LOGTIME << " "
                     << "SSL Error: " << error.errorString();
    }
}

