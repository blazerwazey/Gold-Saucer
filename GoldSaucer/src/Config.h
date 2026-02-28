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
    bool saveToFile(const QString& filename) const;
    
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
    void setStartingEquipmentTier(int tier); // 0: weak, 1: balanced, 2: strong
    int getStartingEquipmentTier() const;
    
    void setOutputFolder(const QString& folder);
    QString getOutputFolder() const;
    
    void setFF7Path(const QString& path);
    QString getFF7Path() const;
    
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
};
