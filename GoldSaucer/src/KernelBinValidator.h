#pragma once

#include <QString>
#include <QByteArray>
#include <QVector>
#include <QMap>
#include <QDateTime>

// KERNEL.BIN validation and backup system
class KernelBinValidator
{
public:
    struct ValidationReport {
        bool isValid;
        QStringList errors;
        QStringList warnings;
        QString version;
        int sectionCount;
        QMap<int, QString> sectionInfo;
        
        ValidationReport() : isValid(false), sectionCount(0) {}
    };
    
    struct BackupInfo {
        QString originalPath;
        QString backupPath;
        QDateTime timestamp;
        QByteArray checksum;
        
        BackupInfo() : timestamp(QDateTime::currentDateTime()) {}
    };
    
    static ValidationReport validateKernelBin(const QByteArray& data);
    static bool isValidMagic(const QByteArray& data);
    static bool isValidSectionStructure(const QByteArray& data);
    static bool validateSectionBoundaries(const QByteArray& data);
    static QByteArray calculateChecksum(const QByteArray& data);
    
    static QString createBackup(const QString& filePath);
    static bool restoreBackup(const QString& originalPath, const QString& backupPath);
    static QVector<BackupInfo> getAvailableBackups(const QString& originalPath);
    static bool cleanupOldBackups(const QString& originalPath, int maxBackups = 5);
    
    static QString getLastError();
    
private:
    static QString s_lastError;
    
    static const QByteArray FF7_KERNEL_MAGIC;
    static const QByteArray KERNEL2B_MAGIC;
    static const int MIN_KERNEL_SIZE;
    static const int MAX_SECTIONS;
    static const int SECTION_OFFSET_START;
};
