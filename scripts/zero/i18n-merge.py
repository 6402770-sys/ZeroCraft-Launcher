# SPDX-License-Identifier: GPL-3.0-only
# ZERO Launcher - checks translated chunks and merges them into launcher/resources/zero/i18n/zero_es.ts (docs/zero/I18N.md).
#
#   python scripts/zero/i18n-merge.py check <chunk.es.json>...          validate only (placeholders, tags, mnemonics, plural forms)
#   python scripts/zero/i18n-merge.py merge <ts file> <chunk.es.json>... validate, then fill the entries and mark them finished
#   python scripts/zero/i18n-merge.py export <ts file> <out prefix> <n>  write the unfinished entries as n chunks <prefix>-<i>.json
#                                                                          (same order as the .ts, no "translation" yet)
#
# A chunk is a JSON list of {"context", "source", ["comment"], "translation"} or, for plural entries ("numerus": true),
# {"context", "source", ["comment"], "numerus": true, "forms": ["<one>", "<other>"]}. Run scripts/zero/i18n-update.sh afterwards so
# lupdate writes the file back in its own format.
import json
import re
import sys
import xml.etree.ElementTree as ET

PLACEHOLDER = re.compile(r"%(?:L?\d+|n)")
TAG = re.compile(r"</?([a-zA-Z][a-zA-Z0-9]*)")


def problems(entry):
    out = []
    src = entry["source"]
    texts = entry.get("forms") if entry.get("numerus") else [entry.get("translation")]
    if entry.get("numerus") and (not isinstance(texts, list) or len(texts) != 2):
        return ["plural entry needs \"forms\": [one, other]"]
    for text in texts:
        if not isinstance(text, str) or not text.strip():
            out.append("empty translation")
            continue
        if sorted(set(PLACEHOLDER.findall(src))) != sorted(set(PLACEHOLDER.findall(text))):
            out.append(f"placeholders differ: {sorted(set(PLACEHOLDER.findall(src)))} vs {sorted(set(PLACEHOLDER.findall(text)))}")
        if sorted(t.lower() for t in TAG.findall(src)) != sorted(t.lower() for t in TAG.findall(text)):
            out.append("HTML tags differ")
        # a mnemonic is "&" + letter/digit that is not an entity ("&amp;"); "Client & server" has no mnemonic
        mnemonic = re.compile(r"&(?![a-zA-Z]+;|#\d+;)(?=[^\W_])")
        if bool(mnemonic.search(src)) != bool(mnemonic.search(text)):
            out.append("mnemonic & differs")
        if src.endswith("...") != text.endswith("...") and src.endswith("…") == text.endswith("…"):
            out.append("trailing ... differs")
    return out


def load(paths):
    entries, bad = [], 0
    for path in paths:
        for entry in json.load(open(path, encoding="utf-8")):
            p = problems(entry)
            if p:
                bad += 1
                print(f"{path}: [{entry['context']}] {entry['source']!r}: {'; '.join(p)}")
            entries.append(entry)
    return entries, bad


def key(context, source, comment):
    return (context, source, comment or "")


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("check", "merge", "export"):
        print(__doc__ if __doc__ else "usage: check|merge|export ...")
        return 2
    if sys.argv[1] == "export":
        ts_path, prefix, n = sys.argv[2], sys.argv[3], int(sys.argv[4])
        todo = []
        for ctx in ET.parse(ts_path).getroot().iter("context"):
            name = ctx.find("name").text
            for msg in ctx.iter("message"):
                tr = msg.find("translation")
                if tr.get("type") != "unfinished":
                    continue
                e = {"context": name, "source": msg.find("source").text or ""}
                comment = msg.find("comment")
                if comment is not None and comment.text:
                    e["comment"] = comment.text
                if msg.get("numerus") == "yes":
                    e["numerus"] = True
                todo.append(e)
        size = -(-len(todo) // n)
        for i in range(n):
            part = todo[i * size:(i + 1) * size]
            with open(f"{prefix}-{i + 1}.json", "w", encoding="utf-8") as f:
                json.dump(part, f, ensure_ascii=False, indent=1)
            print(f"{prefix}-{i + 1}.json: {len(part)} entries")
        return 0
    if sys.argv[1] == "check":
        entries, bad = load(sys.argv[2:])
        print(f"{len(entries)} entries, {bad} with problems")
        return 1 if bad else 0
    ts_path, chunks = sys.argv[2], sys.argv[3:]
    entries, bad = load(chunks)
    if bad:
        print(f"{bad} entries with problems: nothing merged")
        return 1
    wanted = {key(e["context"], e["source"], e.get("comment")): e for e in entries}
    tree = ET.parse(ts_path)
    filled = 0
    for ctx in tree.getroot().iter("context"):
        name = ctx.find("name").text
        for msg in ctx.iter("message"):
            comment = msg.find("comment")
            e = wanted.pop(key(name, msg.find("source").text or "", comment.text if comment is not None else ""), None)
            if e is None:
                continue
            tr = msg.find("translation")
            for child in list(tr):
                tr.remove(child)
            if e.get("numerus"):
                tr.text = None
                for form in e["forms"]:
                    ET.SubElement(tr, "numerusform").text = form
            else:
                tr.text = e["translation"]
            tr.attrib.pop("type", None)
            filled += 1
    tree.write(ts_path, encoding="utf-8", xml_declaration=True)
    print(f"filled {filled}; not found in the .ts: {len(wanted)}")
    for k in list(wanted)[:20]:
        print("  missing:", k)
    return 0


if __name__ == "__main__":
    sys.exit(main())
