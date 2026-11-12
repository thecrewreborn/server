# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import base64
import binascii
import datetime
import json
import re
import ssl
import sys
import threading
import time
import urllib.parse
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from tcreborn import avatar
from tcreborn.accounts import AccountStore
from tcreborn.version import VERSION

APP_ID = "103649f4-9ca6-49a7-a122-522f3099854b"
SPACE_ID = "6fbfb85f-567c-49ae-89fe-48fc6fcbd8ce"
ID_NAMESPACE = uuid.UUID("6f1d5c1e-3c0b-4a51-9a52-7468652d6372")
SESSIONS: dict[str, str] = {}
PLATFORMS: dict[str, str] = {}
PROFILES: dict[str, tuple[str, str]] = {}
AVATAR_UPLOAD = re.compile(r"/avatars/name/([A-Za-z0-9_-]{3,16})\.rgba")
AVATAR_RGBA = re.compile(r"/avatars/profile/([0-9a-f-]{36})/(64|128|256)\.rgba")

GATEWAY_RESOURCES = [
    ("all_connections", "/{env}/{version}/profiles/connections"),
    ("all_profiles/entities", "/{env}/{version}/profiles/entities"),
    ("all_spaces/entities", "/{env}/{version}/spaces/entities"),
    ("all_walls", "/{env}/{version}/profiles/wall"),
    ("certificates", "/{env}/{version}/certificates"),
    ("configs/events", "/{env}/{version}/spaces/{spaceId}/configs/events"),
    ("connections", "/{env}/{version}/profiles/{profileId}/connections"),
    ("events", "/{env}/{version}/profiles/{profileId}/events"),
    ("friends", "/{env}/{version}/profiles/me/friends"),
    ("news", "/{env}/{version}/profiles/me/news"),
    ("populations", "/{env}/{version}/profiles/me/populations"),
    ("profiles", "/{env}/{version}/profiles"),
    ("profiles/actions", "/{env}/{version}/profiles/{profileId}/actions"),
    ("profiles/applications", "/{env}/{version}/profiles/me/applications"),
    ("profiles/entities", "/{env}/{version}/profiles/{profileId}/entities"),
    ("profiles/rewards", "/{env}/{version}/profiles/{profileId}/rewards"),
    ("spaces/actions", "/{env}/{version}/spaces/{spaceId}/actions"),
    ("spaces/entities", "/{env}/{version}/spaces/{spaceId}/entities"),
    ("spaces/rewards", "/{env}/{version}/spaces/{spaceId}/rewards"),
    ("wall", "/{env}/{version}/profiles/{profileId}/wall"),
    ("wall/comments", "/{env}/{version}/profiles/wall/{postId}/comments"),
    ("wall/likes", "/{env}/{version}/profiles/wall/{postId}/likes"),
    ("wall/post", "/{env}/{version}/profiles/wall/{postId}"),
    ("websocket/server", "/{env}"),
]

RESOURCE_NAMES = [
    "ContentFiltering", "FriendsProposal", "FriendsRequest", "FriendsLookup", "SocialfeedRead", "SocialfeedWrite",
    "Localization", "UplayWin", "UplayFriends", "EntitiesProfile", "EntitiesSpace", "Profiles", "ProfilesExternal",
    "Users", "Connection", "Messaging", "News", "Populations", "Event", "UplayPassport", "ExtendSession",
    "KeepAlive", "ApplicationUsed", "FixAccountIssues", "Everything",
]

PUNCH = {
    "ALLOW_PLAY_WITHOUT_PUNCH": 1, "MAX_LOGIN_WAIT_TIME_MS": 120000, "PUNCH_LOGIN_TIMEOUT_MS": 30000,
    "PUNCH_TRAVERSAL_KEEPALIVE_DTMS": 5000, "PUNCH_TRAVERSAL_TIMEOUT_MS": 30000, "PUNCH_ACTION_TIMEOUT_MS": 30000,
    "MAPPING_MAX_DURATION_MS": 300000,
}

EVENT_TAGS = [
    "player.progression", "context.stop", "custom.PURCHASE", "custom.PHOTO_MODE_SCREENSHOT",
    "custom.PHOTO_MODE_END", "custom.TOKEN_UNLOCKED", "custom.SHOP_PURCHASE", "custom.KUDO_END",
    "custom.CAMERA_MODE_END", "custom.PVP_SESSION_END", "player.dlc", "custom.NETWORK_PROFILING_SESSION_END",
    "custom.SESSION_END", "custom.CHASE_END", "custom.NOTIFICATION_END", "custom.GAME_STOP",
    "custom.INGAME_STORE_SHOP_EXIT", "custom.SOCIAL_ACTIVITY", "custom.LOBBY_SEARCH_END",
    "custom.game.progression", "custom.ACCOUNT_CREATION", "custom.ACTIVITY_END", "game.localization",
    "custom.OBJECT_DISCOVERED", "custom.CREW_ACTION", "custom.PROFILING", "custom.HARDWARE_CONFIG",
    "custom.CHALLENGE_COMPLETE", "custom.PLAYER_POSITION", "player.achievement", "context.start",
    "custom.FREEDRIVE_CHALLENGE_END", "custom.CHALLENGE_START", "custom.LOBBY_PVP_EVENT_SELECTION_END",
    "custom.WELCOME_DASHBOARD", "custom.ACHIEVEMENT", "custom.FINE", "custom.MISSION_END", "player.start",
    "custom.FACTION_SALARY", "custom.SKILL_END", "custom.TRIAL_UPGRADE", "custom.LOOK_FOR_CREW_END",
    "game.start", "custom.achievement.unlock", "custom.COOP_STUNT_END", "custom.WHEEL_OF_FORTUNE_RESULT",
    "custom.DPOINT_VARIATION", "custom.PERK_UPGRADE", "custom.CAR_REPAIR", "custom.ACCESS_CARD_UNLOCKED",
    "custom.GRAPHICS_OPTIONS"]


def stable_id(kind: str, name: str) -> str:
    return str(uuid.uuid5(ID_NAMESPACE, f"{kind}:{name}"))


def ubi_time(dt: datetime.datetime) -> str:
    return dt.strftime("%Y-%m-%dT%H:%M:%S.%f0Z")


def ticket_name(auth: str) -> str:
    ticket = auth.split("t=", 1)[1] if "t=" in auth else ""
    return ticket.split("|", 1)[0].strip()


def redact(auth: str) -> str:
    head, sep, ticket = auth.partition("t=")
    if not sep:
        return auth
    if head.strip().lower().startswith("psn2.0"):
        return f"{head}t=***"
    if "|" in ticket:
        return f"{head}t={ticket.split('|', 1)[0]}|***"
    return auth


ONLINE: dict[str, tuple[str, float]] = {}
ONLINE_TIMEOUT = 90.0
_ONLINE_LOCK = threading.Lock()


def _now() -> float:
    return time.monotonic()


def _save_key(name: str) -> str:
    from tcreborn.saves import save_name
    return save_name(name).lower()


def touch(name: str, client_ip: str) -> None:
    with _ONLINE_LOCK:
        ONLINE[_save_key(name)] = (client_ip, _now())


def online_elsewhere(name: str, client_ip: str) -> str | None:
    with _ONLINE_LOCK:
        entry = ONLINE.get(_save_key(name))
    if entry and entry[0] != client_ip and _now() - entry[1] < ONLINE_TIMEOUT:
        return entry[0]
    return None


def logout(client_ip: str) -> None:
    with _ONLINE_LOCK:
        for key in [k for k, (ip, _) in ONLINE.items() if ip == client_ip]:
            del ONLINE[key]


ACCOUNTS: AccountStore | None = None
TICKET_TTL = 3 * 3600
TICKETS: dict[str, tuple[str, str, float]] = {}
FAILED: dict[str, list[float]] = {}
FAIL_LIMIT, FAIL_WINDOW = 5, 600.0
_AUTH_LOCK = threading.Lock()


def credential(body: dict, auth: str) -> str:
    head, sep, ticket = auth.partition("t=")
    if not sep:
        return ""
    if head.strip().lower().startswith("psn2.0"):
        try:
            raw = base64.b64decode(ticket.strip() + "=" * (-len(ticket.strip()) % 4), validate=True).decode()
        except (binascii.Error, ValueError):
            return ""
        code = (urllib.parse.parse_qs(raw).get("code") or [""])[0]
        return "" if code.upper() == "OFFLINE" else code
    return ticket.partition("|")[2]


def ticket_owner(ticket: str) -> tuple[str, str] | None:
    with _AUTH_LOCK:
        entry = TICKETS.get(ticket)
    if entry is None or _now() - entry[2] >= TICKET_TTL:
        return None
    return entry[0], entry[1]


def _issue(ticket: str, name: str, platform: str) -> None:
    now = _now()
    with _AUTH_LOCK:
        for old in [t for t, (_, _, issued) in TICKETS.items() if now - issued >= TICKET_TTL]:
            del TICKETS[old]
        TICKETS[ticket] = (name, platform, now)


def _begin_attempt(client_ip: str) -> float | None:
    now = _now()
    with _AUTH_LOCK:
        recent = [t for t in FAILED.get(client_ip, []) if now - t < FAIL_WINDOW]
        if len(recent) >= FAIL_LIMIT:
            FAILED[client_ip] = recent
            return None
        recent.append(now)
        FAILED[client_ip] = recent
        return now


def _end_attempt(client_ip: str, stamp: float, failed: bool) -> None:
    if failed:
        return
    with _AUTH_LOCK:
        times = FAILED.get(client_ip, [])
        if stamp in times:
            times.remove(stamp)
        if not times:
            FAILED.pop(client_ip, None)


def loggable(reply: object) -> object:
    if ACCOUNTS is not None and isinstance(reply, dict) and ("ticket" in reply or "token" in reply):
        return {k: "***" if k in ("ticket", "token") else v for k, v in reply.items()}
    return reply


def _login_refused(client_ip: str, name: str, why: str) -> tuple[int, dict]:
    print(f"[{datetime.datetime.now():%H:%M:%S}] login {client_ip} refused: {name}: {why}", flush=True)
    return 409, {"error": f"{name}: {why}", "errorCode": 409}


def login_name(body: dict, auth: str = "") -> tuple[str, str]:
    name, platform = body.get("nameOnPlatform"), "psn"
    console = ACCOUNTS is not None and auth.strip().lower().startswith("psn2.0")
    if not name and not console and ticket_name(auth):
        name, platform = ticket_name(auth), "uplay"
    return name or "Player", platform


def session(body: dict, client_ip: str, auth: str = "") -> dict:
    name, platform = login_name(body, auth)
    touch(name, client_ip)
    SESSIONS[client_ip] = name
    PLATFORMS[client_ip] = platform
    profile = stable_id("profile", name)
    PROFILES[profile] = (name, platform)
    utc = datetime.datetime.now(datetime.timezone.utc)
    ticket = base64.b64encode(f"offline-ticket:{profile}".encode()).decode()
    token = base64.b64encode(f"offline-token:{profile}".encode()).decode()
    if ACCOUNTS is not None:
        ticket = base64.b64encode(f"offline-ticket:{uuid.uuid4()}".encode()).decode()
        token = base64.b64encode(f"offline-token:{uuid.uuid4()}".encode()).decode()
        _issue(ticket, ACCOUNTS.display_name(name) or name, platform)
    return {
        "platformType": platform,
        "ticket": ticket,
        "token": token,
        "profileId": profile,
        "userId": stable_id("user", name),
        "nameOnPlatform": name,
        "hasAcceptedLegalOptins": True,
        "spaceId": SPACE_ID,
        "productId": stable_id("product", "thecrew-ps4"),
        "environment": "Prod",
        "expiration": ubi_time(utc + datetime.timedelta(hours=3)),
        "serverTime": ubi_time(utc),
        "clientIp": client_ip,
        "initializeUser": False,
        "sessionId": str(uuid.uuid4()),
        "accountIssues": [],
    }


def configuration(host: str) -> dict:
    base = f"http://{host}"
    switches = [{"name": n, "value": True} for n in RESOURCE_NAMES]
    switches += [{"name": "SendPopulationsInPlayerStart", "value": False}, {"name": "Punch", "value": PUNCH},
                 {"name": "NATSOLVER_DEF_SOLVING_TIME_MS", "value": 30000}, {"name": "MAX_TIME_PROBES", "value": 0}]
    return {"configuration": {
        "platformConfig": {"uplayGameCode": "", "applicationId": APP_ID, "spaceId": SPACE_ID, "platform": "psn",
                           "environment": "Prod"},
        "resources": [{"name": n, "url": f"{base}/{{version}}", "friendlyName": n} for n in RESOURCE_NAMES],
        "legacyUrls": [],
        "sandboxes": [],
        "uplayServices": [{"name": "AvatarBaseUrl", "url": f"{base}/avatars"},
                          {"name": "DefaultAvatarUrl", "url": f"{base}/avatars/default_avatar.png"}],
        "punch": {"detectUrl1": f"{base}/detect", "detectUrl2": f"{base}/detect", "traversalUrl": f"{base}/traversal",
                  **PUNCH},
        "sdkConfig": {"timeoutSec": 30, "keepAliveTimeoutMin": 10, "ticketTTL": 3600, "lspPort": 1200,
                      "httpSafetySleepTime": 0},
        "featuresSwitches": switches,
        "gatewayResources": [{"name": n, "url": (f"ws://{host}" if n == "websocket/server" else base) + p,
                              "version": 1} for n, p in GATEWAY_RESOURCES],
    }}


def profiles(query: str) -> dict:
    ids = [i for k, v in urllib.parse.parse_qsl(query) if k == "profileId" for i in v.split(",") if i]
    found = []
    for pid in ids:
        if pid in PROFILES:
            name, platform = PROFILES[pid]
            found.append({"profileId": pid, "userId": stable_id("user", name), "platformType": platform,
                          "idOnPlatform": stable_id("user", name) if platform == "uplay" else name,
                          "nameOnPlatform": name})
    return {"profiles": found}


def route(method: str, path: str, body: dict, host: str, client: tuple[str, int],
          auth: str = "", raw: bytes = b"") -> tuple[int, object]:
    path, _, query = path.partition("?")
    path = path.rstrip("/")
    if path == "/version":
        client_version = dict(urllib.parse.parse_qsl(query)).get("client")
        if client_version:
            print(f"[{datetime.datetime.now():%H:%M:%S}] version {client[0]}: {client_version}, server {VERSION}",
                  flush=True)
        return 200, {"server": VERSION}
    if method == "GET" and path.endswith("/v2/profiles"):
        return 200, profiles(query)
    m = AVATAR_UPLOAD.fullmatch(path)
    if m and method == "PUT":
        if SESSIONS.get(client[0]) != m.group(1):
            return 403, {"error": "only the logged-in player may set its picture"}
        return (200, {"saved": True}) if avatar.save_upload(m.group(1), raw) else (400, {"error": "need 256x256 RGBA"})
    m = AVATAR_RGBA.fullmatch(path)
    if m:
        return 200, avatar.rgba_for(PROFILES.get(m.group(1), (None,))[0], int(m.group(2)), client[0])
    if path.startswith("/avatars/") and path.endswith(".png"):
        return 200, avatar.for_request(SESSIONS.get(client[0]), path, client[0])
    if path.endswith("/profiles/sessions"):
        if method == "DELETE":
            logout(client[0])
            return 200, {}
        if method != "POST":
            return 200, {}
        name, platform = login_name(body, auth)
        if ACCOUNTS is not None:
            from tcreborn.saves import DEFAULT_NAME, save_name
            if save_name(name).lower() == DEFAULT_NAME.lower():
                return _login_refused(client[0], "this name", "name not allowed, choose another")
            stamp = _begin_attempt(client[0])
            if stamp is None:
                return _login_refused(client[0], name, "too many failed logins, try again later")
            verdict = "unavailable"
            try:
                verdict = ACCOUNTS.check(name, platform, credential(body, auth))
            finally:
                _end_attempt(client[0], stamp, verdict == "wrong")
            if verdict == "wrong":
                return _login_refused(client[0], name, "wrong password")
            if verdict == "missing":
                return _login_refused(client[0], name, "no password set in reborn.ini" if platform == "uplay"
                                      else "PS4 patch too old, it sends no console key")
            if verdict == "unavailable":
                return _login_refused(client[0], name, "account store unavailable")
        other = online_elsewhere(name, client[0]) if name != "Player" else None
        if other:
            print(f"[{datetime.datetime.now():%H:%M:%S}] login {client[0]} refused: {name} is playing on {other}",
                  flush=True)
            return 409, {"error": f"{name} is already playing on another device", "errorCode": 409}
        return 200, session(body, client[0], auth)
    if "/applications/" in path and path.endswith("/configuration"):
        return 200, configuration(host)
    if path.endswith("/native/token"):
        return 200, {"access_token": base64.b64encode(b"offline-shop-token").decode(), "token_type": "bearer",
                     "expires_in": 3600}
    if path.endswith("/profiles/me/news"):
        return 200, {"news": []}
    if "configs/events" in path:
        return 200, {"config": {"hmacEnabled": False, "sendPeriodSeconds": 30}, "tags": EVENT_TAGS}
    if "populations" in path:
        return 200, {"spaceId": SPACE_ID, "name": "TheCrew", "populationSets": [{
            "populationId": stable_id("population", "default"), "platform": "PS4", "revisionId": 1,
            "currentPlayers": 1, "maxPlayers": 1000}]}
    if path.endswith("/detect"):
        return 200, {"publicIp": client[0], "publicPort": client[1], "natType": "open", "status": "ok"}
    if path.endswith("/traversal"):
        return 200, {"status": "ok", "traversalPort": 4000, "relayPort": 4000}
    if any(k in path for k in ("entities", "rewards", "actions")):
        return 200, {"results": []}
    return 200, {}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    log_path: str | None = None
    lock = threading.Lock()

    def version_string(self) -> str:
        return f"TheCrewReborn/{VERSION}"

    def _serve(self) -> None:
        raw = self.rfile.read(int(self.headers.get("Content-Length") or 0))
        try:
            body = json.loads(raw) if raw else {}
        except ValueError:
            body = {}
        host = self.request.getsockname()[0]
        auth = self.headers.get("Authorization", "")
        status, reply = route(self.command, self.path, body if isinstance(body, dict) else {}, host,
                              self.client_address, auth, raw)
        binary = isinstance(reply, bytes)
        payload = reply if binary else json.dumps(reply).encode()
        if binary:
            reply = f"<png {len(payload)} bytes>"
        print(f"[{datetime.datetime.now():%H:%M:%S}] http {self.client_address[0]} {self.command} "
              f"{self.path.split('?')[0]} -> {status}", flush=True)
        if self.log_path:
            with self.lock, open(self.log_path, "a") as f:
                f.write(json.dumps({"time": datetime.datetime.now().isoformat(), "client": self.client_address[0],
                                    "method": self.command, "path": self.path, "auth": redact(auth),
                                    "body": f"<rgba {len(raw)} bytes>" if self.path.endswith(".rgba") and raw
                                    else raw.decode("utf-8", "replace"), "status": status, "reply": loggable(reply)}) + "\n")
        self.send_response(status)
        kind = "application/octet-stream" if self.path.endswith(".rgba") else "image/png"
        self.send_header("Content-Type", kind if binary else "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    do_GET = do_POST = do_PUT = do_DELETE = _serve

    def log_message(self, fmt, *args) -> None:
        pass


class _Server(ThreadingHTTPServer):
    allow_reuse_address = True

    def handle_error(self, request, client_address):
        err = sys.exc_info()[1]
        if isinstance(err, (ConnectionError, ssl.SSLError, TimeoutError)):
            reason = getattr(err, "reason", None) or getattr(err, "strerror", None) or ""
            print(f"[{datetime.datetime.now():%H:%M:%S}] http {client_address[0]} {type(err).__name__} {reason}".rstrip(),
                  flush=True)
        else:
            super().handle_error(request, client_address)


def serve(port: int, log_path: str | None = None) -> ThreadingHTTPServer:
    Handler.log_path = log_path
    server = _Server(("0.0.0.0", port), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


class _TlsServer(_Server):
    context: ssl.SSLContext

    def get_request(self):
        sock, addr = self.socket.accept()
        return self.context.wrap_socket(sock, server_side=True, do_handshake_on_connect=False), addr


def serve_tls(port: int, cert: str, key: str, log_path: str | None = None) -> ThreadingHTTPServer:
    Handler.log_path = log_path
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, key)
    _TlsServer.context = ctx
    server = _TlsServer(("0.0.0.0", port), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server
