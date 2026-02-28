#pragma once

#include <QString>
#include <QMap>
#include <QVector>
#include <QtGlobal>

// FF7 Enemy Classification System
// Based on FF7 research and community documentation

enum class EnemyType {
    Normal,      // Regular encounter enemies (ID 1-200)
    Boss,        // Major story bosses (ID 201-300)
    MiniBoss,    // Sub-bosses and special encounters (ID 301-350)
    Special,     // Unique enemies with special properties (ID 351-500)
    Unused       // Unused enemy slots (ID 501-512)
};

struct EnemyInfo {
    quint16 id;
    QString name;
    EnemyType type;
    quint8 baseLevel;
    QString location;
    bool isStoryBoss;
    bool isOptionalBoss;
    bool isRandomizable;
    
    EnemyInfo() : id(0), type(EnemyType::Normal), baseLevel(1), 
                  isStoryBoss(false), isOptionalBoss(false), isRandomizable(true) {}
};

class EnemyDatabase
{
public:
    static EnemyDatabase& instance();
    
    // Enemy classification methods
    EnemyType getEnemyType(quint16 enemyId) const;
    QString getEnemyName(quint16 enemyId) const;
    bool isBoss(quint16 enemyId) const;
    bool isNormalEnemy(quint16 enemyId) const;
    bool isMiniBoss(quint16 enemyId) const;
    bool isSpecialEnemy(quint16 enemyId) const;
    bool isRandomizable(quint16 enemyId) const;
    
    // Enemy pool management
    QVector<quint16> getNormalEnemyPool() const;
    QVector<quint16> getBossEnemyPool() const;
    QVector<quint16> getMiniBossEnemyPool() const;
    QVector<quint16> getRandomizableEnemyPool() const;
    
    // Enemy information
    EnemyInfo getEnemyInfo(quint16 enemyId) const;
    quint8 getBaseLevel(quint16 enemyId) const;
    QString getLocation(quint16 enemyId) const;
    
    // Database management
    void initializeDatabase();
    void clearDatabase();
    int getEnemyCount() const;
    bool isValidEnemyId(quint16 enemyId) const;
    
    // Debug and validation
    void printDatabaseStats() const;
    bool validateDatabase() const;

private:
    EnemyDatabase();
    ~EnemyDatabase() = default;
    
    // Prevent copying
    EnemyDatabase(const EnemyDatabase&) = delete;
    EnemyDatabase& operator=(const EnemyDatabase&) = delete;
    
    // Database initialization methods
    void initializeNormalEnemies();
    void initializeBossEnemies();
    void initializeMiniBossEnemies();
    void initializeSpecialEnemies();
    
    // Helper methods
    void addEnemy(quint16 id, const QString& name, EnemyType type, 
                   quint8 level, const QString& location, 
                   bool storyBoss = false, bool optionalBoss = false);
    
    // Member variables
    QMap<quint16, EnemyInfo> m_enemyDatabase;
    QVector<quint16> m_normalEnemyPool;
    QVector<quint16> m_bossEnemyPool;
    QVector<quint16> m_miniBossEnemyPool;
    QVector<quint16> m_randomizableEnemyPool;
    bool m_initialized;
    
    // Constants
    static const quint16 NORMAL_ENEMY_START = 1;
    static const quint16 NORMAL_ENEMY_END = 200;
    static const quint16 BOSS_ENEMY_START = 201;
    static const quint16 BOSS_ENEMY_END = 300;
    static const quint16 MINIBOSS_ENEMY_START = 301;
    static const quint16 MINIBOSS_ENEMY_END = 350;
    static const quint16 SPECIAL_ENEMY_START = 351;
    static const quint16 SPECIAL_ENEMY_END = 500;
    static const quint16 UNUSED_ENEMY_START = 501;
    static const quint16 UNUSED_ENEMY_END = 512;
    static const quint16 MAX_ENEMY_ID = 512;
};
