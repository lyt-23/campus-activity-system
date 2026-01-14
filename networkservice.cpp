#include "networkservice.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QSslSocket>

namespace {
const QUrl kAnnouncementUrl(QStringLiteral("https://raw.githubusercontent.com/public-apis/public-apis/master/README.md"));
const QUrl kCategoryUrl(QStringLiteral("https://raw.githubusercontent.com/aoapc-book/aoapc-bac2nd/master/README.md"));
}

NetworkService::NetworkService(QObject *parent)
    : QObject(parent)
{
    connect(&m_manager, &QNetworkAccessManager::finished,
            this, [this](QNetworkReply *reply) {
        if (reply->url() == kAnnouncementUrl) {
            handleAnnouncements(reply);
        } else if (reply->url() == kCategoryUrl) {
            handleCategories(reply);
        } else {
            reply->deleteLater();
        }
    });
}

void NetworkService::fetchAnnouncements()
{
    if (!m_enabled || !QSslSocket::supportsSsl()) {
        emit error(tr("SSL 不可用，使用本地公告"));
        emit announcementsReady(QStringList() << tr("欢迎使用校园活动系统") << tr("公告获取失败，当前离线模式"));
        return;
    }
    QNetworkRequest req(kAnnouncementUrl);
    m_manager.get(req);
}

void NetworkService::fetchCategories()
{
    if (!m_enabled || !QSslSocket::supportsSsl()) {
        emit error(tr("SSL 不可用，使用本地类别"));
        emit categoriesReady(QStringList() << tr("社团") << tr("学术") << tr("体育") << tr("公益"));
        return;
    }
    QNetworkRequest req(kCategoryUrl);
    m_manager.get(req);
}

void NetworkService::handleAnnouncements(QNetworkReply *reply)
{
    const QByteArray data = reply->readAll();
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        emit error(tr("获取公告失败: %1").arg(reply->errorString()));
        emit announcementsReady(QStringList() << tr("公告获取失败（使用本地占位）") << tr("欢迎使用校园活动系统"));
        return;
    }
    const QString text = QString::fromUtf8(data);
    QStringList lines;
    for (const QString &line : text.split('\n')) {
        if (line.startsWith("# ")) {
            lines << line.mid(2).trimmed();
        }
        if (lines.size() >= 10) break;
    }
    if (lines.isEmpty())
        lines << tr("暂无公告");
    emit announcementsReady(lines);
}

void NetworkService::handleCategories(QNetworkReply *reply)
{
    const QByteArray data = reply->readAll();
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        emit error(tr("获取类别失败: %1").arg(reply->errorString()));
        emit categoriesReady(QStringList() << tr("社团") << tr("学术") << tr("体育") << tr("公益"));
        return;
    }
    QStringList cats;
    for (const QString &line : QString::fromUtf8(data).split('\n')) {
        if (line.startsWith("* ")) {
            QString name = line.mid(2).trimmed();
            if (name.size() > 2 && name.size() < 28)
                cats << name;
        }
        if (cats.size() >= 8) break;
    }
    if (cats.isEmpty()) {
        cats << tr("社团") << tr("学术") << tr("体育") << tr("公益");
    }
    emit categoriesReady(cats);
}

