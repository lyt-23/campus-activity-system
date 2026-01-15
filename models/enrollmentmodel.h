#pragma once

#include <QSqlQueryModel>
#include <QSqlDatabase>

class EnrollmentModel : public QSqlQueryModel
{
    Q_OBJECT
public:
    explicit EnrollmentModel(QObject *parent = nullptr, const QSqlDatabase &db = QSqlDatabase());

    void loadAvailableActivities();
    void loadMyEnrollments(const QString &student, bool waitingOnly = false);
    int idForRow(int row) const;

private:
    QSqlDatabase m_db;
};

