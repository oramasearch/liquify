# Tests

`test_repository_view.cpp` creates its readable repository through the pinned `liquid::repository`
API, writes root entries, and proves that `RepositoryView` reads the same on-disk format it links.
It also creates a corrupt `LIQUID_REPO` marker and checks that the liquid error reaches the view.

`test_print_tree_error.cmake` runs the built `liquify --print-tree` command against a corrupt
repository created in the build tree. It requires a non-zero exit and the same liquid error on
standard error. No repository fixture bytes are committed.

`test_version.cpp` checks the public liquify version value and string.
