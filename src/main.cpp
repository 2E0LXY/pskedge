#include "MainWindow.h"
#include "AppConfig.h"
#include "cat/CatController.h"

#include <QApplication>
#include <QMetaType>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    qRegisterMetaType<DecodeLine>("DecodeLine");
    qRegisterMetaType<CatSnapshot>("CatSnapshot");
    qRegisterMetaType<AppConfig>("AppConfig");
    MainWindow window;
    window.show();
    return app.exec();
}
