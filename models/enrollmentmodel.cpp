#include "enrollmentmodel.h"

#include <QSqlQuery>

EnrollmentModel::EnrollmentModel(QObject *parent, const QSqlDatabase &db)
    : QSqlQueryModel(parent),
      m_db(db)
{
}

void EnrollmentModel::loadAvailableActivities()
{
    QSqlQuery q(m_db);
    q.exec(R"(SELECT a.id, a.title AS 标题, a.category AS 类别, a.location AS 地点,
              a.start_time AS 开始, a.end_time AS 结束, a.capacity AS 容量,
              (SELECT COUNT(*) FROM enrollments e WHERE e.activity_id=a.id AND e.status='active') AS 已报名
              FROM activities a WHERE a.status='approved' ORDER BY a.start_time)");
    setQuery(q);
}

void EnrollmentModel::loadMyEnrollments(const QString &student, bool waitingOnly)
{
    QSqlQuery q(m_db);
    if (waitingOnly) {
        q.prepare(R"(SELECT e.id, a.title AS 标题, a.start_time AS 开始, a.end_time AS 结束,
                      e.status AS 状态, e.position AS 候补序号
                      FROM enrollments e
                      JOIN activities a ON e.activity_id=a.id
                      WHERE e.student=? AND e.status='waiting'
                      ORDER BY e.position)");
    } else {
        q.prepare(R"(SELECT e.id, a.title AS 标题, a.start_time AS 开始, a.end_time AS 结束,
                      e.status AS 状态, e.position AS 候补序号
                      FROM enrollments e
                      JOIN activities a ON e.activity_id=a.id
                      WHERE e.student=?
                      ORDER BY a.start_time)");
    }
    q.addBindValue(student);
    q.exec();
    setQuery(q);
}

int EnrollmentModel::idForRow(int row) const
{
    if (row < 0 || row >= rowCount()) return -1;
    return data(index(row, 0)).toInt();
}

