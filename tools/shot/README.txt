Shot -- screenshots on Windows, the way the Mac does them
=========================================================

Run shot.exe. It sits in the tray (bottom right, by the clock) and waits for a shortcut:

  Ctrl+Shift+3        the whole screen (the display the mouse is on), saved
  Ctrl+Shift+4        drag a box to save that part of the screen
                        Space   switch to picking a window instead: click the window
                        Enter   take the same box as last time
                        Esc     cancel (so does a right-click)
  Ctrl+Shift+5        the toolbar: Entire Screen / Window / Selection, Options..., Capture
  Add Alt             Ctrl+Shift+Alt+3 or +4 copies to the clipboard instead of saving

After a save, a thumbnail floats in the bottom-right corner for a few seconds:
  click it            open the picture
  right-click it      Open / Copy / Show in Folder / Delete

Files are named like the Mac's: "Screenshot 2026-11-14 at 09.31.05.png", on your Desktop by default.

Options (the toolbar's Options..., or right-click the tray icon > Options)
  Save to             Desktop, Documents, Pictures\Screenshots, Clipboard, or any folder you pick
  Timer               none, 5 or 10 seconds (a countdown shows, then the shot is taken)
  Show Floating Thumbnail
  Remember Last Selection (Enter reuses the last box)

The tray menu also has Open Screenshots Folder, Start with Windows, and Quit.
Settings are kept in %APPDATA%\Shot\settings.ini.

If a shortcut is already used by another program, Shot says so when it starts; the tray menu always works.

For scripts:  shot.exe --screen [FILE.png]   takes the screen under the mouse, saves it, and exits.

Not done yet: dragging the thumbnail into another app, markup/editing, the shutter sound, capturing a window that
is hidden behind another one, and capturing every display at once.

Built from PARENA (github.com/emilyspringerton/PARENA, tools/shot/). The PNG writer is PARENA code
(stdlib/image/png_encode.prn, stdlib/compress/deflate.prn).
