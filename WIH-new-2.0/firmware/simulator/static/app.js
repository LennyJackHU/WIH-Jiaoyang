"use strict";

const statusNames = {
  "": "READY",
  "NO LINK": "NO LINK · 无可用连接",
  "SENDING / WAIT": "SENDING / WAIT · 等待完成",
  "RECEIVED FROM L": "RECEIVED FROM L · 从左口收到",
  "RECEIVED FROM R": "RECEIVED FROM R · 从右口收到",
  "SENT - NEW WORD": "SENT - NEW WORD · 已确认并换词",
  "SENTENCE FULL/BUSY": "SENTENCE FULL/BUSY · 满句或暂忙",
  "TRANSFER REJECTED": "TRANSFER REJECTED · 传输被拒绝"
};

let busy = false;
let currentState = null;
const errorBox = document.querySelector("#error");

function field(boardElement, name) {
  return boardElement.querySelector(`[data-field="${name}"]`);
}

async function request(path, body) {
  const options = body === undefined ? {} : {
    method: "POST",
    headers: {"Content-Type": "application/json"},
    body: JSON.stringify(body)
  };
  const response = await fetch(path, options);
  const value = await response.json();
  if (!response.ok) {
    throw new Error(value.error || `请求失败（${response.status}）`);
  }
  return value;
}

async function perform(path, body) {
  if (busy) return;
  busy = true;
  errorBox.hidden = true;
  document.querySelectorAll("button").forEach(button => { button.disabled = true; });
  try {
    currentState = await request(path, body);
    render(currentState);
  } catch (error) {
    errorBox.textContent = error.message;
    errorBox.hidden = false;
  } finally {
    busy = false;
    document.querySelectorAll("button").forEach(button => { button.disabled = false; });
  }
}

function renderSentence(container, board) {
  container.replaceChildren();
  if (!board.sentence.length) {
    const empty = document.createElement("span");
    empty.className = "empty-sentence";
    empty.textContent = "Empty sentence · 空句";
    container.append(empty);
    return;
  }
  board.sentence.forEach((word, index) => {
    const item = document.createElement("span");
    item.className = `word${index === board.cursor ? " selected" : ""}`;
    const number = document.createElement("span");
    number.className = "index";
    number.textContent = String(index + 1).padStart(2, "0");
    item.append(number, document.createTextNode(`${word.text} ·${word.source}`));
    container.append(item);
  });
}

function renderPending(container, pending) {
  container.hidden = !pending;
  container.replaceChildren();
  if (!pending) return;
  const title = document.createElement("strong");
  title.textContent = pending.kind === "offer" ? "OFFER pending · 候选词已锁定" : "PULL pending · 位置已固定";
  const detail = document.createElement("span");
  const stage = pending.kind === "offer"
    ? (pending.stage === "offer" ? "等待接收/重试" : "等待 ACCEPT")
    : "等待对端提供候选词";
  const position = pending.positioned || pending.kind === "pull" ? ` · pos=${pending.position}` : " · 无 pos";
  detail.textContent = `${pending.req} → ${pending.peer}${position} · ${stage}`;
  container.append(title, detail);
}

function renderBoard(board) {
  const element = document.querySelector(`[data-board="${board.name}"]`);
  const sentenceMode = board.view === "Sentence";
  const insertMode = sentenceMode && board.mode === "InsertSide";
  field(element, "picker").hidden = sentenceMode;
  field(element, "sentence-view").hidden = !sentenceMode || insertMode;
  field(element, "insert-view").hidden = !insertMode;
  field(element, "status").textContent = statusNames[board.status] || board.status;
  field(element, "candidate-display").textContent = board.candidate;
  field(element, "candidate-display").classList.toggle("confirmed", board.confirmed);
  field(element, "previous").textContent = board.previous || "—";
  field(element, "next").textContent = board.next || "—";
  field(element, "link").textContent = board.connected ? `已连接 ${board.peer}` : "未连接";
  field(element, "link").classList.toggle("connected", board.connected);
  field(element, "connection-detail").textContent = `${board.name}.${board.port} ${board.connected ? `↔ ${board.peer}` : "断开"}`;
  field(element, "mode").textContent = sentenceMode ? `Sentence / ${board.mode}` : "Picker";
  field(element, "cursor").textContent = `${board.sentence.length ? board.cursor : "—"} / ${board.sentence.length}（上限 ${board.capacity}）`;
  field(element, "confirmed").textContent = board.confirmed ? "已确认，尚未发送" : "否";
  renderSentence(field(element, "sentence"), board);
  field(element, "delete-prompt").hidden = board.mode !== "DeleteConfirm";
  field(element, "sentence-footer").textContent = board.mode === "DeleteConfirm"
    ? "再次按 A 删除；按 B 取消"
    : "ROTATE:SELECT　A:DEL/PULL　B:CANCEL";
  field(element, "remote-candidate").textContent = board.remote_candidate || "NO LINK";
  field(element, "before").classList.toggle("selected", !board.insert_after);
  field(element, "after").classList.toggle("selected", board.insert_after);
  renderPending(field(element, "pending"), board.pending);
}

function renderLogs(logs) {
  const list = document.querySelector("#log");
  list.replaceChildren();
  logs.slice().reverse().forEach(entry => {
    const item = document.createElement("li");
    const seq = document.createElement("span");
    seq.className = "seq";
    seq.textContent = `#${entry.seq}`;
    const message = document.createElement("span");
    message.textContent = entry.message;
    item.append(seq, message);
    list.append(item);
  });
}

function render(state) {
  document.querySelector("#connection").textContent = state.connected ? "断开 A.R ↔ B.L" : "连接 A.R ↔ B.L";
  document.querySelector("#connection").classList.toggle("primary", !state.connected);
  const pendingCount = Object.values(state.boards).filter(board => board.pending).length;
  document.querySelector("#step").textContent = pendingCount ? `重试等待中的传输（${pendingCount} 项 pending）` : "重试等待中的传输";
  renderBoard(state.boards.A);
  renderBoard(state.boards.B);
  renderLogs(state.logs);
}

document.querySelector("#connection").addEventListener("click", () => {
  perform("/api/connection", {connected: !currentState.connected});
});
document.querySelector("#step").addEventListener("click", () => perform("/api/step", {}));
document.querySelector("#reset").addEventListener("click", () => {
  if (window.confirm("重置两块板的句子、候选词和日志？")) perform("/api/reset", {});
});

document.querySelectorAll("[data-board]").forEach(element => {
  const board = element.dataset.board;
  element.querySelectorAll("[data-action]").forEach(button => {
    button.addEventListener("click", () => {
      const action = button.dataset.action;
      const body = {board, action};
      if (action === "encoder") body.steps = Number(button.dataset.steps);
      perform("/api/action", body);
    });
  });
  element.querySelector("[data-form='candidate']").addEventListener("submit", event => {
    event.preventDefault();
    const input = event.currentTarget.elements.candidate;
    perform("/api/candidate", {board, text: input.value}).then(() => {
      if (!errorBox.hidden) return;
      input.value = "";
    });
  });
});

perform("/api/state").then(() => {
  window.setInterval(async () => {
    if (busy) return;
    try {
      const state = await request("/api/state");
      if (state.revision !== currentState.revision) {
        currentState = state;
        render(state);
      }
    } catch (_) {
      // A later poll recovers from a transient local-server interruption.
    }
  }, 1500);
});
