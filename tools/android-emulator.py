import tkinter as tk
import subprocess

def refresh_devices_list(_devices: tk.Listbox):
    try:
        _devices.delete(0, tk.END)
        devices_list = subprocess.run(
            ["emulator", "-list-avds"],
            capture_output=True,
            text=True,
            check=True
        ).stdout.splitlines()

        for device in devices_list:
            _devices.insert(tk.END, device)

    except FileNotFoundError:
        _devices.insert(tk.END, "Emulator not found")

window = tk.Tk()
window.geometry("800x600")
window.title("Emulator")

devices = tk.Listbox(window, width=50, height=20)
devices.pack(padx=20, pady=20)

refresh_devices_list(devices)



window.mainloop()