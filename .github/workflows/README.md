# GitHub Actions workflows

`ci.yml` is both the pull-request workflow and the reusable three-platform installer build. It
builds and tests the release executable plus its tree-sitter frontend, packages and verifies the native installers, uploads them
as workflow artifacts, and verifies the AppImage again in Fedora.

`release.yml` calls that same workflow for `v*` tags, downloads only the verified artifacts,
creates the aggregate `SHA256SUMS`, and publishes all installers and digest records to the GitHub
release. The caller grants write access only to the publication job.
