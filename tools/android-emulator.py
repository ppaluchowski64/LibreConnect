from tkinter import filedialog, messagebox
import tkinter as tk
import subprocess
import threading
import json
import os
import platform

CONFIG_FILE = os.path.expanduser("~/.LibreConnect-cache/emulator_config.json")
SIGN_KEY_PATH = os.path.expanduser("~/.LibreConnect-cache/apk_key.key")
KEY_PASSWORD = "MyKeyPassword123"

window = tk.Tk()
window.geometry("400x500")
window.title("Emulator")

main_frame = tk.Frame(window)
running_frame = tk.Frame(window)

env_file_path = "../.env"

# Check for environment variables file
if os.path.exists(".env"):
    env_file_path = ".env"
else:
    if not os.path.exists("../.env"):

        print("No .env file found")
        input("Press Enter to continue...")
        exit(-1)

# Load .env
with open(env_file_path, "r") as f:
    for line in f:
        line = line.strip()
        if line and not line.startswith("#"):
            key, _, value = line.partition("=")
            os.environ[key.strip()] = value.strip()

sdk_dir = os.path.expanduser(os.environ.get("ANDROID_SDK_DIR", ""))
if not sdk_dir:
    raise EnvironmentError("ANDROID_SDK_DIR environment variable is not set.")

is_windows = platform.system() == "Windows"
exe_suffix = ".exe" if is_windows else ""
bat_suffix = ".bat" if is_windows else ""

build_tools_base = os.path.join(sdk_dir, "build-tools")
build_tools_version = 0
build_tools_max_version_dir = ""

if os.path.exists(build_tools_base):
    for entry in os.listdir(build_tools_base):
        full_path = os.path.join(build_tools_base, entry)
        if os.path.isdir(full_path):
            try:
                major_v = int(entry.split(".")[0])
                if major_v >= build_tools_version:
                    build_tools_max_version_dir = entry
                    build_tools_version = major_v
            except ValueError:
                continue

build_tools_dir = os.path.join(build_tools_base, build_tools_max_version_dir)

emulator_path = os.path.join(sdk_dir, "emulator", f"emulator{exe_suffix}")
adb_path = os.path.join(sdk_dir, "platform-tools", f"adb{exe_suffix}")
apk_signer_path = os.path.join(build_tools_dir, f"apksigner{bat_suffix}")
def load_last_apk():
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, "r") as f:
                data = json.load(f)
                return data.get("last_apk", "")
        except (json.JSONDecodeError, IOError):
            return ""
    return ""

def save_last_apk(_path):
    try:
        os.makedirs(os.path.dirname(CONFIG_FILE), exist_ok=True)
        with open(CONFIG_FILE, "w") as f:
            json.dump({"last_apk": _path}, f)
    except IOError:
        pass

def sign_apk(_apk_path: str):
    os.makedirs(os.path.dirname(SIGN_KEY_PATH), exist_ok=True)

    if not os.path.exists(SIGN_KEY_PATH):
        print("hit")
        subprocess.run([
            "keytool",
            "-genkeypair",
            "-keystore", SIGN_KEY_PATH,
            "-storepass", KEY_PASSWORD,
            "-keypass", KEY_PASSWORD,
            "-alias", "app_release",
            "-keyalg", "RSA",
            "-keysize", "2048",
            "-validity", "10000",
            "-dname", "CN=John, OU=Mobile, O=MyCompany, L=Yes, S=DF, C=HD"
        ])

    name = f"{os.path.dirname(_apk_path)}/signed-{os.path.basename(_apk_path)}"

    try:
        subprocess.run([
            apk_signer_path, "sign",
            "--ks", SIGN_KEY_PATH,
            "--ks-pass", f"pass:{KEY_PASSWORD}",
            "--key-pass", f"pass:{KEY_PASSWORD}",
            "--out", name,
            _apk_path,
        ], check=True)
        return name
    except subprocess.CalledProcessError as e:
        messagebox.showerror("Signing Error", f"Failed to sign APK.\n{e}")
        return ""

def refresh_devices_list(_devices: tk.Listbox):
    try:
        _devices.delete(0, tk.END)
        result = subprocess.run(
            [emulator_path, "-list-avds"],
            capture_output=True,
            text=True,
            check=True
        )

        if result.returncode != 0:
            _devices.insert(tk.END, "Error finding emulator")
            return

        for device in result.stdout.splitlines():
            _devices.insert(tk.END, device)

    except FileNotFoundError:
        _devices.insert(tk.END, "Emulator not found")

def start_emulator(_device: str):
    def run():
        main_frame.pack_forget()
        running_frame.pack(fill="both", expand=True)
        subprocess.run([emulator_path, "-avd", _device, "-no-snapshot", "-wipe-data"])
        running_frame.pack_forget()
        main_frame.pack(fill="both", expand=True)

    threading.Thread(target=run, daemon=True).start()

def install_apk(_apk_path: str):
    signed_apk_dir = sign_apk(_apk_path)

    if os.path.exists(signed_apk_dir):
        subprocess.run([adb_path, "install", "-r", signed_apk_dir])
        save_last_apk(_apk_path)

def browse_apk(_entry: tk.Entry):
    apk_path = filedialog.askopenfilename(
        title="Select APK file",
        filetypes=[("Android APK", "*.apk")],
    )

    if apk_path:
        _entry.delete(0, tk.END)
        _entry.insert(0, apk_path)
        save_last_apk(apk_path)

main_frame.pack(fill="both", expand=True)

# Main Frame

tk.Label(main_frame, text="Select and start android emulator").pack(padx=5, pady=5)

devices = tk.Listbox(main_frame, width=50, height=20)
devices.pack(padx=20, pady=20)

refresh_devices_list(devices)

tk.Button(main_frame, text="Refresh Devices", command=lambda: refresh_devices_list(devices)).pack(padx=20, pady=5)
tk.Button(main_frame, text="Start Emulator", command=lambda: start_emulator(devices.get(devices.curselection()[0]))).pack(padx=20, pady=5)

# Running Frame

tk.Label(running_frame, text="APK path").pack(padx=5, pady=5)

apk_entry = tk.Entry(running_frame, width=40)
apk_entry.pack(padx=20, pady=5)

saved_path = load_last_apk()
if saved_path:
    apk_entry.insert(0, saved_path)

tk.Button(
    running_frame,
    text="Browse APK",
    command=lambda: browse_apk(apk_entry)
).pack(padx=20, pady=5)

tk.Button(running_frame, text="Install APK", command=lambda: install_apk(apk_entry.get())).pack(padx=20, pady=5)

window.mainloop()
