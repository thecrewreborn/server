# The Crew Reborn server

The Crew (2014) on your own server, at home or on the internet, for PS4, PS5 and PC. More at
https://thecrewreborn.github.io

## Quick start

From the [download page](https://thecrewreborn.github.io/#download) you need:

1. **The server, for everyone:** `TheCrewReborn-Server-<version>-<platform>.zip` for the computer that runs it (see
   "The server"). Unpack it on a computer in your home network and start `TheCrewReborn-Server`. Or run it on an
   Android phone: `TheCrewReborn-Server-<version>-android.apk` (see "Android phone").
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
  - **PC:** Ubisoft Connect or Steam version, `TheCrew.exe` 1.2.0.0 (build 502193). Tested on Windows 11, also
    Windows 11 on ARM (in a virtual machine).

  Not tested yet: the PS5. Other PS4 regions and game versions don't work yet; the patch fits only CUSA00161 1.00
  and 1.21.
- A computer on the same network as the console or the gaming PC: Windows 10 or 11, macOS 11 or later, or Linux on
  an x64 or ARM64 processor. The server is one program and needs nothing else installed. It can also run on the gaming
  PC itself, or on a computer on the internet (see "Running a server on the internet"). An Android phone with
  Android 9 or later works too (see "Android phone").

## The server

Each platform has its own zip:

| Computer | Zip |
|---|---|
| Windows PC whose Settings > System > About shows "x64-based processor" | `windows-x64` |
| Windows PC whose About page shows "ARM-based processor" | `windows-arm64` |
| Mac, any processor | `macos-universal` |
| Linux, `uname -m` prints `x86_64` | `linux-x64` |
| Linux, `uname -m` prints `aarch64` | `linux-arm64` |

Unpack the zip into a folder of its own. The server keeps everything in that folder, so pick one you can find again.

**Windows:** double-click `TheCrewReborn-Server.exe`. The first time, Windows may show "Windows protected your PC",
because the program has no publisher signature: click "More info", then "Run anyway". Windows then asks whether
the program may use the network: allow it for private networks.

**macOS:** open Terminal, type `cd ` (with a space), drag the folder into the window and press Return. Then run:

```
chmod +x TheCrewReborn-Server
./TheCrewReborn-Server
```

The program does not come from a registered developer, so macOS refuses it the first time. Close the message, open
System Settings > Privacy & Security, scroll down to Security and click "Open Anyway" next to the note about
`TheCrewReborn-Server` (on macOS 12 and older: System Preferences > Security & Privacy > General). Run
`./TheCrewReborn-Server` again and confirm with "Open Anyway" or "Open". If the macOS firewall is on, macOS asks
whether the program may accept incoming connections: allow it. You can also add it under System Settings > Network >
Firewall > Options and set it to allow incoming connections.

**Linux:** in a terminal in the folder:

```
chmod +x TheCrewReborn-Server
./TheCrewReborn-Server
```

The server runs until you close the window or press Ctrl+C. It prints `Server address:` and `Ready.` once it is up.
`--help` lists the options; `-v` also shows the ports and settings at the start.

The server needs HTTPS: the PS4 sends its login code only over HTTPS. HTTPS is on by default (port 443, or 8443, see
"Ports"). With `https_port = 0` in `config.ini` no PS4 can log in, and the server warns at start.

**Where the files live**

Everything sits next to the program:

| File or folder | Holds |
|---|---|
| `config.ini` | your settings. The first start copies `config.example.ini`, which explains every setting. |
| `save/` | the saves, the names and their devices (`accounts.json`), the logins of running games (`tickets.json`), the server's id (`server_id`) |
| `certs/` | the HTTPS certificate and its key, made at the first start |
| `captures/` | the logs (see "Logs") |

To keep these files elsewhere, set the environment variable `TCREBORN_HOME` to that folder before you start the
server; `config.ini`, `save`, `certs` and `captures` then live there. `TCREBORN_CONFIG` names another `config.ini`
on its own. The `accounts` command reads the same variables, so set them the same way when you run it.

**Updating**

Stop the server, unpack the new zip over the old folder and start the server again. The zip holds only the program,
`config.example.ini`, this guide and the licence texts; your `config.ini`, saves, certificate and logs stay as they are.

## Android phone

`TheCrewReborn-Server-<version>-android.apk` is the same server as an app. It runs in the background with a
permanent notification, so the phone can stay on the charger and be the server for the whole home network. Android 9
or later, on a phone with a 64-bit ARM processor (nearly every phone from the last years).

**Installing:** copy the APK to the phone and open it there. Android asks once whether the app you opened it with
(the file manager or the browser) may install apps: allow it. With a computer and USB debugging,
`adb install -r TheCrewReborn-Server-<version>-android.apk` does the same.

**Starting:** open "The Crew Reborn Server" and tap "Start server". The app then shows the admin page (see "The
admin page"), its overview with the server's state and the latest log lines. Allow notifications when Android asks (Android 13 and later): the notification shows that the server runs and
the address the games use, and has a Stop button. Closing the app or swiping it away leaves the server running; open
the app again to see the page. The title bar shows the state and has a start/stop button; the menu has restart, the
page language, light or dark design, the server log and "Keep running in the background".

**Keep it running:** pick "Keep running in the background" in the app's menu once and allow it: Android then leaves
the server alone when the phone sleeps. Some phones have their own battery saver on top; if the server stops while the
phone sleeps, look in the phone's battery settings for the app and allow it to run in the background.

**Ports:** an app may not use ports below 1024, so the server takes 8080 for HTTP and 8443 for HTTPS (written into
its `config.ini` at the first start); the other ports are the usual ones (see "Ports"). Console and PC learn the port
when they find the server on the network, so nothing has to be set.

**The screen off:** some phones do not receive the console's search on the network while their screen is off. A
console or PC that found the server once uses it again, so this matters only for the very first start. For a phone
that is always off, give it a fixed address in your router and set it on the console and the PC: put
`https://<phone address>:8443` into `/data/TheCrew/server.txt` on the console, and `Host=<phone address>` and
`HttpsPort=8443` into `reborn.ini` on the PC.

**Saves:** the app's menu moves them on and off the phone:

- **Export saves** writes the whole `save` folder as one zip, wherever you choose (the phone's Download folder, a
  cloud drive, ...).
- **Import saves** takes such a zip and replaces the server's saves with it. It also takes the `save` folder of a
  server on a computer, zipped (on Windows: right-click > Send to > Compressed folder; on a Mac: right-click >
  Compress), or a zip of the whole server folder (only its `save` folder counts). The app stops the server for it,
  first keeps the current saves as a zip in `backups/` of the server folder, and starts the server again. To move
  your server from a computer to the phone: stop the server on the computer, zip its `save` folder, copy the zip to
  the phone and import it. Then start only one of the two servers.
- **Server folder:** by default the server keeps its files in the app's own storage, which other apps and a computer
  cannot see, and uninstalling the app deletes them. Pick a folder in the phone's storage instead (for example
  `Documents/TheCrewReborn`) and the files are there for a file manager and for a computer connected over USB, and
  they stay when the app is uninstalled. The app asks for the storage permission for it (from Android 11 on "All
  files access", on its own settings page). A folder without server files gets the current ones copied; a folder
  with server files (from an earlier install) is used as it is. Change saves there only while the server is stopped.

To update the app, install the new APK over the old one: the app and its saves stay.

## Ports

TCP 80 and 443, UDP 4000, 4001 and 4100 (4100: the base game 1.00), and TCP 8090 for the admin page. Nothing has to be opened on the router for play
at home. When the system refuses port 80 or 443 (another program uses it, or Linux without root) the server takes 8080
and 8443 instead and tells the game; it says so when it starts. The Android app always uses 8080 and 8443. If the game doesn't find the server, set `Host` in
`reborn.ini` (PC) or put the server address into `/data/TheCrew/server.txt` on the console (PS4).

## Names and devices

Each player name has its own save. The first login with a name claims it for the device that logs in; from then on
only that name's devices can play it. Nobody types a password: each device makes its own key and sends each server
only a code made from it. The key never leaves the device, and a code is bound to the server's id, address and HTTPS
port, so it works only on the server it was made for. A server that copies another server's id and sits at the same
address and port in another network (for example a friend's home network with the same 192.168.x address) could learn
a code that is valid on the original server.

- **PS4 / PS5:** the patch creates `/data/TheCrew/console_key.txt` on its first start. The name is the PS4 user name.
- **PC:** the DLL creates `TheCrewReborn.key` next to `reborn.ini`, or in `%LOCALAPPDATA%\TheCrewReborn` when the game
  folder is not writable. The name is `Name` under `[Player]` in `reborn.ini` (empty: your Windows user name). Leave
  `Password` and `Ticket` empty to log in with the key. If the DLL can save the key in neither folder, the game shows
  a message and the server refuses the login.

A key file copied to another console, or next to another PC's `reborn.ini`, plays the same names there.

Names compare without case, and every character other than A-Z, a-z, 0-9, `-` and `_` counts as `_`: `Max.Power`,
`max_power` and `MAX POWER` are one name and one save. A name has at most 32 characters of plain ASCII (letters,
digits, spaces and signs, no umlauts or accents). The server refuses the name `Player`. A PC whose `Name` and Windows
user name give fewer than 3 usable characters (letters A-Z, digits, `.`, `_` and `-`; umlauts are spelled out, other
letters such as Cyrillic or Chinese are dropped) plays as `TheCrewReborn`, an ordinary name like any other. At home,
two PCs that fall back to it join the same name and share one save, so set your own `Name` in `reborn.ini`. PC names
are cut at 15 characters, so a PS4 name of 16 characters cannot be played on the PC.

**One name on several devices (cross-save)**

The same name on PS4 and PC plays the same save. To continue your PS4 progress on the PC, set `Name` in `reborn.ini`
to your PS4 user name, spelled exactly the same; it works the other way round too. A new device joins the name once:

- **At home** it joins by itself. A device that logs in from your home network (addresses 10.x, 172.16-31.x,
  192.168.x, 127.x) becomes one of the name's devices at its first login.
- **Over the internet** the server's owner opens a link window with `accounts link NAME` (see "Looking after names").
  For the next 10 minutes the first new device that logs in with that name joins it, and the window closes. After
  several failed tries, wait 10 minutes before you open a link window.

A name holds up to 16 devices. With `link_home = off` under `[server]` in `config.ini`, devices at home need a link
window too. Switch it off when a proxy, a tunnel or a container port forward sits in front of the server: then every
player arrives from a private address, and anyone could join any name. `link_home = off` also turns `lan`
multiplayer off (see "Multiplayer"). The server warns at start when `link_home` is on and `[server] ip` is a public
address.

Instead of a link window a PC can also use a PS4's code. Copy `/data/TheCrew/console_key.txt` from the PS4 into the
server folder and run there:

```
./TheCrewReborn-Server console-password console_key.txt <server address>
```

(Windows: `.\TheCrewReborn-Server.exe console-password console_key.txt <server address>`.) Give the address exactly
as in the PS4's `server.txt`, with its port (for example `https://203.0.113.5:8443`); for a console that found the
server on the LAN, give the `Server address` the server shows at its start. The command asks the server for its id on
UDP port 4001; when that port cannot be reached, add `--server-id <id>` with the `server_id` from
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
with a balance of your own, set under `[player]` in `config.ini`:

```ini
crew_credits = 1000000
crew_credits_refill = off
```

A new player then starts with `crew_credits`, and every purchase takes from the balance. Restart the server after
a change.

## DLC

The DLC is off: the content of the paid add-ons (Wild Run's motorbikes and specs, Calling All Units, the car
packs) stays locked. If you own the DLC for your copy of the game, switch it on in one place, the server. Set under
`[player]` in `config.ini`:

```ini
dlc = on
```

Restart the server, then the game. The server cannot check whether you own the DLC, because the official servers
are gone. It unlocks the DLC for everyone who plays on it, so switch it on only if you own it. The PS4 patch and
the PC game take the setting from the server; nothing changes on the console or the PC. With a server in another
network (`server.txt` on the PS4), the console asks that server on UDP port 4001. The base game 1.00 has no DLC.

## Ubisoft Club rewards

Every player owns the Ubisoft Club rewards of the original game: the Electrified paint, the Old School rims, an
interior, the Rust Bucket and beta stickers, the Dodge Viper SRT-10 and the Alfa Romeo 8C Competizione, and 50,000
Crew Credits. The Club was free in the original game, so these rewards do not depend on the DLC setting. The Crew
Credits arrive once per save at the next login, unless `crew_credits_refill` is on. To switch the rewards off, set
under `[player]` in `config.ini`:

```ini
club_rewards = off
```

Restart the server after a change. Game version 1.21 and PC only.

## Chat

The game's chat between players is off by default. To switch it on, set under `[player]` in `config.ini`:

```ini
chat = on
```

A player can then use the chat after the first story mission, as in the original game, and at once with
`abilities = all`. The setting counts at each player's next login; no restart is needed and the saves stay as they
are. Only the PC version has a text chat; the PS4 version has none, so PS4 players cannot read or write messages.
On the PC the chat window opens and shows your own messages; chat between two PCs is not tested yet.

## Multiplayer

`multiplayer` under `[world]` in `config.ini`:

- `lan` (the default, also without the line): players on your home network see each other and play together;
  players from the internet play alone. `lan` needs `link_home = on` (the default): with `link_home = off` it works
  like `off`, because the server can no longer tell home players apart.
- `on` (also `1`, `true`, `yes`): every player, also from the internet. Multiplayer over the internet does not work
  yet (see "Limits").
- `off` (also `0`, `false`, `no`, or an empty value): everyone plays alone.

Any other word stops the start with a message. A `config.ini` made from the `config.example.ini` of 0.0.3 or older
contains `multiplayer = on`. On a server on the internet, change it to `lan` or `off`.

Players connect to each other directly, not through the server. The server tells each game the address it sees the
other game's connection come from, so a VPN or a second network adapter on a PC no longer matters (except for a game
on the server's own computer). A console reaches a PC only when the PC lets the game in: allow `TheCrew.exe` through
the Windows Firewall, on the network type your home network uses (often "Public" by default); without that rule a PS4
cannot join that PC's session or accept its crew invite. After a server restart, players who kept playing still find each other; nobody needs to go back to free roam.

## Player pictures

The PS4 uploads its user's profile picture once per game start, right after the login, over HTTPS. Only a device of
that name may set its picture. A PC uploads the picture set as `Avatar` in `reborn.ini`, else the picture of the
Windows account. Your own PS4 profile shows your console picture first; other players and the PC see the PC picture
first; without either the server shows its logo. The server refuses a picture over 300 000 bytes or larger than
1024 x 1024 pixels.

## Looking after names

The server program has a command for this. In a terminal in the server folder (on Windows type
`.\TheCrewReborn-Server.exe` instead of `./TheCrewReborn-Server`):

```
./TheCrewReborn-Server accounts list
./TheCrewReborn-Server accounts link NAME
./TheCrewReborn-Server accounts free NAME
```

- `list` shows each name with its platform, the day it was claimed, its number of devices and an open link window.
- `link NAME` opens a link window for 10 minutes (`--minutes N` for another length, at most a week). The first new
  device that logs in with that name joins it, from any address.
- `free NAME` frees the name with all its devices; its save stays. The next login with that name claims it again.
  Use it for a player who lost every key, or for a name with 16 devices.

The running server sees the change at once. On Linux, run the command as the user that runs the server
(`sudo -u <user> /opt/thecrewreborn/TheCrewReborn-Server accounts list`), not with plain `sudo`; started as root, the
command gives the file back to its owner, so the server can still read it. To use another accounts file, put
`--file PATH` before `list`, `link` or `free`.

## The admin page

The server has a page for you in the browser: the console log with a search, the connected players, the claimed
names with their devices, the saves (view, download, open the folder) and the server's settings. From there you can
open a link window for a name, free a name, restart the server and stop it.

- **Players:** for each connected player the page shows platform, address, game version, level, XP, bucks, crew
  credits, the current car, the garage, missions done, perks, collectibles, faction, crew and the last position with
  its region. A saved player shows the same from the save file. The page only reads saves; it never writes one.
- **Settings:** every setting of `config.ini`, with its current value and whether it comes from `config.ini` or is the
  default. Saving changes only the lines you changed and keeps your comments, so you can still edit the file by
  hand. If the file changed on disk after you opened the page, the page refuses to save and asks you to reload.
  Settings marked "needs a restart" count after a restart.
- **Restart and stop:** the two buttons at the top. Restart stops the server like Ctrl+C and starts it again in the
  same window; the game stays connected. The page's language is under Settings.
- **Turning the page off:** set `enabled = off` under `[ui]` in `config.ini` and restart the server; the server keeps
  running without the page. Set `enabled = on` to bring it back.

When the server starts it names the page's address, for example `Admin page: http://192.168.1.20:8090/ (home
network)`. Open it on this computer or on any device in your home network, a phone included. The page answers only
addresses from the home network (10.x, 172.16-31.x, 192.168.x) and this computer itself; everyone else is refused.
On a computer with a desktop the browser opens the page at the start.

Settings in `config.ini`, section `[ui]`:

- `enabled = off` switches the page off.
- `open = off` keeps the browser closed at the start.
- `port` sets the page's port (8090 by default).
- `password` sets a password for the page. Everyone in your home network who opens the page must enter it first.
  Set one when others use your network. After five wrong passwords in ten minutes a device has to wait.

A server on the internet (`[server] ip` is a public address, or `link_home = off`) keeps the page on its own computer.
To use it from elsewhere, forward the port through SSH: `ssh -L 8090:127.0.0.1:8090 user@server`, then open
`http://127.0.0.1:8090/` on your own computer.

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
  at start and the relay; the game connects to it. Set it to the server's public IPv4 address (no hostname).
  Behind NAT (a cloud VM, a home router) that is the address the internet sees, which differs from the one the
  machine shows. The server ignores any other value and says so at start. Without it the server announces its
  address in its own network, which players on the internet cannot reach.
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
get only a limited number of entries. The server has no switch to turn the logs off; `--log PATH` after the
program's name (or at the end of `ExecStart=` in the unit) only moves them: the 1.21 relay log goes to PATH,
`requests.jsonl` and `v100.jsonl` into the same folder. The server opens each log for every entry, so you may move or
delete the files at any time; it starts new ones. To keep the folder smaller, delete old files now and then, or let
logrotate do it with a file `/etc/logrotate.d/thecrewreborn` (adjust the path and the user):

```
/opt/thecrewreborn/captures/*.jsonl {
    su crew crew
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

**Option 1: a Linux server**

Any Linux machine with a public IPv4 address works, rented or your own. The steps below are for a Linux system with
apt and systemd; the server needs little (see "Load" below).

1. Open the ports above to the server: in the firewall of your hosting provider, if it has one, and on the machine
   itself. With ufw:

   ```
   sudo ufw allow 80,443/tcp
   sudo ufw allow 4000,4001,4100/udp
   ```

2. Install the server. Set `VERSION` to the newest version on the download page and replace `crew` with the user
   the unit runs as (`User=` in `thecrewreborn.service`, see step 3). The `case` line picks the zip for the
   machine's processor:

   ```
   sudo apt install unzip
   VERSION=0.1.0
   case "$(uname -m)" in aarch64) PLATFORM=linux-arm64 ;; *) PLATFORM=linux-x64 ;; esac
   curl -L -o server.zip https://github.com/thecrewreborn/server/releases/download/v$VERSION/TheCrewReborn-Server-$VERSION-$PLATFORM.zip
   sudo mkdir -p /opt/thecrewreborn
   sudo unzip -o -j server.zip -d /opt/thecrewreborn
   sudo chown -R crew: /opt/thecrewreborn
   cd /opt/thecrewreborn
   sudo -u crew chmod +x TheCrewReborn-Server
   sudo -u crew cp config.example.ini config.ini
   ```

   Keep the folder out of `/home`, which the unit hides. Put the settings above into `config.ini` by changing its
   existing lines. Then, as that user, start `./TheCrewReborn-Server` once by hand to check that it comes up (it
   prints `Server address:` with your public address), and stop it with Ctrl+C. Started by hand, it may not bind
   ports 80 and 443: it says so, suggests a `setcap` command and takes 8080 and 8443. That is expected without the
   unit. The unit in step 3 grants that right (`AmbientCapabilities`), so you need no `setcap`.
3. Install the unit: edit `User=` and the paths in `thecrewreborn.service` if yours differ (keep the folder out
   of `/home`), then `sudo cp thecrewreborn.service /etc/systemd/system/` and
   `sudo systemctl daemon-reload && sudo systemctl enable --now thecrewreborn`.
   Logs: `journalctl -u thecrewreborn -f`. The unit grants the right to bind ports 80 and 443 without root and
   raises the limit of open files to 65536 (`LimitNOFILE`): every connection and log file takes one.
4. Check from another network: `curl -k https://<public-ip>/version` must answer.

To update, run the lines from `VERSION=` to `sudo chown` of step 2 with the new version, then
`sudo systemctl restart thecrewreborn`. Your `config.ini`, saves and logs stay.

**Option 2: at home**

You need a public IPv4 address from your provider (not DS-Lite or CG-NAT). Give the server computer a fixed address,
then forward the ports above to it on the router. A plain port forward keeps the players' own addresses, so
`link_home` can stay on. If your provider changes the address, update `ip` in `config.ini` and `server.txt` on every
PS4; every device then needs to join its name again (see "Server address"), so a server on the internet needs a fixed
address.

**Load**

Measured on 2026-11-12: 1 to 2 kbit/s per player on average, with peaks up to 16 kbit/s, and about 22 KB of save per
player.

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
- `save/accounts.json`, `save/tickets.json`, `sessions.json` and `peers.json` are readable only by the user that
  runs the server. `tickets.json` keeps a hash of each login ticket, never the ticket itself.
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
- A device code is bound to the server's id, address and HTTPS port; the certificate plays no part. Play on servers
  of people you trust, and back up your key files.
- Only one server can run per IP address and port set.
- Two devices of one name behind one IP address (for example a PS4 and a PC at home) knock each other out: each login
  ends the session of the other device.
- Players behind one IP address share limits: at most 16 open HTTP connections and 32 games that have not finished
  their login at the same time. Players behind one shared address (a household, or a provider's carrier-grade NAT)
  also share the server's request and data budget for that address.
- Two consoles behind one IP address that look for the server within the same second may see the DLC as not owned
  for that game run. Restart the game on that console.
- Anyone who can read a player's network traffic and knows the public relay key can replay that player's login ticket
  while any game plays with it; the ticket ends at the owner's next login or with `accounts free`. Only an attacker
  on the network path can do this.
- A dropped repeat request (see "What the server checks") should look to the game like a lost connection; this is
  not yet tested on a console.
- `chat = on` gives PC players the game's text chat; the PS4 version has no text chat, and chat between two PCs is
  not tested yet.

## Bugs and ideas

Report them at https://github.com/thecrewreborn/server/issues (a GitHub account is needed). The `captures` folder
next to the server holds the logs; attach them, they help a lot.

## Licence

The server is free software under the GNU General Public License, version 3 or later (see `LICENSE`). It contains
third-party code under its own licences, listed in `THIRD-PARTY-NOTICES`. The PC patch (`dinput8.dll`) and the PS4
patch are free to use and pass on; their licence is the `LICENSE.txt` in their download. The Crew is a trademark of
Ubisoft. This is a fan project, not affiliated with or endorsed by Ubisoft or Sony; you need your own copy of the
game.
