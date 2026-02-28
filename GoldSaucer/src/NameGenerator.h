#pragma once

#include <QString>
#include <QStringList>
#include <QRandomGenerator>
#include "TextEncoder.h"

// Intelligent name generator for randomized FF7 items
class NameGenerator
{
public:
    enum NamingStyle {
        Generic,        // "Random Weapon 1"
        Descriptive,    // "Mystic Blade"
        LoreBased,      // "Sword of the Ancients"
        TierBased,      // "Common Blade", "Epic Sword"
        Custom          // User-defined templates
    };
    
    enum ItemTier {
        Common = 0,
        Uncommon = 1,
        Rare = 2,
        Epic = 3,
        Legendary = 4
    };
    
    struct NamingTemplate {
        QString prefix;
        QString base;
        QString suffix;
        QStringList adjectives;
        QStringList nouns;
        NamingStyle style;
        
        NamingTemplate() : style(Descriptive) {}
    };
    
    static NameGenerator& instance();
    
    // Main generation methods
    QString generateName(quint16 itemId, ItemCategory category, ItemTier tier = Common, 
                        NamingStyle style = Descriptive);
    QString generateColoredName(quint16 itemId, ItemCategory category, ItemTier tier = Common,
                               NamingStyle style = Descriptive);
    
    // Template management
    void setTemplate(ItemCategory category, const NamingTemplate& template_);
    NamingTemplate getTemplate(ItemCategory category) const;
    void resetToDefaults();
    
    // Style configuration
    void setDefaultStyle(NamingStyle style);
    NamingStyle getDefaultStyle() const;
    
    // Tier detection (based on item ID ranges)
    ItemTier detectItemTier(quint16 itemId, ItemCategory category) const;
    
    // Preview functionality
    QStringList generatePreview(ItemCategory category, int count = 5) const;
    QStringList generateColoredPreview(ItemCategory category, int count = 5) const;
    
    // Validation
    bool isValidName(const QString& name, ItemCategory category) const;
    QString sanitizeName(const QString& name, ItemCategory category) const;
    
    // Statistics
    QMap<ItemCategory, int> getGenerationCounts() const;
    void resetStatistics();
    
private:
    NameGenerator();
    ~NameGenerator() = default;
    
    // Prevent copying
    NameGenerator(const NameGenerator&) = delete;
    NameGenerator& operator=(const NameGenerator&) = delete;
    
    // Internal generation methods
    QString generateGenericName(quint16 itemId, ItemCategory category) const;
    QString generateDescriptiveName(quint16 itemId, ItemCategory category, ItemTier tier) const;
    QString generateLoreBasedName(quint16 itemId, ItemCategory category, ItemTier tier) const;
    QString generateTierBasedName(quint16 itemId, ItemCategory category, ItemTier tier) const;
    QString generateCustomName(quint16 itemId, ItemCategory category, ItemTier tier) const;
    
    // Helper methods
    QString selectAdjective(const QStringList& adjectives, quint16 itemId) const;
    QString selectNoun(const QStringList& nouns, quint16 itemId) const;
    QString getTierPrefix(ItemTier tier) const;
    QString getCategoryName(ItemCategory category) const;
    void initializeDefaultTemplates();
    
    // Member variables
    QMap<ItemCategory, NamingTemplate> m_templates;
    NamingStyle m_defaultStyle;
    QMap<ItemCategory, int> m_generationCounts;
    QRandomGenerator* m_rng;
    
    static NameGenerator* s_instance;
    
    // Word banks for different categories
    static const QStringList WEAPON_ADJECTIVES;
    static const QStringList WEAPON_NOUNS;
    static const QStringList ARMOR_ADJECTIVES;
    static const QStringList ARMOR_NOUNS;
    static const QStringList ACCESSORY_ADJECTIVES;
    static const QStringList ACCESSORY_NOUNS;
    static const QStringList MATERIA_ADJECTIVES;
    static const QStringList MATERIA_NOUNS;
    static const QStringList ITEM_ADJECTIVES;
    static const QStringList ITEM_NOUNS;
    static const QStringList LORE_PREFIXES;
    static const QStringList LORE_SUFFIXES;
    static const QStringList TIER_PREFIXES;
};
