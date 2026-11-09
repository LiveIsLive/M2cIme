#!/usr/bin/env python3
"""把 RIME .dict.yaml 词库导入为 SQLite 数据库（运行时词库 dicts/m2c_dict.db）。

用法（在仓库根目录执行）：
    python tools/import_rime_dict.py <输出.db> <yaml>:<来源>:<权重列> [更多 yaml...]

来源：0 = 普通话，1 = 粤语；权重列：1 = 第三列是词频，0 = 按行序生成递减权重。

示例（重建项目原本的词库，需要从 git 历史或上游取回对应的 RIME 词库文件）：
    python tools/import_rime_dict.py dicts/m2c_dict.db pinyin_simp.dict.yaml:0:1
    python tools/import_rime_dict.py dicts/m2c_dict.db joy.pinyin_yueyu.dict.yaml:1:0  # 单独导入粤语

词条字段（拼音、声母、音节长度等）在这里一次性算好写入数据库，
运行时 Dictionary 直接读列，不再重复切分。
"""

import re
import sqlite3
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SEGMENTER_CPP = REPO_ROOT / "MandarinToCantonese" / "PinyinSegmenter.cpp"
OPENCC_DIR = REPO_ROOT / "tools" / "opencc"


def load_syllables():
    """从 PinyinSegmenter.cpp 的 kSyllables 表提取合法音节，避免两处维护。"""
    text = SEGMENTER_CPP.read_text(encoding="utf-8-sig")
    block = re.search(r"kSyllables\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if not block:
        raise SystemExit(f"无法在 {SEGMENTER_CPP} 中定位 kSyllables 音节表")
    return set(re.findall(r'"([a-z]+)"', block.group(1)))


SYLLABLES = load_syllables()


class S2TConverter:
    """简体 -> 繁体转换（OpenCC 的 STCharacters + STPhrases，最长词组优先）。"""

    def __init__(self, directory: Path):
        self.chars = self._load(directory / "STCharacters.txt")
        self.phrases = self._load(directory / "STPhrases.txt")
        self.max_phrase_len = max((len(k) for k in self.phrases), default=1)
        self.cache = {}

    @staticmethod
    def _load(path: Path):
        table = {}
        for line in path.read_text(encoding="utf-8-sig").splitlines():
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 2:
                continue
            key = parts[0]
            value = parts[1].split(" ")[0]  # 多个写法时取 OpenCC 默认的第一个
            if key and value:
                table[key] = value
        return table

    def convert(self, text: str) -> str:
        cached = self.cache.get(text)
        if cached is not None:
            return cached

        out = []
        i = 0
        n = len(text)
        while i < n:
            hit = None
            for length in range(min(self.max_phrase_len, n - i), 0, -1):
                phrase = text[i:i + length]
                if phrase in self.phrases:
                    hit = (length, self.phrases[phrase])
                    break
            if hit:
                out.append(hit[1])
                i += hit[0]
            else:
                out.append(self.chars.get(text[i], text[i]))
                i += 1

        result = "".join(out)
        self.cache[text] = result
        return result


try:
    S2T = S2TConverter(OPENCC_DIR)
except OSError:
    S2T = None
    print(f"警告: 未找到 {OPENCC_DIR}，word_trad 将留空（运行时使用 Windows 逐字兜底映射）")


def traditional_of(word: str) -> str:
    return S2T.convert(word) if S2T else ""


def segment(text):
    """正向最大匹配切分，返回 (音节列表, 剩余无法切分的部分)。"""
    out = []
    i, n = 0, len(text)
    while i < n:
        best = 0
        for length in range(6, 0, -1):
            if i + length <= n and text[i:i + length] in SYLLABLES:
                best = length
                break
        if best == 0:
            return out, text[i:]
        out.append(text[i:i + best])
        i += best
    return out, ""


def split_syllables(raw, key):
    """按空格/隔音符号拆分；没写分隔符时退回自动切分。"""
    out, cur = [], []
    for c in raw:
        if c in " '":
            if cur:
                out.append("".join(cur))
                cur = []
        else:
            cur.append(c.lower())
    if cur:
        out.append("".join(cur))

    if len(out) <= 1:
        seg, remainder = segment(key)
        if seg and not remainder:
            out = seg
    return out


def initial_of(syllable):
    """zh/ch/sh 取两个字母，其余取首字母。"""
    return syllable[:2] if syllable[:2] in ("zh", "ch", "sh") else syllable[:1]


def is_nasal_only(key):
    """首音节是鼻音自成音节（n/ng/m/hm/hng）。"""
    if len(key) == 1:
        return key in ("n", "m")
    return key.startswith("hng") or key[:2] in ("ng", "hm")


def is_valid_pinyin(key):
    syllables, remainder = segment(key)
    return bool(syllables) and not remainder


def parse_yaml(path, source, has_weight):
    """解析 RIME 词库，产出词条字典；与 C++ 侧的解析规则保持一致。"""
    entries = []
    header_done = False

    with open(path, "rb") as f:
        for raw_line in f.read().split(b"\n"):
            if raw_line.endswith(b"\r"):
                raw_line = raw_line[:-1]

            if not header_done:
                header_done = raw_line == b"..."
                continue

            if not raw_line or raw_line.startswith(b"#"):
                continue

            first = raw_line.find(b"\t")
            if first < 0:
                continue
            second = raw_line.find(b"\t", first + 1)

            word = raw_line[:first]
            pinyin_raw = raw_line[first + 1:] if second < 0 else raw_line[first + 1:second]

            if has_weight and second >= 0:
                m = re.match(rb"\s*([+-]?\d+)", raw_line[second + 1:])
                weight = int(m.group(1)) if m else 0
            else:
                weight = 1000000 - len(entries)

            key = "".join(c for c in pinyin_raw.decode("utf-8").lower() if c not in " '")
            if not word or not key:
                continue

            syllables = split_syllables(pinyin_raw.decode("utf-8"), key)
            word_text = word.decode("utf-8")
            entries.append((
                word_text,
                traditional_of(word_text),
                key,
                "".join(initial_of(s) for s in syllables),
                bytes(len(s) for s in syllables),
                weight,
                source,
                1 if is_nasal_only(key) else 0,
                1 if is_valid_pinyin(key) else 0,
            ))

    return entries


def build(db_path, sources):
    db_path.parent.mkdir(parents=True, exist_ok=True)
    if db_path.exists():
        db_path.unlink()

    con = sqlite3.connect(db_path)
    con.executescript(
        """
        PRAGMA journal_mode = OFF;
        PRAGMA synchronous = OFF;
        CREATE TABLE entries (
            id           INTEGER PRIMARY KEY,
            word         TEXT    NOT NULL,
            word_trad    TEXT    NOT NULL DEFAULT '',
            pinyin       TEXT    NOT NULL,
            initials     TEXT    NOT NULL,
            syl_lens     BLOB    NOT NULL,
            weight       INTEGER NOT NULL,
            source       INTEGER NOT NULL,
            nasal_only   INTEGER NOT NULL,
            valid_pinyin INTEGER NOT NULL
        );
        CREATE INDEX entries_pinyin_idx ON entries (pinyin);
        PRAGMA user_version = 2;
        """
    )

    total = 0
    for path, source, has_weight in sources:
        rows = parse_yaml(path, source, has_weight)
        con.executemany(
            "INSERT INTO entries (word, word_trad, pinyin, initials, syl_lens, weight, source,"
            " nasal_only, valid_pinyin) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
            rows,
        )
        total += len(rows)
        print(f"导入 {path.name}: {len(rows)} 条（source={source}）")

    con.commit()
    con.execute("VACUUM")
    con.commit()
    con.close()
    print(f"已生成 {db_path}，共 {total} 条")


def main(argv):
    if len(argv) <= 2:
        raise SystemExit(__doc__.strip())

    db_path = Path(argv[1]).resolve()
    sources = []
    for spec in argv[2:]:
        path, source, has_weight = spec.rsplit(":", 2)
        sources.append((Path(path).resolve(), int(source), int(has_weight)))

    missing = [str(p) for p, _, _ in sources if not p.exists()]
    if missing:
        raise SystemExit("找不到词库文件:\n  " + "\n  ".join(missing))

    build(db_path, sources)


if __name__ == "__main__":
    main(sys.argv)
