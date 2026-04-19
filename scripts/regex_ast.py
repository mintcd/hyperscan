import json
import copy
import os
import subprocess
import shutil
import sys
from utils.string import unescape_token
from pathlib import Path
from RegexASTVisitor import *


BASE_DIR = Path(__file__).parent.parent.resolve()
OUT_DIR = BASE_DIR / 'output'
ROSE_DUMP_EXE = BASE_DIR / 'build_msvc' / 'bin' / 'dump_rose.exe'
IN_PATH = BASE_DIR / 'dataset' / 'regexes.txt'

# Avoid expanding extremely large repeats into deep trees that exceed
# Python's recursion limit when serializing to JSON.
MAX_EXPAND = 5000
if sys.getrecursionlimit() < 5000:
    sys.setrecursionlimit(5000)


def _make_literal_node(ch):
    return {"value": ch}


class StandardizerVisitor(RegexASTVisitor):
    """Recursive visitor that standardizes an AST (class-based) into the
    normalized dict form used elsewhere in the codebase.
    """

    def visitStdNode(self, node, data=None):
        new_children = [self.visit(ch) for ch in node.iter_children()]
        return {'value': node.value, 'children': new_children}

    def visitBoundary(self, node, data=None):
        if node.child is None:
            return {'value': ''}
        return self.visit(node.child)

    def visitSequence(self, node, data=None):
        children = node.children or []
        if not children:
            return {'value': '.', 'children': []}
        left = self.visit(children[0])
        for c in children[1:]:
            right = self.visit(c)
            left = {'value': '.', 'children': [left, right]}
        return left

    def visitAlternation(self, node, data=None):
        children = node.children or []
        if not children:
            return {'value': '+', 'children': []}
        left = self.visit(children[0])
        for c in children[1:]:
            right = self.visit(c)
            left = {'value': '+', 'children': [left, right]}
        return left

    def visitLiteral(self, node, data=None):
        v = node.value
        try:
            val = unescape_token(v) if isinstance(v, str) else v
        except Exception:
            val = v
        return _make_literal_node(val)

    def visitRange(self, node, data=None):
        start = node.start
        end = node.end
        try:
            if start <= end:
                codes = list(range(start, end + 1))
            else:
                codes = list(range(start, end - 1, -1))
        except Exception:
            return {'value': ''}
        if not codes:
            return {'value': ''}
        left = _make_literal_node(chr(codes[0]))
        for code in codes[1:]:
            right = _make_literal_node(chr(code))
            left = {'value': '+', 'children': [left, right]}
        return left

    def visitRepeat(self, node, data=None):
        child = node.child
        child_norm = self.visit(child) if child is not None else {'value': ''}

        minv = node.min if node.min is not None else 0
        maxv = node.max
        # Some dump formats represent unbounded repeats as null/None
        # (JSON `null`) instead of the string "inf". Treat None as
        # an infinite upper bound for robustness.
        max_is_inf = (maxv is None) or (isinstance(maxv, str) and getattr(maxv, 'lower', lambda: '')() == 'inf')
        try:
            min_int = int(minv)
        except Exception:
            min_int = None
        try:
            max_int = int(maxv) if (maxv is not None and not max_is_inf) else None
        except Exception:
            max_int = None

        # Rule 1: min == 0 and max == inf -> star
        if (min_int == 0 or minv == 0) and max_is_inf:
            return {'value': '*', 'children': [child_norm]}

        # Rule 2: min == n and max == inf -> n sequences then star
        if max_is_inf and (min_int is not None and min_int > 0):
            if min_int > MAX_EXPAND:
                return {'type': 'Repeat', 'min': min_int, 'max': 'inf', 'child': child_norm}
            left = None
            for i in range(min_int):
                copy_child = copy.deepcopy(child_norm)
                if left is None:
                    left = copy_child
                else:
                    left = {'value': '.', 'children': [left, copy_child]}
            star_node = {'value': '*', 'children': [copy.deepcopy(child_norm)]}
            if left is None:
                return star_node
            return {'value': '.', 'children': [left, star_node]}

        # Rule 3: min == n and max == m (both finite) -> Alternation of sequences
        if min_int is not None and max_int is not None:
            if (min_int > MAX_EXPAND) or (max_int > MAX_EXPAND) or (max_int - min_int > MAX_EXPAND):
                return {'type': 'Repeat', 'min': min_int, 'max': max_int, 'child': child_norm}
            # exact match
            if min_int == max_int:
                if min_int == 0:
                    return {'value': ''}
                left = None
                for i in range(min_int):
                    copy_child = copy.deepcopy(child_norm)
                    if left is None:
                        left = copy_child
                    else:
                        left = {'value': '.', 'children': [left, copy_child]}
                return left

            alt = None
            for k in range(min_int, max_int + 1):
                if k == 0:
                    seqnode = {'value': ''}
                else:
                    seqnode = None
                    for i in range(k):
                        copy_child = copy.deepcopy(child_norm)
                        if seqnode is None:
                            seqnode = copy_child
                        else:
                            seqnode = {'value': '.', 'children': [seqnode, copy_child]}
                if alt is None:
                    alt = seqnode
                else:
                    alt = {'value': '+', 'children': [alt, seqnode]}
            return alt

        raise ValueError(f"Unsupported repeat bounds: min={minv}, max={maxv}")

    def generic_visit(self, node, data=None):
        new_children = [self.visit(ch, data) for ch in node.iter_children()]
        return {'value': getattr(node, 'value', ''), 'children': new_children}


class NodeCounter(RegexASTVisitor):
    """Recursive node counter operating over the class-based AST.

    Returns `(total, counts)` where `counts` maps node type strings to ints.
    """

    def _merge_counts(self, into, other):
        for k, v in other.items():
            into[k] = into.get(k, 0) + v

    def visitLiteral(self, node, data=None):
        return (1, {'Literal': 1})

    def visitBoundary(self, node, data=None):
        return (0, {})

    def visitSequence(self, node: Sequence, data=None):
        total = 0
        counts = {}

        assert node.children is not None, "Sequence node has no children list"
        # assert len(node.children) > 0, "Sequence node has empty children list"

        for ch in node.children:
            c_total, c_counts = self.visit(ch, data)
            total += c_total
            self._merge_counts(counts, c_counts)

        seq_nodes = max(0, len(node.children) - 1)
        counts['Sequence'] = counts.get('Sequence', 0) + seq_nodes
        total += seq_nodes
            
        return (total, counts)

    def visitAlternation(self, node, data=None):
        total = 0
        counts = {}

        assert node.children is not None, "Alternation node has no children list"
        # assert len(node.children) > 0, "Alternation node has empty children list"

        for ch in node.children:
            c_total, c_counts = self.visit(ch, data)
            total += c_total
            self._merge_counts(counts, c_counts)

        alt_nodes = max(0, len(node.children) - 1)
        counts['Alternation'] = counts.get('Alternation', 0) + alt_nodes
        total += alt_nodes
                        
        return (total, counts)

    def visitRange(self, node, data=None):
        try:
            start = node.start
            end = node.end
        except Exception:
            return (0, {})
        if start <= end:
            codes = list(range(start, end + 1))
        else:
            codes = list(range(start, end - 1, -1))
        if not codes:
            return (0, {})
        k = len(codes)
        counts = {'Literal': k}
        total = k
        if k > 1:
            counts['Alternation'] = k - 1
            total += (k - 1)
        return (total, counts)

    def visitRepeat(self, node, data=None):
        raw_child = node.child
        if raw_child is not None:
            c_total, c_counts = self.visit(raw_child, data)
        else:
            c_total, c_counts = (0, {})

        minv = node.min if node.min is not None else 0
        maxv = node.max
        # Allow JSON null / Python None to indicate an unbounded repeat
        max_is_inf = (maxv is None) or (isinstance(maxv, str) and getattr(maxv, 'lower', lambda: '')() == 'inf')
        try:
            min_int = int(minv)
        except Exception:
            min_int = None
        try:
            max_int = int(maxv) if (maxv is not None and not max_is_inf) else None
        except Exception:
            max_int = None

        # min == 0 and max == inf -> Kleene star
        if (min_int == 0 or minv == 0) and max_is_inf:
            counts = {}
            if raw_child is not None:
                self._merge_counts(counts, c_counts)
                total = c_total
            else:
                total = 0
            counts['Repeat'] = counts.get('Repeat', 0) + 1
            total += 1
            return (total, counts)

        # max == inf and min > 0 -> Sequence of min, then a standard Repeat (star)
        if max_is_inf and (min_int is not None and min_int > 0):
            if min_int > MAX_EXPAND:
                counts = {'Repeat': 1}
                total = 1
                if raw_child is not None:
                    total += c_total
                    self._merge_counts(counts, c_counts)
                return (total, counts)

            counts = {}
            total = 0
            # left: min copies of child
            if raw_child is not None:
                for k, v in c_counts.items():
                    counts[k] = counts.get(k, 0) + v * min_int
                total += c_total * min_int
            # sequence nodes inside left
            if min_int > 1:
                left_seq = min_int - 1
                counts['Sequence'] = counts.get('Sequence', 0) + left_seq
                total += left_seq
            # star node (one Repeat) and its child copy
            counts['Repeat'] = counts.get('Repeat', 0) + 1
            total += 1
            if raw_child is not None:
                for k, v in c_counts.items():
                    counts[k] = counts.get(k, 0) + v
                total += c_total
            # top-level sequence joining left and star
            counts['Sequence'] = counts.get('Sequence', 0) + 1
            total += 1
            return (total, counts)

        # finite min and max -> Alternation of Sequences
        if min_int is not None and max_int is not None:
            if (min_int > MAX_EXPAND) or (max_int > MAX_EXPAND) or (max_int - min_int > MAX_EXPAND):
                counts = {'Repeat': 1}
                total = 1
                if raw_child is not None:
                    total += c_total
                    self._merge_counts(counts, c_counts)
                return (total, counts)

            counts = {}
            total = 0
            alternatives = max_int - min_int + 1
            for k in range(min_int, max_int + 1):
                if k == 0:
                    # empty alternative contributes no child or sequence nodes
                    continue
                # k copies of the child
                if raw_child is not None:
                    for t, v in c_counts.items():
                        counts[t] = counts.get(t, 0) + v * k
                    total += c_total * k
                # sequence nodes inside this alternative
                seq_nodes = k - 1
                if seq_nodes:
                    counts['Sequence'] = counts.get('Sequence', 0) + seq_nodes
                    total += seq_nodes
            # alternation nodes combine the alternatives
            alt_nodes = max(0, alternatives - 1)
            if alt_nodes:
                counts['Alternation'] = counts.get('Alternation', 0) + alt_nodes
                total += alt_nodes
            return (total, counts)

        raise ValueError(f"Unsupported repeat bounds: min={minv}, max={maxv}")

    def visitStdNode(self, node, data=None):
        # Aggregate children counts, do not count the StdNode itself
        total = 0
        counts = {}
        for ch in node.iter_children():
            res = self.visit(ch, data)
            if not res:
                continue
            c_total, c_counts = res
            total += c_total
            self._merge_counts(counts, c_counts)
        return (total, counts)

    def generic_visit(self, node, data=None):
        # Default: count this node and aggregate children
        total = 1
        counts = {node.__class__.__name__: 1}
        for ch in node.iter_children():
            c_total, c_counts = self.visit(ch, data)
            total += c_total
            self._merge_counts(counts, c_counts)
        return (total, counts)


def countOriginalNodes(root):
    """Return a list of dicts `{type: str, count: int}` for original nodes.

    Only dict nodes are considered nodes; non-dict children are ignored.
    The result is deterministic: sorted by node type name.
    """
    if root is None:
        return []
    if not isinstance(root, dict):
        return []
    res = NodeCounter(root).visit_root()
    if not res:
        return []
    _, counts = res
    return [{'type': t, 'count': cnt} for t, cnt in sorted(counts.items())]

def countNodes(obj):
    """Count nodes for a dump_rose-style object or a raw tree.

    Accepts:
      - a dict containing keys like `{'regex': ..., 'tree': {...}}`
      - a raw tree dict
      - a path to a JSON file containing such an object
      - a list of any of the above (returns a list of results)

    Returns a dict with keys:
      - `regex`: the regex string if present (may be None)
      - `total`: total node count (int)
      - `counts`: list of dicts `{'type': str, 'count': int}` sorted by type
    """
    # file path
    if isinstance(obj, str):
        try:
            with open(obj, 'r', encoding='utf-8') as f:
                data = json.load(f)
        except Exception:
            raise ValueError(f"Could not read JSON from path: {obj}")
        return countNodes(data)

    # list -> map
    if isinstance(obj, list):
        return [countNodes(o) for o in obj]

    # dict with 'tree' key (dump_rose-style)
    if isinstance(obj, dict) and 'tree' in obj:
        root = obj.get('tree')
        regex = obj.get('regex')
        if not isinstance(root, dict):
            return {'regex': regex, 'total': 0, 'counts': []}
        res = NodeCounter(root).visit_root()
        if not res:
            return {'regex': regex, 'total': 0, 'counts': []}
        total, counts = res
        counts_list = [{'type': t, 'count': cnt} for t, cnt in sorted(counts.items())]
        return {'regex': regex, 'total': total, 'counts': counts_list}

    # raw tree dict
    if isinstance(obj, dict):
        res = NodeCounter(obj).visit_root()
        if not res:
            return {'regex': None, 'total': 0, 'counts': []}
        total, counts = res
        counts_list = [{'type': t, 'count': cnt} for t, cnt in sorted(counts.items())]
        return {'regex': None, 'total': total, 'counts': counts_list}

    # otherwise unsupported
    raise TypeError('countNodes expects a path, dict, or list')

def countNodesByRegex(regex):
    dumpOutput = subprocess.run([str(ROSE_DUMP_EXE), regex, f"{OUT_DIR}/test.json"], capture_output=True, text=True)
    # load JSON from the file produced by dump_rose
    with open(f"{OUT_DIR}/test.json", 'r', encoding='utf-8') as f:
        data = json.load(f)
    # Ensure the returned object includes the regex string. If dump_rose
    # omits the `regex` field (or writes a raw tree), attach or wrap
    # the data so `countNodes` will return the regex instead of `None`.
    if isinstance(data, dict):
        if 'regex' not in data:
            if 'tree' in data:
                data['regex'] = regex
            else:
                data = {'regex': regex, 'tree': data}
    counts = countNodes(data)
    print(json.dumps(counts, ensure_ascii=False, indent=2))
    return counts


def standardizeRegexTree(root):
    """Wrapper preserving the original entrypoint signature."""
    if root is None:
        return None
    return StandardizerVisitor(root).visit_root()

def getTree(dump_rose_path, regex, output_path):
    """Run `dump_rose` for `regex` and return the parsed tree."""
    proc = subprocess.run([dump_rose_path, regex], capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"dump_rose failed (rc={proc.returncode}): {proc.stderr}\n{proc.stdout}")
    try:
        data = json.loads(proc.stdout)
        return data.get('tree')
    except Exception as e:
        raise ValueError(f"Failed to parse dump_rose output: {e}\nOutput was:\n{proc.stdout}")

def testStandardizeRegexTree(regex=None, dump_rose_path=None, output_path="output/test.json"):
    """Run `dump_rose` for `regex`, standardize its tree, and print a JSON
    object with fields `regex`, `init_tree`, and `last_tree`.

    If `dump_rose_path` is omitted the function will try common build
    locations and then fall back to the PATH.
    """
    if regex is None:
        try:
            regex = input("regex: ").strip()
        except Exception:
            raise ValueError("No regex provided")

    # candidate locations for dump_rose
    candidates = [
        os.path.join('build_msvc', 'bin', 'dump_rose.exe'),
        os.path.join('build', 'bin', 'dump_rose.exe'),
        os.path.join('build', 'bin', 'dump_rose'),
        os.path.join('build', 'dump_rose.exe'),
        os.path.join('build', 'Debug', 'dump_rose.exe'),
        os.path.join('build', 'Release', 'dump_rose.exe'),
        os.path.join('build_msvc', 'bin', 'dump_rose'),
    ]
    if dump_rose_path:
        candidates.insert(0, dump_rose_path)

    dump_path = None
    for c in candidates:
        if os.path.exists(c) and os.access(c, os.X_OK):
            dump_path = c
            break
    if dump_path is None:
        which = shutil.which('dump_rose')
        if which:
            dump_path = which
    if dump_path is None:
        raise FileNotFoundError("Could not find dump_rose binary; please build it or pass dump_rose_path")

    # ensure output directory exists
    out_dir = os.path.dirname(output_path) or '.'
    os.makedirs(out_dir, exist_ok=True)

    # invoke dump_rose
    proc = subprocess.run([dump_path, regex, output_path], capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"dump_rose failed (rc={proc.returncode}): {proc.stderr}\n{proc.stdout}")

    # load and standardize
    with open(output_path, 'r', encoding='utf-8') as f:
        data = json.load(f)
    init_tree = data.get('tree')
    last_tree = standardizeRegexTree(init_tree)

    out = {"regex": regex, "init_tree": init_tree, "last_tree": last_tree}
    print(json.dumps(out, ensure_ascii=False, indent=2))
    return out

def standardizeAll():
    """Standardize all regex trees in the `output/info` directory and write
    results to `output/standardized`.
    """
    info_dir = os.path.join('output', 'info')
    out_dir = os.path.join('output', 'standardized')
    os.makedirs(out_dir, exist_ok=True)
    if not os.path.isdir(info_dir):
        print(f"info directory not found: {info_dir}")
        return
    for name in sorted(os.listdir(info_dir)):
        if not name.endswith('.json'):
            continue
        src = os.path.join(info_dir, name)
        dst = os.path.join(out_dir, name)
        try:
            with open(src, 'r', encoding='utf-8') as f:
                data = json.load(f)
            tree = data.get('tree')
            standardized_tree = standardizeRegexTree(tree)
            outobj = {'regex': data.get('regex'), 'tree': standardized_tree}
            with open(dst, 'w', encoding='utf-8') as f:
                json.dump(outobj, f, ensure_ascii=False, indent=2)
        except Exception as e:
            print(f"failed processing {src}: {e}")
            continue

if __name__ == "__main__":
    countNodesByRegex("a(b|c)*d{2,4}")