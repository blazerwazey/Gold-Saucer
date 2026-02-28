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
    
    bool randomize();
    
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
