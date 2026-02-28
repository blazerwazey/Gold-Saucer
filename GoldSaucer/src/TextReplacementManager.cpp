#include "TextReplacementManager.h"
#include "NameGenerator.h"
#include <QDebug>
#include <QRandomGenerator>
#include <QSettings>

TextReplacementManager* TextReplacementManager::s_instance = nullptr;

TextReplacementManager& TextReplacementManager::instance()
{
    if (!s_instance) {
        s_instance = new TextReplacementManager();
    }
    return *s_instance;
}

TextReplacementManager::TextReplacementManager()
    : m_settingsLoaded(false)
{
    loadSettings();
}

void TextReplacementManager::registerRandomizer(const QString& name, int priority)
{
    m_randomizerPriorities[name] = priority;
    qDebug() << "Registered randomizer:" << name << "with priority:" << priority;
}

void TextReplacementManager::unregisterRandomizer(const QString& name)
{
    m_randomizerPriorities.remove(name);
    qDebug() << "Unregistered randomizer:" << name;
}

TextReplacementManager::ReplacementResult TextReplacementManager::requestReplacement(
    quint16 itemId, const QString& originalName, ItemCategory category, const QString& requestedBy)
{
    ReplacementRequest request(itemId, originalName, category, requestedBy);
    
    // Set priority based on randomizer
    if (m_randomizerPriorities.contains(requestedBy)) {
        request.priority = m_randomizerPriorities[requestedBy];
    }
    
    return processRequest(request);
}

bool TextReplacementManager::applyAllReplacements(KernelBinParser& parser)
{
    if (!m_settings.enabled) {
        qDebug() << "Text replacement disabled in settings";
        return true;
    }
    
    qDebug() << "Applying" << m_pendingRequests.size() << "text replacements";
    
    // Resolve conflicts before applying
    resolveConflicts();
    
    bool allSuccess = true;
    m_appliedReplacements.clear();
    
    for (const ReplacementRequest& request : m_pendingRequests) {
        ReplacementResult result = processRequest(request);
        m_appliedReplacements.append(result);
        
        if (!result.success) {
            qWarning() << "Failed to replace item" << request.itemId << ":" << result.errorMessage;
            allSuccess = false;
        } else {
            // Apply the replacement to the parser
            switch (request.category) {
                case ItemCategory::Weapon:
                    if (shouldReplaceCategory(ItemCategory::Weapon)) {
                        parser.replaceWeaponName(request.itemId, result.newName);
                    }
                    break;
                case ItemCategory::Armor:
                    if (shouldReplaceCategory(ItemCategory::Armor)) {
                        parser.replaceArmorName(request.itemId, result.newName);
                    }
                    break;
                case ItemCategory::Accessory:
                    if (shouldReplaceCategory(ItemCategory::Accessory)) {
                        parser.replaceAccessoryName(request.itemId, result.newName);
                    }
                    break;
                case ItemCategory::Materia:
                    if (shouldReplaceCategory(ItemCategory::Materia)) {
                        parser.replaceMateriaName(request.itemId, result.newName);
                    }
                    break;
                case ItemCategory::KeyItem:
                    if (shouldReplaceCategory(ItemCategory::KeyItem)) {
                        parser.replaceKeyItemName(request.itemId, result.newName);
                    }
                    break;
                default:
                    if (shouldReplaceCategory(ItemCategory::Consumable)) {
                        parser.replaceItemText(request.itemId, result.newName, request.category);
                    }
                    break;
            }
        }
    }
    
    // Clear pending requests after applying
    m_pendingRequests.clear();
    
    qDebug() << "Text replacement application complete. Success:" << allSuccess;
    return allSuccess;
}

void TextReplacementManager::addReplacementRequest(const ReplacementRequest& request)
{
    m_pendingRequests.append(request);
}

void TextReplacementManager::addReplacementRequests(const QVector<ReplacementRequest>& requests)
{
    m_pendingRequests.append(requests);
}

void TextReplacementManager::clearAllRequests()
{
    m_pendingRequests.clear();
    m_appliedReplacements.clear();
    qDebug() << "Cleared all text replacement requests";
}

QVector<TextReplacementManager::ReplacementRequest> TextReplacementManager::getPendingRequests() const
{
    return m_pendingRequests;
}

QVector<TextReplacementManager::ReplacementResult> TextReplacementManager::getAppliedReplacements() const
{
    return m_appliedReplacements;
}

bool TextReplacementManager::hasPendingRequests() const
{
    return !m_pendingRequests.isEmpty();
}

QString TextReplacementManager::getSummary() const
{
    return QString("Pending: %1, Applied: %2, Enabled: %3")
           .arg(m_pendingRequests.size())
           .arg(m_appliedReplacements.size())
           .arg(m_settings.enabled ? "Yes" : "No");
}

void TextReplacementManager::loadSettings()
{
    if (m_settingsLoaded) return;
    
    TextReplacementConfig config;
    m_settings = config.getSettings();
    m_settingsLoaded = true;
    
    qDebug() << "Loaded text replacement settings. Enabled:" << m_settings.enabled;
}

void TextReplacementManager::saveSettings()
{
    TextReplacementConfig config;
    config.setSettings(m_settings);
    qDebug() << "Saved text replacement settings";
}

TextReplacementConfig::ReplacementSettings TextReplacementManager::getSettings() const
{
    return m_settings;
}

void TextReplacementManager::updateSettings(const TextReplacementConfig::ReplacementSettings& settings)
{
    m_settings = settings;
    saveSettings();
}

QString TextReplacementManager::generateReplacementName(quint16 itemId, ItemCategory category, const QString& prefix)
{
    // Use the intelligent NameGenerator for better naming
    NameGenerator& nameGen = NameGenerator::instance();
    NameGenerator::ItemTier tier = nameGen.detectItemTier(itemId, category);
    NameGenerator::NamingStyle style = NameGenerator::Descriptive; // Default to descriptive
    
    QString name = nameGen.generateName(itemId, category, tier, style);
    
    // Add color coding if enabled
    if (m_settings.useColorCoding) {
        FF7Color color = TextEncoder::getItemColor(category);
        QByteArray coloredName = TextEncoder::encodeColoredText(name, color);
        name = QString::fromLatin1(coloredName);
    }
    
    return name;
}

QString TextReplacementManager::getItemPrefix(ItemCategory category)
{
    switch (category) {
        case ItemCategory::Weapon: return m_settings.weaponPrefix;
        case ItemCategory::Armor: return m_settings.armorPrefix;
        case ItemCategory::Accessory: return m_settings.accessoryPrefix;
        case ItemCategory::Materia: return m_settings.materiaPrefix;
        case ItemCategory::KeyItem: return m_settings.keyItemPrefix;
        default: return m_settings.itemPrefix;
    }
}

bool TextReplacementManager::shouldReplaceCategory(ItemCategory category)
{
    switch (category) {
        case ItemCategory::Weapon: return m_settings.weapons;
        case ItemCategory::Armor: return m_settings.armor;
        case ItemCategory::Accessory: return m_settings.accessories;
        case ItemCategory::Materia: return m_settings.materia;
        case ItemCategory::KeyItem: return m_settings.keyItems;
        default: return m_settings.items;
    }
}

TextReplacementManager::ReplacementResult TextReplacementManager::processRequest(const ReplacementRequest& request)
{
    if (!m_settings.enabled) {
        return ReplacementResult(false, QString(), "Text replacement disabled");
    }
    
    if (!shouldReplaceCategory(request.category)) {
        return ReplacementResult(false, request.originalName, QString("Category replacement disabled"));
    }
    
    QString prefix = getItemPrefix(request.category);
    QString newName = generateReplacementName(request.itemId, request.category, prefix);
    
    // Validate the new name
    if (!TextEncoder::isValidItemName(newName, request.category)) {
        return ReplacementResult(false, request.originalName, "Generated name is invalid");
    }
    
    qDebug() << "Replacing item" << request.itemId << "(" << request.originalName << ") with" << newName 
             << "requested by" << request.requestedBy;
    
    return ReplacementResult(true, newName);
}

void TextReplacementManager::resolveConflicts()
{
    // Sort requests by priority (higher priority first)
    std::sort(m_pendingRequests.begin(), m_pendingRequests.end(), 
              [](const ReplacementRequest& a, const ReplacementRequest& b) {
                  return a.priority > b.priority;
              });
    
    // Remove duplicate requests for the same item (keep highest priority)
    QMap<quint16, ReplacementRequest> uniqueRequests;
    for (const ReplacementRequest& request : m_pendingRequests) {
        if (!uniqueRequests.contains(request.itemId) || 
            request.priority > uniqueRequests[request.itemId].priority) {
            uniqueRequests[request.itemId] = request;
        }
    }
    
    m_pendingRequests = uniqueRequests.values().toVector();
    
    if (m_pendingRequests.size() < uniqueRequests.size()) {
        qDebug() << "Resolved" << (uniqueRequests.size() - m_pendingRequests.size()) << "conflicting replacement requests";
    }
}
