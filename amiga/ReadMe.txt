xkcd for the Amiga
==================

Shows xkcd comics (https://xkcd.com) on an Amiga 500, fetched and
converted by a FujiNet. Each comic is shown with its title and
its real caption: the "alt" text that appears when you hover over
the comic on the web site.

Requirements
------------
 - Any Amiga with Kickstart/Workbench 1.3 or later. The viewer runs
   in 512K. Zoom needs about 80 KB of chip RAM for
   the screen (the check wants about 96 KB free on PAL) plus up to
   256 KB of any RAM for the comic; on a chip-only
   512K machine it says "Not enough chip memory for Zoom". In practice
   that means a 512K trapdoor/slow-RAM expansion or a minimal startup.
 - A FujiNet running FujiNet NIO firmware with the Image
   translator (content translation type 4), connected to WiFi.
   Older NIO firmware cannot convert the comics.
 - The FujiNet NIO drivers installed and loaded, so that
   fujinet-nio.device is resident. On Workbench 1.3 run
   Install-FujiNet-WB13 from the FujiNet NIO release disk, then add
       C:fujinet-load-resident DEVS:fujinet-nio.device fujinet-nio.device
   to S:Startup-Sequence (or S:StartupII) and reboot.

Running
-------
Double-click the xkcd icon, or type xkcd in a Shell. A random
comic appears. The FujiNet turns the web image (PNG, or JPEG and
GIF for some early comics) into a picture the Amiga can show;
this takes a little while on a stock A500.

Buttons and keys
----------------
   < Previous   Left    go back (up to 25 comics)
   Zoom         Z       show the comic full screen, in grey, fitted
                        to the width; Esc returns
   Next >       Right   go forward again, or a new random comic

In Zoom, tall comics scroll: Up/Down 16 rows, Shift+Up/Down or
Space/Backspace one page, T top, B bottom, or drag with the left
mouse button. A bar at the right edge shows the position.

xkcd menu (right mouse button)
---------------------------------
   Fetch ID...       Amiga-F   show a comic by number. Type the
                               number, then OK or Return; Cancel
                               or Esc closes the window.
   Auto Refresh...   Amiga-A   show a new random comic every
                               10 to 600 seconds. Start begins,
                               Stop ends, Cancel changes nothing.
                               The time counts from when the last
                               comic finished loading.
   Quit              Amiga-Q   quit

Messages
--------
Some comics are interactive and have no picture: the caption is
shown with "No static image for this comic". A number that is not
a comic (or #404, xkcd's joke) gives "Comic #N does not exist".
"NIO driver not loaded" means fujinet-nio.device is not resident.

Credits
-------
 Comics by Randall Munroe, https://xkcd.com, licensed under
 Creative Commons Attribution-NonCommercial 2.5.
 Part of the FujiNet xkcd project.
