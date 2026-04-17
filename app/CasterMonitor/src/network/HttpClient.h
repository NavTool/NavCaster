#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <functional>

class HttpClient : public QObject
{
    Q_OBJECT
public:
    explicit HttpClient(QObject *parent = nullptr);

    void setBaseUrl(const QString &url);
    QString baseUrl() const { return _baseUrl; }

    void setToken(const QString &token) { _token = token; }
    QString token() const { return _token; }
    bool isAuthenticated() const { return !_token.isEmpty(); }

    void login(const QString &username, const QString &password);
    void logout();

    using ResponseCallback = std::function<void(bool success, int statusCode, const QByteArray &body)>;

    void get(const QString &path, ResponseCallback callback);
    void post(const QString &path, const QByteArray &body, ResponseCallback callback);
    void put(const QString &path, const QByteArray &body, ResponseCallback callback);
    void del(const QString &path, ResponseCallback callback);

signals:
    void loginSuccess();
    void loginFailed(const QString &error);
    void logoutFinished();

private:
    QNetworkRequest makeRequest(const QString &path) const;
    void handleReply(QNetworkReply *reply, ResponseCallback callback);

    QNetworkAccessManager *_nam;
    QString _baseUrl;
    QString _token;
};
