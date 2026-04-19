#!/usr/bin/env python3
import json
import runpy
from pathlib import Path
import argparse
from RegexMLE import RegexMLE

def iter_trees(info_dir, limit=None):
    p = Path(info_dir)
    files = sorted(p.glob("*.json"))
    print(f"Using {len(files)} samples")
    if limit:
        files = files[:limit]
    for f in files:
        try:
            obj = json.loads(f.read_text(encoding="utf-8"))
            yield obj.get("tree", obj)
        except Exception as e:
            print(f"skipping {f}: {e}")

def main():
    parser = argparse.ArgumentParser(description="Train BST MLE from output/info JSON trees")
    parser.add_argument("-i", "--info-dir", default="output/info", help="directory with JSON tree files")
    parser.add_argument("-n", "--limit", type=int, default=None, help="limit number of files to load")
    parser.add_argument("-a", "--alpha", type=float, default=1.0, help="Laplace smoothing alpha")
    parser.add_argument("-o", "--out", default="output/bst_probs.json", help="output JSON for learned probs")
    args = parser.parse_args()

    model = RegexMLE(alpha=args.alpha)
    trees = iter_trees(args.info_dir, args.limit)
    model.fit(trees)

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(model.probs, indent=2), encoding="utf-8")
    print("Saved probabilities to", out)

if __name__ == "__main__":
    main()
