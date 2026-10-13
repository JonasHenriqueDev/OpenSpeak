#!/usr/bin/env python3
"""Linux host metrics + native OpenSpeak snapshot. Bind only to loopback behind Caddy auth."""
import datetime as dt
import json
import os
import re
from pathlib import Path
import shutil
import sqlite3
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parent
STATE = Path(os.environ.get('STATE_DIRECTORY', '/var/lib/openspeak-monitor'))
SNAPSHOT = Path(os.environ.get('OPENSPEAK_METRICS_FILE', '/run/openspeak/metrics.json'))
TZ = ZoneInfo(os.environ.get('MONITOR_TIMEZONE', 'America/Sao_Paulo'))
INTERVAL = 5
RETENTION = 7 * 86400
lock = threading.Lock()
latest = {}


def network_counters():
    # Physical/virtual NICs only: avoid counting loopback, bridges and tunnels twice.
    counters = {}
    for interface in Path('/sys/class/net').iterdir():
        if interface.name == 'lo' or not (interface / 'device').exists():
            continue
        counters[interface.name] = tuple(int((interface / 'statistics' / f'{d}_bytes').read_text())
                                         for d in ('rx', 'tx'))
    return counters


def counter_delta(previous, current):
    return tuple(sum(max(0, values[i] - previous[name][i]) for name, values in current.items()
                     if name in previous) for i in (0, 1))


def cpu_counters():
    values = list(map(int, Path('/proc/stat').read_text().splitlines()[0].split()[1:9]))
    return sum(values), values[3] + values[4]


def memory():
    fields = {line.split(':')[0]: int(line.split()[1]) * 1024
              for line in Path('/proc/meminfo').read_text().splitlines() if len(line.split()) >= 2}
    return {'total': fields['MemTotal'], 'used': fields['MemTotal'] - fields['MemAvailable'],
            'swapTotal': fields['SwapTotal'], 'swapUsed': fields['SwapTotal'] - fields['SwapFree']}


def room_snapshot():
    try:
        data = json.loads(SNAPSHOT.read_text())
        age = max(0, time.time() - data['timestamp'] / 1000)
        online = age < 10 and Path(f"/proc/{int(data['pid'])}").exists()
        if not online:
            return {'online': False, 'ageSeconds': age, 'clients': [], 'streams': []}
        return {**data, 'online': True, 'ageSeconds': age}
    except (OSError, ValueError, KeyError, TypeError):
        return {'online': False, 'ageSeconds': None, 'clients': [], 'streams': []}


def database():
    db = sqlite3.connect(STATE / 'metrics.sqlite', timeout=15)
    db.execute('PRAGMA journal_mode=WAL')
    return db


def identify_client(client):
    info, agent = client.get('clientInfo', {}), client.get('userAgent', '')
    if info.get('kind') == 'native':
        return {'name': info.get('name', 'OpenSpeak') + ' nativo', 'version': info.get('version', ''),
                'platform': info.get('platform', ''), 'source': 'Informado pelo cliente'}
    platform = ('Android' if 'Android' in agent else 'iOS' if re.search('iPhone|iPad|iPod', agent)
                else 'Windows' if 'Windows' in agent else 'macOS' if 'Macintosh' in agent
                else 'Linux' if 'Linux' in agent else '')
    for name, pattern in [('Edge', r'(?:Edg|EdgA|EdgiOS)/([\d.]+)'), ('Opera', r'(?:OPR|OPiOS)/([\d.]+)'),
                          ('Firefox', r'(?:Firefox|FxiOS)/([\d.]+)'), ('Chrome', r'(?:Chrome|CriOS)/([\d.]+)'),
                          ('Safari', r'Version/([\d.]+).*Safari')]:
        found = re.search(pattern, agent)
        if found:
            return {'name': 'Web · ' + name, 'version': found.group(1), 'platform': platform,
                    'source': 'Identificado pelo User-Agent'}
    if 'Qt' in agent or (not agent and not client.get('origin')):
        return {'name': 'App / cliente não identificado', 'version': '', 'platform': platform,
                'source': 'Sem identificação de versão; pode ser um app nativo anterior'}
    return {'name': 'Web / cliente não identificado', 'version': '', 'platform': platform,
            'source': 'User-Agent não reconhecido'}


class ClientMetrics:
    """Rates use native snapshot timestamps, not collector timings or configured bitrates."""
    def __init__(self):
        self.previous = {}

    def enrich(self, room):
        current = {}
        timestamp = room.get('timestamp', 0) / 1000
        pid = room.get('pid')

        def rates(key, rx, tx):
            old = self.previous.get(key)
            current[key] = (timestamp, rx, tx)
            if not old or timestamp <= old[0] or rx < old[1] or tx < old[2]:
                return None, None
            seconds = timestamp - old[0]
            return (rx - old[1]) / seconds, (tx - old[2]) / seconds

        clients = {c['clid']: c for c in room['clients']}
        for clid, client in clients.items():
            client['client'] = identify_client(client)
            client['activities'] = []
            client['connectedSeconds'] = max(0, timestamp - client.get('connectedAt', timestamp * 1000) / 1000)
            client['signalingRxBytesPerSecond'], client['signalingTxBytesPerSecond'] = rates(
                (pid, clid, 'signal'), client.get('signalingRxBytes', 0), client.get('signalingTxBytes', 0))

        sessions = {(s['streamId'], s['clid'], s['role']): s for s in room.get('mediaSessions', [])}
        for stream in room['streams']:
            participants = [(stream['ownerClid'], 'publisher')] + [(clid, 'viewer') for clid in stream.get('viewerClids', [])]
            for clid, role in participants:
                if clid not in clients:
                    continue
                activity = {'streamId': stream['id'], 'name': stream['name'], 'role': role,
                            'mode': stream['mode'], 'paused': stream['paused'], 'width': stream.get('width'),
                            'height': stream.get('height'), 'fps': stream.get('fps'), 'audio': stream.get('audio')}
                session = sessions.get((stream['id'], clid, role))
                if session:
                    activity.update(session)
                    activity['uploadBytesPerSecond'], activity['downloadBytesPerSecond'] = rates(
                        (pid, stream['id'], clid, role), session['mediaRxBytes'], session['mediaTxBytes'])
                clients[clid]['activities'].append(activity)

        for client in clients.values():
            sfu = [a for a in client['activities'] if a['mode'] == 2]
            measured = [a for a in sfu if a.get('uploadBytesPerSecond') is not None]
            client['hasP2P'] = any(a['mode'] == 1 for a in client['activities'])
            client['sfuUploadBytesPerSecond'] = sum(a['uploadBytesPerSecond'] for a in measured) if len(measured) == len(sfu) else None
            client['sfuDownloadBytesPerSecond'] = sum(a['downloadBytesPerSecond'] for a in measured) if len(measured) == len(sfu) else None
            client['sfuUploadBytes'] = sum(a.get('mediaRxBytes', 0) for a in sfu)
            client['sfuDownloadBytes'] = sum(a.get('mediaTxBytes', 0) for a in sfu)
        self.previous = current
        return room


def initialize():
    STATE.mkdir(parents=True, exist_ok=True)
    with database() as db:
        db.execute('CREATE TABLE IF NOT EXISTS samples (ts REAL PRIMARY KEY, rx REAL, tx REAL, cpu REAL, memory INTEGER, clients INTEGER, streams INTEGER, online INTEGER)')
        db.execute('CREATE TABLE IF NOT EXISTS totals (day TEXT PRIMARY KEY, rx INTEGER, tx INTEGER)')
        db.execute('CREATE TABLE IF NOT EXISTS metadata (key TEXT PRIMARY KEY, value TEXT)')
        db.execute('INSERT OR IGNORE INTO metadata VALUES (?, ?)', ('started', dt.datetime.now(TZ).isoformat()))


def collect():
    global latest
    previous_net, previous_cpu, previous_time = network_counters(), cpu_counters(), time.monotonic()
    client_metrics = ClientMetrics()
    prune_at = 0
    while True:
        try:
            now = time.time()
            tick = time.monotonic()
            elapsed = max(.1, tick - previous_time)
            net, cpu = network_counters(), cpu_counters()
            rx, tx = counter_delta(previous_net, net)
            cpu_total = cpu[0] - previous_cpu[0]
            cpu_percent = max(0, min(100, 100 * (1 - (cpu[1] - previous_cpu[1]) / max(1, cpu_total))))
            room, mem, disk = room_snapshot(), memory(), shutil.disk_usage('/')
            room = client_metrics.enrich(room)
            day = dt.datetime.fromtimestamp(now, TZ).date().isoformat()
            with database() as db:
                db.execute('INSERT INTO samples VALUES (?,?,?,?,?,?,?,?)',
                           (now, rx / elapsed, tx / elapsed, cpu_percent, mem['used'],
                            len(room['clients']), len(room['streams']), int(room['online'])))
                db.execute('INSERT INTO totals VALUES (?,?,?) ON CONFLICT(day) DO UPDATE SET rx=rx+excluded.rx, tx=tx+excluded.tx', (day, rx, tx))
                today = db.execute('SELECT rx,tx FROM totals WHERE day=?', (day,)).fetchone()
                month = db.execute('SELECT COALESCE(SUM(rx),0),COALESCE(SUM(tx),0) FROM totals WHERE day LIKE ?', (day[:7] + '%',)).fetchone()
                started = db.execute("SELECT value FROM metadata WHERE key='started'").fetchone()[0]
                if now - prune_at > 3600:
                    db.execute('DELETE FROM samples WHERE ts < ?', (now - RETENTION,))
                    prune_at = now
            result = {'timestamp': now, 'hostname': os.uname().nodename, 'room': room,
                      'cpuPercent': cpu_percent, 'cores': os.cpu_count(), 'load': os.getloadavg()[0],
                      'memory': mem, 'disk': {'total': disk.total, 'used': disk.used},
                      'uptimeSeconds': float(Path('/proc/uptime').read_text().split()[0]),
                      'network': {'rxBytesPerSecond': rx / elapsed, 'txBytesPerSecond': tx / elapsed,
                                  'interfaces': list(net), 'todayRx': today[0], 'todayTx': today[1],
                                  'monthRx': month[0], 'monthTx': month[1]},
                      'collectionStarted': started, 'timezone': str(TZ)}
            with lock:
                latest = result
            previous_net, previous_cpu, previous_time = net, cpu, tick
        except Exception as exc:
            print(f'Collection failed: {type(exc).__name__}: {exc}', flush=True)
        time.sleep(INTERVAL)


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        url = urlsplit(self.path)
        if url.path in ('/monitor', '/monitor/'):
            self.reply(200, (ROOT / 'index.html').read_bytes(), 'text/html; charset=utf-8')
        elif url.path in ('/monitor/dashboard.js', '/monitor/participants.js'):
            self.reply(200, (ROOT / url.path.rsplit('/', 1)[1]).read_bytes(), 'text/javascript; charset=utf-8')
        elif url.path == '/monitor/api/status':
            with lock:
                data = dict(latest)
            if not data or time.time() - data['timestamp'] > 20:
                self.reply(503, b'{"error":"Coleta indisponivel"}', 'application/json')
            else:
                self.reply(200, json.dumps(data).encode(), 'application/json')
        elif url.path == '/monitor/api/history':
            hours = parse_qs(url.query).get('hours', ['1'])[0]
            if hours not in ('1', '24', '168'):
                self.reply(400, b'Invalid range', 'text/plain')
                return
            duration = int(hours) * 3600
            bucket = max(5, duration // 360)
            with database() as db:
                rows = db.execute('SELECT AVG(ts),AVG(rx),AVG(tx),AVG(cpu),AVG(clients),AVG(streams) FROM samples WHERE ts>=? GROUP BY CAST(ts / ? AS INTEGER) ORDER BY AVG(ts)',
                                  (time.time() - duration, bucket)).fetchall()
            self.reply(200, json.dumps({'points': rows}).encode(), 'application/json')
        else:
            self.reply(404, b'Not found', 'text/plain')

    def reply(self, status, body, content_type):
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('X-Frame-Options', 'DENY')
        self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


if __name__ == '__main__':
    initialize()
    threading.Thread(target=collect, daemon=True).start()
    ThreadingHTTPServer(('127.0.0.1', int(os.environ.get('PORT', '9990'))), Handler).serve_forever()
