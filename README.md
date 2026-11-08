# The Crew Reborn server

English · [Deutsch](#deutsch)

The Crew (2014) on your own home server, for PS4, PS5 and PC. More at https://thecrewreborn.github.io

## Quick start

From the [download page](https://thecrewreborn.github.io/#download) you need:

1. **The server, for everyone:** `TheCrewReborn-Server-<version>.zip`. Unpack it on a computer in your home network and start
   it: `start-server.bat` on Windows (double-click), `./start-server.sh` on macOS and Linux (in a terminal).
2. **Playing on PS4 / PS5:** `TheCrewReborn-PS4-<version>.zip`. Copy `CUSA00161.xml` to
   `/data/GoldHEN/patches/xml/` on the console and switch the patch on in GoldHEN's patch menu.
3. **Playing on PC:** `TheCrewReborn-PC-<version>.zip`. Copy `dinput8.dll` and `reborn.ini` into the game folder,
   next to `TheCrew.exe`.

Start the server first, then the game. Console and PC find the server on your network by themselves. The PS4
remembers the server it found last: if you start the game before the server, start the server and try to log in
again in the game. The very first start needs the server running.

## What you need

- Your own copy of The Crew for PS4 or PC, in one of these versions:
  - **PS4:** European version CUSA00161, on a console with GoldHEN, either with update 1.21 installed (the game info
    shows 01.21, with Wild Run) or as the base game without any update (01.00). Tested on a PS4 with firmware 11.50.
    The base game 1.00 is new and experimental: tested from the start of the story to the first tuning shop.
  - **PC:** Ubisoft Connect or Steam version, `TheCrew.exe` 1.2.0.0 (build 502193). Tested on Windows 11, also Windows 11 on ARM
    (Parallels on a Mac with Apple silicon).

  Not tested yet: the PS5. Other PS4 regions and game versions don't work yet; the
  patch fits only CUSA00161 1.00 and 1.21.
- A computer on the same network as the console or the gaming PC: Windows, macOS or Linux. The server can also run
  on the gaming PC itself. Linux needs Python 3.10 or newer with venv (Debian/Ubuntu:
  `sudo apt install python3 python3-venv`); on Windows and macOS the start script installs Python if it is missing.

## The server

The first start sets up the server and takes a minute (macOS asks for your password if it has to install Python).
Later starts take a few seconds. The server runs until you close the window or press Ctrl+C. Windows asks once
whether Python may use the network: allow it for private networks.

## Your progress, on PS4 and PC

Saves live in the `save` folder next to the server, one file per player name: the PS4 user name, or the PC's `Name`
in `reborn.ini` (your Windows user name if empty). Back up that folder to keep your progress. The base game 1.00 has its own saves
(`save/1.00`); they are separate from those of 1.21 and the PC.

Cross-save: the same name on PS4 and PC plays the same save. To continue your PS4 progress on the PC, set `Name` in
`reborn.ini` to your PS4 user name, spelled exactly the same; it works the other way round too.

One name plays on one device at a time. While a game is online with a name, the server refuses a second login with
that name from another device, so two games never write the same save; the game then shows error #0_35. Quit the
first game, or wait 90 seconds after it lost its connection, and log in again.

## Crew Credits

Every player has 1,000,000 Crew Credits, and buying with them costs nothing: the balance stays where it is. To play
with a balance of your own, copy `config.example.ini` to `config.ini` next to the server and set under `[player]`:

```ini
crew_credits = 1000000
crew_credits_refill = off
```

A new player then starts with `crew_credits`, and every purchase takes from the balance. Restart the server after
a change.

## DLC

The DLC is off: the content of the paid add-ons (Wild Run's motorbikes and specs, Calling All Units, the car
packs) stays locked. If you own the DLC for your copy of the game, switch it on in one place,
the server. Copy `config.example.ini` to `config.ini` next to the server and set under `[player]`:

```ini
dlc = on
```

Restart the server, then the game. The server cannot check whether you own the DLC, because the official servers
are gone. It unlocks the DLC for everyone who plays on it, so switch it on only if you own it. The PS4 patch and
the PC game take the setting from the server; nothing changes on the console or the PC. With a server in another
network (`server.txt` on the PS4), the console asks that server on UDP port 4001. The base game 1.00 has no DLC.

## Ports

TCP 80 and 443, UDP 4000, 4001 and 4100 (4100: the base game 1.00). Nothing has to be opened on the router for play at home. When the system refuses
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

Von der [Download-Seite](https://thecrewreborn.github.io/#download) brauchst du:

1. **Den Server, für alle:** `TheCrewReborn-Server-<Version>.zip`. Entpacke ihn auf einem Rechner in deinem Heimnetz und
   starte ihn: `start-server.bat` unter Windows (Doppelklick), `./start-server.sh` unter macOS und Linux (im Terminal).
2. **Spielen auf PS4 / PS5:** `TheCrewReborn-PS4-<Version>.zip`. Kopiere `CUSA00161.xml` auf der Konsole nach
   `/data/GoldHEN/patches/xml/` und schalte den Patch im Patch-Menü von GoldHEN ein.
3. **Spielen am PC:** `TheCrewReborn-PC-<Version>.zip`. Kopiere `dinput8.dll` und `reborn.ini` in den Spielordner,
   neben `TheCrew.exe`.

Starte zuerst den Server, dann das Spiel. Konsole und PC finden den Server im Heimnetz von selbst. Die PS4
merkt sich den zuletzt gefundenen Server: Startest du das Spiel vor dem Server, starte den Server und melde dich
im Spiel erneut an. Beim allerersten Start muss der Server bereits laufen.

## Das brauchst du

- Dein eigenes The Crew für PS4 oder PC, in einer dieser Versionen:
  - **PS4:** europäische Version CUSA00161 auf einer Konsole mit GoldHEN, entweder mit installiertem Update 1.21 (die
    Spielinfo zeigt 01.21, mit Wild Run) oder als Basisspiel ohne Update (01.00). Getestet auf einer PS4 mit Firmware
    11.50. Das Basisspiel 1.00 ist neu und experimentell: getestet vom Beginn der Story bis zum ersten Tuning-Shop.
  - **PC:** Ubisoft-Connect- oder Steam-Version, `TheCrew.exe` 1.2.0.0 (Build 502193). Getestet unter Windows 11, auch unter Windows 11
    für ARM (Parallels auf einem Mac mit Apple-Chip).

  Noch nicht getestet: die PS5. Andere PS4-Regionen und Spielversionen werden
  noch nicht unterstützt; der Patch passt nur auf CUSA00161 1.00 und 1.21.
- Einen Rechner im selben Netzwerk wie die Konsole oder der Spiele-PC: Windows, macOS oder Linux. Der Server kann
  auch auf dem Spiele-PC selbst laufen. Linux braucht Python 3.10 oder neuer mit venv (Debian/Ubuntu:
  `sudo apt install python3 python3-venv`); unter Windows und macOS installiert das Startskript Python bei Bedarf.

## Der Server

Der erste Start richtet den Server ein und dauert eine Minute (macOS fragt nach deinem Passwort, falls Python
installiert werden muss). Spätere Starts dauern ein paar Sekunden. Der Server läuft, bis du das Fenster schließt
oder Strg+C drückst. Windows fragt einmal, ob Python das Netzwerk nutzen darf: Erlaube den Zugriff für private
Netzwerke.

## Dein Fortschritt, auf PS4 und PC

Spielstände liegen im Ordner `save` neben dem Server, eine Datei pro Spielername: der PS4-Benutzername oder am PC der
`Name` in `reborn.ini` (leer = dein Windows-Benutzername). Sichere diesen Ordner, um deinen Fortschritt zu behalten. Das Basisspiel 1.00 hat eigene
Spielstände (`save/1.00`), getrennt von denen der Version 1.21 und des PCs.

Cross-Save: Derselbe Name auf PS4 und PC spielt denselben Spielstand. Um deinen PS4-Fortschritt am PC
weiterzuspielen, trage in `reborn.ini` bei `Name` deinen PS4-Benutzernamen in exakt derselben Schreibweise ein;
umgekehrt funktioniert es ebenso.

Ein Name spielt immer nur auf einem Gerät. Solange ein Spiel mit einem Namen online ist, lehnt der Server eine zweite
Anmeldung mit diesem Namen von einem anderen Gerät ab, damit nie zwei Spiele in denselben Spielstand schreiben; das
Spiel zeigt dann Fehler #0_35. Beende das erste Spiel oder warte 90 Sekunden, nachdem es die Verbindung verloren hat,
und melde dich erneut an.

## Crew Credits

Jeder Spieler hat 1.000.000 Crew Credits, und Käufe damit kosten nichts: Der Kontostand bleibt, wo er ist. Um mit
einem eigenen Kontostand zu spielen, kopiere `config.example.ini` nach `config.ini` neben dem Server und trage unter
`[player]` ein:

```ini
crew_credits = 1000000
crew_credits_refill = off
```

Ein neuer Spieler startet dann mit `crew_credits`, und jeder Kauf zieht vom Kontostand ab. Starte den Server nach
einer Änderung neu.

## DLC

Die DLCs sind aus: Die Inhalte der kostenpflichtigen Add-ons (die Motorräder und Specs von Wild
Run, Calling All Units, die Autopakete) bleiben gesperrt. Wenn du die DLCs für deine Kopie des Spiels besitzt, schalte sie
an einer Stelle ein, im Server. Kopiere `config.example.ini` nach `config.ini` neben dem Server und trage unter
`[player]` ein:

```ini
dlc = on
```

Starte den Server neu, danach das Spiel. Der Server kann nicht prüfen, ob du die DLCs besitzt, weil es die offiziellen
Server nicht mehr gibt. Er schaltet sie für alle frei, die auf ihm spielen. Schalte sie deshalb nur ein, wenn du sie
besitzt. Der PS4-Patch und das PC-Spiel übernehmen die Einstellung vom Server; an Konsole und PC änderst du nichts.
Bei einem Server in einem anderen Netzwerk (`server.txt` auf der PS4) fragt die Konsole diesen Server über den UDP-Port
4001. Das Basisspiel 1.00 hat keine DLCs.

## Ports

TCP 80 und 443, UDP 4000, 4001 und 4100 (4100: das Basisspiel 1.00). Für das Spielen zu Hause muss am Router nichts freigegeben werden. Wenn das
System Port 80 oder 443 verweigert (ein anderes Programm nutzt ihn, oder Linux ohne root), nimmt der Server 8080 und
8443 und teilt das dem Spiel mit; beim Start steht der Hinweis im Fenster. Findet das Spiel den Server nicht, trage
am PC `Host` in `reborn.ini` ein oder auf der PS4 die Serveradresse in `/data/TheCrew/server.txt`.

## Fehler und Ideen

Melde sie bitte unter https://github.com/thecrewreborn/server/issues (dafür ist ein GitHub-Konto nötig). Der
Ordner `captures` neben dem Server enthält die Logs; hänge sie bitte an, sie helfen bei der Fehlersuche sehr.

## Lizenz

Der Server ist freie Software unter der GNU General Public License, Version 3 oder später (siehe `LICENSE`). Der
PC-Patch (`dinput8.dll`) und der PS4-Patch dürfen kostenlos genutzt und weitergegeben werden; ihre Lizenz ist die
`LICENSE.txt` in ihrem Download. The Crew ist eine Marke von Ubisoft. Dies ist ein Fanprojekt ohne Verbindung zu
Ubisoft oder Sony; du brauchst dein eigenes Exemplar des Spiels.
