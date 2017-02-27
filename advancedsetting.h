#ifndef ADVANCEDSETTING_H
#define ADVANCEDSETTING_H

#include <QWidget>
#include <QMainWindow>

namespace Ui {
class advancedSetting;
}

class advancedSetting : public QMainWindow
{
    Q_OBJECT
public:
    explicit advancedSetting(QWidget *parent = 0);
    ~advancedSetting();

signals:

public slots:
};

#endif // ADVANCEDSETTING_H
