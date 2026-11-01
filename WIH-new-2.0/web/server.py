#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Words in Hand · 本地服务
- 提供静态页面（index.html / serial-bridge.js）
- 代理大模型接口 /api/muse，API Key 只保存在 config.json，不会出现在前端代码里

用法：python server.py 8000
然后浏览器打开 http://localhost:8000
"""

import json
import os
import re
import sys
import urllib.error
import urllib.request
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(ROOT, 'config.json')

MODES = ('critique', 'story', 'learn')

SYSTEM_PROMPTS = {
    'critique': (
        '你是一位温柔而敏锐的文学编辑，名字叫「缪斯」。'
        '用户用英文单词像拼贴诗一样拼出了一句句子。'
        '请用中文点评这句话：关注意象、动词的选择、呼吸与节奏；若有可改进之处，给出一个温柔而具体的建议。'
        '只写 2~3 句话，60~110 字，不分点、不写标题、也不用引号复述原句。'
    ),
    'story': (
        '你是一位擅长写微型故事的作家。'
        '请根据用户用英文单词拼出的句子，用中文写一个 80~150 字的微型故事。'
        '要求：从句子自然生长，有画面、有温度，结尾留白；不要标题，不要分点，不要解释创作思路。'
    ),
    'learn': (
        '你是一位面向中文母语者的英语老师，负责判断学生用英文单词拼出的句子的基础语法，并用中文讲解。'
        '只判断主谓一致、时态、单复数、冠词和常见搭配；不讨论标点符号和首字母大小写，拼贴式短句缺冠词不算错。'
        '判断主谓一致的规则：主语是 he / she / it / 单数名词时，一般现在时动词用三单形式，'
        '例如 “he counts stars” 是正确的；主语是 I / you / we / they / 复数时，动词用原形，'
        '例如 “they count stars” 是正确的。句子形式正确时，不要提出修改建议。'
        '如果句子基础语法没有问题，grammarIssues 只给一条：'
        '{"issue":"基础语法没有明显问题","fix":"无需修改","why":"主语与动词形式一致"}。'
        '请只输出一个 JSON 对象，不要 markdown 代码块、不要任何额外文字，结构如下：'
        '{"grammarIssues":[{"issue":"问题（一句话）","fix":"怎么改（一句话）","why":"为什么（一句话）"}],'
        '"knowledgePoints":[{"title":"知识点名称","detail":"一句话讲解","example":"英文例句"}]}。'
        'grammarIssues 最多 2 条；knowledgePoints 给 1~2 条，只讲最基础的语法点或常见搭配；语言简洁，不做长篇分析。'
    ),
}

MAX_TOKENS = {'critique': 220, 'story': 360, 'learn': 500}


def load_config():
    if not os.path.exists(CONFIG_PATH):
        raise SystemExit('缺少 config.json，请先填写 apiKey / baseUrl / model。')
    with open(CONFIG_PATH, 'r', encoding='utf-8') as f:
        cfg = json.load(f)
    if not cfg.get('apiKey'):
        raise SystemExit('config.json 中缺少 apiKey。')
    if not cfg.get('baseUrl') or not cfg.get('model'):
        raise SystemExit('config.json 中缺少 baseUrl 或 model。')
    return cfg


CONFIG = load_config()


def build_messages(mode, sentence):
    messages = [{'role': 'system', 'content': SYSTEM_PROMPTS[mode]}]
    if mode == 'learn':
        # 两例示例：错误句 → 改对；正确句 → 无需修改。锚定三单方向与输出格式。
        messages.append({'role': 'user', 'content': '句子：he count stars'})
        messages.append({'role': 'assistant', 'content': (
            '{"grammarIssues":[{"issue":"主谓不一致：主语 he 是第三人称单数，动词没有加 -s",'
            '"fix":"把 count 改为 counts","why":"一般现在时中 he/she/it 后的动词要用三单形式"}],'
            '"knowledgePoints":[{"title":"第三人称单数","detail":"主语是 he/she/it 或单数名词时，一般现在时动词要加 -s/-es",'
            '"example":"She reads every evening."}]}'
        )})
        messages.append({'role': 'user', 'content': '句子：he counts stars'})
        messages.append({'role': 'assistant', 'content': (
            '{"grammarIssues":[{"issue":"基础语法没有明显问题","fix":"无需修改",'
            '"why":"主语 he 是第三人称单数，counts 已经是正确的三单形式"}],'
            '"knowledgePoints":[{"title":"第三人称单数","detail":"主语 he/she/it 后一般现在时动词加 -s/-es",'
            '"example":"He counts stars."}]}'
        )})
    messages.append({'role': 'user', 'content': '句子：' + sentence})
    return messages


def parse_json_loose(text):
    text = text.strip()
    text = re.sub(r'^\s*```[a-zA-Z]*', '', text)
    text = re.sub(r'```\s*$', '', text).strip()
    start = text.find('{')
    end = text.rfind('}')
    if start == -1 or end <= start:
        raise ValueError('模型没有返回 JSON 对象')
    candidate = text[start:end + 1]
    # 模型偶尔会把分隔符写成中文全角，修正后重试。
    candidate = candidate.translate(str.maketrans({'：': ':', '，': ','}))
    candidate = re.sub(r',\s*([\]}])', r'\1', candidate)
    return json.loads(candidate)


def as_text(value):
    if isinstance(value, (str, int, float)):
        return str(value)
    if isinstance(value, list):
        return '；'.join(as_text(v) for v in value)
    if isinstance(value, dict):
        return '，'.join('%s：%s' % (k, as_text(v)) for k, v in value.items())
    return ''


def normalize_learn(raw):
    """把模型返回的 JSON 归一成前端可直接渲染的结构。"""
    if not isinstance(raw, dict):
        return {'raw': as_text(raw)}
    out = {}
    issues = raw.get('grammarIssues')
    if isinstance(issues, list):
        out['grammarIssues'] = [
            {
                'issue': as_text(it.get('issue')),
                'fix': as_text(it.get('fix')),
                'why': as_text(it.get('why')),
            }
            for it in issues if isinstance(it, dict) and as_text(it.get('issue'))
        ][:2]
    points = raw.get('knowledgePoints')
    if isinstance(points, list):
        out['knowledgePoints'] = [
            {
                'title': as_text(pt.get('title')),
                'detail': as_text(pt.get('detail')),
                'example': as_text(pt.get('example')),
            }
            for pt in points if isinstance(pt, dict) and as_text(pt.get('title'))
        ][:2]
    return out


# ---------- 主谓一致确定性校验 ----------
# 大模型在 JSON 模式下偶尔会自相矛盾（why 说“需要加 s”，fix 却说“改为原形”）。
# 这里用固定规则复核一般现在时的主谓一致，并过滤模型相关的矛盾条目。

SUBJECT_3SG = {'he', 'she', 'it'}
SUBJECT_PLURAL = {'i', 'you', 'we', 'they'}
MODAL_WORDS = {'can', 'could', 'will', 'would', 'shall', 'should', 'may', 'might', 'must'}

# 常见不规则过去式：出现时跳过主谓一致判断，避免把 lit 改成 lits。
IRREGULAR_PAST = {
    'was', 'were', 'went', 'gone', 'saw', 'seen', 'sang', 'sung', 'held', 'lit',
    'ran', 'wrote', 'written', 'spoke', 'spoken', 'took', 'taken', 'made', 'said',
    'told', 'thought', 'brought', 'bought', 'caught', 'taught', 'found', 'got',
    'gave', 'given', 'kept', 'left', 'lost', 'met', 'paid', 'put', 'sent', 'sat',
    'slept', 'spent', 'stood', 'swam', 'swum', 'won', 'wore', 'worn', 'ate',
    'eaten', 'drank', 'drunk', 'drove', 'driven', 'fell', 'fallen', 'felt',
    'flew', 'flown', 'forgot', 'forgotten', 'grew', 'grown', 'heard', 'hid',
    'hidden', 'hit', 'knew', 'known', 'laid', 'led', 'lent', 'let', 'meant',
    'rode', 'ridden', 'rose', 'risen', 'shone', 'shot', 'shown', 'shut', 'sank',
    'sunk', 'stole', 'stolen', 'struck', 'swept', 'swore', 'sworn', 'threw',
    'thrown', 'understood', 'woke', 'woken', 'bore', 'borne', 'broke', 'broken',
    'chose', 'chosen', 'did', 'done', 'drew', 'drawn', 'fed', 'fought', 'froze',
    'frozen', 'hung', 'lay', 'lain', 'had',
}

# 以 -ed 结尾但其实是一般现在时原形的词
ED_BASE_WORDS = {'need', 'feed', 'seed', 'speed', 'bleed', 'proceed', 'succeed', 'embed'}


def _looks_third_singular(verb):
    if verb in ('is', 'has', 'does', 'goes', 'says'):
        return True
    return (verb.endswith('s') and not verb.endswith('ss')
            and not verb.endswith('us') and not verb.endswith('ous'))


def _third_person(form):
    if form == 'be':
        return 'is'
    if form == 'have':
        return 'has'
    if form == 'do':
        return 'does'
    if form == 'go':
        return 'goes'
    if re.search(r'[^aeiou]y$', form):
        return form[:-1] + 'ies'
    if re.search(r'(s|x|ch|sh|o)$', form):
        return form + 'es'
    return form + 's'


def _base_form(form):
    if form == 'is':
        return 'are'
    if form == 'has':
        return 'have'
    if form == 'does':
        return 'do'
    if form == 'goes':
        return 'go'
    if form.endswith('ies'):
        return form[:-3] + 'y'
    if form.endswith('es'):
        return form[:-2]
    return form[:-1]


def agreement_check(sentence):
    """复核简单句的主谓一致。返回 None 表示无法判断；否则返回 ok / 问题对象。"""
    words = re.findall(r"[a-z']+", sentence.lower())
    if len(words) < 2:
        return None
    subject = None
    idx = -1
    for i, w in enumerate(words):
        if w in SUBJECT_3SG or w in SUBJECT_PLURAL:
            subject, idx = w, i
            break
    if subject is None or idx + 1 >= len(words):
        return None

    verb = words[idx + 1]
    # 情态动词、过去式、副词：主谓一致规则不适用，视为没有问题。
    if verb in MODAL_WORDS or verb in IRREGULAR_PAST or verb.endswith('ly'):
        return {'ok': True, 'why': '句中动词不是一般现在时，主谓一致不适用'}
    if re.search(r'[a-z]ed$', verb) and verb not in ED_BASE_WORDS:
        return {'ok': True, 'why': '句中动词是过去式，主谓一致不适用'}

    third = subject in SUBJECT_3SG
    if third and _looks_third_singular(verb):
        return {'ok': True, 'why': '主语 %s 是第三人称单数，%s 正是正确的三单形式' % (subject, verb)}
    if not third and not _looks_third_singular(verb):
        return {'ok': True, 'why': '主语 %s 不是第三人称单数，动词 %s 用原形正确' % (subject, verb)}

    if third:
        fixed = _third_person(verb)
        return {
            'ok': False,
            'issue': '主谓不一致：主语 %s 是第三人称单数，动词 %s 却是原形' % (subject, verb),
            'fix': '把 %s 改为 %s' % (verb, fixed),
            'why': '一般现在时中 he / she / it 后的动词要用三单形式，即加 -s 或 -es',
        }
    fixed = _base_form(verb)
    return {
        'ok': False,
        'issue': '主谓不一致：主语 %s 不是第三人称单数，动词 %s 却用了三单形式' % (subject, verb),
        'fix': '把 %s 改为 %s' % (verb, fixed),
        'why': '一般现在时中 I / you / we / they 后的动词要用原形',
    }


def _mentions_agreement(text):
    t = text.lower()
    return any(k in t for k in ('主谓', '三单', '第三人称', '单复数', '动词形式', 'agreement', '加 -s', '加-s', '加s'))


def _mentions_article(text):
    t = text.lower()
    return '冠词' in t or 'a/an' in t


def finalize_learn(content, sentence):
    """用确定性规则复核并纠正模型的主谓一致结论。"""
    if not isinstance(content, dict):
        return content
    if 'grammarIssues' not in content and 'knowledgePoints' not in content:
        return content

    issues = content.get('grammarIssues')
    issues = issues if isinstance(issues, list) else []
    check = agreement_check(sentence)

    cleaned = []
    for it in issues:
        blob = ''.join(str(it.get(k, '')) for k in ('issue', 'fix', 'why'))
        # 模型关于主谓一致/冠词的条目可能与规则冲突，一律交给规则层处理。
        if _mentions_agreement(blob) or _mentions_article(blob):
            continue
        cleaned.append(it)

    if check is None:
        content['grammarIssues'] = cleaned if cleaned else issues
        return content
    if check['ok']:
        content['grammarIssues'] = cleaned if cleaned else [{
            'issue': '基础语法没有明显问题',
            'fix': '无需修改',
            'why': check.get('why') or '主语与动词形式一致',
        }]
        return content

    content['grammarIssues'] = ([{
        'issue': check['issue'],
        'fix': check['fix'],
        'why': check['why'],
    }] + cleaned)[:2]
    return content


def upstream_error(code, detail):
    try:
        body = json.loads(detail)
        message = body.get('message') or body.get('error')
        if isinstance(message, dict):
            message = message.get('message')
        if isinstance(message, list):
            message = '；'.join(as_text(m) for m in message)
        if message:
            return '大模型返回 %s：%s' % (code, message)
    except Exception:
        pass
    return '大模型返回 %s：%s' % (code, detail[:200])


def request_upstream(payload):
    req = urllib.request.Request(
        CONFIG['baseUrl'],
        data=json.dumps(payload, ensure_ascii=False).encode('utf-8'),
        headers={
            'Content-Type': 'application/json',
            'Authorization': 'Bearer ' + CONFIG['apiKey'],
        },
        method='POST',
    )
    try:
        with urllib.request.urlopen(req, timeout=CONFIG.get('timeout', 60)) as resp:
            body = json.loads(resp.read().decode('utf-8'))
    except urllib.error.HTTPError as err:
        raise RuntimeError(upstream_error(err.code, err.read().decode('utf-8', 'ignore')))
    except urllib.error.URLError as err:
        raise RuntimeError('无法连接大模型服务：%s' % err.reason)

    try:
        return body['choices'][0]['message']['content']
    except (KeyError, IndexError, TypeError):
        raise RuntimeError('大模型返回格式异常：' + json.dumps(body, ensure_ascii=False)[:200])


def call_model(mode, sentence):
    model = CONFIG.get('learnModel') if mode == 'learn' and CONFIG.get('learnModel') else CONFIG['model']
    payload = {
        'model': model,
        'messages': build_messages(mode, sentence),
        'temperature': CONFIG.get('temperature', 0.8),
        'max_tokens': MAX_TOKENS[mode],
        'stream': False,
    }
    if mode == 'learn':
        payload['temperature'] = 0.2
        payload['response_format'] = {'type': 'json_object'}

    text = request_upstream(payload)
    if mode != 'learn':
        return text.strip()

    # 讲评需要结构化展示：解析失败时自动重试一次，仍失败则原样返回文本。
    for attempt in range(2):
        try:
            return finalize_learn(normalize_learn(parse_json_loose(text)), sentence)
        except Exception:
            if attempt == 1:
                break
            payload['temperature'] = 0.2
            payload['messages'] = payload['messages'] + [{
                'role': 'user',
                'content': '上次输出无法解析为 JSON。请重新只输出一个合法的 JSON 对象，所有键和字符串都用英文半角双引号，不要代码块。'
            }]
            text = request_upstream(payload)
    return {'raw': text.strip()}


class Handler(SimpleHTTPRequestHandler):

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

    def do_GET(self):
        path = self.path.split('?')[0].lstrip('/').lower()
        if path in ('config.json', 'server.py'):
            self.send_json(403, {'ok': False, 'error': 'Forbidden'})
            return
        super().do_GET()

    def do_POST(self):
        path = self.path.split('?')[0]
        if path != '/api/muse':
            self.send_json(404, {'ok': False, 'error': 'Not found'})
            return
        try:
            length = int(self.headers.get('Content-Length') or 0)
            payload = json.loads(self.rfile.read(length) or b'{}')
        except Exception:
            self.send_json(400, {'ok': False, 'error': '请求体不是合法 JSON'})
            return

        sentence = ' '.join(str(payload.get('sentence') or '').split())[:600]
        mode = payload.get('mode', 'critique')
        if mode not in MODES:
            self.send_json(400, {'ok': False, 'error': '未知的 mode：%s' % mode})
            return
        if not sentence:
            self.send_json(400, {'ok': False, 'error': '句子为空'})
            return

        sys.stderr.write('[muse] %s ← %s\n' % (mode, sentence))
        try:
            content = call_model(mode, sentence)
            self.send_json(200, {'ok': True, 'mode': mode, 'content': content})
        except Exception as err:
            self.send_json(502, {'ok': False, 'error': str(err)})

    def send_json(self, code, obj):
        data = json.dumps(obj, ensure_ascii=False).encode('utf-8')
        self.send_response(code)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

    def log_message(self, fmt, *args):
        sys.stderr.write('[%s] %s\n' % (self.log_date_time_string(), fmt % args))


if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    print('Words in Hand 已启动 → http://localhost:%d' % port)
    ThreadingHTTPServer(('127.0.0.1', port), Handler).serve_forever()
