#include "mainwindow.h"
#include "logindialog.h"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QCoreApplication::setOrganizationName("CampusActivity");
    QCoreApplication::setApplicationName("ActivityManager");
    QCoreApplication::setApplicationVersion("1.0");

    auto *login = new LoginDialog();
    login->show();

    MainWindow *mainWin = nullptr;

    QObject::connect(login, &QDialog::accepted, [&]() {
        // 创建/切换到主窗口
        if (mainWin) {
            mainWin->close();
            mainWin->deleteLater();
            mainWin = nullptr;
        }
        mainWin = new MainWindow(login->selectedUser());
        QObject::connect(mainWin, &MainWindow::logoutRequested, [&]() {
            // 返回登录界面
            login->show();
            mainWin->close();
            mainWin->deleteLater();
            mainWin = nullptr;
        });
        login->hide();
        mainWin->show();
    });

    QObject::connect(login, &QDialog::rejected, [&]() {
        if (!mainWin) {
            a.quit();
        }
    });

    return a.exec();
}

