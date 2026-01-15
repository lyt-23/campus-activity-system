#include "activitymodel.h"

#include <QSqlRecord>

ActivityModel::ActivityModel(QObject *parent, const QSqlDatabase &db)
    : QSqlTableModel(parent, db)
{
    setTable("activities");
    setEditStrategy(QSqlTableModel::OnManualSubmit);
    select();
    setHeaderData(0, Qt::Horizontal, tr("ID"));
    setHeaderData(1, Qt::Horizontal, tr("标题"));
    setHeaderData(2, Qt::Horizontal, tr("类别"));
    setHeaderData(3, Qt::Horizontal, tr("地点"));
    setHeaderData(4, Qt::Horizontal, tr("开始"));
    setHeaderData(5, Qt::Horizontal, tr("结束"));
    setHeaderData(6, Qt::Horizontal, tr("容量"));
    setHeaderData(7, Qt::Horizontal, tr("审批人"));
    setHeaderData(8, Qt::Horizontal, tr("状态"));
    setHeaderData(9, Qt::Horizontal, tr("发起人"));
}

void ActivityModel::applyFilter(const QString &role, const QString &username, const QString &category, const QString &status, const QString &keyword)
{
    QStringList filters;
    if (role == "initiator") {
        QString uname = username;
        uname.replace('\'', "''");
        filters << QString("creator='%1'").arg(uname);
    }
    if (!category.isEmpty()) {
        QString cat = category;
        cat.replace('\'', "''");
        filters << QString("category LIKE '%%1%'").arg(cat);
    }
    if (!status.isEmpty()) {
        QString st = status;
        st.replace('\'', "''");
        filters << QString("status='%1'").arg(st);
    }
    if (!keyword.isEmpty()) {
        QString kw = keyword;
        kw.replace('\'', "''");
        filters << QString("(title LIKE '%%1%' OR location LIKE '%%1%')").arg(kw);
    }
    setFilter(filters.join(" AND "));
    select();
}

