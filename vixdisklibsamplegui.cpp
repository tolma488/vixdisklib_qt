#include <QDialog>
#include <QFile>
#include <QMessageBox>
#include <QDir>
#include <QDebug>
#include <QSettings>
#include <QSignalMapper>
#include <QFileDialog>
#include "vixdisklibsamplegui.h"
#include "ui_vixdisklibsamplegui.h"
#include "worker.h"
#include "sslclient.h"


vixdisklibsamplegui::vixdisklibsamplegui(QWidget *parent) :
    QMainWindow(parent), ui(new Ui::vixdisklibsamplegui),
    adv(new Ui::Advanced)
{
    m_worker = new worker;

    advanced = new QDialog(0,0);
    ui->setupUi(this);
    adv->setupUi(advanced);
    ui->textEdit->setReadOnly(true);
    QDir dir;
    m_sSettingsFile = dir.absolutePath() + "/config.ini";

    loadSettings();
    ParseArguments();

    //setting validators

    //QRegExp ip("^(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])$");
    //QRegExp dns("^(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\-]*[a-zA-Z0-9])\.)*([A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\-]*[A-Za-z0-9])$");
    QRegExp dns2("^([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\-]{0,61}[a-zA-Z0-9])(\.([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\-]{0,61}[a-zA-Z0-9]))*$");
    QRegExp thumbReg("^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){19}$");
    QRegExpValidator *vDns2 = new QRegExpValidator(dns2, this);
    QRegExpValidator *vThumbReg = new QRegExpValidator(thumbReg, this);

    ui->portEdit->setValidator(new QIntValidator(1, 65535, this));
    adv->nfcPortEdit->setValidator(new QIntValidator(1, 65535, this));

    //ui->hostIPEdit->setValidator(vIp);
    ui->hostIPEdit->setValidator(vDns2);                             //validate host edit field
    ui->thumbEdit->setValidator(vThumbReg);

    ui->passEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);        //hiding the password text

    //TBD - find info how to change default log file name

    adv->lofileEdit->setDisabled(true);
    adv->logfileBrowseButton->setDisabled(true);
    adv->logfileLabel->setDisabled(true);
    ui->shrinkButton->setDisabled(true);
    ui->defragmentButton->setDisabled(true);
    //adv->generateCmdCheck->setDisabled(true);

    connect( adv->buttonBox, SIGNAL(accepted()),
            this, SLOT(on_saveSettingsButton_clicked()) );          //save settings on closing advanced settings window

    connect( adv->buttonBox, SIGNAL(rejected()),
            this, SLOT(on_buttonBox_rejected()) );                  //revert settings on "Cancel" button pressed

    connect( adv->buttonBox, SIGNAL(accepted()),
            this, SLOT(ParseArguments()) );

    connect( adv->libdirBrowseButton, SIGNAL(clicked(bool)),
            this, SLOT(on_libdirBrowseButton_clicked()) );

    connect( adv->tmpdirBrowseButton, SIGNAL(clicked(bool)),
            this, SLOT(on_tmpdirBrowseButton_clicked()) );

    connect( adv->initexCfgBrowseButton, SIGNAL(clicked(bool)),
             this, SLOT(on_initexCfgBrowseButton_clicked()) );

    connect( adv->initexDirEdit, SIGNAL(textEdited(QString)),
            this, SLOT(ParseInitexConfig()) );                      //reparse arguments if initex cfg field was changed

    connect( ui->hostIPEdit, SIGNAL(editingFinished()),
            this, SLOT(getThumbSlot()),Qt::DirectConnection );      //call sslclient::getThumb if hostname was changed

//    connect( m_worker, SIGNAL(signalStdOut(QString text)),
//            this, SLOT(printWorkerOutput(text)) );
}

vixdisklibsamplegui::~vixdisklibsamplegui()
{
    delete ui;
    delete advanced;
    delete adv;
    delete m_worker;
}

void vixdisklibsamplegui::ParseArguments()
{
    ParseInitexConfig();

    m_worker->appGlobals.host = ui->hostIPEdit->text();       //IP of VC/ESXi
    m_worker->appGlobals.thumbPrint = ui->thumbEdit->text();  //provides a SSL thumbprint string for validation.
                                                              // "Format: xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx
    m_worker->appGlobals.userName = ui->userEdit->text();     //user name on host (Mandatory)
    m_worker->appGlobals.password = ui->passEdit->text();     // password on host (Mandatory)
    m_worker->appGlobals.vmxSpec = ui->vmrefEdit->text();     //vmref of the VM
    m_worker->appGlobals.ssMoRef = ui->ssmorefEdit->text();   // Managed object reference of VM snapshot
    m_worker->appGlobals.diskPath = ui->vmdkPathEdit->text(); //path to VMDK
    m_worker->appGlobals.port = ui->portEdit->text().toInt(); //port to use to connect to VC/ESXi host (default = 443)

    switch (adv->modeCombo->currentIndex()) {                 //mode string to pass into VixDiskLib_ConnectEx
    case 0:
        m_worker->appGlobals.backupMode = bMode::NOT_SET;
        break;
    case 1:
        m_worker->appGlobals.backupMode = bMode::NBD;
        break;
    case 2:
        m_worker->appGlobals.backupMode = bMode::NBDSSL;
        break;
    case 3:
        m_worker->appGlobals.backupMode = bMode::HOTADD;
        break;
    case 4:
        m_worker->appGlobals.backupMode = bMode::SAN;
    }
    m_worker->appGlobals.blockSize = adv->blocksizeSpin->text().toInt();          //blocksize (in sectors) needed for readbench
                                                                                  // or writebench
    if (adv->adapterSpin->currentIndex() == 1)
        m_worker->appGlobals.adapterType = VIXDISKLIB_ADAPTER_IDE;                //bus adapter type for 'create' option
                                                                                  //(default='scsi')
    else
        m_worker->appGlobals.adapterType = VIXDISKLIB_ADAPTER_SCSI_BUSLOGIC;
    m_worker->appGlobals.nfcHostPort = adv->nfcPortEdit->text().toInt();          //port to use to establish NFC connection
                                                                                  // to ESXi host (default = 902)

    m_worker->appGlobals.startSector = adv->startSectorSpin->text().toInt();      //start sector for 'dump/fill' options (default=0)
    m_worker->appGlobals.numSectors = adv->countSpin->text().toInt();             //number of sectors for 'dump/fill' options
                                                                                  // (default=1)
    m_worker->appGlobals.filler = adv->valSpin->text().toInt();                   //byte value to fill with for 'write' option
                                                                                  //(default=255)
    m_worker->appGlobals.mbSize = adv->capacitySpin->text().toInt();              //capacity in MB for -create option (default=100)
    m_worker->appGlobals.numThreads = adv->multithreadSpin->text().toInt();       //start n threads and copy the file to n new files

    if (adv->singleCheck->isChecked())
        m_worker->appGlobals.openFlags |= VIXDISKLIB_FLAG_OPEN_SINGLE_LINK;       //open file as single disk link (default=open
                                                                                  // entire chain)
    else
        m_worker->appGlobals.openFlags = 0;
    if (adv->disableCacheCheck->isChecked())
        m_worker->appGlobals.caching = true;
    else
        m_worker->appGlobals.caching = false;
    if (adv->generateCmdCheck->isChecked())
        m_worker->appGlobals.cmdOnly = true;
    else
        m_worker->appGlobals.cmdOnly = false;

    m_worker->appGlobals.libdir = adv->libdirEdit->text();                        //folder location of the VDDK installation
    m_worker->appGlobals.tmpDir = adv->tmpdirEdit->text();                        //location of temp directory (specified in conf
                                                                                  //file for InitEx)
    m_worker->appGlobals.libdir = adv->libdirEdit->text();                        //folder location of the VDDK installation
    m_worker->appGlobals.tmpDir = adv->tmpdirEdit->text();                        //location of temp directory (specified in conf
                                                                                  //file for InitEx)

    m_worker->appGlobals.cfgFile = adv->initexDirEdit->text();                    //location of initEx config file


    if (adv->initexDirEdit->text() != "")
        m_worker->appGlobals.useInitEx = true;
    else
        m_worker->appGlobals.useInitEx = false;

    m_worker->appGlobals.logFile = adv->lofileEdit->text();                       //log file location (specified in conf file for
                                                                                  // ItitEx)
    m_worker->appGlobals.logLevel = adv->transportLoggingSpin->text().toInt();    //log level 0 to 6 for quiet ranging to verbose
    m_worker->appGlobals.nfcLogLevel = adv->nfcLoggingSpin->text().toInt();       //nfc.LogLevel (o = none, 1 = Error, 2 = Warning,
                                                                                  // 3 = Info, 4 = Debug)
}

void vixdisklibsamplegui::ParseInitexConfig()
{
    if (adv->initexDirEdit->text() != "")
    {
        adv->tmpdirBrowseButton->setEnabled(true);
        adv->tmpdirEdit->setEnabled(true);
        adv->tmpdirLabel->setEnabled(true);
        adv->transportloggingLabel->setEnabled(true);
        adv->transportLoggingSpin->setEnabled(true);
        adv->nfcloggingLabel->setEnabled(true);
        adv->nfcLoggingSpin->setEnabled(true);
    }
    else
    {
        adv->tmpdirBrowseButton->setEnabled(false);
        adv->tmpdirEdit->setEnabled(false);
        adv->tmpdirLabel->setEnabled(false);
        adv->transportloggingLabel->setEnabled(false);
        adv->transportLoggingSpin->setEnabled(false);
        adv->nfcloggingLabel->setEnabled(false);
        adv->nfcLoggingSpin->setEnabled(false);
    }
}

void vixdisklibsamplegui::getThumbSlot()
{
    ParseArguments();
    int port = (m_worker->appGlobals.port) ? m_worker->appGlobals.port : 443;
    m_sslclient = new sslclient(m_worker->appGlobals.host, port, this);
    QString thumb;
    m_sslclient->run(thumb);
    if (thumb != ui->thumbEdit->text() && thumb != "")
        ui->thumbEdit->setText(thumb);
}

void vixdisklibsamplegui::printWorkerOutput(const QString &text)
{
    ui->textEdit->append(text);
}


void vixdisklibsamplegui::on_advancedButton_clicked()
{
    advanced->show();
}

void vixdisklibsamplegui::on_libdirBrowseButton_clicked()
{
    QDir dir;
    QString libdirPath = QFileDialog::getExistingDirectory(advanced, tr("open libdir directory"),
                                                     dir.absolutePath(),
                                                     QFileDialog::ShowDirsOnly
                                                     | QFileDialog::DontResolveSymlinks);

    QString libdir = dir.toNativeSeparators(libdirPath);               //to use backslashes on windows
    if (libdir != "")
        adv->libdirEdit->setText(libdir);
}

void vixdisklibsamplegui::on_readbenchButton_clicked()
{
    m_worker->appGlobals.command = 0;                                  //reset previous value
    m_worker->appGlobals.command |= COMMAND_READBENCH;
    m_worker->appGlobals.openFlags |= VIXDISKLIB_FLAG_OPEN_READ_ONLY;
    generateCmd();
    if (!adv->generateCmdCheck->isChecked())
        m_worker->run();
}

void vixdisklibsamplegui::on_writebenchButton_clicked()
{
    m_worker->appGlobals.command = 0;
    m_worker->appGlobals.command |= COMMAND_WRITEBENCH;
    generateCmd();
    if (!adv->generateCmdCheck->isChecked())
        m_worker->run();
}

void vixdisklibsamplegui::on_createButton_clicked()
{
    m_worker->appGlobals.command = 0;
    m_worker->appGlobals.command |= COMMAND_CREATE;
    generateCmd();
    if (!adv->generateCmdCheck->isChecked())
        m_worker->run();
}

void vixdisklibsamplegui::on_redoButton_clicked()
{
    m_worker->appGlobals.command = 0;
    m_worker->appGlobals.command |= COMMAND_REDO;
    generateCmd();
    if (!adv->generateCmdCheck->isChecked())
        m_worker->run();
}

void vixdisklibsamplegui::on_infoButton_clicked()
{
    m_worker->appGlobals.command = 0;
    m_worker->appGlobals.command |= COMMAND_INFO;
    generateCmd();
    if (!adv->generateCmdCheck->isChecked())
        m_worker->run();
}

void vixdisklibsamplegui::on_readmetaButton_clicked()
{
    m_worker->appGlobals.command = 0;
    m_worker->appGlobals.command |= COMMAND_DUMP_META;
    generateCmd();
    if (!adv->generateCmdCheck->isChecked())
        m_worker->run();
}

void vixdisklibsamplegui::on_checkButton_clicked()
{
    m_worker->appGlobals.command = 0;
    m_worker->appGlobals.command |= COMMAND_CHECKREPAIR;
    m_worker->appGlobals.repair = 0;
    generateCmd();
    if (!adv->generateCmdCheck->isChecked())
        m_worker->run();
}

void vixdisklibsamplegui::on_fillButton_clicked()
{
    m_worker->appGlobals.command = 0;
    m_worker->appGlobals.command |= COMMAND_FILL;
    generateCmd();
    if (!adv->generateCmdCheck->isChecked())
        m_worker->run();
}

void vixdisklibsamplegui::on_shrinkButton_clicked()
{
    m_worker->appGlobals.command = 0;
    m_worker->appGlobals.command |= COMMAND_SHRINK;
    generateCmd();
    if (!adv->generateCmdCheck->isChecked())
        m_worker->run();
}

void vixdisklibsamplegui::on_defragmentButton_clicked()
{
    m_worker->appGlobals.command = 0;
    m_worker->appGlobals.command |= COMMAND_DEFRAG;
    generateCmd();
    if (!adv->generateCmdCheck->isChecked())
        m_worker->run();
}

void vixdisklibsamplegui::on_repairButton_clicked()
{
    m_worker->appGlobals.command = 0;
    m_worker->appGlobals.command |= COMMAND_CHECKREPAIR;
    m_worker->appGlobals.repair = 1;
    generateCmd();
    if (!adv->generateCmdCheck->isChecked())
        m_worker->run();
}

void vixdisklibsamplegui::on_dumpButton_clicked()
{
    m_worker->appGlobals.command = 0;
    m_worker->appGlobals.command |= COMMAND_DUMP;
    generateCmd();
    if (!adv->generateCmdCheck->isChecked())
        m_worker->run();
}

void vixdisklibsamplegui::loadSettings()
{
    QSettings settings(m_sSettingsFile, QSettings::IniFormat);

    QString host = settings.value("host", "").toString();
    if (host != "")
        ui->hostIPEdit->setText(host);

    QString port = settings.value("port", "").toString();
    if (port != "")
        ui->portEdit->setText(port);

    QString thumb = settings.value("thumb", "").toString();
    if (thumb != "")
      ui->thumbEdit->setText(thumb);

    QString user = settings.value("user", "").toString();
    if(user != "")
      ui->userEdit->setText(user);

    QString password = settings.value("password", "").toString();
    if (password != "")
        ui->passEdit->setText(password);

    QString libdir = settings.value("libdir", "").toString();
    if (libdir != "")
        adv->libdirEdit->setText(libdir);

    QString vm = settings.value("vm", "").toString();
    if (vm != "")
       ui->vmrefEdit->setText(vm);

    QString ssmoref = settings.value("ssmoref", "").toString();
    if (ssmoref != "")
        ui->ssmorefEdit->setText(ssmoref);

    QString diskPath = settings.value("diskPath", "").toString();
    if (diskPath != "")
        ui->vmdkPathEdit->setText(diskPath);

    unsigned mbSize = settings.value("cap", "").toInt();
    if (mbSize)
        adv->capacitySpin->setValue(mbSize);

    unsigned numThreads = settings.value("multiThread", "").toInt();
    if (numThreads >= 1)
        adv->multithreadSpin->setValue(numThreads);

    QString sSingle = settings.value("single", "").toString();
    if (sSingle == QString("true"))
        adv->singleCheck->setChecked(true);

    QString backMode = settings.value("mode", "").toString();
    if (backMode == QString("nbd"))
        adv->modeCombo->setCurrentIndex(1);
    else if (backMode == QString("nbdssl"))
        adv->modeCombo->setCurrentIndex(2);
    else if (backMode == QString("hotadd"))
        adv->modeCombo->setCurrentIndex(3);
    else if (backMode == QString("san"))
        adv->modeCombo->setCurrentIndex(4);
    else
        adv->modeCombo->setCurrentIndex(0);

    int bSize = 0;
    bSize = settings.value("blocksize", "").toInt();
    if (bSize)
        adv->blocksizeSpin->setValue(bSize);

    QString adapter = settings.value("adapter", "").toString();
    if (adapter == QString("iSCSI"))
        adv->adapterSpin->setCurrentIndex(0);
    else if (adapter == QString("IDE"))
        adv->adapterSpin->setCurrentIndex(1);

    QString nfcPrt = settings.value("nfchostport", "").toString();
    adv->nfcPortEdit->setText(nfcPrt);

    int startSector = settings.value("start", "").toInt();
    adv->startSectorSpin->setValue(startSector);

    int count = 0;
    count = settings.value("count", "").toInt();
    if (count)
        adv->countSpin->setValue(count);

    int filler = settings.value("val", "").toInt();
    adv->valSpin->setValue(filler);

    int mSize = settings.value("cap", "").toInt();
    adv->capacitySpin->setValue(mSize);

    int nThreads = settings.value("multiThread", "").toInt();
    if (nThreads >= 1)
        adv->multithreadSpin->setValue(nThreads);

    QString sngl = settings.value("single", "").toString();
    if (sngl == QString("true"))
        adv->singleCheck->setChecked(true);

    QString ldir = settings.value("libdir", "").toString();
    adv->libdirEdit->setText(ldir);

    QString cfgFile = settings.value("initex", "").toString();
    adv->initexDirEdit->setText(cfgFile);

    QString tmpPath = settings.value("tmpdir", "").toString();
    adv->tmpdirEdit->setText(tmpPath);

    QString log = settings.value("logfile", "").toString();
    adv->lofileEdit->setText(log);

    QString cmdOnly = settings.value("cmdonly", "").toString();
    if (cmdOnly == QString("true"))
        adv->generateCmdCheck->setChecked(true);

    int logLvl = settings.value("loglevel", "").toInt();
    adv->transportLoggingSpin->setValue(logLvl);

    int nfcLvl = settings.value("nfcLogging").toInt();
    adv->nfcLoggingSpin->setValue(nfcLvl);

    ParseArguments();

}

void vixdisklibsamplegui::generateCmd()
{
    QFile command("command.bat");
    if (!command.open(QIODevice::WriteOnly))
    {
        qDebug() << "Error: Unable to open cmd file\n\n";
        return;
    }
    ParseArguments();
    QTextStream out(&command);
    out << "vixDiskLibSample.exe ";

    if (m_worker->appGlobals.command == COMMAND_READBENCH ||
            m_worker->appGlobals.command == COMMAND_WRITEBENCH)
    {
        if (m_worker->appGlobals.command == COMMAND_READBENCH)
            out << "-readbench ";
        else
            out << "-writebench ";

        out << m_worker->appGlobals.blockSize << " ";

        if (m_worker->appGlobals.libdir != "")                                    //if libdir was specified
        {
            out << "-libdir ";
            out << "\"" << m_worker->appGlobals.libdir << "\"" << " ";
        }

        if (m_worker->appGlobals.backupMode != bMode::NOT_SET)                    //if backup mode was choosen
        {
            out << "-mode ";
            if (m_worker->appGlobals.backupMode == bMode::NBD)
                out << "nbd ";
            else if (m_worker->appGlobals.backupMode == bMode::HOTADD)
                out << "hotadd ";
            else if (m_worker->appGlobals.backupMode == bMode::NBDSSL)
                out << "nbdssl ";
            else
                out << "san ";
        }

        out << "-vm " << m_worker->appGlobals.vmxSpec << " ";
        out << "-ssmoref " << m_worker->appGlobals.ssMoRef << " ";
    }
    else if (m_worker->appGlobals.command == COMMAND_INFO)
        out << "-info ";
    else if (m_worker->appGlobals.command == COMMAND_DUMP_META)
        out << "-meta ";
    else if (m_worker->appGlobals.command == COMMAND_DUMP ||
             m_worker->appGlobals.command == COMMAND_FILL)
    {
        if (m_worker->appGlobals.command == COMMAND_FILL)
        {
            out << "-fill ";
            if (m_worker->appGlobals.filler != 255)
                out << "-val " << m_worker->appGlobals.filler << " ";
        }
        else
            out << "-dump ";

        if (m_worker->appGlobals.startSector)
            out << "-start " << m_worker->appGlobals.startSector << " ";
        if (m_worker->appGlobals.numSectors > 1)                                  //1 - is a default value
            out << "-count " << m_worker->appGlobals.numSectors << " ";
    }

//TBD: write a function for shrink and defragment
//
//    else if(appGlobals.command == COMMAND_SHRINK)
//    {
//
//    }
//    else of(appGlobals.command == COMMAND_DEFRAGMENT)
//    {
//
//    }

    else if (m_worker->appGlobals.command == COMMAND_CREATE)                      //special case for create command as the file is created locally
    {
        out << "-create ";
        if (m_worker->appGlobals.mbSize)
            out << "-cap " << m_worker->appGlobals.mbSize << " ";
        if (m_worker->appGlobals.cfgFile != "")                                   //if config file is specified
        {
            out << "-initex ";
            out << "\"" << m_worker->appGlobals.cfgFile << "\"" << " ";
        }
        if (m_worker->appGlobals.diskPath != "")
            out << "\"" <<  m_worker->appGlobals.diskPath << "\"";
        else
            out << "disk.vmdk";                                            //default value for disk name
        command.close();
        return;
    }

    else if (m_worker->appGlobals.command == COMMAND_CHECKREPAIR)
    {
        out << "-check ";
        if (m_worker->appGlobals.repair)
            out << m_worker->appGlobals.repair << " ";
    }

    if (m_worker->appGlobals.cfgFile != "")                                   //if config file is specified
    {
        out << "-initex ";
        out << "\"" << m_worker->appGlobals.cfgFile << "\"" << " ";
    }

    //values for all commands

    out << "-host " << m_worker->appGlobals.host << " ";

    if (m_worker->appGlobals.port &&                                           //if port is not default
            m_worker->appGlobals.port != 443)
        out << "-port " << m_worker->appGlobals.port << " ";
    if (m_worker->appGlobals.nfcHostPort &&                                   //if port is not default
            m_worker->appGlobals.nfcHostPort != 902)
        out << "-nfchostport " << m_worker->appGlobals.nfcHostPort << " ";

    out << "-user " << m_worker->appGlobals.userName << " ";
    out << "-password " << m_worker->appGlobals.password << " ";
    if (m_worker->appGlobals.thumbPrint != "")
        out << "-thumb " << "\"" << m_worker->appGlobals.thumbPrint << "\"" << " ";
    out << "\"" << m_worker->appGlobals.diskPath << "\"";

    command.close();
}

void vixdisklibsamplegui::on_saveSettingsButton_clicked()
{
    QSettings settings(m_sSettingsFile, QSettings::IniFormat);

    ParseArguments();

    settings.setValue("host",           m_worker->appGlobals.host);
    settings.setValue("port",           m_worker->appGlobals.port);
    settings.setValue("thumb",          m_worker->appGlobals.thumbPrint);
    settings.setValue("user",           m_worker->appGlobals.userName);
    settings.setValue("password",       m_worker->appGlobals.password);
    settings.setValue("libdir",         m_worker->appGlobals.libdir);
    settings.setValue("vm",             m_worker->appGlobals.vmxSpec);
    settings.setValue("ssmoref",        m_worker->appGlobals.ssMoRef);
    settings.setValue("diskPath",       m_worker->appGlobals.diskPath);
    settings.setValue("cap",            m_worker->appGlobals.mbSize);
    settings.setValue("multiThread",    m_worker->appGlobals.numThreads);

    if (m_worker->appGlobals.openFlags == VIXDISKLIB_FLAG_OPEN_SINGLE_LINK)
        settings.setValue("single",     "true");
    else
        settings.setValue("single",     "false");

    if (m_worker->appGlobals.cmdOnly == true)
        settings.setValue("cmdonly",     "true");
    else
        settings.setValue("cmdonly",     "false");

    switch (m_worker->appGlobals.backupMode) {
    case bMode::NBD:
        settings.setValue("mode",       "nbd");
        break;
    case bMode::NBDSSL:
        settings.setValue("mode",       "nbdssl");
        break;
    case bMode::HOTADD:
        settings.setValue("mode",       "hotadd");
        break;
    case bMode::SAN:
        settings.setValue("mode",       "san");
        break;
    default:
        settings.setValue("mode",       "not_set");
    }

    settings.setValue("blocksize",      m_worker->appGlobals.blockSize);

    if (m_worker->appGlobals.adapterType == VIXDISKLIB_ADAPTER_IDE)
        settings.setValue("adapter",    "IDE");
    else
        settings.setValue("adapter",    "iSCSI");

    settings.setValue("nfchostport",    m_worker->appGlobals.nfcHostPort);
    settings.setValue("start",          m_worker->appGlobals.startSector);
    settings.setValue("count",          m_worker->appGlobals.numSectors);
    settings.setValue("val",            m_worker->appGlobals.filler);

    settings.setValue("libdir",         m_worker->appGlobals.libdir);

    settings.setValue("tmpdir",         m_worker->appGlobals.tmpDir);

//TBD: changing the name for log file
//    settings.setValue("logfile",    appGlobals.logFile);
//    if (appGlobals.logFile != "")
//        config.setValue("");

//saving vddk config file for initex

    settings.setValue("loglevel",   m_worker->appGlobals.logLevel);
    settings.setValue("nfcLogging", m_worker->appGlobals.nfcLogLevel);
    settings.setValue("initex",     m_worker->appGlobals.cfgFile);

    if (m_worker->appGlobals.useInitEx)
    {
        QFile vddk_conf(m_worker->appGlobals.cfgFile);
        if (!vddk_conf.open(QIODevice::WriteOnly))
        {
            qDebug() << "Error: Unable to open vddk config file\n\n";
            return;
        }
        QTextStream out(&vddk_conf);

        if (m_worker->appGlobals.tmpDir != "")
        {
            out << "# temporary directory for logs etc.\n";
            out << "tmpDirectory=\"" << m_worker->appGlobals.tmpDir << "\"\n";
        }
        out << "# log level 0 to 6 for quiet ranging to verbose\n";
        out << "vixDiskLib.transport.LogLevel=" << m_worker->appGlobals.logLevel << "\n";

        out << "# nfc.LogLevel (0 = Quiet, 1 = Error, 2 = Warning, 3 = Info, 4 = Debug)\n";
        out << "vixDiskLib.nfc.LogLevel=" << m_worker->appGlobals.nfcLogLevel << "\n";
        vddk_conf.close();
    }

}

//void vixdisklibsamplegui::on_saveSettingsButton_clicked()
//{
//    QFile command("command.bat");
//    if (!command.open(QIODevice::WriteOnly))
//    {
//        qDebug() << "Error: Unable to open config file\n\n";
//        return;
//    }
//    ParseArguments();
//    QTextStream out(&command);
//    out << "vixDiskLibSample.exe ";
//    out << "-info ";
//    out << "-libdir " << "\""
//        << ( (appGlobals.libdir != "") ? appGlobals.libdir : QDir::currentPath() ) << "\" ";
//    out << "-host "
//        << ( (appGlobals.host != "") ? appGlobals.host : "vc.ip.or.hostname" ) << " " ;
//    out << "-thumb " << "\""
//        << ( (appGlobals.thumbPrint != "") ? appGlobals.thumbPrint :
//                                       "xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx" ) << "\" ";
//    out << "-user " << "\""
//        << ( (appGlobals.userName != "") ? appGlobals.userName : "domain\\user" ) << "\" ";
//    out << "-password "
//        << ( (appGlobals.password != "") ? appGlobals.password : "example_password" ) << " " ;
//    out << "-vm " << "\""
//        << ( (appGlobals.vmxSpec != "") ? appGlobals.vmxSpec : "moref=vm-00" ) << "\" ";
//    out << "-ssmoref "
//        << ( (appGlobals.ssMoRef != "") ? appGlobals.ssMoRef : "snapshot-00" ) << " ";
//    command.close();
//}



void vixdisklibsamplegui::on_buttonBox_rejected()
{
    switch (m_worker->appGlobals.backupMode) {                                    //mode string to pass into VixDiskLib_ConnectEx
    case bMode::NOT_SET:
        adv->modeCombo->setCurrentIndex(0);
        break;
    case bMode::NBD:
        adv->modeCombo->setCurrentIndex(1);
        break;
    case bMode::NBDSSL:
        adv->modeCombo->setCurrentIndex(2);
        break;
    case bMode::HOTADD:
        adv->modeCombo->setCurrentIndex(3);
        break;
    case bMode::SAN:
        adv->modeCombo->setCurrentIndex(3);
    }

    adv->blocksizeSpin->setValue(m_worker->appGlobals.blockSize);                 //blocksize (in sectors) needed for readbench or writebench
    if (m_worker->appGlobals.adapterType == VIXDISKLIB_ADAPTER_IDE)               //bus adapter type for 'create' option (default='scsi')
        adv->adapterSpin->setCurrentIndex(1);
    else
        adv->adapterSpin->setCurrentIndex(0);
    adv->nfcPortEdit->setText(QString::number(m_worker->appGlobals.nfcHostPort)); //port to use to establish NFC connection to ESXi host (default = 902)
    adv->startSectorSpin->setValue(m_worker->appGlobals.startSector);             //start sector for 'dump/fill' options (default=0)
    adv->countSpin->setValue(m_worker->appGlobals.numSectors);                    //number of sectors for 'dump/fill' options (default=1)
    adv->valSpin->setValue(m_worker->appGlobals.filler);                          //byte value to fill with for 'write' option (default=255)
    adv->capacitySpin->setValue(m_worker->appGlobals.mbSize);                     //capacity in MB for -create option (default=100)
    adv->multithreadSpin->setValue(m_worker->appGlobals.numThreads);              //start n threads and copy the file to n new files
    if (m_worker->appGlobals.openFlags == VIXDISKLIB_FLAG_OPEN_SINGLE_LINK)
        adv->singleCheck->setChecked(true);                                       //open file as single disk link (default=open entire chain)
    else
        adv->singleCheck->setChecked(false);

    if (m_worker->appGlobals.caching == true)
        adv->disableCacheCheck->setChecked(true);
    else
        adv->disableCacheCheck->setChecked(false);

    adv->libdirEdit->setText(m_worker->appGlobals.libdir);                        //folder location of the VDDK installation
    adv->tmpdirEdit->setText(m_worker->appGlobals.tmpDir);                        //location of temp directory (specified in conf file for ItitEx)
    adv->initexDirEdit->setText(m_worker->appGlobals.cfgFile);
    adv->lofileEdit->setText(m_worker->appGlobals.logFile);                       //log file location (specified in conf file for ItitEx)
    adv->transportLoggingSpin->setValue(m_worker->appGlobals.logLevel);           //log level 0 to 6 for quiet ranging to verbose
    adv->nfcLoggingSpin->setValue(m_worker->appGlobals.nfcLogLevel);              //nfc.LogLevel (o = none, 1 = Error, 2 = Warning, 3 = Info, 4 = Debug)

    ParseInitexConfig();
}

void vixdisklibsamplegui::on_tmpdirBrowseButton_clicked()
{
    QDir dir;
    QString tmpDirPath = QFileDialog::getExistingDirectory(advanced, tr("open tmp directory"),
                                                     dir.absolutePath(),
                                                     QFileDialog::ShowDirsOnly
                                                     | QFileDialog::DontResolveSymlinks);

    QString tmpDir = dir.toNativeSeparators(tmpDirPath);               //to use backslashes on windows
    if (tmpDir != "")
        adv->tmpdirEdit->setText(tmpDir);
}

void vixdisklibsamplegui::on_initexCfgBrowseButton_clicked()
{
    QDir dir;
    QString initExPath = QFileDialog::getSaveFileName(advanced, tr("choose initEx config file"),
                                                     dir.absolutePath() + "/vddk.conf");

    QString initEx = dir.toNativeSeparators(initExPath);               //to use backslashes on windows
    if (initEx != "")
        adv->initexDirEdit->setText(initEx);
}
