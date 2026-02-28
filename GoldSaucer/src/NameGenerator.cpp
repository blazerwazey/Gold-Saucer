#include "NameGenerator.h"
#include <QDebug>
#include <QRandomGenerator>

// Static word banks
const QStringList NameGenerator::WEAPON_ADJECTIVES = {
    "Mystic", "Ancient", "Divine", "Shadow", "Celestial", "Infernal", "Frost", "Thunder",
    "Storm", "Void", "Holy", "Dark", "Light", "Chaos", "Order", "Time", "Space", "Soul", "Spirit",
    "Dragon", "Phoenix", "Titan", "Demon", "Angel", "Valkyrie", "Samurai", "Ninja", "Paladin", "Rogue"
};

const QStringList NameGenerator::WEAPON_NOUNS = {
    "Blade", "Sword", "Edge", "Fang", "Claw", "Spear", "Lance", "Pike", "Halberd", "Axe",
    "Hammer", "Mace", "Staff", "Rod", "Wand", "Dagger", "Knife", "Rapier", "Sabre", "Cutlass",
    "Scythe", "Bow", "Crossbow", "Gun", "Cannon", "Launcher", "Thrower", "Shooter", "Striker", "Smasher"
};

const QStringList NameGenerator::ARMOR_ADJECTIVES = {
    "Guardian", "Aegis", "Bastion", "Fortress", "Sanctuary", "Haven", "Refuge", "Citadel",
    "Shield", "Ward", "Barrier", "Wall", "Defense", "Protection", "Guard", "Watch", "Sentinel", "Keeper",
    "Iron", "Steel", "Crystal", "Diamond", "Emerald", "Ruby", "Sapphire", "Mythril", "Adamant", "Eternal"
};

const QStringList NameGenerator::ARMOR_NOUNS = {
    "Armor", "Plate", "Mail", "Vest", "Chest", "Guard", "Shield", "Barrier", "Ward", "Aegis",
    "Bulwark", "Bastion", "Fortress", "Citadel", "Sanctum", "Refuge", "Haven", "Sanctuary", "Protection", "Defense"
};

const QStringList NameGenerator::ACCESSORY_ADJECTIVES = {
    "Charm", "Talisman", "Amulet", "Relic", "Artifact", "Heirloom", "Treasure", "Legacy",
    "Mystic", "Ancient", "Divine", "Sacred", "Holy", "Blessed", "Enchanted", "Magical", "Arcane",
    "Lucky", "Fortune", "Destiny", "Fate", "Fortune", "Wealth", "Power", "Wisdom", "Courage", "Valor"
};

const QStringList NameGenerator::ACCESSORY_NOUNS = {
    "Ring", "Charm", "Talisman", "Amulet", "Relic", "Artifact", "Heirloom", "Treasure",
    "Gem", "Stone", "Crystal", "Orb", "Sphere", "Jewel", "Medallion", "Badge", "Emblem", "Insignia", "Mark"
};

const QStringList NameGenerator::MATERIA_ADJECTIVES = {
    "Essence", "Wisdom", "Power", "Knowledge", "Mastery", "Insight", "Clarity", "Focus",
    "Elemental", "Cosmic", "Primal", "Ancient", "Divine", "Sacred", "Mystic", "Arcane", "Ethereal",
    "Storm", "Flame", "Frost", "Earth", "Wind", "Water", "Light", "Dark", "Life", "Death", "Time", "Space"
};

const QStringList NameGenerator::MATERIA_NOUNS = {
    "Materia", "Stone", "Crystal", "Shard", "Fragment", "Essence", "Core", "Heart", "Soul", "Spirit",
    "Orb", "Sphere", "Gem", "Jewel", "Focus", "Lens", "Prism", "Catalyst", "Conduit", "Channel"
};

const QStringList NameGenerator::ITEM_ADJECTIVES = {
    "Potion", "Elixir", "Remedy", "Tonic", "Draught", "Brew", "Concoction", "Mixture",
    "Healing", "Restorative", "Revitalizing", "Energizing", "Strengthening", "Protective", "Magical", "Mystical",
    "Rare", "Uncommon", "Common", "Special", "Unique", "Ancient", "Fresh", "Pure", "Refined", "Distilled"
};

const QStringList NameGenerator::ITEM_NOUNS = {
    "Potion", "Elixir", "Remedy", "Tonic", "Draught", "Brew", "Concoction", "Mixture",
    "Salve", "Balm", "Ointment", "Poultice", "Herb", "Root", "Leaf", "Flower", "Extract", "Essence"
};

const QStringList NameGenerator::LORE_PREFIXES = {
    "of the", "of", "from the", "from", "of the Ancient", "of the Divine", "of the Shadow", "of the Light",
    "of the Void", "of the Storm", "of the Flame", "of the Frost", "of the Earth", "of the Wind", "of the Cosmos"
};

const QStringList NameGenerator::LORE_SUFFIXES = {
    "Ancients", "Gods", "Heroes", "Legends", "Myths", "Spirits", "Elements", "Realms", "Worlds", "Times",
    "Dawn", "Dusk", "Night", "Day", "Storm", "Flame", "Frost", "Earth", "Wind", "Light", "Dark", "Void"
};

const QStringList NameGenerator::TIER_PREFIXES = {
    "Common", "Uncommon", "Rare", "Epic", "Legendary"
};

NameGenerator* NameGenerator::s_instance = nullptr;

NameGenerator& NameGenerator::instance()
{
    if (!s_instance) {
        s_instance = new NameGenerator();
    }
    return *s_instance;
}

NameGenerator::NameGenerator()
    : m_defaultStyle(Descriptive)
    , m_rng(new QRandomGenerator(QRandomGenerator::global()->generate()))
{
    initializeDefaultTemplates();
}

QString NameGenerator::generateName(quint16 itemId, ItemCategory category, ItemTier tier, NamingStyle style)
{
    // Update generation statistics
    m_generationCounts[category]++;
    
    QString name;
    switch (style) {
        case Generic:
            name = generateGenericName(itemId, category);
            break;
        case Descriptive:
            name = generateDescriptiveName(itemId, category, tier);
            break;
        case LoreBased:
            name = generateLoreBasedName(itemId, category, tier);
            break;
        case TierBased:
            name = generateTierBasedName(itemId, category, tier);
            break;
        case Custom:
            name = generateCustomName(itemId, category, tier);
            break;
        default:
            name = generateDescriptiveName(itemId, category, tier);
            break;
    }
    
    return name;
}

QString NameGenerator::generateColoredName(quint16 itemId, ItemCategory category, ItemTier tier, NamingStyle style)
{
    QString name = generateName(itemId, category, tier, style);
    FF7Color color = TextEncoder::getItemColor(category);
    QByteArray coloredName = TextEncoder::encodeColoredText(name, color);
    return QString::fromLatin1(coloredName);
}

void NameGenerator::setTemplate(ItemCategory category, const NamingTemplate& template_)
{
    m_templates[category] = template_;
}

NameGenerator::NamingTemplate NameGenerator::getTemplate(ItemCategory category) const
{
    return m_templates.value(category, NamingTemplate());
}

void NameGenerator::resetToDefaults()
{
    initializeDefaultTemplates();
}

void NameGenerator::setDefaultStyle(NamingStyle style)
{
    m_defaultStyle = style;
}

NameGenerator::NamingStyle NameGenerator::getDefaultStyle() const
{
    return m_defaultStyle;
}

NameGenerator::ItemTier NameGenerator::detectItemTier(quint16 itemId, ItemCategory category) const
{
    // Simple tier detection based on item ID ranges
    // This could be made more sophisticated with actual item data
    
    switch (category) {
        case ItemCategory::Weapon:
            if (itemId >= 200) return Legendary;
            if (itemId >= 150) return Epic;
            if (itemId >= 100) return Rare;
            if (itemId >= 50) return Uncommon;
            return Common;
            
        case ItemCategory::Armor:
            if (itemId >= 300) return Legendary;
            if (itemId >= 280) return Epic;
            if (itemId >= 260) return Rare;
            if (itemId >= 240) return Uncommon;
            return Common;
            
        case ItemCategory::Accessory:
            if (itemId >= 400) return Legendary;
            if (itemId >= 380) return Epic;
            if (itemId >= 360) return Rare;
            if (itemId >= 340) return Uncommon;
            return Common;
            
        case ItemCategory::Materia:
            if (itemId >= 80) return Legendary;
            if (itemId >= 60) return Epic;
            if (itemId >= 40) return Rare;
            if (itemId >= 20) return Uncommon;
            return Common;
            
        default:
            return Common;
    }
}

QStringList NameGenerator::generatePreview(ItemCategory category, int count) const
{
    QStringList preview;
    for (int i = 0; i < count; i++) {
        quint16 itemId = i + 1; // Use sequential IDs for preview
        preview.append(const_cast<NameGenerator*>(this)->generateName(itemId, category));
    }
    return preview;
}

QStringList NameGenerator::generateColoredPreview(ItemCategory category, int count) const
{
    QStringList preview;
    for (int i = 0; i < count; i++) {
        quint16 itemId = i + 1;
        preview.append(const_cast<NameGenerator*>(this)->generateColoredName(itemId, category));
    }
    return preview;
}

bool NameGenerator::isValidName(const QString& name, ItemCategory category) const
{
    return TextEncoder::isValidItemName(name, category);
}

QString NameGenerator::sanitizeName(const QString& name, ItemCategory category) const
{
    QString sanitized = name;
    
    // Remove invalid characters
    sanitized = sanitized.simplified();
    
    // Ensure proper length
    int maxLength = TextEncoder::getMaxNameLength(category);
    if (sanitized.length() > maxLength) {
        sanitized = sanitized.left(maxLength);
    }
    
    return sanitized;
}

QMap<ItemCategory, int> NameGenerator::getGenerationCounts() const
{
    return m_generationCounts;
}

void NameGenerator::resetStatistics()
{
    m_generationCounts.clear();
}

QString NameGenerator::generateGenericName(quint16 itemId, ItemCategory category) const
{
    QString categoryName = getCategoryName(category);
    return QString("Random %1 %2").arg(categoryName).arg(itemId % 10 + 1);
}

QString NameGenerator::generateDescriptiveName(quint16 itemId, ItemCategory category, ItemTier tier) const
{
    const NamingTemplate& template_ = m_templates.value(category);
    
    QString adjective = selectAdjective(template_.adjectives, itemId);
    QString noun = selectNoun(template_.nouns, itemId);
    
    QString name = QString("%1 %2").arg(adjective, noun);
    
    // Add tier prefix if significant
    if (tier >= Rare) {
        QString tierPrefix = getTierPrefix(tier);
        name = QString("%1 %2").arg(tierPrefix, name);
    }
    
    return name;
}

QString NameGenerator::generateLoreBasedName(quint16 itemId, ItemCategory category, ItemTier tier) const
{
    const NamingTemplate& template_ = m_templates.value(category);
    QString noun = selectNoun(template_.nouns, itemId);
    
    QString prefix = LORE_PREFIXES[itemId % LORE_PREFIXES.size()];
    QString suffix = LORE_SUFFIXES[itemId % LORE_SUFFIXES.size()];
    
    return QString("%1 %2 %3").arg(noun, prefix, suffix);
}

QString NameGenerator::generateTierBasedName(quint16 itemId, ItemCategory category, ItemTier tier) const
{
    QString tierPrefix = getTierPrefix(tier);
    QString categoryName = getCategoryName(category);
    
    return QString("%1 %2").arg(tierPrefix, categoryName);
}

QString NameGenerator::generateCustomName(quint16 itemId, ItemCategory category, ItemTier tier) const
{
    const NamingTemplate& template_ = m_templates.value(category);
    
    QString name = template_.base;
    if (!template_.prefix.isEmpty()) {
        name = template_.prefix + " " + name;
    }
    if (!template_.suffix.isEmpty()) {
        name = name + " " + template_.suffix;
    }
    
    return name;
}

QString NameGenerator::selectAdjective(const QStringList& adjectives, quint16 itemId) const
{
    if (adjectives.isEmpty()) return "Mystic";
    return adjectives[itemId % adjectives.size()];
}

QString NameGenerator::selectNoun(const QStringList& nouns, quint16 itemId) const
{
    if (nouns.isEmpty()) return "Item";
    return nouns[itemId % nouns.size()];
}

QString NameGenerator::getTierPrefix(ItemTier tier) const
{
    if (tier >= 0 && tier < TIER_PREFIXES.size()) {
        return TIER_PREFIXES[tier];
    }
    return "Common";
}

QString NameGenerator::getCategoryName(ItemCategory category) const
{
    switch (category) {
        case ItemCategory::Weapon: return "Weapon";
        case ItemCategory::Armor: return "Armor";
        case ItemCategory::Accessory: return "Accessory";
        case ItemCategory::Materia: return "Materia";
        case ItemCategory::KeyItem: return "Key Item";
        default: return "Item";
    }
}

void NameGenerator::initializeDefaultTemplates()
{
    // Weapon template
    NamingTemplate weaponTemplate;
    weaponTemplate.adjectives = WEAPON_ADJECTIVES;
    weaponTemplate.nouns = WEAPON_NOUNS;
    weaponTemplate.style = Descriptive;
    m_templates[ItemCategory::Weapon] = weaponTemplate;
    
    // Armor template
    NamingTemplate armorTemplate;
    armorTemplate.adjectives = ARMOR_ADJECTIVES;
    armorTemplate.nouns = ARMOR_NOUNS;
    armorTemplate.style = Descriptive;
    m_templates[ItemCategory::Armor] = armorTemplate;
    
    // Accessory template
    NamingTemplate accessoryTemplate;
    accessoryTemplate.adjectives = ACCESSORY_ADJECTIVES;
    accessoryTemplate.nouns = ACCESSORY_NOUNS;
    accessoryTemplate.style = Descriptive;
    m_templates[ItemCategory::Accessory] = accessoryTemplate;
    
    // Materia template
    NamingTemplate materiaTemplate;
    materiaTemplate.adjectives = MATERIA_ADJECTIVES;
    materiaTemplate.nouns = MATERIA_NOUNS;
    materiaTemplate.style = Descriptive;
    m_templates[ItemCategory::Materia] = materiaTemplate;
    
    // Default item template
    NamingTemplate itemTemplate;
    itemTemplate.adjectives = ITEM_ADJECTIVES;
    itemTemplate.nouns = ITEM_NOUNS;
    itemTemplate.style = Descriptive;
    m_templates[ItemCategory::Consumable] = itemTemplate;
    m_templates[ItemCategory::KeyItem] = itemTemplate;
}
