#ifndef VIXDISKLIBSAMPLEGUI_H
#define VIXDISKLIBSAMPLEGUI_H

#include <QMainWindow>
#include "ui_advanced.h"

class QDialog;
class QString;
class worker;
class sslclient;
class QSignalMapper;
class QFileDialog;

namespace Ui {
class vixdisklibsamplegui;
class Advanced;
}

class vixdisklibsamplegui : public QMainWindow
{
    Q_OBJECT

    QDialog *advanced;
    QString m_sSettingsFile;                             //file with settings of current program
    worker* m_worker;
    QSignalMapper *m_mapper;
    sslclient *m_sslclient;

public:
    explicit vixdisklibsamplegui(QWidget *parent = 0);
    ~vixdisklibsamplegui();
    void loadSettings();                                //load settings from config file
    void generateCmd();                                 //generate CMD line to start vixdisklib manually

signals:

private slots:

    void ParseArguments();                              //parse arguments from gui

    void ParseInitexConfig();                           //separate parser for settings included in
                                                        //initEx config file

    void getThumbSlot();

    void printWorkerOutput(const QString &text);

    void on_advancedButton_clicked();

    void on_libdirBrowseButton_clicked();

    void on_readbenchButton_clicked();

    void on_writebenchButton_clicked();

    void on_createButton_clicked();

    void on_redoButton_clicked();

    void on_infoButton_clicked();

    void on_readmetaButton_clicked();

    void on_checkButton_clicked();

    void on_saveSettingsButton_clicked();

    void on_fillButton_clicked();

    void on_shrinkButton_clicked();

    void on_defragmentButton_clicked();

    void on_repairButton_clicked();

    void on_dumpButton_clicked();

    void on_buttonBox_rejected();                       //revert changes, when button pressed

    void on_tmpdirBrowseButton_clicked();

    void on_initexCfgBrowseButton_clicked();

private:
    Ui::vixdisklibsamplegui *ui;
    Ui::Advanced *adv;                                  //widget for advanced settings
};

#endif // VIXDISKLIBSAMPLEGUI_H
