Gold Saucer - FF7 Randomizer v1.0.0
=====================================

A randomizer for Final Fantasy VII (Steam PC version).

REQUIREMENTS
------------
- Final Fantasy VII (Steam version)
- Windows 10/11 (64-bit)
- Visual C++ Redistributable 2022
  Download: https://aka.ms/vs/17/release/vc_redist.x64.exe

HOW TO USE
----------
1. Run GoldSaucer_GUI.exe
2. Click "Browse..." and select your FF7 installation directory
   (e.g. D:\SteamLibrary\steamapps\common\FINAL FANTASY VII)
3. Set an output folder (defaults to "Randomized" inside your FF7 directory)
4. Check the features you want to randomize:
   - Field Pickup Randomization: shuffles items found in the world
   - Key Item Randomization (Experimental): moves key items to new locations
   - Shop Randomization: randomizes shop inventories
   - Enemy Randomization: randomizes enemy stats and rewards
   - Starting Equipment Randomization: randomizes starting gear
5. Click "Start Randomization"
6. Copy the output folder contents over your FF7 installation to play

Your original FF7 files are NEVER modified. Everything goes to the output folder.

NOTES
-----
- You can use "Save Config" / "Load Config" to save your settings
- The random seed controls reproducibility: same seed = same randomization
- Debug log files are generated in the output folder for troubleshooting
- Key Item Randomization is experimental and may affect game progression

TROUBLESHOOTING
---------------
- If the program won't start, install the VC++ Redistributable linked above
- If randomization fails, check that your FF7 path points to the correct directory
  (it should contain a "data" folder and ff7_en.exe)
- Check the output folder for debug log files if something goes wrong

LICENSE
-------
This project is provided as-is for educational and personal use.
