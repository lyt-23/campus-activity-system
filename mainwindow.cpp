#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QStandardPaths>
#include <QDir>
#include <QMessageBox>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QFileDialog>
#include <QTableView>
#include <QDebug>
#include <QTimer>
#include <QFile>
#include <QHeaderView>

MainWindow::MainWindow(const UserInfo &user, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_user(user)
    , m_activityModel(nullptr)
    , m_enrollmentModel(nullptr)
    , m_waitlistModel(nullptr)
    , m_upcomingModel(new QSqlQueryModel(this))
    , m_reportPreviewModel(new QSqlQueryModel(this))
    , m_reportWorker(new ReportWorker)
{
    ui->setupUi(this);
    setWindowTitle(tr("校园活动管理 - %1 (%2)").arg(user.username, user.role));

    const QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QDir::separator() + "activity.db";
    QDir().mkpath(QFileInfo(dbPath).absolutePath());

    auto openOrReset = [this](const QString &p)->bool {
        if (m_db.open(p) && m_db.initSchema()) return true;
        QFile::remove(p);
        return m_db.open(p) && m_db.initSchema();
    };
    if (!openOrReset(dbPath)) {
        QMessageBox::critical(this, tr("错误"), tr("数据库打开或初始化失败: %1").arg(m_db.lastErrorText()));
        QTimer::singleShot(0, this, &MainWindow::close);
        return;
    }

    setupUiState();
    bindModels();

    connect(ui->refreshAnnouncementsButton, &QPushButton::clicked, this, &MainWindow::loadAnnouncements);
    connect(ui->newActivityButton, &QPushButton::clicked, [this]() {
        ui->titleEdit->clear();
        ui->locationEdit->clear();
        ui->statusEdit->clear();
        ui->capacitySpin->setValue(50);
        ui->titleEdit->setProperty("activityId", QVariant());
    });
    connect(ui->submitActivityButton, &QPushButton::clicked, this, &MainWindow::onSubmitActivity);
    connect(ui->approveButton, &QPushButton::clicked, this, &MainWindow::onApprove);
    connect(ui->rejectButton, &QPushButton::clicked, this, &MainWindow::onReject);
    connect(ui->deleteButton, &QPushButton::clicked, this, &MainWindow::onDelete);

    connect(ui->enrollButton, &QPushButton::clicked, this, &MainWindow::onEnroll);
    connect(ui->cancelEnrollButton, &QPushButton::clicked, this, &MainWindow::onCancelEnroll);
    connect(ui->waitlistButton, &QPushButton::clicked, this, &MainWindow::onWaitlist);
    connect(ui->exportMyEnrollButton, &QPushButton::clicked, this, &MainWindow::onExportMyEnroll);

    connect(ui->exportCsvButton, &QPushButton::clicked, this, &MainWindow::onExportCsv);
    connect(ui->runReportButton, &QPushButton::clicked, this, &MainWindow::onRunReport);
    connect(ui->logoutButton, &QPushButton::clicked, this, &MainWindow::onLogout);

    connect(ui->categoryFilter, &QComboBox::currentTextChanged, this, &MainWindow::reloadActivities);
    connect(ui->statusFilter, &QComboBox::currentTextChanged, this, &MainWindow::reloadActivities);
    connect(ui->keywordEdit, &QLineEdit::textChanged, this, &MainWindow::reloadActivities);

    connect(ui->activityTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onActivitySelected);

    connect(&m_network, &NetworkService::announcementsReady, this, [this](const QStringList &items){
        ui->announcementList->clear();
        ui->announcementList->addItems(items);
    });
    connect(&m_network, &NetworkService::categoriesReady, this, [this](const QStringList &items){
        ui->categoryFilter->clear();
        ui->categoryEdit->clear();
        ui->categoryFilter->addItem("", "");
        for (const QString &c : items) {
            ui->categoryFilter->addItem(c, c);
            ui->categoryEdit->addItem(c);
        }
    });

    m_reportWorker->setDatabase(m_db.database());
    m_reportWorker->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::finished, m_reportWorker, &QObject::deleteLater);
    connect(this, &MainWindow::destroyed, &m_workerThread, &QThread::quit);
    connect(m_reportWorker, &ReportWorker::finished, this, &MainWindow::onReportFinished);
    connect(m_reportWorker, &ReportWorker::conflictChecked, this, &MainWindow::onConflictResult);
    m_workerThread.start();

    // 强制离线模式（避免 OpenSSL 缺失导致崩溃），使用本地占位数据
    m_network.setNetworkEnabled(false);
    ui->announcementList->clear();
    ui->announcementList->addItems(QStringList()
                                   << tr("欢迎使用校园活动系统")
                                   << tr("当前为离线模式，公告/类别使用本地数据"));
    ui->categoryFilter->clear();
    ui->categoryEdit->clear();
    const QStringList localCats { tr("社团"), tr("学术"), tr("体育"), tr("公益") };
    ui->categoryFilter->addItem("", "");
    for (const auto &c : localCats) {
        ui->categoryFilter->addItem(c, c);
        ui->categoryEdit->addItem(c);
    }

    // 如需联网获取，去掉上方 setNetworkEnabled(false) 并解除下行注释
    // loadAnnouncements();
    reloadActivities();
    reloadEnrollments();
    reloadStats();
}

MainWindow::~MainWindow()
{
    m_workerThread.quit();
    m_workerThread.wait(500);
    delete ui;
}

void MainWindow::setupUiState()
{
    const bool isAdmin = (m_user.role == "admin");
    const bool isInitiator = (m_user.role == "initiator");
    const bool isStudent = (m_user.role == "student");

    ui->statusFilter->addItems(QStringList() << "" << "pending" << "approved" << "rejected" << "cancelled");
    ui->startEdit->setDateTime(QDateTime::currentDateTime().addDays(1));
    ui->endEdit->setDateTime(QDateTime::currentDateTime().addDays(1).addSecs(3600));
    ui->userInfoLabel->setText(tr("当前用户: %1 (%2)").arg(m_user.username, m_user.role));

    // 角色隔离：学生只保留报名标签，管理员/发起人只保留活动与报表
    int idxAct = ui->tabWidget->indexOf(ui->tabActivities);
    int idxEnroll = ui->tabWidget->indexOf(ui->tabEnrollment);
    if (isStudent) {
        if (idxAct != -1) ui->tabWidget->removeTab(idxAct);
    } else {
        if (idxEnroll != -1) ui->tabWidget->removeTab(idxEnroll);
    }

    // 按角色控制按钮/表单
    ui->newActivityButton->setVisible(isInitiator);
    ui->submitActivityButton->setVisible(isInitiator);
    ui->approveButton->setVisible(isAdmin);
    ui->rejectButton->setVisible(isAdmin);
    ui->deleteButton->setVisible(isAdmin);

    ui->titleEdit->setEnabled(isInitiator);
    ui->categoryEdit->setEnabled(isInitiator);
    ui->locationEdit->setEnabled(isInitiator);
    ui->capacitySpin->setEnabled(isInitiator);
    ui->startEdit->setEnabled(isInitiator);
    ui->endEdit->setEnabled(isInitiator);

    // 报名相关仅学生可见；冲突检查改为报名时自动执行，不再单独按钮
    ui->enrollButton->setVisible(isStudent);
    ui->cancelEnrollButton->setVisible(isStudent);
    ui->waitlistButton->setVisible(isStudent);
    ui->checkConflictButton->setVisible(false);
    ui->exportMyEnrollButton->setVisible(isStudent);
    ui->exportCsvButton->setVisible(!isStudent);

    ui->upcomingTable->setModel(m_upcomingModel);
    ui->reportPreviewTable->setModel(m_reportPreviewModel);

    // 列宽自适应，提升可读性
    ui->upcomingTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->reportPreviewTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->activityTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    if (isStudent) {
        ui->enrollmentActivityTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        ui->waitlistTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    }
}

void MainWindow::bindModels()
{
    m_activityModel = new ActivityModel(this, m_db.database());
    ui->activityTable->setModel(m_activityModel);
    ui->activityTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->activityTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->activityTable->setColumnHidden(0, true);

    // 报名模型仅学生需要绑定
    if (m_user.role == "student") {
        m_enrollmentModel = new EnrollmentModel(this, m_db.database());
        ui->enrollmentActivityTable->setModel(m_enrollmentModel);
        ui->enrollmentActivityTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->enrollmentActivityTable->setSelectionMode(QAbstractItemView::SingleSelection);
        ui->enrollmentActivityTable->setColumnHidden(0, true);

        m_waitlistModel = new EnrollmentModel(this, m_db.database());
        ui->waitlistTable->setModel(m_waitlistModel);
        ui->waitlistTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->waitlistTable->setSelectionMode(QAbstractItemView::SingleSelection);
        ui->waitlistTable->setColumnHidden(0, true);
    }
}

void MainWindow::loadAnnouncements()
{
    m_network.fetchAnnouncements();
    m_network.fetchCategories();
}

void MainWindow::reloadActivities()
{
    const QString cat = ui->categoryFilter->currentData().toString();
    const QString status = ui->statusFilter->currentText();
    const QString keyword = ui->keywordEdit->text();
    m_activityModel->applyFilter(m_user.role, m_user.username, cat, status, keyword);

    // upcoming table
    QSqlQuery q(m_db.database());
    q.exec(R"(SELECT title AS 标题, start_time AS 开始, end_time AS 结束, location AS 地点, status AS 状态
              FROM activities WHERE status!='cancelled' ORDER BY start_time LIMIT 20)");
    m_upcomingModel->setQuery(q);
}

void MainWindow::reloadEnrollments()
{
    if (m_user.role != "student") {
        return;
    }
    m_enrollmentModel->loadAvailableActivities();
    m_waitlistModel->loadMyEnrollments(m_user.username, false);
}

void MainWindow::reloadStats()
{
    QSqlQuery q(m_db.database());
    q.exec("SELECT COUNT(*) FROM activities");
    if (q.next()) ui->labelTotalAct->setText(tr("活动总数: %1").arg(q.value(0).toInt()));
    q.exec("SELECT COUNT(*) FROM enrollments WHERE status='active'");
    if (q.next()) ui->labelTotalEnroll->setText(tr("报名总数: %1").arg(q.value(0).toInt()));
    q.exec("SELECT COUNT(*) FROM activities WHERE status='approved'");
    if (q.next()) ui->labelApproved->setText(tr("已审核: %1").arg(q.value(0).toInt()));
    q.exec("SELECT COUNT(*) FROM activities WHERE status='pending'");
    if (q.next()) ui->labelPending->setText(tr("待审核: %1").arg(q.value(0).toInt()));

    q.exec(R"(SELECT a.title AS 活动, COUNT(*) AS 报名人数
              FROM enrollments e JOIN activities a ON e.activity_id=a.id
              WHERE e.status='active'
              GROUP BY a.id
              ORDER BY 报名人数 DESC
              LIMIT 10)");
    m_reportPreviewModel->setQuery(q);
}

void MainWindow::onActivitySelected(const QItemSelection &selected)
{
    Q_UNUSED(selected);
    fillFormFromSelection();
}

void MainWindow::fillFormFromSelection()
{
    const int row = ui->activityTable->currentIndex().row();
    if (row < 0) {
        ui->titleEdit->clear();
        ui->locationEdit->clear();
        ui->capacitySpin->setValue(50);
        ui->statusEdit->clear();
        return;
    }
    auto idx = m_activityModel->index(row, 0);
    ui->titleEdit->setText(m_activityModel->data(m_activityModel->index(row, 1)).toString());
    ui->categoryEdit->setCurrentText(m_activityModel->data(m_activityModel->index(row, 2)).toString());
    ui->locationEdit->setText(m_activityModel->data(m_activityModel->index(row, 3)).toString());
    ui->startEdit->setDateTime(QDateTime::fromString(m_activityModel->data(m_activityModel->index(row, 4)).toString(), Qt::ISODate));
    ui->endEdit->setDateTime(QDateTime::fromString(m_activityModel->data(m_activityModel->index(row, 5)).toString(), Qt::ISODate));
    ui->capacitySpin->setValue(m_activityModel->data(m_activityModel->index(row, 6)).toInt());
    ui->statusEdit->setText(m_activityModel->data(m_activityModel->index(row, 8)).toString());
    ui->titleEdit->setProperty("activityId", idx.data());
}

bool MainWindow::saveActivity(bool isNew)
{
    if (m_user.role != "initiator") {
        QMessageBox::warning(this, tr("权限"), tr("仅发起人可发布/编辑活动"));
        return false;
    }
    const QString title = ui->titleEdit->text().trimmed();
    if (title.isEmpty()) {
        QMessageBox::warning(this, tr("校验"), tr("标题不能为空"));
        return false;
    }
    if (ui->capacitySpin->value() <= 0) {
        QMessageBox::warning(this, tr("校验"), tr("容量必须大于 0"));
        return false;
    }
    if (ui->endEdit->dateTime() <= ui->startEdit->dateTime()) {
        QMessageBox::warning(this, tr("校验"), tr("结束时间必须晚于开始时间"));
        return false;
    }
    if (ui->startEdit->dateTime() < QDateTime::currentDateTime().addSecs(-60)) {
        QMessageBox::warning(this, tr("校验"), tr("开始时间不能早于当前时间"));
        return false;
    }
    QSqlQuery q(m_db.database());
    if (isNew) {
        q.prepare(R"(INSERT INTO activities(title, category, location, start_time, end_time, capacity, status, creator)
                     VALUES(?,?,?,?,?,?, 'pending', ?))");
        q.addBindValue(title);
        q.addBindValue(ui->categoryEdit->currentText());
        q.addBindValue(ui->locationEdit->text());
        q.addBindValue(ui->startEdit->dateTime().toString(Qt::ISODate));
        q.addBindValue(ui->endEdit->dateTime().toString(Qt::ISODate));
        q.addBindValue(ui->capacitySpin->value());
        q.addBindValue(m_user.username);
    } else {
        const int id = ui->titleEdit->property("activityId").toInt();
        q.prepare(R"(UPDATE activities SET title=?, category=?, location=?, start_time=?, end_time=?, capacity=? WHERE id=?)");
        q.addBindValue(title);
        q.addBindValue(ui->categoryEdit->currentText());
        q.addBindValue(ui->locationEdit->text());
        q.addBindValue(ui->startEdit->dateTime().toString(Qt::ISODate));
        q.addBindValue(ui->endEdit->dateTime().toString(Qt::ISODate));
        q.addBindValue(ui->capacitySpin->value());
        q.addBindValue(id);
    }
    if (!q.exec()) {
        QMessageBox::critical(this, tr("数据库错误"), q.lastError().text());
        return false;
    }
    return true;
}

void MainWindow::onSubmitActivity()
{
    if (m_user.role != "initiator") {
        QMessageBox::warning(this, tr("权限"), tr("仅发起人可发布活动"));
        return;
    }
    const bool isNew = ui->titleEdit->property("activityId").isNull();
    if (saveActivity(isNew)) {
        reloadActivities();
        reloadEnrollments();
        reloadStats();
        QMessageBox::information(this, tr("成功"), tr("已提交活动"));
        ui->titleEdit->setProperty("activityId", QVariant());
        logAudit("activity_submit", ui->titleEdit->text(), isNew ? "new" : "update");
    }
}

void MainWindow::onApprove()
{
    if (m_user.role != "admin") {
        QMessageBox::warning(this, tr("权限"), tr("仅管理员可审批"));
        return;
    }
    const int id = selectedActivityId(ui->activityTable);
    if (id < 0) return;
    QSqlQuery q(m_db.database());
    q.prepare("UPDATE activities SET status='approved', approver=? WHERE id=?");
    q.addBindValue(m_user.username);
    q.addBindValue(id);
    if (!q.exec()) {
        QMessageBox::critical(this, tr("错误"), q.lastError().text());
    }
    logAudit("activity_approve", QString::number(id), QString("approver=%1").arg(m_user.username));
    reloadActivities();
    reloadStats();
}

void MainWindow::onReject()
{
    if (m_user.role != "admin") {
        QMessageBox::warning(this, tr("权限"), tr("仅管理员可审批"));
        return;
    }
    const int id = selectedActivityId(ui->activityTable);
    if (id < 0) return;
    QSqlQuery q(m_db.database());
    q.prepare("UPDATE activities SET status='rejected' WHERE id=?");
    q.addBindValue(id);
    if (!q.exec()) {
        QMessageBox::critical(this, tr("错误"), q.lastError().text());
    }
    logAudit("activity_reject", QString::number(id));
    reloadActivities();
}

void MainWindow::onDelete()
{
    if (m_user.role != "admin") {
        QMessageBox::warning(this, tr("权限"), tr("仅管理员可删除"));
        return;
    }
    const int id = selectedActivityId(ui->activityTable);
    if (id < 0) return;
    if (QMessageBox::question(this, tr("确认"), tr("删除该活动?")) != QMessageBox::Yes) return;
    QSqlQuery q(m_db.database());
    q.prepare("DELETE FROM activities WHERE id=?");
    q.addBindValue(id);
    q.exec();
    logAudit("activity_delete", QString::number(id));
    reloadActivities();
    reloadEnrollments();
    reloadStats();
}

int MainWindow::selectedActivityId(const QTableView *view) const
{
    const int row = view->currentIndex().row();
    if (row < 0) return -1;
    return view->model()->index(row, 0).data().toInt();
}

bool MainWindow::hasCapacity(int activityId, int capacity)
{
    QSqlQuery q(m_db.database());
    q.prepare("SELECT COUNT(*) FROM enrollments WHERE activity_id=? AND status='active'");
    q.addBindValue(activityId);
    q.exec();
    int cnt = 0;
    if (q.next()) cnt = q.value(0).toInt();
    return cnt < capacity;
}

void MainWindow::onEnroll()
{
    if (m_user.role != "student") {
        QMessageBox::warning(this, tr("权限"), tr("仅学生可报名"));
        return;
    }
    const int id = selectedActivityId(ui->enrollmentActivityTable);
    if (id < 0) return;

    // 检查是否已报名或候补同一活动
    QSqlQuery check(m_db.database());
    check.prepare("SELECT status FROM enrollments WHERE activity_id=? AND student=? AND status IN ('active','waiting')");
    check.addBindValue(id);
    check.addBindValue(m_user.username);
    if (check.exec() && check.next()) {
        QMessageBox::information(this, tr("提示"), tr("你已对该活动报名或在候补队列中，不能重复报名/候补"));
        return;
    }

    // 获取活动时间与容量
    QSqlQuery info(m_db.database());
    info.prepare("SELECT start_time,end_time,capacity FROM activities WHERE id=? AND status='approved'");
    info.addBindValue(id);
    if (!info.exec() || !info.next()) {
        QMessageBox::warning(this, tr("提示"), tr("活动信息不存在或未审核通过"));
        return;
    }
    const QDateTime newStart = QDateTime::fromString(info.value(0).toString(), Qt::ISODate);
    const QDateTime newEnd = QDateTime::fromString(info.value(1).toString(), Qt::ISODate);
    const int cap = info.value(2).toInt();

    // 与已报名活动冲突检测（仅比较 active 且未取消的活动）
    QSqlQuery qConf(m_db.database());
    qConf.prepare(R"(SELECT a.title, a.start_time, a.end_time
                     FROM enrollments e
                     JOIN activities a ON e.activity_id=a.id
                     WHERE e.student=? AND e.status='active' AND a.status!='cancelled')");
    qConf.addBindValue(m_user.username);
    if (!qConf.exec()) {
        QMessageBox::warning(this, tr("错误"), qConf.lastError().text());
        return;
    }
    QStringList conflicts;
    while (qConf.next()) {
        const QString otherTitle = qConf.value(0).toString();
        const QDateTime s = QDateTime::fromString(qConf.value(1).toString(), Qt::ISODate);
        const QDateTime e = QDateTime::fromString(qConf.value(2).toString(), Qt::ISODate);
        if (!(newEnd <= s || newStart >= e)) {
            conflicts << tr("与活动「%1」时间重叠：%2-%3 与 %4-%5")
                            .arg(otherTitle,
                                 s.toString("MM-dd hh:mm"), e.toString("MM-dd hh:mm"),
                                 newStart.toString("MM-dd hh:mm"), newEnd.toString("MM-dd hh:mm"));
        }
    }
    if (!conflicts.isEmpty()) {
        QMessageBox::warning(this, tr("时间冲突"), conflicts.join("\n"));
        return;
    }

    QSqlQuery q(m_db.database());
    const bool hasSlot = hasCapacity(id, cap);
    q.prepare("INSERT INTO enrollments(activity_id, student, created_at, status, position) VALUES(?,?,?,?,?)");
    q.addBindValue(id);
    q.addBindValue(m_user.username);
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (hasSlot) {
        q.addBindValue("active");
        q.addBindValue(0);
    } else {
        // waitlist
        QSqlQuery pos(m_db.database());
        pos.prepare("SELECT COALESCE(MAX(position),0)+1 FROM enrollments WHERE activity_id=? AND status='waiting'");
        pos.addBindValue(id);
        pos.exec();
        int position = 1;
        if (pos.next()) position = pos.value(0).toInt();
        q.addBindValue("waiting");
        q.addBindValue(position);
    }
    if (!q.exec()) {
        QMessageBox::critical(this, tr("错误"), q.lastError().text());
        return;
    }
    reloadEnrollments();
    reloadStats();
    QMessageBox::information(this, tr("提示"), hasSlot ? tr("报名成功") : tr("已加入候补队列"));
    logAudit(hasSlot ? "enroll" : "waitlist", QString::number(id));
}

void MainWindow::onCancelEnroll()
{
    if (m_user.role != "student") {
        QMessageBox::warning(this, tr("权限"), tr("仅学生可操作报名/候补"));
        return;
    }
    const int enrollId = selectedActivityId(ui->waitlistTable);
    if (enrollId < 0) {
        QMessageBox::information(this, tr("提示"), tr("选择候补或报名记录后取消"));
        return;
    }
    QSqlQuery q(m_db.database());
    q.prepare("UPDATE enrollments SET status='cancelled' WHERE id=?");
    q.addBindValue(enrollId);
    q.exec();
    // 取消后尝试将候补第1位转正
    QSqlQuery actIdQ(m_db.database());
    actIdQ.prepare("SELECT activity_id FROM enrollments WHERE id=?");
    actIdQ.addBindValue(enrollId);
    int activityId = -1;
    if (actIdQ.exec() && actIdQ.next()) activityId = actIdQ.value(0).toInt();
    if (activityId > 0) {
        QSqlQuery promote(m_db.database());
        promote.prepare(R"(SELECT id FROM enrollments 
                          WHERE activity_id=? AND status='waiting' 
                          ORDER BY position LIMIT 1)");
        promote.addBindValue(activityId);
        if (promote.exec() && promote.next()) {
            int wid = promote.value(0).toInt();
            QSqlQuery upd(m_db.database());
            upd.prepare("UPDATE enrollments SET status='active', position=0 WHERE id=?");
            upd.addBindValue(wid);
            upd.exec();
        }
    }
    logAudit("enroll_cancel", QString::number(enrollId));
    reloadEnrollments();
    reloadStats();
}

void MainWindow::onWaitlist()
{
    if (m_user.role != "student") {
        QMessageBox::warning(this, tr("权限"), tr("仅学生可候补"));
        return;
    }
    const int id = selectedActivityId(ui->enrollmentActivityTable);
    if (id < 0) return;

    // 检查是否已报名或候补同一活动
    QSqlQuery check(m_db.database());
    check.prepare("SELECT status FROM enrollments WHERE activity_id=? AND student=? AND status IN ('active','waiting')");
    check.addBindValue(id);
    check.addBindValue(m_user.username);
    if (check.exec() && check.next()) {
        QMessageBox::information(this, tr("提示"), tr("你已对该活动报名或在候补队列中，不能重复候补"));
        return;
    }

    QSqlQuery pos(m_db.database());
    pos.prepare("SELECT COALESCE(MAX(position),0)+1 FROM enrollments WHERE activity_id=? AND status='waiting'");
    pos.addBindValue(id);
    pos.exec();
    int position = 1;
    if (pos.next()) position = pos.value(0).toInt();

    QSqlQuery q(m_db.database());
    q.prepare("INSERT INTO enrollments(activity_id, student, created_at, status, position) VALUES(?,?,?,?,?)");
    q.addBindValue(id);
    q.addBindValue(m_user.username);
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    q.addBindValue("waiting");
    q.addBindValue(position);
    q.exec();
    logAudit("waitlist", QString::number(id), QString("position=%1").arg(position));
    reloadEnrollments();
    QMessageBox::information(this, tr("候补"), tr("已加入候补，第 %1 位").arg(position));
}

void MainWindow::onCheckConflict()
{
    if (m_user.role != "student") {
        return;
    }
    QSqlQuery q(m_db.database());
    q.prepare(R"(SELECT a.title, a.start_time, a.end_time
              FROM enrollments e
              JOIN activities a ON e.activity_id=a.id
              WHERE e.student=? AND e.status='active' AND a.status!='cancelled'
              ORDER BY a.start_time)");
    q.addBindValue(m_user.username);
    q.exec();
    struct Item { QString title; QDateTime start; QDateTime end; };
    QList<Item> items;
    QStringList conflicts;
    while (q.next()) {
        Item cur { q.value(0).toString(),
                   QDateTime::fromString(q.value(1).toString(), Qt::ISODate),
                   QDateTime::fromString(q.value(2).toString(), Qt::ISODate) };
        for (const auto &it : items) {
            if (!(cur.end <= it.start || cur.start >= it.end)) {
                conflicts << tr("活动「%1」(%2-%3) 与 「%4」(%5-%6) 时间冲突")
                                .arg(it.title,
                                     it.start.toString("MM-dd hh:mm"), it.end.toString("MM-dd hh:mm"),
                                     cur.title,
                                     cur.start.toString("MM-dd hh:mm"), cur.end.toString("MM-dd hh:mm"));
            }
        }
        items.append(cur);
    }
    QMessageBox::information(this, tr("冲突检查"),
                             conflicts.isEmpty() ? tr("无冲突") : conflicts.join('\n'));
}

void MainWindow::onExportMyEnroll()
{
    if (m_user.role != "student") {
        QMessageBox::warning(this, tr("权限"), tr("仅学生可导出自己的报名"));
        return;
    }
    QSqlQuery q(m_db.database());
    q.prepare(R"(SELECT a.title, a.start_time, a.end_time, e.status
                FROM enrollments e
                JOIN activities a ON e.activity_id=a.id
                WHERE e.student=?)");
    q.addBindValue(m_user.username);
    q.exec();
    QVector<QStringList> rows;
    rows << QStringList{ "标题", "开始", "结束", "状态" };
    while (q.next()) {
        rows << QStringList{ q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString() };
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("导出 CSV"), QDir::homePath() + "/my_enroll.csv", "CSV (*.csv)");
    if (path.isEmpty()) return;
    QString err;
    if (CsvExporter::write(path, rows, &err)) {
        QMessageBox::information(this, tr("导出"), tr("已导出到 %1").arg(path));
        logAudit("export_my_enroll", path);
    } else {
        QMessageBox::critical(this, tr("导出失败"), err);
    }
}

void MainWindow::onExportCsv()
{
    if (m_user.role == "student") {
        QMessageBox::warning(this, tr("权限"), tr("仅管理员/发起人可导出报名列表"));
        return;
    }
    QSqlQuery q(m_db.database());
    q.exec(R"(SELECT a.title, e.student, e.status, e.position
              FROM enrollments e
              JOIN activities a ON e.activity_id=a.id
              ORDER BY a.title, e.status)");
    QVector<QStringList> rows;
    rows << QStringList{ "活动", "学生", "状态", "候补序号" };
    while (q.next()) {
        rows << QStringList{ q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString() };
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("导出 CSV"), QDir::homePath() + "/enrollments.csv", "CSV (*.csv)");
    if (path.isEmpty()) return;
    QString err;
    if (CsvExporter::write(path, rows, &err)) {
        QMessageBox::information(this, tr("导出"), tr("已导出到 %1").arg(path));
        logAudit("export_enrollments", path);
    } else {
        QMessageBox::critical(this, tr("失败"), err);
    }
}

void MainWindow::onRunReport()
{
    ui->reportStatusLabel->setText(tr("状态: 生成中..."));
    ui->runReportButton->setEnabled(false);
    QMetaObject::invokeMethod(m_reportWorker, "generateReport", Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_reportWorker, "checkConflicts", Qt::QueuedConnection);
    logAudit("report_generate");
}

void MainWindow::onReportFinished(const QString &path)
{
    ui->reportStatusLabel->setText(tr("状态: 完成"));
    ui->runReportButton->setEnabled(true);
    if (path.startsWith("导出失败")) {
        QMessageBox::critical(this, tr("报表"), path);
    } else {
        QMessageBox::information(this, tr("报表"), tr("报表已生成: %1").arg(path));
    }
}

void MainWindow::onConflictResult(const QString &result)
{
    QMessageBox::information(this, tr("全局冲突检查"), result);
}

void MainWindow::onLogout()
{
    emit logoutRequested();
    close();
}

void MainWindow::logAudit(const QString &action, const QString &target, const QString &detail)
{
    QSqlQuery q(m_db.database());
    q.prepare("INSERT INTO audit_logs(action, actor, target, detail, created_at) VALUES(?,?,?,?,?)");
    q.addBindValue(action);
    q.addBindValue(m_user.username);
    q.addBindValue(target);
    q.addBindValue(detail);
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    q.exec();
}

