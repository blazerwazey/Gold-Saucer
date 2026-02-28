#pragma once

#include <QString>
#include <QDir>
#include <random>
#include "Config.h"

#include "EnemyRandomizer.h"
#include "ShopRandomizer.h"
#include "FieldPickupRandomizer_ff7tk.h"
#include "StartingEquipmentRandomizer.h"

class EnemyRandomizer;
class ShopRandomizer;
class FieldPickupRandomizer_ff7tk;
class StartingEquipmentRandomizer;

class Randomizer
{
    friend class EnemyRandomizer;
    friend class ShopRandomizer;
    friend class FieldPickupRandomizer_ff7tk;
    friend class StartingEquipmentRandomizer;
public:
    Randomizer(const QString& ff7Path, const Config& config);
    ~Randomizer();
    
    bool randomizeEnemyStats();
    bool randomizeEnemyEncounters();
    bool randomizeShops();
    bool randomizeFieldPickups();
    bool randomizeStartingEquipment();
    
    bool createBackup(const QString& filePath);
    QString getFF7Path() const { return m_ff7Path; }
    QString getOutputPath() const;
    bool createOutputDirectory();
    bool copyOriginalFiles();
    
private:
    QString m_ff7Path;
    const Config& m_config;
    std::mt19937 m_rng;
    
    EnemyRandomizer* m_enemyRandomizer;
    ShopRandomizer* m_shopRandomizer;
    FieldPickupRandomizer_ff7tk* m_fieldPickupRandomizer;
    StartingEquipmentRandomizer* m_startingEquipmentRandomizer;
    
    void initializeRandomizers();
    bool validateFF7Installation();
};
