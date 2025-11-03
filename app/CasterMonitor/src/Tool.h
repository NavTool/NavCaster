#pragma once

#include <QObject>
#include <QtQml/qqml.h>
#include "stdafx.h"

#include "nlohmann/json.hpp"

using json = nlohmann::json;






 nlohmann::json variantToJson(const QVariant &value);
 nlohmann::json variantMapToJson(const QVariantMap &map);
 nlohmann::json variantListToJson(const QList<QVariantMap> &list);
 nlohmann::json QStringToJson(const QString &str);

 QVariant JsonToQVariant(const nlohmann::json &jsonValue);
 QVariantMap JsonToQVariantMap(const nlohmann::json &jsonObj);
 QList<QVariant> JsonToQVariantList(const nlohmann::json &jsonArray);
 QString JsonToQString(const nlohmann::json &json);


