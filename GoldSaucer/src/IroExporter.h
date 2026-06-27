#pragma once

#include <QString>
#include <QStringList>
#include <QByteArray>

class Config;

// IroExporter — packages Gold Saucer's randomized output into a 7th Heaven
// `.iro` (IROS) archive, IN ADDITION to the normal loose-folder output. It does
// not change any randomizer; it runs once at the end of a randomization, reading
// from the output tree (and the original flevel.lgp, to find which fields the
// field randomizer touched).
//
// Binary format + mod layout are documented in IroExporter.cpp.
class IroExporter
{
public:
    // ff7Path:    the user's FF7 install root (classic or 2026 nested layout).
    // outputPath: the folder Gold Saucer wrote its randomized files to.
    IroExporter(const QString& ff7Path, const QString& outputPath);

    // Build the archive at iroPath. Progress notes are appended to `log` (shown
    // in the GUI console). Returns false only on a hard failure (nothing to pack
    // or the archive could not be written).
    bool exportIro(const QString& iroPath, const Config& config, QStringList& log);

private:
    QString m_ff7Path;
    QString m_outputPath;
    QString m_staging;   // temp build tree under m_outputPath (cleaned afterwards)

    // Stage steps. Each returns the number of override files it added (0 = the
    // corresponding randomizer produced no output, which is fine).
    int  stageFields(QStringList& log);       // flevel.lgp/<field>.chunk.1
    int  stageWorldScript(QStringList& log);  // world_us.lgp/wm0.ev
    int  stageDataFile(const QString& outRelPath,
                       const QString& modRelPath, QStringList& log);
    int  stageHext(QStringList& log);         // hext/** (verbatim)

    bool writeModXml(const Config& config, int fieldCount);
    bool packArchive(const QString& iroPath, int& fileCount, QStringList& log);

    // Helpers
    QString resolveOriginalFlevel() const;
    bool    stageBytes(const QString& modRelPath, const QByteArray& bytes);
    static bool copyTree(const QString& srcDir, const QString& dstRoot,
                         const QString& modPrefix, int& count);
};
