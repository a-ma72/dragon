Screen Dragon v0.2 (inspired by "ScreenDragons", 2020 Brian Shepherd)
===========================================================================

![Main App](app.png)

Tool that inserts watermarks into screen presentations to make them  
visually disruptive.

When the application is running, an overlay is placed over the entire  
screen. The layer consists of diagonal lines, (animated) logos, and  
message texts used as signatures.

The logo and signature can be resized, rotated, and positioned via  
drag & drop.

The application has two states:  

1. Layout mode  
   Captures mouse gestures and lets you change the size and position  
   of logos and signatures, as well as their transparency using the  
   mouse wheel.  
   (A green frame at the edges of the screen indicates you're in layout  
   mode.)

2. Shielding mode  
   Passes mouse gestures to the underlying applications.  
   (Keyboard shortcuts remain functional if the application has focus.)

You can toggle between the modes using the `space bar` or the  
`return key`.

On first launch, if no settings file exists, one signature and one logo  
object will be created as examples. Upon quitting the application, a  
settings file is saved in the program directory.  

The settings file contains all configuration values in JSON format and  
can also be edited manually.  

Placeable objects (signatures and logos) can be freely added to the  
"objects" list in the settings file. Supported logo image formats are  
SVG, PNG, BMP, JPG, and GIF. For GIFs, only the first frame is shown by  
default. To enable animation, set the object's "type" in the settings  
file from "Image" to "AnimatedGif".  

Key mappings
------------
```
Keyboard shortcuts (application must have focus):
---------------------------------------------------------------------------
Space / Return                    - Toggle between layout and 
                                    shielding mode
0..5                              - Adjust diagonal line width (0 = off)
R G B S K W (layout mode)         - Change color of diagonal lines, 
                                    signature, or logo
D                                 - Toggle between dashed and solid lines
H                                 - Toggle visibility of overlay ("dragon")
F (layout mode)                   - Flip logo horizontally
X                                 - Quit application (and save settings)
Arrow Left / Right                - Adjust global transparency
Mouse Wheel (layout mode)         - Adjust transparency global or for
                                    logo or signature
Shift + Mouse Wheel (layout mode) - Scale logo or signature
Ctrl + Mouse Wheel (layout mode)  - Rotate logo or signature

Win + Shift + Arrow Left/Right    - Move overlay to another screen

(To modify (scale, rotate, fade) the logo or signature, position the 
mouse cursor over the respective object.)
```

Installation
------------
Only extract the release package into one directory.  
The package contains files:
- dragon.exe
- dragon.png
- Freeman-Regular.TTF
- readme.md
- app.png

---
2025, A. Martin
