#pragma once

#include <QSet>
#include <cstdint>

// ============================================================================
// Formations the enemy randomizer must NEVER shuffle.
//
// GENERATED - do not hand-edit. Regenerate with scratchpad/gen_protected.py
// after any change to the boss-chest table or the Weapon detection ids.
//
// Why this exists: the Archipelago client identifies boss kills BY FORMATION
// ID. Move what lives at one of these ids and the corresponding check, or the
// goal, silently stops firing - the fight still happens, it just is not the
// fight the client is watching for. That failure is invisible until someone
// finishes a run and the goal never completes, so the exclusion list is a
// PREREQUISITE for switching the shuffle on, not a refinement of it.
//
// Five sources, unioned:
//
//   field       Referenced by a field-script BATTLE opcode. Enumerated by
//               walking every entity script in all 753 flevel entries from its
//               real entry points, using this project's opcode-length table -
//               0x70 is a common data byte, so a byte scan invents formations.
//               These are scripted story fights: the script assumes a specific
//               encounter and often what it drops or where it leaves you.
//   weapon      Ruby, Emerald and the whole Ultimate chase. The client latches
//               these kills off the live formation id (_WEAPON_BATTLE_FORMATIONS
//               and the 0xF38 chase stage), because Free Roam skips the
//               post-battle world scripting that would normally set their flags.
//   boss-chest  The Boss in a Box chests, which inject a BATTLE at these ids.
//   chocobo     Any formation containing a chocobo. Shuffling these breaks
//               catching, and with it the whole racing feature.
//   arena       Battle Square. It draws from its own table and a substitution
//               there ranges from absurd to unwinnable.
//
// Names are from the vendored ff7Battles.ts reference; [field] shows the first
// field that starts it, with a +N count when several do.
// ============================================================================

namespace EnemyProtectedFormations {

// What each entry is FOR. The composer needs this because it does not move
// formation ids, so the sources stop being equally load-bearing:
//
//   Chocobo    CORRECTNESS - catching needs a chocobo in the formation, and
//              catching is how chocobo racing works.
//   Weapon     content - the client latches the kill off the live formation id,
//              which never moves here, but you should still fight the Weapon.
//   Arena      content - Battle Square draws from its own table.
//   BossChest  content - a Boss in a Box chest should open onto its boss.
//   Field      a scripted story fight. For a pass that MOVES scenes this was
//              essential. For one that only changes a scene's cast it buys
//              nothing in correctness: the id does not move, so the check fires
//              either way. See EnemyRandomizer::composeEncounters for what is
//              done with that.
enum class Kind { Chocobo, Weapon, Arena, BossChest, Field };

static const quint16 kProtected[] = {
        56,  // chocobo          Mandragora(x2)/Chocobo [Grasslands]
        57,  // chocobo          Lavrikon/Mandragora/Chocobo [Grasslands]
        60,  // chocobo          Lavrikon(x2)/Chocobo [Grasslands]
        61,  // chocobo          Elfadunk(x2)/Chocobo [Grasslands]
        78,  // chocobo          Capparwire(x3)/Chocobo [Grasslands]
        79,  // chocobo          Capparwire(x2)/Chocobo [Grasslands]
        80,  // chocobo          Capparwire(x2)/Chocobo [Grasslands]
        81,  // chocobo          Capparwire(x2)/Chocobo [Grasslands]
        95,  // field+arena      Spencer(x2) [Battle Arena]  [clsin2_1]
        98,  // chocobo          Flapbeat(x2)/Chocobo [Grasslands]
        99,  // chocobo          Spencer(x2)/Chocobo [Grasslands]
       100,  // field            Joker(x2) [Grasslands]  [clsin2_1]
       104,  // chocobo          Flapbeat(x2)/Chocobo [Grasslands]
       105,  // chocobo          Harpy/Chocobo [Grasslands]
       107,  // field            Flapbeat(x4) [Desert]  [clsin2_1]
       117,  // field+arena      Flapbeat [Battle Arena]  [clsin2_1]
       118,  // field+arena      Flapbeat(x2) [Battle Arena]  [clsin2_1]
       119,  // field+arena      Spencer(x3)/Flapbeat [Battle Arena]  [clsin2_1]
       152,  // chocobo          Valron(x2)/Chocobo [Grasslands]
       153,  // chocobo          Kyuvilduns(x2)/Chocobo [Grasslands]
       156,  // chocobo          Kyuvilduns/Velcher Task/Chocobo [Grasslands]
       157,  // chocobo          Velcher Task(x2)/Chocobo [Grasslands]
       162,  // chocobo          Tail Vault(x2)/Chocobo [Mountains]
       163,  // chocobo          Razor Weed(x3)/Chocobo [Mountains]
       166,  // chocobo          Tail Vault(x3)/Chocobo [Mountains]
       167,  // chocobo          Tail Vault(x2)/Chocobo [Mountains]
       182,  // field            Dual Horn(x2) [Grasslands]  [clsin2_1]
       202,  // chocobo          Jumping/Chocobo [Icicle Area]
       203,  // chocobo          Jumping(x2)/Chocobo [Icicle Area]
       206,  // chocobo          Bandersnatch/Jumping/Chocobo [Icicle Area]
       207,  // chocobo          Bandersnatch(x2)/Chocobo [Icicle Area]
       214,  // chocobo          Spiral/Chocobo [Grasslands]
       215,  // chocobo          Spiral(x2)/Chocobo [Grasslands]
       218,  // chocobo          Head Hunter(x3)/Chocobo [Grasslands]
       219,  // chocobo          Head Hunter(x2)/Chocobo [Grasslands]
       280,  // field            Ultimate Weapon [Ancient Forest]  [itown2]
       282,  // weapon           Ultimate Weapon (chase)
       283,  // weapon           Ultimate Weapon (chase)
       284,  // weapon           Ultimate Weapon (chase)
       285,  // weapon           Ultimate Weapon (chase)
       286,  // weapon           Ultimate Weapon (chase)
       287,  // weapon           Ultimate Weapon (final)
       288,  // field            CMD. Grand Horn [Fort Condor]  [blackbg7+1]
       289,  // field            CMD. Grand Horn [Fort Condor]  [convil_2]
       290,  // field            CMD. Grand Horn [Fort Condor]  [convil_2]
       292,  // field            Dragon [Mountains, Raining]  [trackin+1]
       294,  // weapon           Ultimate Weapon (chase)
       300,  // field            MP(x2) [Midgar Reactor]  [md1stin]
       305,  // field            Mono Drive(x2) [Midgar Reactor]  [clsin2_1]
       310,  // field            Grunt(x2) [Midgar Reactor]  [clsin2_1]
       313,  // field            Grunt(x3) [Midgar Reactor]  [clsin2_1]
       324,  // field            Guard Scorpion [Midgar Reactor]  [nmkin_5+1]
       325,  // field            Guard Hound [Streets Midgar]  [clsin2_1]
       326,  // field            Guard Hound(x2) [Streets Midgar]  [clsin2_1]
       327,  // field+arena      Grashtrike [Battle Arena]  [clsin2_1]
       328,  // field            MP(x2) [Streets Midgar]  [md8brdg]
       329,  // field            MP(x3) [Streets Midgar]  [md8brdg]
       330,  // field            MP(x3) [Streets Midgar]  [md8brdg]
       331,  // field+arena      Chude Tank(x2) [Battle Arena]  [clsin2_1]
       333,  // field            Grashtrike(x3) [Midgar Tunnels]  [clsin2_1]
       360,  // arena            Blugu(x3) [Battle Arena]
       364,  // field            Air Buster [Midgar Reactor]  [southmk2]
       367,  // field            Special Combatant(x3) [Midgar Tunnels]  [tunnel_3]
       372,  // field            MP [Church]  [chrin_2]
       373,  // field            Guard Hound(x2) [Church]  [chrin_2]
       374,  // field            Guard Hound/MP [Church]  [chrin_2]
       384,  // field            Corneo's Lackey(x3) [Corneo's Mansion]  [colne_3]
       385,  // field            Corneo's Lackey(x2)/Scotch [Corneo's Mansion  [colne_3]
       386,  // field            Aps [Sewers]  [colne_b1]
       411,  // arena            Eligor [Battle Arena]
       412,  // field            Reno [Sector 7 Plate]  [pillar_3]
       415,  // field            Grenade Combatant(x2) [Shinra Building]  [blin1]
       416,  // field            Grenade Combatant(x2) [Shinra Building]  [blinele]
       417,  // field            Grenade Combatant(x3) [Shinra Building]  [blinele]
       419,  // field            Mighty Grunt [Shinra Building]  [blinele]
       420,  // field            Mighty Grunt(x2) [Shinra Building]  [blinele]
       422,  // field            Mighty Grunt(x3) [Shinra Building]  [blin59]
       423,  // field            Mighty Grunt(x2) [Shinra Building]  [blin60_1]
       424,  // field            Hammer Blaster [Shinra Building]  [blinele]
       425,  // field            Sword Dance [Shinra Building]  [blinele]
       427,  // field            Sword Dance(x2) [Shinra Building]  [blinele]
       436,  // field            Moth Slasher [Shinra Building]  [blinele]
       456,  // field            Sample H0512/Sample H0512 Opt(x3) [Shinra Bu  [blin68_2]
       460,  // field            Hundred Gunner/Heli Gunner [Shinra Building]  [eleout]
       464,  // field            Rufus/Dark Nation [Shinra Building]  [blin70_3]
       468,  // field+boss-chest Motor Ball (Boss in a Box)  [blackbg9+3]
       480,  // field+boss-chest Bottomswell (Boss in a Box)  [ujunon2]
       488,  // field            Jenova Birth [Shinra Boat]  [shpin_3]
       512,  // field            Cokatolis [North Corel]  [mtcrl_8]
       525,  // field+arena      Bullmotor [Battle Arena]  [clsin2_1]
       526,  // field+arena      Bullmotor(x2) [Battle Arena]  [clsin2_1]
       527,  // field            Dyne [Corel Prison]  [dyne]
       534,  // arena            Flower Prong [Battle Arena]
       538,  // arena            Heavy Tank [Battle Arena]
       539,  // field+boss-chest Reno & Rude (Boss in a Box)  [gonjun2]
       551,  // field            Stinger [Gi Cave]  [gidun_4]
       553,  // field            Gi Spector(x2) [Gi Cave]  [gidun_1]
       554,  // field            Gi Spector(x3) [Gi Cave]  [gidun_1]
       555,  // field            Gi Nattak/Soul Fire(x2) [Gi Cave]  [gidun_3]
       576,  // field            Lost Number [Shinra Mansion]  [sinin2_1]
       595,  // field            Materia Keeper [Mt. Nibel]  [nvdun1]
       600,  // field            Palmer [Rocket Town]  [rckt32]
       604,  // field            Razor Weed(x3) [Wutai]  [clsin2_1]
       606,  // field            Jayjujayme(x2)/Bizarre Bug [Wutai]  [clsin2_1]
       609,  // field            Foulander(x2) [Wutai]  [clsin2_1]
       610,  // field            Foulander(x3) [Wutai]  [clsin2_1]
       613,  // field            Garuda(x2) [Wutai]  [clsin2_1]
       614,  // field            Jayjujayme(x2)/Garuda [Wutai]  [clsin2_1]
       617,  // field            Garuda(x3) [Wutai]  [clsin2_1]
       619,  // field            Rapps [Wutai]  [datiao_6]
       620,  // field            Attack Squad(x2) [Wutai]  [yougan]
       621,  // field            Attack Squad(x2) [Grasslands]  [yougan3]
       622,  // field            Attack Squad(x3) [Wutai]  [uttmpin3]
       623,  // field            Gorkii [Wutai]  [tower5]
       624,  // field            Shake [Wutai]  [tower5]
       625,  // field            Chekhov [Wutai]  [tower5]
       626,  // field            Staniv [Wutai]  [tower5]
       628,  // field            Godo [Wutai]  [tower5]
       644,  // field+boss-chest Demons Gate (Boss in a Box)  [kuro_12]
       645,  // field            Toxic Frog(x2)/Jemnezmy [Temple Of Ancients]  [kuro_6]
       647,  // arena            Toxic Frog(x3)/Jemnezmy [Battle Arena]
       648,  // field            Ancient Dragon(x2) [Temple Of Ancients]  [kuro_5]
       649,  // field            8-Eye(x2) [Temple Of Ancients]  [kuro_6]
       652,  // field            Red Dragon [Temple Of Ancients]  [kuro_82]
       671,  // field            Jenova Life [Forgotten Capital]  [ancnt3]
       679,  // field            Snow [Icicle Area]  [hyou13_2]
       716,  // field            Evilhead(x4)/Icicle [Inside Gaea's Cliff]  [gaiin_5]
       717,  // field            Evilhead(x4)/Icicle [Inside Gaea's Cliff]  [gaiin_5]
       718,  // field            Evilhead(x4)/Icicle [Inside Gaea's Cliff]  [gaiin_5]
       729,  // field            Wing Wing [Northern Crater]  [woa_1+2]
       740,  // field            Schizo [Inside Gaea's Cliff]  [gaiin_6]
       743,  // field            Jenova Death [Northern Crater]  [trnad_4]
       763,  // field            Submarine Crew(x2) [Junon]  [junele1+1]
       768,  // field            Submarine Crew [Junon]  [semkin_3]
       769,  // field            Submarine Crew(x2) [Junon]  [semkin_3+1]
       770,  // field            Submarine Crew(x3) [Junon]  [semkin_3]
       771,  // field            Underwater MP(x2) [Junon]  [semkin_3]
       780,  // field            Carry Armor [Junon]  [semkin_5]
       784,  // field            Submarine Crew(x2) [Junon]  [subin_2a]
       785,  // field            Submarine Crew(x3) [Junon]  [subin_2b]
       786,  // field            Submarine Crew(x2)/Captain [Junon]  [subin_1a+1]
       807,  // field            Reno/Rude [Sunken Airship]  [qb]
       808,  // field            Gas Ducter [Coal Train]  [zcoal_2]
       809,  // field            Gas Ducter(x2) [Coal Train]  [zcoal_2]
       810,  // field            Wolfmeister [Coal Train]  [zcoal_2]
       811,  // field            Eagle Gun [Coal Train]  [zcoal_2]
       812,  // field            Attack Squad [Coal Train]  [zcoal_3]
       813,  // field            Attack Squad(x2) [North Corel]  [mtcrl_2]
       816,  // field            Attack Squad(x2) [Rocket Town]  [rcktbas1]
       817,  // field            Attack Squad(x2)/Senior Grunt [Rocket Town]  [rcktbas1]
       818,  // field            Rude/Attack Squad(x2) [Rocket Town]  [rcktbas2]
       819,  // field            Senior Grunt [Junon]  [rcktin2]
       840,  // field            Reno, Rude,/Elena [Midgar]  [tunnel_6]
       844,  // field            Proud Clod [Midgar]  [md8brdg2]
       845,  // arena            Proud Clod [Battle Arena]
       856,  // field            Hojo [All 3 forms] [Midgar]  [canon_2]
       900,  // field            Iron Man [Inside The Planet]  [las4_2+1]
       901,  // field            Dragon Zombie [Inside The Planet]  [las4_2+1]
       902,  // field            Allemagne [Inside The Planet]  [las4_2+1]
       908,  // field            Jenova Synthesis [Inside The Planet]  [las4_4]
       912,  // field            Bizarro Sephiroth [????]  [lastmap]
       916,  // field            Bizarro Sephiroth (Left) [????]  [lastmap]
       920,  // field            Bizarro Sephiroth (Right) [????]  [lastmap]
       924,  // field            Safer Sephiroth [????]  [blackbge+1]
       925,  // field            Final Sephiroth [???? w/ animated bg]  [las4_0]
       928,  // field            Bizarro Sephiroth (Right) [????]  [lastmap]
       932,  // field            Bizarro Sephiroth [????]  [lastmap]
       936,  // field            Bizarro Sephiroth (Left) [????]  [lastmap]
       947,  // field            Attack Squad(x2) [Junon]  [junbin4]
       956,  // arena            Tonberry [Battle Arena]
       960,  // field+arena      Battle Arena 1 [Battle Arena]  [clsin2_1]
       961,  // arena            Battle Arena 2 [Battle Arena]
       962,  // arena            Battle Arena 3 [Battle Arena]
       963,  // arena            Battle Arena 4 [Battle Arena]
       964,  // arena            Battle Arena 5 [Battle Arena]
       965,  // arena            Battle Arena 6 [Battle Arena]
       968,  // arena            Battle Arena 7 [Battle Arena]
       969,  // arena            Battle Arena 8 [Battle Arena]
       970,  // arena            Battle Arena 9 [Battle Arena]
       971,  // arena            Battle Arena 10 [Battle Arena]
       972,  // arena            Battle Arena 11 [Battle Arena]
       973,  // arena            Battle Arena 12 [Battle Arena]
       980,  // field            Diamond Weapon [Hills Near Midgar]  [fr_e]
       982,  // weapon           Ruby Weapon
       983,  // weapon           Ruby Weapon
       984,  // weapon           Emerald Weapon
       985,  // weapon           Emerald Weapon
       986,  // weapon           Emerald Weapon
       999,  // field            Pyramid(x5) [Black Screen]`.split("\n");  [blackbg4+2]
};

// Parallel to kProtected, same order, same length.
static const Kind kProtectedKind[] = {
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Arena,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Field,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Field,
    Kind::Arena,
    Kind::Arena,
    Kind::Arena,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Field,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Chocobo,
    Kind::Field,
    Kind::Weapon,
    Kind::Weapon,
    Kind::Weapon,
    Kind::Weapon,
    Kind::Weapon,
    Kind::Weapon,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Weapon,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Arena,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Arena,
    Kind::Field,
    Kind::Arena,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Arena,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::BossChest,
    Kind::BossChest,
    Kind::Field,
    Kind::Field,
    Kind::Arena,
    Kind::Arena,
    Kind::Field,
    Kind::Arena,
    Kind::Arena,
    Kind::BossChest,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::BossChest,
    Kind::Field,
    Kind::Arena,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Arena,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Field,
    Kind::Arena,
    Kind::Arena,
    Kind::Arena,
    Kind::Arena,
    Kind::Arena,
    Kind::Arena,
    Kind::Arena,
    Kind::Arena,
    Kind::Arena,
    Kind::Arena,
    Kind::Arena,
    Kind::Arena,
    Kind::Arena,
    Kind::Field,
    Kind::Weapon,
    Kind::Weapon,
    Kind::Weapon,
    Kind::Weapon,
    Kind::Weapon,
    Kind::Field,
};

static const int kProtectedCount = int(sizeof(kProtected) / sizeof(kProtected[0]));

inline bool isProtected(quint16 formationId)
{
    static const QSet<quint16> s(kProtected, kProtected + kProtectedCount);
    return s.contains(formationId);
}

}  // namespace EnemyProtectedFormations
