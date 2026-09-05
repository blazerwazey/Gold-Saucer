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
#include <QStackedWidget>
#include <QElapsedTimer>
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

protected:
    // The rail rows are plain QWidgets carrying a "stepIndex" property; this is
    // what makes them clickable without a widget subclass each.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // The window is a five-step wizard plus a finish screen. Steps 1-4 are the
    // setup the player walks; StepRun is the live run; StepDone reports it. The
    // rail is hidden on StepDone so the report gets the full width.
    enum Step { StepGame = 0, StepSeed, StepOptions, StepOutput, StepRun, StepDone, StepCount };
    // Passes, in the order startRandomization() runs them. Crater and IRO are
    // conditional and their rows are hidden when they will not run.
    enum Stage { StageCopy = 0, StageShops, StageFields, StageEquip, StageCrater, StageIro, StageCount };

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

    // --- wizard plumbing ----------------------------------------------------
    QWidget* buildRail();
    QWidget* buildStepGame();
    QWidget* buildStepSeed();
    QWidget* buildStepOptions();
    QWidget* buildStepOutput();
    QWidget* buildStepRun();
    QWidget* buildStepDone();
    void goToStep(int step);
    void refreshRail();
    // Re-reads the install path and repaints step 1's detection panel. Also the
    // gate on Continue: a path we cannot read is not a path worth walking on.
    bool refreshInstallDetection();
    // Repaints step 2's summary from the held .apff7, and step 3/4 from the lock.
    void refreshSeedSummary();
    void refreshOptionsLockUi();
    void refreshOutputStep();
    void setStage(int stage, int state);          // 0 pending, 1 running, 2 done, 3 skipped
    void setStageVisible(int stage, bool visible);

    // UI Elements
    QLineEdit* m_ff7PathEdit;
    QLineEdit* m_outputFolderEdit;
    QCheckBox* m_shopCheckBox;
    QCheckBox* m_fieldCheckBox;
    QCheckBox* m_keyItemCheckBox;      // never shown: see setupUI
    QCheckBox* m_equipmentCheckBox;
    QCheckBox* m_archipelagoCheckBox;  // never shown: an indicator, replaced by the rail
    QCheckBox* m_freeRoamCheckBox;     // never shown: dictated by the seed
    QCheckBox* m_iroCheckBox;
    QLineEdit* m_archipelagoJsonEdit;  // never shown: replaced by the seed card

    QPushButton* m_importArchipelagoButton;
    QPushButton* m_randomSeedButton;
    QPushButton* m_loadConfigButton;
    QSpinBox* m_seedSpin;
    QComboBox* m_equipmentCombo;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    QTextEdit* m_consoleOutput;

    // Wizard
    QStackedWidget* m_pages = nullptr;
    QWidget* m_rail = nullptr;
    int m_currentStep = StepGame;
    QWidget* m_railRow[StepCount]    = {};
    QLabel*  m_railBullet[StepCount] = {};
    QLabel*  m_railTitle[StepCount]  = {};
    QLabel*  m_railSub[StepCount]    = {};

    // Step 1 — install detection
    QWidget* m_detectCard        = nullptr;
    QLabel*  m_detectHeading     = nullptr;
    QLabel*  m_detectRelease     = nullptr;
    QLabel*  m_detectExe         = nullptr;
    QLabel*  m_detectFlevel      = nullptr;
    QLabel*  m_detectKernel      = nullptr;
    QPushButton* m_gameContinue  = nullptr;

    // Step 2 — seed
    QWidget* m_seedCard          = nullptr;
    QLabel*  m_seedFileLabel     = nullptr;
    QLabel*  m_seedPlayerLabel   = nullptr;
    QLabel*  m_seedChips         = nullptr;
    QLabel*  m_seedEmptyHint     = nullptr;
    QPushButton* m_seedReplaceButton = nullptr;
    QPushButton* m_seedSkipButton    = nullptr;

    // Step 3 — options
    QWidget* m_lockedSectionHeader = nullptr;
    QLabel*  m_lockedFootnote      = nullptr;
    QLabel*  m_shopYamlNote        = nullptr;

    // Step 4 — output
    QLabel*  m_seedLockedLabel   = nullptr;   // shown instead of the spin when locked
    QWidget* m_seedEditRow       = nullptr;
    QWidget* m_seedLockedRow     = nullptr;

    // Step 5 — run
    QLabel*  m_stageIcon[StageCount]  = {};
    QLabel*  m_stageLabel[StageCount] = {};
    QWidget* m_stageRow[StageCount]   = {};
    QLabel*  m_runHeadline       = nullptr;
    QLabel*  m_runDetail         = nullptr;
    QPushButton* m_cancelButton  = nullptr;
    bool m_cancelRequested       = false;
    QElapsedTimer m_runTimer;

    // Step 6 — done
    QLabel*  m_doneSubtitle      = nullptr;
    QLabel*  m_donePasses        = nullptr;
    QLineEdit* m_doneFolderEdit  = nullptr;
    QWidget* m_doneIroRow        = nullptr;
    QLineEdit* m_doneIroEdit     = nullptr;
    QString  m_lastIroPath;

    // Archipelago state
    bool m_archipelagoModeEnabled;
    QString m_archipelagoJsonPath;

    // Option-lock state (see setOptionsLocked)
    bool m_optionsLocked = false;
    QHash<QWidget*, QString> m_unlockedTooltips;

    // Archipelago methods
    void importArchipelagoJson();

    // Configuration
    Config m_config;
};
