#include "KernelBinValidator.h"
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>

QString KernelBinValidator::s_lastError;

const QByteArray KernelBinValidator::FF7_KERNEL_MAGIC = "FF7KERNEL";
const QByteArray KernelBinValidator::KERNEL2B_MAGIC = "KERNEL2.B";
const int KernelBinValidator::MIN_KERNEL_SIZE = 0x20;
const int KernelBinValidator::MAX_SECTIONS = 16;
const int KernelBinValidator::SECTION_OFFSET_START = 0x0C;

KernelBinValidator::ValidationReport KernelBinValidator::validateKernelBin(const QByteArray& data)
{
    ValidationReport report;
    s_lastError.clear();
    
    // Check minimum size
    if (data.size() < MIN_KERNEL_SIZE) {
        report.errors.append("KERNEL.BIN too small for header");
        s_lastError = "KERNEL.BIN too small for header";
        return report;
    }
    
    // Validate magic
    if (!isValidMagic(data)) {
        report.errors.append("Invalid KERNEL.BIN magic signature");
        s_lastError = "Invalid KERNEL.BIN magic signature";
        return report;
    }
    
    // Get version and section count
    quint8 version = static_cast<quint8>(data[0x09]);
    quint8 sectionCount = static_cast<quint8>(data[0x0A]);
    
    report.version = QString::number(version);
    report.sectionCount = sectionCount;
    
    if (sectionCount == 0 || sectionCount > MAX_SECTIONS) {
        report.errors.append(QString("Invalid section count: %1").arg(sectionCount));
        s_lastError = QString("Invalid section count: %1").arg(sectionCount);
        return report;
    }
    
    // Validate section structure
    if (!isValidSectionStructure(data)) {
        report.errors.append("Invalid section structure");
        return report;
    }
    
    // Validate section boundaries
    if (!validateSectionBoundaries(data)) {
        report.errors.append("Invalid section boundaries");
        return report;
    }
    
    // Extract section information
    for (int i = 0; i < sectionCount; i++) {
        quint32 offset = *reinterpret_cast<const quint32*>(data.constData() + SECTION_OFFSET_START + (i * 4));
        QString sectionInfo = QString("Section %1: offset=0x%2").arg(i).arg(offset, 8, 16, QChar('0'));
        report.sectionInfo[i] = sectionInfo;
        
        // Check for potential issues
        if (offset == 0) {
            report.warnings.append(QString("Section %1 has zero offset").arg(i));
        }
        if (offset >= data.size()) {
            report.warnings.append(QString("Section %1 offset beyond file size").arg(i));
        }
    }
    
    report.isValid = true;
    return report;
}

bool KernelBinValidator::isValidMagic(const QByteArray& data)
{
    if (data.size() < 9) return false;
    
    QByteArray magic = data.left(9);
    return magic == FF7_KERNEL_MAGIC || magic == KERNEL2B_MAGIC;
}

bool KernelBinValidator::isValidSectionStructure(const QByteArray& data)
{
    if (data.size() < SECTION_OFFSET_START + 4) return false;
    
    quint8 sectionCount = static_cast<quint8>(data[0x0A]);
    int requiredSize = SECTION_OFFSET_START + (sectionCount * 4);
    
    if (data.size() < requiredSize) {
        s_lastError = QString("File too small for %1 sections").arg(sectionCount);
        return false;
    }
    
    return true;
}

bool KernelBinValidator::validateSectionBoundaries(const QByteArray& data)
{
    quint8 sectionCount = static_cast<quint8>(data[0x0A]);
    QVector<quint32> offsets;
    
    // Extract all section offsets
    for (int i = 0; i < sectionCount; i++) {
        quint32 offset = *reinterpret_cast<const quint32*>(data.constData() + SECTION_OFFSET_START + (i * 4));
        offsets.append(offset);
    }
    
    // Validate offsets are in ascending order (except for first section which might be at 0)
    for (int i = 1; i < offsets.size(); i++) {
        if (offsets[i] < offsets[i-1]) {
            s_lastError = QString("Section %1 offset (%2) is before section %3 offset (%4)")
                          .arg(i).arg(offsets[i], 8, 16, QChar('0'))
                          .arg(i-1).arg(offsets[i-1], 8, 16, QChar('0'));
            return false;
        }
    }
    
    // Validate offsets are within file bounds
    for (int i = 0; i < offsets.size(); i++) {
        if (offsets[i] > data.size()) {
            s_lastError = QString("Section %1 offset (%2) beyond file size (%3)")
                          .arg(i).arg(offsets[i], 8, 16, QChar('0'))
                          .arg(data.size());
            return false;
        }
    }
    
    return true;
}

QByteArray KernelBinValidator::calculateChecksum(const QByteArray& data)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(data);
    return hash.result().toHex();
}

QString KernelBinValidator::createBackup(const QString& filePath)
{
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        s_lastError = "Original file does not exist: " + filePath;
        return QString();
    }
    
    // Create backup directory
    QString backupDir = fileInfo.path() + "/backups";
    QDir dir(backupDir);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            s_lastError = "Failed to create backup directory: " + backupDir;
            return QString();
        }
    }
    
    // Generate backup filename with timestamp
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString backupPath = backupDir + "/" + fileInfo.baseName() + "_" + timestamp + "." + fileInfo.suffix();
    
    // Copy original file to backup location
    QFile originalFile(filePath);
    if (!originalFile.copy(backupPath)) {
        s_lastError = "Failed to create backup: " + originalFile.errorString();
        return QString();
    }
    
    qDebug() << "Created backup:" << backupPath;
    return backupPath;
}

bool KernelBinValidator::restoreBackup(const QString& originalPath, const QString& backupPath)
{
    QFile backupFile(backupPath);
    if (!backupFile.exists()) {
        s_lastError = "Backup file does not exist: " + backupPath;
        return false;
    }
    
    // Create backup of current file before restoring
    QString currentBackup = createBackup(originalPath);
    if (currentBackup.isEmpty()) {
        qWarning() << "Warning: Could not backup current file before restore";
    }
    
    // Remove original file
    QFile::remove(originalPath);
    
    // Copy backup to original location
    if (!backupFile.copy(originalPath)) {
        s_lastError = "Failed to restore backup: " + backupFile.errorString();
        return false;
    }
    
    qDebug() << "Restored backup to:" << originalPath;
    return true;
}

QVector<KernelBinValidator::BackupInfo> KernelBinValidator::getAvailableBackups(const QString& originalPath)
{
    QVector<BackupInfo> backups;
    
    QFileInfo fileInfo(originalPath);
    QString backupDir = fileInfo.path() + "/backups";
    QDir dir(backupDir);
    
    if (!dir.exists()) {
        return backups;
    }
    
    QString baseName = fileInfo.baseName() + "_";
    QStringList filters = {baseName + "*." + fileInfo.suffix()};
    QStringList backupFiles = dir.entryList(filters, QDir::Files, QDir::Time);
    
    for (const QString& backupFile : backupFiles) {
        BackupInfo info;
        info.originalPath = originalPath;
        info.backupPath = backupDir + "/" + backupFile;
        
        // Extract timestamp from filename
        QString timestampStr = backupFile;
        timestampStr.remove(baseName);
        timestampStr.remove("." + fileInfo.suffix());
        info.timestamp = QDateTime::fromString(timestampStr, "yyyyMMdd_hhmmss");
        
        // Calculate checksum
        QFile file(info.backupPath);
        if (file.open(QIODevice::ReadOnly)) {
            info.checksum = calculateChecksum(file.readAll());
            file.close();
        }
        
        backups.append(info);
    }
    
    return backups;
}

bool KernelBinValidator::cleanupOldBackups(const QString& originalPath, int maxBackups)
{
    QVector<BackupInfo> backups = getAvailableBackups(originalPath);
    
    if (backups.size() <= maxBackups) {
        return true; // No cleanup needed
    }
    
    // Remove oldest backups (keep the most recent ones)
    int toRemove = backups.size() - maxBackups;
    for (int i = backups.size() - 1; i >= backups.size() - toRemove; i--) {
        QFile::remove(backups[i].backupPath);
        qDebug() << "Removed old backup:" << backups[i].backupPath;
    }
    
    return true;
}

QString KernelBinValidator::getLastError()
{
    return s_lastError;
}
