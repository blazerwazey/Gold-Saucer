#include "MakouLgpManager.h"
#include <QDebug>

MakouLgpManager::MakouLgpManager()
{
}

MakouLgpManager::~MakouLgpManager()
{
    if (isOpen()) {
        close();
    }
}

bool MakouLgpManager::open(const QString &lgpPath)
{
    _lastError.clear();
    
    try {
        // Use ff7tk's Lgp class like Makou does
        _lgp.setFileName(lgpPath);
        
        if (!_lgp.isOpen() && !_lgp.open()) {
            setError(QString("Failed to open LGP file: %1").arg(lgpPath));
            return false;
        }
        
        qDebug() << "MakouLgpManager: Successfully opened LGP:" << lgpPath;
        return true;
        
    } catch (const std::exception &e) {
        setError(QString("Exception opening LGP: %1").arg(e.what()));
        return false;
    }
}

void MakouLgpManager::close()
{
    if (isOpen()) {
        _lgp.close(); // ff7tk automatically saves when closing
        qDebug() << "MakouLgpManager: LGP closed and saved";
    }
}

bool MakouLgpManager::isOpen() const
{
    return _lgp.isOpen();
}

QStringList MakouLgpManager::fileList() const
{
    if (!isOpen()) {
        return QStringList();
    }
    
    return _lgp.fileList();
}

bool MakouLgpManager::fileExists(const QString &fileName) const
{
    if (!isOpen()) {
        return false;
    }
    
    return _lgp.fileExists(fileName);
}

QByteArray MakouLgpManager::fileData(const QString &fileName)
{
    if (!isOpen()) {
        _lastError = "LGP is not open";
        return QByteArray();
    }
    
    // Check modified data first so in-memory edits (key item swaps, etc.)
    // are visible to later processing passes.
    QByteArray data = _lgp.modifiedFileData(fileName);
    if (data.isEmpty()) {
        data = _lgp.fileData(fileName);
    }
    
    if (data.isEmpty()) {
        _lastError = QString("File not found or empty: %1").arg(fileName);
    }
    
    return data;
}

bool MakouLgpManager::setFileData(const QString &fileName, const QByteArray &data)
{
    if (!isOpen()) {
        setError("LGP is not open");
        return false;
    }

    try {
        // Archive::setFileData creates a stack QBuffer whose pointer becomes
        // dangling before pack() reads it -> access violation in ff7tkFormats.dll.
        // Use a heap-allocated QBuffer (like addFile does) so the pointer stays
        // valid until pack() is called and the Lgp object is destroyed.
        QBuffer *buffer = new QBuffer();
        buffer->setData(data);
        buffer->open(QIODevice::ReadOnly);

        bool success = _lgp.setFile(fileName, buffer);

        if (!success) {
            delete buffer;
            setError(QString("Failed to set file data: %1").arg(fileName));
            return false;
        }

        // buffer ownership transferred to / kept alive by the Lgp entry.
        // Do NOT delete it here.
        qDebug() << "MakouLgpManager: Successfully updated file:" << fileName
                 << "size:" << data.size() << "bytes";
        return true;

    } catch (const std::exception &e) {
        setError(QString("Exception setting file data: %1").arg(e.what()));
        return false;
    }
}

bool MakouLgpManager::addFile(const QString &fileName, const QByteArray &data)
{
    if (!isOpen()) {
        setError("LGP is not open");
        return false;
    }
    
    try {
        // Create a QIODevice with the data (like Makou does)
        QBuffer *buffer = new QBuffer();
        buffer->setData(data);
        buffer->open(QIODevice::ReadOnly);
        
        bool success = _lgp.addFile(fileName, buffer);
        
        if (!success) {
            setError(QString("Failed to add file: %1").arg(fileName));
            delete buffer;
            return false;
        }
        
        qDebug() << "MakouLgpManager: Successfully added file:" << fileName 
                 << "size:" << data.size() << "bytes";
        return true;
        
    } catch (const std::exception &e) {
        setError(QString("Exception adding file: %1").arg(e.what()));
        return false;
    }
}

bool MakouLgpManager::removeFile(const QString &fileName)
{
    if (!isOpen()) {
        setError("LGP is not open");
        return false;
    }
    
    try {
        bool success = _lgp.removeFile(fileName);
        
        if (!success) {
            setError(QString("Failed to remove file: %1").arg(fileName));
            return false;
        }
        
        qDebug() << "MakouLgpManager: Successfully removed file:" << fileName;
        return true;
        
    } catch (const std::exception &e) {
        setError(QString("Exception removing file: %1").arg(e.what()));
        return false;
    }
}

bool MakouLgpManager::save(const QString &outputPath)
{
    qDebug() << "MakouLgpManager::save() called with outputPath:" << outputPath;
    
    if (!isOpen()) {
        setError("LGP is not open");
        return false;
    }
    
    try {
        // Debug: Show what paths we're comparing
        QString currentPath = _lgp.fileName();
        qDebug() << "MakouLgpManager: Current path:" << currentPath;
        qDebug() << "MakouLgpManager: Output path:" << outputPath;
        qDebug() << "MakouLgpManager: Paths are different:" << (outputPath != currentPath);
        
        // If outputPath is different from current path, we need to save as
        if (outputPath != currentPath) {
            qDebug() << "MakouLgpManager: Saving to different path:" << outputPath;

            // ff7tk's Lgp::pack() crashes (access violation in ff7tkFormats.dll)
            // when the destination archive already exists and cannot be
            // replaced - it writes a .temp file then faults during the
            // rename/replace. Remove any existing destination and stale .temp
            // BEFORE packing, and abort cleanly if the destination is locked
            // (commonly by 7th Heaven / a running FF7 or mod tool).
            if (QFile::exists(outputPath)) {
                if (!QFile::remove(outputPath)) {
                    setError(QString(
                        "Cannot overwrite '%1' - the file is locked by another "
                        "process. Close 7th Heaven, the game, and any FF7 mod "
                        "tools, then try again.").arg(outputPath));
                    return false;
                }
            }
            const QString tempPath = outputPath + ".temp";
            if (QFile::exists(tempPath)) {
                QFile::remove(tempPath);
            }

            // Use ff7tk's pack method like Makou does
            if (!_lgp.pack(outputPath, nullptr)) {
                setError(QString("Failed to pack LGP to: %1").arg(outputPath));
                return false;
            }
            
            qDebug() << "MakouLgpManager: Successfully packed to different path:" << outputPath;
            return true;
        }
        
        // ff7tk automatically saves when close() is called
        qDebug() << "MakouLgpManager: LGP will be saved on close";
        return true;
        
    } catch (const std::exception &e) {
        setError(QString("Exception saving LGP: %1").arg(e.what()));
        return false;
    }
}

QString MakouLgpManager::lastError() const
{
    return _lastError;
}

void MakouLgpManager::setError(const QString &error)
{
    _lastError = error;
    qDebug() << "MakouLgpManager Error:" << error;
}
