# LibreConnect

LibreConnect is a privacy-focused, peer-to-peer connectivity app that links desktop and mobile devices without relying on third-party cloud relays for core communication.

## What this project solves

LibreConnect is built to solve everyday cross-device friction:

- pairing desktop and mobile devices on the same network
- sharing files directly between paired devices
- streaming camera data securely over the local network
- syncing notifications and clipboard content across platforms
- using a phone as a media remote, remote keyboard, and presenter controller
- accessing Android SMS conversations from the desktop app
- triggering Find My Phone actions on a paired mobile device
- doing all of the above with end-to-end encrypted transport and local-first architecture

## Core functionalities

- Local network device discovery
  - discovers nearby devices on the same network
  - supports pairing and trust establishment between devices
- Secure peer-to-peer communication
  - encrypted transport between connected devices
  - bidirectional messaging/event exchange model
- Cross-device file transfer
  - transfer of files and folders between paired devices
  - integrity validation during transfers
- Camera/media streaming scenarios
  - real-time media transport between devices
  - desktop virtual camera integration on supported platforms
- Notification sharing/synchronization
  - propagates selected notification events across connected devices
  - adapts behavior to each target platform
- Clipboard synchronization
  - syncs clipboard content between paired devices
  - supports desktop-to-mobile and mobile-to-desktop workflows
- Remote input / presenter controls
  - media controls, remote keyboard input, and presenter-mode actions from mobile to desktop
  - uses platform-specific input backends on desktop platforms
- SMS bridge and device utilities
  - exposes Android SMS conversations and send actions in the desktop app
  - includes utility actions such as Find My Phone and shared system/device information
- Cross-platform client experience
  - desktop and mobile frontends built with shared architecture
  - modular feature system so capabilities can be enabled/extended over time

## Project structure

```text
apps/                    # Desktop + mobile app entry points and QML UI
android/                 # Native Android manifest, Gradle config, and platform glue
installers/              # Packaging scripts for Linux and Windows
modules/                 # Feature modules (common + desktop/mobile implementations)
  clipboard-sync/
  common/
  file-share/
  modules-manager/
  network-camera/
  notification-sync/
  remote-input/
  sms-bridge/
  system-info-share/
utilities/               # Shared infrastructure and low-level components
  clipboard/
  find-my/
  network/
  p2p-network/
  permission-manager/
  srtp-stream/
  file-system/
  notifications-handler/
  remote-input/
  sms-handler/
  system-info/
  virtual-camera/
tests/                   # Unit tests and test programs
tools/                   # Local developer utilities
cmake/                   # CMake helper scripts/macros
configure_project.py     # Environment + dependency/bootstrap automation
conanfile.py             # Conan integration (requirements by platform)
conandata.yml            # Dependency versions
```

## Dependencies

### Build/tooling

- [CMake](https://cmake.org/) `4.3.0` (via Conan tool requirement)
- [Ninja](https://ninja-build.org/) `1.13.2`
- [Conan](https://conan.io/) (package/dependency manager)
- [pkgconf](https://github.com/pkgconf/pkgconf) `2.5.1`
- [Python 3](https://www.python.org/) (for `configure_project.py`)

### Core runtime libraries

- [Qt 6](https://www.qt.io/product/qt6) (Core, Gui, Quick, Qml, QuickControls2, Multimedia, Network, and DBus on Linux desktop)
- [FFmpeg](https://ffmpeg.org/) `7.1.3`
- [OpenSSL](https://www.openssl.org/) `3.6.1`
- [Asio](https://think-async.com/Asio/) `1.36.0`
- [Boost](https://www.boost.org/) `1.90.0`
- [fmt](https://fmt.dev/) `12.1.0`
- [magic_enum](https://github.com/Neargye/magic_enum) `0.9.7`
- [nlohmann/json](https://github.com/nlohmann/json) `3.12.0`
- [concurrentqueue](https://github.com/cameron314/concurrentqueue) `1.0.4`
- [libsrtp](https://github.com/cisco/libsrtp) `2.6.0`
- [xxHash](https://github.com/Cyan4973/xxHash) `0.8.3`

### Platform-specific dependencies

- Desktop tests:
  - [GoogleTest](https://github.com/google/googletest) `1.17.0`
  - [Google Benchmark](https://github.com/google/benchmark) `1.9.4`
- Windows:
  - [WIL (Windows Implementation Library)](https://github.com/microsoft/wil) `1.0.250325.1`
  - [WinToast](https://github.com/mohabouje/WinToast) (vendored under `vendor/WinToast`)
- Android:
  - [FFmpeg](https://ffmpeg.org/) `7.1.3`
- Linux:
  - [ALSA](https://www.alsa-project.org/) `1.2.10`

### Fetched at configure/build time

- [Debug-Log](https://github.com/ddj4747/Debug-Log) (CMake `FetchContent`)

## Prerequisites

- C++20-capable compiler
- Conan profile initialized:

```bash
conan profile detect --force
```

- A filled `.env` file (project root) with required variables:
  - `BUILD_FOR=Desktop`, `BUILD_FOR=Android`, or `BUILD_FOR=All`
  - `DISABLE_DEBUG=true|false`
  - `BUILD_TESTS=true|false`
  - Qt path(s):
    - `QT_DIR_DESKTOP` for desktop builds
    - `QT_DIR_ANDROID` for Android builds
  - Android-only values when `BUILD_FOR=Android`:
    - `ANDROID_NDK_DIR`
    - `ANDROID_SDK_DIR`
    - `ANDROID_ARCH` (`armv8`, `armv7`, `x86_64`)
    - `ANDROID_CLANG_VERSION`
    - `ANDROID_OS_API_LEVEL`

## Build

### 1) Install/prepare dependencies

```bash
python configure_project.py
```

This script:

- validates `.env`
- resolves Conan dependencies per target platform
- prepares FFmpeg integration for desktop builds
- installs additional system/deployment dependencies on Linux when required

### 2) Configure + build with CMake presets

Desktop release:

```bash
cmake --preset desktop-release
cmake --build --preset desktop-release
```

Desktop debug (if enabled by `.env`):

```bash
cmake --preset desktop-debug
cmake --build --preset desktop-debug
```

Android release:

```bash
cmake --preset android-release
cmake --build --preset android-release
```

Android debug (if enabled by `.env`):

```bash
cmake --preset android-debug
cmake --build --preset android-debug
```

## Test

If desktop tests are enabled (`BUILD_TESTS=true`), run:

```bash
ctest --preset desktop-release
```

or:

```bash
ctest --preset desktop-debug
```

For Android builds, `BUILD_TESTS=true` enables additional mobile test targets and test apps in `tests/`.

## CI coverage

GitHub Actions currently builds:

- Windows desktop
- Linux desktop (x64 + ARM64)
- macOS desktop
- Android (armv8)

Workflow file: `.github/workflows/Build.yml`

Release packaging is handled separately by `.github/workflows/Deploy.yml`.

## License

This project is licensed under **GNU General Public License v3.0**. See [LICENSE](LICENSE).
