#pragma once

#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QJsonArray>

class Config
{
public:
    enum Feature {
        EnemyStatsRandomization = 0,
        ShopRandomization,
        FieldPickupRandomization,
        StartingEquipmentRandomization,
        ArchipelagoIntegration,
        TextReplacement,
        BossProtection,
        EnemyEncounterRandomization,
        FeatureCount
    };
    
    Config();
    
    bool loadFromFile(const QString& filename);
    // includeApJsonPath: the .apff7 path is per-SEED, not a durable preference —
    // pass false (as the auto-save on Start does) to leave it out so a later launch
    // doesn't silently reload a stale seed path.
    bool saveToFile(const QString& filename, bool includeApJsonPath = true) const;
    
    void setFeatureEnabled(Feature feature, bool enabled);
    bool isFeatureEnabled(Feature feature) const;
    
    void setSeed(unsigned int seed);
    unsigned int getSeed() const;
    
    // Enemy randomization settings
    void setEnemyLevelVariance(int variance);
    int getEnemyLevelVariance() const;
    
    void setEnemyStatsVariance(double variance);
    double getEnemyStatsVariance() const;
    
    // Enemy encounter settings
    void setEncounterBossesIncluded(bool enabled);
    bool getEncounterBossesIncluded() const;
    
    // Boss protection settings
    void setBossProtectionEnabled(bool enabled);
    bool getBossProtectionEnabled() const;
    
    void setBossRandomizationIntensity(int intensity);
    int getBossRandomizationIntensity() const;
    
    // Shop randomization settings
    void setShopItemPoolSize(int size);
    int getShopItemPoolSize() const;
    
    void setShopPriceVariance(double variance);
    double getShopPriceVariance() const;
    
    // Archipelago shop settings
    void setForeignItemChance(int percentage);
    int getForeignItemChance() const;
    
    void setOneTimePurchaseEnabled(bool enabled);
    bool getOneTimePurchaseEnabled() const;
    
    // Field pickup settings
    void setPickupRarityMode(int mode); // 0: balanced, 1: random, 2: high-tier only
    int getPickupRarityMode() const;
    
    void setKeyItemRandomization(bool enabled);
    bool getKeyItemRandomization() const;
    
    // Starting equipment settings
    // Starting equipment tier, stored 0-BASED (0 = weakest .. 4 = strongest).
    // The Archipelago YAML option is 1-5, so SimpleMainWindow subtracts one on
    // import; the count lives here so the GUI, the importer and the randomizer
    // cannot drift apart again. They already had: the importer clamped to 0-2
    // against a 1-5 option, making YAML 3/4/5 identical and tier 0 unreachable.
    static constexpr int STARTING_EQUIPMENT_TIERS = 5;
    void setStartingEquipmentTier(int tier);
    int getStartingEquipmentTier() const;
    
    void setOutputFolder(const QString& folder);
    QString getOutputFolder() const;
    
    void setFF7Path(const QString& path);
    QString getFF7Path() const;

    void setApJsonPath(const QString& path);
    QString getApJsonPath() const;

    void setFreeRoam(bool enabled);
    bool getFreeRoam() const;

    // Also export the randomized files as a 7th Heaven .iro archive
    void setExportIro(bool enabled);
    bool getExportIro() const;

    void setDefaults();
    
private:
    bool m_featuresEnabled[FeatureCount];
    unsigned int m_seed;
    
    // Enemy settings
    int m_enemyLevelVariance;
    double m_enemyStatsVariance;
    bool m_bossProtectionEnabled;
    int m_bossRandomizationIntensity;
    bool m_encounterBossesIncluded;
    
    // Shop settings
    int m_shopItemPoolSize;
    double m_shopPriceVariance;
    int m_foreignItemChance;
    bool m_oneTimePurchaseEnabled;
    
    // Field pickup settings
    int m_pickupRarityMode;
    bool m_keyItemRandomization;
    
    // Starting equipment settings
    int m_startingEquipmentTier;
    
    // Output folder settings
    QString m_outputFolder;
    
    // FF7 installation path
    QString m_ff7Path;

    // Archipelago JSON path (output from AP generator, consumed by Gold Saucer)
    QString m_apJsonPath;

    // Free Roam mode: start on world map at game moment 1997
    bool m_freeRoam;

    // Export randomized files as a 7th Heaven .iro archive (in addition to loose)
    bool m_exportIro;
};
