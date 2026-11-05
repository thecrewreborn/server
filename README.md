# The Crew Reborn server

The Crew (2014) on your own home server, for PS4, PS5 and PC. More at https://thecrewreborn.github.io

## What you need

- Your own copy of The Crew: PS4 (European version 1.21, CUSA00161) or PC.
- A computer on the same network as the console or the gaming PC: Windows, macOS or Linux. The server can also run
  on the gaming PC itself.

## Start the server

- **Windows:** double-click `start-server.bat`.
- **macOS / Linux:** open a terminal in this folder and run `./start-server.sh`. Linux needs Python 3.10 or newer
  with venv (Debian/Ubuntu: `sudo apt install python3 python3-venv`).

The first start installs Python if it is missing (Windows; macOS asks for your password) and sets up the server;
that takes a minute. Later starts take a few seconds. The server runs until you close the window or press Ctrl+C.

On the first start the server also builds a few tables (cars, rewards, summits) from your own copy of the game. It
finds a PC install in the usual places by itself; otherwise it asks for the folder: drag the game folder into the
window and press Enter. For the PS4 version, copy `app0` (and `patch0`, if the game is updated) from the console,
e.g. over GoldHEN's FTP server, and give the server the folder that holds them. You can also set `game_data` in
`config.ini`, or rebuild the tables at any time with `./start-server.sh --generate <game folder>`
(`start-server.bat --generate <game folder>` on Windows).

Windows asks once whether Python may use the network: allow it for private networks.

## Connect the game

- **PS4 / PS5:** the console needs GoldHEN. Copy `CUSA00161.xml` from the PS4 download on the release page to
  `/data/GoldHEN/patches/xml/` on the console and switch the patch on in GoldHEN's patch menu. The console finds
  the server on your network by itself.
- **PC:** copy `dinput8.dll` and `reborn.ini` from the release page into the game folder, next to `TheCrew.exe`.
  The game finds the server on your network by itself; set `Host` in `reborn.ini` if it doesn't. Your Windows user
  name is your player name unless you set `Name` in `reborn.ini`.

## Your progress

Saves live in the `save` folder next to the server, one file per PS4 profile or PC player name. Back up that folder
to keep your progress.

## Ports

TCP 80 and 443, UDP 4000 and 4001. Nothing has to be opened on the router for play at home. When the system refuses
port 80 or 443 (another program uses it, or Linux without root) the server takes 8080 and 8443 instead and tells the
game; it says so when it starts.

## Bugs and ideas

Report them at https://github.com/thecrewreborn/server/issues (a GitHub account is needed). The `captures` folder
next to the server holds the logs; attach them, they help a lot.

## Licence

The server is free software under the GNU General Public License, version 3 or later (see `LICENSE`). The PC patch
(`dinput8.dll`) and the PS4 patch are free to use and pass on; their licence is the `LICENSE.txt` in their download.
The Crew is a trademark of Ubisoft. This is a fan project, not affiliated with or endorsed by Ubisoft or Sony; you
need your own copy of the game.
