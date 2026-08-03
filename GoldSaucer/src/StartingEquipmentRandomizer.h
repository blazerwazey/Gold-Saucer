#pragma once

#include <QString>
#include <QByteArray>
#include <random>
#include <QVector>
#include <QMap>
#include "TextReplacementConfig.h"
#include "KernelBinParser.h"

class Randomizer;

class StartingEquipmentRandomizer
{
public:
    explicit StartingEquipmentRandomizer(Randomizer* parent);
    
    // `shuffleEquipment` gates ONLY the random starting gear. The starting-level
    // patch below is applied unconditionally, so it still lands when the player
    // has starting-equipment randomization switched off.
    bool randomize(bool shuffleEquipment = true);
    
private:
    Randomizer* m_parent;
    std::mt19937& m_rng;
    
    struct CharacterEquipment {
        quint16 weaponId;
        quint16 armorId;
        quint16 accessoryId;
        quint8 materiaSlots[8]; // 8 materia slots total
    };
    
    QString findKernelBin() const;
    bool loadInitialData(const QString& filePath, QByteArray& data);
    bool saveInitialData(const QString& filePath, const QByteArray& data);
    void copyOriginal(const QString& src, const QString& dst);
    
    bool randomizeAll();
    void randomizeStartingEquipment(QByteArray& data);

    // Starting level. Rewrites a character's kernel section-3 init record so a NEW
    // GAME begins at that level, with stats/HP/MP taken from their own growth
    // curves in section 2. `growthData` is kernel section 2 (may be empty, in
    // which case the patch is skipped rather than writing a half-levelled record).
    void applyStartingLevels(QByteArray& initData, const QByteArray& growthData);
    bool growthStatsAt(const QByteArray& growthData, int characterId, int level,
                       quint8 stats[6], quint16& hp, quint16& mp) const;
    void randomizeCharacterEquipment(QByteArray& data, int characterId);
    
    quint16 getRandomWeapon(int characterId, int tier);
    quint16 getRandomArmor(int tier);
    quint16 getRandomAccessory(int tier);
    void randomizeMateria(QByteArray& data, int characterId);
    
    // Equipment pools by tier and character
    QMap<int, QVector<quint16>> m_weaponPools[3]; // 3 tiers
    QVector<quint16> m_armorPools[3];            // 3 tiers
    QVector<quint16> m_accessoryPools[3];        // 3 tiers
    QVector<quint16> m_materiaPools[3];          // 3 tiers
    
    // Text replacement integration
    bool replaceStartingEquipmentText();
    void replaceItemTextByCategory(quint16 itemId, const TextReplacementConfig::ReplacementSettings& settings, KernelBinParser& kernelParser);
    QString generateReplacementName(quint16 itemId, ItemCategory category, const QString& prefix);
    QString getItemPrefix(ItemCategory category, const TextReplacementConfig::ReplacementSettings& settings);
    
    // Track randomized equipment for text replacement
    QMap<int, quint16> m_randomizedWeapons;
    QMap<int, quint16> m_randomizedArmor;
    QMap<int, quint16> m_randomizedAccessories;
    QMap<int, QVector<quint16>> m_randomizedMateria;
    
    void initializeEquipmentPools();
    
    enum Character {
        Cloud = 0,
        Barret = 1,
        Tifa = 2,
        Aerith = 3,
        Red = 4,
        Yuffie = 5,
        CaitSith = 6,
        Vincent = 7,
        Cid = 8
    };
};
