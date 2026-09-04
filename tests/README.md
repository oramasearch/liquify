# Tests

`test_repository_view.cpp` creates its readable repository through the pinned `liquid::repository`
API, writes root entries, and proves that `RepositoryView` reads the same on-disk format it links.
It also creates a corrupt `LIQUID_REPO` marker and checks that the liquid error reaches the view.

`test_print_tree_error.cmake` runs the built `liquify --print-tree` command against a corrupt
repository created in the build tree. It requires a non-zero exit and the same liquid error on
standard error. No repository fixture bytes are committed.

`test_version.cpp` checks the public liquify version value and string.

`test_editor_buffer.cpp` covers line and selection edits, UTF-8 cursor movement, and a save that
reopens the result through `liquid::repository`. `test_text_layout.cpp` proves cached layouts are
reused, an edit invalidates only its replaced lines, inserted lines preserve following cached
layouts, and tabs plus UTF-8 retain byte-to-column positioning.

`test_stub_tree.cpp` parses the checked-in C and C++ declaration fixtures through the public stub
API, checks the exact declaration lists and modifiers, verifies graph persistence and equal-blob
reuse, resolves a declaration across separate stub trees, and proves an edited blob recomputes
without recomputing an unchanged blob.

`test_interactive_analysis.cpp` keeps an invalid C++ fixture open as a volatile overlay and checks
all seven registered analysis queries. It covers closed-file stub hover and definition lookup,
compiler-symbol precedence, translation-unit diagnostics, completion order, local references,
tree-sitter changed-range reuse, cancellation during typing and a successful answer after the edit.
