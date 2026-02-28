#include "EnemyDatabase.h"
#include <QDebug>
#include <QStandardPaths>

EnemyDatabase::EnemyDatabase()
    : m_initialized(false)
{
    initializeDatabase();
}

EnemyDatabase& EnemyDatabase::instance()
{
    static EnemyDatabase instance;
    return instance;
}

void EnemyDatabase::initializeDatabase()
{
    if (m_initialized) {
        return;
    }
    
    qDebug() << "Initializing FF7 Enemy Database...";
    
    clearDatabase();
    
    // Initialize enemy classifications
    initializeNormalEnemies();
    initializeBossEnemies();
    initializeMiniBossEnemies();
    initializeSpecialEnemies();
    
    // Build enemy pools
    for (auto it = m_enemyDatabase.begin(); it != m_enemyDatabase.end(); ++it) {
        const EnemyInfo& enemy = it.value();
        
        if (enemy.isRandomizable) {
            m_randomizableEnemyPool.append(enemy.id);
        }
        
        switch (enemy.type) {
        case EnemyType::Normal:
            m_normalEnemyPool.append(enemy.id);
            break;
        case EnemyType::Boss:
            m_bossEnemyPool.append(enemy.id);
            break;
        case EnemyType::MiniBoss:
            m_miniBossEnemyPool.append(enemy.id);
            break;
        case EnemyType::Special:
        case EnemyType::Unused:
            // Don't add to randomization pools
            break;
        }
    }
    
    m_initialized = true;
    qDebug() << "Enemy Database initialized with" << m_enemyDatabase.size() << "enemies";
    printDatabaseStats();
}

void EnemyDatabase::initializeNormalEnemies()
{
    // Normal enemies (ID 1-200) - Regular encounter enemies
    // These are fully randomizable
    
    // Sector 1 Slums enemies
    addEnemy(1, "MP", EnemyType::Normal, 2, "Sector 1 Slums");
    addEnemy(2, "Guard Hound", EnemyType::Normal, 3, "Sector 1 Slums");
    addEnemy(3, "Two Guns", EnemyType::Normal, 4, "Sector 1 Slums");
    addEnemy(4, "Mono Drive", EnemyType::Normal, 5, "Sector 1 Slums");
    addEnemy(5, "Sweeper", EnemyType::Normal, 6, "Sector 1 Slums");
    
    // Sector 4-5 Plate enemies
    addEnemy(6, "Grashtrike", EnemyType::Normal, 8, "Sector 4-5 Plate");
    addEnemy(7, "Crawler", EnemyType::Normal, 9, "Sector 4-5 Plate");
    addEnemy(8, "Hedgehog Pie", EnemyType::Normal, 10, "Sector 4-5 Plate");
    addEnemy(9, "Zemzelett", EnemyType::Normal, 11, "Sector 4-5 Plate");
    
    // Sector 6 Slums enemies
    addEnemy(10, "Deenglow", EnemyType::Normal, 12, "Sector 6 Slums");
    addEnemy(11, "Hell House", EnemyType::Normal, 13, "Sector 6 Slums");
    addEnemy(12, "Behemoth", EnemyType::Normal, 14, "Sector 6 Slums");
    
    // Train Graveyard enemies
    addEnemy(13, "Ghost", EnemyType::Normal, 15, "Train Graveyard");
    addEnemy(14, "Stinger", EnemyType::Normal, 16, "Train Graveyard");
    
    // Mythril Mines enemies
    addEnemy(15, "Mandragora", EnemyType::Normal, 18, "Mythril Mines");
    addEnemy(16, "Jenova BIRTH", EnemyType::Boss, 25, "Mythril Mines", true, false); // Actually a boss, but in normal range
    
    // Fort Condor enemies
    addEnemy(17, "Cokatris", EnemyType::Normal, 20, "Fort Condor");
    addEnemy(18, "Zolom", EnemyType::MiniBoss, 30, "Fort Condor Marsh", false, true);
    
    // Junon enemies
    addEnemy(19, "Battery", EnemyType::Normal, 22, "Junon Area");
    addEnemy(20, "Bullmotor", EnemyType::Normal, 23, "Junon Area");
    addEnemy(21, "Death Clod", EnemyType::Normal, 24, "Junon Area");
    
    // Cargo Ship enemies
    addEnemy(22, "Jersey", EnemyType::Normal, 26, "Cargo Ship");
    addEnemy(23, "Punisher", EnemyType::Normal, 27, "Cargo Ship");
    addEnemy(24, "Reno", EnemyType::Boss, 35, "Cargo Ship", true, false); // Actually a boss
    
    // Costa del Sol enemies
    addEnemy(25, "Desert Sahagin", EnemyType::Normal, 28, "Costa del Sol Desert");
    addEnemy(26, "Adamantaimai", EnemyType::Normal, 29, "Costa del Sol Desert");
    
    // Mt. Corel enemies
    addEnemy(27, "Moth Slasher", EnemyType::Normal, 30, "Mt. Corel");
    addEnemy(28, "Nerosuferoth", EnemyType::Normal, 31, "Mt. Corel");
    addEnemy(29, "Cactuar", EnemyType::Special, 35, "Corel Desert", false, false); // Special enemy
    
    // Gold Saucer enemies
    addEnemy(30, "Ghost Ship", EnemyType::Normal, 32, "Gold Saucer Battle Square");
    addEnemy(31, "Joker", EnemyType::Normal, 33, "Gold Saucer Battle Square");
    
    // Gongaga enemies
    addEnemy(32, "Mystery Ninja", EnemyType::Special, 40, "Gongaga Forest", false, false); // Yuffie
    addEnemy(33, "Tail Screw", EnemyType::Normal, 34, "Gongaga Area");
    addEnemy(34, "Crysales", EnemyType::Normal, 35, "Gongaga Area");
    
    // Cosmo Canyon enemies
    addEnemy(35, "Ki-rin", EnemyType::Normal, 36, "Cosmo Canyon Area");
    addEnemy(36, "Stealthyman", EnemyType::Normal, 37, "Cosmo Canyon Area");
    
    // Nibelheim enemies
    addEnemy(37, "Dragon", EnemyType::MiniBoss, 40, "Nibelheim Mountains", false, true);
    addEnemy(38, "Materia Keeper", EnemyType::Boss, 45, "Nibelheim Mountains", true, false);
    
    // Rocket Town enemies
    addEnemy(39, "Shinra Soldier", EnemyType::Normal, 38, "Rocket Town");
    addEnemy(40, "Parasite", EnemyType::Normal, 39, "Rocket Town Area");
    
    // Ancient Forest enemies
    addEnemy(41, "Snapping Turtle", EnemyType::Normal, 40, "Ancient Forest");
    addEnemy(42, "Boundfat", EnemyType::Normal, 41, "Ancient Forest");
    
    // Temple of the Ancients enemies
    addEnemy(43, "Magic Pot", EnemyType::Special, 50, "Temple of the Ancients", false, false);
    addEnemy(44, "Movers", EnemyType::Special, 60, "Temple of the Ancients", false, false);
    addEnemy(45, "Dragon Zombie", EnemyType::Normal, 42, "Temple of the Ancients");
    addEnemy(46, "Poltergeist", EnemyType::Normal, 43, "Temple of the Ancients");
    addEnemy(47, "Jersey", EnemyType::Normal, 44, "Temple of the Ancients");
    
    // Bone Village enemies
    addEnemy(48, "Serpent", EnemyType::Normal, 45, "Bone Village Area");
    addEnemy(49, "Goblin", EnemyType::Normal, 46, "Bone Village Area");
    
    // Icicle Inn enemies
    addEnemy(50, "Snow", EnemyType::Normal, 47, "Icicle Inn Area");
    addEnemy(51, "Wind Wing", EnemyType::Normal, 48, "Icicle Inn Area");
    addEnemy(52, "Ice Golem", EnemyType::Normal, 49, "Icicle Inn Area");
    
    // Great Glacier enemies
    addEnemy(53, "Wolf", EnemyType::Normal, 50, "Great Glacier");
    addEnemy(54, "Tonberry", EnemyType::MiniBoss, 55, "Great Glacier", false, true);
    addEnemy(55, "Mover", EnemyType::Special, 60, "Great Glacier", false, false);
    
    // Gaea's Cliff enemies
    addEnemy(56, "Stilva", EnemyType::Normal, 52, "Gaea's Cliff");
    addEnemy(57, "Dragon", EnemyType::Normal, 53, "Gaea's Cliff");
    addEnemy(58, "Schizo", EnemyType::Boss, 55, "Gaea's Cliff", true, false);
    
    // Underwater Reactor enemies
    addEnemy(59, "Ghost Ship", EnemyType::Normal, 54, "Underwater Reactor");
    addEnemy(60, "Carry Armor", EnemyType::Boss, 60, "Underwater Reactor", true, false);
    
    // Midgar enemies (Disc 2)
    addEnemy(61, "Proud Clod", EnemyType::Boss, 65, "Midgar", true, false);
    addEnemy(62, "Grosspanzer", EnemyType::Normal, 56, "Midgar Sewers");
    addEnemy(63, "Bagnadrana", EnemyType::Normal, 57, "Midgar Sewers");
    
    // Northern Crater enemies
    addEnemy(64, "Dark Dragon", EnemyType::Normal, 58, "Northern Crater");
    addEnemy(65, "Magic Dragon", EnemyType::Normal, 59, "Northern Crater");
    addEnemy(66, "Dragon", EnemyType::Normal, 60, "Northern Crater");
    addEnemy(67, "Spooky", EnemyType::Normal, 61, "Northern Crater");
    addEnemy(68, "Malboro", EnemyType::MiniBoss, 70, "Northern Crater", false, true);
    addEnemy(69, "Ultimate Weapon", EnemyType::Boss, 80, "Northern Crater", true, false);
    
    // Additional normal enemies to fill out the database
    for (int i = 70; i <= 200; i++) {
        QString name = QString("Enemy %1").arg(i);
        addEnemy(i, name, EnemyType::Normal, 10 + (i % 50), "Various Locations");
    }
}

void EnemyDatabase::initializeBossEnemies()
{
    // Major story bosses (ID 201-300) - Protected from normal encounters
    // These have limited randomization to preserve story difficulty
    
    // Disc 1 Bosses
    addEnemy(201, "Guard Scorpion", EnemyType::Boss, 15, "Sector 1 Reactor", true, false);
    addEnemy(202, "Air Buster", EnemyType::Boss, 18, "Sector 1 Pillar", true, false);
    addEnemy(203, "Reno", EnemyType::Boss, 35, "Sector 7 Pillar", true, false);
    addEnemy(204, "H0512-OPT", EnemyType::Boss, 25, "Shinra Building", true, false);
    addEnemy(205, "Jenova-BIRTH", EnemyType::Boss, 25, "Shinra Building", true, false);
    addEnemy(206, "Sample: H0512-OPT", EnemyType::Boss, 30, "Shinra Building", true, false);
    addEnemy(207, "Lost Number", EnemyType::Boss, 35, "Shinra Mansion", true, false);
    addEnemy(208, "Jenova-LIFE", EnemyType::Boss, 45, "Cargo Ship", true, false);
    addEnemy(209, "Bottomswell", EnemyType::Boss, 40, "Underwater Reactor", true, false);
    addEnemy(210, "Schizo", EnemyType::Boss, 55, "Gaea's Cliff", true, false);
    addEnemy(211, "Jenova-DEATH", EnemyType::Boss, 60, "Temple of the Ancients", true, false);
    addEnemy(212, "Diamond Weapon", EnemyType::Boss, 70, "Midgar", true, false);
    addEnemy(213, "Ultimate Weapon", EnemyType::Boss, 80, "Northern Crater", true, false);
    addEnemy(214, "Proud Clod", EnemyType::Boss, 65, "Midgar", true, false);
    addEnemy(215, "Jenova-SYNTHESIS", EnemyType::Boss, 85, "Northern Crater", true, false);
    addEnemy(216, "Bizarro∙Sephiroth", EnemyType::Boss, 90, "Northern Crater", true, false);
    addEnemy(217, "Safer∙Sephiroth", EnemyType::Boss, 99, "Northern Crater", true, false);
    
    // Additional boss slots
    for (int i = 218; i <= 300; i++) {
        QString name = QString("Boss %1").arg(i);
        addEnemy(i, name, EnemyType::Boss, 50 + (i % 50), "Boss Arena", true, false);
    }
}

void EnemyDatabase::initializeMiniBossEnemies()
{
    // Mini-bosses and special encounters (ID 301-350)
    // Limited randomization, encounter-specific
    
    addEnemy(301, "Zolom", EnemyType::MiniBoss, 30, "Fort Condor Marsh", false, true);
    addEnemy(302, "Materia Keeper", EnemyType::MiniBoss, 45, "Nibelheim Mountains", false, true);
    addEnemy(303, "Dragon", EnemyType::MiniBoss, 40, "Nibelheim Mountains", false, true);
    addEnemy(304, "Carry Armor", EnemyType::MiniBoss, 60, "Underwater Reactor", false, true);
    addEnemy(305, "Gorky", EnemyType::MiniBoss, 35, "Fort Condor", false, true);
    addEnemy(306, "Shake", EnemyType::MiniBoss, 35, "Fort Condor", false, true);
    addEnemy(307, "Attack Squad", EnemyType::MiniBoss, 40, "Fort Condor", false, true);
    addEnemy(308, "Commander Grand Horn", EnemyType::MiniBoss, 50, "Fort Condor", false, true);
    addEnemy(309, "Tonberry", EnemyType::MiniBoss, 55, "Great Glacier", false, true);
    addEnemy(310, "Malboro", EnemyType::MiniBoss, 70, "Northern Crater", false, true);
    
    // Additional mini-boss slots
    for (int i = 311; i <= 350; i++) {
        QString name = QString("Mini-Boss %1").arg(i);
        addEnemy(i, name, EnemyType::MiniBoss, 40 + (i % 40), "Special Arena", false, true);
    }
}

void EnemyDatabase::initializeSpecialEnemies()
{
    // Special enemies with unique properties (ID 351-500)
    // These are not randomizable or have very limited randomization
    
    addEnemy(351, "Cactuar", EnemyType::Special, 35, "Corel Desert", false, false);
    addEnemy(352, "Tonberry", EnemyType::Special, 55, "Various", false, false);
    addEnemy(353, "Magic Pot", EnemyType::Special, 50, "Temple of the Ancients", false, false);
    addEnemy(354, "Movers", EnemyType::Special, 60, "Temple of the Ancients", false, false);
    addEnemy(355, "Yuffie", EnemyType::Special, 40, "Gongaga Forest", false, false);
    addEnemy(356, "Vincent", EnemyType::Special, 45, "Shinra Mansion", false, false);
    
    // Additional special enemy slots
    for (int i = 357; i <= 500; i++) {
        QString name = QString("Special Enemy %1").arg(i);
        addEnemy(i, name, EnemyType::Special, 50, "Special Location", false, false);
    }
}

void EnemyDatabase::addEnemy(quint16 id, const QString& name, EnemyType type, 
                             quint8 level, const QString& location, 
                             bool storyBoss, bool optionalBoss)
{
    EnemyInfo enemy;
    enemy.id = id;
    enemy.name = name;
    enemy.type = type;
    enemy.baseLevel = level;
    enemy.location = location;
    enemy.isStoryBoss = storyBoss;
    enemy.isOptionalBoss = optionalBoss;
    
    // Determine if enemy is randomizable based on type
    switch (type) {
    case EnemyType::Normal:
        enemy.isRandomizable = true;
        break;
    case EnemyType::Boss:
        enemy.isRandomizable = false; // Bosses are protected
        break;
    case EnemyType::MiniBoss:
        enemy.isRandomizable = false; // Mini-bosses are protected
        break;
    case EnemyType::Special:
        enemy.isRandomizable = false; // Special enemies are protected
        break;
    case EnemyType::Unused:
        enemy.isRandomizable = false;
        break;
    }
    
    m_enemyDatabase[id] = enemy;
}

EnemyType EnemyDatabase::getEnemyType(quint16 enemyId) const
{
    if (!m_enemyDatabase.contains(enemyId)) {
        return EnemyType::Unused;
    }
    return m_enemyDatabase.value(enemyId).type;
}

QString EnemyDatabase::getEnemyName(quint16 enemyId) const
{
    if (!m_enemyDatabase.contains(enemyId)) {
        return QString("Unknown Enemy %1").arg(enemyId);
    }
    return m_enemyDatabase.value(enemyId).name;
}

bool EnemyDatabase::isBoss(quint16 enemyId) const
{
    EnemyType type = getEnemyType(enemyId);
    return type == EnemyType::Boss;
}

bool EnemyDatabase::isNormalEnemy(quint16 enemyId) const
{
    EnemyType type = getEnemyType(enemyId);
    return type == EnemyType::Normal;
}

bool EnemyDatabase::isMiniBoss(quint16 enemyId) const
{
    EnemyType type = getEnemyType(enemyId);
    return type == EnemyType::MiniBoss;
}

bool EnemyDatabase::isSpecialEnemy(quint16 enemyId) const
{
    EnemyType type = getEnemyType(enemyId);
    return type == EnemyType::Special;
}

bool EnemyDatabase::isRandomizable(quint16 enemyId) const
{
    if (!m_enemyDatabase.contains(enemyId)) {
        return false;
    }
    return m_enemyDatabase.value(enemyId).isRandomizable;
}

QVector<quint16> EnemyDatabase::getNormalEnemyPool() const
{
    return m_normalEnemyPool;
}

QVector<quint16> EnemyDatabase::getBossEnemyPool() const
{
    return m_bossEnemyPool;
}

QVector<quint16> EnemyDatabase::getMiniBossEnemyPool() const
{
    return m_miniBossEnemyPool;
}

QVector<quint16> EnemyDatabase::getRandomizableEnemyPool() const
{
    return m_randomizableEnemyPool;
}

EnemyInfo EnemyDatabase::getEnemyInfo(quint16 enemyId) const
{
    if (!m_enemyDatabase.contains(enemyId)) {
        EnemyInfo unknown;
        unknown.id = enemyId;
        unknown.name = QString("Unknown Enemy %1").arg(enemyId);
        unknown.type = EnemyType::Unused;
        unknown.baseLevel = 1;
        unknown.location = "Unknown";
        unknown.isRandomizable = false;
        return unknown;
    }
    return m_enemyDatabase.value(enemyId);
}

quint8 EnemyDatabase::getBaseLevel(quint16 enemyId) const
{
    if (!m_enemyDatabase.contains(enemyId)) {
        return 1;
    }
    return m_enemyDatabase.value(enemyId).baseLevel;
}

QString EnemyDatabase::getLocation(quint16 enemyId) const
{
    if (!m_enemyDatabase.contains(enemyId)) {
        return "Unknown";
    }
    return m_enemyDatabase.value(enemyId).location;
}

void EnemyDatabase::clearDatabase()
{
    m_enemyDatabase.clear();
    m_normalEnemyPool.clear();
    m_bossEnemyPool.clear();
    m_miniBossEnemyPool.clear();
    m_randomizableEnemyPool.clear();
    m_initialized = false;
}

int EnemyDatabase::getEnemyCount() const
{
    return m_enemyDatabase.size();
}

bool EnemyDatabase::isValidEnemyId(quint16 enemyId) const
{
    return enemyId > 0 && enemyId <= MAX_ENEMY_ID;
}

void EnemyDatabase::printDatabaseStats() const
{
    qDebug() << "=== Enemy Database Statistics ===";
    qDebug() << "Total Enemies:" << m_enemyDatabase.size();
    qDebug() << "Normal Enemies:" << m_normalEnemyPool.size();
    qDebug() << "Boss Enemies:" << m_bossEnemyPool.size();
    qDebug() << "Mini-Boss Enemies:" << m_miniBossEnemyPool.size();
    qDebug() << "Randomizable Enemies:" << m_randomizableEnemyPool.size();
    
    qDebug() << "=== Enemy Type Distribution ===";
    QMap<EnemyType, int> typeCount;
    for (const EnemyInfo& enemy : m_enemyDatabase) {
        typeCount[enemy.type]++;
    }
    
    for (auto it = typeCount.begin(); it != typeCount.end(); ++it) {
        QString typeName;
        switch (it.key()) {
        case EnemyType::Normal: typeName = "Normal"; break;
        case EnemyType::Boss: typeName = "Boss"; break;
        case EnemyType::MiniBoss: typeName = "Mini-Boss"; break;
        case EnemyType::Special: typeName = "Special"; break;
        case EnemyType::Unused: typeName = "Unused"; break;
        }
        qDebug() << typeName << ":" << it.value();
    }
    qDebug() << "================================";
}

bool EnemyDatabase::validateDatabase() const
{
    if (!m_initialized) {
        qDebug() << "Database not initialized";
        return false;
    }
    
    // Check for duplicate IDs
    QSet<quint16> ids;
    for (quint16 id : m_enemyDatabase.keys()) {
        if (ids.contains(id)) {
            qDebug() << "Duplicate enemy ID found:" << id;
            return false;
        }
        ids.insert(id);
    }
    
    // Check enemy pools
    for (quint16 id : m_normalEnemyPool) {
        if (!m_enemyDatabase.contains(id) || !isNormalEnemy(id)) {
            qDebug() << "Invalid enemy in normal pool:" << id;
            return false;
        }
    }
    
    for (quint16 id : m_bossEnemyPool) {
        if (!m_enemyDatabase.contains(id) || !isBoss(id)) {
            qDebug() << "Invalid enemy in boss pool:" << id;
            return false;
        }
    }
    
    return true;
}
