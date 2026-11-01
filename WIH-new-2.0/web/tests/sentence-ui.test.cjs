// Optional DOM integration test:
// npm install --no-save jsdom
// node --experimental-vm-modules --test tests/sentence-ui.test.cjs
const { test } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { JSDOM } = require('jsdom');
const settle = () => new Promise(resolve => setImmediate(resolve));
const identity = { protocol: 1, device: '00000000000000f1', session: '00000000000000a1' };

test('actual page button connects, imports ordered tiles, handles empty/errors, preserves single-word flow', async () => {
  const html = readFileSync(path.join(__dirname, '..', 'index.html'), 'utf8');
  const dom = new JSDOM(html, { url: 'http://localhost:8000', runScripts: 'outside-only' });
  const w = dom.window;
  w.scrollTo = () => {};
  Object.assign(w, { TextEncoder, TextDecoder });
  let incoming, words = ['the', 'moon', 'sleeps'];
  const send = msg => incoming.enqueue(new TextEncoder().encode('WIH:' + JSON.stringify({ ...identity, ...msg }) + '\n'));
  const port = {
    readable: null, writable: null,
    async open() {
      this.readable = new ReadableStream({ start(c) { incoming = c; } });
      this.writable = new WritableStream({
        write(bytes) {
          const command = JSON.parse(new TextDecoder().decode(bytes).slice(4));
          const base = { request: command.request, count: words.length };
          send({ ...base, type: 'sentence_begin' });
          words.forEach((word, index) => send({ ...base, type: 'sentence_word', word, index }));
          send({ ...base, type: 'sentence_end' });
        }
      });
    },
    async setSignals() {},
    async close() { this.readable = this.writable = null; }
  };
  Object.defineProperty(w.navigator, 'serial', { value: { async requestPort() { return port; } } });
  const scripts = [...html.matchAll(/<script(?:\s[^>]*)?>([\s\S]*?)<\/script>/g)].map(m => m[1]);
  const errors = [];
  w.console.error = (...args) => errors.push(args);
  const context = dom.getInternalVMContext();
  new vm.Script(scripts[0]).runInContext(context);
  const serial = new vm.SourceTextModule(readFileSync(path.join(__dirname, '..', 'serial-bridge.js'), 'utf8'), { context });
  const page = new vm.SourceTextModule(scripts[1], { context });
  await page.link(() => serial);
  await page.evaluate();
  const click = id => w.document.getElementById(id).click();
  const text = id => w.document.getElementById(id).textContent;
  const tiles = () => [...w.document.querySelectorAll('#sentence-line .tile')].map(x => x.textContent);
  try {
    click('btn-import-sentence');
    await settle();
    assert.match(text('sentence-import-status'), /先连接/);
    click('chip-toggle');
    await settle();
    send({ type: 'ready', sentenceExport: 1 });
    await settle();
    send({ type: 'word_confirmed', seq: 1, word: 'hello' });
    await settle();
    assert.deepEqual(tiles(), ['hello']);
    click('btn-import-sentence');
    await settle();
    assert.deepEqual(tiles(), ['hello', 'the', 'moon', 'sleeps']);
    assert.match(text('sentence-import-status'), /已放上 3 个词/);
    assert.equal(w.document.getElementById('btn-import-sentence').disabled, false);
    click('btn-import-sentence');
    await settle();
    assert.deepEqual(tiles(), ['hello', 'the', 'moon', 'sleeps', 'the', 'moon', 'sleeps']);
    words = [];
    click('btn-import-sentence');
    await settle();
    assert.equal(tiles().length, 7);
    assert.match(text('sentence-import-status'), /没有保存句子/);
    const bankWords = () => [...w.document.querySelectorAll('#bank-tiles .tile')].map(t => t.textContent);
    const firstBank = bankWords();
    assert.equal(firstBank.length, 30);
    assert.equal(new Set(firstBank).size, 30);
    const keptSentence = tiles();
    click('btn-refresh-bank');
    assert.notDeepEqual(bankWords(), firstBank);
    assert.deepEqual(tiles(), keptSentence);
    const bankTile = w.document.querySelector('#bank-tiles .tile');
    const chosen = bankTile.textContent;
    bankTile.click();
    assert.equal(tiles().at(-1), chosen);
    assert.equal(bankTile.disabled, true);
    click('btn-refresh-bank');
    assert.equal(tiles().at(-1), chosen);
    assert.equal(bankWords().includes(chosen), false);
    w.Math.random = () => 0;
    click('btn-refresh-bank');
    const fixedBank = bankWords();
    click('btn-refresh-bank');
    assert.notDeepEqual(bankWords(), fixedBank);
    click('btn-clear');
    assert.deepEqual(tiles(), []);
    words = ['we', 'we'];
    click('btn-import-sentence');
    await settle();
    assert.deepEqual(tiles(), ['we', 'we']);
    click('btn-undo');
    assert.deepEqual(tiles(), ['we']);
    send({ type: 'word_confirmed', seq: 2, word: 'still' });
    await settle();
    assert.deepEqual(tiles(), ['we', 'still']);
    click('chip-toggle');
    await settle();
    assert.equal(w.document.querySelectorAll('.transfer-actions > button').length, 1);
    assert.equal(w.document.getElementById('btn-import-word'), null);
    assert.equal(w.document.getElementById('hardware-word-hint').tagName, 'DIV');
    assert.match(text('hardware-word-hint'), /按模块 A/);
    assert.match(text('hardware-word-status'), /刚刚收到：still/);
    assert.deepEqual(errors, []);
  } finally { dom.window.close(); }
});
