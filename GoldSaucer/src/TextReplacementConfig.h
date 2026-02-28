#pragma once

#include <QString>

// Item categories for text replacement
enum class ItemCategory {
    Consumable,
    Materia,
    KeyItem,
    Weapon,
    Armor,
    Accessory,
    Special
};

// Text replacement configuration
class TextReplacementConfig {
public:
    struct ReplacementSettings {
        bool enabled;
        bool items;
        bool materia;
        bool keyItems;
        bool weapons;
        bool armor;
        bool accessories;
        QString itemPrefix;
        QString materiaPrefix;
        QString keyItemPrefix;
        QString weaponPrefix;
        QString armorPrefix;
        QString accessoryPrefix;
        bool useColorCoding;
        
        ReplacementSettings() 
            : enabled(false)
            , items(true)
            , materia(true)
            , keyItems(true)
            , weapons(true)
            , armor(true)
            , accessories(true)
            , itemPrefix("Random")
            , materiaPrefix("Random")
            , keyItemPrefix("Special")
            , weaponPrefix("Random")
            , armorPrefix("Random")
            , accessoryPrefix("Random")
            , useColorCoding(false)
        {}
    };
    
    // Configuration methods
    void loadSettings();
    void saveSettings();
    ReplacementSettings getSettings() const;
    void setSettings(const ReplacementSettings& settings);
    
    // Default settings
    static ReplacementSettings getDefaultSettings();
    
private:
    ReplacementSettings m_settings;
};
