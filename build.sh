#!/usr/bin/env bash
set -e

export WORKDIR="$(cd "$(dirname "$0")" && pwd)"
export PS4_PAYLOAD_SDK="${PS4_PAYLOAD_SDK:-$WORKDIR/ps4-payload-sdk}"
export SYSROOT="$PS4_PAYLOAD_SDK/target"
export SRCDIR="$WORKDIR/src"

if [ ! -f "$PS4_PAYLOAD_SDK/toolchain/orbis.mk" ]; then
  echo "Downloading ps4-payload-sdk into $PS4_PAYLOAD_SDK..."
  wget -q https://github.com/ps4-payload-dev/sdk/releases/latest/download/ps4-payload-sdk.zip
  unzip -oq ps4-payload-sdk.zip
fi

mkdir -p "$SRCDIR"
cd "$SRCDIR"

# Use PS4 Payload SDK cross-compiler wrappers
export CC="$PS4_PAYLOAD_SDK/bin/orbis-clang"
export CXX="$PS4_PAYLOAD_SDK/bin/orbis-clang++"
export AR="$PS4_PAYLOAD_SDK/bin/orbis-ar"
export RANLIB="$PS4_PAYLOAD_SDK/bin/orbis-ranlib"
export STRIP="$PS4_PAYLOAD_SDK/bin/orbis-strip"
export PKG_CONFIG="$PS4_PAYLOAD_SDK/bin/orbis-pkg-config"

export CFLAGS="-O2 -I$SYSROOT/include"
export LDFLAGS="-L$SYSROOT/lib"

# 1. Download sources (only zlib and OpenSSL)
[ -f zlib-1.4.tar.gz ] || wget https://zlib.net/zlib-1.4.tar.gz
[ -f openssl-4.1.0.tar.gz ] || wget https://github.com/openssl/openssl/releases/download/openssl-4.1.0/openssl-4.1.0.tar.gz

# 2. Build zlib (static, installs directly into $PS4_PAYLOAD_SDK/target)
rm -rf zlib-1.4 && tar xf zlib-1.4.tar.gz
cd zlib-1.4
./configure --prefix="$SYSROOT" --static
make -j$(nproc) && make install
cd "$SRCDIR"

# 3. Build OpenSSL (static, installs directly into $PS4_PAYLOAD_SDK/target)
rm -rf openssl-4.1.0 && tar xf openssl-4.1.0.tar.gz
cd openssl-4.1.0
./Configure BSD-x86_64 \
  --prefix="$SYSROOT" \
  --openssldir="$SYSROOT/ssl" \
  no-shared no-dso no-async no-engine no-tests no-ui-console no-apps \
  -DOPENSSL_NO_SECURE_MEMORY
make -j$(nproc) build_libs && make install_dev
cd "$SRCDIR"

# 4. Build final PS4 Web Proxy payload (ps4-webtor.elf)
cd "$WORKDIR"
make

echo ""
echo "=== BUILD COMPLETE: $WORKDIR/ps4-webtor.elf ==="
echo "Upload via FTP to /data/payloads/ps4-webtor.elf (or /data/GoldHEN/payloads/ps4-webtor.elf)"
echo "and launch from: GoldHEN -> Utilities -> PayLoader -> LaunchPad"
