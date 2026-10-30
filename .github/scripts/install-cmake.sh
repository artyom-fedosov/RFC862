#!/bin/sh
# Installs the CMake release whose `import std;` gate value CMakeLists.txt sets, and puts
# it first on PATH for the following workflow steps.
set -eu
version=4.3.0
curl -fsSL "https://github.com/Kitware/CMake/releases/download/v$version/cmake-$version-linux-$(uname -m).tar.gz" |
    tar -xz -C /opt
echo "/opt/cmake-$version-linux-$(uname -m)/bin" >> "$GITHUB_PATH"
