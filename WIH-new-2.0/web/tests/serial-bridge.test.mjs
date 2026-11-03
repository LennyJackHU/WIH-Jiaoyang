import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

// Load the browser ES module without changing the site's package/module settings.
const source = await readFile(new URL('../serial-bridge.js', import.meta.url), 'utf8');
const { WIHSerialBridge } = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
const encoder = new TextEncoder();
const identity = { protocol: 1, device: '00000000000000f1', session: '00000000000000a1' };
const ready = () => ({ type: 'ready', ...identity });
const word = (seq = 1, text = 'hello', extra = {}) => ({
  type: 'word_confirmed', ...identity, word: text, seq, ...extra
});
const line = (msg) => `WIH:${JSON.stringify(msg)}\n`;
const settle = () => new Promise(resolve => setImmediate(resolve));

function fixture(serial = null) {
  const words = [], statuses = [];
  const bridge = new WIHSerialBridge({
    serial,
    onWord: (text, msg) => words.push({ text, msg }),
    onStatus: (text, state) => statuses.push({ text, ...state })
  });
  return { bridge, words, statuses };
}

function mockPort({ openError, readerError, closeFailures = 0, signalsError = false } = {}) {
  let controller;
  const port = {
    readable: null, writable: null, opens: 0, closes: 0, cancels: 0, writes: [],
    async open(options) {
      this.opens++;
      this.options = options;
      if (openError) throw openError;
      this.readable = new ReadableStream({
        start(c) { controller = c; },
        cancel() { port.cancels++; }
      });
      this.writable = new WritableStream({ write(bytes) { port.writes.push(new TextDecoder().decode(bytes)); } });
      if (readerError) this.readable.getReader = () => { throw readerError; };
    },
    async setSignals(signals) {
      this.signals = signals;
      if (signalsError) throw new Error('signals unsupported');
    },
    async close() {
      this.closes++;
      assert.equal(this.readable?.locked ?? false, false, 'release reader before closing');
      if (closeFailures-- > 0) throw new Error('close failed');
      this.readable = this.writable = null;
    },
    send(text) { controller.enqueue(encoder.encode(text)); },
    end() { controller.close(); },
    unplug() { controller.error(new Error('unplugged')); }
  };
  return port;
}

function chooser(...ports) {
  return {
    calls: 0,
    async requestPort() {
      const port = ports[this.calls++];
      if (port instanceof Error) throw port;
      assert.ok(port, 'unexpected additional chooser call');
      return port;
    }
  };
}

test('fragmented UTF-8, coalesced frames, CRLF and console logs', () => {
  const f = fixture();
  f.bridge.running = true;
  const data = encoder.encode('alive ms=2000\n' + line(ready()) +
    line(word(1, '你好')).replace('\n', '\r\n') + line(word(2)));
  for (const byte of data) f.bridge.acceptChunk(Uint8Array.of(byte));
  assert.deepEqual(f.words.map(x => x.text), ['你好', 'hello']);
  assert.equal(f.statuses.at(-1).connected, true);
});

test('logs, invalid JSON, invalid identities and unknown protocol are ignored', () => {
  const f = fixture();
  const invalid = [null, [], 7, {}, { type: 'ready' },
    { ...ready(), device: 'bad' }, { ...ready(), session: null },
    { ...word(), protocol: 2 }, { ...word(), protocol: '1' }];
  f.bridge.acceptChunk(encoder.encode('boot\nWIH:{bad}\n' + invalid.map(line).join('')));
  assert.equal(f.words.length, 0);
  assert.equal(f.bridge.recognized, false);
});

test('invalid words/sequences are ignored; UTF-8 byte limit is enforced', () => {
  const f = fixture();
  for (const text of ['', '   ', 'a\nb', 'a\u007fb', 'a'.repeat(97), '你'.repeat(33), null]) {
    f.bridge.acceptLine(line(word(1, text)).trim());
  }
  for (const seq of [-1, 1.5, 0x100000000, '1', null, undefined]) {
    f.bridge.acceptLine(line({ ...word(), seq }).trim());
  }
  assert.equal(f.words.length, 0);
  f.bridge.acceptLine(line(word(2, '你'.repeat(32))).trim());
  assert.equal(f.words.length, 1);
});

test('oversized lines are discarded until newline, then recover', () => {
  const f = fixture();
  f.bridge.acceptChunk(encoder.encode('x'.repeat(1100) + line(word())));
  assert.equal(f.words.length, 0);
  assert.equal(f.bridge.buffer.length, 0);
  f.bridge.acceptChunk(encoder.encode(line(word(2))));
  assert.equal(f.words.length, 1);
});

test('dedup uses device/session/sequence, not word text', () => {
  const f = fixture();
  for (const msg of [word(), word(), word(2),
    word(1, 'hello', { session: '00000000000000a2' }),
    word(1, 'hello', { device: '00000000000000f2' }),
    word(1, 'hello', { device: identity.device.toUpperCase() })]) {
    f.bridge.acceptLine(line(msg).trim());
  }
  assert.equal(f.words.length, 4);
});

test('dedup survives parser reset and has bounded memory', () => {
  const f = fixture();
  f.bridge.acceptLine(line(word()).trim());
  f.bridge.resetParser();
  f.bridge.acceptLine(line(word()).trim());
  assert.equal(f.words.length, 1);
  for (let seq = 2; seq <= 1050; seq++) f.bridge.acceptLine(line(word(seq)).trim());
  assert.equal(f.bridge.seen.size, 1024);
});

test('legacy word events work and reset their sequence on reconnect', () => {
  const f = fixture();
  const legacy = { type: 'word_confirmed', word: 'old', seq: 1 };
  f.bridge.acceptLine(line(legacy).trim());
  f.bridge.acceptLine(line(legacy).trim());
  assert.equal(f.words.length, 1);
  f.bridge.resetParser();
  f.bridge.acceptLine(line(legacy).trim());
  f.bridge.acceptLine(line({ type: 'word_confirmed', word: 'no sequence' }).trim());
  assert.equal(f.words.length, 3);
});

test('a page callback failure does not stop parsing subsequent events', () => {
  let calls = 0;
  const bridge = new WIHSerialBridge({ onWord() { calls++; throw new Error('page failure'); } });
  const original = console.error;
  console.error = () => {};
  try { bridge.acceptChunk(encoder.encode(line(word(1)) + line(word(2)))); }
  finally { console.error = original; }
  assert.equal(calls, 2);
});

test('opening a port waits for a real firmware frame; disconnect releases resources', async () => {
  const port = mockPort();
  const f = fixture(chooser(port));
  await f.bridge.connect();
  assert.deepEqual(port.options, { baudRate: 115200 });
  assert.deepEqual(port.signals, { dataTerminalReady: false, requestToSend: false });
  assert.equal(f.statuses.at(-1).connected, false);
  assert.equal(f.statuses.at(-1).running, true);
  port.send('ordinary serial log\n');
  await settle();
  assert.equal(f.bridge.recognized, false);
  port.send(line(ready()));
  await settle();
  assert.equal(f.statuses.at(-1).connected, true);
  await f.bridge.disconnect();
  assert.equal(port.cancels, 1);
  assert.equal(port.closes, 1);
  assert.equal(f.bridge.reader, null);
  assert.equal(f.bridge.port, null);
  assert.equal(f.statuses.at(-1).connected, false);
});

test('word can identify firmware without seeing the ready frame', async () => {
  const port = mockPort();
  const f = fixture(chooser(port));
  await f.bridge.connect();
  port.send(line(word()));
  await settle();
  assert.equal(f.words.length, 1);
  assert.equal(f.statuses.at(-1).connected, true);
  await f.bridge.disconnect();
});

test('unplug cleans up and reconnect shows chooser again without replay duplicates', async () => {
  const a = mockPort(), b = mockPort();
  const serial = chooser(a, b);
  const f = fixture(serial);
  await f.bridge.connect();
  a.send(line(word()));
  await settle();
  a.unplug();
  await f.bridge.loopPromise;
  assert.match(f.bridge.status, /连接中断/);
  assert.equal(f.bridge.running, false);
  assert.equal(f.bridge.port, null);
  await f.bridge.connect();
  b.send(line(word()) + line(word(2)));
  await settle();
  assert.equal(f.words.length, 2);
  assert.equal(serial.calls, 2);
  await f.bridge.disconnect();
});

test('stream end is reported as a disconnected port', async () => {
  const port = mockPort();
  const f = fixture(chooser(port));
  await f.bridge.connect();
  port.end();
  await f.bridge.loopPromise;
  assert.match(f.bridge.status, /连接中断/);
  assert.equal(port.closes, 1);
});

test('synchronous getReader failure does not leave a stale busy guard', async () => {
  const bad = mockPort({ readerError: new Error('reader failed') }), good = mockPort();
  const f = fixture(chooser(bad, good));
  await f.bridge.connect();
  await f.bridge.loopPromise;
  assert.equal(f.bridge.busy, false);
  assert.equal(f.bridge.closing, false);
  assert.equal(f.bridge.running, false);
  await f.bridge.connect();
  assert.equal(f.bridge.running, true);
  await f.bridge.disconnect();
});

test('failed close retains the port for retry before opening another', async () => {
  const bad = mockPort({ closeFailures: 1 }), good = mockPort();
  const f = fixture(chooser(bad, good));
  await f.bridge.connect();
  bad.unplug();
  await f.bridge.loopPromise;
  assert.equal(f.bridge.port, bad);
  assert.match(f.bridge.status, /关闭串口失败/);
  await f.bridge.connect();
  assert.equal(bad.closes, 2);
  assert.equal(f.bridge.port, good);
  await f.bridge.disconnect();
});

test('user cancelling the chooser is recoverable', async () => {
  const cancel = new Error('cancel');
  cancel.name = 'NotFoundError';
  const port = mockPort();
  const f = fixture(chooser(cancel, port));
  await assert.rejects(f.bridge.connect(), { name: 'NotFoundError' });
  assert.equal(f.bridge.status, '未连接');
  assert.equal(f.bridge.busy, false);
  await f.bridge.connect();
  await f.bridge.disconnect();
});

test('open failure is reported without claiming connection', async () => {
  const port = mockPort({ openError: new Error('port occupied') });
  const f = fixture(chooser(port));
  await assert.rejects(f.bridge.connect(), /port occupied/);
  assert.match(f.bridge.status, /连接失败/);
  assert.equal(f.bridge.port, null);
  assert.equal(f.bridge.busy, false);
});

test('unsupported Web Serial gives an actionable error', async () => {
  const f = fixture();
  await assert.rejects(f.bridge.connect(), /Chrome\/Edge/);
  assert.equal(f.bridge.running, false);
  assert.equal(f.bridge.busy, false);
});

test('unsupported signal setting is nonfatal', async () => {
  const port = mockPort({ signalsError: true });
  const f = fixture(chooser(port));
  const original = console.debug;
  console.debug = () => {};
  try { await f.bridge.connect(); } finally { console.debug = original; }
  assert.equal(f.bridge.running, true);
  await f.bridge.disconnect();
});

test('simultaneous connect/disconnect attempts do not open duplicate readers', async () => {
  const port = mockPort();
  const serial = chooser(port);
  const f = fixture(serial);
  await Promise.all([f.bridge.connect(), f.bridge.connect()]);
  assert.equal(serial.calls, 1);
  await Promise.all([f.bridge.disconnect(), f.bridge.disconnect()]);
  assert.equal(port.closes, 1);
});

test('partial lines from a previous connection are not joined with new data', async () => {
  const a = mockPort(), b = mockPort();
  const f = fixture(chooser(a, b));
  await f.bridge.connect();
  a.send('WIH:{"type":');
  await settle();
  await f.bridge.disconnect();
  await f.bridge.connect();
  b.send(line(word()));
  await settle();
  assert.equal(f.words.length, 1);
  await f.bridge.disconnect();
});


async function sentenceFixture() {
  const port = mockPort();
  const f = fixture(chooser(port));
  await f.bridge.connect();
  port.send(line({ ...ready(), sentenceExport: 1 }));
  await settle();
  return { ...f, port };
}
async function startSentence(f, options) {
  const result = f.bridge.requestSentence(options);
  await settle();
  const cmd = JSON.parse(f.port.writes.at(-1).slice(4));
  assert.equal(cmd.type, 'get_sentence');
  assert.equal(cmd.protocol, 1);
  assert.match(cmd.request, /^[0-9a-f]{16}$/);
  return { result, request: cmd.request };
}
function snapshot(f, request, type, count, extra = {}) {
  f.port.send(line({ ...identity, request, type, count, ...extra }));
}

test('sentence request writes command and commits ordered repeated words only at end', async () => {
  const f = await sentenceFixture();
  try {
    const { result, request } = await startSentence(f);
    let completed = false;
    result.then(() => { completed = true; });
    snapshot(f, request, 'sentence_begin', 3);
    snapshot(f, request, 'sentence_word', 3, { index: 2, word: '你好' });
    snapshot(f, request, 'sentence_word', 3, { index: 0, word: 'hello' });
    snapshot(f, request, 'sentence_word', 3, { index: 1, word: 'hello' });
    f.port.send(line(word(1, 'parallel')));
    await settle();
    assert.equal(completed, false);
    assert.deepEqual(f.words.map(x => x.text), ['parallel']);
    snapshot(f, request, 'sentence_end', 3);
    assert.deepEqual(await result, ['hello', 'hello', '你好']);
    assert.equal(f.bridge.pendingSentence, null);
  } finally { await f.bridge.disconnect(); }
});

test('empty snapshot resolves empty and unsolicited/old request frames do nothing', async () => {
  const f = await sentenceFixture();
  try {
    snapshot(f, '0000000000000000', 'sentence_begin', 2);
    const { result, request } = await startSentence(f);
    snapshot(f, '0000000000000000', 'sentence_end', 2);
    snapshot(f, request, 'sentence_begin', 0, { session: '00000000000000a2' });
    snapshot(f, request, 'sentence_begin', 0);
    snapshot(f, request, 'sentence_end', 0);
    assert.deepEqual(await result, []);
  } finally { await f.bridge.disconnect(); }
});

test('missing words, changed count, invalid text/index and conflicting duplicates reject atomically', async () => {
  for (const bad of [
    { type: 'sentence_end' },
    { type: 'sentence_word', count: 2, index: 0, word: 'hello' },
    { type: 'sentence_word', index: 1, word: 'hello' },
    { type: 'sentence_word', index: 0, word: 'a\nb' },
    { type: 'sentence_word', index: 0, word: 'a'.repeat(97) },
    { type: 'sentence_word', index: 0, word: '你'.repeat(33) }
  ]) {
    const f = await sentenceFixture();
    try {
      const { result, request } = await startSentence(f);
      const rejected = assert.rejects(result, /不完整或无效/);
      snapshot(f, request, 'sentence_begin', 1);
      snapshot(f, request, bad.type, bad.count ?? 1, bad);
      await rejected;
      assert.equal(f.words.length, 0);
    } finally { await f.bridge.disconnect(); }
  }
  const f = await sentenceFixture();
  try {
    const { result, request } = await startSentence(f);
    const rejected = assert.rejects(result, /不完整或无效/);
    snapshot(f, request, 'sentence_begin', 1);
    snapshot(f, request, 'sentence_word', 1, { index: 0, word: 'a' });
    snapshot(f, request, 'sentence_word', 1, { index: 0, word: 'b' });
    await rejected;
  } finally { await f.bridge.disconnect(); }
});

test('12 maximum UTF-8 words and fragmented frames stay under parser line limit', async () => {
  const f = await sentenceFixture();
  try {
    const { result, request } = await startSentence(f);
    snapshot(f, request, 'sentence_begin', 12);
    await settle();
    const words = Array(12).fill('你'.repeat(32));
    for (let index = 0; index < words.length; index++) {
      const text = line({ ...identity, request, type: 'sentence_word', count: 12, index, word: words[index] });
      for (const byte of encoder.encode(text)) f.bridge.acceptChunk(Uint8Array.of(byte));
    }
    snapshot(f, request, 'sentence_end', 12);
    assert.deepEqual(await result, words);
  } finally { await f.bridge.disconnect(); }
});

test('timeout clears pending state and retry can complete', async () => {
  const f = await sentenceFixture();
  try {
    const result = f.bridge.requestSentence({ timeoutMs: 15 });
    await assert.rejects(result, /超时/);
    assert.equal(f.bridge.pendingSentence, null);
    const next = await startSentence(f);
    snapshot(f, next.request, 'sentence_begin', 0);
    snapshot(f, next.request, 'sentence_end', 0);
    assert.deepEqual(await next.result, []);
  } finally { await f.bridge.disconnect(); }
});

test('disconnect, unplug and reboot reject a pending import', async () => {
  for (const action of ['disconnect', 'unplug', 'reboot']) {
    const f = await sentenceFixture();
    try {
      const { result } = await startSentence(f);
      const rejected = assert.rejects(result, /断开|中断|重启/);
      if (action === 'disconnect') await f.bridge.disconnect();
      else if (action === 'unplug') f.port.unplug();
      else f.port.send(line({ ...ready(), sentenceExport: 1, session: '00000000000000a2' }));
      await rejected;
      assert.equal(f.bridge.pendingSentence, null);
    } finally { await f.bridge.disconnect(); }
  }
});

test('old firmware, no connection and concurrent request give actionable errors', async () => {
  const f = fixture();
  await assert.rejects(f.bridge.requestSentence(), /先连接/);
  const live = await sentenceFixture();
  try {
    live.bridge.sentenceExport = false;
    await assert.rejects(live.bridge.requestSentence(), /新版固件/);
    live.bridge.sentenceExport = true;
    const { result, request } = await startSentence(live);
    await assert.rejects(live.bridge.requestSentence(), /稍候/);
    snapshot(live, request, 'sentence_begin', 0);
    snapshot(live, request, 'sentence_end', 0);
    await result;
  } finally { await live.bridge.disconnect(); }
});

test('write failure releases writable lock and clears pending request', async () => {
  const f = await sentenceFixture();
  const writable = new WritableStream({ write() { throw new Error('write failed'); } });
  f.port.writable = writable;
  try {
    await assert.rejects(f.bridge.requestSentence(), /发送整句请求失败/);
    await settle();
    assert.equal(f.bridge.pendingSentence, null);
    assert.equal(writable.locked, false);
  } finally { await f.bridge.disconnect(); }
});


test('module-initiated upload commits only a complete ordered sequence', async () => {
  const f = await sentenceFixture();
  const uploaded = [];
  f.bridge.onSentence = words => uploaded.push(words);
  try {
    const request = '0123456789abcdef';
    const event = (type, extra = {}) => f.port.send(line({
      ...identity, type, request, count: 2, ...extra
    }));
    event('upload_begin');
    event('upload_word', { index: 0, word: 'hello' });
    event('upload_end');
    await settle();
    assert.deepEqual(uploaded, []);
    event('upload_word', { index: 1, word: 'world' });
    event('upload_end');
    await settle();
    assert.deepEqual(uploaded, [['hello', 'world']]);
  } finally { await f.bridge.disconnect(); }
});
