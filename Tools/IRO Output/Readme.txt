Postscriptthree's Gameplay Tweaks is a mod meant to enhance the existing game, giving the player many new ways to enjoy the game better than possible before. Most of these were things I thought would make the game more fun, and I use many of them myself whenever I play FF7. Clicking "The Works" is pretty much my default way to play the game these days.

For a lot more info about each specific mod, see the Nexus Mod page, https://www.nexusmods.com/finalfantasy7/mods/82/.

The rest of this will be an explanation of how to use the "Mod Enablers." They are settings that don't do anything on their own, but give new tools to other modders.

Mod Enablers:

Multi-Linked Slots:
This mod lets you link every materia slot on a weapon or armor to the next. In the link A-B-C, B is linked to both A and C, but A and C are not linked. To implement this in your own mod, use either of the tools "Wall Market" or "Scarlet." In Wall Market, all that matters is slots that link to the left. Every slot that has a link to the left will be treated as being linked to the slot to the left. In Scarlet, make sure to enable PS3 Tweaks in the main settings, open the kernel editor, then right click a slot and choose "double linked slot."

Morph As Well:
This mod lets you make a "Morph as well" materia, much like the "Steal as well" materia. Any time the paired command kills an enemy, they will be morphed. To implement this in your own mod, use either of the tools "Wall Market" or "Scarlet." For Wall Market, make a support materia like Steal as well, then set "Materia Type Modifier 1" to 96. For Scarlet, make sure to enable PS3 Tweaks in the main settings, open the kernel editor, make a support materia, click "Change attributes," and change Affected attribute to "Morph As Well." For either option, you'll still have to give it to the player somehow.

AP Plus:
This mod lets you make an "AP Plus" materia, which boosts the AP gain of paired materia, like AP Up in remake. If paired with two AP Plus, materia will gain 3x AP. Does not work with AP Share mod yet. Implementing this is the same as Morph As Well but with Modifier 1 as 97 instead of 96, or choosing "AP Plus" in Scarlet.

Materia Drops:
This mod lets the player obtain materia in battle through steal, morph, and drops. To implement this in your own mod, you need to use the tool "Scarlet." Make sure to enable PS3 Tweaks in the main settings, open the scene editor, choose an enemy, then change their steal/drop/morph to any materia.