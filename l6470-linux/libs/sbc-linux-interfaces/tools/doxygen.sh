#!/usr/bin/env sh

set -e
set -u

cd $(dirname $0)/../
doxygen docs/Doxyfile
