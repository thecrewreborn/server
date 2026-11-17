#!/bin/sh
set -eu

ANDROID=$(cd "$(dirname "$0")" && pwd)
ROOT=$(dirname "$ANDROID")
NATIVE="$ROOT/native"

: "${JAVA_HOME:=/opt/homebrew/opt/openjdk@17}"
: "${ANDROID_HOME:=$HOME/Library/Android/sdk}"
export JAVA_HOME ANDROID_HOME

if [ "${1:-}" != "--no-server" ]; then
  "$NATIVE/build.sh" linux-arm64 linux-x64
fi
for pair in arm64-v8a:linux-arm64 x86_64:linux-x64; do
  abi=${pair%%:*}
  target=${pair##*:}
  mkdir -p "$ANDROID/app/jni/$abi"
  cp "$NATIVE/dist/$target/TheCrewReborn-Server" "$ANDROID/app/jni/$abi/libtcreborn.so"
done

if [ -n "${TCR_ANDROID_KEYSTORE:-}" ]; then
  [ -f "$TCR_ANDROID_KEYSTORE" ] || { echo "TCR_ANDROID_KEYSTORE: no such file: $TCR_ANDROID_KEYSTORE" >&2; exit 1; }
  [ -n "${TCR_ANDROID_KEYSTORE_PASSWORD:-}" ] || { echo "TCR_ANDROID_KEYSTORE_PASSWORD is not set" >&2; exit 1; }
else
  echo "no TCR_ANDROID_KEYSTORE: the APK is signed with the debug key" >&2
fi
printf 'sdk.dir=%s\n' "$ANDROID_HOME" > "$ANDROID/local.properties"
(cd "$ANDROID" && ./gradlew --quiet assembleRelease)

mkdir -p "$NATIVE/dist/android"
cp "$ANDROID/app/build/outputs/apk/release/app-release.apk" "$NATIVE/dist/android/TheCrewReborn-Server.apk"
printf '%s  %s bytes\n' "$NATIVE/dist/android/TheCrewReborn-Server.apk" \
  "$(wc -c < "$NATIVE/dist/android/TheCrewReborn-Server.apk" | tr -d ' ')"
