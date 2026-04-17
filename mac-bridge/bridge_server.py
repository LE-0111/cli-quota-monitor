#!/usr/bin/env python3
"""
Bridge server: reads local usage data for Claude Code, Codex CLI, and Gemini CLI,
serves it over HTTP for the ESP32 display.

Endpoints:
    GET /usage   -> JSON with claude + codex + gemini usage
    GET /health  -> {"ok": true}
"""

import argparse
import datetime
import glob
import json
import os
import time
import urllib.parse
import urllib.request
from http.server import HTTPServer, BaseHTTPRequestHandler

CLAUDE_CACHE = "/tmp/cc-display-claude.json"
CODEX_SESSIONS_DIR = os.path.expanduser("~/.codex/sessions")
GEMINI_DIR = os.path.expanduser("~/.gemini")
GEMINI_OAUTH_CREDS = os.path.join(GEMINI_DIR, "oauth_creds.json")

# Gemini OAuth refresh requires a client id and client secret.
# For open-source distribution they are not hard-coded here; provide them via
# environment variables if you want Gemini quota refresh to work:
#   GEMINI_OAUTH_CLIENT_ID
#   GEMINI_OAUTH_CLIENT_SECRET
GEMINI_OAUTH_CLIENT_ID = os.environ.get("GEMINI_OAUTH_CLIENT_ID", "")
GEMINI_OAUTH_CLIENT_SECRET = os.environ.get("GEMINI_OAUTH_CLIENT_SECRET", "")
GEMINI_OAUTH_TOKEN_URL = "https://oauth2.googleapis.com/token"
GEMINI_API_BASE = "https://cloudcode-pa.googleapis.com/v1internal"

# Which two models we surface on the device (flash -> 5h slot, pro -> week slot).
GEMINI_MODEL_PRIMARY = "gemini-2.5-flash"
GEMINI_MODEL_SECONDARY = "gemini-2.5-pro"
GEMINI_QUOTA_TTL_SEC = 60

FRESH_THRESHOLD_SEC = 6 * 3600   # data older than 6h => "STALE"


# =====================================================================
# Helpers
# =====================================================================

def _to_float(val, default=0.0):
    try:
        return float(val)
    except (TypeError, ValueError):
        return default


def _extract_reset(entry):
    if not isinstance(entry, dict):
        return 0
    for k in ("resetsAt", "resets_at", "resetAt", "reset_at"):
        if k in entry:
            try:
                v = entry[k]
                if isinstance(v, (int, float)):
                    return int(v)
                return int(v)  # already epoch
            except (TypeError, ValueError):
                return 0
    return 0


def _extract_pct(entry):
    if not isinstance(entry, dict):
        return 0.0
    for k in ("usedPercentage", "used_percentage", "utilization", "usage"):
        if k in entry:
            return _to_float(entry[k])
    return 0.0


def _plan_label(epoch, empty_label="NO DATA"):
    """
    Plan label shown to the device.
    Freshness is computed on-device from last_update, so this field is purely
    the subscription tier. Hard-coded to 'Pro' when data is present.
    """
    if not epoch:
        return empty_label
    return "Pro"


def _empty_service():
    return {
        "five_hour": {"used_percentage": 0, "resets_at": 0},
        "seven_day": {"used_percentage": 0, "resets_at": 0},
        "plan": "NO DATA",
        "last_update": 0,
    }


# =====================================================================
# Claude Code — via OpenIsland statusLine cache
# =====================================================================

def read_claude_usage():
    svc = _empty_service()
    try:
        stat = os.stat(CLAUDE_CACHE)
        last_update = int(stat.st_mtime)

        with open(CLAUDE_CACHE, "r") as f:
            data = json.loads(f.read().strip())

        for key in ("fiveHour", "five_hour"):
            if key in data:
                svc["five_hour"] = {
                    "used_percentage": _extract_pct(data[key]),
                    "resets_at": _extract_reset(data[key]),
                }
                break

        for key in ("sevenDay", "seven_day"):
            if key in data:
                svc["seven_day"] = {
                    "used_percentage": _extract_pct(data[key]),
                    "resets_at": _extract_reset(data[key]),
                }
                break

        svc["last_update"] = last_update
        svc["plan"] = _plan_label(last_update)
    except (FileNotFoundError, json.JSONDecodeError, KeyError, OSError):
        pass
    return svc


# =====================================================================
# Codex CLI — via rollout JSONL files
# =====================================================================

def read_codex_usage():
    """
    Walk Codex rollout JSONL files newest-first, looking for the most
    recent rate_limits event. Codex writes these inside token_count events:
        {"payload": {"type": "token_count",
                     "rate_limits": {"primary": {...}, "secondary": {...},
                                     "plan_type": "plus"}}}
    primary  = 5-hour window (window_minutes=300)
    secondary= weekly window (window_minutes=10080)
    """
    svc = _empty_service()
    try:
        pattern = os.path.join(CODEX_SESSIONS_DIR, "**", "rollout-*.jsonl")
        files = glob.glob(pattern, recursive=True)
        if not files:
            return svc

        files.sort(key=os.path.getmtime, reverse=True)

        for fpath in files[:10]:
            parsed = _parse_codex_rollout(fpath)
            if parsed:
                svc["five_hour"] = parsed.get("five_hour", svc["five_hour"])
                svc["seven_day"] = parsed.get("seven_day", svc["seven_day"])
                svc["last_update"] = parsed.get("mtime", int(os.path.getmtime(fpath)))
                plan_type = parsed.get("plan_type")
                if plan_type:
                    svc["plan"] = plan_type.capitalize()
                else:
                    svc["plan"] = _plan_label(svc["last_update"])
                return svc
    except Exception:
        pass
    return svc


def _codex_window(win):
    if not isinstance(win, dict):
        return None
    pct = win.get("used_percent", win.get("used_percentage"))
    resets = win.get("resets_at", win.get("resetsAt"))
    return {
        "used_percentage": _to_float(pct),
        "resets_at": int(resets) if isinstance(resets, (int, float)) else 0,
    }


def _parse_codex_rollout(filepath):
    """
    Return the *latest* rate_limits snapshot in the file (events accumulate,
    only the newest reflects current quota).
    """
    result = {}
    try:
        with open(filepath, "r") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    entry = json.loads(line)
                except json.JSONDecodeError:
                    continue

                payload = entry.get("payload") or {}
                rl = payload.get("rate_limits") or entry.get("rate_limits")
                if not isinstance(rl, dict):
                    continue

                snap = {}
                five = _codex_window(rl.get("primary"))
                if five:
                    snap["five_hour"] = five
                week = _codex_window(rl.get("secondary"))
                if week:
                    snap["seven_day"] = week
                if rl.get("plan_type"):
                    snap["plan_type"] = rl["plan_type"]

                ts = entry.get("timestamp")
                if isinstance(ts, str):
                    try:
                        import datetime
                        dt = datetime.datetime.fromisoformat(ts.replace("Z", "+00:00"))
                        snap["mtime"] = int(dt.timestamp())
                    except Exception:
                        pass

                if snap:
                    result = snap  # keep latest
    except Exception:
        pass
    return result


# =====================================================================
# Gemini CLI — via Google Code Assist internal API (same endpoint the CLI uses)
# =====================================================================
#
# Gemini CLI itself calls:
#   POST https://cloudcode-pa.googleapis.com/v1internal:retrieveUserQuota
#   Body: {"project": "<cloudaicompanionProject>"}
#   Auth: Bearer <OAuth access_token from ~/.gemini/oauth_creds.json>
#
# Response: {buckets: [{modelId, tokenType, remainingFraction, resetTime}, ...]}
# Free tier only exposes remainingFraction (no remainingAmount), so we report
# used% = (1 - remainingFraction) * 100.

_gemini_project_cache = {"id": None, "fetched_at": 0.0}
_gemini_quota_cache = {"svc": None, "fetched_at": 0.0}


def _gemini_load_creds():
    if not os.path.isfile(GEMINI_OAUTH_CREDS):
        return None
    try:
        with open(GEMINI_OAUTH_CREDS, "r") as f:
            return json.load(f)
    except (json.JSONDecodeError, OSError):
        return None


def _gemini_save_creds(creds):
    try:
        with open(GEMINI_OAUTH_CREDS, "w") as f:
            json.dump(creds, f, indent=2)
    except OSError:
        pass


def _gemini_ensure_token():
    """Return a non-expired access_token, refreshing via refresh_token if needed."""
    creds = _gemini_load_creds()
    if not creds or not creds.get("refresh_token"):
        return None
    if not GEMINI_OAUTH_CLIENT_ID or not GEMINI_OAUTH_CLIENT_SECRET:
        return None

    now_ms = int(time.time() * 1000)
    if creds.get("expiry_date", 0) - now_ms > 60_000:
        return creds.get("access_token")

    body = urllib.parse.urlencode({
        "client_id": GEMINI_OAUTH_CLIENT_ID,
        "client_secret": GEMINI_OAUTH_CLIENT_SECRET,
        "refresh_token": creds["refresh_token"],
        "grant_type": "refresh_token",
    }).encode()
    req = urllib.request.Request(
        GEMINI_OAUTH_TOKEN_URL,
        data=body,
        headers={"Content-Type": "application/x-www-form-urlencoded"},
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            tok = json.loads(resp.read())
    except (urllib.error.URLError, urllib.error.HTTPError, json.JSONDecodeError, TimeoutError):
        return None

    creds["access_token"] = tok["access_token"]
    creds["expiry_date"] = now_ms + int(tok.get("expires_in", 3600)) * 1000
    _gemini_save_creds(creds)
    return creds["access_token"]


def _gemini_api_post(path, token, payload, timeout=10):
    body = json.dumps(payload).encode()
    req = urllib.request.Request(
        f"{GEMINI_API_BASE}:{path}",
        data=body,
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read())


def _gemini_get_project_id(token):
    cached = _gemini_project_cache
    if cached["id"] and time.time() - cached["fetched_at"] < 3600:
        return cached["id"]
    try:
        res = _gemini_api_post("loadCodeAssist", token, {
            "metadata": {
                "ideType": "IDE_UNSPECIFIED",
                "platform": "PLATFORM_UNSPECIFIED",
                "pluginType": "GEMINI",
            }
        })
    except Exception:
        return None
    pid = res.get("cloudaicompanionProject")
    if pid:
        cached["id"] = pid
        cached["fetched_at"] = time.time()
    return pid


def _iso_to_epoch(iso_str):
    if not iso_str:
        return 0
    try:
        dt = datetime.datetime.fromisoformat(iso_str.replace("Z", "+00:00"))
        return int(dt.timestamp())
    except ValueError:
        return 0


def _gemini_bucket(buckets, model_id):
    for b in buckets:
        if b.get("modelId") == model_id and b.get("tokenType", "REQUESTS") == "REQUESTS":
            return b
    return None


def read_gemini_usage():
    cached = _gemini_quota_cache
    if cached["svc"] and time.time() - cached["fetched_at"] < GEMINI_QUOTA_TTL_SEC:
        return cached["svc"]

    svc = _empty_service()
    token = _gemini_ensure_token()
    if not token:
        return svc

    project_id = _gemini_get_project_id(token)
    if not project_id:
        return svc

    try:
        data = _gemini_api_post("retrieveUserQuota", token, {"project": project_id})
    except Exception:
        return svc

    buckets = data.get("buckets") or []
    flash = _gemini_bucket(buckets, GEMINI_MODEL_PRIMARY)
    pro = _gemini_bucket(buckets, GEMINI_MODEL_SECONDARY)

    if flash:
        svc["five_hour"] = {
            "used_percentage": round((1 - flash.get("remainingFraction", 0)) * 100, 1),
            "resets_at": _iso_to_epoch(flash.get("resetTime", "")),
        }
    if pro:
        svc["seven_day"] = {
            "used_percentage": round((1 - pro.get("remainingFraction", 0)) * 100, 1),
            "resets_at": _iso_to_epoch(pro.get("resetTime", "")),
        }

    if flash or pro:
        svc["last_update"] = int(time.time())
        svc["plan"] = "Free"

    cached["svc"] = svc
    cached["fetched_at"] = time.time()
    return svc


# =====================================================================
# HTTP server
# =====================================================================

class BridgeHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/usage":
            self._serve_usage()
        elif self.path == "/health":
            self._send_json({"ok": True})
        else:
            self.send_error(404)

    def _serve_usage(self):
        payload = {
            "claude": read_claude_usage(),
            "codex":  read_codex_usage(),
            "gemini": read_gemini_usage(),
            "timestamp": int(time.time()),
        }
        self._send_json(payload)

    def _send_json(self, obj):
        body = json.dumps(obj).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        return


def main():
    parser = argparse.ArgumentParser(description="CC Display Bridge Server")
    parser.add_argument("--port", type=int, default=8899)
    args = parser.parse_args()

    server = HTTPServer(("0.0.0.0", args.port), BridgeHandler)
    print(f"Bridge server listening on 0.0.0.0:{args.port}", flush=True)
    print(f"  Claude cache:  {CLAUDE_CACHE}", flush=True)
    print(f"  Codex dir:     {CODEX_SESSIONS_DIR}", flush=True)
    print(f"  Gemini dir:    {GEMINI_DIR}", flush=True)
    print(f"  Endpoint:      http://localhost:{args.port}/usage", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
