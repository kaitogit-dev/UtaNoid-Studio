#include "MainWindow.h"
#include <QApplication>
#include <QStyleFactory>

int main(int argc, char *argv[]) {
    QApplication application(argc, argv);
    application.setApplicationName("UtaNoid Studio");
    application.setOrganizationName("UtaNoid");
    application.setStyle(QStyleFactory::create("Fusion"));
    MainWindow window;
    window.show();
    return application.exec();
}
