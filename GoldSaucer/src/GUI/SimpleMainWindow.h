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
    void applyConfigToUI();
    bool validateArchipelagoJSON(const QString& filePath);
    
    // UI Elements
    QLineEdit* m_ff7PathEdit;
    QLineEdit* m_outputFolderEdit;
    QCheckBox* m_shopCheckBox;
    QCheckBox* m_fieldCheckBox;
    QCheckBox* m_keyItemCheckBox;
    QCheckBox* m_equipmentCheckBox;
    QCheckBox* m_archipelagoCheckBox;
    QLineEdit* m_archipelagoJsonEdit;
    
    QSlider* m_nameComplexitySlider;
    QLabel* m_complexityLabel;
    QCheckBox* m_useIntelligentNamingCheckBox;
    QGroupBox* m_previewGroup;
    QPushButton* m_importArchipelagoButton;
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
