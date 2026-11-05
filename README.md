# The Crew Reborn server

The Crew (2014) on your own home server, for PS4, PS5 and PC. More at https://thecrewreborn.github.io

## What you need

- Your own copy of The Crew: PS4 (European version 1.21, CUSA00161) or PC.
- A computer on the same network as the console or the gaming PC: Windows or macOS. The server can also run on the
  gaming PC itself.

## Start the server

- **Windows:** double-click `start-server.bat`.
- **macOS:** open Terminal in this folder and run `./start-server.sh`.

The first start installs Python if it is missing (macOS asks for your password) and sets up the server; that takes
a minute. Later starts take a few seconds. The server runs until you close the window or press Ctrl+C.

Windows asks once whether Python may use the network: allow it for private networks.

## Connect the game

- **PS4 / PS5:** load the console patch for The Crew from the release page. The console finds the server on your
  network by itself.
- **PC:** copy `dinput8.dll` and `reborn.ini` from the release page into the game folder, next to `TheCrew.exe`.
  The game finds the server on your network by itself; set `Host` in `reborn.ini` if it doesn't. Your Windows user
  name is your player name unless you set `Name` in `reborn.ini`.

## Your progress

Saves live in the `save` folder next to the server, one file per PS4 profile or PC player name. Back up that folder
to keep your progress.

## Ports

TCP 80 and 443, UDP 4000 and 4001. Nothing has to be opened on the router for play at home.
