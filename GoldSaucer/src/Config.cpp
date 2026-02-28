#include "Config.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QDebug>
#include <random>

Config::Config()
{
    setDefaults();
}

void Config::setDefaults()
{
    // Enable all features by default
    for (int i = 0; i < FeatureCount; ++i) {
        m_featuresEnabled[i] = true;
    }
    
    // Random seed
    std::random_device rd;
    m_seed = rd();
    
    // Enemy settings
    m_enemyLevelVariance = 10; // ±10 levels
    m_enemyStatsVariance = 0.3; // ±30% stats
    m_bossProtectionEnabled = true; // Enable boss protection by default
    m_bossRandomizationIntensity = 10; // 10% intensity for boss randomization
    m_encounterBossesIncluded = false; // Don't shuffle bosses by default
    
    // Shop settings
    m_shopItemPoolSize = 50; // Use 50 random items for shops
    m_shopPriceVariance = 0.5; // ±50% price variance
    m_foreignItemChance = 30; // 30% chance for foreign items
    m_oneTimePurchaseEnabled = true; // Enable one-time purchases
    
    // Field pickup settings
    m_pickupRarityMode = 0; // Balanced mode
    m_keyItemRandomization = false; // Disabled by default (experimental)
    
    // Starting equipment settings
    m_startingEquipmentTier = 1; // Balanced tier
    
    // Output folder - default to "Randomized" next to FF7 installation
    m_outputFolder = "Randomized";
}

bool Config::loadFromFile(const QString& filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Could not open config file:" << filename << "Error:" << file.errorString();
        return false;
    }
    
    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    
    if (doc.isNull() || !doc.isObject()) {
        qDebug() << "Invalid JSON in config file:" << filename;
        return false;
    }
    
    QJsonObject root = doc.object();
    
    // Load feature flags
    QJsonArray features = root["features"].toArray();
    for (int i = 0; i < features.size() && i < FeatureCount; ++i) {
        m_featuresEnabled[i] = features[i].toBool(true);
    }
    
    // Load seed
    if (root.contains("seed")) {
        m_seed = static_cast<unsigned int>(root["seed"].toInt(m_seed));
    }
    
    // Load enemy settings
    QJsonObject enemySettings = root["enemyRandomization"].toObject();
    if (enemySettings.contains("levelVariance")) {
        m_enemyLevelVariance = enemySettings["levelVariance"].toInt(m_enemyLevelVariance);
    }
    if (enemySettings.contains("statsVariance")) {
        m_enemyStatsVariance = enemySettings["statsVariance"].toDouble(m_enemyStatsVariance);
    }
    if (enemySettings.contains("bossProtectionEnabled")) {
        m_bossProtectionEnabled = enemySettings["bossProtectionEnabled"].toBool(m_bossProtectionEnabled);
    }
    if (enemySettings.contains("bossRandomizationIntensity")) {
        m_bossRandomizationIntensity = enemySettings["bossRandomizationIntensity"].toInt(m_bossRandomizationIntensity);
    }
    if (enemySettings.contains("encounterBossesIncluded")) {
        m_encounterBossesIncluded = enemySettings["encounterBossesIncluded"].toBool(m_encounterBossesIncluded);
    }
    
    // Load shop settings
    QJsonObject shopSettings = root["shopRandomization"].toObject();
    if (shopSettings.contains("itemPoolSize")) {
        m_shopItemPoolSize = shopSettings["itemPoolSize"].toInt(m_shopItemPoolSize);
    }
    if (shopSettings.contains("priceVariance")) {
        m_shopPriceVariance = shopSettings["priceVariance"].toDouble(m_shopPriceVariance);
    }
    if (shopSettings.contains("foreignItemChance")) {
        m_foreignItemChance = shopSettings["foreignItemChance"].toInt(m_foreignItemChance);
    }
    if (shopSettings.contains("oneTimePurchaseEnabled")) {
        m_oneTimePurchaseEnabled = shopSettings["oneTimePurchaseEnabled"].toBool(m_oneTimePurchaseEnabled);
    }
    
    // Load field pickup settings
    QJsonObject pickupSettings = root["fieldPickupRandomization"].toObject();
    if (pickupSettings.contains("rarityMode")) {
        m_pickupRarityMode = pickupSettings["rarityMode"].toInt(m_pickupRarityMode);
    }
    if (pickupSettings.contains("keyItemRandomization")) {
        m_keyItemRandomization = pickupSettings["keyItemRandomization"].toBool(m_keyItemRandomization);
    }
    
    // Load starting equipment settings
    QJsonObject equipmentSettings = root["startingEquipmentRandomization"].toObject();
    if (equipmentSettings.contains("tier")) {
        m_startingEquipmentTier = equipmentSettings["tier"].toInt(m_startingEquipmentTier);
    }
    
    // Load output folder settings
    if (root.contains("outputFolder")) {
        m_outputFolder = root["outputFolder"].toString(m_outputFolder);
    }
    
    // Load FF7 path settings
    if (root.contains("ff7Path")) {
        m_ff7Path = root["ff7Path"].toString(m_ff7Path);
    }
    
    qDebug() << "Config loaded from:" << filename;
    return true;
}

bool Config::saveToFile(const QString& filename) const
{
    QJsonObject root;
    
    // Save feature flags
    QJsonArray features;
    for (int i = 0; i < FeatureCount; ++i) {
        features.append(m_featuresEnabled[i]);
    }
    root["features"] = features;
    
    // Save seed
    root["seed"] = static_cast<int>(m_seed);
    
    // Save enemy settings
    QJsonObject enemySettings;
    enemySettings["levelVariance"] = m_enemyLevelVariance;
    enemySettings["statsVariance"] = m_enemyStatsVariance;
    enemySettings["bossProtectionEnabled"] = m_bossProtectionEnabled;
    enemySettings["bossRandomizationIntensity"] = m_bossRandomizationIntensity;
    enemySettings["encounterBossesIncluded"] = m_encounterBossesIncluded;
    root["enemyRandomization"] = enemySettings;
    
    // Save shop settings
    QJsonObject shopSettings;
    shopSettings["itemPoolSize"] = m_shopItemPoolSize;
    shopSettings["priceVariance"] = m_shopPriceVariance;
    shopSettings["foreignItemChance"] = m_foreignItemChance;
    shopSettings["oneTimePurchaseEnabled"] = m_oneTimePurchaseEnabled;
    root["shopRandomization"] = shopSettings;
    
    // Save field pickup settings
    QJsonObject pickupSettings;
    pickupSettings["rarityMode"] = m_pickupRarityMode;
    pickupSettings["keyItemRandomization"] = m_keyItemRandomization;
    root["fieldPickupRandomization"] = pickupSettings;
    
    // Save starting equipment settings
    QJsonObject equipmentSettings;
    equipmentSettings["tier"] = m_startingEquipmentTier;
    root["startingEquipmentRandomization"] = equipmentSettings;
    
    // Save output folder settings
    root["outputFolder"] = m_outputFolder;
    
    // Save FF7 path settings
    root["ff7Path"] = m_ff7Path;
    
    QJsonDocument doc(root);
    
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "Could not write config file:" << filename << "Error:" << file.errorString();
        return false;
    }
    
    file.write(doc.toJson());
    qDebug() << "Config saved to:" << filename;
    return true;
}

void Config::setFeatureEnabled(Feature feature, bool enabled)
{
    if (feature >= 0 && feature < FeatureCount) {
        m_featuresEnabled[feature] = enabled;
    }
}

bool Config::isFeatureEnabled(Feature feature) const
{
    if (feature >= 0 && feature < FeatureCount) {
        return m_featuresEnabled[feature];
    }
    return false;
}

void Config::setSeed(unsigned int seed)
{
    m_seed = seed;
}

unsigned int Config::getSeed() const
{
    return m_seed;
}

void Config::setEnemyLevelVariance(int variance)
{
    m_enemyLevelVariance = variance;
}

int Config::getEnemyLevelVariance() const
{
    return m_enemyLevelVariance;
}

void Config::setEnemyStatsVariance(double variance)
{
    m_enemyStatsVariance = variance;
}

double Config::getEnemyStatsVariance() const
{
    return m_enemyStatsVariance;
}

void Config::setEncounterBossesIncluded(bool enabled)
{
    m_encounterBossesIncluded = enabled;
}

bool Config::getEncounterBossesIncluded() const
{
    return m_encounterBossesIncluded;
}

void Config::setBossProtectionEnabled(bool enabled)
{
    m_bossProtectionEnabled = enabled;
}

bool Config::getBossProtectionEnabled() const
{
    return m_bossProtectionEnabled;
}

void Config::setBossRandomizationIntensity(int intensity)
{
    m_bossRandomizationIntensity = qBound(0, intensity, 100);
}

int Config::getBossRandomizationIntensity() const
{
    return m_bossRandomizationIntensity;
}

void Config::setShopItemPoolSize(int size)
{
    m_shopItemPoolSize = size;
}

int Config::getShopItemPoolSize() const
{
    return m_shopItemPoolSize;
}

void Config::setShopPriceVariance(double variance)
{
    m_shopPriceVariance = variance;
}

double Config::getShopPriceVariance() const
{
    return m_shopPriceVariance;
}

void Config::setForeignItemChance(int percentage)
{
    m_foreignItemChance = qBound(0, percentage, 100);
}

int Config::getForeignItemChance() const
{
    return m_foreignItemChance;
}

void Config::setOneTimePurchaseEnabled(bool enabled)
{
    m_oneTimePurchaseEnabled = enabled;
}

bool Config::getOneTimePurchaseEnabled() const
{
    return m_oneTimePurchaseEnabled;
}

void Config::setPickupRarityMode(int mode)
{
    m_pickupRarityMode = mode;
}

int Config::getPickupRarityMode() const
{
    return m_pickupRarityMode;
}

void Config::setKeyItemRandomization(bool enabled)
{
    m_keyItemRandomization = enabled;
}

bool Config::getKeyItemRandomization() const
{
    return m_keyItemRandomization;
}

void Config::setStartingEquipmentTier(int tier)
{
    m_startingEquipmentTier = tier;
}

int Config::getStartingEquipmentTier() const
{
    return m_startingEquipmentTier;
}

void Config::setOutputFolder(const QString& folder)
{
    m_outputFolder = folder;
}

QString Config::getOutputFolder() const
{
    return m_outputFolder;
}

void Config::setFF7Path(const QString& path)
{
    qDebug() << "Config::setFF7Path called with:" << path;
    qDebug() << "Previous FF7 path:" << m_ff7Path;
    m_ff7Path = path;
    qDebug() << "New FF7 path set to:" << m_ff7Path;
}

QString Config::getFF7Path() const
{
    return m_ff7Path;
}
