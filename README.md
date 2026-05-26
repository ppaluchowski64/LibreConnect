<h1 align="center">
  <img src="apps/desktop/res/libreconnect_logo.png" alt="LibreConnect Logo" width="150"><br>
  LibreConnect
</h1>

<p align="center">
  An application for seamless communication and integration between computers and mobile devices, developed with a strong focus on user privacy. LibreConnect provides a unified ecosystem that bridges the gap between your devices while ensuring your data remains under your control.
</p>

---

## Core Philosophy: Privacy by Design

LibreConnect is built from the ground up with privacy as its foundational principle. Unlike many similar solutions, it does not rely on intermediary servers or cloud infrastructure.

- **Local Network Only:** All communication occurs exclusively within your local network.
- **Peer-to-Peer (P2P):** Devices connect directly to each other, eliminating third-party bottlenecks and potential points of interception.
- **End-to-End Encryption (E2E):** All data transmitted between devices is encrypted end-to-end, providing an additional layer of security even in trusted local environments.
- **Open Source:** The codebase is entirely transparent and open for auditing, ensuring no hidden telemetry or data collection.

---

## Key Features

### Device Info
Get an instant overview of your connected devices.
- Monitor hostname and device identification.
- Real-time battery level tracking for both mobile devices and computers (where hardware permits).
<img width="336" height="127" alt="image" src="https://github.com/user-attachments/assets/0af4b8fd-962d-4e52-afd5-29f32646ffca" />

### Virtual Camera
Transform your smartphone into a high-quality webcam for your computer. The system recognizes the phone as a native camera device.
- Adjustable resolution and orientation.
- Status notifications on the mobile device with quick-disable functionality.
<img width="2097" height="1215" alt="image" src="https://github.com/user-attachments/assets/7ec36cc8-6522-4ee5-9c31-3e6468d3ed55" />

### Virtual Microphone
Use your mobile device as a wireless microphone for your desktop.
- Seamless integration with system audio inputs.
- Active status notifications for privacy awareness.
<img width="1696" height="1046" alt="image" src="https://github.com/user-attachments/assets/472ba009-d103-4ab7-a3f2-fb4a63fbf738" />

### File Manager
Access and manage your Android file system directly from your desktop.
- Browse files with thumbnail previews.
- Open mobile files directly on your computer.
- Drag-and-drop support for near-instant file transfers.
- Share Provider: Integrated with the Android system share menu, allowing you to send files, images, or links from your phone to your computer instantly.
<img width="1260" height="788" alt="image" src="https://github.com/user-attachments/assets/ebd239ce-2a1c-495d-86f5-ae2d254734cf" />

### Notification Sync
Receive and manage mobile notifications on your desktop.
- View all active notifications in the desktop UI.
- Dismiss notifications on your phone directly from your computer.
<img width="918" height="438" alt="image" src="https://github.com/user-attachments/assets/bf26945c-3b43-42fc-9890-e8e3ed9dff5a" />

### Clipboard Sync
Synchronize your clipboard across all devices.
- **Desktop to Mobile:** Automatic synchronization.
- **Mobile to Desktop:** Manual synchronization via Quick Settings tile (due to Android security restrictions).
- Optional manual-only mode for both platforms.

### SMS/MMS Messaging
Read and reply to text messages from your computer.
- Full conversation history access.
- Seamless message sending as if using the phone directly.
<img width="933" height="777" alt="image" src="https://github.com/user-attachments/assets/43f7c247-93e8-43c9-b6fd-9155e6025ba5" />

### Media Remote
Bidirectional multimedia control for both computers and mobile devices.
- Synchronized metadata: Title, Artist, Album, Cover Art, and Playback Position.
- System-level media notifications for quick control without opening the app.
- Remote volume adjustment.
<img width="1566" height="893" alt="image" src="https://github.com/user-attachments/assets/b57353ca-b068-4fa3-86bf-62fe2bbd645b" />

### Remote Keyboard
Utilize your mobile device as a remote keyboard for your computer.

<p align="left">
  <img width="300" alt="image" src="https://github.com/user-attachments/assets/b1f25f77-0ccb-47eb-8885-e825a90c3020" />
</p>

### Presenter Mode
Control slideshow presentations remotely.
- Start/End presentation mode.
- Slide navigation.
<img width="300" alt="image" src="https://github.com/user-attachments/assets/91b86c14-7ea3-4424-a596-9cf292497104" />

### Find My Phone
Locate your misplaced mobile device from your computer.
- Triggers an audible alert at maximum volume.
- Overrides "Do Not Disturb" and silent modes.
<img width="332" height="196" alt="image" src="https://github.com/user-attachments/assets/409d599b-529e-498f-aee7-7e65705ba1dd" />

### Streamer Mode
A built-in privacy feature designed for those who share or stream their screens.
- Instantly masks sensitive data within the application.
- Replaces hostnames, IP addresses, and contact information with generic placeholders (e.g., "Connected Device", "Unknown Contact") to prevent accidental disclosure.
<img width="1255" height="363" alt="image" src="https://github.com/user-attachments/assets/cb30ad1c-345b-4e1b-8b09-c88da74d76c1" />

---

## Supported Platforms

LibreConnect aims to support as many modern platforms as possible, with the currently supported systems listed below.

| Operating System | Package Format | Supported Architectures |
|------------------|----------------|-------------------------|
| **Windows**      | .msi           | x86_64, ARM64           |
| **Linux**        | .deb, .rpm, .pkg.tar.zst, .tar.gz | x86_64, ARM64 |
| **macOS**        | .dmg           | ARM64 (Apple Silicon)   |
| **Android**      | .apk           | ARM64                   |

*Note: iOS is currently not supported due to platform restrictions regarding background processes and permissions.*

---

## Download

The simplest way to get LibreConnect is to download the latest installation package for your platform from our [Releases](https://github.com/ppaluchowski64/LibreConnect/releases) page.

### Recommended Installation Methods

For certain operating systems, we provide automated installation methods to ensure you always have the latest features and security updates.

#### Android
We offer two primary ways to keep LibreConnect updated on Android. You can use **Obtainium** to track our GitHub releases directly, or add our repository to **F-Droid**.

<a href="https://apps.obtainium.imranr.dev/redirect.html?r=obtainium://app/%7B%22id%22%3A%22com.LibreConnect.mobile%22%2C%22url%22%3A%22https%3A%2F%2Fgithub.com%2Fppaluchowski64%2FLibreConnect%22%2C%22name%22%3A%22LibreConnect%22%2C%22preferredAssetFilter%22%3A%22.*-arm64-v8a%5C%5C.apk%22%7D">
  <img src="https://github.com/user-attachments/assets/713d71c5-3dec-4ec4-a3f2-8d28d025a9c6" alt="Add to Obtainium" height="125">
</a>
<a href="https://libreconnect.one/fdroid/repo/index.html">
  <img src="https://f-droid.org/badge/get-it-on.png" alt="Get it on F-Droid" height="125">
</a>

#### Linux
For Linux users, we maintain native repositories that allow your system package manager to handle updates automatically.

##### Debian / Ubuntu / Linux Mint
**1. Configure the repository:**
```bash
sudo install -d -m 0755 /etc/apt/keyrings
curl -fsSL https://libreconnect.one/apt/libreconnect-archive-keyring.asc | sudo gpg --dearmor -o /etc/apt/keyrings/libreconnect-archive-keyring.gpg
echo "deb [arch=amd64,arm64 signed-by=/etc/apt/keyrings/libreconnect-archive-keyring.gpg] https://libreconnect.one/apt stable main" | sudo tee /etc/apt/sources.list.d/libreconnect.list
```

**2. Install the application:**
```bash
sudo apt update
sudo apt install -y libreconnect
```

##### Fedora / RHEL / CentOS
**1. Configure the repository:**
```bash
sudo curl -fsSL https://libreconnect.one/dnf/libreconnect.repo -o /etc/yum.repos.d/libreconnect.repo
```

**2. Install the application:**
```bash
sudo dnf install -y libreconnect
```

##### Arch Linux / Manjaro / EndeavourOS
**1. Configure the repository:**
```bash
curl -fsSL https://libreconnect.one/pacman/libreconnect-packages.gpg | sudo pacman-key --add -
sudo pacman-key --lsign-key 01E6938CCB71C239
echo -e "\n[libreconnect]\nSigLevel = Required DatabaseRequired\nServer = https://libreconnect.one/pacman/\$arch" | sudo tee -a /etc/pacman.conf
```

**2. Install the application:**
```bash
sudo pacman -Sy
sudo pacman -S --noconfirm libreconnect
```

---

## Getting Started

### Initial Setup
1. Install and launch LibreConnect on both your computer and your mobile device.
2. Ensure both devices are connected to the same local network.
3. On the mobile device, grant all requested permissions to ensure full functionality.

### Pairing Process
1. In the desktop application, your mobile device should appear with its name and an Android icon.
2. Select the device and click **Pair** (or double-click the device entry).
3. A 6-digit verification code will appear on the mobile device.
4. Enter this code into the desktop application.
5. Complete the second factor of authentication by confirming the pairing request on your mobile device.

---

## Contributing

We welcome contributions from the community. If you are interested in improving LibreConnect, please refer to our [CONTRIBUTING.md](CONTRIBUTING.md) file for guidelines on how to get started.

---

## License

LibreConnect is released under the **GNU GPL v3.0** license. See the [LICENSE](LICENSE) file for more details.
