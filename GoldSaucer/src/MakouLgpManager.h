#pragma once

#include <QString>
#include <QByteArray>
#include <Lgp>

/**
 * Makou-inspired LGP manager using ff7tk library
 * This replaces our manual LGP parsing/rebuilding with Makou's proven approach
 */
class MakouLgpManager
{
public:
    explicit MakouLgpManager();
    ~MakouLgpManager();

    // Open LGP file
    bool open(const QString &lgpPath);
    
    // Close LGP file (saves changes)
    void close();
    
    // Check if LGP is open
    bool isOpen() const;
    
    // Get list of all files in LGP
    QStringList fileList() const;
    
    // Check if file exists in LGP
    bool fileExists(const QString &fileName) const;
    
    // Get file data from LGP
    QByteArray fileData(const QString &fileName);
    
    // Update file in LGP (or add if doesn't exist)
    bool setFileData(const QString &fileName, const QByteArray &data);
    
    // Add new file to LGP
    bool addFile(const QString &fileName, const QByteArray &data);
    
    // Remove file from LGP
    bool removeFile(const QString &fileName);
    
    // Save LGP to specific path
    bool save(const QString &outputPath);
    
    // Get error information
    QString lastError() const;

private:
    Lgp _lgp;
    QString _lastError;
    
    void setError(const QString &error);
};
