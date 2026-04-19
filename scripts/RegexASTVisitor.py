from utils.string import decode_byte

# --- AST classes and recursive visitor base ---------------------------------
class ASTNode:
    def accept(self, visitor, data=None):
        method_name = 'visit' + self.__class__.__name__
        visit_fn = getattr(visitor, method_name, None)
        if callable(visit_fn):
            return visit_fn(self, data)
        return visitor.generic_visit(self, data)

    def iter_children(self):
        return []


class Literal(ASTNode):
    def __init__(self, value):
        self.value = value

    def iter_children(self):
        return []


class Sequence(ASTNode):
    def __init__(self, children: list[ASTNode]):
        self.children = children



class Alternation(ASTNode):
    def __init__(self, children: list[ASTNode]):
        self.children = children


class Range(ASTNode):
    def __init__(self, start, end):
        self.start = start
        self.end = end


class Repeat(ASTNode):
    def __init__(self, child=None, min=0, max=None):
        self.child = child
        self.min = min
        self.max = max

    def iter_children(self):
        return [self.child] if self.child is not None else []


class Boundary(ASTNode):
    def __init__(self, child=None):
        self.child = child

    def iter_children(self):
        return [self.child] if self.child is not None else []


class StdNode(ASTNode):
    """Represents an already-standardized dict node with 'value' and 'children'.

    Used to preserve nodes that are already in the normalized output form.
    """

    def __init__(self, value, children=None):
        self.value = value
        self.children = children or []

    def iter_children(self):
        return [c for c in self.children if c is not None]


class Visitor:
    """Simple recursive visitor with dynamic dispatch to `visit<Type>`.

    Subclasses should implement `visitSequence`, `visitAlternation`,
    `visitLiteral`, `visitRange`, `visitRepeat`, `visitBoundary`, and
    `visitStdNode` as needed. `generic_visit` is a fallback.
    """

    def visit(self, node, data=None):
        if node is None:
            return None
        return node.accept(self, data)

    def generic_visit(self, node, data=None):
        # Default traversal: visit children and return a list of results.
        results = []
        for ch in node.iter_children():
            results.append(self.visit(ch, data))
        return results


def dict_to_ast(obj):
    """Convert a dump_rose-style dict tree (or already-standardized dict)
    into the class-based AST defined above.
    """
    if obj is None:
        return None
    if not isinstance(obj, dict):
        return Literal(obj)

    # Already-standardized node (no 'type')
    if 'type' not in obj:
        value = obj.get('value')
        children = [dict_to_ast(ch) for ch in (obj.get('children') or [])]
        return StdNode(value, children)

    t = obj.get('type')
    if t == 'Literal':
        return Literal(obj.get('value'))
    if t == 'Sequence':
        return Sequence([dict_to_ast(ch) for ch in (obj.get('children') or [])])
    if t == 'Alternation':
        return Alternation([dict_to_ast(ch) for ch in (obj.get('children') or [])])
    if t == 'Range':
        val = obj.get('value', [])
        try:
            start = decode_byte(val[0])
            end = decode_byte(val[1])
            return Range(start, end)
        except Exception:
            return Literal("")
    if t == 'Repeat':
        raw_child = obj.get('child')
        if raw_child is None:
            children = obj.get('children') or []
            if children:
                raw_child = children[0]
        child_ast = dict_to_ast(raw_child) if raw_child is not None else None
        return Repeat(child_ast, obj.get('min', 0), obj.get('max', None))
    if t == 'Boundary':
        raw_child = obj.get('child')
        if raw_child is None:
            children = obj.get('children') or []
            if children:
                raw_child = children[0]
        child_ast = dict_to_ast(raw_child) if raw_child is not None else None
        return Boundary(child_ast)

    # Fallback: treat as a standardized node preserving 'value' and 'children'
    value = obj.get('value')
    children = [dict_to_ast(ch) for ch in (obj.get('children') or [])]
    return StdNode(value, children)


class RegexASTVisitor(Visitor):
    """Base visitor that accepts a raw dict tree or an ASTNode and
    converts dict inputs to the class-based AST in its constructor.

    Call `visit_root(data=None)` to run the visitor on the stored tree.
    """

    def __init__(self, root=None):
        # root may be a dict (raw dump_rose tree), an ASTNode, or None
        if root is None:
            self.root = None
        elif isinstance(root, ASTNode):
            self.root = root
        elif isinstance(root, dict):
            self.root = dict_to_ast(root)
        else:
            # Fallback: try converting whatever was passed
            try:
                self.root = dict_to_ast(root)
            except Exception:
                self.root = None

    def visit_root(self, data=None):
        return self.visit(self.root, data)
