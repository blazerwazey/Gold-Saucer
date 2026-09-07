#include "../ApSeedFile.h"
#include "../GsLog.h"
#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QSpinBox>
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
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QFileInfo>
#include <QStackedWidget>
#include <QFrame>
#include <QDesktopServices>
#include <QUrl>
#include <QGuiApplication>
#include <QClipboard>
#include <QStyle>
#include <QEvent>
#include <QVariant>
#include <QTimer>
#include <QSet>
#include "SimpleMainWindow.h"
#include "../Randomizer.h"
#include "../Config.h"
#include "../IroExporter.h"

// ---------------------------------------------------------------------------
// Look and feel.
//
// The palette is the one the approved mockups were drawn in; the two typefaces
// they used (Archivo / JetBrains Mono) are NOT here, deliberately. Shipping font
// files would mean carrying binaries in the repo for a cosmetic gain, and Qt
// cannot pull a webfont. Segoe UI and Consolas are on every Windows 11 machine
// and are the closest stand-ins. Swap kUiFont / kMonoFont if fonts are ever
// bundled.
// ---------------------------------------------------------------------------
namespace {

const char* kUiFont   = "Segoe UI";
const char* kMonoFont = "Consolas";

const char* kStyleSheet = R"QSS(
QMainWindow, QWidget#page { background-color: #14161a; }
QWidget { color: #e8eaed; }
QToolTip { background-color: #1c1f25; color: #cfd4db; border: 1px solid #2e333c; padding: 6px; }

QWidget#rail { background-color: #1a1d23; border-right: 1px solid #2e333c; }
QLabel#wordmark { color: #ffd700; font-size: 17px; font-weight: 700; }
QLabel#version  { color: #6f7783; font-family: "%MONO%"; font-size: 11px; }

QLabel#eyebrow { color: #6f7783; font-size: 10px; font-weight: 700; }
QLabel#title   { color: #e8eaed; font-size: 21px; font-weight: 700; }
QLabel#subtitle{ color: #7d8592; }
QLabel#muted   { color: #7d8592; }
QLabel#faint   { color: #6f7783; font-size: 12px; }
QLabel#fainter { color: #5c6470; font-size: 11px; }
QLabel#mono    { color: #cfd4db; font-family: "%MONO%"; font-size: 12px; }

QWidget#card       { background-color: #1c1f25; border: 1px solid #2e333c; border-radius: 6px; }
QWidget#cardGold   { background-color: #1c1f25; border: 1px solid #4a3f1e; border-radius: 6px; }
QWidget#cardLocked { background-color: #191b20; border: 1px solid #24282f; border-radius: 6px; }
QWidget#cardGood   { background-color: #16211a; border: 1px solid #2c4634; border-radius: 6px; }
QWidget#cardBad    { background-color: #21181a; border: 1px solid #4a2f2c; border-radius: 6px; }

QLineEdit { background-color: #1c1f25; border: 1px solid #2e333c; border-radius: 5px;
            padding: 8px 10px; color: #cfd4db; font-family: "%MONO%"; font-size: 12px; }
QLineEdit:disabled { background-color: #191b20; color: #6f7783; border-color: #24282f; }
QLineEdit:focus { border-color: #4a5261; }

QPushButton { background-color: #23272f; border: 1px solid #3a404a; border-radius: 5px;
              padding: 9px 16px; color: #cfd4db; font-size: 13px; }
QPushButton:hover { border-color: #4a5261; color: #e8eaed; }
QPushButton:disabled { background-color: #1b1e24; color: #4d545e; border-color: #2a2e36; }
QPushButton#primary { background-color: #ffd700; border: none; color: #14161a;
                      font-weight: 700; font-size: 14px; padding: 11px 26px; }
QPushButton#primary:hover { background-color: #ffdf33; }
QPushButton#primary:disabled { background-color: #2c3038; color: #6f7783; }
QPushButton#ghost { background-color: transparent; color: #9aa3af; }
QPushButton#ghost:hover { color: #e8eaed; border-color: #4a5261; }
QPushButton#link { background-color: transparent; border: none; color: #5c6470;
                   font-size: 11px; padding: 2px 0; text-align: left; }
QPushButton#link:hover { color: #cfd4db; }
QPushButton#creditLink { background-color: transparent; border: none; color: #6f7783;
                         font-family: "%MONO%"; font-size: 10px; padding: 0;
                         text-align: left; }
QPushButton#creditLink:hover { color: #ffd700; }

QCheckBox { spacing: 10px; color: #e8eaed; font-size: 14px; font-weight: 600; }
QCheckBox::indicator { width: 34px; height: 20px; border-radius: 10px;
                       background-color: #363b45; border: none; }
QCheckBox::indicator:checked { background-color: #4ea86b; }
QCheckBox::indicator:disabled { background-color: #2a2e36; }
QCheckBox::indicator:checked:disabled { background-color: #2f4438; }
QCheckBox:disabled { color: #9aa3af; }

QComboBox { background-color: #23272f; border: 1px solid #3a404a; border-radius: 5px;
            padding: 6px 12px; color: #e8eaed; font-size: 12px; }
QComboBox:disabled { background-color: #1b1e24; color: #6f7783; border-color: #2a2e36; }
QComboBox QAbstractItemView { background-color: #1c1f25; border: 1px solid #3a404a;
                              selection-background-color: #2f353f; color: #e8eaed; }
QSpinBox { background-color: #1c1f25; border: 1px solid #2e333c; border-radius: 5px;
           padding: 8px 10px; color: #e8eaed; font-family: "%MONO%"; font-size: 15px; }
QSpinBox:disabled { background-color: #191b20; color: #8b929d; border-color: #2a2e36; }

QProgressBar { background-color: #22262d; border: none; border-radius: 3px;
               min-height: 5px; max-height: 5px; text-align: center; color: transparent; }
QProgressBar::chunk { background-color: #ffd700; border-radius: 3px; }

QTextEdit#log { background-color: #101216; border: 1px solid #22262d; border-radius: 6px;
                color: #7d8592; font-family: "%MONO%"; font-size: 11px; padding: 8px; }

QFrame#sep { background-color: #2e333c; max-height: 1px; border: none; }
QFrame#sepDim { background-color: #23272f; max-height: 1px; border: none; }
QWidget#footer { background-color: transparent; border-top: 1px solid #2e333c; }
QWidget#railRowActive { background-color: #23272f; border-left: 3px solid #ffd700; }
QWidget#railRow { background-color: transparent; border-left: 3px solid transparent; }
)QSS";

QString buildStyleSheet()
{
    return QString::fromUtf8(kStyleSheet)
        .replace("%UI%", QString::fromLatin1(kUiFont))
        .replace("%MONO%", QString::fromLatin1(kMonoFont));
}

QLabel* mkLabel(const QString& text, const char* role, QWidget* parent = nullptr)
{
    QLabel* l = new QLabel(text, parent);
    if (role) l->setObjectName(QString::fromLatin1(role));
    if (role && qstrcmp(role, "eyebrow") == 0) {
        QFont f = l->font();
        f.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
        l->setFont(f);
    }
    l->setWordWrap(true);
    return l;
}

QFrame* mkSep(bool dim = false)
{
    QFrame* f = new QFrame();
    f->setObjectName(dim ? "sepDim" : "sep");
    f->setFrameShape(QFrame::HLine);
    f->setFixedHeight(1);
    return f;
}

// A bullet is a fixed 22px disc. Its look is entirely stylesheet, so the four
// states below are the only place the wizard's progress vocabulary is defined.
void styleBullet(QLabel* dot, int state, int number)
{
    // 0 pending, 1 current, 2 done
    dot->setFixedSize(22, 22);
    dot->setAlignment(Qt::AlignCenter);
    switch (state) {
    case 2:
        // U+2713. Segoe UI carries it, and a glyph beats shipping an icon file
        // for one mark.
        dot->setText(QString::fromUtf8("✓"));
        dot->setStyleSheet("background: #4ea86b; border-radius: 11px; border: none;"
                           "color: #14161a; font-size: 12px; font-weight: 700;");
        break;
    case 1:
        dot->setText(QString::number(number));
        dot->setStyleSheet("background: transparent; border: 2px solid #ffd700;"
                           "border-radius: 11px; color: #ffd700;"
                           "font-size: 11px; font-weight: 700;");
        break;
    default:
        dot->setText(QString::number(number));
        dot->setStyleSheet("background: transparent; border: 2px solid #3a404a;"
                           "border-radius: 11px; color: #6f7783;"
                           "font-size: 11px; font-weight: 700;");
        break;
    }
}

void styleStageDot(QLabel* dot, int state)
{
    // 0 pending, 1 running, 2 done, 3 skipped
    dot->setFixedSize(14, 14);
    switch (state) {
    case 2:  dot->setStyleSheet("background: #4ea86b; border-radius: 7px; border: none;"); break;
    case 1:  dot->setStyleSheet("background: transparent; border: 2px solid #ffd700; border-radius: 7px;"); break;
    case 3:  dot->setStyleSheet("background: transparent; border: 2px dashed #2e333c; border-radius: 7px;"); break;
    default: dot->setStyleSheet("background: transparent; border: 2px solid #2e333c; border-radius: 7px;"); break;
    }
}

// One chip of seed metadata. Gold means "this changes the shape of the run".
QString chipHtml(const QString& text, bool accent = false)
{
    const QString bg     = accent ? "#2a2416" : "#23272f";
    const QString border = accent ? "#5c4a1c" : "#3a404a";
    const QString fg     = accent ? "#e8b923" : "#cfd4db";
    return QString("<span style=\"background:%1; border:1px solid %2; color:%3;"
                   "padding:3px 9px; border-radius:3px;\">&nbsp;%4&nbsp;</span>")
        .arg(bg, border, fg, text.toHtmlEscaped());
}

} // namespace

// ---------------------------------------------------------------------------

SimpleMainWindow::SimpleMainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_archipelagoModeEnabled(false)
{
    setupUI();
    loadConfig();

    appendConsoleMessage("Gold Saucer ready.");
    refreshInstallDetection();
    goToStep(StepGame);

    const QByteArray shot = qgetenv("GS_UI_SHOT");
    if (!shot.isEmpty()) {
        const int step = qgetenv("GS_UI_SHOT_STEP").toInt();
        QTimer::singleShot(1400, this, [this, shot, step]() {
            if (step > 0) {
                m_currentStep = StepGame;
                goToStep(step);
            }
            QApplication::processEvents();
            grab().save(QString::fromLocal8Bit(shot));
            qApp->quit();
        });
    }
}

void SimpleMainWindow::setupUI()
{
    setWindowTitle("Gold Saucer - FF7 Randomizer");
    setMinimumSize(860, 660);
    resize(900, 700);
    QFont appFont(QString::fromLatin1(kUiFont), 10);
    QApplication::setFont(appFont);
    setStyleSheet(buildStyleSheet());

    // --- widgets the rest of the class expects to exist -----------------------
    // Four of these are never placed in a layout. They are still real widgets so
    // updateConfig(), applyConfigToUI() and setOptionsLocked() keep working
    // untouched; the wizard just presents their state differently.
    //   m_archipelagoCheckBox - an indicator, now the rail's step-2 subtitle
    //   m_freeRoamCheckBox    - dictated by the seed, now a chip on step 2
    //   m_archipelagoJsonEdit - now the seed card
    //   m_keyItemCheckBox     - hidden by request: it does nothing for an AP run
    //                           and can strand a vanilla one, so it is forced off
    m_archipelagoCheckBox = new QCheckBox(this);
    m_archipelagoCheckBox->setVisible(false);
    m_freeRoamCheckBox = new QCheckBox(this);
    m_freeRoamCheckBox->setVisible(false);
    m_archipelagoJsonEdit = new QLineEdit(this);
    m_archipelagoJsonEdit->setVisible(false);
    m_keyItemCheckBox = new QCheckBox(this);
    m_keyItemCheckBox->setVisible(false);
    m_keyItemCheckBox->setChecked(false);

    QWidget* central = new QWidget(this);
    setCentralWidget(central);
    QHBoxLayout* shell = new QHBoxLayout(central);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);

    m_rail = buildRail();
    shell->addWidget(m_rail);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildStepGame());     // StepGame
    m_pages->addWidget(buildStepSeed());     // StepSeed
    m_pages->addWidget(buildStepOptions());  // StepOptions
    m_pages->addWidget(buildStepOutput());   // StepOutput
    m_pages->addWidget(buildStepRun());      // StepRun
    m_pages->addWidget(buildStepDone());     // StepDone
    shell->addWidget(m_pages, 1);

    // Qt does not paint background or border from a stylesheet on a PLAIN QWidget
    // unless WA_StyledBackground is set. Every panel here is a plain QWidget with
    // an object name the sheet targets, so without this the rail, the footers and
    // every card were drawn as bare window background — which is exactly what the
    // first build looked like. Applied by name so the runtime swaps
    // (cardGood <-> cardBad, railRow <-> railRowActive) keep working.
    static const QSet<QString> kStyledPanels = {
        "rail", "page", "footer", "railRow", "railRowActive",
        "card", "cardGold", "cardLocked", "cardGood", "cardBad"
    };
    const QList<QWidget*> panels = findChildren<QWidget*>();
    for (QWidget* w : panels) {
        if (kStyledPanels.contains(w->objectName()))
            w->setAttribute(Qt::WA_StyledBackground, true);
    }
}

QWidget* SimpleMainWindow::buildRail()
{
    QWidget* rail = new QWidget(this);
    rail->setObjectName("rail");
    rail->setFixedWidth(236);

    QVBoxLayout* v = new QVBoxLayout(rail);
    v->setContentsMargins(0, 20, 0, 18);
    v->setSpacing(0);

    QWidget* brand = new QWidget(rail);
    QHBoxLayout* bl = new QHBoxLayout(brand);
    bl->setContentsMargins(20, 0, 20, 20);
    bl->setSpacing(8);
    bl->addWidget(mkLabel("Gold Saucer", "wordmark"));
    QLabel* ver = mkLabel("v0.0.6", "version");
    bl->addWidget(ver, 0, Qt::AlignBottom);
    bl->addStretch(1);
    v->addWidget(brand);

    static const char* kNames[StepCount - 1] = {
        "Find your game", "Load your seed", "Choose options", "Where it goes", "Randomize"
    };

    for (int i = 0; i < StepCount - 1; ++i) {
        QWidget* row = new QWidget(rail);
        row->setObjectName("railRow");
        QHBoxLayout* rl = new QHBoxLayout(row);
        rl->setContentsMargins(17, 10, 20, 10);
        rl->setSpacing(11);

        QLabel* dot = new QLabel(row);
        styleBullet(dot, 0, i + 1);
        rl->addWidget(dot, 0, Qt::AlignTop);

        QWidget* textCol = new QWidget(row);
        QVBoxLayout* tl = new QVBoxLayout(textCol);
        tl->setContentsMargins(0, 0, 0, 0);
        tl->setSpacing(0);
        QLabel* title = mkLabel(QString::fromLatin1(kNames[i]), nullptr, textCol);
        QLabel* sub   = mkLabel(QString(), "fainter", textCol);
        sub->setVisible(false);
        tl->addWidget(title);
        tl->addWidget(sub);
        rl->addWidget(textCol, 1);

        // Clicking a rail row jumps to it. That is what makes a five-step wizard
        // bearable on a re-run: change one toggle and go, rather than Continue
        // four times. Guarded in goToStep() so it can never skip a step whose
        // prerequisite is missing.
        row->setCursor(Qt::PointingHandCursor);
        row->installEventFilter(this);
        row->setProperty("stepIndex", i);

        m_railRow[i]    = row;
        m_railBullet[i] = dot;
        m_railTitle[i]  = title;
        m_railSub[i]    = sub;
        v->addWidget(row);
    }

    v->addStretch(1);

    QWidget* links = new QWidget(rail);
    QVBoxLayout* ll = new QVBoxLayout(links);
    ll->setContentsMargins(20, 0, 20, 0);
    ll->setSpacing(4);
    m_loadConfigButton = new QPushButton("Load settings…", links);
    m_loadConfigButton->setObjectName("link");
    m_loadConfigButton->setCursor(Qt::PointingHandCursor);
    QPushButton* saveBtn = new QPushButton("Save settings", links);
    saveBtn->setObjectName("link");
    saveBtn->setCursor(Qt::PointingHandCursor);
    QPushButton* resetBtn = new QPushButton("Reset to defaults", links);
    resetBtn->setObjectName("link");
    resetBtn->setCursor(Qt::PointingHandCursor);
    ll->addWidget(m_loadConfigButton);
    ll->addWidget(saveBtn);
    ll->addWidget(resetBtn);
    v->addWidget(links);

    connect(m_loadConfigButton, &QPushButton::clicked, this, &SimpleMainWindow::loadConfig);
    connect(saveBtn,  &QPushButton::clicked, this, &SimpleMainWindow::saveConfig);
    connect(resetBtn, &QPushButton::clicked, this, &SimpleMainWindow::resetToDefaults);

    // Credit, at the foot of the rail on every step.
    QWidget* credit = new QWidget(rail);
    QVBoxLayout* cl = new QVBoxLayout(credit);
    cl->setContentsMargins(20, 12, 20, 0);
    cl->setSpacing(2);
    QFrame* sep = mkSep(true);
    QLabel* by = mkLabel("Developed by Blazerwazey", "fainter", credit);
    QPushButton* repo = new QPushButton("github.com/blazerwazey/Gold-Saucer", credit);
    repo->setObjectName("creditLink");
    repo->setCursor(Qt::PointingHandCursor);
    repo->setToolTip("https://github.com/blazerwazey/Gold-Saucer");
    connect(repo, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/blazerwazey/Gold-Saucer"));
    });
    cl->addWidget(sep);
    cl->addSpacing(8);
    cl->addWidget(by);
    cl->addWidget(repo);
    v->addWidget(credit);

    return rail;
}

// A step page: eyebrow / title / subtitle, a body, then a footer of buttons.
static QWidget* makePage(QWidget* parent, QLabel** eyebrowOut, QLabel** titleOut,
                         QLabel** subtitleOut, QVBoxLayout** bodyOut, QHBoxLayout** footerOut)
{
    QWidget* page = new QWidget(parent);
    page->setObjectName("page");
    QVBoxLayout* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    QWidget* head = new QWidget(page);
    QVBoxLayout* hl = new QVBoxLayout(head);
    hl->setContentsMargins(30, 26, 30, 0);
    hl->setSpacing(5);
    *eyebrowOut  = mkLabel(QString(), "eyebrow", head);
    *titleOut    = mkLabel(QString(), "title", head);
    *subtitleOut = mkLabel(QString(), "subtitle", head);
    (*subtitleOut)->setMaximumWidth(560);
    hl->addWidget(*eyebrowOut);
    hl->addWidget(*titleOut);
    hl->addWidget(*subtitleOut);
    v->addWidget(head);

    QWidget* body = new QWidget(page);
    *bodyOut = new QVBoxLayout(body);
    (*bodyOut)->setContentsMargins(30, 22, 30, 22);
    (*bodyOut)->setSpacing(10);
    v->addWidget(body, 1);

    QWidget* footer = new QWidget(page);
    footer->setObjectName("footer");
    footer->setFixedHeight(68);
    *footerOut = new QHBoxLayout(footer);
    (*footerOut)->setContentsMargins(30, 0, 30, 0);
    (*footerOut)->setSpacing(12);
    v->addWidget(footer);

    return page;
}

QWidget* SimpleMainWindow::buildStepGame()
{
    QLabel *eb, *ti, *sub; QVBoxLayout* body; QHBoxLayout* footer;
    QWidget* page = makePage(this, &eb, &ti, &sub, &body, &footer);
    eb->setText("STEP 1 OF 5");
    ti->setText("Where is Final Fantasy VII installed?");
    sub->setText("Gold Saucer reads your own game files and never ships any. Pick the install "
                 "root — the 2026 re-release's nested folders are found automatically.");

    QWidget* row = new QWidget(page);
    QHBoxLayout* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(9);
    m_ff7PathEdit = new QLineEdit(row);
    m_ff7PathEdit->setPlaceholderText("Select your Final Fantasy VII folder…");
    QPushButton* browse = new QPushButton("Browse…", row);
    rl->addWidget(m_ff7PathEdit, 1);
    rl->addWidget(browse);
    body->addWidget(row);

    m_detectCard = new QWidget(page);
    m_detectCard->setObjectName("cardGood");
    QVBoxLayout* dl = new QVBoxLayout(m_detectCard);
    dl->setContentsMargins(17, 15, 17, 15);
    dl->setSpacing(9);
    m_detectHeading = mkLabel(QString(), nullptr, m_detectCard);
    m_detectHeading->setStyleSheet("font-size: 14px; font-weight: 600;");
    dl->addWidget(m_detectHeading);

    QGridLayout* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(22);
    grid->setVerticalSpacing(7);
    auto addFact = [&](int r, int c, const QString& name, QLabel** out) {
        grid->addWidget(mkLabel(name, "faint", m_detectCard), r, c * 2);
        *out = mkLabel("—", "mono", m_detectCard);
        (*out)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(*out, r, c * 2 + 1);
    };
    addFact(0, 0, "Release",    &m_detectRelease);
    addFact(0, 1, "Executable", &m_detectExe);
    addFact(1, 0, "Field data", &m_detectFlevel);
    addFact(1, 1, "Kernel",     &m_detectKernel);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    dl->addLayout(grid);
    body->addWidget(m_detectCard);
    body->addStretch(1);

    footer->addStretch(1);
    m_gameContinue = new QPushButton("Continue", page);
    m_gameContinue->setObjectName("primary");
    footer->addWidget(m_gameContinue);

    connect(browse, &QPushButton::clicked, this, &SimpleMainWindow::browseFF7Path);
    connect(m_ff7PathEdit, &QLineEdit::textChanged, this,
            [this](const QString&) { refreshInstallDetection(); });
    connect(m_gameContinue, &QPushButton::clicked, this,
            [this]() { goToStep(StepSeed); });
    return page;
}

QWidget* SimpleMainWindow::buildStepSeed()
{
    QLabel *eb, *ti, *sub; QVBoxLayout* body; QHBoxLayout* footer;
    QWidget* page = makePage(this, &eb, &ti, &sub, &body, &footer);
    eb->setText("STEP 2 OF 5");
    ti->setText("Load your Archipelago seed");
    sub->setText("The .apff7 file from your generated multiworld. It decides where every "
                 "check goes.");

    m_seedCard = new QWidget(page);
    m_seedCard->setObjectName("cardGold");
    QHBoxLayout* sl = new QHBoxLayout(m_seedCard);
    sl->setContentsMargins(19, 17, 19, 17);
    sl->setSpacing(16);
    QWidget* col = new QWidget(m_seedCard);
    QVBoxLayout* cl = new QVBoxLayout(col);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(3);
    m_seedFileLabel = mkLabel(QString(), nullptr, col);
    m_seedFileLabel->setStyleSheet(QString("font-family: \"%1\"; font-size: 14px; font-weight: 700;")
                                       .arg(QString::fromLatin1(kMonoFont)));
    m_seedPlayerLabel = mkLabel(QString(), "faint", col);
    cl->addWidget(m_seedFileLabel);
    cl->addWidget(m_seedPlayerLabel);
    sl->addWidget(col, 1);
    m_seedReplaceButton = new QPushButton("Replace…", m_seedCard);
    sl->addWidget(m_seedReplaceButton, 0, Qt::AlignVCenter);
    body->addWidget(m_seedCard);

    body->addSpacing(4);
    body->addWidget(mkLabel("WHAT THIS SEED ASKS FOR", "eyebrow", page));
    m_seedChips = mkLabel(QString(), nullptr, page);
    m_seedChips->setTextFormat(Qt::RichText);
    m_seedChips->setWordWrap(true);
    body->addWidget(m_seedChips);
    body->addWidget(mkLabel("Read straight out of the seed — worth a glance against the "
                            "YAML you submitted.", "faint", page));

    m_seedEmptyHint = mkLabel("No seed loaded. Import one to play a multiworld, or carry on "
                              "to randomize on your own.", "subtitle", page);
    body->addWidget(m_seedEmptyHint);

    body->addStretch(1);
    body->addWidget(mkSep());
    QWidget* alt = new QWidget(page);
    QHBoxLayout* al = new QHBoxLayout(alt);
    al->setContentsMargins(0, 6, 0, 0);
    al->setSpacing(12);
    al->addWidget(mkLabel("Not playing a multiworld?", "muted", alt));
    m_seedSkipButton = new QPushButton("Randomize without a seed", alt);
    al->addWidget(m_seedSkipButton);
    al->addStretch(1);
    body->addWidget(alt);

    QPushButton* back = new QPushButton("Back", page);
    back->setObjectName("ghost");
    m_importArchipelagoButton = new QPushButton("Import seed…", page);
    QPushButton* cont = new QPushButton("Continue", page);
    cont->setObjectName("primary");
    footer->addWidget(back);
    footer->addStretch(1);
    footer->addWidget(m_importArchipelagoButton);
    footer->addWidget(cont);

    connect(back, &QPushButton::clicked, this, [this]() { goToStep(StepGame); });
    connect(cont, &QPushButton::clicked, this, [this]() { goToStep(StepOptions); });
    connect(m_importArchipelagoButton, &QPushButton::clicked,
            this, &SimpleMainWindow::importArchipelagoJSON);
    connect(m_seedReplaceButton, &QPushButton::clicked,
            this, &SimpleMainWindow::importArchipelagoJSON);
    connect(m_seedSkipButton, &QPushButton::clicked, this, [this]() {
        clearArchipelagoSeed();
        applyConfigToUI();
        appendConsoleMessage("Seed cleared - randomizing without Archipelago.");
        goToStep(StepOptions);
    });
    return page;
}

QWidget* SimpleMainWindow::buildStepOptions()
{
    QLabel *eb, *ti, *sub; QVBoxLayout* body; QHBoxLayout* footer;
    QWidget* page = makePage(this, &eb, &ti, &sub, &body, &footer);
    eb->setText("STEP 3 OF 5");
    ti->setText("What should Gold Saucer randomize?");
    sub->setText("Most of this is set by your seed and has to stay that way. Shop stock is "
                 "yours — it only changes what sits around your checks.");

    // --- the one control the multiworld has no stake in ----------------------
    QWidget* shopCard = new QWidget(page);
    shopCard->setObjectName("cardGold");
    QHBoxLayout* shl = new QHBoxLayout(shopCard);
    shl->setContentsMargins(16, 14, 16, 14);
    shl->setSpacing(12);
    m_shopCheckBox = new QCheckBox(shopCard);
    shl->addWidget(m_shopCheckBox, 0, Qt::AlignTop);
    QWidget* shc = new QWidget(shopCard);
    QVBoxLayout* shcl = new QVBoxLayout(shc);
    shcl->setContentsMargins(0, 0, 0, 0);
    shcl->setSpacing(3);
    QLabel* shopTitle = mkLabel("Shop stock", nullptr, shc);
    shopTitle->setStyleSheet("font-size: 14px; font-weight: 600;");
    shcl->addWidget(shopTitle);
    shcl->addWidget(mkLabel("Reshuffle what each shop sells. Turn it off to leave stores "
                            "vanilla — your seed's shop checks are placed either way, if "
                            "randomize_shops is on in your Archipelago YAML.", "muted", shc));
    m_shopYamlNote = mkLabel("That YAML option is off by default. With it off your seed carries "
                             "no shop checks at all, whatever this is set to.", "faint", shc);
    shcl->addWidget(m_shopYamlNote);
    shl->addWidget(shc, 1);
    body->addWidget(shopCard);

    // --- everything the seed dictates ----------------------------------------
    m_lockedSectionHeader = new QWidget(page);
    QHBoxLayout* lhl = new QHBoxLayout(m_lockedSectionHeader);
    lhl->setContentsMargins(2, 8, 2, 0);
    lhl->setSpacing(9);
    lhl->addWidget(mkLabel("SET BY YOUR SEED", "eyebrow", m_lockedSectionHeader));
    lhl->addWidget(mkLabel("these change what the multiworld expects, so they follow your YAML",
                           "fainter", m_lockedSectionHeader), 1);
    body->addWidget(m_lockedSectionHeader);

    QWidget* fieldCard = new QWidget(page);
    fieldCard->setObjectName("card");
    QHBoxLayout* fl = new QHBoxLayout(fieldCard);
    fl->setContentsMargins(16, 14, 16, 14);
    fl->setSpacing(12);
    m_fieldCheckBox = new QCheckBox(fieldCard);
    fl->addWidget(m_fieldCheckBox, 0, Qt::AlignTop);
    QWidget* fc = new QWidget(fieldCard);
    QVBoxLayout* fcl = new QVBoxLayout(fc);
    fcl->setContentsMargins(0, 0, 0, 0);
    fcl->setSpacing(3);
    QLabel* fieldTitle = mkLabel("Field pickups", nullptr, fc);
    fieldTitle->setStyleSheet("font-size: 14px; font-weight: 600;");
    fcl->addWidget(fieldTitle);
    fcl->addWidget(mkLabel("Chests and field rewards. This is where most of your checks live.",
                           "muted", fc));
    fl->addWidget(fc, 1);
    body->addWidget(fieldCard);

    QWidget* eqCard = new QWidget(page);
    eqCard->setObjectName("card");
    QHBoxLayout* el = new QHBoxLayout(eqCard);
    el->setContentsMargins(16, 14, 16, 14);
    el->setSpacing(12);
    m_equipmentCheckBox = new QCheckBox(eqCard);
    el->addWidget(m_equipmentCheckBox, 0, Qt::AlignTop);
    QWidget* ec = new QWidget(eqCard);
    QVBoxLayout* ecl = new QVBoxLayout(ec);
    ecl->setContentsMargins(0, 0, 0, 0);
    ecl->setSpacing(7);
    QLabel* eqTitle = mkLabel("Starting equipment", nullptr, ec);
    eqTitle->setStyleSheet("font-size: 14px; font-weight: 600;");
    ecl->addWidget(eqTitle);
    ecl->addWidget(mkLabel("What everyone is carrying when the run begins.", "muted", ec));
    QWidget* tierRow = new QWidget(ec);
    QHBoxLayout* trl = new QHBoxLayout(tierRow);
    trl->setContentsMargins(0, 0, 0, 0);
    trl->setSpacing(9);
    trl->addWidget(mkLabel("Tier", "faint", tierRow));
    m_equipmentCombo = new QComboBox(tierRow);
    // Five, matching the Archipelago option's 1-5 range. Index 2 is the YAML
    // default (3). Config stores this 0-based; the importer converts.
    m_equipmentCombo->addItems({"1 - Weakest", "2 - Weak", "3 - Balanced",
                                "4 - Strong", "5 - Strongest"});
    m_equipmentCombo->setCurrentIndex(2);
    trl->addWidget(m_equipmentCombo);
    trl->addStretch(1);
    ecl->addWidget(tierRow);
    el->addWidget(ec, 1);
    body->addWidget(eqCard);

    body->addStretch(1);
    m_lockedFootnote = mkLabel("Locked settings come from the .apff7 and have to match the "
                               "multiworld. Randomizing without a seed makes all of them yours.",
                               "faint", page);
    body->addWidget(m_lockedFootnote);

    QPushButton* back = new QPushButton("Back", page);
    back->setObjectName("ghost");
    QPushButton* cont = new QPushButton("Continue", page);
    cont->setObjectName("primary");
    footer->addWidget(back);
    footer->addStretch(1);
    footer->addWidget(cont);
    connect(back, &QPushButton::clicked, this, [this]() { goToStep(StepSeed); });
    connect(cont, &QPushButton::clicked, this, [this]() { goToStep(StepOutput); });
    return page;
}

QWidget* SimpleMainWindow::buildStepOutput()
{
    QLabel *eb, *ti, *sub; QVBoxLayout* body; QHBoxLayout* footer;
    QWidget* page = makePage(this, &eb, &ti, &sub, &body, &footer);
    eb->setText("STEP 4 OF 5");
    ti->setText("Where should the files go?");
    sub->setText("Your original install is never written to. Pick somewhere else for the "
                 "randomized copies.");

    QWidget* row = new QWidget(page);
    QHBoxLayout* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(9);
    m_outputFolderEdit = new QLineEdit(row);
    m_outputFolderEdit->setPlaceholderText("Choose a folder for the randomized files…");
    QPushButton* browse = new QPushButton("Browse…", row);
    rl->addWidget(m_outputFolderEdit, 1);
    rl->addWidget(browse);
    body->addWidget(row);

    QWidget* iroCard = new QWidget(page);
    iroCard->setObjectName("cardGold");
    QHBoxLayout* il = new QHBoxLayout(iroCard);
    il->setContentsMargins(16, 14, 16, 14);
    il->setSpacing(12);
    m_iroCheckBox = new QCheckBox(iroCard);
    il->addWidget(m_iroCheckBox, 0, Qt::AlignTop);
    QWidget* ic = new QWidget(iroCard);
    QVBoxLayout* icl = new QVBoxLayout(ic);
    icl->setContentsMargins(0, 0, 0, 0);
    icl->setSpacing(3);
    QLabel* iroTitle = mkLabel("Also pack a 7th Heaven .iro", nullptr, ic);
    iroTitle->setStyleSheet("font-size: 14px; font-weight: 600;");
    icl->addWidget(iroTitle);
    icl->addWidget(mkLabel("A single mod archive you import in 7th Heaven, alongside the "
                           "loose files.", "muted", ic));
    il->addWidget(ic, 1);
    body->addWidget(iroCard);

    body->addSpacing(6);
    body->addWidget(mkLabel("SEED NUMBER", "eyebrow", page));

    m_seedEditRow = new QWidget(page);
    QHBoxLayout* sel = new QHBoxLayout(m_seedEditRow);
    sel->setContentsMargins(0, 0, 0, 0);
    sel->setSpacing(10);
    m_seedSpin = new QSpinBox(m_seedEditRow);
    m_seedSpin->setRange(0, 999999);
    m_seedSpin->setValue(12345);
    m_seedSpin->setMinimumWidth(120);
    m_randomSeedButton = new QPushButton("Roll a new one", m_seedEditRow);
    sel->addWidget(m_seedSpin);
    sel->addWidget(m_randomSeedButton);
    sel->addWidget(mkLabel("The same number always produces the same result.", "faint",
                           m_seedEditRow), 1);
    body->addWidget(m_seedEditRow);

    m_seedLockedRow = new QWidget(page);
    QHBoxLayout* sll = new QHBoxLayout(m_seedLockedRow);
    sll->setContentsMargins(0, 0, 0, 0);
    sll->setSpacing(11);
    m_seedLockedLabel = mkLabel(QString(), nullptr, m_seedLockedRow);
    m_seedLockedLabel->setStyleSheet(
        QString("background: #191b20; border: 1px solid #2a2e36; border-radius: 5px;"
                "padding: 10px 14px; color: #8b929d; font-family: \"%1\"; font-size: 15px;")
            .arg(QString::fromLatin1(kMonoFont)));
    sll->addWidget(m_seedLockedLabel);
    sll->addWidget(mkLabel("Set by your seed file — it has to match the multiworld. "
                           "Randomizing without a seed lets you choose one.", "faint",
                           m_seedLockedRow), 1);
    body->addWidget(m_seedLockedRow);

    body->addStretch(1);

    QPushButton* back = new QPushButton("Back", page);
    back->setObjectName("ghost");
    QPushButton* go = new QPushButton("Randomize", page);
    go->setObjectName("primary");
    footer->addWidget(back);
    footer->addStretch(1);
    footer->addWidget(go);

    connect(browse, &QPushButton::clicked, this, &SimpleMainWindow::browseOutputFolder);
    connect(back, &QPushButton::clicked, this, [this]() { goToStep(StepOptions); });
    connect(go, &QPushButton::clicked, this, &SimpleMainWindow::startRandomization);
    connect(m_randomSeedButton, &QPushButton::clicked, this, &SimpleMainWindow::randomSeed);
    return page;
}

QWidget* SimpleMainWindow::buildStepRun()
{
    QLabel *eb, *ti, *sub; QVBoxLayout* body; QHBoxLayout* footer;
    QWidget* page = makePage(this, &eb, &ti, &sub, &body, &footer);
    eb->setText("STEP 5 OF 5");
    m_runHeadline = ti;
    m_runDetail   = sub;
    m_runHeadline->setText("Randomizing…");
    m_runDetail->setText("This usually takes a minute or two.");

    m_progressBar = new QProgressBar(page);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(5);
    body->addWidget(m_progressBar);

    static const char* kStages[StageCount] = {
        "Copy original files", "Shops + AP slots", "Field pickups",
        "Starting equipment", "Northern Crater barrier", "Pack .iro archive"
    };
    QWidget* stages = new QWidget(page);
    QVBoxLayout* stl = new QVBoxLayout(stages);
    stl->setContentsMargins(0, 6, 0, 0);
    stl->setSpacing(3);
    for (int i = 0; i < StageCount; ++i) {
        QWidget* row = new QWidget(stages);
        QHBoxLayout* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 3, 0, 3);
        rl->setSpacing(10);
        QLabel* dot = new QLabel(row);
        styleStageDot(dot, 0);
        QLabel* lab = mkLabel(QString::fromLatin1(kStages[i]), "faint", row);
        rl->addWidget(dot, 0, Qt::AlignVCenter);
        rl->addWidget(lab, 1);
        m_stageIcon[i]  = dot;
        m_stageLabel[i] = lab;
        m_stageRow[i]   = row;
        stl->addWidget(row);
    }
    body->addWidget(stages);

    QWidget* logHead = new QWidget(page);
    QHBoxLayout* lhl = new QHBoxLayout(logHead);
    lhl->setContentsMargins(0, 8, 0, 0);
    lhl->setSpacing(8);
    lhl->addWidget(mkLabel("RUN LOG", "eyebrow", logHead));
    lhl->addStretch(1);
    QPushButton* copyLog = new QPushButton("Copy", logHead);
    copyLog->setStyleSheet("padding: 3px 10px; font-size: 11px;");
    lhl->addWidget(copyLog);
    body->addWidget(logHead);

    m_consoleOutput = new QTextEdit(page);
    m_consoleOutput->setObjectName("log");
    m_consoleOutput->setReadOnly(true);
    body->addWidget(m_consoleOutput, 1);

    // The status label the old window carried. Nothing shows it any more — the
    // headline and the stage list say the same thing better — but every pass
    // still writes to it, so it stays alive and hidden rather than forcing edits
    // through startRandomization().
    m_statusLabel = new QLabel(page);
    m_statusLabel->setVisible(false);

    footer->addStretch(1);
    m_cancelButton = new QPushButton("Cancel", page);
    m_cancelButton->setObjectName("ghost");
    footer->addWidget(m_cancelButton);

    connect(copyLog, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(m_consoleOutput->toPlainText());
    });
    // Cancellation is checked BETWEEN passes, not inside them: a pass writes whole
    // files and interrupting one would leave a half-patched output folder. So the
    // current pass finishes, then the run stops.
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        m_cancelRequested = true;
        m_cancelButton->setEnabled(false);
        m_cancelButton->setText("Stopping…");
        appendConsoleMessage("Cancel requested - stopping after the current pass.");
    });
    return page;
}

QWidget* SimpleMainWindow::buildStepDone()
{
    QWidget* page = new QWidget(this);
    page->setObjectName("page");
    QVBoxLayout* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    QWidget* brand = new QWidget(page);
    QHBoxLayout* bl = new QHBoxLayout(brand);
    bl->setContentsMargins(30, 20, 30, 0);
    bl->setSpacing(8);
    bl->addWidget(mkLabel("Gold Saucer", "wordmark"));
    bl->addWidget(mkLabel("v0.0.6", "version"), 0, Qt::AlignBottom);
    bl->addStretch(1);
    v->addWidget(brand);

    QWidget* mid = new QWidget(page);
    QVBoxLayout* ml = new QVBoxLayout(mid);
    ml->setContentsMargins(60, 0, 60, 0);
    ml->setSpacing(22);
    ml->addStretch(1);

    QLabel* tick = new QLabel(QString::fromUtf8("✓"), mid);
    tick->setFixedSize(62, 62);
    tick->setAlignment(Qt::AlignCenter);
    tick->setStyleSheet("background: #16211a; border: 2px solid #2c4634; border-radius: 31px;"
                        "color: #4ea86b; font-size: 30px; font-weight: 700;");
    ml->addWidget(tick, 0, Qt::AlignHCenter);

    QLabel* head = mkLabel("Your randomized game is ready", nullptr, mid);
    head->setStyleSheet("font-size: 23px; font-weight: 700;");
    head->setAlignment(Qt::AlignCenter);
    ml->addWidget(head);

    m_doneSubtitle = mkLabel(QString(), "subtitle", mid);
    m_doneSubtitle->setAlignment(Qt::AlignCenter);
    ml->addWidget(m_doneSubtitle);

    QWidget* card = new QWidget(mid);
    card->setObjectName("card");
    card->setFixedWidth(620);
    QVBoxLayout* cl = new QVBoxLayout(card);
    cl->setContentsMargins(22, 16, 22, 16);
    cl->setSpacing(11);
    m_donePasses = mkLabel(QString(), "muted", card);
    cl->addWidget(m_donePasses);

    QWidget* folderRow = new QWidget(card);
    QHBoxLayout* frl = new QHBoxLayout(folderRow);
    frl->setContentsMargins(0, 0, 0, 0);
    frl->setSpacing(9);
    frl->addWidget(mkLabel("Folder", "faint", folderRow));
    m_doneFolderEdit = new QLineEdit(folderRow);
    m_doneFolderEdit->setReadOnly(true);
    QPushButton* openFolder = new QPushButton("Open", folderRow);
    frl->addWidget(m_doneFolderEdit, 1);
    frl->addWidget(openFolder);
    cl->addWidget(folderRow);

    m_doneIroRow = new QWidget(card);
    QHBoxLayout* irl = new QHBoxLayout(m_doneIroRow);
    irl->setContentsMargins(0, 0, 0, 0);
    irl->setSpacing(9);
    irl->addWidget(mkLabel("Archive", "faint", m_doneIroRow));
    m_doneIroEdit = new QLineEdit(m_doneIroRow);
    m_doneIroEdit->setReadOnly(true);
    m_doneIroEdit->setStyleSheet("border-color: #4a3f1e; color: #e8b923;");
    QPushButton* showIro = new QPushButton("Show", m_doneIroRow);
    irl->addWidget(m_doneIroEdit, 1);
    irl->addWidget(showIro);
    cl->addWidget(m_doneIroRow);

    ml->addWidget(card, 0, Qt::AlignHCenter);

    QWidget* hint = new QWidget(mid);
    hint->setObjectName("cardLocked");
    hint->setFixedWidth(620);
    QHBoxLayout* hl2 = new QHBoxLayout(hint);
    hl2->setContentsMargins(17, 13, 17, 13);
    hl2->addWidget(mkLabel("Import the archive in 7th Heaven, then start the Archipelago "
                           "client and connect.", "muted", hint));
    ml->addWidget(hint, 0, Qt::AlignHCenter);
    ml->addStretch(1);
    v->addWidget(mid, 1);

    QWidget* footer = new QWidget(page);
    footer->setObjectName("footer");
    footer->setFixedHeight(64);
    QHBoxLayout* fl = new QHBoxLayout(footer);
    fl->setContentsMargins(30, 0, 30, 0);
    QPushButton* again = new QPushButton("Randomize again", footer);
    fl->addStretch(1);
    fl->addWidget(again);
    v->addWidget(footer);

    QWidget* credit = new QWidget(page);
    QVBoxLayout* crl = new QVBoxLayout(credit);
    crl->setContentsMargins(30, 0, 30, 16);
    crl->setSpacing(2);
    QLabel* by = mkLabel("Developed by Blazerwazey", "fainter", credit);
    by->setAlignment(Qt::AlignCenter);
    QPushButton* repo = new QPushButton("github.com/blazerwazey/Gold-Saucer", credit);
    repo->setObjectName("creditLink");
    repo->setCursor(Qt::PointingHandCursor);
    repo->setStyleSheet("text-align: center;");
    connect(repo, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/blazerwazey/Gold-Saucer"));
    });
    crl->addWidget(by);
    crl->addWidget(repo);
    v->addWidget(credit);

    connect(openFolder, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_doneFolderEdit->text()));
    });
    connect(showIro, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_lastIroPath).absolutePath()));
    });
    connect(again, &QPushButton::clicked, this, [this]() { goToStep(StepOptions); });
    return page;
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

bool SimpleMainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        QVariant idx = watched->property("stepIndex");
        if (idx.isValid()) {
            goToStep(idx.toInt());
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void SimpleMainWindow::goToStep(int step)
{
    if (step < 0 || step >= StepCount)
        return;
    // A run in flight owns the window; and step 1 has to produce a readable
    // install before anything past it means anything.
    if (m_currentStep == StepRun && step != StepDone)
        return;
    if (step > StepGame && !refreshInstallDetection()) {
        step = StepGame;
    }

    m_currentStep = step;
    m_pages->setCurrentIndex(step);
    m_rail->setVisible(step != StepDone);

    if (step == StepSeed)    refreshSeedSummary();
    if (step == StepOptions) refreshOptionsLockUi();
    if (step == StepOutput)  refreshOutputStep();
    refreshRail();
}

void SimpleMainWindow::refreshRail()
{
    const int active = qMin(m_currentStep, int(StepRun));
    for (int i = 0; i < StepCount - 1; ++i) {
        const int state = (i < active) ? 2 : (i == active ? 1 : 0);
        styleBullet(m_railBullet[i], state, i + 1);
        m_railRow[i]->setObjectName(state == 1 ? "railRowActive" : "railRow");
        m_railRow[i]->style()->unpolish(m_railRow[i]);
        m_railRow[i]->style()->polish(m_railRow[i]);
        m_railTitle[i]->setStyleSheet(state == 1
            ? "color: #ffffff; font-weight: 700;"
            : (state == 2 ? "color: #cfd4db; font-weight: 600;" : "color: #6f7783;"));
    }

    // Each completed step reports what it settled, so the rail doubles as a
    // summary on a re-run.
    auto setSub = [](QLabel* l, const QString& text) {
        l->setText(text);
        l->setVisible(!text.isEmpty());
    };
    setSub(m_railSub[StepGame], m_detectRelease ? m_detectRelease->text() : QString());
    setSub(m_railSub[StepSeed], m_archipelagoJsonPath.isEmpty()
               ? QString("No seed")
               : QFileInfo(m_archipelagoJsonPath).completeBaseName());
    int on = (m_shopCheckBox && m_shopCheckBox->isChecked() ? 1 : 0)
           + (m_fieldCheckBox && m_fieldCheckBox->isChecked() ? 1 : 0)
           + (m_equipmentCheckBox && m_equipmentCheckBox->isChecked() ? 1 : 0);
    setSub(m_railSub[StepOptions], QString("%1 of 3 on").arg(on));
    setSub(m_railSub[StepOutput], m_outputFolderEdit && !m_outputFolderEdit->text().isEmpty()
               ? QFileInfo(m_outputFolderEdit->text()).fileName()
               : QString());
    setSub(m_railSub[StepRun], QString());
}

// Reads the install path and repaints step 1. Returns whether it looks usable —
// the old window accepted any path and only failed several passes later.
bool SimpleMainWindow::refreshInstallDetection()
{
    if (!m_ff7PathEdit || !m_detectCard)
        return false;

    const QString path = m_ff7PathEdit->text();
    auto fail = [&](const QString& why) {
        m_detectCard->setObjectName("cardBad");
        m_detectCard->style()->unpolish(m_detectCard);
        m_detectCard->style()->polish(m_detectCard);
        m_detectHeading->setText(why);
        m_detectHeading->setStyleSheet("font-size: 14px; font-weight: 600; color: #e08b82;");
        m_detectRelease->setText("—");
        m_detectExe->setText("—");
        m_detectFlevel->setText("—");
        m_detectKernel->setText("—");
        if (m_gameContinue) m_gameContinue->setEnabled(false);
        return false;
    };

    if (path.isEmpty())
        return fail("Choose your Final Fantasy VII folder");
    QDir root(path);
    if (!root.exists())
        return fail("That folder does not exist");

    // Same two layouts startRandomization() accepts: classic (data/ at the root)
    // and the 2026 re-release (nested under ff7/workingdir/).
    QString dataRoot = path;
    QString release  = "2013 Steam / classic";
    if (!root.exists("data") && root.exists("ff7/workingdir/data")) {
        dataRoot = path + "/ff7/workingdir";
        release  = "2026 re-release";
    } else if (!root.exists("data")) {
        return fail("No data folder here — is this the install root?");
    }

    QString exeName;
    for (const char* candidate : {"ff7_en.exe", "ff7.exe", "FF7_Launcher.exe"}) {
        if (QFileInfo::exists(QDir(dataRoot).filePath(candidate))
            || QFileInfo::exists(QDir(path).filePath(candidate))) {
            exeName = QString::fromLatin1(candidate);
            break;
        }
    }
    const bool flevel = QFileInfo::exists(QDir(dataRoot).filePath("data/field/flevel.lgp"));

    // kernel.bin is NOT at data/kernel/ on a Steam install — the language packs
    // put it under data/lang-en/. IroExporter already stages both spellings from
    // there; this list is the same set, so detection agrees with what the passes
    // will actually read.
    static const char* kKernelCandidates[] = {
        "data/lang-en/kernel/KERNEL.BIN",
        "data/lang-en/kernel/kernel.bin",
        "data/kernel/KERNEL.BIN",
        "data/kernel/kernel.bin",
    };
    QString kernelFound;
    for (const char* candidate : kKernelCandidates) {
        if (QFileInfo::exists(QDir(dataRoot).filePath(QString::fromLatin1(candidate)))) {
            kernelFound = QString::fromLatin1(candidate);
            break;
        }
    }

    if (!flevel || kernelFound.isEmpty())
        return fail(flevel ? "Found the game, but kernel.bin is missing"
                           : "Found the game, but flevel.lgp is missing");

    m_detectCard->setObjectName("cardGood");
    m_detectCard->style()->unpolish(m_detectCard);
    m_detectCard->style()->polish(m_detectCard);
    m_detectHeading->setText("Found a working install");
    m_detectHeading->setStyleSheet("font-size: 14px; font-weight: 600; color: #8fd3a6;");
    m_detectRelease->setText(release);
    m_detectExe->setText(exeName.isEmpty() ? "not found" : exeName);
    m_detectFlevel->setText("flevel.lgp");
    m_detectKernel->setText(QFileInfo(kernelFound).fileName());
    if (m_gameContinue) m_gameContinue->setEnabled(true);
    return true;
}

// Repaints step 2 from the held .apff7. Every chip is read out of the file, so
// what is shown is what Gold Saucer will actually act on.
void SimpleMainWindow::refreshSeedSummary()
{
    const bool has = !m_archipelagoJsonPath.isEmpty();
    m_seedCard->setVisible(has);
    m_seedChips->setVisible(has);
    m_seedEmptyHint->setVisible(!has);
    if (!has) {
        m_seedChips->clear();
        return;
    }

    m_seedFileLabel->setText(QFileInfo(m_archipelagoJsonPath).fileName());

    // "randomize_field_items" + "true"  ->  "Field items"
    // "field_items_mode"       + "shuffle" -> "Field items mode: shuffle"
    // "exp_multiplier"         + "2"     ->  "Exp multiplier: 2"
    // A bare "false" becomes "No <thing>" so an OFF switch reads as a statement
    // rather than a label with an easily-missed value hanging off it.
    auto prettyOption = [](const QString& name, const QJsonValue& val,
                           const QString& display) -> QString {
        QString label = name;
        label.replace(QLatin1Char('_'), QLatin1Char(' '));
        if (!label.isEmpty()) label[0] = label[0].toUpper();
        const QString d = display.toLower();
        // `disable_x` inverts: TRUE means the thing is gone, FALSE means it is
        // present. Rendering the raw label would print "Disable gold saucer" for
        // a seed that HAS the Gold Saucer — the exact opposite of the truth.
        if (name.startsWith(QLatin1String("disable_"))) {
            QString thing = label.mid(8);
            return d == QLatin1String("true") ? QStringLiteral("No %1").arg(thing)
                                              : QStringLiteral("%1 on").arg(
                                                    thing.at(0).toUpper() + thing.mid(1));
        }
        if (d == QLatin1String("true"))  return label;
        if (d == QLatin1String("false")) {
            QString rest = label;
            if (rest.startsWith(QLatin1String("Randomize ")))
                rest = rest.mid(10);
            rest[0] = rest[0].toLower();
            return QStringLiteral("No %1").arg(rest);
        }
        return QStringLiteral("%1: %2").arg(label, display.isEmpty()
                                                       ? QString::number(val.toInt())
                                                       : display);
    };

    QStringList chips;
    QString who;
    const QByteArray raw = ApSeedFile::readJson(m_archipelagoJsonPath);
    if (!raw.isEmpty()) {
        const QJsonObject root = QJsonDocument::fromJson(raw).object();
        const QJsonObject player = root.value("player").toObject();
        if (!player.isEmpty()) {
            who = QString("Player %1 · %2")
                      .arg(player.value("slot").toInt())
                      .arg(player.value("name").toString());
        }
        const QJsonObject rules = root.value("rules").toObject();
        const bool freeRoam = root.value("free_roam").toBool(rules.value("free_roam").toBool());
        if (freeRoam)
            chips << chipHtml("Free Roam", true);
        const int shopSlots = root.value("shops").toArray().size();
        if (shopSlots > 0)
            chips << chipHtml(QString("%1 shop slots").arg(shopSlots));
        else if (!rules.value("randomize_shops").toBool())
            chips << chipHtml("No shop checks");
        const int placements = root.value("placements").toArray().size();
        if (placements > 0)
            chips << chipHtml(QString("%1 field checks").arg(placements));
        chips << chipHtml(rules.value("town_gating").toBool() ? "Town gating on"
                                                             : "Town gating off");
        if (rules.value("randomize_starting_equipment").toBool()) {
            // The seed carries the YAML value, 1-5.
            static const char* kTiers[] = {"Weakest", "Weak", "Balanced",
                                           "Strong", "Strongest"};
            const int yamlTier = rules.value("starting_equipment_tier").toInt(3);
            const int idx = qBound(0, yamlTier - 1,
                                   Config::STARTING_EQUIPMENT_TIERS - 1);
            chips << chipHtml(QString("Equipment: %1 (%2)")
                                  .arg(QString::fromLatin1(kTiers[idx])).arg(yamlTier));
        }
        if (rules.value("death_link").toBool())
            chips << chipHtml("Death Link", true);

        // Everything else the YAML asked for, straight from the seed's `options`
        // array (see FF7Exporter._serialize_options). Rendered generically, so a
        // new YAML option shows up here with no change to this file.
        //
        // The sixteen individual trap WEIGHTS are folded into the single traps
        // chip rather than listed: they only matter when traps are on at all, and
        // sixteen extra chips would bury the handful of settings a player is
        // actually checking against their YAML.
        const QJsonArray options = root.value("options").toArray();
        int trapPercent = 0, trapKinds = 0;
        QStringList optionChips;
        for (const QJsonValue& v : options) {
            const QJsonObject o = v.toObject();
            const QString name = o.value("name").toString();
            const QJsonValue val = o.value("value");
            const QString display = o.value("display").toString();

            if (name.endsWith(QLatin1String("_weight"))) {
                if (val.toInt() > 0) ++trapKinds;
                continue;
            }
            if (name == QLatin1String("trap_fill_percent")) {
                trapPercent = val.toInt();
                continue;
            }
            // Already shown above, in a friendlier form.
            if (name == QLatin1String("free_roam")
                || name == QLatin1String("town_gating")
                || name == QLatin1String("death_link")
                || name == QLatin1String("starting_equipment_tier"))
                continue;

            // Highlight anything the player actually changed. Defaults still
            // show — the row is a full account of the seed — but the settings
            // worth checking against the YAML are the ones that stand out.
            const QJsonValue def = o.value("default");
            const bool changed = !def.isNull() && def.toVariant() != val.toVariant();
            optionChips << chipHtml(prettyOption(name, val, display), changed);
        }
        if (trapPercent > 0)
            chips << chipHtml(trapKinds > 0
                                  ? QString("Traps %1%% · %2 kinds").arg(trapPercent).arg(trapKinds)
                                  : QString("Traps %1%%").arg(trapPercent));
        else
            chips << chipHtml("No traps");
        chips += optionChips;
    }
    m_seedPlayerLabel->setText(who);
    m_seedPlayerLabel->setVisible(!who.isEmpty());
    m_seedChips->setText(chips.join(" "));
}

// Step 3 presents the same three checkboxes whether or not a seed is held; the
// lock is what changes. With a seed, the two the multiworld cares about are
// disabled by setOptionsLocked() and the "set by your seed" framing appears.
void SimpleMainWindow::refreshOptionsLockUi()
{
    const bool locked = m_optionsLocked;
    if (m_lockedSectionHeader) m_lockedSectionHeader->setVisible(locked);
    if (m_lockedFootnote)      m_lockedFootnote->setVisible(locked);
    if (m_shopYamlNote)        m_shopYamlNote->setVisible(locked);
}

void SimpleMainWindow::refreshOutputStep()
{
    const bool locked = m_optionsLocked;
    if (m_seedEditRow)   m_seedEditRow->setVisible(!locked);
    if (m_seedLockedRow) m_seedLockedRow->setVisible(locked);
    if (locked && m_seedLockedLabel && m_seedSpin)
        m_seedLockedLabel->setText(QString::number(m_seedSpin->value()));
}

void SimpleMainWindow::setStage(int stage, int state)
{
    if (stage < 0 || stage >= StageCount || !m_stageIcon[stage])
        return;
    styleStageDot(m_stageIcon[stage], state);
    m_stageLabel[stage]->setStyleSheet(
        state == 1 ? "color: #e8eaed; font-size: 12px; font-weight: 600;"
                   : (state == 2 ? "color: #7d8592; font-size: 12px;"
                                 : "color: #5c6470; font-size: 12px;"));
}

void SimpleMainWindow::setStageVisible(int stage, bool visible)
{
    if (stage >= 0 && stage < StageCount && m_stageRow[stage])
        m_stageRow[stage]->setVisible(visible);
}

// ---------------------------------------------------------------------------
// Slots carried over unchanged in behaviour
// ---------------------------------------------------------------------------

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
        goToStep(StepGame);
        return;
    }

    QDir ff7Dir(ff7Path);
    if (!ff7Dir.exists()) {
        QMessageBox::warning(this, "Error", "FF7 installation path does not exist");
        goToStep(StepGame);
        return;
    }

    // Accept both the classic layout (data/ at the root) and the 2026 re-release
    // (engine + data nested under ff7/workingdir/).
    if (!ff7Dir.exists("data") && !ff7Dir.exists("ff7/workingdir/data")) {
        QMessageBox::warning(this, "Error", "Invalid FF7 installation: data directory not found");
        goToStep(StepGame);
        return;
    }
    if (m_outputFolderEdit->text().isEmpty()) {
        QMessageBox::warning(this, "Error", "Please choose an output folder");
        return;
    }

    updateConfig();

    m_cancelRequested = false;
    m_cancelButton->setEnabled(true);
    m_cancelButton->setText("Cancel");
    // A stopped or failed run repoints this button at "Back"; put it back to
    // cancelling before every run, or the second run cannot be stopped.
    disconnect(m_cancelButton, nullptr, nullptr, nullptr);
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        m_cancelRequested = true;
        m_cancelButton->setEnabled(false);
        m_cancelButton->setText("Stopping…");
        appendConsoleMessage("Cancel requested - stopping after the current pass.");
    });
    m_runTimer.start();
    m_consoleOutput->clear();
    m_lastIroPath.clear();
    for (int i = 0; i < StageCount; ++i) {
        setStage(i, 0);
        setStageVisible(i, true);
    }
    // Two passes are conditional; hide the rows rather than leaving them pending
    // forever, so the list is an honest account of what this run will do.
    const bool willCrater = m_config.getFreeRoam();
    const bool willIro    = m_config.getExportIro();
    setStageVisible(StageCrater, willCrater);
    setStageVisible(StageIro, willIro);

    goToStep(StepRun);
    m_currentStep = StepRun;   // goToStep may bounce on a bad path; pin it here
    m_progressBar->setValue(0);
    m_runHeadline->setText("Randomizing…");
    m_runDetail->setText("This usually takes a minute or two.");
    QApplication::processEvents();

    appendConsoleMessage("=== Starting Randomization ===");
    appendConsoleMessage("FF7 Path: " + ff7Path);
    appendConsoleMessage("Output: " + m_outputFolderEdit->text());

    // First run: no config file exists yet, so persist the settings the player just
    // entered rather than making them find the Save button. Only ever writes when
    // the file is ABSENT — once it exists, saving stays an explicit action so a
    // one-off tweak for a single run is never silently made permanent.
    // The .apff7 path is deliberately excluded (saveToFile's includeApJsonPath):
    // it belongs to one seed, and a stale path reloaded on the next launch would
    // silently randomize against the wrong world.
    {
        const QString configPath = configFilePath();
        if (!QFileInfo::exists(configPath)) {
            if (m_config.saveToFile(configPath, /*includeApJsonPath=*/false))
                appendConsoleMessage("No config found - saved your settings to: " + configPath);
            else
                appendConsoleMessage("WARNING: could not write config to: " + configPath);
        }
    }

    QStringList ran;
    bool cancelled = false;
    auto stopHere = [&](int nextStage) {
        if (!m_cancelRequested)
            return false;
        cancelled = true;
        for (int i = nextStage; i < StageCount; ++i)
            setStage(i, 3);
        appendConsoleMessage("=== Cancelled ===");
        return true;
    };

    // Everything needed to reproduce the run, at the top of the log, before
    // anything can go wrong. A bug report that starts here is actionable.
    GsLog::banner("Randomization run");
    qInfo().noquote() << "FF7 path   :" << ff7Path;
    qInfo().noquote() << "Output     :" << m_config.getOutputFolder();
    qInfo().noquote() << "Seed       :" << QString::number(m_config.getSeed());
    qInfo().noquote() << "AP seed    :" << (m_config.getApJsonPath().isEmpty()
                                                ? QStringLiteral("(none)")
                                                : m_config.getApJsonPath());
    qInfo().noquote() << "Features   : fields="
                      << m_config.isFeatureEnabled(Config::FieldPickupRandomization)
                      << " shops=" << m_config.isFeatureEnabled(Config::ShopRandomization)
                      << " equip=" << m_config.isFeatureEnabled(Config::StartingEquipmentRandomization)
                      << " enemyStats=" << m_config.isFeatureEnabled(Config::EnemyStatsRandomization)
                      << " encounters=" << m_config.isFeatureEnabled(Config::EnemyEncounterRandomization)
                      << " ap=" << m_config.isFeatureEnabled(Config::ArchipelagoIntegration);

    try {
        Randomizer randomizer(ff7Path, m_config);
        qInfo().noquote() << "Resolved   :" << randomizer.getFF7Path()
                          << "(2026 layout detected if this differs from FF7 path)";

        setStage(StageCopy, 1);
        m_runDetail->setText("Preparing the output folder…");
        m_statusLabel->setText("Preparing output directory...");
        appendConsoleMessage("Preparing output directory...");
        QApplication::processEvents();

        if (!randomizer.copyOriginalFiles()) {
            reportPassFailure("Copying the original files",
                              QStringLiteral("Could not copy the game files into:\n%1\n\n"
                                             "Check the output folder is writable and has room for a "
                                             "full copy of the game data.")
                                  .arg(randomizer.getOutputPath()));
            goToStep(StepOutput);
            return;
        }
        appendConsoleMessage("Original files copied successfully");
        setStage(StageCopy, 2);
        m_progressBar->setValue(15);
        ran << "Copied the original files";

        // The shop pass does TWO independent jobs: Gold Saucer's own stock
        // randomization (the checkbox) and injecting the seed's Archipelago shop
        // slots. Gating the whole pass on the checkbox meant a player who wanted
        // vanilla stock silently lost every AP shop check, because the tokens were
        // never written. So run it whenever EITHER applies; ShopRandomizer itself
        // decides whether to reshuffle the stock.
        const bool wantStock = m_config.isFeatureEnabled(Config::ShopRandomization);
        const bool wantApSlots = !m_config.getApJsonPath().isEmpty();
        if (!stopHere(StageShops) && (wantStock || wantApSlots)) {
            setStage(StageShops, 1);
            const QString what = wantStock
                ? (wantApSlots ? "Randomizing shops + injecting AP slots..."
                               : "Randomizing shops...")
                : "Injecting AP shop slots (stock randomization off)...";
            m_statusLabel->setText(what);
            m_runDetail->setText(what);
            appendConsoleMessage(what);
            QApplication::processEvents();

            if (!randomizer.randomizeShops()) {
                reportPassFailure("Shop randomization", randomizer.lastError());
                goToStep(StepOptions);
                return;
            }
            appendConsoleMessage("Shop pass completed successfully");
            setStage(StageShops, 2);
            ran << (wantStock ? "Randomized shop stock" : "Injected the seed's shop slots");
        } else if (!cancelled) {
            setStage(StageShops, 3);
        }
        m_progressBar->setValue(35);

        if (!stopHere(StageFields) && m_config.isFeatureEnabled(Config::FieldPickupRandomization)) {
            setStage(StageFields, 1);
            m_statusLabel->setText("Randomizing Field Pickups...");
            m_runDetail->setText("Patching field pickups — this is the long one.");
            appendConsoleMessage("Randomizing Field Pickups...");
            QApplication::processEvents();

            if (!randomizer.randomizeFieldPickups()) {
                reportPassFailure("Field pickup randomization", randomizer.lastError());
                goToStep(StepOptions);
                return;
            }
            appendConsoleMessage("Field pickup randomization completed successfully");
            setStage(StageFields, 2);
            ran << "Patched the field pickups";
        } else if (!cancelled) {
            setStage(StageFields, 3);
        }
        m_progressBar->setValue(60);

        // ALWAYS run the kernel init-data pass: it also sets Cloud's starting
        // level, which must apply whether or not starting-equipment
        // randomization is enabled. The flag gates only the equipment shuffle.
        if (!stopHere(StageEquip)) {
            const bool shuffleEquipment =
                m_config.isFeatureEnabled(Config::StartingEquipmentRandomization);
            setStage(StageEquip, 1);
            m_statusLabel->setText(shuffleEquipment
                                       ? "Randomizing Starting Equipment..."
                                       : "Applying Starting Levels...");
            m_runDetail->setText(shuffleEquipment ? "Randomizing starting equipment…"
                                                  : "Applying starting levels…");
            appendConsoleMessage(shuffleEquipment
                                     ? "Randomizing Starting Equipment..."
                                     : "Applying starting levels (equipment shuffle off)...");
            QApplication::processEvents();

            if (!randomizer.randomizeStartingEquipment(shuffleEquipment)) {
                reportPassFailure("Starting equipment/level pass", randomizer.lastError());
                goToStep(StepOptions);
                return;
            }
            appendConsoleMessage(shuffleEquipment
                                     ? "Starting equipment randomization completed successfully"
                                     : "Starting levels applied successfully");
            setStage(StageEquip, 2);
            ran << (shuffleEquipment ? "Randomized starting equipment"
                                     : "Applied starting levels");
        }
        m_progressBar->setValue(75);

        if (!stopHere(StageCrater) && willCrater) {
            setStage(StageCrater, 1);
            m_runDetail->setText("Reactivating the Northern Crater barrier…");
            appendConsoleMessage("Reactivating Northern Crater barrier (goal gate)...");
            QApplication::processEvents();
            if (!randomizer.applyCraterBarrier()) {
                appendConsoleMessage("WARNING: Crater barrier patch failed — world_us.lgp not found or unrecognised; "
                                     "crater will remain open");
                setStage(StageCrater, 3);
            } else {
                appendConsoleMessage("Crater barrier patch applied to world_us.lgp");
                setStage(StageCrater, 2);
                ran << "Re-gated the Northern Crater";
            }
        }
        m_progressBar->setValue(88);

        // Optional: pack the randomized output into a 7th Heaven .iro archive.
        if (!stopHere(StageIro) && willIro) {
            setStage(StageIro, 1);
            m_statusLabel->setText("Exporting .iro...");
            m_runDetail->setText("Packing the 7th Heaven archive…");
            appendConsoleMessage("Exporting 7th Heaven .iro archive...");
            QApplication::processEvents();

            QString outDir = randomizer.getOutputPath();
            QString iroName = QString("FF7_AP_%1.iro").arg(m_config.getSeed());
            QString iroPath = QDir(outDir).filePath(iroName);

            IroExporter iro(ff7Path, outDir);
            QStringList iroLog;
            bool iroOk = iro.exportIro(iroPath, m_config, iroLog);
            for (const QString& line : iroLog)
                appendConsoleMessage(line);
            if (iroOk) {
                appendConsoleMessage("IRO export complete: " + iroPath);
                m_lastIroPath = iroPath;
                setStage(StageIro, 2);
                ran << "Packed a 7th Heaven .iro";
            } else {
                appendConsoleMessage("WARNING: IRO export produced no archive (see notes above)");
                setStage(StageIro, 3);
            }
        }

        m_progressBar->setValue(100);
        m_statusLabel->setText("Randomization Complete!");

        if (cancelled) {
            m_runHeadline->setText("Stopped");
            m_runDetail->setText("The passes that had already finished are in your output "
                                 "folder. Run again to start over.");
            m_cancelButton->setText("Back");
            m_cancelButton->setEnabled(true);
            disconnect(m_cancelButton, nullptr, nullptr, nullptr);
            connect(m_cancelButton, &QPushButton::clicked, this,
                    [this]() { m_currentStep = StepOutput; goToStep(StepOutput); });
            return;
        }

        appendConsoleMessage("=== Randomization Complete ===");

        // Finish screen. Everything on it is something this run actually reported;
        // no counts are invented.
        const double secs = m_runTimer.elapsed() / 1000.0;
        QString when = secs < 60 ? QString("%1s").arg(secs, 0, 'f', 1)
                                 : QString("%1m %2s").arg(int(secs) / 60).arg(int(secs) % 60);
        m_doneSubtitle->setText(QString("Seed %1 · finished in %2")
                                    .arg(m_config.getSeed()).arg(when));
        m_donePasses->setText(ran.join("  ·  "));
        m_doneFolderEdit->setText(randomizer.getOutputPath());
        m_doneIroRow->setVisible(!m_lastIroPath.isEmpty());
        if (!m_lastIroPath.isEmpty())
            m_doneIroEdit->setText(QFileInfo(m_lastIroPath).fileName());
        m_currentStep = StepRun;
        goToStep(StepDone);

    } catch (const std::exception& e) {
        appendConsoleMessage("ERROR: " + QString(e.what()));
        qCritical() << "Unhandled exception during randomization:" << e.what();
        reportPassFailure("Randomization",
                          QStringLiteral("Unexpected error: %1").arg(QString::fromUtf8(e.what())));
        m_runHeadline->setText("Randomization failed");
        m_runDetail->setText("The log below has the details.");
        m_cancelButton->setText("Back");
        disconnect(m_cancelButton, nullptr, nullptr, nullptr);
        connect(m_cancelButton, &QPushButton::clicked, this,
                [this]() { m_currentStep = StepOutput; goToStep(StepOutput); });
    }
}

void SimpleMainWindow::loadConfig()
{
    QString configPath = configFilePath();
    if (m_config.loadFromFile(configPath)) {
        applyConfigToUI();
        appendConsoleMessage(QString("Config loaded from: %1").arg(configPath));
    } else {
        appendConsoleMessage(QString("Could not load config from: %1").arg(configPath));
    }
}

QString SimpleMainWindow::configFilePath() const
{
    return QCoreApplication::applicationDirPath() + "/randomizer_config.json";
}

void SimpleMainWindow::saveConfig()
{
    updateConfig();
    QString configPath = configFilePath();
    bool saveResult = m_config.saveToFile(configPath);
    appendConsoleMessage(QString("Config saved to: %1 (Success: %2)").arg(configPath).arg(saveResult));
}

void SimpleMainWindow::resetToDefaults()
{
    // Reset restores randomization OPTION defaults. The install path, the output
    // folder and the .IRO toggle are machine-local output choices, not options —
    // the same three the seed lock leaves editable — so they survive a Reset rather
    // than making the player re-browse for their game install.
    //
    // Both paths have to be carried across explicitly: Config::setDefaults()
    // replaces the output folder with the bare "Randomized" placeholder, and it
    // never touches m_ff7Path, so that one would come back as whatever happened to
    // be synced last (empty, if the player typed a path and hit Reset before any
    // import/save/start). Reading the widgets is what makes this correct in every
    // case — they are the live truth, the config may lag them.
    const QString ff7Path      = m_ff7PathEdit->text();
    const QString outputFolder = m_outputFolderEdit->text();
    const bool    exportIro    = m_iroCheckBox->isChecked();

    m_config.setDefaults();
    clearArchipelagoSeed();   // also releases the option lock via applyConfigToUI

    m_config.setFF7Path(ff7Path);
    m_config.setOutputFolder(outputFolder);
    m_config.setExportIro(exportIro);

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

    m_config.setFeatureEnabled(Config::ArchipelagoIntegration, m_archipelagoCheckBox->isChecked());
    m_config.setFreeRoam(m_freeRoamCheckBox->isChecked());
    m_config.setExportIro(m_iroCheckBox->isChecked());

    // Settings. Shop pool size, shop price variance and field pickup rarity used
    // to be written here from three controls that no randomizer ever read back —
    // and Gold Saucer does not randomize prices at all. The controls are gone;
    // the Config setters remain for save-file compatibility.
    m_config.setStartingEquipmentTier(m_equipmentCombo->currentIndex());
    m_config.setSeed(m_seedSpin->value());

    // Paths
    m_config.setOutputFolder(m_outputFolderEdit->text());
    m_config.setFF7Path(m_ff7PathEdit->text());

    // Archipelago settings
    m_config.setApJsonPath(m_archipelagoJsonPath);
}

// Every control whose value is dictated by the loaded .apff7. A seed pins these
// on the generation side, so letting the player change them here would silently
// produce a build that doesn't match the multiworld (wrong seed, a feature the
// server expects switched off, Free Roam disagreeing with the rules the logic was
// generated under). Deliberately NOT locked: the two paths + their Browse buttons
// and the .IRO toggle (all purely local output choices), plus Import (load a
// different seed), Save Config, Reset and Start.
QList<QWidget*> SimpleMainWindow::lockableOptionWidgets() const
{
    // NOT m_shopCheckBox. Gold Saucer's own stock reshuffle is the one setting here
    // the multiworld has no stake in: the seed's AP shop slots are injected either
    // way (see ShopRandomizer::applyApShops), and the vanilla stock around them is
    // pure flavour that no AP rule reads. A player who wants their seed's shop checks
    // sitting in otherwise-vanilla stores must be able to say so with a seed loaded.
    // The seed still SETS it on import from features[1]; it just stays editable.
    //
    // The rest stay locked because they do change what the seed means: field pickups
    // are where AP checks live, starting equipment is what you begin holding, and
    // Free Roam / AP mode define the run.
    return {
        m_fieldCheckBox, m_keyItemCheckBox, m_equipmentCheckBox,
        m_archipelagoCheckBox, m_freeRoamCheckBox,
        m_equipmentCombo,
        m_seedSpin, m_randomSeedButton,
        // Loading a config would overwrite the seed-derived settings wholesale,
        // which is exactly what the lock exists to prevent.
        m_loadConfigButton,
    };
}

void SimpleMainWindow::setOptionsLocked(bool locked)
{
    if (locked == m_optionsLocked)
        return;
    m_optionsLocked = locked;

    const QString why = QStringLiteral(
        "Locked by the imported Archipelago seed.\n"
        "This setting comes from the .apff7 and must match the multiworld.\n"
        "Use Reset to clear the seed if you want to randomize manually.");

    for (QWidget* w : lockableOptionWidgets()) {
        if (!w)
            continue;
        if (locked) {
            if (!m_unlockedTooltips.contains(w))
                m_unlockedTooltips.insert(w, w->toolTip());
            w->setToolTip(why);
        } else if (m_unlockedTooltips.contains(w)) {
            w->setToolTip(m_unlockedTooltips.value(w));
        }
        w->setEnabled(!locked);
    }

    // The Archipelago toggle is an indicator, not a control: meaningless without a
    // seed and mandatory with one, so it stays disabled either way. Without this
    // the unlock pass above would re-enable it with no seed loaded.
    if (m_archipelagoCheckBox)
        m_archipelagoCheckBox->setEnabled(false);

    appendConsoleMessage(locked
        ? "Settings locked to the imported Archipelago seed (paths, shop stock and .IRO export stay editable)"
        : "Settings unlocked - no Archipelago seed loaded");

    refreshOptionsLockUi();
    refreshOutputStep();
}

// Drop the held seed and release the lock, so the player can go back to a plain
// manual randomization without restarting.
void SimpleMainWindow::clearArchipelagoSeed()
{
    m_archipelagoJsonPath.clear();
    m_archipelagoModeEnabled = false;
    m_config.setApJsonPath(QString());
    m_config.setFeatureEnabled(Config::ArchipelagoIntegration, false);
    if (m_archipelagoJsonEdit)
        m_archipelagoJsonEdit->clear();
}

void SimpleMainWindow::applyConfigToUI()
{
    // Features
    m_shopCheckBox->setChecked(m_config.isFeatureEnabled(Config::ShopRandomization));
    m_fieldCheckBox->setChecked(m_config.isFeatureEnabled(Config::FieldPickupRandomization));
    // Key item randomization is hidden and forced off; see setupUI.
    m_keyItemCheckBox->setChecked(false);
    m_equipmentCheckBox->setChecked(m_config.isFeatureEnabled(Config::StartingEquipmentRandomization));

    // Archipelago mode (only enable if JSON was imported)
    bool archipelagoConfigEnabled = m_config.isFeatureEnabled(Config::ArchipelagoIntegration);
    QString savedApJson = m_config.getApJsonPath();
    // Only adopt a saved seed path if the file is still there — otherwise we would
    // come up locked to a .apff7 the player has finished or deleted, with no
    // obvious way to tell why everything is greyed out.
    if (!savedApJson.isEmpty() && QFileInfo::exists(savedApJson)) {
        m_archipelagoJsonPath = savedApJson;
        m_archipelagoJsonEdit->setText(QFileInfo(savedApJson).fileName());
    } else if (!savedApJson.isEmpty()) {
        appendConsoleMessage("Saved Archipelago seed no longer exists, ignoring: " + savedApJson);
        m_config.setApJsonPath(QString());
    }
    if (archipelagoConfigEnabled && !m_archipelagoJsonPath.isEmpty()) {
        m_archipelagoCheckBox->setChecked(true);
        m_archipelagoCheckBox->setEnabled(true);
        m_archipelagoModeEnabled = true;
    } else {
        m_archipelagoCheckBox->setChecked(false);
        m_archipelagoCheckBox->setEnabled(false);
        m_archipelagoModeEnabled = false;
    }

    m_freeRoamCheckBox->setChecked(m_config.getFreeRoam());
    m_iroCheckBox->setChecked(m_config.getExportIro());

    // Settings
    m_equipmentCombo->setCurrentIndex(m_config.getStartingEquipmentTier());
    m_seedSpin->setValue(m_config.getSeed());

    // Paths
    m_outputFolderEdit->setText(m_config.getOutputFolder());
    m_ff7PathEdit->setText(m_config.getFF7Path());

    // Must be last: this disables widgets the branches above just enabled.
    setOptionsLocked(m_archipelagoModeEnabled && !m_archipelagoJsonPath.isEmpty());

    refreshInstallDetection();
    refreshSeedSummary();
    refreshOptionsLockUi();
    refreshOutputStep();
    refreshRail();
}

void SimpleMainWindow::reportPassFailure(const QString& passName, const QString& reason)
{
    // One place that turns a failed pass into something a user can act on or
    // send us. Before this, every pass failed with a bare "<pass> failed" and
    // the actual reason - when one existed at all - went to a qDebug() stream
    // that a WIN32 build discards.
    const QString detail = reason.trimmed();
    appendConsoleMessage("ERROR: " + passName + " failed");
    if (!detail.isEmpty())
        appendConsoleMessage("REASON: " + detail);

    const QString logPath = GsLog::path();
    if (!logPath.isEmpty())
        appendConsoleMessage("Full log: " + logPath);

    QMessageBox box(this);
    box.setIcon(QMessageBox::Critical);
    box.setWindowTitle("Randomization failed");
    box.setText(passName + " failed.");
    box.setInformativeText(detail.isEmpty()
                               ? QStringLiteral("No reason was reported. The run log has the last "
                                                "steps the pass took before it stopped.")
                               : detail);
    if (!logPath.isEmpty())
        box.setDetailedText("Log file:\n" + logPath +
                            "\n\nAttach this file to a bug report - it has the full run.");

    QPushButton* openLog = nullptr;
    if (!logPath.isEmpty())
        openLog = box.addButton("Open log folder", QMessageBox::ActionRole);
    box.addButton(QMessageBox::Close);
    box.exec();

    if (openLog && box.clickedButton() == openLog)
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(logPath).absolutePath()));
}

void SimpleMainWindow::appendConsoleMessage(const QString& message)
{
    // Tee to the run log so a bug report contains the user-facing narrative and
    // the internal diagnostics interleaved in one file. The console pane itself
    // is memory-only and dies with the window.
    GsLog::note(message);

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
        "Select Archipelago FF7 File",
        QDir::homePath(),
        "Archipelago FF7 Files (*.apff7);;JSON Files (*.json);;All Files (*)");

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

    // Capture whatever the user has already entered into the UI BEFORE touching
    // m_config. The import finishes with applyConfigToUI(), which copies config ->
    // widgets; on a first launch (no saved config) every unsynced field would be
    // overwritten with an empty/default value. The line edits are only ever read
    // into m_config here and in updateConfig() — even the Browse buttons just set
    // the text — so without this the install path and output folder are CLEARED the
    // moment a .apff7 is imported (reported by a first-time user). The IRO and key
    // item checkboxes were lost the same way, since the import below never syncs
    // them from the JSON.
    updateConfig();

    // Sync seed and randomizer settings from AP JSON
    {
        const QByteArray seedJson = ApSeedFile::readJson(filePath);
        if (!seedJson.isEmpty()) {
            QJsonDocument seedDoc = QJsonDocument::fromJson(seedJson);
            QJsonObject seedRoot = seedDoc.object();

            if (seedRoot.contains("seed")) {
                // AP seeds can be very large (>64-bit). Read as string and fold
                // into the seed-spin range to keep determinism.
                QString seedStr = seedRoot["seed"].toVariant().toString();
                quint64 seedHash = 1469598103934665603ULL; // FNV-1a 64-bit offset
                for (QChar c : seedStr) {
                    seedHash ^= static_cast<quint64>(c.unicode());
                    seedHash *= 1099511628211ULL;
                }
                unsigned int apSeed = static_cast<unsigned int>(seedHash % 1000000ULL);
                m_seedSpin->setValue(static_cast<int>(apSeed));
                m_config.setSeed(apSeed);
                appendConsoleMessage(QString("Seed synced from Archipelago JSON: %1 (raw: %2)")
                    .arg(apSeed).arg(seedStr));
            }

            // The .apff7 has NO top-level "options" object. Everything the seed
            // dictates lives under "rules" (see the apworld's
            // json_export._serialize_rules). This read used seedRoot["options"],
            // which is always an empty object, so starting_equipment_tier was NEVER
            // synced from the seed and silently stayed on whatever the GUI had -
            // reported 2026-09-02 as "the tier does nothing". "options" is kept as a
            // fallback in case an older seed ever wrote one.
            QJsonObject rules = seedRoot["rules"].toObject();
            QJsonObject opts  = seedRoot["options"].toObject();
            auto seedValue = [&](const char* key) -> QJsonValue {
                const QString k = QString::fromLatin1(key);
                if (rules.contains(k)) return rules.value(k);
                if (opts.contains(k))  return opts.value(k);
                return QJsonValue();
            };

            const QJsonValue tierValue = seedValue("starting_equipment_tier");
            if (!tierValue.isUndefined() && !tierValue.isNull()) {
                // The YAML option is 1-5; Config stores it 0-based. This used to
                // treat the value as a direct index and clamp to 0-2, so YAML 3,
                // 4 and 5 all became "Strong" and tier 0 could never be reached.
                const int yamlTier = tierValue.toInt(3);
                const int tier = qBound(0, yamlTier - 1,
                                        Config::STARTING_EQUIPMENT_TIERS - 1);
                m_equipmentCombo->setCurrentIndex(tier);
                m_config.setStartingEquipmentTier(tier);
                appendConsoleMessage(
                    QString("Starting equipment tier synced: %1 (YAML %2)")
                        .arg(m_equipmentCombo->currentText()).arg(yamlTier));
            }

            // Read free_roam from top-level or rules sub-object
            bool freeRoamFromJson = false;
            if (seedRoot.contains("free_roam")) {
                freeRoamFromJson = seedRoot["free_roam"].toBool(false);
            } else if (seedRoot.contains("rules")) {
                freeRoamFromJson = seedRoot["rules"].toObject()["free_roam"].toBool(false);
            }
            m_config.setFreeRoam(freeRoamFromJson);
            m_freeRoamCheckBox->setChecked(freeRoamFromJson);
            if (freeRoamFromJson) {
                appendConsoleMessage("Free Roam mode enabled from Archipelago JSON");
            }
            // Load feature flags from the features array (boolean array indexed by Feature enum)
            if (seedRoot.contains("features")) {
                QJsonArray features = seedRoot["features"].toArray();
                if (features.size() >= 4) {
                    m_config.setFeatureEnabled(Config::EnemyStatsRandomization, features[0].toBool(false));
                    m_config.setFeatureEnabled(Config::ShopRandomization, features[1].toBool(false));
                    m_config.setFeatureEnabled(Config::FieldPickupRandomization, features[2].toBool(false));
                    m_config.setFeatureEnabled(Config::StartingEquipmentRandomization, features[3].toBool(false));
                    if (features.size() >= 7) {
                        m_config.setFeatureEnabled(Config::BossProtection, features[6].toBool(false));
                    }
                    m_config.setFeatureEnabled(Config::ArchipelagoIntegration, true);
                    m_config.setFeatureEnabled(Config::TextReplacement, true);
                    appendConsoleMessage("Feature flags synced from Archipelago JSON");
                }
            }
            applyConfigToUI();  // Update checkboxes to reflect loaded features
        }
    }

    // Enable Archipelago mode
    m_archipelagoJsonPath = filePath;
    m_config.setApJsonPath(filePath);
    // Importing a seed IS the enable signal — don't leave this to the JSON's
    // optional "features" array (updateConfig() above latches it from the
    // checkbox, which is still unchecked on a first import).
    m_config.setFeatureEnabled(Config::ArchipelagoIntegration, true);
    m_archipelagoJsonEdit->setText(QFileInfo(filePath).fileName());
    m_archipelagoCheckBox->setEnabled(true);
    m_archipelagoCheckBox->setChecked(true);
    m_archipelagoModeEnabled = true;

    // After the AP widgets above are in their final state, not before.
    setOptionsLocked(true);

    appendConsoleMessage("Archipelago JSON imported: " + QFileInfo(filePath).fileName());

    refreshSeedSummary();
    refreshRail();
}

void SimpleMainWindow::toggleArchipelagoMode(bool enabled)
{
    m_archipelagoModeEnabled = enabled;

    if (enabled) {
        appendConsoleMessage("Archipelago mode ENABLED");
    } else {
        appendConsoleMessage("Archipelago mode DISABLED");
    }
}

bool SimpleMainWindow::validateArchipelagoJSON(const QString& filePath)
{
    // Reads both formats: legacy bare JSON and the APPlayerContainer zip
    // (archipelago.json manifest + ff7_seed.json payload) the apworld now emits.
    const QByteArray data = ApSeedFile::readJson(filePath);
    if (data.isEmpty()) {
        return false;
    }

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
