#pragma once

#include <QMainWindow>
#include <QLineEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QProgressBar>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>
#include <QGroupBox>
#include <QSlider>
#include <QHash>
#include "../Config.h"

class SimpleMainWindow : public QMainWindow
{
public:
    explicit SimpleMainWindow(QWidget *parent = nullptr);

private slots:
    void browseFF7Path();
    void browseOutputFolder();
    void startRandomization();
    void loadConfig();
    void saveConfig();
    void resetToDefaults();
    void randomSeed();
    void appendConsoleMessage(const QString& message);
    void importArchipelagoJSON();
    void toggleArchipelagoMode(bool enabled);

private:
    void setupUI();
    void updateConfig();
    QString configFilePath() const;
    void applyConfigToUI();
    bool validateArchipelagoJSON(const QString& filePath);
    // Everything a loaded .apff7 dictates gets disabled while a seed is held, so
    // the player can't desync the build from the multiworld. Paths, the .IRO
    // toggle and Import/Save/Reset/Start stay live.
    void setOptionsLocked(bool locked);
    QList<QWidget*> lockableOptionWidgets() const;
    void clearArchipelagoSeed();
    
    // UI Elements
    QLineEdit* m_ff7PathEdit;
    QLineEdit* m_outputFolderEdit;
    QCheckBox* m_shopCheckBox;
    QCheckBox* m_fieldCheckBox;
    QCheckBox* m_keyItemCheckBox;
    QCheckBox* m_equipmentCheckBox;
    QCheckBox* m_archipelagoCheckBox;
    QCheckBox* m_freeRoamCheckBox;
    QCheckBox* m_iroCheckBox;
    QLineEdit* m_archipelagoJsonEdit;
    
    QSlider* m_nameComplexitySlider;
    QLabel* m_complexityLabel;
    QCheckBox* m_useIntelligentNamingCheckBox;
    QGroupBox* m_previewGroup;
    QPushButton* m_importArchipelagoButton;
    QPushButton* m_randomSeedButton;
    QPushButton* m_loadConfigButton;
    QSpinBox* m_shopPoolSpin;
    QSpinBox* m_shopPriceSpin;
    QSpinBox* m_seedSpin;
    QComboBox* m_pickupCombo;
    QComboBox* m_equipmentCombo;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    QTextEdit* m_consoleOutput;
    
    // Archipelago state
    bool m_archipelagoModeEnabled;
    QString m_archipelagoJsonPath;

    // Option-lock state (see setOptionsLocked)
    bool m_optionsLocked = false;
    QHash<QWidget*, QString> m_unlockedTooltips;
    
    // Archipelago methods
    void importArchipelagoJson();
    
    // Text replacement methods - REMOVED (now handled automatically by FF7TK field randomization)
    // void setupTextReplacementControls();
    // void loadTextReplacementSettings();
    // void saveTextReplacementSettings();
    // void toggleTextReplacement(bool enabled);
    // void updateTextReplacementControls();
    
    // Enhanced text replacement methods - REMOVED
    // void setupEnhancedTextControls();
    // void generateNamePreview();
    // void updateNamingStyle();
    // void updateComplexityLabel(int value);
    // void toggleIntelligentNaming(bool enabled);
    // void refreshPreview();
    
    // Configuration
    Config m_config;
};
