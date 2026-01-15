#pragma once

#include <QSqlTableModel>

class ActivityModel : public QSqlTableModel
{
    Q_OBJECT
public:
    explicit ActivityModel(QObject *parent, const QSqlDatabase &db);
    void applyFilter(const QString &role, const QString &username, const QString &category, const QString &status, const QString &keyword);
};

