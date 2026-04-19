import random
import math
from collections import defaultdict

class RegexMLE:
    """
    Standardizes n-ary regex ASTs on the fly and computes 0-order MLE.
    Handles 'Sequence', 'Alternation', 'Repeat', 'Literal', and 'Range'.
    """
    def __init__(self, alpha=1.0):
        self.alpha = alpha
        self.counts = {
            "c_u": 0,
            "c_b": 0,
            "unary_ops": defaultdict(int),
            "binary_ops": defaultdict(int),
            "leaves": defaultdict(int)
        }
        self.probs = {}

    def fit(self, X):
        """
        X: Iterable of unstandardized AST dictionaries.
        """
        for ast in X:
            if not ast:
                continue
            
            # 1. Standardize n-ary tree to strictly binary using Catalan distribution
            tree = ast.get("tree", ast)
            binary_ast = self._standardize_to_binary(tree)
            
            # 2. Compute subtree sizes and extract MLE counts
            self._extract_counts(binary_ast)
            
        self._compute_probabilities()
        return self

    def _catalan(self, n):
        if n <= 0: return 1
        return math.comb(2 * n, n) // (n + 1)

    def _sample_split_index(self, k):
        """
        Samples a split index x consistent with a BST-like generative model.
        k: Number of children in the flat sequence.
        """
        if k <= 2:
            return 1
        # Uniform size distribution, matching the BST generation step
        return random.randint(1, k - 1)

    def _standardize_to_binary(self, node):
        """Recursively converts flat n-ary arrays into binary trees."""
        if not isinstance(node, dict):
            return node

        node_type = node.get("type")

        if node_type in ("Sequence", "Alternation"):
            children = [self._standardize_to_binary(c) for c in node.get("children", [])]
            k = len(children)
            binary_op = "." if node_type == "Sequence" else "|"
            
            if k == 0:
                return {"type": "Leaf", "value": "empty"}
            if k == 1:
                return children[0]
            if k == 2:
                return {"type": "Binary", "value": binary_op, "children": children}
            
            # Stochastic Catalan split for k > 2
            x = self._sample_split_index(k)
            left_branch = self._standardize_to_binary({"type": node_type, "children": node["children"][:x]})
            right_branch = self._standardize_to_binary({"type": node_type, "children": node["children"][x:]})
            
            return {"type": "Binary", "value": binary_op, "children": [left_branch, right_branch]}

        elif node_type == "Repeat":
            # Treat Repeat as a Unary operator. Prevents `{1024}` recursion limits.
            child = self._standardize_to_binary(node.get("child"))
            quantifier = f"{{{node.get('min')},{node.get('max')}}}"
            return {"type": "Unary", "value": quantifier, "children": [child]}
            
        elif node_type in ("Literal", "Range"):
            val = node.get("value")
            # Flatten range array into a string representation for counting
            if isinstance(val, list): val = f"[{val[0]}-{val[1]}]"
            return {"type": "Leaf", "value": val}

        return node

    def _get_size(self, node):
        if not node or "type" not in node: return 0
        if node["type"] == "Leaf": return 1
        if node["type"] == "Unary": return 1 + self._get_size(node["children"][0])
        if node["type"] == "Binary": return 1 + self._get_size(node["children"][0]) + self._get_size(node["children"][1])
        return 0

    def _extract_counts(self, node):
        if not node or "type" not in node:
            return
            
        n = self._get_size(node)
        node_type = node.get("type")
        val = node.get("value")
        
        # 1. Structural and Operator generative choices (strictly for n > 2)
        if n > 2:
            if node_type == "Unary":
                self.counts["c_u"] += 1
                self.counts["unary_ops"][val] += 1
            elif node_type == "Binary":
                self.counts["c_b"] += 1
                self.counts["binary_ops"][val] += 1
                
        # 2. Leaf generative choices (n = 1)
        if n == 1 and node_type == "Leaf":
            self.counts["leaves"][val] += 1
            
        # Traverse children
        for child in node.get("children", []):
            self._extract_counts(child)

    def _compute_probabilities(self):
        """Calculates Laplace smoothed MLE parameters."""
        total_struct = self.counts["c_u"] + self.counts["c_b"]
        self.probs["p_u"] = (self.counts["c_u"] + self.alpha) / (total_struct + 2 * self.alpha)
        self.probs["p_b"] = (self.counts["c_b"] + self.alpha) / (total_struct + 2 * self.alpha)
        
        for category in ["unary_ops", "binary_ops", "leaves"]:
            self.probs[category] = {}
            k_vocab = len(self.counts[category])
            total_ops = sum(self.counts[category].values())
            for op, count in self.counts[category].items():
                self.probs[category][op] = (count + self.alpha) / (total_ops + k_vocab * self.alpha)

# Example execution is intentionally omitted when importing this module.
if __name__ == "__main__":
    # Example usage:
    # - Import `RegexMLE` from this file and call `fit` with an iterable of AST dicts.
    # - Prefer using `scripts/train_bst.py` to run MLE on `output/info` JSON files.
    print("RegexMLE module. Run scripts/train_bst.py to train on output/info JSON files.")