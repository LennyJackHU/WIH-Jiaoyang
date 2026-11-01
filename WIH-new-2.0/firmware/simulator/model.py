"""Deterministic two-board sentence-editor simulation."""

from dataclasses import dataclass, field
from typing import Dict, List, Optional


MAX_WORDS = 12
MAX_RECEIPTS = 8
MAX_TEXT_BYTES = 96
WORDS = ("微光", "回声", "山海", "星尘", "微风", "远行", "留白", "相逢", "飞鸟", "月色", "新芽", "梦境")


def validate_text(value: object) -> str:
    if not isinstance(value, str):
        raise ValueError("候选词必须是字符串")
    encoded = value.encode("utf-8")
    if not encoded:
        raise ValueError("候选词不能为空")
    if len(encoded) > MAX_TEXT_BYTES:
        raise ValueError("候选词不能超过 96 个 UTF-8 字节")
    if any(ord(char) < 32 or 127 <= ord(char) <= 159 for char in value):
        raise ValueError("候选词不能包含控制字符")
    return value


@dataclass
class PendingOffer:
    req: str
    dst: str
    text: str
    positioned: bool
    position: int
    stage: str = "offer"


@dataclass
class PendingPull:
    req: str
    dst: str
    position: int


@dataclass
class Receipt:
    req: str
    src: str
    text: str
    positioned: bool
    position: int
    side: str
    completed: bool = False
    order: int = 0


@dataclass
class Board:
    name: str
    bank_index: int
    candidate: str = ""
    confirmed: bool = False
    view: str = "Picker"
    mode: str = "Navigate"
    insert_after: bool = False
    words: List[Dict[str, str]] = field(default_factory=list)
    cursor: int = 0
    status: str = ""
    offer: Optional[PendingOffer] = None
    pull: Optional[PendingPull] = None
    receipts: List[Receipt] = field(default_factory=list)
    history: List[str] = field(default_factory=list)
    history_index: int = 0

    def __post_init__(self) -> None:
        self.candidate = WORDS[self.bank_index]
        self.history = [self.candidate]

    @property
    def pending(self) -> bool:
        return self.offer is not None or self.pull is not None

    def next_bank_word(self) -> str:
        for _ in range(len(WORDS) + 1):
            self.bank_index = (self.bank_index + 1) % len(WORDS)
            if WORDS[self.bank_index] != self.candidate:
                return WORDS[self.bank_index]
        return self.candidate

    def replace_candidate(self, text: str) -> None:
        self.candidate = text
        self.history[self.history_index] = text
        del self.history[self.history_index + 1 :]

    def refresh_candidate(self) -> None:
        self.replace_candidate(self.next_bank_word())

    def move_picker(self, steps: int) -> None:
        direction = 1 if steps > 0 else -1
        for _ in range(abs(steps)):
            if direction > 0:
                if self.history_index + 1 < len(self.history):
                    self.history_index += 1
                else:
                    self.history.append(self.next_bank_word())
                    self.history_index += 1
            elif self.history_index > 0:
                self.history_index -= 1
            else:
                for _ in range(len(WORDS) + 1):
                    self.bank_index = (self.bank_index - 1) % len(WORDS)
                    if WORDS[self.bank_index] != self.candidate:
                        break
                self.history.insert(0, WORDS[self.bank_index])
            self.candidate = self.history[self.history_index]

    def move_cursor(self, steps: int) -> None:
        if self.words:
            self.cursor = (self.cursor + steps) % len(self.words)

    def insert_word(self, position: int, text: str, source: str) -> None:
        self.words.insert(position, {"text": text, "source": source})
        self.cursor = position

    def remove_word(self) -> None:
        del self.words[self.cursor]
        if not self.words:
            self.cursor = 0
        elif self.cursor >= len(self.words):
            self.cursor = len(self.words) - 1

    def clear_sentence(self) -> None:
        self.words.clear()
        self.cursor = 0
        self.view = "Picker"
        self.mode = "Navigate"
        self.insert_after = False

    def pending_dict(self) -> Optional[dict]:
        if self.offer:
            return {
                "kind": "offer",
                "req": self.offer.req,
                "peer": self.offer.dst,
                "text": self.offer.text,
                "positioned": self.offer.positioned,
                "position": self.offer.position,
                "stage": self.offer.stage,
            }
        if self.pull:
            return {
                "kind": "pull",
                "req": self.pull.req,
                "peer": self.pull.dst,
                "position": self.pull.position,
                "stage": "pull",
            }
        return None

    def to_dict(self, connected: bool, peer: str, remote_candidate: Optional[str]) -> dict:
        previous = self.history[self.history_index - 1] if self.history_index > 0 else None
        following = (
            self.history[self.history_index + 1]
            if self.history_index + 1 < len(self.history)
            else None
        )
        return {
            "name": self.name,
            "view": self.view,
            "mode": self.mode,
            "candidate": self.candidate,
            "confirmed": self.confirmed,
            "pending": self.pending_dict(),
            "status": self.status,
            "sentence": [dict(word) for word in self.words],
            "cursor": self.cursor,
            "insert_after": self.insert_after,
            "insert_side": "R:AFTER" if self.insert_after else "L:BEFORE",
            "connected": connected,
            "port": "R" if self.name == "A" else "L",
            "peer": peer if connected else None,
            "remote_candidate": remote_candidate if connected else None,
            "previous": previous,
            "next": following,
            "capacity": MAX_WORDS,
        }


class Simulator:
    def __init__(self) -> None:
        self.reset()

    def reset(self) -> None:
        self.boards = {"A": Board("A", 0), "B": Board("B", 1)}
        self.connected = False
        self.request_counter = 0
        self.receipt_counter = 0
        self.logs: List[Dict[str, object]] = []
        self.log_counter = 0
        self.revision = 0
        self._log("模拟器已重置；连接 A.R 与 B.L 后即可传词")

    def _log(self, message: str) -> None:
        self.log_counter += 1
        self.logs.append({"seq": self.log_counter, "message": message})
        if len(self.logs) > 200:
            self.logs = self.logs[-200:]
        self.revision += 1

    def _peer_name(self, board: Board) -> str:
        return "B" if board.name == "A" else "A"

    def _peer(self, board: Board) -> Board:
        return self.boards[self._peer_name(board)]

    def _new_request(self) -> str:
        self.request_counter += 1
        return f"{self.request_counter:032x}"

    def _short_req(self, req: str) -> str:
        return req[-8:]

    def set_connected(self, connected: bool) -> None:
        if not isinstance(connected, bool):
            raise ValueError("connected 必须是布尔值")
        if connected == self.connected:
            return
        self.connected = connected
        if not connected:
            for board in self.boards.values():
                if board.mode == "InsertSide":
                    board.mode = "Navigate"
                    board.insert_after = False
            self._log("A.R ↔ B.L 已断开；句子与 pending 保留")
        else:
            self._log("A.R ↔ B.L 已连接；候选词与邻接信息有效")

    def set_candidate(self, name: str, text: object) -> None:
        board = self.boards[name]
        value = validate_text(text)
        if board.offer:
            raise RuntimeError("offer pending 期间候选词已锁定，请先完成或取消")
        board.replace_candidate(value)
        board.confirmed = False
        board.status = ""
        self._log(f"{name} 通过电脑设置候选词为「{value}」")

    def press_confirm(self, name: str) -> None:
        board = self.boards[name]
        if board.pending:
            self._log(f"{name} pending 期间忽略 A 按钮")
            return
        if board.view == "Sentence":
            self._confirm_sentence(board)
            return
        if not board.confirmed:
            board.confirmed = True
            board.status = ""
            self._log(f"{name} 第一次按 A：确认「{board.candidate}」，尚未发送")
            return
        if not self.connected:
            board.status = "NO LINK"
            self._log(f"{name} 第二次按 A：NO LINK，未发送")
            return
        peer = self._peer(board)
        req = self._new_request()
        board.offer = PendingOffer(req, peer.name, board.candidate, False, 0)
        board.status = "SENDING / WAIT"
        self._log(f"{name} 第二次按 A：向 {peer.name} 发起 OFFER {self._short_req(req)}")

    def _confirm_sentence(self, board: Board) -> None:
        if board.mode == "DeleteConfirm":
            if not board.words:
                return
            removed = board.words[board.cursor]["text"]
            board.remove_word()
            board.mode = "Navigate"
            board.status = ""
            self._log(f"{name(board)} 第二次按 A：删除「{removed}」，cursor={board.cursor}")
            return
        if board.mode == "InsertSide":
            if len(board.words) >= MAX_WORDS:
                board.mode = "Navigate"
                board.status = "SENTENCE FULL/BUSY"
                self._log(f"{name(board)} 句子已满，未发起 pull")
                return
            if not self.connected:
                board.mode = "Navigate"
                board.status = "NO LINK"
                self._log(f"{name(board)} 连接已断开，返回 Navigate")
                return
            position = 0 if not board.words else board.cursor + (1 if board.insert_after else 0)
            peer = self._peer(board)
            req = self._new_request()
            board.pull = PendingPull(req, peer.name, position)
            board.status = "SENDING / WAIT"
            self._log(
                f"{name(board)} 发起 PULL {self._short_req(req)}，固定 pos={position}，"
                f"候选提供方为 {peer.name}"
            )
            return
        if not self.connected:
            if not board.words:
                self._log(f"{name(board)} 空句且无连接，A 不动作")
                return
            board.mode = "DeleteConfirm"
            board.status = ""
            self._log(f"{name(board)} 进入 DeleteConfirm，目标「{board.words[board.cursor]['text']}」")
            return
        if len(board.words) >= MAX_WORDS:
            board.status = "SENTENCE FULL/BUSY"
            self._log(f"{name(board)} 句子已满，不能进入 InsertSide")
            return
        board.mode = "InsertSide"
        board.status = ""
        self._log(f"{name(board)} 进入 InsertSide，remote candidate=「{self._peer(board).candidate}」")

    def press_refresh(self, name: str) -> None:
        board = self.boards[name]
        if board.pull:
            req = board.pull.req
            board.pull = None
            board.mode = "Navigate"
            board.insert_after = False
            board.status = "SENDING / WAIT" if board.offer else ""
            self._log(f"{name} 按 B：取消本机 PULL {self._short_req(req)}，不撤销对端状态")
            return
        if board.offer:
            req = board.offer.req
            board.offer = None
            board.confirmed = False
            board.status = ""
            self._log(f"{name} 按 B：取消本机 OFFER {self._short_req(req)}，对端已收词不会回滚")
            return
        if board.view == "Sentence":
            if board.mode == "DeleteConfirm":
                board.mode = "Navigate"
                board.status = ""
                self._log(f"{name} 按 B：取消删除，句子不变")
            elif board.mode == "InsertSide":
                board.insert_after = not board.insert_after
                board.status = ""
                self._log(f"{name} 按 B：切换为 {('R:AFTER' if board.insert_after else 'L:BEFORE')}")
            else:
                board.clear_sentence()
                board.status = ""
                self._log(f"{name} 按 B：清空整句并回 Picker；接收记录保留")
            return
        if board.confirmed:
            board.confirmed = False
            board.status = ""
            self._log(f"{name} 按 B：取消确认，不换词")
            return
        old = board.candidate
        board.refresh_candidate()
        board.status = ""
        self._log(f"{name} 按 B：刷新候选词「{old}」→「{board.candidate}」")

    def turn_encoder(self, name: str, steps: int) -> None:
        if isinstance(steps, bool) or not isinstance(steps, int) or steps == 0 or abs(steps) > 100:
            raise ValueError("steps 必须是 -100 到 100 之间的非零整数")
        board = self.boards[name]
        if board.view == "Sentence":
            if board.mode == "DeleteConfirm" or not board.words:
                self._log(f"{name} 当前编码器不动作")
                return
            board.move_cursor(steps)
            if not board.pending:
                board.status = ""
            self._log(f"{name} 编码器 {steps:+d}：cursor={board.cursor}")
            return
        if board.offer:
            req = board.offer.req
            board.offer = None
            self._log(f"{name} 转动编码器：取消本机 OFFER {self._short_req(req)}")
        board.confirmed = False
        board.move_picker(steps)
        board.status = ""
        self._log(f"{name} 编码器 {steps:+d}：候选词变为「{board.candidate}」")

    def settle(self) -> None:
        for _ in range(4):
            if not self.connected or not any(board.pending for board in self.boards.values()):
                return
            self.step()

    def step(self) -> None:
        operations = 0
        pulls = [(board, board.pull) for board in self.boards.values() if board.pull]
        for puller, pull in pulls:
            if puller.pull is not pull or not self.connected:
                continue
            provider = self.boards[pull.dst]
            operations += 1
            self._log(
                f"{puller.name} → {provider.name}：WORD_PULL {self._short_req(pull.req)} pos={pull.position}"
            )
            existing = provider.offer
            if existing and existing.req == pull.req and existing.dst == puller.name:
                if not existing.positioned or existing.position != pull.position:
                    self._reject_pull(puller, provider, pull.req)
                continue
            if existing:
                puller.status = "SENTENCE FULL/BUSY"
                self._log(f"{provider.name} → {puller.name}：ERROR BUSY，提供方已有 offer")
                continue
            provider.offer = PendingOffer(
                pull.req, puller.name, provider.candidate, True, pull.position
            )
            provider.status = "SENDING / WAIT"
            self._log(f"{provider.name} 固定当前候选「{provider.candidate}」，建立定位 OFFER")

        offers = [(board, board.offer) for board in self.boards.values() if board.offer]
        sent_offer_ids = set()
        for sender, offer in offers:
            if sender.offer is not offer or offer.stage != "offer" or not self.connected:
                continue
            receiver = self.boards[offer.dst]
            operations += 1
            position_text = f" pos={offer.position}" if offer.positioned else " 无 pos"
            self._log(
                f"{sender.name} → {receiver.name}：WORD_OFFER {self._short_req(offer.req)}"
                f"{position_text}「{offer.text}」"
            )
            result = self._receive_offer(receiver, sender, offer)
            if result == "ok":
                offer.stage = "accept"
                sent_offer_ids.add(id(offer))
            elif result == "busy":
                sender.status = "SENTENCE FULL/BUSY"
                self._log(f"{receiver.name} → {sender.name}：ERROR BUSY，同 req 保留等待重试")
            else:
                self._reject_offer(sender, receiver, offer.req)

        accepts = [
            (board, board.offer)
            for board in self.boards.values()
            if board.offer and board.offer.stage == "accept"
        ]
        # Offers sent above must wait until the next explicit step for ACCEPT.
        for sender, offer in accepts:
            if id(offer) in sent_offer_ids or sender.offer is not offer or not self.connected:
                continue
            receiver = self.boards[offer.dst]
            operations += 1
            side = self._ingress(receiver)
            self._log(f"{receiver.name} → {sender.name}：WORD_ACCEPT {self._short_req(offer.req)} side={side}")
            self._complete_receipt(receiver, sender.name, offer.req)
            old = sender.candidate
            sender.offer = None
            sender.confirmed = False
            sender.view = "Sentence" if sender.words else "Picker"
            if sender.view == "Picker":
                sender.mode = "Navigate"
            sender.refresh_candidate()
            sender.status = "SENT - NEW WORD"
            self._log(f"{sender.name} → {receiver.name}：WORD_DONE {self._short_req(offer.req)}")
            self._log(f"{sender.name} 收到 ACCEPT 后才换词：「{old}」→「{sender.candidate}」")

        if not operations:
            waiting = any(board.pending for board in self.boards.values())
            self._log("连接断开，pending 保持并等待重连" if waiting else "没有待推进的传输")

    def _ingress(self, receiver: Board) -> str:
        return "L" if receiver.name == "B" else "R"

    def _receive_offer(self, receiver: Board, sender: Board, offer: PendingOffer) -> str:
        for receipt in receiver.receipts:
            if receipt.req != offer.req or receipt.src != sender.name:
                continue
            if (
                receipt.text != offer.text
                or receipt.positioned != offer.positioned
                or (offer.positioned and receipt.position != offer.position)
            ):
                return "invalid"
            receipt.side = self._ingress(receiver)
            self._log(f"{receiver.name} 命中接收去重记录，不重复插入「{offer.text}」")
            return "ok"

        position = offer.position if offer.positioned else (0 if self._ingress(receiver) == "L" else len(receiver.words))
        if len(receiver.words) >= MAX_WORDS or position > len(receiver.words):
            receiver.status = "SENTENCE FULL/BUSY"
            return "busy"
        reusable = None
        if len(receiver.receipts) >= MAX_RECEIPTS:
            completed = [receipt for receipt in receiver.receipts if receipt.completed]
            if not completed:
                receiver.status = "SENTENCE FULL/BUSY"
                return "busy"
            reusable = min(completed, key=lambda receipt: receipt.order)

        receiver.insert_word(position, offer.text, sender.name)
        self.receipt_counter += 1
        receipt = Receipt(
            offer.req,
            sender.name,
            offer.text,
            offer.positioned,
            position,
            self._ingress(receiver),
            False,
            self.receipt_counter,
        )
        if reusable is None:
            receiver.receipts.append(receipt)
        else:
            receiver.receipts[receiver.receipts.index(reusable)] = receipt
        if (
            receiver.pull
            and offer.positioned
            and receiver.pull.req == offer.req
            and receiver.pull.dst == sender.name
            and receiver.pull.position == position
        ):
            receiver.pull = None
        receiver.view = "Sentence"
        receiver.mode = "Navigate"
        receiver.status = f"RECEIVED FROM {self._ingress(receiver)}"
        self._log(
            f"{receiver.name} 在 pos={position} 插入「{offer.text}」，cursor={position}，进入 Sentence/Navigate"
        )
        return "ok"

    def _complete_receipt(self, receiver: Board, source: str, req: str) -> None:
        for receipt in receiver.receipts:
            if receipt.req == req and receipt.src == source:
                receipt.completed = True
                return

    def _reject_offer(self, sender: Board, receiver: Board, req: str) -> None:
        sender.offer = None
        sender.status = "TRANSFER REJECTED"
        if receiver.pull and receiver.pull.req == req:
            receiver.pull = None
            receiver.mode = "Navigate"
            receiver.insert_after = False
            receiver.status = "TRANSFER REJECTED"
        self._log(f"{receiver.name} → {sender.name}：ERROR INVALID，对应 pending 已取消")

    def _reject_pull(self, puller: Board, provider: Board, req: str) -> None:
        puller.pull = None
        puller.mode = "Navigate"
        puller.insert_after = False
        puller.status = "TRANSFER REJECTED"
        if provider.offer and provider.offer.req == req:
            provider.offer = None
            provider.status = "TRANSFER REJECTED"
        self._log(f"{provider.name} → {puller.name}：ERROR INVALID，pull 位置与已有 offer 不一致")

    def action(self, name: str, action: str, steps: object = None) -> None:
        if name not in self.boards:
            raise ValueError("board 只能是 A 或 B")
        if action == "confirm":
            self.press_confirm(name)
        elif action == "refresh":
            self.press_refresh(name)
        elif action == "encoder":
            self.turn_encoder(name, steps)
        else:
            raise ValueError("action 只能是 confirm、refresh 或 encoder")

    def state(self) -> dict:
        remote_a = self.boards["B"].candidate if self.connected else None
        remote_b = self.boards["A"].candidate if self.connected else None
        return {
            "connected": self.connected,
            "connection": "A.R ↔ B.L" if self.connected else "A.R ✕ B.L",
            "revision": self.revision,
            "boards": {
                "A": self.boards["A"].to_dict(self.connected, "B", remote_a),
                "B": self.boards["B"].to_dict(self.connected, "A", remote_b),
            },
            "logs": list(self.logs),
        }


def name(board: Board) -> str:
    return board.name
