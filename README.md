# PS4 WebTor Browser Proxy Payload (`ps4-webtor`)

A native companion web proxy payload (`.elf`) with a built-in web browser UI for jailbroken PlayStation 4 consoles. Designed to work alongside **[tor-ps4](https://github.com/jessyclement2026/tor-ps4)**, it allows you to load `.onion` hidden services and modern HTTPS websites directly in the stock PS4 web browser.

## How It Works
The built-in PS4 web browser lacks modern SSL/TLS support and blocks `.onion` domains. `ps4-webtor` solves this by hosting a local HTTP web interface on the console. When you enter a URL into the WebTor UI, the payload fetches and downloads the page locally through `tor-ps4`'s SOCKS5 proxy (`127.0.0.1:9050`) using statically linked OpenSSL, then serves it cleanly over local HTTP to your PS4 browser.

## Features
- **Built-in Web Browser UI:** Clean local web interface accessible directly from the PS4 stock browser.
- **`.onion` & Modern TLS Support:** Bypasses PS4 browser restrictions using statically linked OpenSSL over `127.0.0.1:9050`.
- **No System Proxy Needed:** Works with standard **Easy** PS4 network settings—just open the local WebTor URL in your browser.
- **Custom `mmap` Allocator:** Bypasses GoldHEN's static heap limit for smooth page fetching and rendering.

## Prerequisites
- A jailbroken PS4 running **GoldHEN**.
- **[tor-ps4](https://github.com/jessyclement2026/tor-ps4)** running in the background (`127.0.0.1:9050`) and bootstrapped to 100%.

## Usage on PS4
1. Make sure **`tor-ps4.elf`** is already running in the background on your PS4.
2. Enable **FTP Server** in **GoldHEN → Settings → Server Settings** (port `2121`).
3. Upload `ps4-webtor.elf` via FTP to **`/data/payloads/ps4-webtor.elf`** (or **`/data/GoldHEN/payloads/ps4-webtor.elf`**).
4. On your PS4, open **GoldHEN → Utilities → PayLoader → LaunchPad** and select **`ps4-webtor.elf`**.
5. Open the PS4 **Internet Browser** and go to **`http://127.0.0.1:8080`**
6. Type any `.onion` or `https://` address into the WebTor search/URL bar to load it.

## Building from Source

### 1. Install Build Tools & SDK Dependencies

- **Arch / CachyOS:**
  ```bash
  sudo pacman -S --needed base-devel clang lld llvm cmake autoconf automake libtool pkgconf git wget python
  ```

- **Debian / Ubuntu:**
  ```bash
  sudo apt-get update && sudo apt-get upgrade # optional
  sudo apt-get install -y bash clang-18 lld-18 build-essential autoconf automake libtool git wget python3 # required
  sudo apt-get install -y socat cmake meson pkg-config # optional / build tools
  ```

- **Fedora (tested on Fedora 41):**
  ```bash
  sudo dnf install -y bash llvm-devel clang lld autoconf automake libtool git wget python3 # required
  sudo dnf install -y socat cmake meson pkg-config # optional / build tools
  ```

- **macOS (tested on macOS Sequoia via Homebrew):**
  ```bash
  brew install llvm@18 autoconf automake libtool wget # required
  export LLVM_CONFIG=/opt/homebrew/opt/llvm@18/bin/llvm-config # required
  brew install socat cmake meson pkg-config # optional / build tools
  ```

### 2. Download `ps4-payload-sdk` & Build Payload
```bash
wget https://github.com/jessyclement2026/tor-ps4/jessyclement2026/ps4-webtor/ps4-payload-dev/sdk/releases/latest/download/ps4-payload-sdk.zip
unzip ps4-payload-sdk.zip
export PS4SDK=\$PWD/ps4-payload-sdk

chmod +x build.sh
./build.sh
```
This compiles `zlib` and `OpenSSL`, and outputs `ps4-webtor.elf`.

## Third-Party Libraries
Pre-compiled releases statically link:
- **OpenSSL** (Apache License 2.0)
- **zlib** (zlib License)

## License & Disclaimer
This project is licensed under the **BSD 3-Clause License** — see the [LICENSE](LICENSE) file for details.  
*Note: This is an unofficial community project and is not affiliated with or endorsed by The Tor Project.*

> **⚠️ Warning:** Make sure **System Software Updates** are disabled in **PS4 Settings → System → Automatic Downloads** before going online so you do not accidentally upgrade your firmware and lose your jailbreak.

## Brewology Verification
Official releases for this project are also published on the Brewology PS4 Homebrew Store by **`cutiefly`**.

*Disclaimer: This software is an independent, open-source community project developed using the open-source `ps4-payload-sdk` and is not affiliated with or endorsed by The Tor Project or Sony Interactive Entertainment. It is provided "as is", without warranty of any kind, strictly for lawful privacy, educational, and research purposes. Users are solely responsible for complying with their local laws when using this software.*

## Credits & License
- **Tor Core:** Built on top of the C-Tor source code from [The Tor Project](https://torproject.org) (BSD-3-Clause).
- **Dependencies:** Statically linked with [OpenSSL 4.1.0](https://www.openssl.org/) and [zlib 1.4](https://zlib.net).
- **SDK:** [ps4-payload-dev / ps4-payload-sdk](https://github.com/ps4-payload-dev/sdk).
- **License:** **GPL-3.0-or-later** (inherited from `PBS-Payload`).

---

## Support / Donations

If **ps4-stream** helped you stream your PS4 with ultra-low latency and you'd like to support further PS4 homebrew development (such as PS4 audio streaming support and testing on more firmwares):

- **Bitcoin (BTC):** `bc1qt9wxjtq6th3nxmkwg5pa8jhskrn29z6zd7nvum`

Starring the repository (⭐) or sharing test results on other PS4 firmwares also helps a lot!
