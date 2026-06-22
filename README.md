# Gold Saucer - FF7 Randomizer

A randomizer for Final Fantasy VII (Steam PC version) built with Qt 6, ff7tk, and zlib. It can run standalone **or**
as the patcher for an **Archipelago** multiworld (see [Archipelago Integration](#-archipelago-integration)).

## Features

- ✅ **Field Pickup Randomization** - Randomizes item pickups across all field maps, with automatic text updates so the in-game message matches the new item
- 🚧 **Key Item Randomization** (WIP) - Shuffles key items into valid locations respecting a 16-sphere progression system to keep the game completable
- ✅ **Shop Randomization** - Randomizes shop inventories using hext patches, category-aware (weapon shops get weapons, materia shops get materia, etc.)
- ✅ **Starting Equipment Randomization** - Randomizes initial character equipment
- ✅ **Archipelago Integration** - Imports an `.apff7` seed file to place multiworld items/shops; ships `shophook.dll` for native-grid AP shop slots
- ✅ **Safe output** - All modifications go to a separate output folder; your original FF7 installation is never touched

## Requirements

### 🎮 For End Users
- **Final Fantasy VII** (Steam version)
- **Windows 10/11** (64-bit)
- **Visual C++ Redistributable 2022**

### 🔧 For Building from Source
- **CMake 3.16+**
- **Qt 6.x** (Core, Widgets, Xml, Svg, Core5Compat, Network)
- **ff7tk 1.3.x**
- **zlib 1.3.x**
- **MSVC 2022**

### 📦 Build Instructions

```bash
# Configure
cmake -B build

# Build (Release)
cmake --build build --config Release

# Output will be in build/Release/GoldSaucer_GUI.exe
```

## 🚀 Usage

1. **Run** `GoldSaucer_GUI.exe`
2. **Set** **FF7 Installation Path** to your Steam FF7 directory  
   (e.g. `C:\\Program Files\Steam\steamapps\common\FINAL FANTASY VII` for 2013 Steam release or `C:\\Program Files\Steam\steamapps\common\FINAL FANTASY VII Steam Editon` for the 2026 Steam release)
3. **Set** **Output Folder** (defaults to `Randomized` inside the FF7 directory)
4. **Toggle** the features you want
5. **Click** **Start Randomization**
6a. **Copy** the output folder contents into your FF7 installation to play or
6b. **Import the mod in 7th Heaven** In 7th Heaven, Press the "Import Mod" button next to the mod manager list and choose the "From Folder" option. Choose the folder where your randomized files output to and press "Choose Folder" followed by OK.
You can then launch the game as normal via the "Play" button in the top left of 7th Heaven.

> 💡 **Tip**: Hover over any setting in the GUI to see helpful tooltips explaining what each option does!

## 🌐 Archipelago Integration

Gold Saucer doubles as the file patcher for the **Final Fantasy VII** Archipelago world (`FF7pelago`).

Please note: Please do not adjust any settings once you have imported your .apff7 file in to Gold Saucer

1. Generate the multiworld; Archipelago writes an `AP_<seed>_P<slot>_<name>.apff7` file (a JSON payload,
   `"format": "apff7"`) for each FF7 slot.
2. In Gold Saucer, **import the `.apff7`** to enable Archipelago mode. It uses the `placements` and `shops` arrays to
   put the correct (possibly cross-player) items at each field location and to reserve shop slots as AP checks.
3. Run the randomization and load the output through **7th Heaven**.
4. Copy **`ShopHook/build/Release/shophook.dll`** next to `FF7_EN.exe` so the AP client can inject it for Tier-3
   shop slots (custom names/descriptions + purchase detection).
5. Connect with the **Final Fantasy VII Client** from the Archipelago Launcher.

The runtime side (detecting checks, granting received items, applying multipliers, shop display/detection) is handled
by the AP client and `shophook.dll` — see `ShopHook/README.md`. The `.apff7` schema is documented in the FF7pelago
`worlds/ff7/docs/multiworld_en.md`.

## ⚙️ Configuration

Settings are automatically saved/loaded from `randomizer_config.json`. Use the **Save**/**Load** buttons in the GUI or edit the JSON directly.


## 🐛 Debug Information

Debug logs are written to your output folder alongside the randomized game files:
- `field_randomization_debug.txt` - Field randomization details
- `shop_randomization_debug.txt` - Shop randomization details  
- `enemy_randomization_debug.txt` - Enemy stat randomization details
- `encounter_randomization_debug.txt` - Enemy encounter shuffling details

In the event of an issue, Please upload all debug files from the output folder to Github and create an issue for them

## 🔌 7th Heaven Mod Usage

For users using the **7th Heaven Mod Manager**, you can package the randomized output as a mod:

**Import the mod in 7th Heaven** In 7th Heaven, Press the "Import Mod" button next to the mod manager list and choose the "From Folder" option. Choose the folder where your randomized files output to and press "Choose Folder" followed by OK.

You can then launch the game as normal via the "Play" button in the top left of 7th Heaven.

> 💡 **Note**: Compatability with other mods has not been tested and likely will not be verified.

## 📜 License

This project is provided as-is for educational and personal use.

---

> ⚠️ **Disclaimer**: This is an experimental randomizer. Always backup your original FF7 installation before using randomized files.
