#include "Randomizer.h"
#include "EnemyRandomizer.h"
#include "ShopRandomizer.h"
#include "FieldPickupRandomizer_ff7tk.h"
#include "StartingEquipmentRandomizer.h"
#include <QFile>
#include <QDir>
#include <QDebug>

Randomizer::Randomizer(const QString& ff7Path, const Config& config)
    : m_ff7Path(ff7Path)
    , m_config(config)
    , m_rng(config.getSeed())
    , m_enemyRandomizer(nullptr)
    , m_shopRandomizer(nullptr)
    , m_fieldPickupRandomizer(nullptr)
    , m_startingEquipmentRandomizer(nullptr)
{
    initializeRandomizers();
}

Randomizer::~Randomizer()
{
    delete m_enemyRandomizer;
    delete m_shopRandomizer;
    delete m_fieldPickupRandomizer;
    delete m_startingEquipmentRandomizer;
}

void Randomizer::initializeRandomizers()
{
    m_enemyRandomizer = new EnemyRandomizer(this);
    m_shopRandomizer = new ShopRandomizer(this);
    m_fieldPickupRandomizer = new FieldPickupRandomizer_ff7tk(this);
    m_startingEquipmentRandomizer = new StartingEquipmentRandomizer(this);
}

bool Randomizer::validateFF7Installation()
{
    QDir ff7Dir(m_ff7Path);
    
    // Check for essential directories and files
    if (!ff7Dir.exists("data")) {
        qDebug() << "Error: data directory not found in FF7 installation";
        return false;
    }
    
    if (!ff7Dir.exists("data/lang-en/battle")) {
        qDebug() << "Error: data/lang-en/battle directory not found";
        return false;
    }
    
    if (!ff7Dir.exists("data/lang-en/kernel") && !ff7Dir.exists("data/lang-en/kernel.bin")) {
        qDebug() << "Error: data/lang-en/kernel directory or file not found";
        return false;
    }
    
    if (!ff7Dir.exists("data/field") && !ff7Dir.exists("data/flevel")) {
        qDebug() << "Error: data/field or data/flevel directory not found";
        return false;
    }
    
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
    if (!validateFF7Installation()) {
        return false;
    }
    
    if (!m_fieldPickupRandomizer) {
        qDebug() << "Error: Field pickup randomizer not initialized";
        return false;
    }
    
    return m_fieldPickupRandomizer->randomize();
}

bool Randomizer::randomizeStartingEquipment()
{
    // Equipment randomizer finds and validates kernel.bin on its own
    if (!m_startingEquipmentRandomizer) {
        qDebug() << "Error: Starting equipment randomizer not initialized";
        return false;
    }
    
    return m_startingEquipmentRandomizer->randomize();
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
