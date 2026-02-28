# Gold Saucer - FF7 Randomizer

A randomizer for Final Fantasy VII (Steam PC version) built with Qt 6, ff7tk, and zlib.

## Features

- ✅ **Field Pickup Randomization** - Randomizes item pickups across all field maps, with automatic text updates so the in-game message matches the new item
- 🚧 **Key Item Randomization** (WIP) - Shuffles key items into valid locations respecting a 16-sphere progression system to keep the game completable
- ✅ **Shop Randomization** - Randomizes shop inventories using hext patches, category-aware (weapon shops get weapons, materia shops get materia, etc.)
- 🔄 **Enemy Randomization** (Coming Soon) - Randomizes enemy stats, levels, HP, MP, and rewards
- ✅ **Starting Equipment Randomization** - Randomizes initial character equipment
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
   (e.g. `D:\SteamLibrary\steamapps\common\FINAL FANTASY VII`)
3. **Set** **Output Folder** (defaults to `Randomized` inside the FF7 directory)
4. **Toggle** the features you want
5. **Click** **Start Randomization**
6. **Copy** the output folder contents into your FF7 installation to play

> 💡 **Tip**: Hover over any setting in the GUI to see helpful tooltips explaining what each option does!

## ⚙️ Configuration

Settings are automatically saved/loaded from `randomizer_config.json`. Use the **Save**/**Load** buttons in the GUI or edit the JSON directly.

## 📁 Project Structure

```
GoldSaucer/
├── CMakeLists.txt              # Build configuration
├── README.md                   # This file
├── .gitignore                  # Git exclusions
└── src/
    ├── main_gui.cpp            # GUI entry point
    ├── Randomizer.cpp/.h       # Main orchestrator
    ├── Config.cpp/.h           # Settings management
    ├── EnemyRandomizer.cpp/.h  # Enemy stat randomization (Coming Soon)
    ├── EnemyDatabase.cpp/.h    # Enemy data definitions
    ├── ShopRandomizer.cpp/.h   # ff7.exe shop randomization
    ├── FieldPickupRandomizer_ff7tk.cpp/.h  # Field item randomization
    ├── StartingEquipmentRandomizer.cpp/.h  # Starting gear
    ├── MakouLgpManager.cpp/.h  # LGP archive I/O
    ├── KernelBinParser.cpp/.h  # KERNEL.BIN parser
    ├── ScriptManager.cpp/.h    # Field script utilities
    ├── FieldTextManager.cpp/.h # Field text section handling
    ├── FF7String.cpp/.h        # FF7 text encoding
    ├── TextEncoder.cpp/.h      # Item text encoding
    ├── TextReplacementConfig.cpp/.h
    ├── TextReplacementManager.cpp/.h
    ├── NameGenerator.cpp/.h
    ├── KernelBinValidator.cpp/.h
    ├── UserFeedback.cpp/.h
    ├── ulgp_lgp_writer.cpp/.h   # LGP write support
    └── GUI/
        └── SimpleMainWindow.cpp/.h  # Main application window
```

## 🐛 Debug Information

Debug logs are written to your output folder alongside the randomized game files:
- `field_randomization_debug.txt` - Field randomization details
- `shop_randomization_debug.txt` - Shop randomization details  
- `enemy_randomization_debug.txt` - Enemy stat randomization details
- `encounter_randomization_debug.txt` - Enemy encounter shuffling details

In the event of an issue, Please upload all debug files from the output folder to Github and create an issue for them

## � 7th Heaven Mod Integration

For users who prefer using the **7th Heaven Mod Manager**, you can package the randomized output as a mod:

**Import the mod in 7th Heaven** In 7th Heaven, Press the "Import Mod" button next to the mod manager list and choose the "From Folder" option. Choose the folder where your randomized files output to and press "Choose Folder" followed by OK.

You can then launch the game as normal via the "Play" button in the top left of 7th Heaven.

> 💡 **Note**: Compatability with other mods has not been tested and likely will not be verified.

## �� License

This project is provided as-is for educational and personal use.

---

> ⚠️ **Disclaimer**: This is an experimental randomizer. Always backup your original FF7 installation before using randomized files.
