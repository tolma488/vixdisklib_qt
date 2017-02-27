#include "vixdisklibsamplegui.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    vixdisklibsamplegui w;
    w.show();

    return a.exec();
}
