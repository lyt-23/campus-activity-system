#pragma once

#include <QMainWindow>
#include <QSqlTableModel>
#include <QItemSelection>
#include <QSqlQueryModel>
#include <QTableView>
#include <QThread>
#include "dbmanager.h"
#include "models/activitymodel.h"
#include "models/enrollmentmodel.h"
#include "networkservice.h"
#include "reportworker.h"
#include "utils/csvexporter.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const UserInfo &user, QWidget *parent = nullptr);
    ~MainWindow();

signals:
    void logoutRequested();

private slots:
    void loadAnnouncements();
    void reloadActivities();
    void reloadEnrollments();
    void reloadStats();

    void onActivitySelected(const QItemSelection &selected);
    void onSubmitActivity();
    void onApprove();
    void onReject();
    void onDelete();

    void onEnroll();
    void onCancelEnroll();
    void onWaitlist();
    void onCheckConflict();
    void onExportMyEnroll();

    void onExportCsv();
    void onRunReport();
    void onReportFinished(const QString &path);
    void onConflictResult(const QString &result);
    void onLogout();
    void logAudit(const QString &action, const QString &target = QString(), const QString &detail = QString());

private:
    void setupUiState();
    void bindModels();
    void fillFormFromSelection();
    bool saveActivity(bool isNew);
    int selectedActivityId(const QTableView *view) const;
    bool hasCapacity(int activityId, int capacity);

    Ui::MainWindow *ui;
    UserInfo m_user;
    DbManager m_db;
    ActivityModel *m_activityModel;
    EnrollmentModel *m_enrollmentModel;
    EnrollmentModel *m_waitlistModel;
    QSqlQueryModel *m_upcomingModel;
    QSqlQueryModel *m_reportPreviewModel;
    QThread m_workerThread;
    ReportWorker *m_reportWorker;
    NetworkService m_network;
};

