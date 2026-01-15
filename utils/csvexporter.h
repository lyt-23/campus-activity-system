#pragma once

#include <QString>
#include <QVector>
#include <QStringList>

class CsvExporter
{
public:
    static bool write(const QString &path, const QVector<QStringList> &rows, QString *error = nullptr);
};

