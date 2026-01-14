#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariantList>

struct UserInfo {
    QString username;
    QString role; // admin / initiator / student
};

class DbManager : public QObject
{
    Q_OBJECT
public:
    explicit DbManager(QObject *parent = nullptr);
    ~DbManager();
    bool open(const QString &path);
    bool initSchema();

    bool validateUser(const QString &username, const QString &password, UserInfo &outUser);
    bool createUser(const QString &username, const QString &password, const QString &role, QString *error = nullptr);
    QSqlDatabase database() const { return m_db; }
    QString lastErrorText() const { return m_lastError; }

signals:
    void error(const QString &message);

private:
    bool ensureSampleData();
    QSqlDatabase m_db;
    QString m_lastError;
    QString m_connName;
};

