#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QProgressBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QDebug>
#include <QRandomGenerator>
#include <QTextEdit>
#include <QScrollBar>
#include <QCoreApplication>
#include <QGroupBox>
#include <QSlider>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QFileInfo>
#include "SimpleMainWindow.h"
// REMOVED: Text replacement includes - no longer needed
// #include "../TextReplacementConfig.h"
// #include "../TextEncoder.h"
// #include "../NameGenerator.h"
// #include "../TextReplacementManager.h"
// #include "../UserFeedback.h"
#include "../Randomizer.h"
#include "../Config.h"

SimpleMainWindow::SimpleMainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_archipelagoModeEnabled(false)
{
    setupUI();
    loadConfig();
    
    // Add initial message
    appendConsoleMessage("Gold Saucer FF7 Randomizer GUI started");
    appendConsoleMessage("Version 1.0.0");
    appendConsoleMessage("Ready for randomization...");
}

void SimpleMainWindow::setupUI()
{
    setWindowTitle("Gold Saucer - FF7 Randomizer");
    setMinimumSize(800, 600);
    resize(900, 700);
    
    // Central widget
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    // Main layout
    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    
    // Title
    QLabel* titleLabel = new QLabel("Gold Saucer - FF7 Randomizer", this);
    titleLabel->setStyleSheet("font-size: 24px; font-weight: bold; color: #ffd700;");
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);
    
    // FF7 Path Selection
    QHBoxLayout* pathLayout = new QHBoxLayout();
    QLabel* pathLabel = new QLabel("FF7 Installation Path:", this);
    pathLabel->setToolTip("Path to your Final Fantasy VII installation.\nShould contain the 'data' folder with flevel.lgp, kernel.bin, etc.");
    m_ff7PathEdit = new QLineEdit(this);
    m_ff7PathEdit->setPlaceholderText("Select Final Fantasy VII installation directory...");
    m_ff7PathEdit->setToolTip("Path to your Final Fantasy VII installation.\nShould contain the 'data' folder with flevel.lgp, kernel.bin, etc.");
    QPushButton* browseButton = new QPushButton("Browse...", this);
    browseButton->setToolTip("Browse for Final Fantasy VII installation directory.");
    
    pathLayout->addWidget(pathLabel);
    pathLayout->addWidget(m_ff7PathEdit);
    pathLayout->addWidget(browseButton);
    mainLayout->addLayout(pathLayout);
    
    // Output Folder Selection
    QHBoxLayout* outputLayout = new QHBoxLayout();
    QLabel* outputLabel = new QLabel("Output Folder:", this);
    outputLabel->setToolTip("Directory where randomized files will be saved.\nThis should be separate from your original FF7 installation.");
    m_outputFolderEdit = new QLineEdit(this);
    m_outputFolderEdit->setPlaceholderText("Select output directory for randomized files...");
    m_outputFolderEdit->setToolTip("Directory where randomized files will be saved.\nThis should be separate from your original FF7 installation.");
    QPushButton* browseOutputButton = new QPushButton("Browse...", this);
    browseOutputButton->setToolTip("Browse for output directory to save randomized files.");
    
    outputLayout->addWidget(outputLabel);
    outputLayout->addWidget(m_outputFolderEdit);
    outputLayout->addWidget(browseOutputButton);
    mainLayout->addLayout(outputLayout);
    
    // Features
    QLabel* featuresLabel = new QLabel("Randomization Features:", this);
    featuresLabel->setStyleSheet("font-weight: bold;");
    mainLayout->addWidget(featuresLabel);
    
    QVBoxLayout* featuresLayout = new QVBoxLayout();
    m_shopCheckBox = new QCheckBox("Shop Randomization", this);
    m_shopCheckBox->setToolTip("Randomizes shop inventories and prices.\nItems are replaced with appropriate category items (weapons in weapon shops, etc.).");
    m_fieldCheckBox = new QCheckBox("Field Pickup Randomization", this);
    m_fieldCheckBox->setToolTip("Randomizes items and materia found in field pickups.\nChests, treasure chests, and field rewards are randomized.");
    m_keyItemCheckBox = new QCheckBox("Key Item Randomization (Experimental)", this);
    m_keyItemCheckBox->setToolTip("Swaps key items with regular item pickups within the same field.\nWARNING: May cause softlocks if key items become inaccessible!");
    m_equipmentCheckBox = new QCheckBox("Starting Equipment Randomization", this);
    m_equipmentCheckBox->setToolTip("Randomizes equipment given to characters at game start.\nCharacters will receive random equipment of the selected tier.");
    
    featuresLayout->addWidget(m_shopCheckBox);
    featuresLayout->addWidget(m_fieldCheckBox);
    featuresLayout->addWidget(m_keyItemCheckBox);
    featuresLayout->addWidget(m_equipmentCheckBox);
    mainLayout->addLayout(featuresLayout);
    
    // Archipelago Section
    QLabel* archipelagoLabel = new QLabel("Archipelago Multiworld:", this);
    archipelagoLabel->setStyleSheet("font-weight: bold;");
    mainLayout->addWidget(archipelagoLabel);
    
    QVBoxLayout* archipelagoLayout = new QVBoxLayout();
    
    // Archipelago JSON import
    QHBoxLayout* jsonLayout = new QHBoxLayout();
    m_archipelagoJsonEdit = new QLineEdit(this);
    m_archipelagoJsonEdit->setPlaceholderText("Select Archipelago JSON file...");
    m_archipelagoJsonEdit->setReadOnly(true);
    m_importArchipelagoButton = new QPushButton("Import JSON...", this);
    m_importArchipelagoButton->setEnabled(true);
    
    jsonLayout->addWidget(new QLabel("Archipelago JSON:", this));
    jsonLayout->addWidget(m_archipelagoJsonEdit);
    jsonLayout->addWidget(m_importArchipelagoButton);
    
    archipelagoLayout->addLayout(jsonLayout);
    
    // Archipelago toggle (initially disabled)
    m_archipelagoCheckBox = new QCheckBox("Enable Archipelago Mode", this);
    m_archipelagoCheckBox->setEnabled(false);
    m_archipelagoCheckBox->setToolTip("Import a valid Archipelago JSON file to enable this option");
    archipelagoLayout->addWidget(m_archipelagoCheckBox);
    
    mainLayout->addLayout(archipelagoLayout);
    
    // Text Replacement Section - REMOVED (now handled automatically by FF7TK field randomization)
    // setupTextReplacementControls();
    // setupEnhancedTextControls(); // TODO: Fix ItemCategory enum issues
    
    // Settings
    QLabel* settingsLabel = new QLabel("Settings:", this);
    settingsLabel->setStyleSheet("font-weight: bold;");
    mainLayout->addWidget(settingsLabel);
    
    QGridLayout* settingsLayout = new QGridLayout();
    
    // Shop settings
    QLabel* shopPoolLabel = new QLabel("Shop Item Pool Size:", this);
    shopPoolLabel->setToolTip("Number of random items available for shop inventories.\nLarger pools = more variety, smaller pools = more repeats.");
    settingsLayout->addWidget(shopPoolLabel, 0, 0);
    m_shopPoolSpin = new QSpinBox(this);
    m_shopPoolSpin->setRange(10, 200);
    m_shopPoolSpin->setValue(50);
    m_shopPoolSpin->setToolTip("Number of random items available for shop inventories.\nLarger pools = more variety, smaller pools = more repeats.");
    settingsLayout->addWidget(m_shopPoolSpin, 0, 1);
    
    QLabel* shopPriceLabel = new QLabel("Shop Price Variance (%):", this);
    shopPriceLabel->setToolTip("Maximum percentage that shop prices can vary from original.\n0% = no change, 100% = prices can be 0-200% of original.");
    settingsLayout->addWidget(shopPriceLabel, 1, 0);
    m_shopPriceSpin = new QSpinBox(this);
    m_shopPriceSpin->setRange(0, 100);
    m_shopPriceSpin->setValue(50);
    m_shopPriceSpin->setToolTip("Maximum percentage that shop prices can vary from original.\n0% = no change, 100% = prices can be 0-200% of original.");
    settingsLayout->addWidget(m_shopPriceSpin, 1, 1);
    
    // Field pickup settings
    QLabel* pickupLabel = new QLabel("Field Pickup Rarity:", this);
    pickupLabel->setToolTip("Controls the quality of items found in field pickups.\nBalanced = mix of common/rare items\nRandom = completely random\nHigh-tier Only = only rare/powerful items");
    settingsLayout->addWidget(pickupLabel, 2, 0);
    m_pickupCombo = new QComboBox(this);
    m_pickupCombo->addItems({"Balanced", "Random", "High-tier Only"});
    m_pickupCombo->setToolTip("Controls the quality of items found in field pickups.\nBalanced = mix of common/rare items\nRandom = completely random\nHigh-tier Only = only rare/powerful items");
    settingsLayout->addWidget(m_pickupCombo, 2, 1);
    
    // Starting equipment settings
    QLabel* equipmentLabel = new QLabel("Starting Equipment Tier:", this);
    equipmentLabel->setToolTip("Quality of equipment given to characters at game start.\nWeak = basic equipment\nBalanced = standard equipment\nStrong = advanced equipment");
    settingsLayout->addWidget(equipmentLabel, 3, 0);
    m_equipmentCombo = new QComboBox(this);
    m_equipmentCombo->addItems({"Weak", "Balanced", "Strong"});
    m_equipmentCombo->setCurrentIndex(1);
    m_equipmentCombo->setToolTip("Quality of equipment given to characters at game start.\nWeak = basic equipment\nBalanced = standard equipment\nStrong = advanced equipment");
    settingsLayout->addWidget(m_equipmentCombo, 3, 1);
    
    // Seed
    QLabel* seedLabel = new QLabel("Random Seed:", this);
    seedLabel->setToolTip("Seed value for randomization.\nSame seed = same results, different seed = different randomization.");
    settingsLayout->addWidget(seedLabel, 4, 0);
    m_seedSpin = new QSpinBox(this);
    m_seedSpin->setRange(0, 999999);
    m_seedSpin->setValue(12345);
    m_seedSpin->setToolTip("Seed value for randomization.\nSame seed = same results, different seed = different randomization.");
    settingsLayout->addWidget(m_seedSpin, 4, 1);
    
    QPushButton* randomSeedButton = new QPushButton("Random Seed", this);
    randomSeedButton->setToolTip("Generate a random seed value.");
    settingsLayout->addWidget(randomSeedButton, 4, 2);
    
    mainLayout->addLayout(settingsLayout);
    
    // Progress
    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    mainLayout->addWidget(m_progressBar);
    
    // Status label
    m_statusLabel = new QLabel("Ready", this);
    mainLayout->addWidget(m_statusLabel);
    
    // Console Output
    QLabel* consoleLabel = new QLabel("Console Output:", this);
    consoleLabel->setStyleSheet("font-weight: bold;");
    mainLayout->addWidget(consoleLabel);
    
    m_consoleOutput = new QTextEdit(this);
    m_consoleOutput->setMaximumHeight(150);
    m_consoleOutput->setMinimumHeight(100);
    m_consoleOutput->setReadOnly(true);
    m_consoleOutput->setStyleSheet("background-color: #2b2b2b; color: #00ff00; font-family: 'Courier New', monospace;");
    mainLayout->addWidget(m_consoleOutput);
    
    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    
    QPushButton* loadButton = new QPushButton("Load Config", this);
    QPushButton* saveButton = new QPushButton("Save Config", this);
    QPushButton* resetButton = new QPushButton("Reset", this);
    
    QPushButton* startButton = new QPushButton("Start Randomization", this);
    startButton->setStyleSheet("background-color: #00cc66; color: white; font-weight: bold; padding: 10px;");
    
    buttonLayout->addWidget(loadButton);
    buttonLayout->addWidget(saveButton);
    buttonLayout->addWidget(resetButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(startButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Connect signals
    connect(browseButton, &QPushButton::clicked, this, &SimpleMainWindow::browseFF7Path);
    connect(browseOutputButton, &QPushButton::clicked, this, &SimpleMainWindow::browseOutputFolder);
    connect(startButton, &QPushButton::clicked, this, &SimpleMainWindow::startRandomization);
    connect(loadButton, &QPushButton::clicked, this, &SimpleMainWindow::loadConfig);
    connect(saveButton, &QPushButton::clicked, this, &SimpleMainWindow::saveConfig);
    connect(resetButton, &QPushButton::clicked, this, &SimpleMainWindow::resetToDefaults);
    connect(randomSeedButton, &QPushButton::clicked, this, &SimpleMainWindow::randomSeed);
    
    // Archipelago connections
    connect(m_importArchipelagoButton, &QPushButton::clicked, this, &SimpleMainWindow::importArchipelagoJSON);
    connect(m_archipelagoCheckBox, &QCheckBox::toggled, this, &SimpleMainWindow::toggleArchipelagoMode);
}

void SimpleMainWindow::browseFF7Path()
{
    QString path = QFileDialog::getExistingDirectory(this, 
        "Select Final Fantasy VII Installation Directory",
        m_ff7PathEdit->text().isEmpty() ? QDir::homePath() : m_ff7PathEdit->text());
    
    if (!path.isEmpty()) {
        m_ff7PathEdit->setText(path);
    }
}

void SimpleMainWindow::browseOutputFolder()
{
    QString path = QFileDialog::getExistingDirectory(this,
        "Select Output Directory for Randomized Files",
        m_outputFolderEdit->text().isEmpty() ? QDir::homePath() : m_outputFolderEdit->text());
    
    if (!path.isEmpty()) {
        m_outputFolderEdit->setText(path);
    }
}

void SimpleMainWindow::startRandomization()
{
    QString ff7Path = m_ff7PathEdit->text();
    if (ff7Path.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select FF7 installation path");
        return;
    }
    
    QDir ff7Dir(ff7Path);
    if (!ff7Dir.exists()) {
        QMessageBox::warning(this, "Error", "FF7 installation path does not exist");
        return;
    }
    
    if (!ff7Dir.exists("data")) {
        QMessageBox::warning(this, "Error", "Invalid FF7 installation: data directory not found");
        return;
    }
    
    // Clear console and add header
    m_consoleOutput->clear();
    appendConsoleMessage("=== Starting Randomization ===");
    appendConsoleMessage("FF7 Path: " + ff7Path);
    appendConsoleMessage("Output: " + m_outputFolderEdit->text());
    
    // Update config
    updateConfig();
    
    // Create randomizer and run
    try {
        Randomizer randomizer(ff7Path, m_config);
        
        m_progressBar->setVisible(true);
        m_progressBar->setValue(0);
        m_statusLabel->setText("Preparing output directory...");
        appendConsoleMessage("Preparing output directory...");
        QApplication::processEvents();
        
        if (!randomizer.copyOriginalFiles()) {
            appendConsoleMessage("ERROR: Failed to copy original files to output directory");
            QMessageBox::critical(this, "Error", "Failed to copy original files to output directory");
            return;
        }
        appendConsoleMessage("Original files copied successfully");
        
        if (m_config.isFeatureEnabled(Config::ShopRandomization)) {
            m_progressBar->setValue(25);
            m_statusLabel->setText("Randomizing Shops...");
            appendConsoleMessage("Randomizing Shops...");
            QApplication::processEvents();
            
            if (!randomizer.randomizeShops()) {
                appendConsoleMessage("ERROR: Shop randomization failed");
                QMessageBox::critical(this, "Error", "Shop randomization failed");
                return;
            }
            appendConsoleMessage("Shop randomization completed successfully");
        }
        
        if (m_config.isFeatureEnabled(Config::FieldPickupRandomization)) {
            m_progressBar->setValue(50);
            m_statusLabel->setText("Randomizing Field Pickups...");
            appendConsoleMessage("Randomizing Field Pickups...");
            QApplication::processEvents();
            
            if (!randomizer.randomizeFieldPickups()) {
                appendConsoleMessage("ERROR: Field pickup randomization failed");
                QMessageBox::critical(this, "Error", "Field pickup randomization failed");
                return;
            }
            appendConsoleMessage("Field pickup randomization completed successfully");
        }
        
        if (m_config.isFeatureEnabled(Config::StartingEquipmentRandomization)) {
            m_progressBar->setValue(75);
            m_statusLabel->setText("Randomizing Starting Equipment...");
            appendConsoleMessage("Randomizing Starting Equipment...");
            QApplication::processEvents();
            
            if (!randomizer.randomizeStartingEquipment()) {
                appendConsoleMessage("ERROR: Starting equipment randomization failed");
                QMessageBox::critical(this, "Error", "Starting equipment randomization failed");
                return;
            }
            appendConsoleMessage("Starting equipment randomization completed successfully");
        }
        
        // Complete
        m_progressBar->setValue(100);
        m_statusLabel->setText("Randomization Complete!");
        appendConsoleMessage("=== Randomization Complete ===");
        appendConsoleMessage("All files have been successfully randomized!");
        appendConsoleMessage("You can find the randomized files in your output folder.");
        
        QMessageBox::information(this, "Success", "Randomization completed successfully!");
        
    } catch (const std::exception& e) {
        appendConsoleMessage("ERROR: " + QString(e.what()));
        QMessageBox::critical(this, "Error", QString("Randomization failed: %1").arg(e.what()));
    }
    
    m_progressBar->setVisible(false);
    m_statusLabel->setText("Ready");
}

void SimpleMainWindow::loadConfig()
{
    QString configPath = QCoreApplication::applicationDirPath() + "/randomizer_config.json";
    if (m_config.loadFromFile(configPath)) {
        applyConfigToUI();
        appendConsoleMessage(QString("Config loaded from: %1").arg(configPath));
    } else {
        appendConsoleMessage(QString("Could not load config from: %1").arg(configPath));
    }
}

void SimpleMainWindow::saveConfig()
{
    updateConfig();
    QString configPath = QCoreApplication::applicationDirPath() + "/randomizer_config.json";
    bool saveResult = m_config.saveToFile(configPath);
    appendConsoleMessage(QString("Config saved to: %1 (Success: %2)").arg(configPath).arg(saveResult));
}

void SimpleMainWindow::resetToDefaults()
{
    m_config.setDefaults();
    applyConfigToUI();
}

void SimpleMainWindow::randomSeed()
{
    m_seedSpin->setValue(QRandomGenerator::global()->bounded(999999));
}

void SimpleMainWindow::updateConfig()
{
    // Features
    m_config.setFeatureEnabled(Config::ShopRandomization, m_shopCheckBox->isChecked());
    m_config.setFeatureEnabled(Config::FieldPickupRandomization, m_fieldCheckBox->isChecked());
    m_config.setKeyItemRandomization(m_keyItemCheckBox->isChecked());
    m_config.setFeatureEnabled(Config::StartingEquipmentRandomization, m_equipmentCheckBox->isChecked());
    
    // Text replacement settings - REMOVED (now handled automatically by FF7TK field randomization)
    // saveTextReplacementSettings();
    m_config.setFeatureEnabled(Config::ArchipelagoIntegration, m_archipelagoCheckBox->isChecked());
    
    // Settings
    m_config.setShopItemPoolSize(m_shopPoolSpin->value());
    m_config.setShopPriceVariance(m_shopPriceSpin->value() / 100.0);
    m_config.setPickupRarityMode(m_pickupCombo->currentIndex());
    m_config.setStartingEquipmentTier(m_equipmentCombo->currentIndex());
    m_config.setSeed(m_seedSpin->value());
    
    // Paths
    m_config.setOutputFolder(m_outputFolderEdit->text());
    m_config.setFF7Path(m_ff7PathEdit->text());
    
    // Archipelago settings
    if (m_archipelagoModeEnabled && !m_archipelagoJsonPath.isEmpty()) {
        appendConsoleMessage("Archipelago mode will be used for randomization");
    }
}

void SimpleMainWindow::applyConfigToUI()
{
    // Features
    m_shopCheckBox->setChecked(m_config.isFeatureEnabled(Config::ShopRandomization));
    m_fieldCheckBox->setChecked(m_config.isFeatureEnabled(Config::FieldPickupRandomization));
    m_keyItemCheckBox->setChecked(m_config.getKeyItemRandomization());
    m_equipmentCheckBox->setChecked(m_config.isFeatureEnabled(Config::StartingEquipmentRandomization));
    
    // Text replacement settings - REMOVED (now handled automatically by FF7TK field randomization)
    // loadTextReplacementSettings();
    
    // Archipelago mode (only enable if JSON was imported)
    bool archipelagoConfigEnabled = m_config.isFeatureEnabled(Config::ArchipelagoIntegration);
    if (archipelagoConfigEnabled && !m_archipelagoJsonPath.isEmpty()) {
        m_archipelagoCheckBox->setChecked(true);
        m_archipelagoCheckBox->setEnabled(true);
        m_archipelagoModeEnabled = true;
    } else {
        m_archipelagoCheckBox->setChecked(false);
        m_archipelagoCheckBox->setEnabled(false);
        m_archipelagoModeEnabled = false;
    }
    
    // Settings
    m_shopPoolSpin->setValue(m_config.getShopItemPoolSize());
    m_shopPriceSpin->setValue(static_cast<int>(m_config.getShopPriceVariance() * 100));
    m_pickupCombo->setCurrentIndex(m_config.getPickupRarityMode());
    m_equipmentCombo->setCurrentIndex(m_config.getStartingEquipmentTier());
    m_seedSpin->setValue(m_config.getSeed());
    
    // Paths
    m_outputFolderEdit->setText(m_config.getOutputFolder());
    m_ff7PathEdit->setText(m_config.getFF7Path());
}

void SimpleMainWindow::appendConsoleMessage(const QString& message)
{
    if (m_consoleOutput) {
        m_consoleOutput->append(message);
        // Auto-scroll to bottom
        QScrollBar *scrollBar = m_consoleOutput->verticalScrollBar();
        scrollBar->setValue(scrollBar->maximum());
    }
}

void SimpleMainWindow::importArchipelagoJSON()
{
    QString filePath = QFileDialog::getOpenFileName(this,
        "Select Archipelago JSON File",
        QDir::homePath(),
        "JSON Files (*.json)");
    
    if (filePath.isEmpty()) {
        return;
    }
    
    // Validate the JSON file
    if (!validateArchipelagoJSON(filePath)) {
        QMessageBox::warning(this, "Invalid JSON", 
            "The selected file is not a valid Archipelago JSON file.\n"
            "Please select a JSON file generated by the Archipelago multiworld system.");
        return;
    }
    
    // Enable Archipelago mode
    m_archipelagoJsonPath = filePath;
    m_archipelagoJsonEdit->setText(QFileInfo(filePath).fileName());
    m_archipelagoCheckBox->setEnabled(true);
    m_archipelagoCheckBox->setChecked(true);
    m_archipelagoModeEnabled = true;
    
    appendConsoleMessage("Archipelago JSON imported: " + QFileInfo(filePath).fileName());
    appendConsoleMessage("Archipelago mode enabled - foreign items will appear in shops and field pickups");
    
    QMessageBox::information(this, "Archipelago Enabled", 
        "Archipelago mode has been enabled!\n\n"
        "Foreign items will now appear in:\n"
        "• Shop inventories (as one-time purchases)\n"
        "• Field pickups (with colored text)\n\n"
        "The randomizer will use the imported JSON for item mapping.");
}

void SimpleMainWindow::toggleArchipelagoMode(bool enabled)
{
    m_archipelagoModeEnabled = enabled;
    
    if (enabled) {
        appendConsoleMessage("Archipelago mode ENABLED");
        m_archipelagoCheckBox->setToolTip("Archipelago mode is active - foreign items will be included");
    } else {
        appendConsoleMessage("Archipelago mode DISABLED");
        m_archipelagoCheckBox->setToolTip("Archipelago mode is inactive - only local items will be used");
    }
}

bool SimpleMainWindow::validateArchipelagoJSON(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    
    if (error.error != QJsonParseError::NoError) {
        return false;
    }
    
    QJsonObject root = doc.object();
    
    // Check for Archipelago-specific fields
    if (!root.contains("slot_data") && !root.contains("seed_name") && !root.contains("players")) {
        return false;
    }
    
    // Check for player data
    if (root.contains("slot_data")) {
        QJsonObject slotData = root["slot_data"].toObject();
        if (!slotData.contains("item_name_to_id") && !slotData.contains("id_to_item_name")) {
            return false;
        }
    }
    
    // Check for multiworld player information
    if (root.contains("players")) {
        QJsonArray players = root["players"].toArray();
        if (players.isEmpty()) {
            return false;
        }
    }
    
    return true;
}

// REMOVED: Text replacement controls are no longer needed
// Text replacement is now handled automatically by FF7TK field randomization
// void SimpleMainWindow::setupTextReplacementControls()
// {
//     // Text Replacement Section - REMOVED
// }

// REMOVED: All text replacement methods are no longer needed
// Text replacement is now handled automatically by FF7TK field randomization
// void SimpleMainWindow::loadTextReplacementSettings() { /* REMOVED */ }
// void SimpleMainWindow::saveTextReplacementSettings() { /* REMOVED */ }
// void SimpleMainWindow::toggleTextReplacement(bool enabled) { /* REMOVED */ }
// void SimpleMainWindow::updateTextReplacementControls() { /* REMOVED */ }

// REMOVED: All enhanced text methods are no longer needed
// void SimpleMainWindow::generateNamePreview() { /* REMOVED */ }
// void SimpleMainWindow::updateNamingStyle() { /* REMOVED */ }
// void SimpleMainWindow::updateComplexityLabel(int value) { /* REMOVED */ }
// void SimpleMainWindow::toggleIntelligentNaming(bool enabled) { /* REMOVED */ }
// void SimpleMainWindow::refreshPreview() { /* REMOVED */ }
