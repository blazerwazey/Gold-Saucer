use rand::{rngs::StdRng, Rng, SeedableRng};

use crate::RandomiserSettings;

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub(crate) enum ItemRole {
    Normal,
    KeyOptional,
    KeyProgression,
    KeyDressProgression,
    KeyMiscTiaraProgression,
    KeyWigProgression,
    KeyMiscCorneoProgression,
    //KeyMergedKeyItems: Merged Items are added via Adding the Bits together
}

#[derive(Copy, Clone, Debug)]
pub(crate) struct KeyItemFlag {
    pub bank: u8,
    pub addr: u8,
    pub bit: u8,
    pub inventory_id: Option<u16>,
    pub name: &'static str,
    pub role: ItemRole,
}

#[allow(dead_code)]
pub(crate) const KEY_ITEM_FLAGS: &[KeyItemFlag] = &[
    KeyItemFlag {
        bank: 1,
        addr: 66,
        bit: 64,
        inventory_id: None,
        name: "Huge Materia (Blue)",
        role: ItemRole::KeyOptional,
        //Underwater Reactor Huge Materia
    },
    KeyItemFlag {
        bank: 1,
        addr: 66,
        bit: 16,
        inventory_id: None,
        name: "Huge Materia (Green)",
        role: ItemRole::KeyOptional,
        //Fort Condor Huge Materia
    },
    KeyItemFlag {
        bank: 1,
        addr: 66,
        bit: 128,
        inventory_id: None,
        name: "Huge Materia (Yellow)",
        role: ItemRole::KeyOptional,
        //Rocket Huge Materia
    },
    KeyItemFlag{
        bank: 1,
        addr: 66,
        bit: 32,
        inventory_id: None,
        name: "Huge Materia (Red)",
        role: ItemRole::KeyOptional,
        // Mt Corel Train Huge Materia
    },
    KeyItemFlag{
        bank: 1,
        addr: 64,
        bit: 8,
        inventory_id: None,
        name: "Wig",
        role: ItemRole::KeyWigProgression,
    },
    KeyItemFlag {
        bank: 1,
        addr: 64,
        bit: 1,
        inventory_id: None,
        name: "Cotton Dress",
        role: ItemRole::KeyDressProgression,
    },
    KeyItemFlag {
        bank: 1,
        addr: 64,
        bit: 2,
        inventory_id: None,
        name: "Satin Dress",
        role: ItemRole::KeyDressProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 64,
        bit: 4,
        inventory_id: None,
        name: "Silk Dress",
        role: ItemRole::KeyDressProgression,
    },
    KeyItemFlag {
        bank: 1,
        addr: 64,
        bit: 16,
        inventory_id: None,
        name: "Dyed Wig",
        role: ItemRole::KeyWigProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 64,
        bit: 32,
        inventory_id: None,
        name: "Blonde Wig",
        role: ItemRole::KeyWigProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 64,
        bit: 64,
        inventory_id: None,
        name: "Glass Tiara",
        role: ItemRole::KeyMiscTiaraProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 64,
        bit: 128,
        inventory_id: None,
        name: "Ruby Tiara",
        role: ItemRole::KeyMiscTiaraProgression,
    },
    KeyItemFlag {
        bank: 1,
        addr: 65,
        bit: 1,
        inventory_id: None,
        name: "Diamond Tiara",
        role: ItemRole::KeyMiscTiaraProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 65,
        bit: 2,
        inventory_id: None,
        name: "Cologne",
        role: ItemRole::KeyMiscTiaraProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 65,
        bit: 4,
        inventory_id: None,
        name: "Flower Cologne",
        role: ItemRole::KeyMiscTiaraProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 65,
        bit: 8,
        inventory_id: None,
        name: "Sexy Cologne",
        role: ItemRole::KeyMiscTiaraProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 65,
        bit: 16,
        inventory_id: None,
        name: "Members Card",
        role: ItemRole::KeyMiscCorneoProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 65,
        bit: 32,
        inventory_id: None,
        name: "Lingerie",
        role: ItemRole::KeyMiscCorneoProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 65,
        bit: 64,
        inventory_id: None,
        name: "Mystery Panties",
        role: ItemRole::KeyMiscCorneoProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 65,
        bit: 128,
        inventory_id: None,
        name: "Bikini Briefs",
        role: ItemRole::KeyMiscCorneoProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 66,
        bit: 1,
        inventory_id: None,
        name: "Pharmacy Coupons",
        role: ItemRole::KeyOptional,
    },
    KeyItemFlag{
        bank: 1,
        addr: 66,
        bit: 2,
        inventory_id: None,
        name: "Disinfectant",
        role: ItemRole::KeyMiscCorneoProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 66,
        bit: 4,
        inventory_id: None,
        name: "Deoderant",
        role: ItemRole::KeyMiscCorneoProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 66,
        bit: 8,
        inventory_id: None,
        name: "Digestive",
        role: ItemRole::KeyMiscCorneoProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 67,
        bit: 1,
        inventory_id: None,
        name: "Key to Ancients",
        role: ItemRole::KeyProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 67,
        bit: 8,
        inventory_id: None,
        name: "Lunar Harp",
        role: ItemRole::KeyProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 67,
        bit: 16,
        inventory_id: None,
        name: "Basement Key",
        role: ItemRole::KeyOptional,
    },
    KeyItemFlag{
        bank: 1,
        addr: 67,
        bit: 32,
        inventory_id: None,
        name: "Key to Sector 5",
        role: ItemRole::KeyOptional,
    },
    KeyItemFlag{
        bank: 1,
        addr: 67,
        bit: 64,
        inventory_id: None,
        name: "Keycard 60",
        role: ItemRole::KeyProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 67,
        bit: 128,
        inventory_id: None,
        name: "Keycard 62",
        role: ItemRole::KeyProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 68,
        bit: 1,
        inventory_id: None,
        name: "Keycard 65",
        role: ItemRole::KeyProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 68,
        bit: 2,
        inventory_id: None,
        name: "Keycard 66",
        role: ItemRole::KeyProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 68,
        bit: 4,
        inventory_id: None,
        name: "Keycard 68",
        role: ItemRole::KeyProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 68,
        bit: 8,
        inventory_id: None,
        name: "Midgar Part #1",
        role: ItemRole::KeyProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 68,
        bit: 16,
        inventory_id: None,
        name: "Midgar Part #2",
        role: ItemRole::KeyProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 68,
        bit: 32,
        inventory_id: None,
        name: "Midgar Part #3",
        role: ItemRole::KeyProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 68,
        bit: 64,
        inventory_id: None,
        name: "Midgar Part #4",
        role: ItemRole::KeyProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 68,
        bit: 128,
        inventory_id: None,
        name: "Midgar Part #5",
        role: ItemRole::KeyProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 69,
        bit: 1,
        inventory_id: None,
        name: "PHS",
        role: ItemRole::KeyOptional,
    },
    KeyItemFlag{
        bank: 1,
        addr: 69,
        bit: 2,
        inventory_id: None,
        name: "Gold Ticket",
        role: ItemRole::KeyOptional,
    },
    KeyItemFlag{
        bank: 1,
        addr: 69,
        bit: 4,
        inventory_id: None,
        name: "Keystone",
        role: ItemRole::KeyProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 69,
        bit: 8,
        inventory_id: None,
        name: "Leviathan Scales",
        role: ItemRole::KeyOptional,
    },
    KeyItemFlag{
        bank: 1,
        addr: 69,
        bit: 16,
        inventory_id: None,
        name: "Glacier Map",
        role: ItemRole::KeyProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 69,
        bit: 32,
        inventory_id: None,
        name: "A Coupon",
        role: ItemRole::KeyOptional,
    },
    KeyItemFlag{
        bank: 1,
        addr: 69,
        bit: 64,
        inventory_id: None,
        name: "B Coupon",
        role: ItemRole::KeyOptional,
    },
    KeyItemFlag{
        bank: 1,
        addr: 69,
        bit: 128,
        inventory_id: None,
        name: "C Coupon",
        role: ItemRole::KeyOptional,
    },
    KeyItemFlag{
        bank: 1,
        addr: 70,
        bit: 1,
        inventory_id: None,
        name: "Black Materia",
        role: ItemRole::KeyProgression,
    },
    KeyItemFlag{
        bank: 1,
        addr: 70,
        bit: 2,
        inventory_id: None,
        name: "Mythril",
        role: ItemRole::KeyOptional,
    },
    KeyItemFlag{
        bank: 1,
        addr: 70,
        bit: 4,
        inventory_id: None,
        name: "Snowboard",
        role: ItemRole::KeyProgression,
    },
];

pub(crate) fn all_key_item_flags() -> &'static [KeyItemFlag] {
    KEY_ITEM_FLAGS
}

pub(crate) fn key_item_flags_with_role(
    role: ItemRole,
) -> impl Iterator<Item = &'static KeyItemFlag> {
    KEY_ITEM_FLAGS.iter().filter(move |flag| flag.role == role)
}

pub(crate) fn key_item_groups_by_var() -> Vec<((u8, u8), Vec<&'static KeyItemFlag>)> {
    let mut groups: Vec<((u8, u8), Vec<&'static KeyItemFlag>)> = Vec::new();

    for flag in KEY_ITEM_FLAGS.iter() {
        let key = (flag.bank, flag.addr);
        if let Some((_, list)) = groups.iter_mut().find(|(k, _)| *k == key) {
            list.push(flag);
        } else {
            groups.push((key, vec![flag]));
        }
    }

    groups
}

pub const HUGE_MATERIA_BITS: [u8; 3] = [4, 5, 7];

pub fn make_huge_materia_perm(seed: u64) -> [u8; 3] {
    let mut perm = HUGE_MATERIA_BITS;
    let mut rng = StdRng::seed_from_u64(seed ^ 0x4A4A_77F7_u64);
    let len = perm.len();
    let mut i = len;
    while i > 1 {
        i -= 1;
        let j = rng.gen_range(0..=i);
        if j != i {
            perm.swap(i, j);
        }
    }
    perm
}

pub fn build_field_item_pool(settings: &RandomiserSettings) -> Vec<u16> {
    const FIELD_MAX_ITEM_ID: u16 = 0x0068;
    const ITEM_END: u16 = 0x0080;
    const WEAPON_COUNT: u16 = 0x0080;
    const ARMOR_COUNT: u16 = 0x0020;
    const ACCESSORY_COUNT: u16 = 0x0020;
    const WEAPON_END: u16 = ITEM_END + WEAPON_COUNT;
    const ARMOR_END: u16 = WEAPON_END + ARMOR_COUNT;
    const ACCESSORY_END: u16 = ARMOR_END + ACCESSORY_COUNT;

    let mut pool = Vec::new();

    for id in 0..=FIELD_MAX_ITEM_ID {
        pool.push(id);
    }

    if settings.randomize_equipment {
        for id in ITEM_END..WEAPON_END {
            pool.push(id);
        }
        for id in WEAPON_END..ARMOR_END {
            pool.push(id);
        }
        for id in ARMOR_END..ACCESSORY_END {
            pool.push(id);
        }
    }

    pool
}

pub const GUARANTEED_FIELD_ITEMS: &[u16] = &[
    0x0057, // Omnislash
    0x0058, // Catastrophe
    0x0059, // Final Heaven
    0x005A, // Great Gospel
    0x005B, // Cosmo Memory
    0x005C, // All Creation
    0x005D, // Chaos
    0x005E, // Highwind
    0x0062, // Save Crystal
    0x0066, // Desert Rose
    0x0067, // Earth Harp
    0x0068, // Guide Book
];

pub fn lookup_item_name(item_id: u16) -> &'static str {
    match item_id {
        0x0000 => "Potion",
        0x0001 => "Hi-Potion",
        0x0002 => "X-Potion",
        0x0003 => "Ether",
        0x0004 => "Turbo Ether",
        0x0005 => "Elixir",
        0x0006 => "Megalixir",
        0x0007 => "Phoenix Down",
        0x0008 => "Antidote",
        0x0009 => "Soft",
        0x000A => "Maiden's Kiss",
        0x000B => "Cornucopia",
        0x000C => "Echo Screen",
        0x000D => "Hyper",
        0x000E => "Tranquilizer",
        0x000F => "Remedy",
        0x0010 => "Smoke Bomb",
        0x0011 => "Speed Drink",
        0x0012 => "Hero Drink",
        0x0013 => "Vaccine",
        0x0014 => "Grenade",
        0x0015 => "Shrapnel",
        0x0016 => "Right Arm",
        0x0017 => "Hourglass",
        0x0018 => "Kiss of Death",
        0x0019 => "Spider Web",
        0x001A => "Dream Powder",
        0x001B => "Mute Mask",
        0x001C => "War Gong",
        0x001D => "Loco Weed",
        0x001E => "Fire Fang",
        0x001F => "Fire Veil",
        0x0020 => "Antarctic Wind",
        0x0021 => "Ice Crystal",
        0x0022 => "Bolt Plume",
        0x0023 => "Swift Bolt",
        0x0024 => "Earth Drum",
        0x0025 => "Earth Mallet",
        0x0026 => "Deadly Waste",
        0x0027 => "M-Tentacles",
        0x0028 => "Stardust",
        0x0029 => "Vampire Fang",
        0x002A => "Ghost Hand",
        0x002B => "Vagyrisk Claw",
        0x002C => "Light Curtain",
        0x002D => "Lunar Curtain",
        0x002E => "Mirror",
        0x002F => "Holy Torch",
        0x0030 => "Bird Wing",
        0x0031 => "Dragon Scales",
        0x0032 => "Impaler",
        0x0033 => "Shrivel",
        0x0034 => "Eye Drop",
        0x0035 => "Molotov",
        0x0036 => "S-Mine",
        0x0037 => "8-Inch Cannon",
        0x0038 => "Graviball",
        0x0039 => "T/S Bomb",
        0x003A => "Ink",
        0x003B => "Dazers",
        0x003C => "Dragon Fang",
        0x003D => "Cauldron",
        0x003E => "Sylkis Greens",
        0x003F => "Reagan Greens",
        0x0040 => "Mimett Greens",
        0x0041 => "Curiel Greens",
        0x0042 => "Pahsana Greens",
        0x0043 => "Tantal Greens",
        0x0044 => "Krakka Greens",
        0x0045 => "Gysahl Greens",
        0x0046 => "Tent",
        0x0047 => "Power Source",
        0x0048 => "Guard Source",
        0x0049 => "Magic Source",
        0x004A => "Mind Source",
        0x004B => "Speed Source",
        0x004C => "Luck Source",
        0x004D => "Zeio Nut",
        0x004E => "Carob Nut",
        0x004F => "Porov Nut",
        0x0050 => "Pram Nut",
        0x0051 => "Lasan Nut",
        0x0052 => "Saraha Nut",
        0x0053 => "Luchile Nut",
        0x0054 => "Pepio Nut",
        0x0055 => "Battery",
        0x0056 => "Tissue",
        0x0057 => "Omnislash",
        0x0058 => "Catastrophe",
        0x0059 => "Final Heaven",
        0x005A => "Great Gospel",
        0x005B => "Cosmo Memory",
        0x005C => "All Creation",
        0x005D => "Chaos",
        0x005E => "Highwind",
        0x005F => "1/35 Soldier",
        0x0060 => "Super Sweeper",
        0x0061 => "Masamune Blade",
        0x0062 => "Save Crystal",
        0x0063 => "Combat Diary",
        0x0064 => "Autograph",
        0x0065 => "Gambler",
        0x0066 => "Desert Rose",
        0x0067 => "Earth Harp",
        0x0068 => "Guide Book",
        _ => "?",
    }
}

fn lookup_weapon_name(index: u8) -> &'static str {
    match index {
        0x00 => "Buster Sword",
        0x01 => "Mythril Saber",
        0x02 => "Hardedge",
        0x03 => "Butterfly Edge",
        0x04 => "Enhance Sword",
        0x05 => "Organics",
        0x06 => "Crystal Sword",
        0x07 => "Force Stealer",
        0x08 => "Rune Blade",
        0x09 => "Murasame",
        0x0A => "Nail Bat",
        0x0B => "Yoshiyuki",
        0x0C => "Apocalypse",
        0x0D => "Heaven's Cloud",
        0x0E => "Ragnarok",
        0x0F => "Ultima Weapon",
        0x10 => "Leather Glove",
        0x11 => "Metal Knuckle",
        0x12 => "Mythril Claw",
        0x13 => "Grand Glove",
        0x14 => "Tiger Fang",
        0x15 => "Diamond Knuckle",
        0x16 => "Dragon Claw",
        0x17 => "Crystal Glove",
        0x18 => "Motor Drive",
        0x19 => "Platinum Fist",
        0x1A => "Kaiser Knuckle",
        0x1B => "Work Glove",
        0x1C => "Powersoul",
        0x1D => "Master Fist",
        0x1E => "God's Hand",
        0x1F => "Premium Heart",
        0x20 => "Gatling Gun",
        0x21 => "Assault Gun",
        0x22 => "Cannon Ball",
        0x23 => "Atomic Scissors",
        0x24 => "Heavy Vulcan",
        0x25 => "Chainsaw",
        0x26 => "Microlaser",
        0x27 => "A-M Cannon",
        0x28 => "W Machine Gun",
        0x29 => "Drill Arm",
        0x2A => "Solid Bazooka",
        0x2B => "Rocket Punch",
        0x2C => "Enemy Launcher",
        0x2D => "Pile Banger",
        0x2E => "Max Ray",
        0x2F => "Missing Score",
        0x30 => "Mythril Clip",
        0x31 => "Diamond Pin",
        0x32 => "Silver Barrette",
        0x33 => "Gold Barrette",
        0x34 => "Adaman Clip",
        0x35 => "Crystal Comb",
        0x36 => "Magic Comb",
        0x37 => "Plus Barrette",
        0x38 => "Centclip",
        0x39 => "Hairpin",
        0x3A => "Seraph Comb",
        0x3B => "Behemoth Horn",
        0x3C => "Spring Gun Clip",
        0x3D => "Limited Moon",
        0x3E => "Guard Stick",
        0x3F => "Mythril Rod",
        0x40 => "Full Metal Staff",
        0x41 => "Striking Staff",
        0x42 => "Prism Staff",
        0x43 => "Aurora Rod",
        0x44 => "Wizard Staff",
        0x45 => "Wizer Staff",
        0x46 => "Fairy Tale",
        0x47 => "Umbrella",
        0x48 => "Princess Guard",
        0x49 => "Spear",
        0x4A => "Slash Lance",
        0x4B => "Trident",
        0x4C => "Mast Ax",
        0x4D => "Partisan",
        0x4E => "Viper Halberd",
        0x4F => "Javelin",
        0x50 => "Grow Lance",
        0x51 => "Mop",
        0x52 => "Dragoon Lance",
        0x53 => "Scimitar",
        0x54 => "Flayer",
        0x55 => "Spirit Lance",
        0x56 => "Venus Gospel",
        0x57 => "4-point Shuriken",
        0x58 => "Boomerang",
        0x59 => "Pinwheel",
        0x5A => "Razor Ring",
        0x5B => "Hawkeye",
        0x5C => "Crystal Cross",
        0x5D => "Wind Slash",
        0x5E => "Twin Viper",
        0x5F => "Spiral Shuriken",
        0x60 => "Superball",
        0x61 => "Magic Shuriken",
        0x62 => "Rising Sun",
        0x63 => "Oritsuru",
        0x64 => "Conformer",
        0x65 => "Yellow M-Phone",
        0x66 => "Green M-Phone",
        0x67 => "Blue M-Phone",
        0x68 => "Red M-Phone",
        0x69 => "Crystal M-Phone",
        0x6A => "White M-Phone",
        0x6B => "Black M-Phone",
        0x6C => "Silver M-Phone",
        0x6D => "Trumpet Shell",
        0x6E => "Gold M-Phone",
        0x6F => "Battle Trumpet",
        0x70 => "Starlight Phone",
        0x71 => "HP Shout",
        0x72 => "Quicksilver",
        0x73 => "Shotgun",
        0x74 => "Shortbarrel",
        0x75 => "Lariat",
        0x76 => "Winchester",
        0x77 => "Peacemaker",
        0x78 => "Buntline",
        0x79 => "Long Barrel R",
        0x7A => "Silver Rifle",
        0x7B => "Sniper CR",
        0x7C => "Supershot",
        0x7D => "Outsider",
        0x7E => "Death Penalty",
        0x7F => "Masamune",
        _ => "?",
    }
}

fn lookup_armor_name(index: u8) -> &'static str {
    match index {
        0x00 => "Bronze Bangle",
        0x01 => "Iron Bangle",
        0x02 => "Titan Bangle",
        0x03 => "Mythril Armlet",
        0x04 => "Carbon Bangle",
        0x05 => "Silver Armlet",
        0x06 => "Gold Armlet",
        0x07 => "Diamond Bangle",
        0x08 => "Crystal Bangle",
        0x09 => "Platinum Bangle",
        0x0A => "Rune Armlet",
        0x0B => "Edincoat",
        0x0C => "Wizard Bracelet",
        0x0D => "Adaman Bangle",
        0x0E => "Gigas Armlet",
        0x0F => "Imperial Guard",
        0x10 => "Aegis Armlet",
        0x11 => "Fourth Bracelet",
        0x12 => "Warrior Bangle",
        0x13 => "Shinra Beta",
        0x14 => "Shinra Alpha",
        0x15 => "Four Slots",
        0x16 => "Fire Armlet",
        0x17 => "Aurora Armlet",
        0x18 => "Bolt Armlet",
        0x19 => "Dragon Armlet",
        0x1A => "Minerva Band",
        0x1B => "Escort Guard",
        0x1C => "Mystile",
        0x1D => "Ziedrich",
        0x1E => "Precious Watch",
        0x1F => "Chocobracelet",
        _ => "?",
    }
}

fn lookup_accessory_name(index: u8) -> &'static str {
    match index {
        0x00 => "Power Wrist",
        0x01 => "Protect Vest",
        0x02 => "Earring",
        0x03 => "Talisman",
        0x04 => "Choco Feather",
        0x05 => "Amulet",
        0x06 => "Champion Belt",
        0x07 => "Poison Ring",
        0x08 => "Tough Ring",
        0x09 => "Circlet",
        0x0A => "Star Pendant",
        0x0B => "Silver Glasses",
        0x0C => "Headband",
        0x0D => "Fairy Ring",
        0x0E => "Jem Ring",
        0x0F => "White Cape",
        0x10 => "Sprint Shoes",
        0x11 => "Peace Ring",
        0x12 => "Ribbon",
        0x13 => "Fire Ring",
        0x14 => "Ice Ring",
        0x15 => "Bolt Ring",
        0x16 => "Tetra Elemental",
        0x17 => "Safety Bit",
        0x18 => "Fury Ring",
        0x19 => "Curse Ring",
        0x1A => "Protect Ring",
        0x1B => "Cat's Bell",
        0x1C => "Reflect Ring",
        0x1D => "Water Ring",
        0x1E => "Sneak Glove",
        0x1F => "HypnoCrown",
        _ => "?",
    }
}

pub(crate) fn lookup_materia_name(id: u8) -> &'static str {
    match id {
        0x00 => "MP Plus",
        0x01 => "HP Plus",
        0x02 => "Speed Plus",
        0x03 => "Magic Plus",
        0x04 => "Luck Plus",
        0x05 => "EXP Plus",
        0x06 => "Gil Plus",
        0x07 => "Enemy Away",
        0x08 => "Enemy Lure",
        0x09 => "Chocobo Lure",
        0x0A => "Pre-Emptive",
        0x0B => "Long Range",
        0x0C => "Mega All",
        0x0D => "Counter Attack",
        0x0E => "Slash-All",
        0x0F => "Double Cut",
        0x10 => "Cover",
        0x11 => "Underwater",
        0x12 => "HP <-> MP",
        0x13 => "W-Magic",
        0x14 => "W-Summon",
        0x15 => "W-Item",
        0x16 => "(unused)",
        0x17 => "All",
        0x18 => "Counter",
        0x19 => "Magic Counter",
        0x1A => "MP Turbo",
        0x1B => "MP Absorb",
        0x1C => "HP Absorb",
        0x1D => "Elemental",
        0x1E => "Added Effect",
        0x1F => "Sneak Attack",
        0x20 => "Final Attack",
        0x21 => "Added Cut",
        0x22 => "Steal As Well",
        0x23 => "Quadra Magic",
        0x24 => "Steal",
        0x25 => "Sense",
        0x26 => "(unused)",
        0x27 => "Throw",
        0x28 => "Morph",
        0x29 => "Deathblow",
        0x2A => "Manipulate",
        0x2B => "Mime",
        0x2C => "Enemy Skill",
        0x2D => "(unused)",
        0x2E => "(unused)",
        0x2F => "(unused)",
        0x30 => "Master Command",
        0x31 => "Fire",
        0x32 => "Ice",
        0x33 => "Earth",
        0x34 => "Lightning",
        0x35 => "Restore",
        0x36 => "Heal",
        0x37 => "Revive",
        0x38 => "Seal",
        0x39 => "Mystify",
        0x3A => "Transform",
        0x3B => "Exit",
        0x3C => "Poison",
        0x3D => "Gravity",
        0x3E => "Barrier",
        0x3F => "(unused)",
        0x40 => "Comet",
        0x41 => "Time",
        0x42 => "(unused)",
        0x43 => "(unused)",
        0x44 => "Destruct",
        0x45 => "Contain",
        0x46 => "Full Cure",
        0x47 => "Shield",
        0x48 => "Ultima",
        0x49 => "Master Magic",
        0x4A => "Choco/Mog",
        0x4B => "Shiva",
        0x4C => "Ifrit",
        0x4D => "Titan",
        0x4E => "Ramuh",
        0x4F => "Odin",
        0x50 => "Leviathan",
        0x51 => "Bahamut",
        0x52 => "Kujata",
        0x53 => "Alexander",
        0x54 => "Phoenix",
        0x55 => "Neo Bahamut",
        0x56 => "Hades",
        0x57 => "Typoon",
        0x58 => "Bahamut ZERO",
        0x59 => "Knights of the Round",
        0x5A => "Master Summon",
        _ => "?",
    }
}

pub fn lookup_inventory_name(item_id: u16) -> &'static str {
    if item_id <= 0x0068 {
        return lookup_item_name(item_id);
    }

    if item_id >= 0x0080 && item_id < 0x0100 {
        let idx = (item_id - 0x0080) as u8;
        return lookup_weapon_name(idx);
    }

    if item_id >= 0x0100 && item_id < 0x0120 {
        let idx = (item_id - 0x0100) as u8;
        return lookup_armor_name(idx);
    }

    if item_id >= 0x0120 && item_id < 0x0140 {
        let idx = (item_id - 0x0120) as u8;
        return lookup_accessory_name(idx);
    }

    "?"
}
