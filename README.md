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

## Running a public server

By default the server is for your own network: anyone who reaches it plays, and names are not protected. To open it
to other players over the internet, set `public = on` in the `[server]` section of `config.ini` (default: off;
`config.example.ini` shows the line). Home play without that setting works as before, with one rule for every
server: two games cannot play the same name at the same time. For that check names compare without case, and every
character other than A-Z, a-z, 0-9, `-` and `_` counts as `_` (`Max.Power`, `max_power` and `MAX POWER` are one
name).

**What `public = on` does**

- The first login with a name claims it. Later logins need the same credential, so nobody else can play on that
  save. The default name `Player` cannot be claimed.
- Names compare without case, and every character other than A-Z, a-z, 0-9, `-` and `_` counts as `_`:
  `Max.Power`, `Max_Power` and `max_power` are one name (at home, `Ünal` and `_nal` share one save the same way). A
  public server accepts names of at most 32 characters of plain ASCII (letters, digits, spaces and signs, no
  umlauts or accents) and refuses others.
- One IP address can claim at most 3 new names per hour. Logins to names it already claimed are not limited.
- After 5 wrong passwords from one IP address within 10 minutes, that address is refused for the rest of the window.
- At start the server lists saves nobody has claimed yet ("unclaimed saves"); anyone may claim them at their next
  login. A new player starts with a fresh save: your own single-player save (`save/1.21/player.json`) is not handed
  to the first stranger. To keep it, copy it to `save/1.21/players/<Name>.json` and claim that name first.
- The logs in `captures` leave out tickets, passwords and keys, so players may attach them to bug reports.

**What players do**

- PC: in `reborn.ini` set `Ticket = Name|Password` under `[Player]`. Everything after the first `|` is the password.
  Use a password that you use nowhere else (see "Limits").
- PS4: the PS4 patch from release 0.0.4 on creates a key for the console in `/data/TheCrew/console_key.txt` on its
  first start. Players should back this file up. Copying it to a new console keeps the name; a damaged file is kept
  as `console_key.bad`. An older PS4 patch sends no key and is refused on a public server.
- PS4: put the server's public IPv4 address into `/data/TheCrew/server.txt` (an address like `203.0.113.5`, no
  hostname). If the server uses 8080/8443 (see Ports), write `https://203.0.113.5:8443`.
- PC: set `Host` in `reborn.ini`; it takes an address or a hostname.

**Looking after names**

In the server folder (the release start script creates `.venv` there):

```
.venv/bin/python tools/accounts.py list
.venv/bin/python tools/accounts.py free NAME
```

`free` lets the next login with that name claim it again (for a player who lost the password or the key). To use
another accounts file, put `--file PATH` before `list` or `free`:
`.venv/bin/python tools/accounts.py --file PATH list`. `save/accounts.json` holds salted hashes, never passwords;
`save/<version>/sessions.json` holds the session keys. Never share the `save` folder. The `captures` folder is the
one players send.

**Settings for a public server**

In `config.ini`:

```
[server]
public = on
ip = 203.0.113.5

[world]
multiplayer = off
```

- `ip` is the address the server announces to the game, and the game connects to it. With `public = on` the server
  also puts it into the service configuration the game fetches at start. Set it to the server's public IPv4
  address (not a hostname). Without it the server announces its address in its own network, which players on the
  internet cannot reach.
- `multiplayer = off`: multiplayer over the internet does not work yet (see "Limits").
- Test the first login yourself from another network, PS4 or PC, before you invite players.

**Logs**

The `captures` folder grows while people play: about 40 KB per minute of play and player (measured). The server
has no switch to turn the logs off; `--log PATH` after `./start-server.sh` (or at the end of `ExecStart=` in the
unit) only moves them: the 1.21 relay log goes to PATH, `requests.jsonl` and `v100.jsonl` into the same folder. The
server opens each log for every entry, so you may move or delete the files at any time; it starts new ones. To keep
the folder small, delete old files now and then, or let logrotate do it with a file
`/etc/logrotate.d/thecrewreborn` (adjust the path and the user):

```
/opt/thecrewreborn/captures/*.jsonl {
    su ubuntu ubuntu
    weekly
    rotate 4
    compress
    missingok
    notifempty
}
```

**Ports**

See "Ports" above: open exactly those between the internet and the server. If the server uses 8080 and 8443
instead of 80 and 443, open those two.

**Option 1: Oracle Cloud Always Free**

Oracle's documentation lists, for Always Free, Arm A1 Flex instances with up to 2 OCPUs and 12 GB of memory in
total and 10 TB of outbound data per month. The server needs a small fraction of that (see "Load" below).

1. Create an Always Free account, then an instance: shape VM.Standard.A1.Flex (1 OCPU and 6 GB are plenty), image
   Ubuntu, a public IPv4 address, and your SSH key.
2. In the instance's subnet, open its security list and add ingress rules (source `0.0.0.0/0`): TCP 80 and 443, UDP
   4000, 4001 and 4100.
3. Open the same ports in the VM itself. Oracle's Ubuntu images are widely reported to ship iptables rules that
   reject everything except SSH (reported, not checked here). If that is so on your VM:

   ```
   sudo iptables -I INPUT 5 -p tcp --dport 80 -j ACCEPT
   sudo iptables -I INPUT 5 -p tcp --dport 443 -j ACCEPT
   sudo iptables -I INPUT 5 -p udp -m multiport --dports 4000,4001,4100 -j ACCEPT
   sudo netfilter-persistent save
   ```

   Run `sudo iptables -L INPUT -n --line-numbers` first and insert the rules above the final REJECT rule (the
   position 5 is an example).
4. Install the server: `sudo apt install python3 python3-venv unzip`, unpack the release into `/opt/thecrewreborn`
   (owned by the user that runs it; not under `/home`, which the unit hides), run `chmod +x start-server.sh`, put
   the settings above into `config.ini`, and start `./start-server.sh` once by hand to check that it comes up.
5. Install the unit: edit `User=` and the two paths in `thecrewreborn.service` if yours differ (keep the folder out
   of `/home`), then `sudo cp thecrewreborn.service /etc/systemd/system/` and
   `sudo systemctl daemon-reload && sudo systemctl enable --now thecrewreborn`.
   Logs: `journalctl -u thecrewreborn -f`. The unit grants the right to bind ports 80 and 443 without root.
6. Check from another network: `curl -k https://<public-ip>/version` must answer.

Oracle states that it may reclaim an idle Always Free instance: when for 7 days CPU use (95th percentile), network
use and memory use are all below 20 %. A game server with few players can fall below that. Upgrading the account to
Pay As You Go is the commonly reported way to avoid the reclaim, while the Always Free resources stay free (reported,
not checked here).

**Option 2: at home**

You need a public IPv4 address from your provider (not DS-Lite or CG-NAT). Give the server computer a fixed address,
then forward the ports above to it on the router. On a FritzBox: Internet, Freigaben, Portfreigaben, add a device,
one forwarding per port. If your provider changes the address, update `ip` in `config.ini`; PS4 players must update
`server.txt`, PC players may use a dynamic DNS hostname (for example MyFRITZ!) as `Host`.

**Limits**

- Multiplayer over the internet is not built yet: there is no NAT traversal, one session is shared, and at most 5
  players can be nearby. Public mode is for the solo game with saves; set `multiplayer = off` (see above).
- The games accept any server certificate, so someone on the network path between a player and the server can read
  the PC password or the console key. Players should use a password only for this server.
- Only one server can run per IP address and port set.
- Anyone who can read a player's network traffic and knows the public relay key can replay that player's login ticket
  for up to 3 hours. Only an attacker on the network path can do this.

**Load**

Measured on 2026-11-12: about 0.02 % of one CPU core per player (Apple M2 Max), 1 to 2 kbit/s on average with
peaks up to 16 kbit/s, and about 22 KB of save per player. The server process uses about 50 MB of memory.

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

## Einen öffentlichen Server betreiben

Standardmäßig ist der Server für dein eigenes Netzwerk gedacht: Jeder, der ihn erreicht, spielt mit, Namen sind nicht
geschützt. Damit auch andere Spieler über das Internet mitspielen können, setze in `config.ini` im Abschnitt
`[server]` die Zeile `public = on` (Standard: aus; `config.example.ini` zeigt die Zeile). Das Spielen zu Hause
funktioniert ohne diese Einstellung wie bisher, mit einer Regel für jeden Server: Zwei Spiele können nicht
gleichzeitig mit demselben Namen spielen. Bei dieser Prüfung zählt Groß- und Kleinschreibung nicht, und jedes
Zeichen außer A-Z, a-z, 0-9, `-` und `_` zählt als `_` (`Max.Power`, `max_power` und `MAX POWER` sind ein Name).

**Was `public = on` bewirkt**

- Der erste Login mit einem Namen reserviert ihn. Spätere Logins brauchen dieselben Zugangsdaten; niemand sonst kann
  auf diesem Spielstand spielen. Der Standardname `Player` kann nicht reserviert werden.
- Namen werden ohne Groß- und Kleinschreibung verglichen, und jedes Zeichen außer A-Z, a-z, 0-9, `-` und `_` zählt
  als `_`: `Max.Power`, `Max_Power` und `max_power` sind ein Name (zu Hause teilen sich `Ünal` und `_nal` auf
  dieselbe Weise einen Spielstand). Ein öffentlicher Server nimmt Namen mit höchstens 32 Zeichen aus einfachem ASCII
  an (Buchstaben, Ziffern, Leerzeichen und Satzzeichen, keine Umlaute oder Akzente) und lehnt andere ab.
- Eine IP-Adresse kann höchstens 3 neue Namen pro Stunde reservieren. Logins mit Namen, die sie schon reserviert
  hat, sind nicht begrenzt.
- Nach 5 falschen Passwörtern von einer IP-Adresse innerhalb von 10 Minuten wird diese Adresse für den Rest des
  Zeitfensters abgewiesen.
- Beim Start listet der Server Spielstände auf, die noch niemand reserviert hat („unclaimed saves“); jeder darf sie
  beim nächsten Login übernehmen. Ein neuer Spieler beginnt mit einem frischen Spielstand: Dein eigener
  Einzelspieler-Spielstand (`save/1.21/player.json`) wird nicht an den ersten Fremden vergeben. Willst du ihn
  behalten, kopiere ihn nach `save/1.21/players/<Name>.json` und reserviere zuerst diesen Namen.
- Die Logs in `captures` enthalten keine Tickets, Passwörter und Schlüssel; Spieler können sie deshalb an
  Fehlermeldungen anhängen.

**Was Spieler tun**

- PC: Trage in `reborn.ini` unter `[Player]` die Zeile `Ticket = Name|Passwort` ein. Alles nach dem ersten `|` ist
  das Passwort. Nimm ein Passwort, das du nirgends sonst verwendest (siehe „Grenzen“).
- PS4: Der PS4-Patch ab Version 0.0.4 legt beim ersten Start einen Schlüssel für die Konsole in
  `/data/TheCrew/console_key.txt` an. Spieler sollten diese Datei sichern (Backup). Wird sie auf eine neue Konsole
  kopiert, bleibt der Name erhalten; eine beschädigte Datei wird als `console_key.bad` aufbewahrt. Ein älterer
  PS4-Patch sendet keinen Schlüssel und wird von einem öffentlichen Server abgelehnt.
- PS4: Trage die öffentliche IPv4-Adresse des Servers in `/data/TheCrew/server.txt` ein (eine Adresse wie
  `203.0.113.5`, kein Hostname). Nutzt der Server 8080/8443 (siehe Ports), schreibe `https://203.0.113.5:8443`.
- PC: Setze `Host` in `reborn.ini`; dort ist eine Adresse oder ein Hostname möglich.

**Namen verwalten**

Im Serverordner (das Startskript der Version legt dort `.venv` an):

```
.venv/bin/python tools/accounts.py list
.venv/bin/python tools/accounts.py free NAME
```

`free` gibt den Namen frei. Der nächste Login mit diesem Namen reserviert ihn dann neu (nützlich, wenn ein Spieler
Passwort oder Schlüssel verloren hat). Für eine andere Accounts-Datei schreibe `--file PATH` vor `list` oder `free`:
`.venv/bin/python tools/accounts.py --file PATH list`. `save/accounts.json` enthält
gesalzene Hashes, nie Passwörter; `save/<Version>/sessions.json` enthält die Sitzungsschlüssel. Gib den Ordner `save`
niemals weiter. Spieler senden den Ordner `captures` ein.

**Einstellungen für einen öffentlichen Server**

In `config.ini`:

```
[server]
public = on
ip = 203.0.113.5

[world]
multiplayer = off
```

- `ip` ist die Adresse, die der Server dem Spiel mitteilt; mit ihr verbindet sich das Spiel. Mit `public = on`
  trägt der Server sie auch in die Dienstkonfiguration ein, die das Spiel beim Start abruft. Trage die öffentliche
  IPv4-Adresse des Servers ein (keinen Hostnamen). Ohne diese Zeile teilt der Server seine Adresse im eigenen
  Netzwerk mit, die Spieler im Internet nicht erreichen.
- `multiplayer = off`: Mehrspieler über das Internet funktioniert noch nicht (siehe „Grenzen“).
- Teste den ersten Login selbst aus einem anderen Netzwerk, mit PS4 oder PC, bevor du Spieler einlädst.

**Logs**

Der Ordner `captures` wächst, während gespielt wird: etwa 40 KB pro Spielminute und Spieler (gemessen). Der Server
hat keinen Schalter, der die Logs abschaltet; `--log PFAD` hinter `./start-server.sh` (oder am Ende von
`ExecStart=` in der Unit) verschiebt sie nur: Das Relay-Log von 1.21 landet in PFAD, `requests.jsonl` und
`v100.jsonl` im selben Ordner. Der Server öffnet jedes Log für jeden Eintrag neu; du kannst die Dateien also
jederzeit verschieben oder löschen, er legt neue an. Damit der Ordner klein bleibt, lösche alte Dateien ab und zu
oder überlasse das logrotate mit einer Datei `/etc/logrotate.d/thecrewreborn` (passe Pfad und Benutzer an):

```
/opt/thecrewreborn/captures/*.jsonl {
    su ubuntu ubuntu
    weekly
    rotate 4
    compress
    missingok
    notifempty
}
```

**Ports**

Siehe „Ports“ weiter oben: Öffne genau diese zwischen dem Internet und dem Server. Nutzt der Server statt 80 und 443
die Ports 8080 und 8443, öffne diese beiden.

**Variante 1: Oracle Cloud Always Free**

Laut Oracles Dokumentation umfasst Always Free Arm-A1-Flex-Instanzen mit insgesamt bis zu 2 OCPUs und 12 GB
Arbeitsspeicher sowie 10 TB ausgehenden Datenverkehr pro Monat. Der Server braucht davon nur einen kleinen Bruchteil
(siehe „Last“ unten).

1. Lege ein Always-Free-Konto an und darin eine Instanz: Shape VM.Standard.A1.Flex (1 OCPU und 6 GB genügen), Image
   Ubuntu, öffentliche IPv4-Adresse und dein SSH-Schlüssel.
2. Öffne im Subnetz der Instanz die Security List und füge Ingress-Regeln hinzu (Quelle `0.0.0.0/0`): TCP 80 und 443,
   UDP 4000, 4001 und 4100.
3. Öffne dieselben Ports in der VM selbst. Oracles Ubuntu-Images liefern laut verbreiteten Berichten iptables-Regeln
   mit, die alles außer SSH ablehnen (berichtet, hier nicht geprüft). Ist das bei deiner VM so:

   ```
   sudo iptables -I INPUT 5 -p tcp --dport 80 -j ACCEPT
   sudo iptables -I INPUT 5 -p tcp --dport 443 -j ACCEPT
   sudo iptables -I INPUT 5 -p udp -m multiport --dports 4000,4001,4100 -j ACCEPT
   sudo netfilter-persistent save
   ```

   Führe vorher `sudo iptables -L INPUT -n --line-numbers` aus und füge die Regeln oberhalb der abschließenden
   REJECT-Regel ein (die Position 5 ist ein Beispiel).
4. Installiere den Server: `sudo apt install python3 python3-venv unzip`, entpacke die Version nach
   `/opt/thecrewreborn` (Eigentümer: der Benutzer, der den Server ausführt; nicht unter `/home`, das die Unit
   ausblendet), führe `chmod +x start-server.sh` aus, trage die Einstellungen von oben in `config.ini` ein und
   starte einmal von Hand `./start-server.sh`, um zu prüfen, ob er hochfährt.
5. Installiere die Unit: Passe `User=` und die beiden Pfade in `thecrewreborn.service` an, falls deine abweichen
   (lege den Ordner nicht unter `/home`), dann `sudo cp thecrewreborn.service /etc/systemd/system/` und
   `sudo systemctl daemon-reload && sudo systemctl enable --now thecrewreborn`.
   Logs: `journalctl -u thecrewreborn -f`. Die Unit erlaubt das Belegen der Ports 80 und 443 ohne root.
6. Prüfe aus einem anderen Netzwerk: `curl -k https://<öffentliche-IP>/version` muss antworten.

Oracle gibt an, dass eine ungenutzte Always-Free-Instanz zurückgefordert werden kann: wenn 7 Tage lang CPU-Auslastung
(95. Perzentil), Netzwerk- und Speicherauslastung alle unter 20 % liegen. Ein Spieleserver mit wenigen Spielern kann
darunter fallen. Häufig wird empfohlen, das Konto auf Pay As You Go umzustellen, um die Rückforderung zu
vermeiden; die Always-Free-Ressourcen bleiben kostenlos (berichtet, hier nicht geprüft).

**Variante 2: zu Hause**

Du brauchst eine öffentliche IPv4-Adresse von deinem Anbieter (kein DS-Lite oder CG-NAT). Gib dem Server-Rechner eine
feste Adresse und leite dann am Router die obigen Ports auf ihn weiter. Bei einer FritzBox: Internet, Freigaben,
Portfreigaben, Gerät hinzufügen, je Port eine Freigabe. Ändert dein Anbieter die Adresse, passe `ip` in `config.ini`
an; PS4-Spieler müssen `server.txt` anpassen, PC-Spieler können einen DynDNS-Hostnamen (zum Beispiel MyFRITZ!) als
`Host` nutzen.

**Grenzen**

- Mehrspieler über das Internet gibt es noch nicht: Es fehlt NAT-Traversal, es gibt eine gemeinsame Sitzung, und
  höchstens 5 Spieler können in der Nähe sein. Der öffentliche Modus ist für das Solospiel mit Spielständen gedacht;
  setze `multiplayer = off` (siehe oben).
- Die Spiele akzeptieren jedes Serverzertifikat. Wer auf dem Netzwerkpfad zwischen einem Spieler und dem Server
  mitliest, kann deshalb das PC-Passwort oder den Konsolenschlüssel lesen. Spieler sollten ein Passwort nur für
  diesen Server verwenden.
- Pro IP-Adresse und Portsatz kann nur ein Server laufen.
- Wer den Netzwerkverkehr eines Spielers mitlesen kann und den öffentlichen Relay-Schlüssel kennt, kann das
  Login-Ticket dieses Spielers bis zu 3 Stunden lang wiederverwenden. Das kann nur ein Angreifer auf dem
  Netzwerkpfad.

**Last**

Gemessen am 12.11.2026: etwa 0,02 % eines CPU-Kerns pro Spieler (Apple M2 Max), 1 bis 2 kbit/s im Durchschnitt mit
Spitzen bis 16 kbit/s und etwa 22 KB Spielstand pro Spieler. Der Serverprozess belegt etwa 50 MB Arbeitsspeicher.

## Fehler und Ideen

Melde sie bitte unter https://github.com/thecrewreborn/server/issues (dafür ist ein GitHub-Konto nötig). Der
Ordner `captures` neben dem Server enthält die Logs; hänge sie bitte an, sie helfen bei der Fehlersuche sehr.

## Lizenz

Der Server ist freie Software unter der GNU General Public License, Version 3 oder später (siehe `LICENSE`). Der
PC-Patch (`dinput8.dll`) und der PS4-Patch dürfen kostenlos genutzt und weitergegeben werden; ihre Lizenz ist die
`LICENSE.txt` in ihrem Download. The Crew ist eine Marke von Ubisoft. Dies ist ein Fanprojekt ohne Verbindung zu
Ubisoft oder Sony; du brauchst dein eigenes Exemplar des Spiels.
