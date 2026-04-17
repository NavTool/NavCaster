#include "HttpClient.h"

HttpClient::HttpClient(QObject *parent)
    : QObject(parent)
    , _nam(new QNetworkAccessManager(this))
{
}

void HttpClient::setBaseUrl(const QString &url)
{
    _baseUrl = url;
    while (_baseUrl.endsWith('/'))
        _baseUrl.chop(1);
}

void HttpClient::login(const QString &username, const QString &password)
{
    QJsonObject body;
    body["username"] = username;
    body["password"] = password;
    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);

    post("/api/auth/login", data, [this](bool success, int statusCode, const QByteArray &respBody) {
        if (success && statusCode == 200)
        {
            QJsonDocument doc = QJsonDocument::fromJson(respBody);
            if (doc.isObject())
            {
                QString token = doc.object().value("token").toString();
                if (!token.isEmpty())
                {
                    _token = token;
                    emit loginSuccess();
                    return;
                }
            }
        }
        QJsonDocument doc = QJsonDocument::fromJson(respBody);
        QString error = doc.isObject()
                            ? doc.object().value("error").toString("Login failed")
                            : "Login failed";
        emit loginFailed(error);
    });
}

void HttpClient::logout()
{
    if (_token.isEmpty())
    {
        emit logoutFinished();
        return;
    }

    post("/api/auth/logout", QByteArray(), [this](bool, int, const QByteArray &) {
        _token.clear();
        emit logoutFinished();
    });
}

QNetworkRequest HttpClient::makeRequest(const QString &path) const
{
    QUrl url(_baseUrl + path);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!_token.isEmpty())
    {
        request.setRawHeader("Authorization", ("Bearer " + _token).toUtf8());
    }
    return request;
}

void HttpClient::handleReply(QNetworkReply *reply, ResponseCallback callback)
{
    connect(reply, &QNetworkReply::finished, this, [reply, cb = std::move(callback)]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray body = reply->readAll();
        bool success = (reply->error() == QNetworkReply::NoError);
        reply->deleteLater();
        if (cb)
            cb(success, statusCode, body);
    });
}

void HttpClient::get(const QString &path, ResponseCallback callback)
{
    auto *reply = _nam->get(makeRequest(path));
    handleReply(reply, std::move(callback));
}

void HttpClient::post(const QString &path, const QByteArray &body, ResponseCallback callback)
{
    auto *reply = _nam->post(makeRequest(path), body);
    handleReply(reply, std::move(callback));
}

void HttpClient::put(const QString &path, const QByteArray &body, ResponseCallback callback)
{
    auto *reply = _nam->put(makeRequest(path), body);
    handleReply(reply, std::move(callback));
}

void HttpClient::del(const QString &path, ResponseCallback callback)
{
    auto *reply = _nam->deleteResource(makeRequest(path));
    handleReply(reply, std::move(callback));
}
