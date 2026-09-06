# Windows packaging inputs

`AppxManifest.xml` defines the desktop MSIX identity, executable, tiles, and full-trust capability.
`package.ps1` stages the release executable, recursively copies non-system DLLs reported by
`llvm-objdump`, generates the required tile images, and records the unsigned digest. It signs with
SHA-256 and an RFC 3161 timestamp, verifies the signature, installs the package, and runs the
installed `liquify version` with a system-only `PATH` so runner tool directories cannot hide a
missing packaged DLL.

Pull-request CI creates and trusts a temporary self-signed certificate. Tag releases use the PFX,
password, publisher subject, and optional timestamp URL supplied through repository secrets. The
temporary certificate and package installation are removed after verification.
