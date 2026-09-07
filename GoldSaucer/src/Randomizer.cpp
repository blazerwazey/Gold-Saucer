#include "Randomizer.h"
#include "EnemyRandomizer.h"
#include "ShopRandomizer.h"
#include "FieldPickupRandomizer_ff7tk.h"
#include "StartingEquipmentRandomizer.h"
#include "CraterBarrierPatcher.h"
#include <QFile>
#include <QDir>
#include <QDebug>

QString Randomizer::resolveFF7Root(const QString& path)
{
    // 2026 Steam re-release: FFNx engine (ff7_en.exe) + data live in ff7/workingdir.
    const QString nested = QDir(path).filePath("ff7/workingdir");
    if (QFile::exists(QDir(nested).filePath("ff7_en.exe"))) {
        qDebug() << "Detected 2026 re-release layout; using FF7 root:" << nested;
        return nested;
    }
    return path;
}

Randomizer::Randomizer(const QString& ff7Path, const Config& config)
    : m_ff7Path(resolveFF7Root(ff7Path))
    , m_config(config)
    , m_rng(config.getSeed())
    , m_enemyRandomizer(nullptr)
    , m_shopRandomizer(nullptr)
    , m_fieldPickupRandomizer(nullptr)
    , m_startingEquipmentRandomizer(nullptr)
    , m_craterBarrierPatcher(nullptr)
{
    initializeRandomizers();
}

Randomizer::~Randomizer()
{
    delete m_enemyRandomizer;
    delete m_shopRandomizer;
    delete m_fieldPickupRandomizer;
    delete m_startingEquipmentRandomizer;
    delete m_craterBarrierPatcher;
}

void Randomizer::initializeRandomizers()
{
    m_enemyRandomizer = new EnemyRandomizer(this);
    m_shopRandomizer = new ShopRandomizer(this);
    m_fieldPickupRandomizer = new FieldPickupRandomizer_ff7tk(this);
    m_startingEquipmentRandomizer = new StartingEquipmentRandomizer(this);
    m_craterBarrierPatcher = new CraterBarrierPatcher(m_ff7Path, getOutputPath());
}

bool Randomizer::validateFF7Installation()
{
    QDir ff7Dir(m_ff7Path);

    // Every branch here names the exact path that was missing. This check used
    // to fail with nothing but a qDebug() into a GUI-subsystem void, so a user
    // whose install did not match got "<pass> failed" and no way to find out why.
    auto missing = [&](const QString& what, const QString& hint) {
        m_lastError = QStringLiteral("This does not look like a complete FF7 install.\n"
                                     "Missing: %1\nUnder: %2%3")
                          .arg(what, m_ff7Path,
                               hint.isEmpty() ? QString()
                                              : QStringLiteral("\n\n%1").arg(hint));
        qCritical() << "FF7 install validation failed - missing" << what << "under" << m_ff7Path;
        return false;
    };

    if (!ff7Dir.exists("data"))
        return missing(QStringLiteral("data"),
                       QStringLiteral("Pick the folder that contains ff7_en.exe and data. On the "
                                      "2026 re-release that is ff7/workingdir inside the Steam "
                                      "install, which the tool normally finds on its own."));

    // The language checks are the ones that bite. These paths are hardcoded to
    // lang-en, so a German/French/Spanish/Japanese install fails here even
    // though it is perfectly intact - say that outright rather than reporting a
    // missing folder the user can see is not missing.
    if (!ff7Dir.exists("data/lang-en/battle") ||
        (!ff7Dir.exists("data/lang-en/kernel") && !ff7Dir.exists("data/lang-en/kernel.bin"))) {
        const QStringList langs =
            QDir(m_ff7Path + "/data").entryList(QStringList("lang-*"),
                                                QDir::Dirs | QDir::NoDotAndDotDot);
        const bool otherLanguage = !langs.isEmpty() && !langs.contains("lang-en");
        return missing(ff7Dir.exists("data/lang-en/battle")
                           ? QStringLiteral("data/lang-en/kernel (or kernel.bin)")
                           : QStringLiteral("data/lang-en/battle"),
                       otherLanguage
                           ? QStringLiteral("This install has %1 instead of lang-en. Gold Saucer "
                                            "only supports the ENGLISH game files - set the game's "
                                            "language to English in Steam, let it download, then "
                                            "run the randomizer again.")
                                 .arg(langs.join(QStringLiteral(", ")))
                           : QStringLiteral("The install looks incomplete. Verify the game files "
                                            "through Steam and try again."));
    }

    if (!ff7Dir.exists("data/field") && !ff7Dir.exists("data/flevel"))
        return missing(QStringLiteral("data/field (or data/flevel)"),
                       QStringLiteral("Verify the game files through Steam - the field data is "
                                      "missing from this install."));

    m_lastError.clear();
    return true;
}

bool Randomizer::createBackup(const QString& filePath)
{
    QString backupPath = filePath + ".backup";
    QFile originalFile(filePath);
    
    if (!originalFile.exists()) {
        qDebug() << "Warning: Original file does not exist:" << filePath;
        return false;
    }
    
    // If backup already exists, don't overwrite
    if (QFile::exists(backupPath)) {
        return true;
    }
    
    return originalFile.copy(backupPath);
}

bool Randomizer::randomizeEnemyStats()
{
    if (!validateFF7Installation()) {
        return false;
    }
    
    if (!m_enemyRandomizer) {
        qDebug() << "Error: Enemy randomizer not initialized";
        return false;
    }
    
    return m_enemyRandomizer->randomize();
}

bool Randomizer::randomizeEnemyEncounters()
{
    if (!validateFF7Installation()) {
        return false;
    }
    
    if (!m_enemyRandomizer) {
        qDebug() << "Error: Enemy randomizer not initialized";
        return false;
    }
    
    return m_enemyRandomizer->randomizeEncounters();
}

bool Randomizer::randomizeShops()
{
    // Shop randomizer only needs ff7.exe — skip full installation validation
    // (it finds and validates the exe on its own)
    if (!m_shopRandomizer) {
        qDebug() << "Error: Shop randomizer not initialized";
        return false;
    }
    
    return m_shopRandomizer->randomize();
}

bool Randomizer::randomizeFieldPickups()
{
    m_lastError.clear();

    if (!validateFF7Installation()) {
        return false;   // validateFF7Installation() has set m_lastError
    }

    if (!m_fieldPickupRandomizer) {
        m_lastError = QStringLiteral("Internal error: the field pickup randomizer was never "
                                     "constructed. Please report this with the log.");
        qCritical() << m_lastError;
        return false;
    }

    if (!m_fieldPickupRandomizer->randomize()) {
        m_lastError = m_fieldPickupRandomizer->lastError();
        if (m_lastError.isEmpty()) {
            // A failure path that predates fail(); still better than nothing.
            m_lastError = QStringLiteral("The field pass stopped without reporting a reason. "
                                         "The run log has the last steps it took.");
        }
        return false;
    }
    return true;
}

bool Randomizer::randomizeStartingEquipment(bool shuffleEquipment)
{
    // Equipment randomizer finds and validates kernel.bin on its own
    if (!m_startingEquipmentRandomizer) {
        qDebug() << "Error: Starting equipment randomizer not initialized";
        return false;
    }
    
    return m_startingEquipmentRandomizer->randomize(shuffleEquipment);
}

bool Randomizer::applyCraterBarrier()
{
    if (!m_craterBarrierPatcher) {
        qDebug() << "Error: Crater barrier patcher not initialized";
        return false;
    }
    m_craterBarrierPatcher->setApJsonPath(m_config.getApJsonPath());
    return m_craterBarrierPatcher->patch();
}

QString Randomizer::getOutputPath() const
{
    QString outputFolder = m_config.getOutputFolder();
    if (QDir(outputFolder).isAbsolute()) {
        return outputFolder;
    }
    
    // Relative path - combine with FF7 path
    QDir ff7Dir(m_ff7Path);
    return ff7Dir.filePath(outputFolder);
}

bool Randomizer::createOutputDirectory()
{
    QString outputPath = getOutputPath();
    QDir outputDir(outputPath);
    
    if (!outputDir.exists()) {
        if (!outputDir.mkpath(".")) {
            qDebug() << "Error: Could not create output directory:" << outputPath;
            return false;
        }
        qDebug() << "Created output directory:" << outputPath;
    }
    
    return true;
}

bool Randomizer::copyOriginalFiles()
{
    if (!createOutputDirectory()) {
        return false;
    }
    
    QString outputPath = getOutputPath();
    QDir ff7Dir(m_ff7Path);
    QDir outputDir(outputPath);
    
    qDebug() << "Copying original files to output directory...";
    
    // Copy enemy data
    QString enemySource = ff7Dir.filePath("data/lang-en/battle/scene.bin");
    QString enemyDest = outputDir.filePath("data/lang-en/battle/scene.bin");
    
    QFileInfo enemyInfo(enemySource);
    if (enemyInfo.exists()) {
        QDir enemyOutputDir = QFileInfo(enemyDest).dir();
        if (!enemyOutputDir.exists()) {
            enemyOutputDir.mkpath(".");
        }
        if (QFile::exists(enemyDest)) {
            QFile::remove(enemyDest);
        }
        if (QFile::copy(enemySource, enemyDest)) {
            qDebug() << "Copied: data/lang-en/battle/scene.bin";
        } else {
            qDebug() << "Error: Could not copy enemy data";
        }
    } else {
        qDebug() << "Warning: Enemy data not found:" << enemySource;
    }
    
    // Copy kernel data (check if directory or file)
    QString kernelDir = ff7Dir.filePath("data/lang-en/kernel");
    QString kernelFile = ff7Dir.filePath("data/lang-en/kernel.bin");
    
    if (QDir(kernelDir).exists()) {
        // kernel is a directory
        QString kernelDest = outputDir.filePath("data/lang-en/kernel");
        QDir sourceKernelDir(kernelDir);
        QDir destKernelDir(kernelDest);
        
        if (!destKernelDir.exists()) {
            destKernelDir.mkpath(".");
        }
        
        // Copy all files in kernel directory
        QStringList kernelFiles = sourceKernelDir.entryList(QDir::Files);
        for (const QString& file : kernelFiles) {
            QString srcFile = sourceKernelDir.filePath(file);
            QString dstFile = destKernelDir.filePath(file);
            if (QFile::exists(dstFile)) {
                QFile::remove(dstFile);
            }
            if (QFile::copy(srcFile, dstFile)) {
                qDebug() << "Copied: data/lang-en/kernel/" << file;
            }
        }
    } else if (QFile::exists(kernelFile)) {
        // kernel.bin is a file
        QString kernelDest = outputDir.filePath("data/lang-en/kernel/kernel.bin");
        QDir kernelOutputDir = QFileInfo(kernelDest).dir();
        if (!kernelOutputDir.exists()) {
            kernelOutputDir.mkpath(".");
        }
        if (QFile::exists(kernelDest)) {
            QFile::remove(kernelDest);
        }
        if (QFile::copy(kernelFile, kernelDest)) {
            qDebug() << "Copied: data/lang-en/kernel.bin";
        }
    } else {
        qDebug() << "Warning: Kernel data not found";
    }
    
    // Copy field data (check both field and flevel directories)
    QStringList fieldPaths = {"data/field/flevel.lgp", "data/flevel/flevel.lgp"};
    bool fieldCopied = false;
    
    for (const QString& fieldPath : fieldPaths) {
        QString fieldSource = ff7Dir.filePath(fieldPath);
        QString fieldDest = outputDir.filePath(fieldPath);
        
        QFileInfo fieldInfo(fieldSource);
        if (fieldInfo.exists()) {
            QDir fieldOutputDir = QFileInfo(fieldDest).dir();
            if (!fieldOutputDir.exists()) {
                fieldOutputDir.mkpath(".");
            }
            if (QFile::exists(fieldDest)) {
                QFile::remove(fieldDest);
            }
            if (QFile::copy(fieldSource, fieldDest)) {
                qDebug() << "Copied:" << fieldPath;
                fieldCopied = true;
                break;
            }
        }
    }
    
    if (!fieldCopied) {
        qDebug() << "Warning: Field data not found in either data/field/ or data/flevel/";
    }
    
    qDebug() << "File copying completed.";
    return true;
}
