import sys
import json
sys.path.insert(0, r'd:\Projects\hyperscan')
from scripts import regex_ast as ra

# Test: Sequence of 3 literals
seq = {'type': 'Sequence', 'children': [
    {'type': 'Literal', 'value': 'a'},
    {'type': 'Literal', 'value': 'b'},
    {'type': 'Literal', 'value': 'c'},
]}

# Test: Alternation of 3 literals
alt = {'type': 'Alternation', 'children': [
    {'type': 'Literal', 'value': 'x'},
    {'type': 'Literal', 'value': 'y'},
    {'type': 'Literal', 'value': 'z'},
]}

# Test: Range a-c (use integer byte values)
rng = {'type': 'Range', 'value': [97, 99]}

# Test: Repeat {1,3} of 'a' (finite)
rep_finite = {'type': 'Repeat', 'min': 1, 'max': 3, 'child': {'type': 'Literal', 'value': 'a'}}

# Test: Repeat {2,inf} -> sequence of 2 then star
rep_inf = {'type': 'Repeat', 'min': 2, 'max': 'inf', 'child': {'type': 'Literal', 'value': 'a'}}

# Test: Kleene star {0,inf}
rep_star = {'type': 'Repeat', 'min': 0, 'max': 'inf', 'child': {'type': 'Literal', 'value': 'a'}}

cases = [
    ('seq3', seq),
    ('alt3', alt),
    ('range_a_c', rng),
    ('rep_1_3', rep_finite),
    ('rep_2_inf', rep_inf),
    ('rep_star', rep_star),
]

for name, tree in cases:
    print('---')
    print('CASE:', name)
    print('TREE:', json.dumps(tree, ensure_ascii=False))
    res = ra.countNodes(tree)
    print('RESULT:')
    print(json.dumps(res, ensure_ascii=False, indent=2))

print('\nDone')
