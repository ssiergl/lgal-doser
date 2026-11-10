# Mock of /api/v1/mqtt (settings, status, CA). Host names steer the simulated
# connection: "...fail..." -> dns error, "...auth..." -> auth error,
# "...slow..." -> stays connecting; everything else connects after 2 s.
import re, time

CFG = {"enabled": False, "host": "", "port": 0, "tls": "off", "username": "", "password": "",
       "base_topic": "", "discovery": True, "discovery_prefix": "homeassistant", "control": False}
CA = {"pem": ""}
ST = {"since": time.time(), "connects": 0, "saved": time.time()}
HOST = re.compile(r"^(?=.{1,64}$)([A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?)(\.[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*$")

def _topic_ok(t, mx):
    return 0 < len(t) <= mx and not t.startswith("/") and not t.endswith("/") and "//" not in t and \
        all(33 <= ord(c) < 127 and c not in "+#" for c in t)

def _err(h, code, field=None, status=400):
    e = {"code": code, "message": field or code}
    if field: e["field"] = field
    h.send(status, {"error": e})

def _status():
    c = CFG
    if not c["enabled"]: return {"state": "disabled", "error": None}
    h = c["host"]
    if "fail" in h: return {"state": "connecting", "error": "dns"}
    if "auth" in h: return {"state": "connecting", "error": "auth"}
    if "slow" in h or time.time() - ST["saved"] < 2: return {"state": "connecting", "error": None}
    if c["tls"] == "custom" and not CA["pem"]: return {"state": "connecting", "error": "tls"}
    return {"state": "connected", "error": None}

def _doc():
    s = {k: v for k, v in CFG.items() if k != "password"}
    s["password_set"] = bool(CFG["password"])
    port = CFG["port"] or (1883 if CFG["tls"] == "off" else 8883)
    st = _status(); st["since_s"] = int(time.time() - ST["saved"]); st["connects"] = ST["connects"]
    ca = {"present": bool(CA["pem"])}
    if CA["pem"]:
        ca.update(count=CA["pem"].count("-----BEGIN CERTIFICATE-----"), subject="Test Root CA", valid_to="2036-11-07")
    return {"settings": s, "base_topic": CFG["base_topic"] or "lg-aquatic-life/doser/lgal-doser-e4f5", "port": port, "status": st, "ca": ca}

def route(h, method, path, q, body):
    if path == "/api/v1/mqtt":
        if method == "GET": h.send(200, _doc()); return True
        if method == "PUT":
            if not isinstance(body, dict): _err(h, "invalid_param", "body"); return True
            c = dict(CFG)
            for k in ("enabled", "discovery", "control"):
                if k in body:
                    if not isinstance(body[k], bool): _err(h, "invalid_param", k); return True
                    c[k] = body[k]
            for k, mx in (("host", 64), ("username", 64), ("password", 64), ("base_topic", 64), ("discovery_prefix", 32)):
                if k in body:
                    if not isinstance(body[k], str) or len(body[k]) > mx: _err(h, "invalid_param", k); return True
                    c[k] = body[k]
            if "port" in body:
                if not isinstance(body["port"], int) or isinstance(body["port"], bool) or not 0 <= body["port"] <= 65535:
                    _err(h, "invalid_param", "port"); return True
                c["port"] = body["port"]
            if "tls" in body:
                if body["tls"] not in ("off", "bundle", "custom"): _err(h, "invalid_param", "tls"); return True
                c["tls"] = body["tls"]
            if not c["username"]: c["password"] = ""
            if c["host"] and not HOST.match(c["host"]): _err(h, "invalid_param", "host"); return True
            if c["enabled"] and not c["host"]: _err(h, "invalid_param", "host"); return True
            if c["base_topic"] and not _topic_ok(c["base_topic"], 64): _err(h, "invalid_param", "base_topic"); return True
            if not _topic_ok(c["discovery_prefix"], 32): _err(h, "invalid_param", "discovery_prefix"); return True
            if c["enabled"] and c["tls"] == "custom" and not CA["pem"]: _err(h, "ca_missing", "tls"); return True
            CFG.update(c); ST["saved"] = time.time()
            if c["enabled"]: ST["connects"] += 1
            h.send(200, _doc()); return True
    if path == "/api/v1/mqtt/ca":
        if method == "PUT":
            pem = body if isinstance(body, str) else ""
            if "-----BEGIN CERTIFICATE-----" not in pem or "-----END CERTIFICATE-----" not in pem or len(pem) >= 8192:
                h.send(400, {"error": {"code": "invalid_certificate", "message": "No valid PEM certificate"}}); return True
            CA["pem"] = pem; ST["saved"] = time.time(); h.send(200, _doc()); return True
        if method == "DELETE":
            if CFG["enabled"] and CFG["tls"] == "custom":
                h.send(409, {"error": {"code": "ca_in_use", "message": "x"}}); return True
            CA["pem"] = ""; h.send(200, _doc()); return True
    return False
