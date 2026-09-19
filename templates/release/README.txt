@DISPLAY_NAME@ @VERSION@ - unofficial native PC recompilation
====================================================================

This is a native Windows build of the Xbox 360 version of @DISPLAY_NAME@.
It contains no game data: you need your own Xbox 360 disc image (.iso).

Supported disc: @SUPPORTED_DISC@

FIRST RUN
---------
1. Install the Microsoft Visual C++ Redistributable (x64) if you don't have it:
   https://aka.ms/vs/17/release/vc_redist.x64.exe
2. Extract this zip to a folder you can write to (not Program Files).
3. Run "@DISPLAY_NAME@.exe" and choose your Xbox 360 ISO when asked. The game
   files are copied into the "game" folder next to the executable once; the
   ISO is not needed after that.

CONTROLS
--------
- Xbox, PlayStation and Switch-compatible controllers work out of the box.
- System menu (Resume, Settings, Exit Game): press View + Menu together, or Esc.

DLC
---
Put downloadable content packages in the "dlc" folder next to the executable.
They are installed the next time the game starts.

SAVES AND SETTINGS
------------------
Saves, settings and logs are stored in your user folder. Create an empty file
named portable.txt next to the executable to keep them beside it instead.

SYSTEM REQUIREMENTS
-------------------
@SYSTEM_REQUIREMENTS@

MORE
----
Known issues, supported regions and source code: @PROJECT_URL@

Not affiliated with or endorsed by @PUBLISHER@ or Microsoft. You must own the
game. See LICENSE.txt and THIRD-PARTY-NOTICES.txt.
