# MikuMikuSwitchPlugin
A framework for injecting C/C++ code into Nintendo Switch applications/applet/sysmodules.
Forked specifically for Hatsune Miku Project Diva Megamix!
^.^

> [!NOTE]
> If you have issues, reach out to `lsmsmx` on Discord.


-------------------------------------------------------------------------------------------------------------------------

# Current Features
- Full support of mod_ prefix! Making it easy to install mods as on PC
- FT Restoration (graphic options and stuff) Use with improved shaders (tonemap improved!) from https://github.com/lsmsmx/Megamix-FT_Restoration
- Rewritten memory allocators to prevent from crashing
- 95% New Classics Port (USE ORIGINAL ASSETS FROM PC VERSION)
- DEBUG AND FREECAM
- MLAA, TAA, leaf effect restoration (Sega bugs)
- Spr_set_back Sega bug fix (rescaling bg image in pv)
- Full 2D and NoMovie modes
- Attract theater mode restoration (restores absent buttons) missing files https://gamebanana.com/mods/728835
- No Songs limit, saving scores and modules and custom items in external savedata
- Touch to mouse, touch to flicks/stars/sliders for pv game
- Keyboard mode
- Skip Logos patch
- Autoplay mode (NC supported)
- Randomizer mode partial and full (NC supported)
- Song searchbar through ImGUI
- Config file with lots of options to adjust, graphical, gameplay etc, e.g. optable toon back, no logos
- Song ID limit up to (2^32 - 2)
- Increased Limit Of Spritesets to be loaded from 4096 to 32768
- Subsurface Scattering restoration
- FXAA, forced toon removed and ft lighting in customization menu
- No Module, COS limit
- Hairstyles entries Sega bug fixed
- Opd generation, light_param and other goodies save and load through debug
- Aet effects limit increased from 83 to 256
- Huge full implementation of str_array code injection to make modules to work properly without crashing the game, since simple patches wasn't enough. Custom hairs work too!
- Increased limit of lyric entries from 150 to 1000 in pv_db
- Mod_str_array.toml support
- Challenge time for all difficulties
- Saturation patch for my friend
- AetDB fix port to prevent unnecessary memory allocations
- SpriteDrawLimit memory allocations fix port for debug mode
- Resolution scaler and Post Processing overlay
- FPS Counter Overlay
- Audio Boost
- Dimming overlay
- Bone Control
- Record/Play recorded motions
- Input Track Overlay
- State Switcher
- Free RAM overlay
- Ext Savedata backup
- Wind Control
- Hints

# Instructions
- External save file (for NC too) is located in sdmc:/MikuMikuSwitchPlugin/Save/
- mod_str_array.toml can be placed in sdmc:/MikuMikuSwitchPlugin/lang2/folder. Apparently you can also use game dlc prefixes, idea by Dandy Bleat. But with new mods support, just don't touch the location of already existing mod specific mod_str_array.toml and enjoy names
- Drop your mods in sdmc:/atmosphere/contents/TitleID/romfs/mods (just to make sure, you still have to convert usm->mp4 and rename in pv_db .usm->.mp4)
- Config file location is sdmc:/MikuMikuSwitchPlugin/config.toml
- Motion dumps in sdmc:/MikuMikuSwitchPlugin/Dump/*.txt
- Wind dumps in sdmc:/MikuMikuSwitchPlugin/Wind/*.txt
- Disable mods by putting `mods = false`


-------------------------------------------------------------------------------------------------------------------------

# Debug and Freecam
Introducing fully ported **Debug mode** and **Freecam** mod from PC to Nintendo Switch! Oh, and rendering while paused.
NOTE: Debug and freecam is totally disabled by default. You should go to global config.toml at sdmc:/MikuMikuSwitchPlugin/ and change `debug = false` to `debug = true`

Here is a list of debug and freecam features:
- Access _most_ of debug game substates
- Advanced freecam in Rhythm game, PV modes; In customization menu as well; Pausing will make game render stuff
- Use mouse to open a real time dw gui for other debug windows by right click (Ctrl + RMC alternative)
- Full usability with joycons/controller (A LOT of hotkeys, listed below)
- Physical mouse support / touch support
- Generate **osage play data** in corresponding substate! Move files from `sdmc:/atmosphere/contents/TID/romfs/ram/osage_play_data_tmp/0/` to `osage_play_data` directory (for example create a new mod, or put in any dlc folder)
- Save and load debug config files (lighting, effects, etc) inside of `sdmc:/atmosphere/contents/TID/romfs/dev_ram/`
- Quickly jump to Main Menu
- Included removed limits (SWITCH EXCLUSIVE) for stages and modules and a3d (cos limit, three digits truncations; these are specific to debug)
- Excluding any (ig?) conflicts with game and between each other making it possible to use freecam in debug


Hotkeys:

**Freecam**
- (L + R + Minus) — Enable/Disable Freecam
- (D-Pad + Up-Down-Left-Right) — Move Forward-Backward-Left-Right
- (Right stick) — Look in a desired direction judged by stick movement
- (B/X) — Move Down/Up
- (L/R) — Zoom Out/In
- (Hold_Y + L/R) — Rotate (lean) Left/Right
- (Hold_R3) — Speed boost of any camera movement
 _note_: you can still use A button to pause without menu and capture beauty

**Debug Mode**
- (L + R + Plus) — Enable/Disable Debug Mode
- (L3) — Toggle Mouse
- (Left stick) or Sensor — Mouse control
- (ZL) or One finger tap — Left Mouse Click
- (ZR) or Two fingers tap — Right Mouse Click
- (Y_Hold) — Mouse movement speed boost
- (L + R + D-Pad_Up) — Fast travel to Test Mode State
- (L + R + D-Pad_Down) — Fast travel to Data Test State
- (L + R + D-Pad_Left) — Fast travel to CS Menu (umm, instant crash tho)
- (L + R + D-Pad_Right) — Fast travel to Data Menu Switch (very useful to quickly jump to main menu)
- (ZR_Hold + ZL_Hold) for 0.5 seconds — Quick switch between in-game debug and imgui window


-------------------------------------------------------------------------------------------------------------------------

# New Classics
- Playalable port of New Classics
- Chart packs or nc song packs load the same
- Specific aet hit sounds dont work, it needs an id from mod_aet_db.bin to put instead of name
- NC Settings - go to customization menu and press ZL to change. Fully ported to ImGUI


-------------------------------------------------------------------------------------------------------------------------

# ImGUI API Integration for Song Searchbar, Dimming, Volume Boost, Input Tracker, .bss, Resolution Scale & Post Processing, FPS Counter, Hints Overlays; NC Options, Bones Control, Animation Recorder Player, State Switcher, Wind control, Savedata saving, Help, Virtual keyboard windows;
- ImGui Debug Window can be toggled by pressing "+" & "-"
- NVN functiones hooked and shader written to display ImGUI
- All windows use official nn::pl Nintendo font
- Song searchbar through ImGUI in pv_sel menu, touch to call virtual keyboard or possible to use physical, fuzzy search by name and id search
- 2D mode with double ZR tap, No movies with L3+ZR, single ZR for Autoplay, tied to pv_sel 6th internal state
- Choosing song lets you enable and configure Randomizer with X and ZL/ZR buttons
- Dynamic Resolution & Post Processing Overlay - hold L3+R3+ZR, touch to use presets and change values (R for reset to game default)
- FPS Counter Overlay - hold L3+R3+"+"
- Audio Boost - BGM/SE/BTN - hold L3+R3+R
- Dimming overlay works for any song, hold L3+R3+L
- Uses custom virtual keyboard (ENG only)
- The controls are pretty much the same as in debug, including a mouse movement speed increase by holding Y button
- To start recording, reset and freeze motion (for example using "MOTION TEST" window and selecting "STEP"), then click "Arm Recording", then get back to in-game debug motion window and start playing anim set, as it starts recording it. Its not hard to pose body on your own except of face
- You can load as many recorded anims as you want at the same time but it may break the physics lol so use it with caution. Click "Open Ultimate Motion Player" to select and play motion you recorded (you can rename motions txt files), you can delete the slot or add new one, and of course you can use slider to go between frames. Don't forget to check and uncheck the check mark
- Motion Control tab offers you a variety of stuff but mostly it's: select a current bone use sliders to rotate and move, there are no specific ways to tell what each index does, it's basically hooking bone matrix update func, but at least i had enough courage to include some useful information about bones
- Toggling Input Overlay will open a new imgui window that is tied to centered bottom of screen, no user interaction, slight transparency. Tracks all your inputs. Useful for people who records videos. Will look like Nintendo Switch Grip for Joycons. It will use keyboard overlay after you press F10 or L3+R3 second time
- Ext Savedata backup within debug menu
- Wind Control window with fully customizable control params and saving option
- Mouth Focus Switch by holding ZR + ZL buttons for 0.5 seconds for quick switch between in-game debug and imgui window
- Change Substates within the menu
- .bss free RAM overlay with update button, enable with holding L3+R3+ZL. Update with L3 while in menu
- New Classics window toggled with ZL in customization menu
- Hints - help window and badges


-------------------------------------------------------------------------------------------------------------------------

# Memory Allocation improvement
- Sega set up 900mb pool for any data above 128kb and 384mb pool for less than 128kb
- With their default management, heavy songs with mods dbs loaded, empties out one of pools causing OOM crash
- Fixed by managing to use both pools for any data, saving game from crash
- spr_ps4_cmn being prevented from loading, saving 100mb from GPU pool, -ft_ui is unusable anymore ig lol
- Also fixed stupid SEGA tex_db and obj_db allocation which takes 100+ mbs of RAM for sorting O(MaxID) on PC mods with 7 digit ids
_remember to optimise mods, and keep in mind that menus take several hundreds mbs on the boot_


-------------------------------------------------------------------------------------------------------------------------

# Other info:
- If youre from older version, remember to move files from DMLSwitchPort to MikuMikuSwitchPlugin
- Delete subsdk8, it was outdated, and suddenly turned out to cause framerate drop, everything is within subsdk9 now
- Config lets you set many things, all explained
- You can totally disable debug and freecam by having `debug = false` in global config file. This way, none of hotkeys and state selectors can be used
- Debug may crash randomly when you select something. From my experience, the same random crashes happen in MM+
- Aet_DT is known to crash, even tho it works fine in MM+. It seems to fail at `strlen and cmp` or something. No solution was found, even after disabling half of plugin features and all mods even LayerFS ones
- Forced Japanese option came with broken pause menu, because of Sega putting whole global spr_db.bin in dlc18 that made it break jp interface slightly. But i got it fixed by redirection in code
- Make sure to not overload the game with mods, game isnt optimized well for ns
- Newly investigated, auth_db allocates memory strongly up to max slot so there is a script available to change numbers in auth db and stage db, USE IT! put it net to stage_data.bin and auth3d_db folder
- Heavy modules arent that heavy if you just use another script that lowers 4K textures to 1024px. Put objset folder next to the scriptl, it will generate folder with optimized .obj files
- args.txt explained https://gamebanana.com/threads/229175


-------------------------------------------------------------------------------------------------------------------------

# KEYBOARD MODE
- Notes / D-Pad: W, A, S, D / Arrows
- Arcade Buttons: I, J, K, L
- Left Stick: Q, E (Left / Right)
- Right Stick: U, O (Left / Right)
- Triggers: LeftShift/X (L), RightShift/M (R), LeftCtrl/Z/CapsLock (ZL), RightCtrl/,/;/Spacebar (ZR)
- Menus: Esc/Enter/P (+), Tab (-), BackSpace (B)


-------------------------------------------------------------------------------------------------------------------------

# TitleID to be used
const char *possible_tids[] = {

       - "0100F3100DA46000", // JP (Mega39s)
       - "01001CC00FA1A000", // EN (MegaMix)
       - "0100BE300FF62000", // KR (Mega39s)
};

# Credit
- Atmosphère: A great reference and guide.
- oss-rtld: Included for (pending) interop with rtld in applications (License [here](https://github.com/shadowninja108/exlaunch/blob/main/source/lib/reloc/rtld/LICENSE.txt)).
- DML https://github.com/blueskythlikesclouds/DivaModLoader/tree/master/Source/DivaModLoader
- All that debug stuff is inspired by original deck window from MM+ Debug mod by `nastys`
- Freecam code by vixen https://github.com/vixen256/camera
- Challenge Time by vixen https://github.com/vixen256/challenge
- Render while paused by mokk244
- Big thanks to https://github.com/god-jester/d3hack/tree/main for nvn hooking
- Bone Control is a part of DivaImGUI i ported by lybxlpsv https://github.com/lybxlpsv/divaimgui thx to him<3
- New Classics by https://github.com/mrcloverthecoder/nc
- Several patches for graphics thx to https://github.com/PDModdingCommunity/PD-Loader
- Big thanks to ReDIVA reverse engineering https://github.com/korenkonder/ReDIVA
- SpriteDrawLimit by https://github.com/korenkonder/MMPlusMods/tree/master/src/SpriteDrawLimit
- AetDB fix by https://github.com/blueskythlikesclouds/DivaModLoader/pull/37
- Skip logos patch original ips by bakonpancakz
- Thanks to Dandy Bleat for help with New Classics


-------------------------------------------------------------------------------------------------------------------------

# Motion ImGUI bones info
- 0-3 looks like rotation of body slightly and mostly pelvis, moving forward and backward
- 3-6 neck and head rotation (4th best)
- 6-91 a lot of face stuff, don't recommend touching any of it, just use facemot
- 92-97 left shoulder
- 96-98 left wrist
- 98-101 left hand index finger
- 102-105 left hand smallest finger
- 105-109 left hand 4th finter
- 109-113 left hand middle finger
- 113-117 left hand thumb
- 124-129 right shoulder
- 128-130 right wrist
- 130-133 right hand index finger
- 134-137 right hand smallest finger
- 137-140 right hand 4th finger
- 141-145 right hand middle finger
- 144-148 right hand thumb
- 155-157 left arm
- 157-159 right arm
- 160-162 waist rotation
- 162-164 left leg
- 164-166 left foot
- 166-168 right leg
- 168-170 right foot
- 178-181 xyz dimensions


-------------------------------------------------------------------------------------------------------------------------

