#include "csvexporter.h"

#include <QFile>
#include <QTextStream>

bool CsvExporter::write(const QString &path, const QVector<QStringList> &rows, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }

    QTextStream out(&file);
    // ================ Qt6.9.2 正确写法 ================
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
// Qt6 写法 - 使用 QStringConverter::Encoding
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Qt6.8+ 支持 QTextStream::setEncoding
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true);
#else
    // Qt6.0-6.7 的写法
    out.setEncoding(QStringConverter::Utf8);
#endif
#else
    // Qt5 兼容写法
    out.setCodec("UTF-8");
    out.setGenerateByteOrderMark(true);
#endif
    // =================================================

    for (const auto &row : rows) {
        QStringList escaped;
        for (const QString &cell : row) {
            QString val = cell;
            if (val.contains('"') || val.contains(',')) {
                val.replace("\"", "\"\"");
                val = "\"" + val + "\"";
            }
            escaped << val;
        }
        out << escaped.join(',') << '\n';
    }

    // 确保写入完成
    out.flush();
    file.close();

    return !out.status();
}
