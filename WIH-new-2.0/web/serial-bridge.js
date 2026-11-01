// USB-to-UART console, 115200 baud; sentence snapshots are explicitly requested.
export class WIHSerialBridge {
  constructor({ onWord, onStatus = () => {}, serial = null }) {
    this.onWord = onWord;
    this.onStatus = onStatus;
    this.serial = serial;
    this.port = null;
    this.reader = null;
    this.running = false;
    this.busy = false;
    this.closing = false;
    this.recognized = false;
    this.loopPromise = null;
    this.status = '未连接';
    this.seen = new Set();
    this.pendingSentence = null;
    this.writePromise = null;
    this.sentenceExport = false;
    this.identity = null;
    this.resetParser();
  }

  resetParser() {
    this.buffer = '';
    this.discardLine = false;
    this.lastLegacySeq = null;
    this.decoder = new TextDecoder();
  }

  setStatus(value) {
    this.status = value;
    this.onStatus(value, {
      connected: this.running && this.recognized,
      running: this.running
    });
  }

  recognize() {
    if (this.recognized) return;
    this.recognized = true;
    this.setStatus('已连接');
  }

  acceptLine(line) {
    if (!line.startsWith('WIH:')) return;
    let msg;
    try { msg = JSON.parse(line.slice(4)); } catch { return; }
    if (!msg || typeof msg !== 'object' || Array.isArray(msg)) return;
    const modern = msg.protocol === 1 &&
      typeof msg.device === 'string' && typeof msg.session === 'string' &&
      /^[0-9a-f]{16}$/i.test(msg.device) && /^[0-9a-f]{16}$/i.test(msg.session);
    // Unknown protocol versions must not be interpreted as legacy events.
    if (msg.protocol !== undefined && !modern) return;
    if (modern && msg.type === 'ready') {
      if (this.identity && (this.identity.device !== msg.device || this.identity.session !== msg.session))
        this.failSentence(new Error('词块已重启，请重新导入整句'));
      this.identity = { device: msg.device, session: msg.session };
      this.sentenceExport = msg.sentenceExport === 1;
      this.recognize();
      return;
    }
    if (modern && ['sentence_begin', 'sentence_word', 'sentence_end'].includes(msg.type)) {
      this.acceptSentence(msg);
      return;
    }
    if (msg.type !== 'word_confirmed' || typeof msg.word !== 'string' ||
        !msg.word.trim() || /[\u0000-\u001f\u007f]/.test(msg.word) ||
        new TextEncoder().encode(msg.word).length > 96) return;
    if (modern || msg.seq !== undefined) {
      if (!Number.isInteger(msg.seq) || msg.seq < 0 || msg.seq > 0xffffffff) return;
    }
    this.recognize();
    if (modern) {
      const key = `${msg.device.toLowerCase()}:${msg.session.toLowerCase()}:${msg.seq}`;
      if (this.seen.has(key)) return;
      this.seen.add(key);
      // Retain across reconnects; a reboot's session makes seq=1 a new event.
      if (this.seen.size > 1024) this.seen.delete(this.seen.values().next().value);
    } else {
      // Compatibility with the earlier word_confirmed-only firmware.
      if (msg.seq !== undefined && msg.seq === this.lastLegacySeq) return;
      this.lastLegacySeq = msg.seq ?? null;
    }
    try { this.onWord(msg.word, msg); }
    catch (err) { console.error('网页处理词块失败', err); }
  }

  acceptChunk(bytes) {
    for (const ch of this.decoder.decode(bytes, { stream: true })) {
      if (ch === '\n') {
        if (!this.discardLine) this.acceptLine(this.buffer.trim());
        this.buffer = '';
        this.discardLine = false;
      } else if (!this.discardLine) {
        this.buffer += ch;
        if (this.buffer.length > 1024) {
          this.buffer = '';
          this.discardLine = true;
        }
      }
    }
  }

  async closePort() {
    if (!this.port) return;
    if (this.writePromise) await this.writePromise;
    if (this.port.readable || this.port.writable) await this.port.close();
    this.port = null;
  }

  async connect() {
    if (this.busy || this.running || this.closing) return;
    this.busy = true;
    this.recognized = false;
    this.sentenceExport = false;
    this.identity = null;
    this.setStatus('正在连接…');
    try {
      const serial = this.serial ?? globalThis.navigator?.serial;
      if (!serial) throw new Error('请使用 Chrome/Edge，并通过 localhost 或 HTTPS 打开网页');
      await this.closePort();
      this.loopPromise = null;
      // Show the chooser on every attempt, so a wrong/unplugged port is not sticky.
      const port = await serial.requestPort();
      await port.open({ baudRate: 115200 });
      this.port = port;
      try { await port.setSignals({ dataTerminalReady: false, requestToSend: false }); }
      catch (err) { console.debug('无法设置串口控制信号', err); }
      if (!port.readable) throw new Error('所选串口没有可读数据流');
      this.resetParser();
      this.running = true;
      this.setStatus('串口已打开，等待词块固件…');
      this.loopPromise = this.readLoop();
    } catch (err) {
      this.running = false;
      try { await this.closePort(); } catch (closeError) { console.warn(closeError); }
      this.setStatus(err.name === 'NotFoundError' ? '未连接' : '连接失败：' + err.message);
      throw err;
    } finally { this.busy = false; }
  }

  async readLoop() {
    let finalStatus = '未连接';
    try {
      this.reader = this.port.readable.getReader();
      while (this.running) {
        const { value, done } = await this.reader.read();
        if (!this.running) break;
        if (done) {
          finalStatus = '连接中断：串口已关闭，请重新连接';
          break;
        }
        if (value) this.acceptChunk(value);
      }
    } catch (err) {
      if (this.running) finalStatus = '连接中断：' + err.message;
    } finally {
      // Cleanup has its own guard: do not restore a stale connect/disconnect
      // busy flag when getReader() fails before connect() has returned.
      this.closing = true;
      this.running = false;
      this.recognized = false;
      this.failSentence(new Error('设备连接已中断，请重新连接后导入'));
      try { this.reader?.releaseLock(); }
      catch (err) { finalStatus = '释放串口失败：' + err.message; }
      this.reader = null;
      try { await this.closePort(); }
      catch (err) { finalStatus = '关闭串口失败：' + err.message; }
      this.closing = false;
      this.setStatus(finalStatus);
    }
  }

  async disconnect() {
    if (this.busy) return;
    this.busy = true;
    try {
      this.running = false;
      this.failSentence(new Error('设备已断开'));
      if (this.reader) {
        try { await this.reader.cancel(); } catch { /* Unplug may already have errored. */ }
      }
      if (this.loopPromise) await this.loopPromise;
      await this.closePort();
      this.recognized = false;
      this.setStatus('未连接');
    } finally { this.busy = false; }
  }

  failSentence(error) {
    const pending = this.pendingSentence;
    if (!pending) return;
    this.pendingSentence = null;
    clearTimeout(pending.timer);
    pending.reject(error);
  }

  requestSentence({ timeoutMs = 5000 } = {}) {
    if (!this.running || !this.recognized || !this.port?.writable)
      return Promise.reject(new Error('请先连接词块'));
    if (!this.sentenceExport)
      return Promise.reject(new Error('尚未识别整句功能，请稍等 2 秒；仍不可用请烧录新版固件'));
    if (this.pendingSentence || this.writePromise)
      return Promise.reject(new Error('正在读取整句，请稍候'));
    const bytes = new Uint8Array(8);
    globalThis.crypto.getRandomValues(bytes);
    const request = Array.from(bytes, n => n.toString(16).padStart(2, '0')).join('');
    const result = new Promise((resolve, reject) => {
      this.pendingSentence = {
        request, ...this.identity, resolve, reject, count: null, words: [],
        timer: setTimeout(() => this.failSentence(new Error('读取整句超时，请检查固件和串口后重试')), timeoutMs)
      };
    });
    this.writePromise = (async () => {
      let writer;
      try {
        writer = this.port.writable.getWriter();
        await writer.write(new TextEncoder().encode('WIH:' + JSON.stringify({
          type: 'get_sentence', protocol: 1, request
        }) + '\n'));
      } catch (error) {
        this.failSentence(new Error('发送整句请求失败：' + error.message));
      } finally {
        writer?.releaseLock();
      }
    })();
    this.writePromise.then(() => { this.writePromise = null; });
    return result;
  }

  acceptSentence(msg) {
    const p = this.pendingSentence;
    if (!p || msg.request !== p.request || msg.device !== p.device || msg.session !== p.session) return;
    const invalid = () => this.failSentence(new Error('整句数据不完整或无效，请重试'));
    if (!Number.isInteger(msg.count) || msg.count < 0 || msg.count > 12) return invalid();
    if (msg.type === 'sentence_begin') {
      if (p.count !== null) return invalid();
      p.count = msg.count;
    } else {
      if (p.count === null || msg.count !== p.count) return invalid();
      if (msg.type === 'sentence_word') {
        if (!Number.isInteger(msg.index) || msg.index < 0 || msg.index >= p.count ||
            typeof msg.word !== 'string' || !msg.word.trim() ||
            /[\u0000-\u001f\u007f]/.test(msg.word) ||
            new TextEncoder().encode(msg.word).length > 96) return invalid();
        if (p.words[msg.index] !== undefined && p.words[msg.index] !== msg.word) return invalid();
        p.words[msg.index] = msg.word;
      } else {
        for (let i = 0; i < p.count; i++) if (p.words[i] === undefined) return invalid();
        this.pendingSentence = null;
        clearTimeout(p.timer);
        p.resolve(p.words.slice(0, p.count));
      }
    }
  }
}
