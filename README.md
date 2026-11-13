# The Crew Reborn server

English · [Deutsch](#deutsch)

The Crew (2014) on your own server, at home or on the internet, for PS4, PS5 and PC. More at
https://thecrewreborn.github.io

## Quick start

From the [download page](https://thecrewreborn.github.io/#download) you need:

1. **The server, for everyone:** `TheCrewReborn-Server-<version>.zip`. Unpack it on a computer in your home network and start
   it: `start-server.bat` on Windows (double-click), `./start-server.sh` on macOS and Linux (in a terminal).
2. **Playing on PS4 / PS5:** `TheCrewReborn-PS4-<version>.zip`. Copy `CUSA00161.xml` to
   `/data/GoldHEN/patches/xml/` on the console and switch the patch on in GoldHEN's patch menu.
3. **Playing on PC:** `TheCrewReborn-PC-<version>.zip`. Copy `dinput8.dll` and `reborn.ini` into the game folder,
   next to `TheCrew.exe`.

Take all three from the same release. A server from 0.0.4 on refuses the login of older patches without a password.

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
  on the gaming PC itself, or on a computer on the internet (see "Running a server on the internet"). Linux needs
  Python 3.10 or newer with venv (Debian/Ubuntu: `sudo apt install python3 python3-venv`); on Windows and macOS the
  start script installs Python if it is missing.

## The server

The first start sets up the server and takes a minute (macOS asks for your password if it has to install Python).
Later starts take a few seconds. The server runs until you close the window or press Ctrl+C. Windows asks once
whether Python may use the network: allow it for private networks.

The server needs HTTPS: the PS4 sends its login code only over HTTPS. HTTPS is on by default (port 443, or 8443, see
"Ports"). With `https_port = 0` in `config.ini` no PS4 can log in, and the server warns at start.

## Ports

TCP 80 and 443, UDP 4000, 4001 and 4100 (4100: the base game 1.00). Nothing has to be opened on the router for play at home. When the system refuses
port 80 or 443 (another program uses it, or Linux without root) the server takes 8080 and 8443 instead and tells the
game; it says so when it starts. If the game doesn't find the server, set `Host` in `reborn.ini` (PC) or put the
server address into `/data/TheCrew/server.txt` on the console (PS4).

## Names and devices

Each player name has its own save. The first login with a name claims it for the device that logs in; from then on
only that name's devices can play it. Nobody types a password: each device makes its own key and sends each server
only a code made from it. The key never leaves the device, and a code is bound to the server's id, address and HTTPS port, so it works only on the server it was made for. A server that copies another server's id and sits at the same address and port in another network (for example a friend's home network with the same 192.168.x address) could learn a code that is valid on the original server.

- **PS4 / PS5:** the patch creates `/data/TheCrew/console_key.txt` on its first start. The name is the PS4 user name.
- **PC:** the DLL creates `TheCrewReborn.key` next to `reborn.ini`, or in `%LOCALAPPDATA%\TheCrewReborn` when the game
  folder is not writable. The name is `Name` under `[Player]` in `reborn.ini` (empty: your Windows user name). Leave
  `Password` and `Ticket` empty to log in with the key. If the DLL can save the key in neither folder, the game shows
  a message and the server refuses the login.

A key file copied to another console, or next to another PC's `reborn.ini`, plays the same names there.

Names compare without case, and every character other than A-Z, a-z, 0-9, `-` and `_` counts as `_`: `Max.Power`,
`max_power` and `MAX POWER` are one name and one save. A name has at most 32 characters of plain ASCII (letters,
digits, spaces and signs, no umlauts or accents). The server refuses the name `Player`. A PC whose `Name` and Windows user name give
fewer than 3 usable characters (letters A-Z, digits, `.`, `_` and `-`; umlauts are spelled out, other letters such as
Cyrillic or Chinese are dropped) plays as `TheCrewReborn`, an ordinary name like any other. At home, two PCs that fall
back to it join the same name and share one save, so set your own `Name` in `reborn.ini`. PC names are cut at 15 characters, so a PS4 name of 16 characters cannot be played on the PC.

**One name on several devices (cross-save)**

The same name on PS4 and PC plays the same save. To continue your PS4 progress on the PC, set `Name` in `reborn.ini`
to your PS4 user name, spelled exactly the same; it works the other way round too. A new device joins the name once:

- **At home** it joins by itself. A device that logs in from your home network (addresses 10.x, 172.16-31.x,
  192.168.x, 127.x) becomes one of the name's devices at its first login.
- **Over the internet** the server's owner opens a link window with `tools/accounts.py link NAME` (see "Looking
  after names"). For the next 10 minutes the first new device that logs in with that name joins it, and the window
  closes. After several failed tries, wait 10 minutes before you open a link window.

A name holds up to 16 devices. With `link_home = off` under `[server]` in `config.ini`, devices at home need a link
window too. Switch it off when a proxy, a tunnel or a container port forward sits in front of the server: then every
player arrives from a private address, and anyone could join any name. `link_home = off` also turns `lan`
multiplayer off (see "Multiplayer"). The server warns at start when `link_home` is on and `[server] ip` is a public
address.

Instead of a link window a PC can also use a PS4's code. Copy `/data/TheCrew/console_key.txt` from the PS4 and run,
in a server folder, `.venv/bin/python tools/console_password.py console_key.txt <server address>` (Windows:
`.venv\Scripts\python tools\console_password.py console_key.txt <server address>`). Give the address exactly as in the
PS4's `server.txt`, with its port (for example `https://203.0.113.5:8443`); for a console that found the server on
the LAN, give the `Server address` the server shows at its start. The tool asks the server for its id on UDP port
4001; when that port cannot be reached, add `--server-id <id>` with the `server_id` from
`https://<server address>/version`. Set the printed value as `Password = ...` under `[Player]` in `reborn.ini`. It
works only on that server at that address and HTTPS port.

**Server address**

A device's code also depends on the server's address and HTTPS port as the game reaches them. When either changes,
every device sends a new code: at home they join again by themselves; over the internet each name needs a link window
or `free` (see "Looking after names"). Each new join takes one of the name's 16 places.

**One device at a time**

While a game is online with a name, the server refuses a second login with that name from another device, so two
games never write the same save; the game then shows error #0_35. Quit the first game, or wait 90 seconds after it
lost its connection, and log in again.

## Your progress and what to back up

Saves live in the `save` folder next to the server, one file per player name. The base game 1.00 has its own saves
(`save/1.00`); they are separate from those of 1.21 and the PC. Back up:

- **The `save` folder**, all of it: the saves, `save/accounts.json` (the names and their devices, as salted hashes,
  never keys or passwords) and `save/server_id` (the server's id). Never share the `save` folder.
- **PS4:** `/data/TheCrew/console_key.txt`. A damaged file is kept as `console_key.bad`.
- **PC:** `TheCrewReborn.key`, next to `reborn.ini` or in `%LOCALAPPDATA%\TheCrewReborn`.

A device without its key is a new device and joins its names again as described above. If `save/server_id` is lost,
the server makes a new id and every device sends a new code, as after an address change. A damaged `save/server_id`
is never replaced: the server keeps it as `save/server_id.bad` (`server_id.bad.1` and so on when an older one exists)
and does not start until you restore `save/server_id` from the backup or delete the `.bad` files to start with a new
id. The id itself is no password: every discovery answer and `/version` carry it.

At start the server lists saves that no name has claimed yet ("unclaimed saves"); the next login with that name
claims it. A new name starts with a fresh save. A save without a name (`save/1.21/player.json`) is no longer played,
and the server names it at start: to keep it, rename it to `save/1.21/players/<Name>.json` before that name's first
login, then log in as `<Name>`.

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

## Multiplayer

`multiplayer` under `[world]` in `config.ini`:

- `lan` (the default, also without the line): players on your home network see each other and play together;
  players from the internet play alone. `lan` needs `link_home = on` (the default): with `link_home = off` it works
  like `off`, because the server can no longer tell home players apart.
- `on` (also `1`, `true`, `yes`): every player, also from the internet. Multiplayer over the internet does not work
  yet (see "Limits").
- `off` (also `0`, `false`, `no`, or an empty value): everyone plays alone.

Any other word stops the start with a message. A `config.ini` made from the `config.example.ini` of 0.0.3 or older contains `multiplayer = on`. On a server on the
internet, change it to `lan` or `off`.

## Player pictures

The PS4 uploads its user's profile picture once per game start, right after the login, over HTTPS. Only a device of
that name may set its picture. A PC uploads the picture set as `Avatar` in `reborn.ini`, else the picture of the
Windows account. Your own PS4 profile shows your console picture first; other players and the PC see the PC picture
first; without either the server shows its logo. The server refuses a picture over 300 000 bytes or larger than
1024 x 1024 pixels.

## Looking after names

In the server folder (the start script creates `.venv` there; on Windows use `.venv\Scripts\python`):

```
.venv/bin/python tools/accounts.py list
.venv/bin/python tools/accounts.py link NAME
.venv/bin/python tools/accounts.py free NAME
```

- `list` shows each name with its platform, the day it was claimed, its number of devices and an open link window.
- `link NAME` opens a link window for 10 minutes (`--minutes N` for another length, at most a week). The first new
  device that logs in with that name joins it, from any address.
- `free NAME` frees the name with all its devices; its save stays. The next login with that name claims it again.
  Use it for a player who lost every key, or for a name with 16 devices.

The server sees the change at once. Run the tool as the user that runs the server
(`sudo -u <user> .venv/bin/python tools/accounts.py list`), not with plain `sudo`; started as root, the tool gives
the file back to its owner, so the server can still read it. To use another accounts file, put `--file PATH` before
`list`, `link` or `free`.

## Running a server on the internet

The same server runs on the internet; there is nothing to switch on, because names are always checked. You need a
fixed public IPv4 address and a fixed HTTPS port: every device's code depends on both (see "Server address").

**Settings**

In `config.ini`, change the existing lines (`ip` is already under `[server]`, `crew_credits_refill` under
`[player]`). Do not add a second `[server]` heading: the server refuses to start with one.

```
[server]
ip = 203.0.113.5

[world]
multiplayer = lan

[player]
crew_credits_refill = off
```

- `ip` is the address the server announces to the game, in the discovery answer, the configuration the game fetches
  at start and the relay; the game connects to it. Set it to the server's public IPv4 address (not a hostname).
  Behind NAT (an Oracle VM, a home router) this is the public address, not the one the machine sees. The server
  ignores any other value and says so at start. Without it the server announces its address in its own
  network, which players on the internet cannot reach.
- `multiplayer`: `lan` or `off` (see "Multiplayer").
- `crew_credits_refill = off` is recommended. With the default `on` every player always has the full balance and
  every purchase with Crew Credits is free, so anyone can buy everything on the first day.
- `link_home`: leave it on, unless a proxy, a tunnel or a container port forward sits in front of the server (see
  "One name on several devices"). Off also turns `lan` multiplayer off.
- Test the first login yourself from another network, PS4 or PC, before you invite players.

**What players set**

- PS4: put the server's public IPv4 address into `/data/TheCrew/server.txt` (an address like `203.0.113.5`, no
  hostname). If the server uses 8080/8443 (see "Ports"), write `https://203.0.113.5:8443`. Do not use `http://`:
  over plain HTTP the PS4 sends no code, and the server refuses the login. The console must also reach the server
  on UDP port 4001: it learns the server's id there, and without it the login is refused.
- PC: set `Host` under `[Server]` in `reborn.ini`; it takes an address or a hostname. If the server uses 8443, also
  set `HttpsPort = 8443`. A hostname whose address changes (dynamic DNS) gives the PC a new code with every change.
- The first login claims the name. To add a second device, the player asks the owner for a link window.

**Logs**

The `captures` folder grows while people play: about 50 to 100 KB per minute of play and player. A log file that
passes 256 MB moves to `<name>.1` (replacing an older `.1`), so each log takes at most about 512 MB. The logs leave
out tickets, passwords, codes and keys, so players may attach them to bug reports; games that have not logged in
get only a limited number of entries. The server has no switch to turn the logs off; `--log PATH` after
`./start-server.sh` (or at the end of `ExecStart=` in the unit) only moves them: the 1.21 relay log goes to PATH,
`requests.jsonl` and `v100.jsonl` into the same folder. The server opens each log for every entry, so you may move or
delete the files at any time; it starts new ones. To keep the folder smaller, delete old files now and then, or let
logrotate do it with a file `/etc/logrotate.d/thecrewreborn` (adjust the path and the user):

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

Open exactly the ports of "Ports" between the internet and the server. If the server uses 8080 and 8443 instead of
80 and 443, open those two.

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
4. Install the server. Set `VERSION` to the newest version on the download page (0.0.4 or later) and replace
   `ubuntu` with the user the unit runs as (`User=` in `thecrewreborn.service`, see step 5):

   ```
   sudo apt install python3 python3-venv unzip
   VERSION=0.0.4
   curl -L -o server.zip https://github.com/thecrewreborn/server/releases/download/v$VERSION/TheCrewReborn-Server-$VERSION.zip
   unzip server.zip
   sudo mkdir -p /opt/thecrewreborn
   sudo cp -a TheCrewReborn-Server-$VERSION/. /opt/thecrewreborn/
   sudo chown -R ubuntu: /opt/thecrewreborn
   cd /opt/thecrewreborn
   sudo -u ubuntu chmod +x start-server.sh
   sudo -u ubuntu cp config.example.ini config.ini
   ```

   Keep the folder out of `/home`, which the unit hides. Put the settings above into `config.ini` by changing its
   existing lines. Then, as that user, start `./start-server.sh` once by hand to check that it comes up (it prints
   `Server address:` with your public address), and stop it with Ctrl+C. Started by hand, it may not bind ports 80
   and 443: it says so, suggests a `setcap` command and takes 8080 and 8443. That is expected without the unit. The
   unit in step 5 grants that right (`AmbientCapabilities`), so do not run the `setcap` command: it would apply to
   the system's Python and every program that runs it.
5. Install the unit: edit `User=` and the two paths in `thecrewreborn.service` if yours differ (keep the folder out
   of `/home`), then `sudo cp thecrewreborn.service /etc/systemd/system/` and
   `sudo systemctl daemon-reload && sudo systemctl enable --now thecrewreborn`.
   Logs: `journalctl -u thecrewreborn -f`. The unit grants the right to bind ports 80 and 443 without root and
   raises the limit of open files to 65536 (`LimitNOFILE`): every connection and log file takes one.
6. Check from another network: `curl -k https://<public-ip>/version` must answer.

Oracle states that it may reclaim an idle Always Free instance: when for 7 days CPU use (95th percentile), network
use and memory use are all below 20 %. A game server with few players can fall below that. Upgrading the account to
Pay As You Go is the commonly reported way to avoid the reclaim, while the Always Free resources stay free (reported,
not checked here).

**Option 2: at home**

You need a public IPv4 address from your provider (not DS-Lite or CG-NAT). Give the server computer a fixed address,
then forward the ports above to it on the router. On a FritzBox: Internet, Freigaben, Portfreigaben, add a device,
one forwarding per port. A plain port forward keeps the players' own addresses, so `link_home` can stay on. If your
provider changes the address, update `ip` in `config.ini` and `server.txt` on every PS4; every device then needs to
join its name again (see "Server address"), so a server on the internet needs a fixed address.

**Load**

Measured on 2026-11-12: about 0.02 % of one CPU core per player (Apple M2 Max), 1 to 2 kbit/s on average with
peaks up to 16 kbit/s, and about 22 KB of save per player. The server process uses about 50 MB of memory.

## What the server checks

These rules apply to every server, at home and on the internet:

- Logins: one IP address can claim at most 3 new names per hour (all addresses together 200) and log in at most 30
  times in 10 minutes (a game logs in once per start). After 5 wrong codes or passwords from one address within 10
  minutes, the server refuses that address for the rest of the window.
- Until a game has logged in with its name, the relay answers only the requests of the login itself: no purchases,
  no rewards, no multiplayer.
- A login ticket is random. It stays valid while its game plays and for 3 hours after the game's last packet, so a
  long session that reconnects to the relay is not refused.
- HTTP: at most 16 open connections per address and 512 in all; 15 seconds for a request's first line and headers,
  30 seconds for its body, 120 seconds for an idle connection; a body over 300 000 bytes gets error 413.
- Game requests over 64 KB are dropped, and every address has a request and data budget. Discovery answers one
  address at most once a second.
- Saves: values too large for the game's fields are cut when a save loads. A save earns at most 200 000 bucks and
  50 000 XP within any 60 seconds (losses such as a police fine and one-off payments do not count). A car model holds
  at most 254 configurations, the garage 1100. A tuner purchase the server refuses does not write the save.
- The server answers a request the game sends again from its store of recent replies. When that reply is no longer
  stored, the server drops the request, so a purchase is never made twice.
- `save/accounts.json` and `sessions.json` are readable only by the user that runs the server.
- Multiplayer serves only logged-in games, with `lan` only those from the home network while `link_home` is on.
- A `config.ini` the server cannot read (for example one with a second `[server]` heading), or an unknown
  `link_home` or `multiplayer` value, stops the start with a message.

## Limits

- Multiplayer over the internet is not built yet: there is no NAT traversal, one session is shared, and at most 5
  players can be nearby. Keep `multiplayer` at `lan` or `off` on a server on the internet.
- The games accept any server certificate, so someone on the network path between a player and the server can read
  a device's code for this server (not its key) or a PC password, and log in with it on this server. Use a
  `Password` only for this server.
- At home with `link_home = on`, every device on your home network can join any name.
- A link window admits the first new device that logs in with the name, from anywhere. Open it only when the player is
  ready to log in.
- A device code is bound to the server's id, address and HTTPS port, not to its certificate. Play on servers of people
  you trust, and back up your key files.
- Only one server can run per IP address and port set.
- Two devices of one name behind one IP address (for example a PS4 and a PC at home) knock each other out: each login
  ends the session of the other device.
- Players behind one IP address share limits: at most 16 open HTTP connections and 32 games that have not finished
  their login at the same time. Players behind one shared address (a household, or a provider's carrier-grade NAT)
  also share the server's request and data budget for that address.
- Two consoles behind one IP address that look for the server within the same second may see the DLC as not owned
  for that game run. Restart the game on that console.
- Anyone who can read a player's network traffic and knows the public relay key can replay that player's login ticket
  while any game plays with it; the ticket ends at the owner's next login or with `tools/accounts.py free`. Only an
  attacker on the network path can do this.
- A dropped repeat request (see "What the server checks") should look to the game like a lost connection; this is
  not yet tested on a console.

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

The Crew (2014) auf deinem eigenen Server, zu Hause oder im Internet, für PS4, PS5 und PC. Mehr unter
https://thecrewreborn.github.io

## Schnellstart

Von der [Download-Seite](https://thecrewreborn.github.io/#download) brauchst du:

1. **Den Server, für alle:** `TheCrewReborn-Server-<Version>.zip`. Entpacke ihn auf einem Rechner in deinem Heimnetz und
   starte ihn: `start-server.bat` unter Windows (Doppelklick), `./start-server.sh` unter macOS und Linux (im Terminal).
2. **Spielen auf PS4 / PS5:** `TheCrewReborn-PS4-<Version>.zip`. Kopiere `CUSA00161.xml` auf der Konsole nach
   `/data/GoldHEN/patches/xml/` und schalte den Patch im Patch-Menü von GoldHEN ein.
3. **Spielen am PC:** `TheCrewReborn-PC-<Version>.zip`. Kopiere `dinput8.dll` und `reborn.ini` in den Spielordner,
   neben `TheCrew.exe`.

Nimm alle drei aus demselben Release. Ein Server ab 0.0.4 lehnt die Anmeldung älterer Patches ohne Passwort ab.

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
  auch auf dem Spiele-PC selbst laufen oder auf einem Rechner im Internet (siehe „Einen Server im Internet
  betreiben“). Linux braucht Python 3.10 oder neuer mit venv (Debian/Ubuntu: `sudo apt install python3 python3-venv`);
  unter Windows und macOS installiert das Startskript Python bei Bedarf.

## Der Server

Der erste Start richtet den Server ein und dauert eine Minute (macOS fragt nach deinem Passwort, falls Python
installiert werden muss). Spätere Starts dauern ein paar Sekunden. Der Server läuft, bis du das Fenster schließt
oder Strg+C drückst. Windows fragt einmal, ob Python das Netzwerk nutzen darf: Erlaube den Zugriff für private
Netzwerke.

Der Server braucht HTTPS: Die PS4 sendet ihren Login-Code nur über HTTPS. HTTPS ist standardmäßig an (Port 443 oder
8443, siehe „Ports“). Mit `https_port = 0` in `config.ini` kann sich keine PS4 anmelden, und der Server warnt beim
Start.

## Ports

TCP 80 und 443, UDP 4000, 4001 und 4100 (4100: das Basisspiel 1.00). Für das Spielen zu Hause muss am Router nichts freigegeben werden. Wenn das
System Port 80 oder 443 verweigert (ein anderes Programm nutzt ihn, oder Linux ohne root), nimmt der Server 8080 und
8443 und teilt das dem Spiel mit; beim Start steht der Hinweis im Fenster. Findet das Spiel den Server nicht, trage
am PC `Host` in `reborn.ini` ein oder auf der PS4 die Serveradresse in `/data/TheCrew/server.txt`.

## Namen und Geräte

Jeder Spielername hat seinen eigenen Spielstand. Der erste Login mit einem Namen reserviert ihn für das Gerät, das
sich anmeldet; danach können nur noch die Geräte dieses Namens mit ihm spielen. Niemand tippt ein Passwort ein: Jedes
Gerät erzeugt seinen eigenen Schlüssel und sendet jedem Server nur einen daraus erzeugten Code. Der Schlüssel verlässt
das Gerät nie, und ein Code ist an die ID, die Adresse und den HTTPS-Port des Servers gebunden, gilt also nur für den Server, für den er erzeugt wurde. Ein Server, der die ID eines anderen Servers übernimmt und in einem anderen Netz unter derselben Adresse und demselben Port steht (zum Beispiel im Heimnetz eines Freundes mit derselben 192.168.x-Adresse), könnte einen Code erfahren, der auf dem ursprünglichen Server gültig ist.

- **PS4 / PS5:** Der Patch legt beim ersten Start `/data/TheCrew/console_key.txt` an. Der Name ist der
  PS4-Benutzername.
- **PC:** Die DLL legt `TheCrewReborn.key` neben `reborn.ini` an, oder in `%LOCALAPPDATA%\TheCrewReborn`, wenn der
  Spielordner nicht beschreibbar ist. Der Name ist `Name` unter `[Player]` in `reborn.ini` (leer: dein
  Windows-Benutzername). Lass `Password` und `Ticket` leer, damit sich das Spiel mit dem Schlüssel anmeldet. Kann die
  DLL den Schlüssel in keinem der beiden Ordner speichern, zeigt das Spiel eine Meldung, und der Server lehnt die
  Anmeldung ab.

Eine Schlüsseldatei, die du auf eine andere Konsole oder neben die `reborn.ini` eines anderen PCs kopierst, spielt
dort dieselben Namen.

Namen werden ohne Groß- und Kleinschreibung verglichen, und jedes Zeichen außer A-Z, a-z, 0-9, `-` und `_` zählt als
`_`: `Max.Power`, `max_power` und `MAX POWER` sind ein Name und ein Spielstand. Ein Name hat höchstens 32 Zeichen aus
einfachem ASCII (Buchstaben, Ziffern, Leerzeichen und Satzzeichen, keine Umlaute oder Akzente). Den Namen `Player`
lehnt der Server ab. Ein PC, dessen `Name` und dessen Windows-Benutzername weniger als 3 verwendbare Zeichen ergeben
(Buchstaben A-Z, Ziffern, `.`, `_` und `-`; Umlaute werden ausgeschrieben, andere Buchstaben wie kyrillische oder
chinesische fallen weg), spielt als `TheCrewReborn`, ein gewöhnlicher Name wie jeder andere. Zu Hause treten zwei PCs,
die auf ihn zurückfallen, demselben Namen bei und teilen sich einen Spielstand. Trage deshalb einen eigenen `Name` in
`reborn.ini` ein. PC-Namen werden nach 15 Zeichen
abgeschnitten; einen PS4-Namen mit 16 Zeichen kannst du am PC also nicht spielen.

**Ein Name auf mehreren Geräten (Cross-Save)**

Derselbe Name auf PS4 und PC spielt denselben Spielstand. Um deinen PS4-Fortschritt am PC weiterzuspielen, trage in
`reborn.ini` bei `Name` deinen PS4-Benutzernamen in exakt derselben Schreibweise ein; umgekehrt funktioniert es
ebenso. Ein neues Gerät tritt dem Namen einmal bei:

- **Zu Hause** tritt es von selbst bei. Ein Gerät, das sich aus deinem Heimnetz anmeldet (Adressen 10.x,
  172.16-31.x, 192.168.x, 127.x), wird bei seinem ersten Login zu einem Gerät des Namens.
- **Über das Internet** öffnet der Betreiber des Servers mit `tools/accounts.py link NAME` ein Zeitfenster (siehe
  „Namen verwalten“). In den nächsten 10 Minuten tritt das erste neue Gerät, das sich mit diesem Namen anmeldet, dem
  Namen bei, und das Zeitfenster schließt sich. Warte nach mehreren fehlgeschlagenen Versuchen 10 Minuten, bevor du ein Zeitfenster öffnest.

Ein Name fasst bis zu 16 Geräte. Mit `link_home = off` unter `[server]` in `config.ini` brauchen auch Geräte zu Hause
ein Zeitfenster. Schalte es aus, wenn ein Proxy, ein Tunnel oder eine Portweiterleitung eines Containers vor dem
Server sitzt: Dann kommt jeder Spieler von einer privaten Adresse, und jeder könnte jedem Namen beitreten.
`link_home = off` schaltet auch den Mehrspieler `lan` ab (siehe „Mehrspieler“). Der Server warnt beim Start, wenn
`link_home` an ist und `[server] ip` eine öffentliche Adresse ist.

Statt eines Zeitfensters kann ein PC auch den Code einer PS4 verwenden. Kopiere `/data/TheCrew/console_key.txt` von
der PS4 und führe in einem Serverordner `.venv/bin/python tools/console_password.py console_key.txt <Serveradresse>`
aus (Windows: `.venv\Scripts\python tools\console_password.py console_key.txt <Serveradresse>`). Gib die Adresse genau
so an wie in der `server.txt` der PS4, mit ihrem Port (zum Beispiel `https://203.0.113.5:8443`); für eine Konsole, die
den Server im LAN gefunden hat, gib die `Server address` an, die der Server beim Start anzeigt. Das Werkzeug fragt den
Server über UDP-Port 4001 nach seiner Kennung; ist dieser Port nicht erreichbar, ergänze `--server-id <Kennung>` mit
der `server_id` aus `https://<Serveradresse>/version`. Trage den ausgegebenen Wert als `Password = ...` unter
`[Player]` in `reborn.ini` ein. Er gilt nur für diesen Server unter dieser Adresse und diesem HTTPS-Port.

**Serveradresse**

Der Code eines Geräts hängt auch von der Adresse und dem HTTPS-Port ab, unter denen das Spiel den Server erreicht.
Ändert sich eines davon, sendet jedes Gerät einen neuen Code: Zu Hause treten die Geräte von selbst wieder bei; über
das Internet braucht jeder Name ein Zeitfenster oder `free` (siehe „Namen verwalten“). Jeder neue Beitritt belegt
einen der 16 Plätze des Namens.

**Ein Gerät zur Zeit**

Solange ein Spiel mit einem Namen online ist, lehnt der Server eine zweite Anmeldung mit diesem Namen von einem anderen
Gerät ab, damit nie zwei Spiele in denselben Spielstand schreiben; das Spiel zeigt dann Fehler #0_35. Beende das erste
Spiel oder warte 90 Sekunden, nachdem es die Verbindung verloren hat, und melde dich erneut an.

## Dein Fortschritt und was du sichern solltest

Spielstände liegen im Ordner `save` neben dem Server, eine Datei pro Spielername. Das Basisspiel 1.00 hat eigene
Spielstände (`save/1.00`), getrennt von denen der Version 1.21 und des PCs. Sichere:

- **Den Ordner `save`**, vollständig: die Spielstände, `save/accounts.json` (die Namen und ihre Geräte als gesalzene
  Hashes, nie Schlüssel oder Passwörter) und `save/server_id` (die Kennung des Servers). Gib den Ordner `save`
  niemals weiter.
- **PS4:** `/data/TheCrew/console_key.txt`. Eine beschädigte Datei wird als `console_key.bad` aufbewahrt.
- **PC:** `TheCrewReborn.key`, neben `reborn.ini` oder in `%LOCALAPPDATA%\TheCrewReborn`.

Ein Gerät ohne seinen Schlüssel ist ein neues Gerät und tritt seinen Namen wie oben beschrieben erneut bei. Geht
`save/server_id` verloren, erzeugt der Server eine neue Kennung, und jedes Gerät sendet einen neuen Code, wie nach
einer Adressänderung. Eine beschädigte `save/server_id` wird nie ersetzt: Der Server bewahrt sie als
`save/server_id.bad` auf (`server_id.bad.1` usw., wenn schon eine ältere existiert) und startet nicht, bis du
`save/server_id` aus der Sicherung wiederherstellst oder die `.bad`-Dateien löschst, um mit einer neuen Kennung zu
beginnen. Die Kennung selbst ist kein Passwort: Jede Antwort der Serversuche und `/version` enthalten sie.

Beim Start listet der Server Spielstände auf, die noch kein Name reserviert hat („unclaimed saves“); der nächste
Login mit diesem Namen reserviert ihn. Ein neuer Name beginnt mit einem frischen Spielstand. Einen Spielstand ohne
Namen (`save/1.21/player.json`) spielt der Server nicht mehr und nennt ihn beim Start: Willst du ihn behalten, benenne
ihn vor dem ersten Login des Namens in `save/1.21/players/<Name>.json` um und melde dich dann als `<Name>` an.

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

## Mehrspieler

`multiplayer` unter `[world]` in `config.ini`:

- `lan` (Standard, auch ohne die Zeile): Spieler in deinem Heimnetz sehen einander und spielen zusammen; Spieler aus
  dem Internet spielen allein. `lan` braucht `link_home = on` (den Standard): Mit `link_home = off` wirkt es wie
  `off`, weil der Server Spieler zu Hause dann nicht mehr erkennt.
- `on` (auch `1`, `true`, `yes`): alle Spieler, auch aus dem Internet. Mehrspieler über das Internet funktioniert noch
  nicht (siehe „Grenzen“).
- `off` (auch `0`, `false`, `no` oder ein leerer Wert): Alle spielen allein.

Jedes andere Wort beendet den Start mit einer Meldung. Eine aus der `config.example.ini` von 0.0.3 oder älter erstellte `config.ini` enthält `multiplayer = on`. Ändere den
Wert auf einem Server im Internet auf `lan` oder `off`.

## Spielerbilder

Die PS4 lädt das Profilbild ihres Benutzers einmal pro Spielstart direkt nach dem Login über HTTPS hoch. Nur ein Gerät
des Namens darf dessen Bild setzen. Ein PC lädt das Bild hoch, das in `reborn.ini` als `Avatar` eingetragen ist, sonst
das Bild des Windows-Kontos. Dein eigenes PS4-Profil zeigt zuerst dein Konsolenbild; andere Spieler und der PC sehen
zuerst das PC-Bild; ohne beide zeigt der Server sein Logo. Der Server lehnt ein Bild über 300 000 Bytes oder größer
als 1024 x 1024 Pixel ab.

## Namen verwalten

Im Serverordner (das Startskript legt dort `.venv` an; unter Windows nimm `.venv\Scripts\python`):

```
.venv/bin/python tools/accounts.py list
.venv/bin/python tools/accounts.py link NAME
.venv/bin/python tools/accounts.py free NAME
```

- `list` zeigt jeden Namen mit seiner Plattform, dem Tag der Reservierung, der Zahl seiner Geräte und einem offenen
  Zeitfenster.
- `link NAME` öffnet ein Zeitfenster für 10 Minuten (`--minutes N` für eine andere Dauer, höchstens eine Woche). Das
  erste neue Gerät, das sich mit diesem Namen anmeldet, tritt ihm bei, von jeder Adresse aus.
- `free NAME` gibt den Namen mit allen seinen Geräten frei; sein Spielstand bleibt. Der nächste Login mit diesem Namen
  reserviert ihn neu. Nutze das für einen Spieler, der jeden Schlüssel verloren hat, oder für einen Namen mit 16
  Geräten.

Der Server übernimmt die Änderung sofort. Führe das Werkzeug als der Benutzer aus, der den Server ausführt
(`sudo -u <Benutzer> .venv/bin/python tools/accounts.py list`), nicht mit einfachem `sudo`; als root gestartet, gibt
das Werkzeug die Datei ihrem Eigentümer zurück, damit der Server sie weiter lesen kann. Für eine andere
Accounts-Datei schreibe `--file PFAD` vor `list`, `link` oder `free`.

## Einen Server im Internet betreiben

Derselbe Server läuft auch im Internet; einschalten musst du nichts, denn der Server prüft Namen immer. Du brauchst
eine feste öffentliche IPv4-Adresse und einen festen HTTPS-Port: Der Code jedes Geräts hängt von beiden ab (siehe
„Serveradresse“).

**Einstellungen**

Ändere in `config.ini` die vorhandenen Zeilen (`ip` steht bereits unter `[server]`, `crew_credits_refill` unter
`[player]`). Füge keine zweite Überschrift `[server]` hinzu: Damit startet der Server nicht.

```
[server]
ip = 203.0.113.5

[world]
multiplayer = lan

[player]
crew_credits_refill = off
```

- `ip` ist die Adresse, die der Server dem Spiel mitteilt: in der Antwort der Serversuche, in der
  Dienstkonfiguration, die das Spiel beim Start abruft, und im Relay; mit ihr verbindet sich das Spiel. Trage die
  öffentliche IPv4-Adresse des Servers ein (keinen Hostnamen). Hinter NAT (eine Oracle-VM, ein Router zu Hause) ist
  das die öffentliche Adresse, nicht die, die der Rechner selbst sieht. Jeden anderen Wert ignoriert der Server und meldet das
  beim Start. Ohne diese Zeile teilt der Server seine Adresse im eigenen Netzwerk mit, die Spieler im Internet nicht
  erreichen.
- `multiplayer`: `lan` oder `off` (siehe „Mehrspieler“).
- `crew_credits_refill = off` wird empfohlen. Mit dem Standard `on` hat jeder Spieler immer den vollen Kontostand,
  und jeder Kauf mit Crew Credits ist kostenlos; jeder kann also am ersten Tag alles kaufen.
- `link_home`: Lass es an, außer ein Proxy, ein Tunnel oder eine Portweiterleitung eines Containers sitzt vor dem
  Server (siehe „Ein Name auf mehreren Geräten“). Aus schaltet auch den Mehrspieler `lan` ab.
- Teste den ersten Login selbst aus einem anderen Netzwerk, mit PS4 oder PC, bevor du Spieler einlädst.

**Was Spieler einstellen**

- PS4: Trage die öffentliche IPv4-Adresse des Servers in `/data/TheCrew/server.txt` ein (eine Adresse wie
  `203.0.113.5`, kein Hostname). Nutzt der Server 8080/8443 (siehe „Ports“), schreibe `https://203.0.113.5:8443`.
  Verwende kein `http://`: Über einfaches HTTP sendet die PS4 keinen Code, und der Server lehnt die Anmeldung ab. Die
  Konsole muss den Server außerdem über UDP-Port 4001 erreichen: Dort erfährt sie die Kennung des Servers, ohne sie
  wird die Anmeldung abgelehnt.
- PC: Setze `Host` unter `[Server]` in `reborn.ini`; dort ist eine Adresse oder ein Hostname möglich. Nutzt der
  Server 8443, setze außerdem `HttpsPort = 8443`. Ein Hostname, dessen Adresse sich ändert (DynDNS), gibt dem PC bei
  jeder Änderung einen neuen Code.
- Der erste Login reserviert den Namen. Für ein zweites Gerät bittet der Spieler den Betreiber um ein Zeitfenster.

**Logs**

Der Ordner `captures` wächst, während gespielt wird: etwa 50 bis 100 KB pro Spielminute und Spieler. Überschreitet
eine Log-Datei 256 MB, benennt der Server sie in `<Name>.1` um (eine ältere `.1` wird ersetzt); jedes Log belegt
also höchstens etwa 512 MB. Die Logs enthalten keine Tickets, Passwörter, Codes und Schlüssel; Spieler können sie
deshalb an Fehlermeldungen anhängen. Von Spielen, die sich noch nicht angemeldet haben, schreibt der Server nur eine
begrenzte Zahl von Einträgen. Der Server hat keinen Schalter, der die Logs abschaltet; `--log PFAD` hinter
`./start-server.sh` (oder am Ende von `ExecStart=` in der Unit) verschiebt sie nur: Das Relay-Log von 1.21 landet in
PFAD, `requests.jsonl` und `v100.jsonl` im selben Ordner. Der Server öffnet jedes Log für jeden Eintrag neu; du kannst
die Dateien also jederzeit verschieben oder löschen, er legt neue an. Damit der Ordner kleiner bleibt, lösche alte
Dateien ab und zu oder überlasse das logrotate mit einer Datei `/etc/logrotate.d/thecrewreborn` (passe Pfad und
Benutzer an):

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

Öffne genau die Ports aus „Ports“ zwischen dem Internet und dem Server. Nutzt der Server statt 80 und 443 die Ports
8080 und 8443, öffne diese beiden.

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
4. Installiere den Server. Setze `VERSION` auf die neueste Version der Download-Seite (0.0.4 oder neuer) und
   ersetze `ubuntu` durch den Benutzer, unter dem die Unit läuft (`User=` in `thecrewreborn.service`, siehe
   Schritt 5):

   ```
   sudo apt install python3 python3-venv unzip
   VERSION=0.0.4
   curl -L -o server.zip https://github.com/thecrewreborn/server/releases/download/v$VERSION/TheCrewReborn-Server-$VERSION.zip
   unzip server.zip
   sudo mkdir -p /opt/thecrewreborn
   sudo cp -a TheCrewReborn-Server-$VERSION/. /opt/thecrewreborn/
   sudo chown -R ubuntu: /opt/thecrewreborn
   cd /opt/thecrewreborn
   sudo -u ubuntu chmod +x start-server.sh
   sudo -u ubuntu cp config.example.ini config.ini
   ```

   Lege den Ordner nicht unter `/home`, das die Unit ausblendet. Trage die Einstellungen von oben in `config.ini`
   ein, indem du die vorhandenen Zeilen änderst. Starte dann als dieser Benutzer einmal von Hand
   `./start-server.sh`, um zu prüfen, ob er hochfährt (er schreibt `Server address:` mit deiner öffentlichen
   Adresse), und beende ihn mit Strg+C. Von Hand gestartet darf er die Ports 80 und 443 unter Umständen nicht
   belegen: Er meldet das, schlägt einen `setcap`-Befehl vor und nimmt 8080 und 8443. Ohne die Unit ist das zu
   erwarten. Die Unit aus Schritt 5 erteilt dieses Recht (`AmbientCapabilities`); führe den `setcap`-Befehl deshalb
   nicht aus: Er gälte für das Python des Systems und jedes Programm, das es nutzt.
5. Installiere die Unit: Passe `User=` und die beiden Pfade in `thecrewreborn.service` an, falls deine abweichen
   (lege den Ordner nicht unter `/home`), dann `sudo cp thecrewreborn.service /etc/systemd/system/` und
   `sudo systemctl daemon-reload && sudo systemctl enable --now thecrewreborn`.
   Logs: `journalctl -u thecrewreborn -f`. Die Unit erlaubt das Belegen der Ports 80 und 443 ohne root und hebt
   die Grenze für offene Dateien auf 65536 an (`LimitNOFILE`): Jede Verbindung und jede Log-Datei belegt eine.
6. Prüfe aus einem anderen Netzwerk: `curl -k https://<öffentliche-IP>/version` muss antworten.

Oracle gibt an, dass eine ungenutzte Always-Free-Instanz zurückgefordert werden kann: wenn 7 Tage lang CPU-Auslastung
(95. Perzentil), Netzwerk- und Speicherauslastung alle unter 20 % liegen. Ein Spieleserver mit wenigen Spielern kann
darunter fallen. Häufig wird empfohlen, das Konto auf Pay As You Go umzustellen, um die Rückforderung zu
vermeiden; die Always-Free-Ressourcen bleiben kostenlos (berichtet, hier nicht geprüft).

**Variante 2: zu Hause**

Du brauchst eine öffentliche IPv4-Adresse von deinem Anbieter (kein DS-Lite oder CG-NAT). Gib dem Server-Rechner eine
feste Adresse und leite dann am Router die obigen Ports auf ihn weiter. Bei einer FritzBox: Internet, Freigaben,
Portfreigaben, Gerät hinzufügen, je Port eine Freigabe. Eine einfache Portfreigabe behält die eigenen Adressen der
Spieler bei, `link_home` kann also an bleiben. Ändert dein Anbieter die Adresse, passe `ip` in `config.ini` und
`server.txt` auf jeder PS4 an; jedes Gerät muss seinem Namen dann erneut beitreten (siehe „Serveradresse“). Ein
Server im Internet braucht daher eine feste Adresse.

**Last**

Gemessen am 12.11.2026: etwa 0,02 % eines CPU-Kerns pro Spieler (Apple M2 Max), 1 bis 2 kbit/s im Durchschnitt mit
Spitzen bis 16 kbit/s und etwa 22 KB Spielstand pro Spieler. Der Serverprozess belegt etwa 50 MB Arbeitsspeicher.

## Was der Server prüft

Diese Regeln gelten für jeden Server, zu Hause und im Internet:

- Logins: Eine IP-Adresse kann höchstens 3 neue Namen pro Stunde reservieren (alle Adressen zusammen 200) und sich
  innerhalb von 10 Minuten höchstens 30-mal anmelden (ein Spiel meldet sich einmal pro Start an). Nach 5 falschen
  Codes oder Passwörtern von einer Adresse innerhalb von 10 Minuten lehnt der Server diese Adresse für den Rest des
  Zeitfensters ab.
- Bis ein Spiel sich mit seinem Namen angemeldet hat, beantwortet das Relay nur die Anfragen der Anmeldung selbst:
  keine Käufe, keine Belohnungen, kein Mehrspieler.
- Ein Login-Ticket ist zufällig. Es bleibt gültig, solange sein Spiel spielt, und noch 3 Stunden nach dem letzten
  Paket des Spiels; eine lange Sitzung, die sich neu mit dem Relay verbindet, wird also nicht abgewiesen.
- HTTP: höchstens 16 offene Verbindungen pro Adresse und 512 insgesamt; 15 Sekunden für die erste Zeile und die
  Header einer Anfrage, 30 Sekunden für ihren Inhalt, 120 Sekunden für eine untätige Verbindung; ein Inhalt über
  300 000 Bytes erhält den Fehler 413.
- Spielanfragen über 64 KB verwirft der Server, und jede Adresse hat ein Anfrage- und Datenbudget. Die Serversuche
  antwortet einer Adresse höchstens einmal pro Sekunde.
- Spielstände: Werte, die zu groß für die Felder des Spiels sind, kürzt der Server beim Laden. Ein Spielstand
  verdient innerhalb von 60 Sekunden höchstens 200 000 Bucks und 50 000 XP (Verluste wie ein Bußgeld der Polizei und
  einmalige Zahlungen zählen nicht mit). Ein Automodell fasst höchstens 254 Konfigurationen, die Garage 1100. Ein
  Kauf beim Tuner, den der Server ablehnt, schreibt den Spielstand nicht.
- Eine Anfrage, die das Spiel erneut sendet, beantwortet der Server aus seinem Speicher der letzten Antworten. Ist
  diese Antwort dort nicht mehr vorhanden, verwirft der Server die Anfrage; ein Kauf geschieht also nie doppelt.
- `save/accounts.json` und `sessions.json` kann nur der Benutzer lesen, unter dem der Server läuft.
- Mehrspieler bedient nur angemeldete Spiele, mit `lan` nur die aus dem Heimnetz, solange `link_home` an ist.
- Eine `config.ini`, die der Server nicht lesen kann (etwa mit einer zweiten Überschrift `[server]`), oder ein
  unbekannter Wert bei `link_home` oder `multiplayer` beendet den Start mit einer Meldung.

## Grenzen

- Mehrspieler über das Internet gibt es noch nicht: Es fehlt NAT-Traversal, es gibt eine gemeinsame Sitzung, und
  höchstens 5 Spieler können in der Nähe sein. Lass `multiplayer` auf einem Server im Internet auf `lan` oder `off`.
- Die Spiele akzeptieren jedes Serverzertifikat. Wer auf dem Netzwerkpfad zwischen einem Spieler und dem Server
  mitliest, kann deshalb den Code eines Geräts für diesen Server (nicht seinen Schlüssel) oder ein PC-Passwort lesen
  und sich damit auf diesem Server anmelden. Verwende ein `Password` nur für diesen Server.
- Zu Hause kann mit `link_home = on` jedes Gerät in deinem Heimnetz jedem Namen beitreten.
- Ein Zeitfenster nimmt das erste neue Gerät auf, das sich mit dem Namen anmeldet, von überall her. Öffne es erst,
  wenn der Spieler bereit ist, sich anzumelden.
- Ein Gerätecode ist an die ID, die Adresse und den HTTPS-Port des Servers gebunden, nicht an sein Zertifikat. Spiele
  nur auf Servern von Personen, denen du vertraust, und sichere deine Schlüsseldateien.
- Pro IP-Adresse und Portsatz kann nur ein Server laufen.
- Zwei Geräte eines Namens hinter einer IP-Adresse (zum Beispiel eine PS4 und ein PC zu Hause) werfen sich
  gegenseitig hinaus: Jeder Login beendet die Sitzung des anderen Geräts.
- Spieler hinter einer IP-Adresse teilen sich Grenzen: höchstens 16 offene HTTP-Verbindungen und 32 Spiele, die ihren
  Login noch nicht abgeschlossen haben, gleichzeitig. Spieler hinter einer gemeinsamen Adresse (ein Haushalt oder das
  Carrier-Grade-NAT eines Anbieters) teilen sich außerdem das Anfrage- und Datenbudget des Servers für diese Adresse.
- Zwei Konsolen hinter einer IP-Adresse, die in derselben Sekunde nach dem Server suchen, sehen die DLCs unter
  Umständen für diesen Spielstart als nicht gekauft. Starte das Spiel auf dieser Konsole neu.
- Wer den Netzwerkverkehr eines Spielers mitlesen kann und den öffentlichen Relay-Schlüssel kennt, kann das
  Login-Ticket dieses Spielers wiederverwenden, solange irgendein Spiel mit ihm spielt; das Ticket endet beim nächsten
  Login des Besitzers oder mit `tools/accounts.py free`. Das kann nur ein Angreifer auf dem Netzwerkpfad.
- Eine verworfene wiederholte Anfrage (siehe „Was der Server prüft“) sollte das Spiel wie eine verlorene Verbindung
  behandeln; das ist auf einer Konsole noch nicht getestet.

## Fehler und Ideen

Melde sie bitte unter https://github.com/thecrewreborn/server/issues (dafür ist ein GitHub-Konto nötig). Der
Ordner `captures` neben dem Server enthält die Logs; hänge sie bitte an, sie helfen bei der Fehlersuche sehr.

## Lizenz

Der Server ist freie Software unter der GNU General Public License, Version 3 oder später (siehe `LICENSE`). Der
PC-Patch (`dinput8.dll`) und der PS4-Patch dürfen kostenlos genutzt und weitergegeben werden; ihre Lizenz ist die
`LICENSE.txt` in ihrem Download. The Crew ist eine Marke von Ubisoft. Dies ist ein Fanprojekt ohne Verbindung zu
Ubisoft oder Sony; du brauchst dein eigenes Exemplar des Spiels.
