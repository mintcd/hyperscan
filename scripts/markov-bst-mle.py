import random
from collections import defaultdict

class MarkovRegexMLE:
    def __init__(self, alpha=1.0):
        self.alpha = alpha
        
        # Raw counts
        self.trans_counts = {
            'unary': defaultdict(lambda: defaultdict(int)),
            'binary_left': defaultdict(lambda: defaultdict(int)),
            'binary_right': defaultdict(lambda: defaultdict(int))
        }
        self.leaf_counts = defaultdict(int)
        self.root_counts = {'u': 0, 'b': 0}
        
        # Computed probabilities matrices
        self.P_trans = {'unary': {}, 'binary_left': {}, 'binary_right': {}}
        self.P_leaf = {}
        self.P_root = {}

    def fit(self, X):
        """
        X: Iterable of Abstract Syntax Tree dictionaries.
        """
        # Extract counts
        for ast in X:
            if ast:
                self._traverse(ast, parent_val=None, branch_type=None)
                
        # Discover vocabularies for smoothing denominators
        self.ops = set()
        for branch in self.trans_counts.values():
            for parent, children in branch.items():
                self.ops.add(parent)
                self.ops.update(children.keys())
        self.k_ops = len(self.ops)
        self.k_leaves = len(self.leaf_counts)

        # Compute standard MLE with Laplace smoothing
        self._compute_probabilities()
        return self

    def _traverse(self, node, parent_val, branch_type):
        val = node.get("value")
        children = node.get("children", [])
        n = self._get_size(node)

        if n == 1:
            self.leaf_counts[val] += 1
            return

        if parent_val is None and n > 2:
            if len(children) == 1: self.root_counts['u'] += 1
            if len(children) == 2: self.root_counts['b'] += 1

        if parent_val is not None and n > 2:
            self.trans_counts[branch_type][parent_val][val] += 1

        if len(children) == 1:
            self._traverse(children[0], val, 'unary')
        elif len(children) == 2:
            self._traverse(children[0], val, 'binary_left')
            self._traverse(children[1], val, 'binary_right')

    def _get_size(self, node):
        if not node: return 0
        return 1 + sum(self._get_size(c) for c in node.get("children", []))

    def _compute_probabilities(self):
        # 1. Root Probabilities (0-order fallback for the very first node)
        total_root = self.root_counts['u'] + self.root_counts['b']
        self.P_root['u'] = (self.root_counts['u'] + self.alpha) / (total_root + 2 * self.alpha)
        self.P_root['b'] = (self.root_counts['b'] + self.alpha) / (total_root + 2 * self.alpha)

        # 2. Transition Probabilities (1st-order)
        for branch_type, matrix in self.trans_counts.items():
            for parent in self.ops:
                total_transitions = sum(matrix[parent].values())
                self.P_trans[branch_type][parent] = {}
                for child in self.ops:
                    count = matrix[parent].get(child, 0)
                    self.P_trans[branch_type][parent][child] = (count + self.alpha) / (total_transitions + self.k_ops * self.alpha)

        # 3. Leaf Probabilities
        total_leaves = sum(self.leaf_counts.values())
        for leaf in self.leaf_counts.keys(): # Assuming closed vocabulary for leaves here
            self.P_leaf[leaf] = (self.leaf_counts[leaf] + self.alpha) / (total_leaves + self.k_leaves * self.alpha)

    def generate(self, n, parent_val=None, branch_type=None):
        """
        Generates an AST of exactly size n using learned Markov states.
        """
        if n == 1:
            leaf = self._sample_dict(self.P_leaf)
            return {"value": leaf}
        
        if n == 2:
            # Must be a unary operator. In a full implementation, you'd filter P_trans for unary ops.
            # Simplified here to just pick a known unary operator, e.g., '*'
            return {"value": "*", "children": [self.generate(1)]}

        # Size > 2: Determine if we generate Unary or Binary
        if parent_val is None:
            # Use root probabilities if we are at the top of the tree
            is_unary = random.random() < self.P_root['u']
        else:
            # Look up transition probabilities given the parent
            trans_dist = self.P_trans[branch_type][parent_val]
            op = self._sample_dict(trans_dist)
            # Logic required here to check if sampled `op` is unary or binary based on your syntax rules
            is_unary = op in ['*', '+', '?'] # Example check

        if is_unary:
            # Need to sample the specific unary operator (if multiple exist) and recurse
            return {"value": op if parent_val else "*", "children": [self.generate(n - 1, op, 'unary')]}
        else:
            # Uniform split for binary operator
            x = random.randint(1, n - 2)
            op = op if parent_val else "|" # Fallback if root
            left_child = self.generate(x, op, 'binary_left')
            right_child = self.generate(n - x - 1, op, 'binary_right')
            return {"value": op, "children": [left_child, right_child]}

    def _sample_dict(self, dist):
        r = random.random()
        cumulative = 0.0
        for k, v in dist.items():
            cumulative += v
            if r < cumulative:
                return k
        return list(dist.keys())[-1]