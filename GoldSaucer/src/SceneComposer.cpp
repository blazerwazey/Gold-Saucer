#include "SceneComposer.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <QStringList>

SceneComposer::SceneComposer(std::mt19937& rng,
                             std::function<int(const QByteArray&)> sizeOf)
    : m_rng(rng), m_sizeOf(std::move(sizeOf))
{
}

// ── small readers ──────────────────────────────────────────────────────────

quint16 SceneComposer::readU16(const QByteArray& b, int at)
{
    if (at < 0 || at + 2 > b.size()) return NULL16;
    return static_cast<quint16>(static_cast<quint8>(b[at]))
         | static_cast<quint16>(static_cast<quint8>(b[at + 1])) << 8;
}

void SceneComposer::writeU16(QByteArray& b, int at, quint16 v)
{
    if (at < 0 || at + 2 > b.size()) return;
    b[at]     = static_cast<char>(v & 0xFF);
    b[at + 1] = static_cast<char>((v >> 8) & 0xFF);
}

bool SceneComposer::enemyPresent(const QByteArray& scene, int slot)
{
    if (scene.size() != SCENE_SIZE) return false;
    // an absent enemy's record opens with the 0xFF that terminates its name
    return static_cast<quint8>(scene[ENEMY_BASE + slot * ENEMY_RECORD + REC_NAME]) != 0xFF;
}

QString SceneComposer::enemyName(const QByteArray& scene, int slot)
{
    QString out;
    const int base = ENEMY_BASE + slot * ENEMY_RECORD + REC_NAME;
    for (int i = 0; i < 32; ++i) {
        const quint8 c = static_cast<quint8>(scene[base + i]);
        if (c == 0xFF) break;
        out += (c <= 0xCF) ? QChar(c + 0x20) : QChar('?');
    }
    return out.trimmed();
}

// Attack ids an enemy can use. The manipulated list matters as much as the main
// one: manipulating an enemy whose manip attack is absent from the scene's table
// is a crash, not a missing move.
QVector<quint16> SceneComposer::recordAttackIds(const QByteArray& record)
{
    QVector<quint16> ids;
    auto take = [&](int at, int count) {
        for (int i = 0; i < count; ++i) {
            const quint16 v = readU16(record, at + i * 2);
            if (v != NULL16 && !ids.contains(v)) ids.append(v);
        }
    };
    take(REC_ATTACK_IDS, 16);
    take(REC_MANIP_IDS, 3);
    return ids;
}

// ── lifting one enemy out of its home scene ────────────────────────────────

QByteArray SceneComposer::aiContainer(const QByteArray& scene, int slot) const
{
    const quint16 off = readU16(scene, ENM_AI_OFFSETS + slot * 2);
    if (off == NULL16 || off < AI_OFFSET_ORIGIN) return QByteArray();

    // the block's used extent, so the last container knows where to stop
    int used = ENM_AI_SIZE;
    while (used > 0 && static_cast<quint8>(scene[ENM_AI + used - 1]) == 0xFF) --used;

    int end = used;
    for (int i = 0; i < ENEMIES_PER_SCENE; ++i) {
        const quint16 o = readU16(scene, ENM_AI_OFFSETS + i * 2);
        if (o != NULL16 && o > off) end = qMin(end, static_cast<int>(o) - AI_OFFSET_ORIGIN);
    }
    const int start = static_cast<int>(off) - AI_OFFSET_ORIGIN;
    if (start < 0 || end < start || end > ENM_AI_SIZE) return QByteArray();
    return scene.mid(ENM_AI + start, end - start);
}

SceneComposer::Donor SceneComposer::makeDonor(const QVector<QByteArray>& scenes,
                                              int scene, int slot) const
{
    Donor d;
    const QByteArray& s = scenes[scene];
    if (s.size() != SCENE_SIZE || !enemyPresent(s, slot)) return d;

    d.scene   = scene;
    d.slot    = slot;
    d.modelId = readU16(s, MODEL_IDS + slot * 2);
    d.record  = s.mid(ENEMY_BASE + slot * ENEMY_RECORD, ENEMY_RECORD);
    d.ai      = aiContainer(s, slot);
    d.attackIds = recordAttackIds(d.record);

    // the attack rows this enemy needs, taken from its own scene's table
    for (int i = 0; i < ATTACK_SLOTS; ++i) {
        const quint16 id = readU16(s, ATK_IDS + i * 2);
        if (id == NULL16 || !d.attackIds.contains(id)) continue;
        d.atkData.insert(id, s.mid(ATK_DATA + i * ATK_DATA_STRIDE, ATK_DATA_STRIDE));
        d.atkName.insert(id, s.mid(ATK_NAMES + i * ATK_NAME_STRIDE, ATK_NAME_STRIDE));
    }
    // A handful of shipped enemies (Lessaloploth, one King Behemoth) name an
    // attack their own scene does not carry. They cannot be donors: there is no
    // row to copy, and moving them would take the missing attack with them.
    for (quint16 id : d.attackIds) {
        if (!d.atkData.contains(id)) return d;   // stays invalid
    }
    d.valid = true;
    return d;
}

double SceneComposer::sceneAvgHp(const QByteArray& scene)
{
    if (scene.size() != SCENE_SIZE) return 0.0;
    double total = 0.0;
    int n = 0;
    for (int e = 0; e < ENEMIES_PER_SCENE; ++e) {
        if (!enemyPresent(scene, e)) continue;
        quint32 hp = 0;
        memcpy(&hp, scene.constData() + ENEMY_BASE + e * ENEMY_RECORD + ENM_HP, 4);
        if (hp == 0 || hp == 0xFFFFFFFFu) continue;
        total += hp;
        ++n;
    }
    return n ? total / n : 0.0;
}

// ── difficulty scaling ─────────────────────────────────────────────────────
//
// A composed scene keeps the PLACE's difficulty, not the arrivals'. The ratio is
//     (average HP the slot originally had) / (average HP the new cast has)
// and each stat moves by that ratio raised to its own exponent.
//
// The exponents are FITTED from the shipped scene.bin, not chosen: ordinary
// least squares of log(stat) on log(HP) across all 615 enemies. FF7 carries
// difficulty almost entirely in HP - a monster with 100x the HP has only about
// 4.6x the Strength (100^0.34) and 27x the EXP - so scaling everything linearly
// would turn a modest correction into a difficulty and economy break.
//
// Speed, Luck and Evade are the softest movers on purpose: the fit barely
// supports them (Speed r=0.60, Luck r=0.33) and Evade is flat in vanilla, so it
// borrows Luck's exponent rather than a measured one.
void SceneComposer::scaleSceneStats(QByteArray& scene, double ratio) const
{
    struct StatScale { int offset; int width; double exponent; };
    static const StatScale kStatScaling[] = {
        { ENM_LEVEL, 1, 0.30 }, { ENM_SPEED, 1, 0.18 }, { ENM_LUCK,  1, 0.15 },
        { ENM_EVADE, 1, 0.15 }, { ENM_STR,   1, 0.34 }, { ENM_DEF,   1, 0.34 },
        { ENM_MAG,   1, 0.38 }, { ENM_MDEF,  1, 0.34 }, { ENM_MP,    2, 0.38 },
        { ENM_HP,    4, 1.00 },   // the reference the ratio is built from
        { ENM_EXP,   4, 0.69 }, { ENM_GIL,   4, 0.70 },
    };
    if (scene.size() != SCENE_SIZE) return;
    if (!(ratio > 0.0) || qFuzzyCompare(ratio, 1.0)) return;

    char* base = scene.data();
    for (int e = 0; e < ENEMIES_PER_SCENE; ++e) {
        if (!enemyPresent(scene, e)) continue;
        const int off = ENEMY_BASE + e * ENEMY_RECORD;
        for (const StatScale& st : kStatScaling) {
            const int p = off + st.offset;
            quint32 cur = 0;
            if      (st.width == 1) cur = static_cast<quint8>(scene.at(p));
            else if (st.width == 2) { quint16 v; memcpy(&v, base + p, 2); cur = v; }
            else                    { quint32 v; memcpy(&v, base + p, 4); cur = v; }
            // 0 means the designers left this stat empty; scaling invents one.
            if (cur == 0) continue;
            double scaled = static_cast<double>(cur) * std::pow(ratio, st.exponent);
            const double cap = (st.width == 1) ? 255.0
                             : (st.width == 2) ? 65535.0 : 4294967295.0;
            if (scaled < 1.0) scaled = 1.0;   // never scale a stat out of existence
            if (scaled > cap) scaled = cap;
            const quint32 v32 = static_cast<quint32>(scaled + 0.5);
            if      (st.width == 1) base[p] = static_cast<char>(static_cast<quint8>(v32));
            else if (st.width == 2) { quint16 v = static_cast<quint16>(v32); memcpy(base + p, &v, 2); }
            else                    { quint32 v = v32;                       memcpy(base + p, &v, 4); }
        }
    }
}

// ── which enemies must never be separated ──────────────────────────────────
//
// Derived from the player's own file rather than shipped as a table. For each
// enemy, intersect the casts of every formation it appears in; whatever survives
// is a companion it has never been seen without. A wolf that turns up beside
// dozens of monsters intersects to nothing. A Right Arm that only ever appears
// beside Carry Armor intersects to Carry Armor.
//
// This over-captures — a unique boss with a fixed escort also intersects to its
// escort — and that is the safe direction, since those want to stay together.
QHash<quint16, int> SceneComposer::derivePartSets(const QVector<QByteArray>& scenes,
                                                  QVector<QSet<quint16>>& setsOut) const
{
    QHash<quint16, QSet<quint16>> alwaysWith;
    QSet<quint16> seen;

    for (const QByteArray& s : scenes) {
        if (s.size() != SCENE_SIZE) continue;
        for (int f = 0; f < FORMATION_COUNT; ++f) {
            QSet<quint16> cast;
            for (int p = 0; p < PLACEMENTS; ++p) {
                const quint16 m = readU16(s, FORMATIONS + f * FORMATION_STRIDE
                                             + p * PLACEMENT_SIZE);
                if (m != NULL16) cast.insert(m);
            }
            if (cast.isEmpty()) continue;
            for (quint16 m : cast) {
                if (!seen.contains(m)) {
                    seen.insert(m);
                    alwaysWith.insert(m, cast);
                } else {
                    alwaysWith[m].intersect(cast);
                }
            }
        }
    }

    // union-find over "never seen without", so a body and both its arms land in
    // one set even when only the arms name the body
    QHash<quint16, quint16> parent;
    std::function<quint16(quint16)> find = [&](quint16 x) -> quint16 {
        if (!parent.contains(x)) parent.insert(x, x);
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    auto unite = [&](quint16 a, quint16 b) {
        const quint16 ra = find(a), rb = find(b);
        if (ra != rb) parent[ra] = rb;
    };

    for (auto it = alwaysWith.constBegin(); it != alwaysWith.constEnd(); ++it) {
        find(it.key());
        for (quint16 p : it.value())
            if (p != it.key()) unite(it.key(), p);
    }

    QHash<quint16, QSet<quint16>> grouped;
    for (auto it = parent.constBegin(); it != parent.constEnd(); ++it)
        grouped[find(it.key())].insert(it.key());

    QHash<quint16, int> setOf;
    setsOut.clear();
    for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it) {
        if (it.value().size() < 2) continue;
        const int id = setsOut.size();
        setsOut.append(it.value());
        for (quint16 m : it.value()) setOf.insert(m, id);
    }
    return setOf;
}

// ── the draw pool ──────────────────────────────────────────────────────────

QVector<SceneComposer::Unit> SceneComposer::buildUnits(
        const QVector<QByteArray>& scenes, const QHash<quint16, int>& setOf,
        const QSet<int>& skipScenes, int* multiPartDropped) const
{
    QVector<Unit> units;
    if (multiPartDropped) *multiPartDropped = 0;
    for (int i = 0; i < scenes.size(); ++i) {
        if (scenes[i].size() != SCENE_SIZE) continue;
        if (skipScenes.contains(i)) continue;   // its enemies are not on offer

        QHash<int, QVector<QPair<int, Donor>>> bySet;   // set id -> (slot, donor)
        QVector<QPair<int, Donor>> singles;

        for (int e = 0; e < ENEMIES_PER_SCENE; ++e) {
            if (!enemyPresent(scenes[i], e)) continue;
            const Donor d = makeDonor(scenes, i, e);
            if (!d.valid) continue;
            const int sid = setOf.value(d.modelId, -1);
            if (sid < 0) singles.append(qMakePair(e, d));
            else         bySet[sid].append(qMakePair(e, d));
        }

        for (auto it = bySet.constBegin(); it != bySet.constEnd(); ++it) {
            if (it.value().size() < 2) {
                // the set exists, but only one member is in THIS scene, so this
                // instance coordinates with nobody here
                singles.append(it.value().first());
                continue;
            }
            // A "set" whose members all share ONE model id is not a multi-part
            // enemy — it is the same monster standing in two slots, which the
            // co-occurrence detector cannot tell from a real pair. Ordinary
            // singles, and safe to draw.
            QSet<quint16> models;
            for (const auto& pr : it.value()) models.insert(pr.second.modelId);
            if (models.size() == 1) {
                for (const auto& pr : it.value()) singles.append(pr);
                continue;
            }
            // A genuine multi-part enemy is NOT offered to other scenes.
            // Keeping the set together is necessary but not sufficient: a
            // member only reaches the field if a PLACEMENT references its slot,
            // and 721 of the 996 formations that field a combatant field
            // exactly ONE. So wherever it landed, its first member would
            // usually fight alone while its AI waited for partners that never
            // arrive — reported from play as Grosspanzer with a frozen ATB.
            //
            // Its own scene is untouched by this: the boss tier keeps Carry
            // Armor, Emerald and Ruby fighting as themselves.
            if (multiPartDropped) ++(*multiPartDropped);
        }
        for (const auto& pr : singles) {
            Unit u;
            u.coupled = false;
            u.homeSlots.append(pr.first);
            u.members.append(pr.second);
            units.append(u);
        }
    }
    return units;
}

QHash<int, quint16> SceneComposer::derivePinnedModels(
        const QVector<QByteArray>& scenes, const QSet<int>& pinFormations) const
{
    // Derived from the player's own scene.bin, never from a NAME: enemy names
    // are localised and this has to hold in every language.
    //
    // Not from the models the protected scenes have in common either - they have
    // none. Checked against the shipped file: the fourteen chocobo scenes field
    // NINE different chocobos, one per area (a Mideel chocobo is not a Grasslands
    // chocobo), so an intersection comes back empty and pins nothing.
    //
    // What does resolve, 14 of 14: protection is per FORMATION. Take the bodies
    // the protected formations field, then remove every body that any
    // UNPROTECTED formation anywhere in the file fields. An ordinary monster
    // always turns up in some unprotected fight; the protected species never
    // does, which is why it needed protecting.
    auto formationModels = [&](const QByteArray& sc, int f) {
        QSet<quint16> out;
        for (int p = 0; p < PLACEMENTS; ++p) {
            const quint16 v = readU16(sc, FORMATIONS + f * FORMATION_STRIDE
                                          + p * PLACEMENT_SIZE);
            if (v != NULL16) out.insert(v);
        }
        return out;
    };

    QSet<quint16> fieldedElsewhere;
    for (int i = 0; i < scenes.size(); ++i) {
        if (scenes[i].size() != SCENE_SIZE) continue;
        for (int f = 0; f < FORMATION_COUNT; ++f) {
            if (pinFormations.contains(i * FORMATION_COUNT + f)) continue;
            fieldedElsewhere.unite(formationModels(scenes[i], f));
        }
    }

    QHash<int, quint16> out;
    QSet<int> pinScenes;
    for (int f : pinFormations) pinScenes.insert(f / FORMATION_COUNT);
    for (int s : pinScenes) {
        if (s < 0 || s >= scenes.size() || scenes[s].size() != SCENE_SIZE) continue;
        QSet<quint16> candidates;
        for (int f = 0; f < FORMATION_COUNT; ++f) {
            if (!pinFormations.contains(s * FORMATION_COUNT + f)) continue;
            candidates.unite(formationModels(scenes[s], f));
        }
        candidates.subtract(fieldedElsewhere);
        if (candidates.size() == 1) out.insert(s, *candidates.constBegin());
    }
    return out;
}

bool SceneComposer::drawCast(const QVector<Unit>& units, QVector<Donor>& castOut,
                             bool* coupledOut, const QHash<int, Donor>& prefill)
{
    if (coupledOut) *coupledOut = false;
    if (units.isEmpty()) return false;
    if (prefill.size() >= ENEMIES_PER_SCENE) {
        // nothing left to draw: the scene is entirely pinned
        castOut.clear();
        for (int s = 0; s < ENEMIES_PER_SCENE; ++s) castOut.append(prefill.value(s));
        return true;
    }
    std::uniform_int_distribution<int> pick(0, units.size() - 1);

    for (int attempt = 0; attempt < 40; ++attempt) {
        QHash<int, Donor> filled = prefill;   // slot -> who is in it
        bool usedCoupled = false;
        for (int guard = 0; guard < 64 && filled.size() < ENEMIES_PER_SCENE; ++guard) {
            const Unit& u = units[pick(m_rng)];
            if (filled.size() + u.members.size() > ENEMIES_PER_SCENE) continue;

            if (u.coupled) {
                // a set keeps its home slots, or its AI addresses the wrong actor
                bool clash = false;
                for (int s : u.homeSlots) if (filled.contains(s)) { clash = true; break; }
                if (clash) continue;
                for (int k = 0; k < u.homeSlots.size(); ++k)
                    filled.insert(u.homeSlots[k], u.members[k]);
                usedCoupled = true;
            } else {
                QVector<int> free;
                for (int s = 0; s < ENEMIES_PER_SCENE; ++s)
                    if (!filled.contains(s)) free.append(s);
                if (free.isEmpty()) break;
                std::uniform_int_distribution<int> fp(0, free.size() - 1);
                filled.insert(free[fp(m_rng)], u.members.first());
            }
        }
        if (filled.size() != ENEMIES_PER_SCENE) continue;
        if (coupledOut) *coupledOut = usedCoupled;
        castOut.clear();
        for (int s = 0; s < ENEMIES_PER_SCENE; ++s) castOut.append(filled.value(s));
        return true;
    }
    return false;
}

// ── the composition itself ─────────────────────────────────────────────────

bool SceneComposer::composeScene(const QByteArray& dest, const QVector<Donor>& cast,
                                 bool castIsCoupled, QByteArray& out,
                                 QString* whyNot) const
{
    if (dest.size() != SCENE_SIZE || cast.size() != ENEMIES_PER_SCENE) {
        if (whyNot) *whyNot = QStringLiteral("bad input");
        return false;
    }
    out = dest;   // keeps setup, camera, formation placements and formation AI

    // ── the shared attack table: the union over all three ─────────────────
    QVector<quint16> ids;
    QHash<quint16, QByteArray> data, names;
    for (const Donor& d : cast) {
        for (quint16 id : d.attackIds) {
            if (ids.contains(id)) continue;
            ids.append(id);
            data.insert(id, d.atkData.value(id));
            names.insert(id, d.atkName.value(id));
        }
    }
    if (ids.size() > ATTACK_SLOTS) {
        if (whyNot) *whyNot = QStringLiteral("%1 attacks, table holds %2")
                                  .arg(ids.size()).arg(ATTACK_SLOTS);
        return false;
    }
    std::sort(ids.begin(), ids.end());

    const QByteArray blankData(ATK_DATA_STRIDE, '\xFF');
    const QByteArray blankName(ATK_NAME_STRIDE, '\xFF');
    for (int i = 0; i < ATTACK_SLOTS; ++i) {
        const bool live = i < ids.size();
        writeU16(out, ATK_IDS + i * 2, live ? ids[i] : NULL16);
        out.replace(ATK_DATA + i * ATK_DATA_STRIDE, ATK_DATA_STRIDE,
                    live ? data.value(ids[i]) : blankData);
        out.replace(ATK_NAMES + i * ATK_NAME_STRIDE, ATK_NAME_STRIDE,
                    live ? names.value(ids[i]) : blankName);
    }

    // ── enemy AI: concatenate the containers, each 2-byte aligned ─────────
    QByteArray blob;
    int pos = AI_OFFSET_ORIGIN;
    QVector<quint16> aiOff;
    for (const Donor& d : cast) {
        if (d.ai.isEmpty()) { aiOff.append(NULL16); continue; }
        QByteArray chunk = d.ai;
        if (chunk.size() % 2) chunk.append('\xFF');
        aiOff.append(static_cast<quint16>(pos));
        blob.append(chunk);
        pos += chunk.size();
    }
    if (blob.size() > ENM_AI_SIZE) {
        if (whyNot) *whyNot = QStringLiteral("AI %1 > %2").arg(blob.size()).arg(ENM_AI_SIZE);
        return false;
    }
    blob.append(QByteArray(ENM_AI_SIZE - blob.size(), '\xFF'));
    out.replace(ENM_AI, ENM_AI_SIZE, blob);
    for (int i = 0; i < ENEMIES_PER_SCENE; ++i)
        writeU16(out, ENM_AI_OFFSETS + i * 2, aiOff[i]);

    // ── the enemies, and the model ids the placements key on ──────────────
    QVector<quint16> oldIds, newIds;
    for (int i = 0; i < ENEMIES_PER_SCENE; ++i) {
        oldIds.append(readU16(dest, MODEL_IDS + i * 2));
        newIds.append(cast[i].modelId);
        out.replace(ENEMY_BASE + i * ENEMY_RECORD, ENEMY_RECORD, cast[i].record);
        writeU16(out, MODEL_IDS + i * 2, cast[i].modelId);
    }

    // Placements name an enemy by MODEL ID, so every reference to the enemy that
    // used to hold a slot has to become the id of the one that holds it now.
    //
    // Then SPREAD them. Vanilla formations are overwhelmingly single-species -
    // 708 of the 996 that have a combatant reference exactly one model, while
    // 690 field two or more bodies - so preserving the pattern means seven in
    // ten composed fights are one monster three times, however varied the cast
    // behind them. Redealing the bodies round-robin across the three slots is
    // what turns a varied cast into a varied FIGHT. The number of combatants
    // never changes; only which of the scene's three enemies each body draws.
    //
    // Never for a cast holding a multi-part set: those members' AI addresses
    // enemy SLOTS and expects its partners on the field, so redealing could
    // leave a body without its arms, or arms with no body.
    for (int f = 0; f < FORMATION_COUNT; ++f) {
        QVector<int> live;
        for (int p = 0; p < PLACEMENTS; ++p) {
            const int at = FORMATIONS + f * FORMATION_STRIDE + p * PLACEMENT_SIZE;
            const quint16 eid = readU16(out, at);
            if (eid == NULL16) continue;
            live.append(at);
            for (int i = 0; i < ENEMIES_PER_SCENE; ++i) {
                if (oldIds[i] != NULL16 && eid == oldIds[i]) {
                    writeU16(out, at, newIds[i]);
                    break;
                }
            }
        }
        if (castIsCoupled || live.size() < 2) continue;

        // Only deal between bodies that are genuinely interchangeable. A
        // placement carries a ROW (+8), COVER flags (+10) and 32 bits of
        // INITIAL CONDITIONS (+12) as well as its coordinates, and handing a
        // monster the conditions written for the one that used to stand there
        // is how a Gargoyle ends up hatching on someone else's schedule.
        QVector<bool> taken(live.size(), false);
        for (int a = 0; a < live.size(); ++a) {
            if (taken[a]) continue;
            QVector<int> group;
            group.append(live[a]);
            taken[a] = true;
            for (int b = a + 1; b < live.size(); ++b) {
                if (taken[b]) continue;
                bool same = true;
                for (int w = 8; w < PLACEMENT_SIZE; w += 2)
                    if (readU16(out, live[a] + w) != readU16(out, live[b] + w)) {
                        same = false;
                        break;
                    }
                if (!same) continue;
                group.append(live[b]);
                taken[b] = true;
            }
            // A lone body has nothing to swap with; it keeps what the remap gave
            // it, which is the enemy now holding its old slot.
            if (group.size() < 2) continue;
            for (int k = 0; k < group.size(); ++k)
                writeU16(out, group[k], newIds[k % ENEMIES_PER_SCENE]);
        }
    }

    if (out.size() != SCENE_SIZE) {
        if (whyNot) *whyNot = QStringLiteral("emitted %1 bytes").arg(out.size());
        return false;
    }
    return true;
}

QStringList SceneComposer::validateScene(const QByteArray& scene,
                                         const QVector<Donor>& cast) const
{
    QStringList bad;
    if (scene.size() != SCENE_SIZE) {
        bad << QStringLiteral("size %1").arg(scene.size());
        return bad;
    }

    QSet<quint16> table;
    for (int i = 0; i < ATTACK_SLOTS; ++i) {
        const quint16 id = readU16(scene, ATK_IDS + i * 2);
        if (id == NULL16) continue;
        if (table.contains(id)) bad << QStringLiteral("attack id %1 twice").arg(id);
        table.insert(id);
        if (static_cast<quint8>(scene[ATK_NAMES + i * ATK_NAME_STRIDE]) == 0xFF
            && false) { /* an unnamed but live attack is legal */ }
    }

    for (int e = 0; e < ENEMIES_PER_SCENE; ++e) {
        if (!enemyPresent(scene, e)) {
            bad << QStringLiteral("slot %1 empty after compose").arg(e);
            continue;
        }
        const QByteArray rec = scene.mid(ENEMY_BASE + e * ENEMY_RECORD, ENEMY_RECORD);
        for (quint16 id : recordAttackIds(rec)) {
            if (!table.contains(id))
                bad << QStringLiteral("%1 uses attack %2, absent from the table")
                           .arg(enemyName(scene, e)).arg(id);
        }
    }

    // AI offsets: inside the block, in order, and the bytes we put in are the
    // bytes that came back out
    int last = AI_OFFSET_ORIGIN;
    for (int e = 0; e < ENEMIES_PER_SCENE; ++e) {
        const quint16 off = readU16(scene, ENM_AI_OFFSETS + e * 2);
        if (off == NULL16) continue;
        if (off < AI_OFFSET_ORIGIN
            || off - AI_OFFSET_ORIGIN + cast[e].ai.size() > ENM_AI_SIZE) {
            bad << QStringLiteral("enemy %1 AI offset %2 outside the block").arg(e).arg(off);
            continue;
        }
        if (off < last) bad << QStringLiteral("enemy %1 AI offset goes backwards").arg(e);
        last = off;
        if (!cast[e].ai.isEmpty()) {
            const int at = ENM_AI + off - AI_OFFSET_ORIGIN;
            if (scene.mid(at, cast[e].ai.size()) != cast[e].ai)
                bad << QStringLiteral("enemy %1 AI did not survive the round trip").arg(e);
        }
    }

    // every placement points at an enemy that is actually in this scene
    QSet<quint16> live;
    for (int i = 0; i < ENEMIES_PER_SCENE; ++i)
        live.insert(readU16(scene, MODEL_IDS + i * 2));
    for (int f = 0; f < FORMATION_COUNT; ++f) {
        for (int p = 0; p < PLACEMENTS; ++p) {
            const quint16 eid = readU16(scene, FORMATIONS + f * FORMATION_STRIDE
                                                + p * PLACEMENT_SIZE);
            if (eid != NULL16 && !live.contains(eid))
                bad << QStringLiteral("formation %1 placement %2 points at model %3, "
                                      "not in this scene").arg(f).arg(p).arg(eid);
        }
    }
    return bad;
}

// ── the pass ───────────────────────────────────────────────────────────────

bool SceneComposer::compose(QVector<QByteArray>& scenes,
                            const QVector<int>& blockOf,
                            const QSet<int>& held,
                            const QSet<int>& pinFormations,
                            const QSet<int>& neverDonateFormations,
                            bool includeBosses,
                            QTextStream& log,
                            Result* result)
{
    Result r;
    if (scenes.size() != blockOf.size()) {
        log << "Composer: scene/block arrays disagree\n";
        return false;
    }

    // Every scene's difficulty, captured BEFORE anything is replaced - it is
    // both the boss test and the target the new cast is scaled toward.
    QVector<double> origAvg(scenes.size(), 0.0);
    QSet<int> bossScenes;
    for (int i = 0; i < scenes.size(); ++i) {
        origAvg[i] = sceneAvgHp(scenes[i]);
        if (origAvg[i] >= BOSS_HP_THRESHOLD) bossScenes.insert(i);
    }
    // With bosses excluded the tier is held BOTH ways: these scenes keep their
    // vanilla cast, and their enemies are not offered to anyone else's.
    const QSet<int> donorSkip = includeBosses ? QSet<int>() : bossScenes;

    // Held scenes keep their own cast, but holding a scene says nothing about
    // whether its enemies may visit other fights. For the Weapons the answer is
    // no, regardless of Include bosses - see neverDonateFormations in the header.
    QSet<quint16> neverDonateModels;
    for (int f : neverDonateFormations) {
        const int s = f / FORMATION_COUNT, slot = f % FORMATION_COUNT;
        if (s < 0 || s >= scenes.size() || scenes[s].size() != SCENE_SIZE) continue;
        for (int p = 0; p < PLACEMENTS; ++p) {
            const quint16 m = readU16(scenes[s], FORMATIONS + slot * FORMATION_STRIDE
                                                 + p * PLACEMENT_SIZE);
            if (m != NULL16) neverDonateModels.insert(m);
        }
    }

    QVector<QSet<quint16>> sets;
    const QHash<quint16, int> setOf = derivePartSets(scenes, sets);
    int multiPartDropped = 0;
    QVector<Unit> units = buildUnits(scenes, setOf, donorSkip, &multiPartDropped);
    int weaponDropped = 0;
    for (int u = units.size() - 1; u >= 0; --u) {
        bool weapon = false;
        for (const Donor& d : units[u].members)
            if (neverDonateModels.contains(d.modelId)) { weapon = true; break; }
        if (weapon) { units.remove(u); ++weaponDropped; }
    }
    r.multiPartDropped = multiPartDropped;
    r.coupledSets = sets.size();
    r.units = units.size();

    int biggest = 0;
    for (const QSet<quint16>& s : sets) biggest = qMax(biggest, s.size());
    log << "Composer: " << sets.size() << " multi-part sets (largest " << biggest
        << "), " << units.size() << " indivisible draws\n";
    log << "Composer: " << multiPartDropped << " multi-part enem(ies) kept out of "
        << "the donor pool - a member only reaches the field if a placement "
        << "references its slot, and most formations field only one" << Qt::endl;
    if (!neverDonateModels.isEmpty())
        log << "Composer: " << neverDonateModels.size() << " Weapon bod(ies) kept "
            << "out of other fights (" << weaponDropped << " donor instance(s)) - "
            << "their AI is written for their own fight, and Ultimate's flee "
            << "ends the battle\n";
    log << "Include bosses: " << (includeBosses ? "YES" : "no");
    if (!includeBosses)
        log << " - " << bossScenes.size() << " boss-tier scene(s) keep their cast "
            << "and their enemies are out of the pool";
    log << "\n";
    r.bossDonors = includeBosses ? 0 : bossScenes.size();
    if (biggest > ENEMIES_PER_SCENE) {
        // Nothing in the shipped file trips this. If a mod does, those sets can
        // never be placed, so say so rather than silently splitting them.
        log << "  WARNING: a set larger than " << ENEMIES_PER_SCENE
            << " enemies cannot be placed and will be skipped\n";
    }

    // ── the pinned scenes ────────────────────────────────────────────────
    // Protection is per-SCENE and a scene is FOUR formations, so holding a scene
    // because one of its formations can spawn a chocobo also froze the three
    // ordinary encounters that share its enemy records. That is 14 scenes and 56
    // formations of open world-map grass - reported from play as the Wutai and
    // Mideel grass still being vanilla.
    //
    // Catching needs the chocobo in the formation; it does not need the monsters
    // standing next to it to be the vanilla ones. So the chocobo keeps its slot
    // and everything in it, and the rest of the cast is composed normally.
    QHash<int, QHash<int, Donor>> pinned;    // scene -> slot -> the enemy that stays
    QSet<int> heldPlusFailedPins = held;
    QSet<int> pinScenes;
    for (int f : pinFormations) pinScenes.insert(f / FORMATION_COUNT);
    const QHash<int, quint16> pinnedModelOf = derivePinnedModels(scenes, pinFormations);
    for (int s : pinScenes) {
        if (s < 0 || s >= scenes.size() || scenes[s].size() != SCENE_SIZE) continue;
        // NOT named `slots`: Qt defines that as a keyword macro expanding to
        // nothing, so the declaration would vanish and every use of it becomes a
        // syntax error at the dot.
        QHash<int, Donor> pinSlots;
        bool ok = pinnedModelOf.contains(s);
        if (ok) {
            const quint16 model = pinnedModelOf.value(s);
            for (int e = 0; e < ENEMIES_PER_SCENE; ++e) {
                if (!enemyPresent(scenes[s], e)) continue;
                if (readU16(scenes[s], MODEL_IDS + e * 2) != model) continue;
                const Donor d = makeDonor(scenes, s, e);
                if (!d.valid) { ok = false; break; }
                pinSlots.insert(e, d);
            }
        }
        // Could not name the body, or could not lift it intact: hold the scene
        // whole, which is the behaviour this replaces and never worse than it.
        if (!ok || pinSlots.isEmpty()) {
            heldPlusFailedPins.insert(s);
            ++r.pinnedHeld;
            continue;
        }
        pinned.insert(s, pinSlots);
    }

    // what each scene costs today, and what each block is carrying
    QVector<int> size(scenes.size(), 0);
    QHash<int, int> used;
    for (int i = 0; i < scenes.size(); ++i) {
        if (scenes[i].size() != SCENE_SIZE) continue;
        size[i] = m_sizeOf(scenes[i]);
        used[blockOf[i]] += size[i];
    }
    const int budget = BLOCK_SIZE - BLOCK_HEADER;

    QVector<int> candidates;
    for (int i = 0; i < scenes.size(); ++i) {
        if (scenes[i].size() != SCENE_SIZE || heldPlusFailedPins.contains(i)) continue;
        bool any = false;
        for (int e = 0; e < ENEMIES_PER_SCENE; ++e)
            if (enemyPresent(scenes[i], e)) { any = true; break; }
        if (!any) continue;          // an unused slot: filling it is pure growth
        if (!includeBosses && bossScenes.contains(i)) { ++r.bossHeld; continue; }
        candidates.append(i);
    }
    r.eligible = candidates.size();

    // tightest block first, so the fullest blocks get first call on their slack
    std::sort(candidates.begin(), candidates.end(), [&](int a, int b) {
        return (budget - used[blockOf[a]]) < (budget - used[blockOf[b]]);
    });

    int firstDraw = 0;
    QVector<int> noRoom;
    for (int idx : candidates) {
        const int block = blockOf[idx];
        const int slack = budget - used[block] + size[idx];
        const QHash<int, Donor> prefill = pinned.value(idx);
        bool done = false;
        for (int t = 0; t < 60 && !done; ++t) {
            QVector<Donor> cast;
            bool coupled = false;
            if (!drawCast(units, cast, &coupled, prefill)) continue;
            QByteArray built;
            QString why;
            // A pinned scene keeps its BODY LAYOUT for the same reason a
            // multi-part cast does. Re-dealing the bodies could point the
            // chocobo's placement at another monster, or hand a second body the
            // chocobo's model - either changes how many chocobos are on the
            // field, which is the one thing catching depends on.
            const bool keepLayout = coupled || !prefill.isEmpty();
            if (!composeScene(scenes[idx], cast, keepLayout, built, &why)) continue;
            if (!validateScene(built, cast).isEmpty()) continue;
            // Match the arrivals to the neighbourhood BEFORE measuring: scaling
            // rewrites stat bytes, so a size taken first is a size for a scene
            // that will not be written.
            const double came = sceneAvgHp(built);
            if (origAvg[idx] > 0.0 && came > 0.0) {
                const double ratio = origAvg[idx] / came;
                scaleSceneStats(built, ratio);
                r.worstScale = qMax(r.worstScale, qMax(ratio, 1.0 / ratio));
            }
            // The pinned enemy is restored verbatim afterwards: its stats are the
            // ones the catching code was written against, and it is only in the
            // scene's average at all because it shares the fight.
            for (auto it = prefill.constBegin(); it != prefill.constEnd(); ++it)
                built.replace(ENEMY_BASE + it.key() * ENEMY_RECORD, ENEMY_RECORD,
                              it.value().record);
            const int csz = m_sizeOf(built);
            if (csz > slack) continue;      // would not fit the block: draw again
            used[block] += csz - size[idx];
            size[idx] = csz;
            scenes[idx] = built;
            ++r.composed;
            if (!prefill.isEmpty()) ++r.pinnedComposed;
            if (t == 0) ++firstDraw;
            done = true;
        }
        if (!done) { ++r.leftVanilla; noRoom.append(idx); }
    }

    log << "Composer: rebuilt the cast of " << r.composed << " of " << r.eligible
        << " eligible scenes (" << firstDraw << " on the first draw); "
        << r.leftVanilla << " left vanilla for want of room\n";
    if (r.pinnedComposed || r.pinnedHeld)
        log << "Composer: " << r.pinnedComposed << " protected-species scene(s) "
            << "recast around the enemy they were protected for, "
            << r.pinnedHeld << " held whole because that enemy could not be "
            << "named from the data\n";
    // Name them. A player reporting "area X is still vanilla" should be
    // answerable from this file instead of a byte-diff of scene.bin.
    if (!noRoom.isEmpty()) {
        log << "Composer: no cast fit the block for scene(s):";
        for (int i : noRoom)
            log << " " << i << " (" << enemyName(scenes[i], 0) << ")";
        log << "\n";
    }
    if (!heldPlusFailedPins.isEmpty()) {
        log << "Composer: held whole:";
        for (int i : heldPlusFailedPins) {
            if (i < 0 || i >= scenes.size() || scenes[i].size() != SCENE_SIZE) continue;
            log << " " << i << " (" << enemyName(scenes[i], 0) << ")";
        }
        log << "\n";
    }

    int worst = 0;
    for (auto it = used.constBegin(); it != used.constEnd(); ++it)
        worst = qMax(worst, it.value());
    log << "Composer: fullest block now " << (worst + BLOCK_HEADER) << " of "
        << BLOCK_SIZE << " bytes\n";
    log << "Composer: scaled every rebuilt cast to the difficulty of where it "
        << "sits (largest correction " << QString::number(r.worstScale, 'f', 2)
        << "x)\n";

    if (result) *result = r;
    return true;
}
