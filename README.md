# The Crew Reborn server

English · [Deutsch](#deutsch)

The Crew (2014) on your own home server, for PS4, PS5 and PC. More at https://thecrewreborn.github.io

## Quick start

From the [release page](https://github.com/thecrewreborn/server/releases) you need:

1. **The server, for everyone:** the **Source code (zip)**. Unpack it on a computer in your home network and start
   it: `start-server.bat` on Windows (double-click), `./start-server.sh` on macOS and Linux (in a terminal).
2. **Playing on PS4 / PS5:** `TheCrewReborn-PS4-<version>.zip`. Copy `CUSA00161.xml` to
   `/data/GoldHEN/patches/xml/` on the console and switch the patch on in GoldHEN's patch menu.
3. **Playing on PC:** `TheCrewReborn-PC-<version>.zip`. Copy `dinput8.dll` and `reborn.ini` into the game folder,
   next to `TheCrew.exe`.

Start the server first, then the game. Console and PC find the server on your network by themselves.

## What you need

- Your own copy of The Crew for PS4 or PC, in one of these versions:
  - **PS4:** European version CUSA00161, version 1.00 with update 1.21 installed (the game info shows 01.21), on a
    console with GoldHEN. Tested on a PS4 with firmware 11.50.
  - **PC:** Ubisoft Connect version, `TheCrew.exe` 1.2.0.0 (build 502193). Tested on Windows 11.

  Not tested yet: the Steam version for PC and the PS5. Other PS4 regions and game versions don't work yet; the
  patch fits only CUSA00161 with update 1.21.
- A computer on the same network as the console or the gaming PC: Windows, macOS or Linux. The server can also run
  on the gaming PC itself. Linux needs Python 3.10 or newer with venv (Debian/Ubuntu:
  `sudo apt install python3 python3-venv`); on Windows and macOS the start script installs Python if it is missing.

## The server

The first start sets up the server and takes a minute (macOS asks for your password if it has to install Python).
Later starts take a few seconds. The server runs until you close the window or press Ctrl+C. Windows asks once
whether Python may use the network: allow it for private networks.

## Your progress, on PS4 and PC

Saves live in the `save` folder next to the server, one file per player name: the PS4 user name, or the PC's `Name`
in `reborn.ini` (your Windows user name if empty). Back up that folder to keep your progress.

Cross-save: the same name on PS4 and PC plays the same save. To continue your PS4 progress on the PC, set `Name` in
`reborn.ini` to your PS4 user name, spelled exactly the same; it works the other way round too.

One name plays on one device at a time. While a game is online with a name, the server refuses a second login with
that name from another device, so two games never write the same save; the game then shows error #0_35. Quit the
first game, or wait 90 seconds after it lost its connection, and log in again.

## Ports

TCP 80 and 443, UDP 4000 and 4001. Nothing has to be opened on the router for play at home. When the system refuses
port 80 or 443 (another program uses it, or Linux without root) the server takes 8080 and 8443 instead and tells the
game; it says so when it starts. If the game doesn't find the server, set `Host` in `reborn.ini` (PC) or put the
server address into `/data/TheCrew/server.txt` on the console (PS4).

## Bugs and ideas

Report them at https://github.com/thecrewreborn/server/issues (a GitHub account is needed). The `captures` folder
next to the server holds the logs; attach them, they help a lot.

## Licence

The server is free software under the GNU General Public License, version 3 or later (see `LICENSE`). The PC patch
(`dinput8.dll`) and the PS4 patch are free to use and pass on; their licence is the `LICENSE.txt` in their download.
The Crew is a trademark of Ubisoft. This is a fan project, not affiliated with or endorsed by Ubisoft or Sony; you
need your own copy of the game.

---

<a id="deutsch"></a>

# The Crew Reborn Server (Deutsch)

The Crew (2014) auf deinem eigenen Server zu Hause, für PS4, PS5 und PC. Mehr unter https://thecrewreborn.github.io

## Schnellstart

Von der [Release-Seite](https://github.com/thecrewreborn/server/releases) brauchst du:

1. **Den Server, für alle:** den **Source code (zip)**. Entpack ihn auf einem Rechner in deinem Heimnetz und starte
   ihn: `start-server.bat` unter Windows (Doppelklick), `./start-server.sh` unter macOS und Linux (im Terminal).
2. **Spielen auf PS4 / PS5:** `TheCrewReborn-PS4-<Version>.zip`. Kopier `CUSA00161.xml` auf der Konsole nach
   `/data/GoldHEN/patches/xml/` und schalte den Patch im Patch-Menü von GoldHEN ein.
3. **Spielen am PC:** `TheCrewReborn-PC-<Version>.zip`. Kopier `dinput8.dll` und `reborn.ini` in den Spielordner,
   neben `TheCrew.exe`.

Starte zuerst den Server, dann das Spiel. Konsole und PC finden den Server im Heimnetz von selbst.

## Das brauchst du

- Dein eigenes The Crew für PS4 oder PC, in einer dieser Versionen:
  - **PS4:** europäische Version CUSA00161, Version 1.00 mit installiertem Update 1.21 (die Spielinfo zeigt 01.21),
    auf einer Konsole mit GoldHEN. Getestet auf einer PS4 mit Firmware 11.50.
  - **PC:** Ubisoft-Connect-Version, `TheCrew.exe` 1.2.0.0 (Build 502193). Getestet unter Windows 11.

  Noch nicht getestet: die Steam-Version für PC und die PS5. Andere PS4-Regionen und Spielversionen gehen noch
  nicht; der Patch passt nur auf CUSA00161 mit Update 1.21.
- Einen Rechner im selben Netzwerk wie die Konsole oder der Spiele-PC: Windows, macOS oder Linux. Der Server kann
  auch auf dem Spiele-PC selbst laufen. Linux braucht Python 3.10 oder neuer mit venv (Debian/Ubuntu:
  `sudo apt install python3 python3-venv`); unter Windows und macOS installiert das Startskript Python bei Bedarf.

## Der Server

Der erste Start richtet den Server ein und dauert eine Minute (macOS fragt nach deinem Passwort, falls Python
installiert werden muss). Spätere Starts dauern ein paar Sekunden. Der Server läuft, bis du das Fenster schließt
oder Strg+C drückst. Windows fragt einmal, ob Python das Netzwerk nutzen darf: für private Netzwerke erlauben.

## Dein Fortschritt, auf PS4 und PC

Spielstände liegen im Ordner `save` neben dem Server, eine Datei pro Spielername: der PS4-Benutzername oder am PC der
`Name` in `reborn.ini` (leer = dein Windows-Benutzername). Sichere diesen Ordner, um deinen Fortschritt zu behalten.

Cross-Save: Derselbe Name auf PS4 und PC spielt denselben Spielstand. Um deinen PS4-Fortschritt am PC
weiterzuspielen, trag in `reborn.ini` bei `Name` deinen PS4-Benutzernamen ein, genau gleich geschrieben; umgekehrt
geht es genauso.

Ein Name spielt immer nur auf einem Gerät. Solange ein Spiel mit einem Namen online ist, lehnt der Server eine zweite
Anmeldung mit diesem Namen von einem anderen Gerät ab, damit nie zwei Spiele in denselben Spielstand schreiben; das
Spiel zeigt dann Fehler #0_35. Beende das erste Spiel oder warte 90 Sekunden, nachdem es die Verbindung verloren hat,
und melde dich erneut an.

## Ports

TCP 80 und 443, UDP 4000 und 4001. Für das Spielen zu Hause muss am Router nichts freigegeben werden. Wenn das
System Port 80 oder 443 verweigert (ein anderes Programm nutzt ihn, oder Linux ohne root), nimmt der Server 8080 und
8443 und sagt das dem Spiel; beim Start steht das im Fenster. Findet das Spiel den Server nicht, trag am PC `Host` in
`reborn.ini` ein oder auf der PS4 die Serveradresse in `/data/TheCrew/server.txt`.

## Fehler und Ideen

Meld sie unter https://github.com/thecrewreborn/server/issues (dafür brauchst du ein GitHub-Konto). Der Ordner
`captures` neben dem Server enthält die Logs; häng sie an, sie helfen sehr.

## Lizenz

Der Server ist freie Software unter der GNU General Public License, Version 3 oder später (siehe `LICENSE`). Der
PC-Patch (`dinput8.dll`) und der PS4-Patch dürfen kostenlos genutzt und weitergegeben werden; ihre Lizenz ist die
`LICENSE.txt` in ihrem Download. The Crew ist eine Marke von Ubisoft. Dies ist ein Fanprojekt ohne Verbindung zu
Ubisoft oder Sony; du brauchst dein eigenes Exemplar des Spiels.
