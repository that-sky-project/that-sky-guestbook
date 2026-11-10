#!/usr/bin/env bash
# Build that-sky-guestbook.
#
# Requirements:
#   - MinGW-w64 g++ (tested with GCC 12/15)
#   - HTModLoader SDK (HTML SDK), extracted somewhere and pointed to by HTML_SDK
#
# The SDK root must contain:
#   includes/htmodloader/includes/htmod.h
#   includes/imgui-1.92.2b/imgui.h
#   lib/htmodloader.lib
set -e
cd "$(dirname "$0")"

[ -d /c/msys64/ucrt64/bin ] && export PATH="/c/msys64/ucrt64/bin:$PATH"
if command -v cygpath >/dev/null 2>&1; then
  _TMPWIN="$(cygpath -w "$(mktemp -d)")"
  export TMPDIR="$_TMPWIN" TMP="$_TMPWIN" TEMP="$_TMPWIN"
fi

SDK="${HTML_SDK:-./html-sdk}"
if [ ! -f "$SDK/includes/htmodloader/includes/htmod.h" ]; then
  echo "HTModLoader SDK not found at: $SDK" >&2
  echo "Download the HTML SDK from the HTModLoader releases and set HTML_SDK." >&2
  exit 1
fi

INC="-I$SDK/includes/htmodloader -I$SDK/includes/imgui-1.92.2b"
CFLAGS="-O2 -ffunction-sections -fdata-sections -static -s -mavx -msse -Wall \
  -Wno-unused-const-variable -Wno-unused-function -Wno-missing-braces \
  -finput-charset=UTF-8 -fexec-charset=UTF-8"
LFLAGS="-Wl,--gc-sections,-O2,--version-script=src/exports.txt -L$SDK/lib -lhtmodloader"

mkdir -p dist
rm -f dist/*.o dist/that-sky-guestbook.dll

g++ $CFLAGS $INC -c src/mod.cpp -o dist/mod.o
g++ $CFLAGS dist/mod.o -shared -o dist/that-sky-guestbook.dll $LFLAGS
echo "OK -> dist/that-sky-guestbook.dll"
