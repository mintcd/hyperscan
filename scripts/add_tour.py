import json

graph_path = r"D:\Projects\hyperscan\.understand-anything\knowledge-graph.json"

with open(graph_path, 'r', encoding='utf-8') as f:
    data = json.load(f)

files = [n for n in data.get('nodes', []) if n.get('type') == 'file']
important_names = ['hs.h', 'hs_compile.h', 'hs_runtime.h', 'ch.h']
tour_nodes = []
for name in important_names:
    for f in files:
        if f.get('name') == name:
            tour_nodes.append(f)
            break

if not tour_nodes:
    tour_nodes = files[:5]

tour = []
for i, node in enumerate(tour_nodes, 1):
    tour.append({
        "nodeId": node["id"],
        "title": f"Step {i}: {node.get('name')}",
        "description": f"Explore {node.get('name')}. {node.get('summary', '')}"
    })

data['tour'] = tour

with open(graph_path, 'w', encoding='utf-8') as f:
    json.dump(data, f, indent=2)

print(f"Added tour with {len(tour)} steps.")
