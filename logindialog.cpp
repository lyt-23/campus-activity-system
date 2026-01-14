#include "logindialog.h"
#include "ui_logindialog.h"

#include <QMessageBox>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>

LoginDialog::LoginDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::LoginDialog)
{
    ui->setupUi(this);
    // setWindowTitle(tr("校园活动报名与签到 - 登录"));
    ui->passwordEdit->setEchoMode(QLineEdit::Password);

    // Init DB
    const QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QDir::separator() + "activity.db";
    QDir().mkpath(QFileInfo(dbPath).absolutePath());

    auto openOrReset = [this](const QString &p)->bool {
        if (m_db.open(p) && m_db.initSchema()) return true;
        QFile::remove(p);
        return m_db.open(p) && m_db.initSchema();
    };
    if (!openOrReset(dbPath)) {
        QMessageBox::critical(this, tr("错误"), tr("数据库无法打开/初始化: %1").arg(m_db.lastErrorText()));
    }

    connect(ui->loginButton, &QPushButton::clicked, this, &LoginDialog::onLogin);
    connect(ui->registerButton, &QPushButton::clicked, this, &LoginDialog::onRegister);
}

LoginDialog::~LoginDialog()
{
    delete ui;
}

void LoginDialog::onLogin()
{
    const QString user = ui->usernameEdit->text().trimmed();
    const QString pwd = ui->passwordEdit->text();
    QString role;
    switch (ui->roleCombo->currentIndex()) {
    case 0: role = "admin"; break;
    case 1: role = "initiator"; break;
    case 2: role = "student"; break;
    default: role.clear(); break;
    }
    if (user.isEmpty() || pwd.isEmpty() || role.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请输入用户名、密码并选择角色"));
        return;
    }
    ui->hintLabel->clear();
    UserInfo info;
    if (!m_db.validateUser(user, pwd, info)) {
        ui->hintLabel->setText(tr("用户名或密码错误"));
        return;
    }
    if (info.role != role) {
        ui->hintLabel->setText(tr("角色不匹配，当前账号角色：%1").arg(info.role));
        return;
    }
    m_user = info;
    accept();
}

void LoginDialog::onRegister()
{
    const QString user = ui->usernameEdit->text().trimmed();
    const QString pwd = ui->passwordEdit->text();
    QString role;
    switch (ui->roleCombo->currentIndex()) {
    case 0: role = "admin"; break;
    case 1: role = "initiator"; break;
    case 2: role = "student"; break;
    default: role.clear(); break;
    }
    if (user.isEmpty() || pwd.isEmpty() || role.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请输入用户名、密码并选择角色"));
        return;
    }
    QString err;
    if (m_db.createUser(user, pwd, role, &err)) {
        ui->hintLabel->setText(tr("账号创建成功，可直接登录"));
    } else {
        ui->hintLabel->setText(tr("创建失败: %1").arg(err));
    }
}
