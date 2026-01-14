#pragma once

#include <QDialog>
#include "dbmanager.h"

namespace Ui {
class LoginDialog;
}

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr);
    ~LoginDialog();

    UserInfo selectedUser() const { return m_user; }

private slots:
    void onLogin();
    void onRegister();

private:
    Ui::LoginDialog *ui;
    DbManager m_db;
    UserInfo m_user;
};

