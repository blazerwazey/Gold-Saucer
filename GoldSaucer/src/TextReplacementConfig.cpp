#include "TextReplacementConfig.h"
#include <QSettings>
#include <QDebug>

void TextReplacementConfig::loadSettings()
{
    QSettings settings("GoldSaucer", "TextReplacement");
    
    settings.beginGroup("TextReplacement");
    
    m_settings.enabled = settings.value("enabled", false).toBool();
    m_settings.items = settings.value("items", true).toBool();
    m_settings.materia = settings.value("materia", true).toBool();
    m_settings.keyItems = settings.value("keyItems", true).toBool();
    m_settings.weapons = settings.value("weapons", true).toBool();
    m_settings.armor = settings.value("armor", true).toBool();
    m_settings.accessories = settings.value("accessories", true).toBool();
    
    m_settings.itemPrefix = settings.value("itemPrefix", "Random").toString();
    m_settings.materiaPrefix = settings.value("materiaPrefix", "Random").toString();
    m_settings.keyItemPrefix = settings.value("keyItemPrefix", "Special").toString();
    m_settings.weaponPrefix = settings.value("weaponPrefix", "Random").toString();
    m_settings.armorPrefix = settings.value("armorPrefix", "Random").toString();
    m_settings.accessoryPrefix = settings.value("accessoryPrefix", "Random").toString();
    
    m_settings.useColorCoding = settings.value("useColorCoding", false).toBool();
    
    settings.endGroup();
    
    qDebug() << "Loaded text replacement settings - enabled:" << m_settings.enabled;
}

void TextReplacementConfig::saveSettings()
{
    QSettings settings("GoldSaucer", "TextReplacement");
    
    settings.beginGroup("TextReplacement");
    
    settings.setValue("enabled", m_settings.enabled);
    settings.setValue("items", m_settings.items);
    settings.setValue("materia", m_settings.materia);
    settings.setValue("keyItems", m_settings.keyItems);
    settings.setValue("weapons", m_settings.weapons);
    settings.setValue("armor", m_settings.armor);
    settings.setValue("accessories", m_settings.accessories);
    
    settings.setValue("itemPrefix", m_settings.itemPrefix);
    settings.setValue("materiaPrefix", m_settings.materiaPrefix);
    settings.setValue("keyItemPrefix", m_settings.keyItemPrefix);
    settings.setValue("weaponPrefix", m_settings.weaponPrefix);
    settings.setValue("armorPrefix", m_settings.armorPrefix);
    settings.setValue("accessoryPrefix", m_settings.accessoryPrefix);
    
    settings.setValue("useColorCoding", m_settings.useColorCoding);
    
    settings.endGroup();
    
    qDebug() << "Saved text replacement settings";
}

TextReplacementConfig::ReplacementSettings TextReplacementConfig::getSettings() const
{
    return m_settings;
}

void TextReplacementConfig::setSettings(const ReplacementSettings& settings)
{
    m_settings = settings;
}

TextReplacementConfig::ReplacementSettings TextReplacementConfig::getDefaultSettings()
{
    return ReplacementSettings();
}
