# Interactive-analysis fixtures

`interactive.cpp` deliberately ends with an incomplete return expression. It exercises cursor
queries, local references, completion and diagnostics while tree-sitter retains a recoverable
tree. `provider.hpp` remains closed and contributes only its stub record, including the comment
used by hover.
