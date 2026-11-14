#!/bin/sh
set -eu

NATIVE=$(cd "$(dirname "$0")" && pwd)
DIST="$NATIVE/dist"
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
ALL="macos-universal windows-x64 windows-arm64 linux-x64 linux-arm64"
TARGETS=${*:-$ALL}

zig_wrappers() {
  mkdir -p "$1/zig"
  for tool in cc c++; do
    printf '#!/bin/sh\nexec zig %s -target %s "$@"\n' "$tool" "$2" > "$1/zig/$tool"
    chmod +x "$1/zig/$tool"
  done
  for tool in ar ranlib; do
    printf '#!/bin/sh\nexec zig %s "$@"\n' "$tool" > "$1/zig/$tool"
    chmod +x "$1/zig/$tool"
  done
}

toolchain() {
  cat > "$1/toolchain.cmake" <<EOF
set(CMAKE_SYSTEM_NAME $2)
set(CMAKE_SYSTEM_PROCESSOR $3)
set(CMAKE_C_COMPILER "$4")
set(CMAKE_CXX_COMPILER "$5")
set(CMAKE_AR "$6" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "$7" CACHE FILEPATH "" FORCE)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
EOF
  if [ "$8" != "-" ]; then echo "set(CMAKE_RC_COMPILER \"$8\")" >> "$1/toolchain.cmake"; fi
}

build() {
  target=$1
  dir="$NATIVE/build-$target"
  exe=TheCrewReborn-Server
  mkdir -p "$dir"
  set -- -S "$NATIVE" -B "$dir" -DCMAKE_BUILD_TYPE=Release -DTCR_BUILD_TESTS=OFF
  case "$target" in
    macos-universal)
      set -- "$@" "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
      strip_cmd="strip -x"
      ;;
    windows-x64)
      toolchain "$dir" Windows AMD64 x86_64-w64-mingw32-gcc x86_64-w64-mingw32-g++ \
        "$(command -v x86_64-w64-mingw32-ar)" "$(command -v x86_64-w64-mingw32-ranlib)" \
        "$(command -v x86_64-w64-mingw32-windres)"
      set -- "$@" -DCMAKE_TOOLCHAIN_FILE="$dir/toolchain.cmake" \
        "-DCMAKE_EXE_LINKER_FLAGS=-static -static-libgcc -static-libstdc++ -s"
      exe=TheCrewReborn-Server.exe
      strip_cmd=":"
      ;;
    windows-arm64)
      zig_wrappers "$dir" aarch64-windows-gnu
      toolchain "$dir" Windows ARM64 "$dir/zig/cc" "$dir/zig/c++" "$dir/zig/ar" "$dir/zig/ranlib" -
      set -- "$@" -DCMAKE_TOOLCHAIN_FILE="$dir/toolchain.cmake" "-DCMAKE_EXE_LINKER_FLAGS=-static -s" \
        -DTCR_WINDOWS_RC_AT_LINK=ON
      exe=TheCrewReborn-Server.exe
      strip_cmd=":"
      ;;
    linux-x64 | linux-arm64)
      arch=x86_64
      [ "$target" = linux-arm64 ] && arch=aarch64
      zig_wrappers "$dir" "$arch-linux-musl"
      toolchain "$dir" Linux "$arch" "$dir/zig/cc" "$dir/zig/c++" "$dir/zig/ar" "$dir/zig/ranlib" -
      set -- "$@" -DCMAKE_TOOLCHAIN_FILE="$dir/toolchain.cmake" "-DCMAKE_EXE_LINKER_FLAGS=-static -s"
      strip_cmd=":"
      ;;
    *)
      echo "unknown target $target (known: $ALL)" >&2
      exit 2
      ;;
  esac
  echo "== $target"
  cmake "$@" > "$dir/configure.log" 2>&1 || { cat "$dir/configure.log"; exit 1; }
  cmake --build "$dir" --target TheCrewReborn-Server -j "$JOBS" > "$dir/build.log" 2>&1 || { tail -50 "$dir/build.log"; exit 1; }
  mkdir -p "$DIST/$target"
  cp "$dir/$exe" "$DIST/$target/$exe"
  $strip_cmd "$DIST/$target/$exe"
  printf '%s  %s bytes\n' "$DIST/$target/$exe" "$(wc -c < "$DIST/$target/$exe" | tr -d ' ')"
}

for t in $TARGETS; do
  build "$t"
done
