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
`reborn.ini` to your PS4 user name, spelled exactly the same; it works the other way round too. On a public server
the name also needs its password or console key (see "What players do" under "Running a public server").

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
`config.example.ini` shows the line). Home play without that setting works as before, apart from the changes in the
next list.

**What changes at home (`public = off`)**

These changes apply to every server, also without `public = on`:

- Two games cannot play the same name at the same time. For that check names compare without case, and every
  character other than A-Z, a-z, 0-9, `-` and `_` counts as `_` (`Max.Power`, `max_power` and `MAX POWER` are one
  name). A PS4 login without a name plays as `Player`.
- `requests.jsonl` hides secrets: tickets, passwords and keys in the `Authorization` header, and the values of query
  parameters whose name contains ticket, token, key or pass (`?ticket=***`). Bodies over 4096 bytes are cut, binary
  bodies appear only as their size, and paths are cut at 2048 characters. A log file that passes 256 MB moves to
  `<name>.1`. A failing log write no longer stops the server.
- When the server takes port 8080 instead of 80, the service configuration names that port.
- HTTP: at most 16 open connections per address and 512 in all. Every request has 15 seconds for its request line
  and headers (on a kept connection counted from its first byte) and 30 seconds for its whole body; an idle
  connection is closed after 120 seconds. A body over 300 000 bytes gets error 413, a `Content-Length` that is not a
  number or is negative error 400.
- `sessions.json` (the session keys of connected games) is readable only by the user that runs the server.
- `[server] ip` must be an IPv4 address; the server ignores any other value and says so at start.
- `[world] multiplayer`: without the line multiplayer is on at home. An empty value (`multiplayer =`) means off, as
  before. `config.example.ini` now leaves the line commented out.
- Avatars: a console picture over 2 MB or larger than 1024 x 1024 pixels gets the default picture. The server keeps
  up to 32 MB of decoded PC pictures in memory.
- Discovery answers one address at most once a second and prints at most 20 lines a minute.
- The server drops game requests over 64 KB (the largest the games send has about 20 KB). Requests that arrive in
  parts may hold at most 64 MB in all while they wait for their last part; the oldest go first.
- A reply the server cannot send prints one line on the console instead of an error report.
- When a save loads, values too large for the game's fields (money, levels, perk levels and the like) are cut to
  the largest value that fits.
- A tuner purchase the server refuses does not write the save.
- Rewards: a save earns at most 200 000 bucks and 50 000 XP within any 60 seconds; the rest of a larger reward is not
  credited. Losses such as a police fine and one-off payments (the dealer budget, a first reward, the daily reward)
  do not count.
- Garage: a car model holds at most 254 configurations, the whole garage 1100. A purchase past either cap fails like
  a purchase the money does not cover.
- The server answers a request the game sends again from its store of recent replies. When that reply is no longer
  stored, the server drops the request instead of running it a second time, so a purchase is never made twice. The
  game is expected to treat it like a lost connection; this is not yet tested on a console.
- `public` in `[server]` takes `on` or `off` (also `yes`/`no`, `true`/`false`, `1`/`0`, any case; empty is off). With
  any other value the server does not start and names the value. Every start prints `public mode: on` or
  `public mode: off`. A `config.ini` the server cannot read, for example one with a second `[server]` heading, also
  stops the start with a message.

**What `public = on` does**

- The first login with a name claims it. Later logins need the same credential, so nobody else can play on that
  save. The default name `Player` cannot be claimed, and a PC cannot claim `TheCrewReborn` (see "What players do").
- Names compare without case, and every character other than A-Z, a-z, 0-9, `-` and `_` counts as `_`:
  `Max.Power`, `Max_Power` and `max_power` are one name (at home, `Ünal` and `_nal` share one save the same way). A
  public server accepts names of at most 32 characters of plain ASCII (letters, digits, spaces and signs, no
  umlauts or accents) and refuses others.
- One IP address can claim at most 3 new names per hour (all addresses together 200) and log in successfully at most
  30 times in 10 minutes (a game logs in once per start).
- After 5 wrong passwords from one IP address within 10 minutes, that address is refused for the rest of the window.
- Until a game has logged in with its name, the relay answers only the requests of the login itself (no purchases,
  no rewards, no multiplayer); it drops everything else without an answer.
- At start the server lists saves nobody has claimed yet ("unclaimed saves"); anyone may claim them at their next
  login. A new player starts with a fresh save: your own single-player save (`save/1.21/player.json`) is not handed
  to the first stranger. To keep it, copy it to `save/1.21/players/<Name>.json` and claim that name first.
- The logs in `captures` leave out tickets, passwords and keys, so players may attach them to bug reports.
- A login ticket stays valid while its game plays and for 3 hours after the game's last packet, so a long session
  that reconnects to the relay is not refused.

**What players do**

- PC (release 0.0.4 and later): in `reborn.ini` set `Password = ...` under `[Player]`. The game logs in with its
  `Name` and this password. Use a password that you use nowhere else (see "Limits"). The older line
  `Ticket = Name|Password` still works, and it overrides `Password`, but only if the name before the `|` is exactly
  the game's `Name` (letters, digits, `.`, `_` and `-`, umlauts spelled out, no spaces, at most 15 characters).
  Everything after the first `|` is the password.
- PC: set your own `Name` under `[Player]` in `reborn.ini`. Without it the game takes the Windows user name. A name
  with fewer than 3 usable characters (letters A-Z, digits, `.`, `_` and `-`; umlauts are spelled out, other letters
  such as Cyrillic or Chinese are dropped) becomes `TheCrewReborn`, and a public server refuses that name for a PC.
  PC names are cut at 15 characters, so a PS4 name of 16 characters cannot be played on the PC.
- Cross-save: a name claimed by a PS4 is bound to that console. The console's key never leaves it: the PS4 sends each
  server its own code, made from the key, that server's id and the address and HTTPS port the game talks to. To play the
  same name on the PC, copy `/data/TheCrew/console_key.txt` from the PS4 and run, in a server folder (the server
  release; its start script creates `.venv`),
  `.venv/bin/python tools/console_password.py console_key.txt <server address>` (Windows:
  `.venv\Scripts\python tools\console_password.py console_key.txt <server address>`). Give the server address exactly as
  in the PS4's `server.txt`, with its port (for example `https://203.0.113.5:8443`; without a port the tool takes 443,
  as the PS4 does). The tool asks the server for its id on UDP port 4001. For a console without `server.txt` (it found
  the server on the LAN), give the address the server announces: `ip` under `[server]` in `config.ini`, else the
  `Server address` the server shows at its start, with `:<port>` when its HTTPS port is not 443; the tool names the
  address and port when they differ. When UDP port 4001 cannot be reached, add `--server-id <id>` with the `server_id`
  that `https://<server address>/version` shows. Set the printed value as `Password` in `reborn.ini`. It works only on
  that server at that address and HTTPS port. A name claimed on the PC with another password cannot be played on a PS4,
  so claim it on the PS4 first.
- Fixed address: the PS4 code works only at the server address and HTTPS port it was made for. When a public server's
  address or HTTPS port changes, every PS4 player gets a new code and is refused for the claimed name; the owner frees
  those names with `tools/accounts.py free NAME`, and the players claim them again. A public server therefore needs a
  fixed public IPv4 address and HTTPS port.
- PS4: the patch from 0.0.4 on creates `/data/TheCrew/console_key.txt` on its first start. Back this file up; copying
  it to a new console keeps the name, and a damaged file is kept as `console_key.bad`. Older PS4 patches send no
  key, and a 0.0.4 patch sends none to a server older than 0.0.4 (it sends `OFFLINE`); a public server refuses that
  login.
- PS4: put the server's public IPv4 address into `/data/TheCrew/server.txt` (an address like `203.0.113.5`, no
  hostname). If the server uses 8080/8443 (see Ports), write `https://203.0.113.5:8443`. The console must also reach
  the server on UDP port 4001: it learns the server's id there, and without it the login is refused. Public play
  needs HTTPS: over `http://` the PS4 sends no code (`OFFLINE`), and a public server refuses the login.
- PC: set `Host` in `reborn.ini`; it takes an address or a hostname.

**Looking after names**

In the server folder (the release start script creates `.venv` there):

```
.venv/bin/python tools/accounts.py list
.venv/bin/python tools/accounts.py free NAME
```

Run them as the user that runs the server (`sudo -u <user> .venv/bin/python tools/accounts.py list`), not with
plain `sudo`; started as root, the tool gives the file back to its owner, so the server can still read it. `free`
lets the next login with that name claim it again (for a player who lost the password or the key). To use
another accounts file, put `--file PATH` before `list` or `free`:
`.venv/bin/python tools/accounts.py --file PATH list`. `save/accounts.json` holds salted hashes, never passwords;
`save/<version>/sessions.json` holds the session keys. `save/server_id` holds the server's id: back it up together
with `save/accounts.json`. If it is lost, the server makes a new id, every PS4 sends a new code, and every name
claimed by a PS4 on that server must be freed and claimed again. A damaged file is never replaced: the server keeps
it as `save/server_id.bad` (`server_id.bad.1` and so on when an older one exists) and does not start until you
restore `save/server_id` from the backup or delete the `.bad` files to start with a new id. The id itself is no password (every discovery
answer and `/version` carry it), but keep the file with the rest of `save`. Never share the `save` folder. The
`captures` folder is the one players send.

**Settings for a public server**

In `config.ini`, change the existing lines (`public` and `ip` are already under `[server]`, `crew_credits_refill`
under `[player]`). Do not add a second `[server]` heading: the server refuses to start with one.

```
[server]
public = on
ip = 203.0.113.5

[world]
multiplayer = off

[player]
crew_credits_refill = off
```

- `ip` is the address the server announces to the game, and the game connects to it. With `public = on` the server
  also puts it into the service configuration the game fetches at start. Set it to the server's public IPv4
  address (not a hostname). The server ignores any other value and says so at start. Without it the server announces
  its address in its own network, which players on the internet cannot reach, and it prints a warning at start.
- `multiplayer`: with `public = on` multiplayer is off unless `config.ini` sets it, because multiplayer over the
  internet does not work yet (see "Limits"). An empty value (`multiplayer =`) is off too. A `config.ini` made from
  an older `config.example.ini` contains `multiplayer = on`: change it to `off` or leave the value empty.
- `crew_credits_refill = off` is recommended. With the default `on` every player always has the full balance and
  every purchase with Crew Credits is free, so anyone can buy everything on the first day. With `off` each save keeps
  its own balance and purchases take from it (see "Crew Credits").
- Test the first login yourself from another network, PS4 or PC, before you invite players.

**Logs**

The `captures` folder grows while people play: about 50 to 100 KB per minute of play and player. A log file that
passes 256 MB moves to `<name>.1` (replacing an older `.1`), so each log takes at most about 512 MB. With
`public = on` `proxy.jsonl` (`v100.jsonl` for 1.00) holds only the traffic of games that got a session key,
`requests.jsonl` every HTTP request with its secrets masked, and long entries are cut. The server has no switch
to turn the logs off; `--log PATH` after `./start-server.sh` (or at the end of `ExecStart=` in the
unit) only moves them: the 1.21 relay log goes to PATH, `requests.jsonl` and `v100.jsonl` into the same folder. The
server opens each log for every entry, so you may move or delete the files at any time; it starts new ones. To keep
the folder smaller, delete old files now and then, or let logrotate do it with a file
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
   `public mode: on`), and stop it with Ctrl+C. Started by hand, it may not bind ports 80 and 443: it says so,
   suggests a `setcap` command and takes 8080 and 8443. That is expected without the unit. The unit in step 5 grants
   that right (`AmbientCapabilities`), so do not run the `setcap` command: it would apply to the system's Python and
   every program that runs it.
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
one forwarding per port. If your provider changes the address, update `ip` in `config.ini`; PS4 players must update
`server.txt`, PC players may use a dynamic DNS hostname (for example MyFRITZ!) as `Host`. A new address also changes
every PS4 code: names claimed by a PS4 must be freed and claimed again (see "Looking after names"), so a public
server needs a fixed address.

**Limits**

- Multiplayer over the internet is not built yet: there is no NAT traversal, one session is shared, and at most 5
  players can be nearby. Public mode is for the solo game with saves; set `multiplayer = off` (see above).
- The games accept any server certificate, so someone on the network path between a player and the server can read
  the PC password or the PS4's code for this server (not the console key itself). Players should use a password only
  for this server.
- Only one server can run per IP address and port set.
- One account plays on one device at a time. A login from a second IP address is refused while the first device
  plays. Two devices behind one IP address (for example a PS4 and a PC at home) knock each other out: each login
  ends the session of the other device.
- Players behind one IP address share limits: at most 16 open HTTP connections and 32 games that have not finished
  their login at the same time. Players behind one shared address (a household, or a provider's carrier-grade NAT)
  also share the server's request and data budget for that address.
- Two consoles behind one IP address that look for the server within the same second may see the DLC as not owned
  for that game run. Restart the game on that console.
- Anyone who can read a player's network traffic and knows the public relay key can replay that player's login ticket
  while any game plays with it; the ticket ends at the owner's next login or with `tools/accounts.py free`. Only an
  attacker on the network path can do this.

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
umgekehrt funktioniert es ebenso. Auf einem öffentlichen Server braucht der Name zusätzlich sein Passwort oder seinen
Konsolenschlüssel (siehe „Was Spieler tun“ unter „Einen öffentlichen Server betreiben“).

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
funktioniert ohne diese Einstellung wie bisher, bis auf die Änderungen in der folgenden Liste.

**Was sich zu Hause ändert (`public = off`)**

Diese Änderungen gelten für jeden Server, auch ohne `public = on`:

- Zwei Spiele können nicht gleichzeitig mit demselben Namen spielen. Bei dieser Prüfung zählt Groß- und
  Kleinschreibung nicht, und jedes Zeichen außer A-Z, a-z, 0-9, `-` und `_` zählt als `_` (`Max.Power`, `max_power`
  und `MAX POWER` sind ein Name). Ein PS4-Login ohne Namen spielt als `Player`.
- `requests.jsonl` verbirgt Geheimnisse: Tickets, Passwörter und Schlüssel im `Authorization`-Header sowie die Werte
  von Abfrageparametern, deren Name ticket, token, key oder pass enthält (`?ticket=***`). Inhalte über 4096 Bytes
  werden gekürzt, binäre Inhalte erscheinen nur mit ihrer Größe, und Pfade werden nach 2048 Zeichen gekürzt. Eine
  Logdatei über 256 MB wird zu `<Name>.1`. Ein fehlgeschlagener Logeintrag hält den Server nicht mehr an.
- Nimmt der Server Port 8080 statt 80, nennt die Dienstkonfiguration diesen Port.
- HTTP: höchstens 16 offene Verbindungen pro Adresse und 512 insgesamt. Jede Anfrage hat 15 Sekunden für ihre
  Anfragezeile samt Headern (auf einer gehaltenen Verbindung ab ihrem ersten Byte gezählt) und 30 Sekunden für ihren
  ganzen Inhalt; eine untätige Verbindung wird nach 120 Sekunden geschlossen. Ein Inhalt über 300 000 Bytes erhält
  den Fehler 413, ein `Content-Length`, das keine Zahl oder negativ ist, den Fehler 400.
- `sessions.json` (die Sitzungsschlüssel verbundener Spiele) kann nur der Benutzer lesen, unter dem der Server läuft.
- `[server] ip` muss eine IPv4-Adresse sein; jeden anderen Wert ignoriert der Server und meldet das beim Start.
- `[world] multiplayer`: Ohne die Zeile ist Mehrspieler zu Hause an. Ein leerer Wert (`multiplayer =`) bedeutet aus,
  wie bisher. `config.example.ini` enthält die Zeile jetzt nur als Kommentar.
- Avatare: Ein Konsolenbild über 2 MB oder größer als 1024 x 1024 Pixel erhält das Standardbild. Der Server hält
  bis zu 32 MB entpackter PC-Bilder im Arbeitsspeicher.
- Die Serversuche (Discovery) antwortet einer Adresse höchstens einmal pro Sekunde und schreibt höchstens 20 Zeilen
  pro Minute.
- Der Server verwirft Spielanfragen über 64 KB (die größte, die die Spiele senden, hat etwa 20 KB). Anfragen, die in
  Teilen ankommen, dürfen zusammen höchstens 64 MB belegen, während sie auf ihren letzten Teil warten; die ältesten
  fallen zuerst weg.
- Kann der Server eine Antwort nicht senden, schreibt er eine Zeile auf die Konsole statt eines Fehlerberichts.
- Beim Laden eines Spielstands werden Werte, die zu groß für die Felder des Spiels sind (Geld, Stufen,
  Perk-Stufen und Ähnliches), auf den größten passenden Wert gekürzt.
- Ein Kauf beim Tuner, den der Server ablehnt, schreibt den Spielstand nicht.
- Belohnungen: Ein Spielstand verdient innerhalb von 60 Sekunden höchstens 200 000 Bucks und 50 000 XP; den Rest
  einer größeren Belohnung schreibt der Server nicht gut. Verluste wie ein Bußgeld der Polizei und einmalige
  Zahlungen (das Händlerbudget, eine erste Belohnung, die tägliche Belohnung) zählen nicht mit.
- Garage: Ein Automodell fasst höchstens 254 Konfigurationen, die ganze Garage 1100. Ein Kauf über eine dieser
  Grenzen hinaus schlägt fehl wie ein Kauf, für den das Geld nicht reicht.
- Eine Anfrage, die das Spiel erneut sendet, beantwortet der Server aus seinem Speicher der letzten Antworten. Ist
  diese Antwort dort nicht mehr vorhanden, verwirft der Server die Anfrage, statt sie ein zweites Mal auszuführen;
  ein Kauf geschieht also nie doppelt. Das Spiel sollte sie wie eine verlorene Verbindung behandeln; das ist auf
  einer Konsole noch nicht getestet.
- `public` in `[server]` nimmt `on` oder `off` an (auch `yes`/`no`, `true`/`false`, `1`/`0`, Groß- und
  Kleinschreibung egal; leer bedeutet aus). Mit jedem anderen Wert startet der Server nicht und nennt den Wert. Jeder
  Start schreibt `public mode: on` oder `public mode: off`. Eine `config.ini`, die der Server nicht lesen kann, etwa
  mit einer zweiten Überschrift `[server]`, beendet den Start ebenfalls mit einer Meldung.

**Was `public = on` bewirkt**

- Der erste Login mit einem Namen reserviert ihn. Spätere Logins brauchen dieselben Zugangsdaten; niemand sonst kann
  auf diesem Spielstand spielen. Der Standardname `Player` kann nicht reserviert werden, und ein PC kann
  `TheCrewReborn` nicht reservieren (siehe „Was Spieler tun“).
- Namen werden ohne Groß- und Kleinschreibung verglichen, und jedes Zeichen außer A-Z, a-z, 0-9, `-` und `_` zählt
  als `_`: `Max.Power`, `Max_Power` und `max_power` sind ein Name (zu Hause teilen sich `Ünal` und `_nal` auf
  dieselbe Weise einen Spielstand). Ein öffentlicher Server nimmt Namen mit höchstens 32 Zeichen aus einfachem ASCII
  an (Buchstaben, Ziffern, Leerzeichen und Satzzeichen, keine Umlaute oder Akzente) und lehnt andere ab.
- Eine IP-Adresse kann höchstens 3 neue Namen pro Stunde reservieren (alle Adressen zusammen 200) und sich innerhalb
  von 10 Minuten höchstens 30-mal erfolgreich anmelden (ein Spiel meldet sich einmal pro Start an).
- Nach 5 falschen Passwörtern von einer IP-Adresse innerhalb von 10 Minuten wird diese Adresse für den Rest des
  Zeitfensters abgewiesen.
- Bis ein Spiel sich mit seinem Namen angemeldet hat, beantwortet das Relay nur die Anfragen der Anmeldung selbst
  (keine Käufe, keine Belohnungen, kein Mehrspieler); alles andere verwirft es ohne Antwort.
- Beim Start listet der Server Spielstände auf, die noch niemand reserviert hat („unclaimed saves“); jeder darf sie
  beim nächsten Login übernehmen. Ein neuer Spieler beginnt mit einem frischen Spielstand: Dein eigener
  Einzelspieler-Spielstand (`save/1.21/player.json`) wird nicht an den ersten Fremden vergeben. Willst du ihn
  behalten, kopiere ihn nach `save/1.21/players/<Name>.json` und reserviere zuerst diesen Namen.
- Die Logs in `captures` enthalten keine Tickets, Passwörter und Schlüssel; Spieler können sie deshalb an
  Fehlermeldungen anhängen.
- Ein Login-Ticket bleibt gültig, solange sein Spiel spielt, und noch 3 Stunden nach dem letzten Paket des Spiels;
  eine lange Sitzung, die sich neu mit dem Relay verbindet, wird also nicht abgewiesen.

**Was Spieler tun**

- PC (ab Version 0.0.4): Trage in `reborn.ini` unter `[Player]` die Zeile `Password = ...` ein. Das Spiel meldet
  sich mit seinem `Name` und diesem Passwort an. Nimm ein Passwort, das du nirgends sonst verwendest (siehe
  „Grenzen“). Die ältere Zeile `Ticket = Name|Passwort` funktioniert weiterhin und hat Vorrang vor `Password`, aber
  nur, wenn der Name vor dem `|` exakt dem `Name` des Spiels entspricht (Buchstaben, Ziffern, `.`, `_` und `-`,
  Umlaute ausgeschrieben, keine Leerzeichen, höchstens 15 Zeichen). Alles nach dem ersten `|` ist das Passwort.
- PC: Trage in `reborn.ini` unter `[Player]` einen eigenen `Name` ein. Ohne ihn nimmt das Spiel den
  Windows-Benutzernamen. Ein Name mit weniger als 3 verwendbaren Zeichen (Buchstaben A-Z, Ziffern, `.`, `_` und `-`;
  Umlaute werden ausgeschrieben, andere Buchstaben wie kyrillische oder chinesische fallen weg) wird zu
  `TheCrewReborn`, und diesen Namen lehnt ein öffentlicher Server für einen PC ab. PC-Namen werden nach 15 Zeichen
  abgeschnitten; einen PS4-Namen mit 16 Zeichen kannst du am PC also nicht spielen.
- Cross-Save: Ein Name, den eine PS4 reserviert hat, ist an diese Konsole gebunden. Der Schlüssel der Konsole verlässt
  sie nie: Die PS4 schickt jedem Server einen eigenen Code aus dem Schlüssel, der Kennung dieses Servers sowie der
  Adresse und dem HTTPS-Port, mit denen das Spiel spricht. Um denselben Namen am PC zu spielen, kopiere
  `/data/TheCrew/console_key.txt` von der PS4 und führe in einem Serverordner (die Serverversion; ihr Startskript legt
  `.venv` an) `.venv/bin/python tools/console_password.py console_key.txt <Serveradresse>` aus (Windows:
  `.venv\Scripts\python tools\console_password.py console_key.txt <Serveradresse>`). Gib die Serveradresse genau so an
  wie in der `server.txt` der PS4, mit ihrem Port (zum Beispiel `https://203.0.113.5:8443`; ohne Port nimmt das Werkzeug
  wie die PS4 443). Das Werkzeug fragt den Server über UDP-Port 4001 nach seiner Kennung. Für eine Konsole ohne
  `server.txt` (sie hat den Server im LAN gefunden) gib die Adresse an, die der Server bekannt gibt: `ip` unter
  `[server]` in `config.ini`, sonst die `Server address`, die der Server beim Start anzeigt, mit `:<Port>`, wenn sein
  HTTPS-Port nicht 443 ist; weichen Adresse oder Port ab, nennt das Werkzeug sie. Ist UDP-Port 4001 nicht erreichbar,
  ergänze `--server-id <Kennung>` mit der `server_id`, die `https://<Serveradresse>/version` anzeigt. Trage den
  ausgegebenen Wert als `Password` in `reborn.ini` ein. Er gilt nur für diesen Server unter dieser Adresse und diesem
  HTTPS-Port. Ein am PC mit einem anderen Passwort reservierter Name lässt sich auf keiner PS4 spielen; reserviere ihn
  daher zuerst auf der PS4.
- Feste Adresse: Der PS4-Code gilt nur für die Serveradresse und den HTTPS-Port, für die er erzeugt wurde. Ändert sich
  die Adresse oder der HTTPS-Port eines öffentlichen Servers, bekommt jede PS4 einen neuen Code und wird für den
  reservierten Namen abgelehnt; der Betreiber gibt diese Namen mit `tools/accounts.py free NAME` frei, und die Spieler
  reservieren sie neu. Ein öffentlicher Server braucht daher eine feste öffentliche IPv4-Adresse und einen festen
  HTTPS-Port.
- PS4: Der Patch ab 0.0.4 legt beim ersten Start `/data/TheCrew/console_key.txt` an. Sichere diese Datei; auf eine
  neue Konsole kopiert, bleibt der Name erhalten, und eine beschädigte Datei wird als `console_key.bad` aufbewahrt.
  Ältere PS4-Patches senden keinen Schlüssel, und ein Patch ab 0.0.4 sendet einem Server vor 0.0.4 keinen (er sendet
  `OFFLINE`); ein öffentlicher Server lehnt diese Anmeldung ab.
- PS4: Trage die öffentliche IPv4-Adresse des Servers in `/data/TheCrew/server.txt` ein (eine Adresse wie
  `203.0.113.5`, kein Hostname). Nutzt der Server 8080/8443 (siehe Ports), schreibe `https://203.0.113.5:8443`. Die
  Konsole muss den Server außerdem über UDP-Port 4001 erreichen: Dort erfährt sie die Kennung des Servers, ohne sie
  wird die Anmeldung abgelehnt. Öffentliches Spiel braucht HTTPS: Über `http://` sendet die PS4 keinen Code
  (`OFFLINE`), und ein öffentlicher Server lehnt die Anmeldung ab.
- PC: Setze `Host` in `reborn.ini`; dort ist eine Adresse oder ein Hostname möglich.

**Namen verwalten**

Im Serverordner (das Startskript der Version legt dort `.venv` an):

```
.venv/bin/python tools/accounts.py list
.venv/bin/python tools/accounts.py free NAME
```

Führe sie als der Benutzer aus, der den Server ausführt
(`sudo -u <Benutzer> .venv/bin/python tools/accounts.py list`), nicht mit einfachem `sudo`; als root gestartet, gibt
das Werkzeug die Datei ihrem Eigentümer zurück, damit der Server sie weiter lesen kann. `free` gibt den Namen frei.
Der nächste Login mit diesem Namen reserviert ihn dann neu (nützlich, wenn ein Spieler Passwort oder Schlüssel
verloren hat). Für eine andere Accounts-Datei schreibe `--file PATH` vor `list` oder `free`:
`.venv/bin/python tools/accounts.py --file PATH list`. `save/accounts.json` enthält gesalzene Hashes, nie Passwörter;
`save/<Version>/sessions.json` enthält die Sitzungsschlüssel. `save/server_id` enthält die Kennung des Servers: Sichere
sie zusammen mit `save/accounts.json`. Geht sie verloren, erzeugt der Server eine neue Kennung, jede PS4 sendet einen
neuen Code, und jeder von einer PS4 reservierte Name auf diesem Server muss freigegeben und neu reserviert werden.
Eine beschädigte Datei wird nie ersetzt: Der Server bewahrt sie als `save/server_id.bad` auf (`server_id.bad.1` usw.,
wenn schon eine ältere existiert) und startet nicht, bis du `save/server_id` aus der Sicherung wiederherstellst oder die
`.bad`-Dateien löschst, um mit einer neuen Kennung zu beginnen. Die Kennung selbst ist kein Passwort (jede Antwort der Serversuche und `/version` enthalten sie), bewahre die Datei
aber mit dem übrigen Ordner `save` auf. Gib den Ordner `save` niemals weiter. Spieler senden den Ordner `captures`
ein.

**Einstellungen für einen öffentlichen Server**

Ändere in `config.ini` die vorhandenen Zeilen (`public` und `ip` stehen bereits unter `[server]`,
`crew_credits_refill` unter `[player]`). Füge keine zweite Überschrift `[server]` hinzu: Damit startet der Server
nicht.

```
[server]
public = on
ip = 203.0.113.5

[world]
multiplayer = off

[player]
crew_credits_refill = off
```

- `ip` ist die Adresse, die der Server dem Spiel mitteilt; mit ihr verbindet sich das Spiel. Mit `public = on`
  trägt der Server sie auch in die Dienstkonfiguration ein, die das Spiel beim Start abruft. Trage die öffentliche
  IPv4-Adresse des Servers ein (keinen Hostnamen). Jeden anderen Wert ignoriert der Server und meldet das beim
  Start. Ohne diese Zeile teilt der Server seine Adresse im eigenen Netzwerk mit, die Spieler im Internet nicht
  erreichen, und schreibt beim Start eine Warnung.
- `multiplayer`: Mit `public = on` ist Mehrspieler aus, solange `config.ini` den Wert nicht setzt, denn Mehrspieler
  über das Internet funktioniert noch nicht (siehe „Grenzen“). Ein leerer Wert (`multiplayer =`) bedeutet ebenfalls
  aus. Eine aus einer älteren `config.example.ini` erstellte `config.ini` enthält `multiplayer = on`: Ändere den Wert
  auf `off` oder lass ihn leer.
- `crew_credits_refill = off` wird empfohlen. Mit dem Standard `on` hat jeder Spieler immer den vollen Kontostand,
  und jeder Kauf mit Crew Credits ist kostenlos; jeder kann also am ersten Tag alles kaufen. Mit `off` behält jeder
  Spielstand seinen eigenen Kontostand, und Käufe ziehen davon ab (siehe „Crew Credits“).
- Teste den ersten Login selbst aus einem anderen Netzwerk, mit PS4 oder PC, bevor du Spieler einlädst.

**Logs**

Der Ordner `captures` wächst, während gespielt wird: etwa 50 bis 100 KB pro Spielminute und Spieler. Überschreitet
eine Log-Datei 256 MB, benennt der Server sie in `<Name>.1` um (eine ältere `.1` wird ersetzt); jedes Log belegt
also höchstens etwa 512 MB. Mit `public = on` enthält `proxy.jsonl` (für 1.00 `v100.jsonl`) nur den Verkehr von
Spielen, die einen Sitzungsschlüssel erhalten haben, `requests.jsonl` jede HTTP-Anfrage mit verdeckten Geheimnissen;
lange Einträge werden gekürzt. Der Server hat keinen Schalter, der die Logs abschaltet; `--log PFAD` hinter
`./start-server.sh` (oder am Ende von `ExecStart=` in der Unit) verschiebt sie nur: Das Relay-Log von 1.21 landet in
PFAD, `requests.jsonl` und `v100.jsonl` im selben Ordner. Der Server öffnet jedes Log für jeden Eintrag neu; du kannst die Dateien also
jederzeit verschieben oder löschen, er legt neue an. Damit der Ordner kleiner bleibt, lösche alte Dateien ab und zu
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
   `./start-server.sh`, um zu prüfen, ob er hochfährt (er schreibt `public mode: on`), und beende ihn mit Strg+C. Von
   Hand gestartet darf er die Ports 80 und 443 unter Umständen nicht belegen: Er meldet das, schlägt einen
   `setcap`-Befehl vor und nimmt 8080 und 8443. Ohne die Unit ist das zu erwarten. Die Unit aus Schritt 5 erteilt
   dieses Recht (`AmbientCapabilities`); führe den `setcap`-Befehl deshalb nicht aus: Er gälte für das Python des
   Systems und jedes Programm, das es nutzt.
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
Portfreigaben, Gerät hinzufügen, je Port eine Freigabe. Ändert dein Anbieter die Adresse, passe `ip` in `config.ini`
an; PS4-Spieler müssen `server.txt` anpassen, PC-Spieler können einen DynDNS-Hostnamen (zum Beispiel MyFRITZ!) als
`Host` nutzen. Eine neue Adresse ändert auch jeden PS4-Code: Von einer PS4 reservierte Namen müssen freigegeben und
neu reserviert werden (siehe „Namen verwalten“), ein öffentlicher Server braucht daher eine feste Adresse.

**Grenzen**

- Mehrspieler über das Internet gibt es noch nicht: Es fehlt NAT-Traversal, es gibt eine gemeinsame Sitzung, und
  höchstens 5 Spieler können in der Nähe sein. Der öffentliche Modus ist für das Solospiel mit Spielständen gedacht;
  setze `multiplayer = off` (siehe oben).
- Die Spiele akzeptieren jedes Serverzertifikat. Wer auf dem Netzwerkpfad zwischen einem Spieler und dem Server
  mitliest, kann deshalb das PC-Passwort oder den Code der PS4 für diesen Server lesen (nicht den Konsolenschlüssel
  selbst). Spieler sollten ein Passwort nur für diesen Server verwenden.
- Pro IP-Adresse und Portsatz kann nur ein Server laufen.
- Ein Account spielt immer nur auf einem Gerät. Ein Login von einer zweiten IP-Adresse wird abgelehnt, solange das
  erste Gerät spielt. Zwei Geräte hinter einer IP-Adresse (zum Beispiel eine PS4 und ein PC zu Hause) werfen sich
  gegenseitig hinaus: Jeder Login beendet die Sitzung des anderen Geräts.
- Spieler hinter einer IP-Adresse teilen sich Grenzen: höchstens 16 offene HTTP-Verbindungen und 32 Spiele, die ihren
  Login noch nicht abgeschlossen haben, gleichzeitig. Spieler hinter einer gemeinsamen Adresse (ein Haushalt oder das
  Carrier-Grade-NAT eines Anbieters) teilen sich außerdem das Anfrage- und Datenbudget des Servers für diese Adresse.
- Zwei Konsolen hinter einer IP-Adresse, die in derselben Sekunde nach dem Server suchen, sehen die DLCs unter
  Umständen für diesen Spielstart als nicht gekauft. Starte das Spiel auf dieser Konsole neu.
- Wer den Netzwerkverkehr eines Spielers mitlesen kann und den öffentlichen Relay-Schlüssel kennt, kann das
  Login-Ticket dieses Spielers wiederverwenden, solange irgendein Spiel mit ihm spielt; das Ticket endet beim nächsten Login des Besitzers oder mit
  `tools/accounts.py free`. Das kann nur ein Angreifer auf dem Netzwerkpfad.

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
