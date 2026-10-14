import importlib.util
import json
from pathlib import Path
import tempfile
import time
import unittest

spec = importlib.util.spec_from_file_location('monitor', Path(__file__).with_name('monitor.py'))
monitor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(monitor)


class MonitoringTests(unittest.TestCase):
    def test_browser_and_native_identification(self):
        edge = monitor.identify_client({'userAgent': 'Mozilla/5.0 (Windows NT 10.0) Chrome/140.0 Safari/537.36 Edg/140.0'})
        self.assertEqual(edge['name'], 'Web · Edge')
        self.assertEqual(edge['platform'], 'Windows')
        phone = monitor.identify_client({'userAgent': 'Mozilla/5.0 (Linux; Android 14) Chrome/140.0 Mobile Safari/537.36'})
        self.assertEqual(phone['platform'], 'Android')
        native = monitor.identify_client({'clientInfo': {'kind': 'native', 'name': 'OpenSpeak', 'version': '0.2.7', 'platform': 'Windows 11'}})
        self.assertEqual(native['name'], 'OpenSpeak nativo')
        self.assertEqual(native['version'], '0.2.7')
        self.assertIn('não identificado', monitor.identify_client({})['name'])

    def test_per_user_media_rates_membership_and_restart(self):
        def snapshot(timestamp, uploaded, downloaded, pid=1):
            return {'pid': pid, 'timestamp': timestamp, 'clients': [{'clid': 1}, {'clid': 2}],
                    'streams': [{'id': 's1', 'name': 'Tela', 'ownerClid': 1, 'viewerClids': [2], 'mode': 2, 'paused': False},
                                {'id': 's2', 'name': 'P2P', 'ownerClid': 1, 'viewerClids': [2], 'mode': 1, 'paused': False}],
                    'mediaSessions': [{'clid': 1, 'streamId': 's1', 'role': 'publisher', 'mediaRxBytes': uploaded, 'mediaTxBytes': 0},
                                      {'clid': 2, 'streamId': 's1', 'role': 'viewer', 'mediaRxBytes': 0, 'mediaTxBytes': downloaded}]}
        metrics = monitor.ClientMetrics()
        first = metrics.enrich(snapshot(10000, 1000, 2000))
        self.assertIsNone(first['clients'][0]['sfuUploadBytesPerSecond'])
        second = metrics.enrich(snapshot(14000, 5000, 10000))
        owner, viewer = second['clients']
        self.assertEqual(owner['sfuUploadBytesPerSecond'], 1000)
        self.assertEqual(viewer['sfuDownloadBytesPerSecond'], 2000)
        self.assertEqual(viewer['activities'][0]['name'], 'Tela')
        self.assertEqual(viewer['activities'][0]['role'], 'viewer')
        self.assertTrue(viewer['hasP2P'])
        self.assertNotIn('uploadBytesPerSecond', viewer['activities'][1])
        reset = metrics.enrich(snapshot(18000, 100, 200))
        self.assertIsNone(reset['clients'][0]['sfuUploadBytesPerSecond'])
        restarted = metrics.enrich(snapshot(22000, 5000, 10000, pid=2))
        self.assertIsNone(restarted['clients'][0]['sfuUploadBytesPerSecond'])
        metrics.enrich({'clients': [], 'streams': []})
        self.assertEqual(metrics.previous, {})

    def test_network_handles_reboot_and_new_interfaces(self):
        self.assertEqual(monitor.counter_delta({'ens3': (100, 200)}, {'ens3': (130, 280)}), (30, 80))
        self.assertEqual(monitor.counter_delta({'ens3': (100, 200)}, {'ens3': (10, 20)}), (0, 0))
        self.assertEqual(monitor.counter_delta({}, {'ens3': (5000, 6000)}), (0, 0))

    def test_snapshot_does_not_report_stale_people_as_online(self):
        with tempfile.TemporaryDirectory() as directory:
            monitor.SNAPSHOT = Path(directory) / 'metrics.json'
            self.assertFalse(monitor.room_snapshot()['online'])
            monitor.SNAPSHOT.write_text(json.dumps({'timestamp': (time.time() - 30) * 1000,
                                                   'pid': 1, 'clients': [{'nickname': 'old'}], 'streams': []}))
            result = monitor.room_snapshot()
            self.assertFalse(result['online'])
            self.assertEqual(result['clients'], [])
            monitor.SNAPSHOT.write_text('{')
            self.assertFalse(monitor.room_snapshot()['online'])

    def test_storage_preserves_totals_across_restart(self):
        with tempfile.TemporaryDirectory() as directory:
            monitor.STATE = Path(directory)
            monitor.initialize()
            with monitor.database() as db:
                db.execute('INSERT INTO totals VALUES (?,?,?)', ('2026-10-13', 123, 456))
            monitor.initialize()
            with monitor.database() as db:
                self.assertEqual(db.execute('SELECT rx,tx FROM totals').fetchone(), (123, 456))

    def test_alerts_wait_repeat_and_recover(self):
        sent = []
        alerts = monitor.Alerts(lambda title, *_: sent.append(title), repeat=3600)
        state = lambda active: {'cpu': (active, 600, 'CPU alta', '')}
        alerts.check(state(True), 0)
        alerts.check(state(True), 599)
        self.assertEqual(sent, [])
        alerts.check(state(True), 600)
        alerts.check(state(True), 1200)
        self.assertEqual(sent, ['CPU alta'])
        alerts.check(state(True), 4200)
        self.assertEqual(sent, ['CPU alta', 'CPU alta'])
        alerts.check(state(False), 4205)
        alerts.check(state(False), 4210)
        self.assertEqual(sent[-1], 'Normalizado: CPU alta')
        self.assertEqual(len(sent), 3)
        alerts.check(state(True), 4215)
        alerts.check(state(False), 4220)
        self.assertEqual(len(sent), 3)

    def test_monthly_egress_projection(self):
        tz = monitor.TZ
        started = monitor.dt.datetime(2026, 10, 1, tzinfo=tz).isoformat()
        now = monitor.dt.datetime(2026, 10, 11, tzinfo=tz).timestamp()
        self.assertAlmostEqual(monitor.projected_month_egress(10e12 / 31 * 10, now, started), 10e12, delta=1e6)
        late = monitor.dt.datetime(2026, 10, 10, 12, tzinfo=tz).isoformat()
        self.assertEqual(monitor.projected_month_egress(1e12, now, late), 0)
        result = {'memory': {'used': 95, 'total': 100}, 'disk': {'used': 10, 'total': 100}, 'cpuPercent': 5,
                  'room': {'online': True}, 'collectionStarted': started,
                  'network': {'txBytesPerSecond': 0, 'monthTx': 9e12}}
        conditions = monitor.alert_conditions(result, now)
        self.assertTrue(conditions['memory'][0])
        self.assertTrue(conditions['egress'][0])
        self.assertFalse(conditions['offline'][0])
        self.assertFalse(conditions['disk'][0])


if __name__ == '__main__':
    unittest.main()
