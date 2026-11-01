import http.client
import json
import threading
import unittest

from simulator.model import MAX_TEXT_BYTES, MAX_WORDS, Simulator
from simulator.server import create_server


class SimulatorModelTests(unittest.TestCase):
    def setUp(self):
        self.sim = Simulator()

    def words(self, name):
        return [word["text"] for word in self.sim.boards[name].words]

    def offer(self, sender, text):
        receiver = "B" if sender == "A" else "A"
        self.sim.set_candidate(sender, text)
        self.sim.press_confirm(sender)
        self.sim.press_confirm(sender)
        self.assertIsNotNone(self.sim.boards[sender].offer)
        return receiver

    def settle_offer(self):
        self.sim.step()
        self.sim.step()

    def test_double_confirm_offer_accept_and_chinese_text(self):
        self.sim.set_candidate("A", "你好，微光")
        self.sim.set_candidate("B", "保留候选")
        self.sim.set_connected(True)
        self.sim.press_confirm("A")
        self.assertTrue(self.sim.boards["A"].confirmed)
        self.assertIsNone(self.sim.boards["A"].offer)
        self.sim.press_confirm("A")
        pending = self.sim.boards["A"].offer
        self.assertIsNotNone(pending)

        self.sim.step()
        self.assertEqual(["你好，微光"], self.words("B"))
        self.assertEqual("A", self.sim.boards["B"].words[0]["source"])
        self.assertEqual("Sentence", self.sim.boards["B"].view)
        self.assertEqual("Navigate", self.sim.boards["B"].mode)
        self.assertEqual(0, self.sim.boards["B"].cursor)
        self.assertIs(self.sim.boards["A"].offer, pending)
        self.assertEqual("你好，微光", self.sim.boards["A"].candidate)
        self.assertEqual("保留候选", self.sim.boards["B"].candidate)

        self.sim.step()
        self.assertIsNone(self.sim.boards["A"].offer)
        self.assertFalse(self.sim.boards["A"].confirmed)
        self.assertNotEqual("你好，微光", self.sim.boards["A"].candidate)
        self.assertTrue(self.sim.boards["B"].receipts[0].completed)

    def test_unpositioned_offer_uses_receiver_ingress(self):
        self.sim.set_connected(True)
        self.offer("A", "第一句")
        self.settle_offer()
        self.offer("A", "新的句首")
        self.settle_offer()
        self.assertEqual(["新的句首", "第一句"], self.words("B"))
        self.sim.press_refresh("B")
        self.assertEqual("Picker", self.sim.boards["B"].view)

        self.offer("B", "右口一")
        self.settle_offer()
        self.offer("B", "右口二")
        self.settle_offer()
        self.assertEqual(["右口一", "右口二"], self.words("A"))

    def test_positioned_pull_after_and_cursor_during_pending(self):
        self.sim.set_connected(True)
        self.offer("A", "alpha")
        self.settle_offer()
        self.offer("A", "beta")
        self.settle_offer()
        self.assertEqual(["beta", "alpha"], self.words("B"))
        self.sim.set_candidate("A", "中间")

        self.sim.press_confirm("B")
        self.assertEqual("InsertSide", self.sim.boards["B"].mode)
        self.assertEqual("中间", self.sim.boards["B"].to_dict(True, "A", "中间")["remote_candidate"])
        self.sim.press_refresh("B")
        self.assertTrue(self.sim.boards["B"].insert_after)
        self.sim.press_confirm("B")
        pull = self.sim.boards["B"].pull
        self.assertEqual(1, pull.position)
        self.sim.turn_encoder("B", 1)
        self.assertEqual(1, self.sim.boards["B"].cursor)
        self.assertEqual(1, self.sim.boards["B"].pull.position)

        self.sim.step()
        self.assertEqual(["beta", "中间", "alpha"], self.words("B"))
        self.assertEqual(1, self.sim.boards["B"].cursor)
        self.assertIsNone(self.sim.boards["B"].pull)
        self.assertEqual("中间", self.sim.boards["A"].candidate)
        self.sim.step()
        self.assertIsNone(self.sim.boards["A"].offer)
        self.assertNotEqual("中间", self.sim.boards["A"].candidate)

    def test_empty_sentence_pulls_at_zero(self):
        self.sim.set_connected(True)
        self.offer("A", "seed")
        self.settle_offer()
        self.sim.set_connected(False)
        self.sim.press_confirm("B")
        self.sim.press_confirm("B")
        self.assertEqual([], self.words("B"))
        self.assertEqual("Sentence", self.sim.boards["B"].view)

        self.sim.set_candidate("A", "新起点")
        self.sim.set_connected(True)
        self.sim.press_confirm("B")
        self.sim.press_refresh("B")
        self.sim.press_confirm("B")
        self.assertEqual(0, self.sim.boards["B"].pull.position)
        self.settle_offer()
        self.assertEqual(["新起点"], self.words("B"))

    def test_cursor_delete_confirm_cancel_and_clear(self):
        self.sim.set_connected(True)
        for text in ("alpha", "beta", "omega"):
            self.offer("A", text)
            self.settle_offer()
        self.assertEqual(["omega", "beta", "alpha"], self.words("B"))
        self.sim.set_connected(False)
        board = self.sim.boards["B"]
        self.assertEqual(0, board.cursor)
        self.sim.turn_encoder("B", -1)
        self.assertEqual(2, board.cursor)
        self.sim.turn_encoder("B", 1)
        self.assertEqual(0, board.cursor)

        self.sim.press_confirm("B")
        self.assertEqual("DeleteConfirm", board.mode)
        self.sim.press_refresh("B")
        self.assertEqual("Navigate", board.mode)
        self.assertEqual(3, len(board.words))
        self.sim.press_confirm("B")
        self.sim.press_confirm("B")
        self.assertEqual(["beta", "alpha"], self.words("B"))
        self.assertEqual(0, board.cursor)
        self.sim.press_refresh("B")
        self.assertEqual([], self.words("B"))
        self.assertEqual("Picker", board.view)

    def test_hotplug_preserves_request_and_never_duplicates(self):
        self.sim.set_connected(True)
        self.offer("A", "只插一次")
        board = self.sim.boards["A"]
        req = board.offer.req
        self.sim.set_connected(False)
        self.sim.step()
        self.assertEqual(req, board.offer.req)
        self.assertEqual([], self.words("B"))

        self.sim.set_connected(True)
        self.sim.step()
        self.assertEqual(["只插一次"], self.words("B"))
        self.sim.set_connected(False)
        self.sim.step()
        self.assertEqual(req, board.offer.req)
        self.sim.set_connected(True)
        self.sim.step()
        self.assertIsNone(board.offer)
        self.assertEqual(["只插一次"], self.words("B"))

    def test_cancel_pending_is_local(self):
        self.sim.set_connected(True)
        self.offer("A", "已入句")
        self.sim.step()
        self.sim.press_refresh("A")
        self.assertIsNone(self.sim.boards["A"].offer)
        self.assertEqual(["已入句"], self.words("B"))
        self.sim.step()
        self.assertEqual(["已入句"], self.words("B"))

    def test_no_link_and_picker_b_behavior(self):
        board = self.sim.boards["A"]
        original = board.candidate
        self.sim.press_confirm("A")
        self.sim.press_confirm("A")
        self.assertEqual("NO LINK", board.status)
        self.assertTrue(board.confirmed)
        self.sim.press_refresh("A")
        self.assertFalse(board.confirmed)
        self.assertEqual(original, board.candidate)
        self.sim.press_refresh("A")
        self.assertNotEqual(original, board.candidate)

    def test_sentence_capacity_and_busy_retry(self):
        self.sim.set_connected(True)
        for index in range(MAX_WORDS):
            self.offer("A", f"词{index}")
            self.settle_offer()
        self.assertEqual(MAX_WORDS, len(self.sim.boards["B"].words))
        self.offer("A", "第13词")
        self.sim.step()
        self.assertEqual(MAX_WORDS, len(self.sim.boards["B"].words))
        self.assertEqual("SENTENCE FULL/BUSY", self.sim.boards["A"].status)
        self.assertIsNotNone(self.sim.boards["A"].offer)

        self.sim.press_confirm("B")
        self.assertEqual("Navigate", self.sim.boards["B"].mode)
        self.sim.set_connected(False)
        self.sim.press_confirm("B")
        self.sim.press_confirm("B")
        self.assertEqual(MAX_WORDS - 1, len(self.sim.boards["B"].words))
        self.sim.set_connected(True)
        self.settle_offer()
        self.assertEqual(MAX_WORDS, len(self.sim.boards["B"].words))
        self.assertIn("第13词", self.words("B"))

    def test_candidate_validation_and_pending_lock(self):
        with self.assertRaises(ValueError):
            self.sim.set_candidate("A", "")
        with self.assertRaises(ValueError):
            self.sim.set_candidate("A", "bad\nword")
        with self.assertRaises(ValueError):
            self.sim.set_candidate("A", "字" * (MAX_TEXT_BYTES // 3 + 1))
        self.sim.set_connected(True)
        self.offer("A", "locked")
        with self.assertRaises(RuntimeError):
            self.sim.set_candidate("A", "change")
        self.sim.turn_encoder("A", 1)
        self.assertIsNone(self.sim.boards["A"].offer)
        self.sim.set_candidate("A", "可以修改")
        self.assertEqual("可以修改", self.sim.boards["A"].candidate)


class SimulatorWebTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.httpd = create_server(port=0)
        cls.httpd.quiet = True
        cls.thread = threading.Thread(target=cls.httpd.serve_forever, daemon=True)
        cls.thread.start()
        cls.port = cls.httpd.server_address[1]

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()
        cls.thread.join(timeout=2)

    def setUp(self):
        self.httpd.simulator.reset()

    def request(self, method, path, value=None, raw=None, content_type="application/json"):
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=3)
        headers = {}
        body = raw
        if value is not None:
            body = json.dumps(value, ensure_ascii=False).encode("utf-8")
        if body is not None:
            headers["Content-Type"] = content_type
        connection.request(method, path, body=body, headers=headers)
        response = connection.getresponse()
        data = response.read()
        result = (response.status, dict(response.getheaders()), data)
        connection.close()
        return result

    def json_request(self, method, path, value=None, raw=None, content_type="application/json"):
        status, headers, data = self.request(method, path, value, raw, content_type)
        return status, headers, json.loads(data.decode("utf-8"))

    def test_static_page_is_chinese_and_self_contained(self):
        status, headers, data = self.request("GET", "/")
        page = data.decode("utf-8")
        self.assertEqual(200, status)
        self.assertIn("WIH 句子编辑器模拟器", page)
        self.assertIn("板 A", page)
        self.assertIn("板 B", page)
        self.assertNotIn("https://", page)
        self.assertIn("text/html", headers["Content-Type"])
        self.assertIn("default-src 'self'", headers["Content-Security-Policy"])
        self.assertEqual(200, self.request("GET", "/app.js")[0])
        self.assertEqual(200, self.request("GET", "/style.css")[0])
        self.assertEqual(404, self.request("GET", "/../model.py")[0])

    def test_complete_web_offer_scenario_and_log(self):
        self.assertEqual(200, self.json_request("POST", "/api/candidate", {"board": "A", "text": "中文来词"})[0])
        self.assertEqual(200, self.json_request("POST", "/api/connection", {"connected": True})[0])
        self.assertEqual(200, self.json_request("POST", "/api/action", {"board": "A", "action": "confirm"})[0])
        state = self.json_request("POST", "/api/action", {"board": "A", "action": "confirm"})[2]
        self.assertEqual("中文来词", state["boards"]["B"]["sentence"][0]["text"])
        self.assertEqual("Sentence", state["boards"]["B"]["view"])
        self.assertIsNone(state["boards"]["A"]["pending"])
        self.assertEqual("SENT - NEW WORD", state["boards"]["A"]["status"])
        self.assertTrue(any("WORD_OFFER" in item["message"] for item in state["logs"]))
        self.assertTrue(any("WORD_ACCEPT" in item["message"] for item in state["logs"]))
        self.assertTrue(any("WORD_DONE" in item["message"] for item in state["logs"]))

    def test_web_pull_autocompletes_when_peer_idle(self):
        self.json_request("POST", "/api/connection", {"connected": True})
        self.json_request("POST", "/api/candidate", {"board": "A", "text": "root"})
        self.json_request("POST", "/api/action", {"board": "A", "action": "confirm"})
        self.json_request("POST", "/api/action", {"board": "A", "action": "confirm"})
        self.json_request("POST", "/api/candidate", {"board": "A", "text": "pulled"})
        self.json_request("POST", "/api/action", {"board": "B", "action": "confirm"})
        self.json_request("POST", "/api/action", {"board": "B", "action": "refresh"})
        state = self.json_request("POST", "/api/action", {"board": "B", "action": "confirm"})[2]
        self.assertEqual(["root", "pulled"], [word["text"] for word in state["boards"]["B"]["sentence"]])
        self.assertIsNone(state["boards"]["A"]["pending"])
        self.assertIsNone(state["boards"]["B"]["pending"])
        self.assertNotEqual("pulled", state["boards"]["A"]["candidate"])
        self.assertTrue(any("WORD_PULL" in item["message"] for item in state["logs"]))
        self.assertTrue(any("pos=1" in item["message"] for item in state["logs"]))

    def test_api_rejects_invalid_requests(self):
        cases = [
            ("/api/candidate", {"board": "C", "text": "word"}),
            ("/api/candidate", {"board": ["A"], "text": "word"}),
            ("/api/candidate", {"board": "A", "text": ""}),
            ("/api/candidate", {"board": "A", "text": "字" * 33}),
            ("/api/candidate", {"board": "A", "text": "x", "extra": True}),
            ("/api/action", {"board": "A", "action": "unknown"}),
            ("/api/action", {"board": "A", "action": "encoder", "steps": True}),
            ("/api/action", {"board": "A", "action": "confirm", "steps": 1}),
            ("/api/connection", {"connected": "yes"}),
            ("/api/step", {"unexpected": 1}),
        ]
        for path, value in cases:
            with self.subTest(path=path, value=value):
                status, _, result = self.json_request("POST", path, value)
                self.assertEqual(400, status)
                self.assertIn("error", result)
        self.assertEqual(400, self.json_request("POST", "/api/step", raw=b"{")[0])
        self.assertEqual(400, self.json_request("POST", "/api/step", raw=b'{"x":1,"x":2}')[0])
        self.assertEqual(415, self.json_request("POST", "/api/step", raw=b"{}", content_type="text/plain")[0])
        self.assertEqual(404, self.json_request("POST", "/api/missing", {})[0])


if __name__ == "__main__":
    unittest.main()
