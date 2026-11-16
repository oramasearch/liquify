# SPDX-License-Identifier: MIT
# The packaging files carry the release version that project(VERSION) names (liquify#154).
foreach(pair
    "packaging/linux/package.sh|LIQUIFY_PACKAGE_VERSION:-${VERSION}}"
    "packaging/macos/package.sh|LIQUIFY_PACKAGE_VERSION:-${VERSION}}"
    "packaging/macos/Info.plist|<string>${VERSION}</string>"
    "packaging/windows/AppxManifest.xml|Version=\"${VERSION}.0\""
    "packaging/windows/package.ps1|PackageVersion = '${VERSION}.0'")
  string(REPLACE "|" ";" parts "${pair}")
  list(GET parts 0 file)
  list(GET parts 1 expected)
  file(READ "${SOURCE_DIR}/${file}" text)
  string(FIND "${text}" "${expected}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "${file} does not carry version ${VERSION} (expected ${expected})")
  endif()
endforeach()
