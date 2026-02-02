from tkinter import filedialog
import tkinter as tk
import subprocess
import threading
import json
import os

CONFIG_FILE = os.path.expanduser("~/.LibreConnect-cache/emulator_config.json")

window = tk.Tk()
window.geometry("400x500")
window.title("Emulator")

main_frame = tk.Frame(window)
running_frame = tk.Frame(window)

def load_last_apk():
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, "r") as f:
                data = json.load(f)
                return data.get("last_apk", "")
        except (json.JSONDecodeError, IOError):
            return ""
    return ""

def save_last_apk(path):
    try:
        os.makedirs(os.path.dirname(CONFIG_FILE), exist_ok=True)
        with open(CONFIG_FILE, "w") as f:
            json.dump({"last_apk": path}, f)
    except IOError:
        pass

def refresh_devices_list(_devices: tk.Listbox):
    try:
        _devices.delete(0, tk.END)
        result = subprocess.run(
            ["emulator", "-list-avds"],
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
        subprocess.run(["emulator", "-avd", _device, "-no-snapshot", "-wipe-data"])
        running_frame.pack_forget()
        main_frame.pack(fill="both", expand=True)

    threading.Thread(target=run, daemon=True).start()

def install_apk(_apk_path: str):
    subprocess.run(["adb", "install", "-r", _apk_path])
    save_last_apk(_apk_path)

def browse_apk(entry: tk.Entry):
    apk_path = filedialog.askopenfilename(
        title="Select APK file",
        filetypes=[("Android APK", "*.apk")],
    )
    if apk_path:
        entry.delete(0, tk.END)
        entry.insert(0, apk_path)
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