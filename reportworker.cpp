#include "reportworker.h"
#include "utils/csvexporter.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QStringList>
#include <QVariant>

ReportWorker::ReportWorker(QObject *parent)
    : QObject(parent)
{
}

void ReportWorker::setDatabase(const QSqlDatabase &db)
{
    m_dbPath = db.databaseName();
    m_connName = QString("worker-%1").arg(reinterpret_cast<quintptr>(this));
}

QSqlDatabase ReportWorker::openDb()
{
    if (QSqlDatabase::contains(m_connName)) {
        return QSqlDatabase::database(m_connName);
    }
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", m_connName);
    db.setDatabaseName(m_dbPath);
    db.open();
    return db;
}

void ReportWorker::generateReport()
{
    QSqlDatabase db = openDb();
    if (!db.isOpen()) return;
    QSqlQuery q(db);
    q.exec(R"(SELECT a.title, a.category, a.start_time, a.end_time, a.capacity,
              (SELECT COUNT(*) FROM enrollments e WHERE e.activity_id=a.id AND e.status='active') AS enrolled
              FROM activities a ORDER BY a.start_time)");
    QVector<QStringList> rows;
    rows << QStringList{ "标题", "类别", "开始", "结束", "容量", "已报名" };
    while (q.next()) {
        rows << QStringList{
            q.value(0).toString(),
            q.value(1).toString(),
            q.value(2).toString(),
            q.value(3).toString(),
            q.value(4).toString(),
            q.value(5).toString()
        };
    }
    const QString path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + QDir::separator() + QString("activity_report_%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmm"));
    QString err;
    if (CsvExporter::write(path, rows, &err)) {
        emit finished(path);
    } else {
        emit finished(QStringLiteral("导出失败: %1").arg(err));
    }
}

void ReportWorker::checkConflicts()
{
    QSqlDatabase db = openDb();
    if (!db.isOpen()) return;
    QSqlQuery q(db);
    q.exec(R"(SELECT e.student, a.title, a.start_time, a.end_time
              FROM enrollments e
              JOIN activities a ON e.activity_id=a.id
              WHERE e.status='active' AND a.status!='cancelled'
              ORDER BY e.student, a.start_time)");
    struct Item { QString title; QDateTime start; QDateTime end; };
    QString lastStudent;
    QList<Item> items;
    QStringList conflictLines;
    while (q.next()) {
        const QString student = q.value(0).toString();
        const QString title = q.value(1).toString();
        const QDateTime start = QDateTime::fromString(q.value(2).toString(), Qt::ISODate);
        const QDateTime end = QDateTime::fromString(q.value(3).toString(), Qt::ISODate);
        if (student != lastStudent) {
            items.clear();
            lastStudent = student;
        }
        for (const auto &it : items) {
            if (!(end <= it.start || start >= it.end)) {
                conflictLines << QString("%1: 「%2」(%3-%4) 与 「%5」(%6-%7) 时间冲突")
                                     .arg(student,
                                          it.title,
                                          it.start.toString("MM-dd hh:mm"), it.end.toString("MM-dd hh:mm"),
                                          title,
                                          start.toString("MM-dd hh:mm"), end.toString("MM-dd hh:mm"));
            }
        }
        items.append(Item{title, start, end});
    }
    if (conflictLines.isEmpty()) {
        emit conflictChecked(QStringLiteral("未发现时间冲突"));
    } else {
        emit conflictChecked(conflictLines.join('\n'));
    }
}

