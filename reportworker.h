#pragma once

#include <QObject>
#include <QSqlDatabase>

class ReportWorker : public QObject
{
    Q_OBJECT
public:
    explicit ReportWorker(QObject *parent = nullptr);
    void setDatabase(const QSqlDatabase &db);

public slots:
    void generateReport();
    void checkConflicts();

signals:
    void finished(const QString &path);
    void conflictChecked(const QString &message);

private:
    QString m_dbPath;
    QString m_connName;
    QSqlDatabase openDb();
};

