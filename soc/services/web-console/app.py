"""
ZT-SOC admin web console.

A thin Flask + HTMX UI in front of the incident-service REST API.
- Lists incidents and pending actions.
- Lets an admin approve/reject NetworkPolicy actions (Layer-1).
- Lets an admin schedule a follow-up delete_pod / shutdown_pod (Layer-2).

Auth: Basic HTTP. Credentials come from env (ADMIN_USERNAME / ADMIN_PASSWORD).
The console forwards requests to incident-service using a server-side admin
token (RBAC_ADMIN_TOKEN) — the browser never holds the API token.
"""

from __future__ import annotations

import os
from functools import wraps

import requests
from flask import Flask, Response, abort, redirect, render_template, request

# ─── Configuration ────────────────────────────────────────────────────────

INCIDENT_API = os.environ.get(
    "INCIDENT_API_URL", "http://zt-incident-service.soc.svc.cluster.local:8080"
)
ADMIN_TOKEN = os.environ.get("RBAC_ADMIN_TOKEN", "admin-demo-token")
BASIC_USER = os.environ.get("ADMIN_USERNAME", "admin")
BASIC_PASS = os.environ.get("ADMIN_PASSWORD", "admin")

REQUEST_TIMEOUT = 5  # seconds

app = Flask(__name__)


# ─── Helpers ──────────────────────────────────────────────────────────────


def _require_basic_auth(view):
    @wraps(view)
    def wrapper(*args, **kwargs):
        auth = request.authorization
        if not auth or auth.username != BASIC_USER or auth.password != BASIC_PASS:
            return Response(
                "Authentication required",
                401,
                {"WWW-Authenticate": 'Basic realm="ZT-SOC Console"'},
            )
        return view(*args, **kwargs)

    return wrapper


def _api(method: str, path: str, **kwargs) -> requests.Response:
    """Call the incident-service REST API as the SOC admin token bearer."""
    url = INCIDENT_API.rstrip("/") + path
    headers = kwargs.pop("headers", {})
    headers["Authorization"] = f"Bearer {ADMIN_TOKEN}"
    return requests.request(method, url, headers=headers, timeout=REQUEST_TIMEOUT, **kwargs)


# ─── Routes ───────────────────────────────────────────────────────────────


@app.get("/health")
def health():
    return "ok"


@app.get("/")
@_require_basic_auth
def index():
    incidents = _api("GET", "/api/v1/incidents?limit=50").json() or []
    actions = _api("GET", "/api/v1/actions?status=pending&limit=50").json() or []
    return render_template("incidents.html", incidents=incidents, actions=actions)


@app.get("/incidents/<int:inc_id>")
@_require_basic_auth
def incident_detail(inc_id: int):
    inc_resp = _api("GET", f"/api/v1/incidents/{inc_id}")
    if inc_resp.status_code == 404:
        abort(404)
    inc = inc_resp.json()
    audit = _api("GET", f"/api/v1/incidents/{inc_id}/audit?limit=100").json() or []
    actions = _api("GET", "/api/v1/actions?limit=200").json() or []
    related_actions = [a for a in actions if a.get("incident_id") == inc_id]
    return render_template(
        "incident_detail.html",
        incident=inc,
        actions=related_actions,
        audit=audit,
    )


@app.post("/actions/<int:action_id>/approve")
@_require_basic_auth
def approve_action(action_id: int):
    resp = _api("POST", f"/api/v1/actions/{action_id}/approve")
    return _htmx_action_row(resp.json())


@app.post("/actions/<int:action_id>/reject")
@_require_basic_auth
def reject_action(action_id: int):
    resp = _api("POST", f"/api/v1/actions/{action_id}/reject")
    return _htmx_action_row({"action": resp.json()})


@app.get("/reports")
@_require_basic_auth
def reports_index():
    week = request.args.get("week", "")
    params = {"format": "json"}
    if week:
        params["week"] = week
    resp = _api("GET", "/api/v1/reports/weekly", params=params)
    if resp.status_code != 200:
        return f"<div class='error'>API error {resp.status_code}: {resp.text}</div>", 400
    return render_template("reports.html", report=resp.json(), week=week)


@app.post("/reports/generate")
@_require_basic_auth
def reports_generate():
    body = {"week": request.form.get("week", ""), "upload": True}
    resp = _api("POST", "/api/v1/reports/weekly", json=body)
    if resp.status_code >= 400:
        return f"<div class='error'>{resp.status_code}: {resp.text}</div>", 400
    payload = resp.json()
    uploaded = payload.get("uploaded_to") or "(not uploaded — MinIO sink disabled)"
    return (
        f"<div class='ok'>Generated week <b>{payload.get('week')}</b>.<br>"
        f"Uploaded to: <code>{uploaded}</code></div>"
    )


@app.get("/audit/export")
@_require_basic_auth
def audit_export_redirect():
    # Browser → incident-service, carrying the admin token as Bearer via
    # query forwarded by the gateway. For simplicity we proxy through.
    qs = request.query_string.decode()
    suffix = ("?" + qs) if qs else ""
    return redirect(f"/audit/export/proxy{suffix}", code=302)


@app.get("/audit/export/proxy")
@_require_basic_auth
def audit_export_proxy():
    resp = _api("GET", "/api/v1/audit/export", params=request.args, stream=True)
    headers = {
        "Content-Type": resp.headers.get("Content-Type", "text/csv"),
        "Content-Disposition": resp.headers.get(
            "Content-Disposition", 'attachment; filename="audit.csv"'),
        "X-Export-Total-Rows": resp.headers.get("X-Export-Total-Rows", "0"),
    }
    return Response(resp.iter_content(chunk_size=8192), status=resp.status_code, headers=headers)


@app.post("/incidents/<int:inc_id>/follow-up")
@_require_basic_auth
def follow_up(inc_id: int):
    body = {
        "type": request.form.get("type", "delete_pod"),
        "namespace": request.form.get("namespace", ""),
        "pod_name": request.form.get("pod_name", ""),
        "reason": request.form.get("reason", ""),
    }
    resp = _api("POST", f"/api/v1/incidents/{inc_id}/follow-up-actions", json=body)
    if resp.status_code >= 400:
        return f"<div class='error'>API error {resp.status_code}: {resp.text}</div>", 400
    return render_template("_action_row.html", a=resp.json())


def _htmx_action_row(payload: dict):
    a = payload.get("action") or payload
    return render_template("_action_row.html", a=a, applied_to=payload.get("applied_to"))


if __name__ == "__main__":
    # Dev only; production uses gunicorn (see Dockerfile CMD).
    app.run(host="0.0.0.0", port=5000, debug=True)
