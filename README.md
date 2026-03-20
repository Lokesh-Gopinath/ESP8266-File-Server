# ESP8266-File-Server

# ESP8266 Notes Server & Auto-Tunneling (bore.pub)

A secure, remote-access notes server for the ESP8266. This project features a **Telnet interface**, local storage via **LittleFS**, and an **automatic TCP tunnel** using `bore.pub`. It also pushes its dynamic public port to a GitHub repository so you can always find your device from anywhere in the world.

---

## 🚀 Features

* **Remote Access:** Access your ESP8266 via Telnet from any network using `bore.pub` tunneling.
* **GitHub Port Tracking:** Automatically updates a `ports.txt` file in your GitHub repo with the latest tunnel port.
* **Persistent Storage:** Save and manage notes locally on the flash memory using **LittleFS** and **ArduinoJson**.
* **Time Awareness:** Syncs IST (India Standard Time) via HTTP headers without needing a dedicated NTP library.
* **Multi-Port Fallback:** Attempts multiple ports (7889, 8080, etc.) to bypass restricted firewalls.
* **Security:** Simple username/password authentication for the Telnet session.

---

## 🛠️ Hardware & Software Requirements

### Hardware
* **ESP8266 Board** (NodeMCU, Wemos D1 Mini, etc.)
* A stable WiFi connection (Mobile Hotspot recommended if port 7889 is blocked).

### Libraries Used
* `ESP8266WiFi` & `ESP8266HTTPClient`
* `LittleFS` (Local file storage)
* `ESPTelnet` (by Lennart Hennigs)
* `ArduinoJson` (v6 or higher)
* `WiFiClientSecure` (GitHub API communication)

---

## ⚙️ Configuration

Before uploading, update the following variables in the code:

### 1. WiFi & Credentials
| Variable | Description |
| :--- | :--- |
| `ssid` | Your WiFi Name |
| `wifi_password` | Your WiFi Password |
| `telnet_user` | Username for Telnet login |
| `telnet_pass` | Password for Telnet login |

### 2. GitHub Integration
* **Personal Access Token:** Create a token (classic) with `repo` scope.
* **`githubRepo`:** Use format `"username/repository"`.
* **`dataBranch`:** The branch where `ports.txt` and `log.txt` will be stored.

### 3. Static IP
Adjust `local_IP`, `gateway`, and `subnet` to match your local network range (default is `192.168.0.x`).

---

## 🖥️ Usage

### Connecting
Once the ESP8266 boots, it will attempt to create a tunnel and sync time.
1.  Check the **Serial Monitor** to see the assigned public port.
2.  Alternatively, check your **GitHub Repository** for the updated `ports.txt`.
3.  Connect via your terminal:
    ```bash
    telnet bore.pub <PORT_FROM_GITHUB>
    ```

### Telnet Commands
Once authenticated, the following commands are available:

| Command | Description |
| :--- | :--- |
| `help` | Lists all available commands. |
| `add title:"..." content:"..."` | Saves a new note to LittleFS. |
| `list` | Displays titles of all saved notes. |
| `status` | Shows system uptime, heap memory, and tunnel status. |
| `reconnect` | Forces a new tunnel connection to `bore.pub`. |
| `setport <num>` | Manually update the port on GitHub if auto-tunnel fails. |
| `settime HH MM SS` | Manually set the system time. |
| `format` | Deletes all notes from local storage. |
| `logout` | Ends the current session. |

---

## 📂 Data Management

* **Local Storage:** Notes are stored in `/notes.json` on the ESP8266 flash.
* **Remote Logs:** Critical events (Boot, Login, Formatting) are appended to `log.txt` on GitHub.
* **Base64 Encoding:** Content is encoded to Base64 before being pushed to GitHub to ensure data integrity during HTTP transfers.

---

## ⚠️ Troubleshooting
* **Tunnel Failed:** Many ISP routers block port `7889`. If the tunnel won't connect, try a mobile hotspot.
* **GitHub Push 401/404:** Verify your GitHub Token is valid and the repository name/branch is exactly correct.
* **Memory Issues:** The ESP8266 has limited RAM. Large note files may cause crashes; keep notes concise or monitor `status` for free heap space.
