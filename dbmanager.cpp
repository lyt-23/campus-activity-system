#include "dbmanager.h"

#include <QDir>
#include <QDateTime>
#include <QSqlRecord>
#include <QDebug>
#include <QRandomGenerator>

namespace {
QString createUsersTable()
{
    return QStringLiteral(R"SQL(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            password TEXT NOT NULL,
            role TEXT NOT NULL
        );
    )SQL");
}

QString createActivitiesTable()
{
    return QStringLiteral(R"SQL(
        CREATE TABLE IF NOT EXISTS activities (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            category TEXT NOT NULL,
            location TEXT NOT NULL,
            start_time TEXT NOT NULL,
            end_time TEXT NOT NULL,
            capacity INTEGER NOT NULL DEFAULT 0,
            approver TEXT,
            status TEXT NOT NULL DEFAULT 'pending', -- pending/approved/rejected/cancelled
            creator TEXT NOT NULL
        );
    )SQL");
}

QString createEnrollmentsTable()
{
    return QStringLiteral(R"SQL(
        CREATE TABLE IF NOT EXISTS enrollments (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            activity_id INTEGER NOT NULL,
            student TEXT NOT NULL,
            created_at TEXT NOT NULL,
            status TEXT NOT NULL DEFAULT 'active', -- active/cancelled/waiting
            position INTEGER DEFAULT 0,
            FOREIGN KEY(activity_id) REFERENCES activities(id)
        );
    )SQL");
}

QString createAuditLogsTable()
{
    return QStringLiteral(R"SQL(
        CREATE TABLE IF NOT EXISTS audit_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            action TEXT NOT NULL,
            actor TEXT NOT NULL,
            target TEXT,
            detail TEXT,
            created_at TEXT NOT NULL
        );
    )SQL");
}

}

DbManager::DbManager(QObject *parent)
    : QObject(parent)
{
}

DbManager::~DbManager()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    // 不在析构时 removeDatabase，避免仍有引用导致崩溃
}

bool DbManager::open(const QString &path)
{
    // 每次打开使用独立连接名，避免与其他窗口/线程冲突
    m_connName = QStringLiteral("conn-%1").arg(QRandomGenerator::global()->generate64());
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connName);
    m_db.setDatabaseName(path);

    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        emit error(tr("Failed to open database: %1").arg(m_lastError));
        return false;
    }
    return true;
}

bool DbManager::initSchema()
{
    QSqlQuery q(m_db);
    if (!q.exec(createUsersTable())) {
        m_lastError = q.lastError().text();
        emit error(m_lastError);
        return false;
    }
    if (!q.exec(createActivitiesTable())) {
        m_lastError = q.lastError().text();
        emit error(m_lastError);
        return false;
    }
    if (!q.exec(createEnrollmentsTable())) {
        m_lastError = q.lastError().text();
        emit error(m_lastError);
        return false;
    }
    if (!q.exec(createAuditLogsTable())) {
        m_lastError = q.lastError().text();
        emit error(m_lastError);
        return false;
    }
    // 单独执行索引创建，避免一次多语句
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_enrollment_activity ON enrollments(activity_id)")) {
        m_lastError = q.lastError().text();
        emit error(m_lastError);
        return false;
    }
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_enrollment_student ON enrollments(student)")) {
        m_lastError = q.lastError().text();
        emit error(m_lastError);
        return false;
    }
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_audit_time ON audit_logs(created_at)")) {
        m_lastError = q.lastError().text();
        emit error(m_lastError);
        return false;
    }
    return ensureSampleData();
}

bool DbManager::ensureSampleData()
{
    QSqlQuery q(m_db);
    QSqlQuery insert(m_db);
    insert.prepare("INSERT OR IGNORE INTO users(username,password,role) VALUES(?,?,?)");

    // 如果 admin 不存在，或用户表为空，则补充默认账号
    bool needSeed = false;
    if (q.exec("SELECT COUNT(*) FROM users") && q.next()) {
        needSeed = q.value(0).toInt() == 0;
    }
    QSqlQuery checkAdmin(m_db);
    if (checkAdmin.exec("SELECT COUNT(*) FROM users WHERE username='admin'") && checkAdmin.next()) {
        if (checkAdmin.value(0).toInt() == 0) needSeed = true;
    }

    if (needSeed) {
        QVariantList users = {
            QVariantList{"admin", "admin123", "admin"},
            QVariantList{"host", "host123", "initiator"},
            QVariantList{"alice", "alice123", "student"},
            QVariantList{"bob", "bob123", "student"}
        };
        for (const QVariant &v : users) {
            const QVariantList row = v.toList();
            insert.addBindValue(row[0]);
            insert.addBindValue(row[1]);
            insert.addBindValue(row[2]);
            if (!insert.exec()) {
                qWarning() << "Failed to insert sample user" << insert.lastError();
            }
            insert.finish();
        }
    }

    // 强制确保 admin 密码为默认值
    QSqlQuery up(m_db);
    up.prepare("UPDATE users SET password='admin123', role='admin' WHERE username='admin'");
    up.exec();

    q.exec("SELECT COUNT(*) FROM activities;");
    if (q.next() && q.value(0).toInt() == 0) {
        QSqlQuery act(m_db);
        act.prepare(R"(INSERT INTO activities(title, category, location, start_time, end_time, capacity, status, creator, approver)
                       VALUES(?,?,?,?,?,?,?,?,?))");
        const QDateTime now = QDateTime::currentDateTime();
        QList<QVariantList> seed = {
            { "开学迎新", "社团", "礼堂", now.addDays(1).toString(Qt::ISODate), now.addDays(1).addSecs(7200).toString(Qt::ISODate), 50, "approved", "host", "admin" },
            { "篮球赛", "体育", "体育馆", now.addDays(2).toString(Qt::ISODate), now.addDays(2).addSecs(5400).toString(Qt::ISODate), 30, "approved", "host", "admin" },
            { "AI 讲座", "学术", "会议室A", now.addDays(3).toString(Qt::ISODate), now.addDays(3).addSecs(3600).toString(Qt::ISODate), 80, "pending", "host", QVariant() }
        };
        for (const auto &row : seed) {
            for (const auto &val : row) act.addBindValue(val);
            if (!act.exec()) {
                qWarning() << "Failed seed activity" << act.lastError();
            }
            act.finish();
        }
    }
    return true;
}

bool DbManager::validateUser(const QString &username, const QString &password, UserInfo &outUser)
{
    QSqlQuery q(m_db);
    q.prepare("SELECT role FROM users WHERE username=? AND password=?");
    q.addBindValue(username);
    q.addBindValue(password);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        emit error(m_lastError);
        return false;
    }
    if (q.next()) {
        outUser.username = username;
        outUser.role = q.value(0).toString();
        return true;
    }

    // 如果用户表为空，尝试重新注入示例数据后再查一次（防止首次初始化失败）
    QSqlQuery count(m_db);
    if (count.exec("SELECT COUNT(*) FROM users") && count.next() && count.value(0).toInt() == 0) {
        ensureSampleData();
        q.finish();
        q.prepare("SELECT role FROM users WHERE username=? AND password=?");
        q.addBindValue(username);
        q.addBindValue(password);
        if (q.exec() && q.next()) {
            outUser.username = username;
            outUser.role = q.value(0).toString();
            return true;
        }
    }
    return false;
}

bool DbManager::createUser(const QString &username, const QString &password, const QString &role, QString *error)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO users(username,password,role) VALUES(?,?,?)");
    q.addBindValue(username);
    q.addBindValue(password);
    q.addBindValue(role);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        if (error) *error = m_lastError;
        emit this->error(m_lastError);
        return false;
    }
    return true;
}

