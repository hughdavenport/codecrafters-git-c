#!/bin/sh
#
# Use this script to run your program LOCALLY.
#
# Note: Changing this script WILL NOT affect how CodeCrafters runs your program.
#
# Learn more: https://codecrafters.io/program-interface

set -e # Exit early if any commands fail

# Copied from .codecrafters/compile.sh
#
# - Edit this to change how your program compiles locally
# - Edit .codecrafters/compile.sh to change how your program compiles remotely
(
  cd "$(dirname "$0")" # Ensure compile steps are run within the repository directory
  # { [ -d build ] || mkdir build; } && gcc -Wall -Werror -Wpedantic -Wextra -fsanitize=address -ggdb -g -o build/git src/main.c
  # { [ -d build ] || mkdir build; } && clang -Wall -Werror -Wpedantic -Wextra -Wno-gnu-zero-variadic-macro-arguments -fsanitize=address -ggdb -g -o build/git src/main.c
  { [ -d build ] || mkdir build; } && tcc -Wall -Werror -Wpedantic -Wextra -fsanitize=address -ggdb -g -o build/git src/main.c
  # cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
  # cmake --build ./build
)

# Copied from .codecrafters/run.sh
#
# - Edit this to change how your program runs locally
# - Edit .codecrafters/run.sh to change how your program runs remotely
exec $(dirname $0)/build/git "$@"
