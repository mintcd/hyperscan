import regex_ast as ra

# Construct a standardized-style tree where children contains a None entry
# This simulates JSON input like {"value": ".", "children": [null, {"value": "a"}]}
obj = {'tree': {'value': '.', 'children': [None, {'value': 'a'}]}}

print('Input object:')
print(obj)
print('\nResult:')
print(ra.countNodes(obj))
