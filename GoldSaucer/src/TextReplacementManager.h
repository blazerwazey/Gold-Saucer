#pragma once

#include <QString>
#include <QVector>
#include <QMap>
#include "TextReplacementConfig.h"
#include "TextEncoder.h"
#include "KernelBinParser.h"

// Centralized text replacement manager to coordinate all randomizers
class TextReplacementManager
{
public:
    struct ReplacementRequest {
        quint16 itemId;
        QString originalName;
        ItemCategory category;
        QString requestedBy; // Which randomizer made the request
        int priority; // Higher priority overrides lower priority
        
        ReplacementRequest() : itemId(0), category(ItemCategory::Consumable), priority(0) {}
        ReplacementRequest(quint16 id, const QString& name, ItemCategory cat, const QString& requester, int prio = 0)
            : itemId(id), originalName(name), category(cat), requestedBy(requester), priority(prio) {}
    };
    
    struct ReplacementResult {
        bool success;
        QString newName;
        QString errorMessage;
        
        ReplacementResult() : success(false) {}
        ReplacementResult(bool s, const QString& name, const QString& error = QString())
            : success(s), newName(name), errorMessage(error) {}
    };
    
    static TextReplacementManager& instance();
    
    // Registration and coordination
    void registerRandomizer(const QString& name, int priority = 0);
    void unregisterRandomizer(const QString& name);
    
    // Text replacement coordination
    ReplacementResult requestReplacement(quint16 itemId, const QString& originalName, 
                                        ItemCategory category, const QString& requestedBy);
    bool applyAllReplacements(KernelBinParser& parser);
    
    // Batch operations
    void addReplacementRequest(const ReplacementRequest& request);
    void addReplacementRequests(const QVector<ReplacementRequest>& requests);
    void clearAllRequests();
    
    // Status and debugging
    QVector<ReplacementRequest> getPendingRequests() const;
    QVector<ReplacementResult> getAppliedReplacements() const;
    bool hasPendingRequests() const;
    QString getSummary() const;
    
    // Configuration
    void loadSettings();
    void saveSettings();
    TextReplacementConfig::ReplacementSettings getSettings() const;
    void updateSettings(const TextReplacementConfig::ReplacementSettings& settings);
    
private:
    TextReplacementManager();
    ~TextReplacementManager() = default;
    
    // Prevent copying
    TextReplacementManager(const TextReplacementManager&) = delete;
    TextReplacementManager& operator=(const TextReplacementManager&) = delete;
    
    // Internal methods
    QString generateReplacementName(quint16 itemId, ItemCategory category, const QString& prefix);
    QString getItemPrefix(ItemCategory category);
    bool shouldReplaceCategory(ItemCategory category);
    ReplacementResult processRequest(const ReplacementRequest& request);
    void resolveConflicts();
    
    // Member variables
    QMap<QString, int> m_randomizerPriorities;
    QVector<ReplacementRequest> m_pendingRequests;
    QVector<ReplacementResult> m_appliedReplacements;
    TextReplacementConfig::ReplacementSettings m_settings;
    bool m_settingsLoaded;
    
    static TextReplacementManager* s_instance;
};
