# Implementation

`app.cpp` owns the native rendering and input loop. Repository and editor workers use the pinned
public libliquid API. `repository_error.cpp` adds artifact and upgrade guidance when that API
rejects an unsupported store format, preserving unrelated errors. `version.cpp` reports the
configured IDE and library revisions. See
[store compatibility](../docs/build/store-compatibility.md) for the format and proof contract.
