#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSslSocket>

class NetworkService : public QObject
{
    Q_OBJECT
public:
    explicit NetworkService(QObject *parent = nullptr);

    void fetchAnnouncements();
    void fetchCategories();
    void setNetworkEnabled(bool enabled) { m_enabled = enabled; }

signals:
    void announcementsReady(const QStringList &items);
    void categoriesReady(const QStringList &items);
    void error(const QString &message);

private slots:
    void handleAnnouncements(QNetworkReply *reply);
    void handleCategories(QNetworkReply *reply);

private:
    QNetworkAccessManager m_manager;
    bool m_enabled { true };
};

