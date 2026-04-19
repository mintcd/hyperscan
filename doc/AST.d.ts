type AST = {
  type: "Sequence",
  children: AST[]
} | {
  type: "Alternation",
  children: AST[]
} | {
  type: "Literal",
  value: string
} | {
  type: "Range",
  value: [string, string]
} | {
  type: "Repeat",
  min: number,
  max: number,
  child: AST
} | {
  type: "Boundary"
}