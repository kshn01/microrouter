# 📖 MicroRouter Novice Setup Guide

Welcome to MicroRouter! If you are new to ESP32 microcontrollers, home networking, or DNS filtering, this guide will walk you through everything step-by-step in plain English.

---

## 🧭 Table of Contents
1. [What Does MicroRouter Do?](#1-what-does-microrouter-do)
2. [What You Need (Hardware & Tools)](#2-what-you-need)
3. [Step 1: Installing the Software on the ESP32](#step-1-installing-the-software-on-the-esp32)
4. [Step 2: Connecting the ESP32 to Your Home Wi-Fi](#step-2-connecting-the-esp32-to-your-home-wi-fi)
5. [Step 3: Pointing Your Router (or Devices) to MicroRouter](#step-3-pointing-your-router-or-devices-to-microrouter)
6. [Step 4: Using the MicroRouter Web Dashboard](#step-4-using-the-microrouter-web-dashboard)
7. [Alternative: Setting Up Single Devices (Without Touching Router)](#alternative-setting-up-single-devices-without-touching-router)
8. [Frequently Asked Questions & Troubleshooting](#frequently-asked-questions--troubleshooting)

---

## 1. What Does MicroRouter Do?

Most basic home Wi-Fi routers (provided by your internet provider or bought off the shelf) lack advanced security features. They don't block ads, they don't let you set bedtime curfews for your kids, and they don't show you which device is doing what on your network.

**MicroRouter solves this using a $5 ESP32-S3 microcontroller.**

Think of the ESP32 as a **smart security guard** standing next to your router:
- Whenever a phone or computer on your Wi-Fi wants to visit a website (like `google.com` or `tiktok.com`), it asks the security guard: *"What is the IP address for this website?"* (This process is called **DNS**).
- If it's a good website, MicroRouter instantly answers and connects you.
- If it's an advertisement, tracker, malware site, or blocked social media app, MicroRouter says: *"Blocked!"*
- At bedtime, MicroRouter can enforce a scheduled Wi-Fi curfew.

---

## 2. What You Need

1. **An ESP32-S3 board**:
   - Any standard **ESP32-S3 DevKit** with 8MB Flash (available on Amazon, AliExpress, or electronic hobby stores for $4 to $6).
2. **A USB-C Cable**:
   - To connect the board to your computer initially, and later to power it from any USB wall charger or your router's USB port.
3. **Your Computer (Mac, Windows, or Linux)**:
   - To upload the code once.

---

## Step 1: Installing the Software on the ESP32

We use **PlatformIO**, which is a free tool for building and uploading code to microcontrollers.

### 1. Install PlatformIO
- Download and install [Visual Studio Code](https://code.visualstudio.com/) (free).
- Open VS Code, click the **Extensions** icon on the left sidebar, search for **PlatformIO IDE**, and click **Install**.

### 2. Plug In the ESP32
- Plug your ESP32-S3 board into your computer using a USB-C data cable.

### 3. Open This Project
- In VS Code, go to **File > Open Folder...** and select the `dev` folder containing this project.

### 4. Upload Firmware & Web Files
You only need to run two commands in the VS Code terminal (or click the PlatformIO checkmark & arrow buttons at the bottom):

```bash
# Upload the C++ firmware into the ESP32
pio run -e serial -t upload

# Upload the web interface (HTML/CSS/JS) into the ESP32 flash memory
pio run -e serial -t uploadfs
```

> 💡 *Once both commands say `[SUCCESS]`, the ESP32 is ready! You can now unplug it from your computer and plug it into any USB power brick near your Wi-Fi router.*

---

## Step 2: Connecting the ESP32 to Your Home Wi-Fi

When the ESP32 boots for the first time, it doesn't know your home Wi-Fi password yet. So it temporarily broadcasts its own setup hotspot.

1. Take your phone or laptop and go to your **Wi-Fi settings**.
2. Look for a Wi-Fi network named **`MicroRouter-Setup`**.
3. Tap it and enter the password:
   ```
   setup1234
   ```
4. A setup portal should pop up automatically. If it doesn't, open any web browser (Safari, Chrome) and visit:
   ```
   http://192.168.4.1
   ```
5. On the screen, you will see a list of nearby Wi-Fi networks. Select your home Wi-Fi, enter your Wi-Fi password, and tap **Connect**.
6. The ESP32 will reboot and join your home Wi-Fi! Note the IP address shown on the screen (for example, `192.168.1.142`).

---

## Step 3: Pointing Your Router (or Devices) to MicroRouter

To let MicroRouter protect all devices in your home, your home router needs to know that the ESP32 is the **DNS Server**.

### Method: Configure in Your Main Router (Protects Everything)
1. Open a browser and log into your home router's admin page (usually `http://192.168.1.1` or `http://192.168.0.1` — check the sticker on the bottom of your router).
2. Go to **Network Settings**, **LAN**, or **DHCP Server**.
3. Look for the setting called **DNS Server** or **Primary DNS**.
4. Change it from `Auto` or `8.8.8.8` to your **ESP32's IP address** (e.g. `192.168.1.142`).
5. *(Optional but recommended)* For **Secondary DNS**, enter `1.1.1.1` (Cloudflare) so your internet keeps working if the ESP32 is ever unplugged.
6. Click **Save** or **Apply**, then restart your router.

🎉 That's it! Now every device in your home is protected by MicroRouter!

---

## Step 4: Using the MicroRouter Web Dashboard

From any device connected to your home Wi-Fi, open your browser and go to:
👉 **`http://microrouter.local`** (or enter the ESP32's IP address directly, e.g. `http://192.168.1.142`)

You will see the dark glassmorphism dashboard:

### 1. 🛡️ DNS Shield (`#/dns`)
- **Choose an Upstream Provider**:
  - **Cloudflare (`1.1.1.1`)**: Maximum speed and privacy.
  - **Cloudflare Family Safe (`1.1.1.3`)**: Blocks malware and adult sites automatically.
  - **AdGuard (`94.140.14.14`)**: Blocks ads and trackers network-wide.
  - **Custom**: Enter any DNS IP you want (e.g. Pi-hole or NextDNS).
- **Content Shields**:
  - **DoH Canary Sinkhole**: Prevents web browsers from secretly bypassing MicroRouter.
  - **Meta & Social Shield**: Sinkholes Facebook, Instagram, and WhatsApp.
  - **TikTok Shield**: Blocks TikTok servers and video streams.
- **Network Spyglass**:
  - Watch live domain lookups as devices in your home browse the web. See what is allowed and what was blocked in real-time.

### 2. 📱 Device Inventory (`#/devices`)
- Displays all devices connected to your network.
- Shows the **manufacturer brand** (Apple, Samsung, Nintendo, Sony, TP-Link, Xiaomi, etc.).
- Shows **Windows PC names** detected via NetBIOS.
- **Block / Allow button**: Cut off internet access for an individual device with one click.
- **Waiver button**: Grant a device temporary access (15m, 30m, 1h, 2h) during curfew hours.

### 3. 🌙 Parental Controls (`#/parental`)
- **Bedtime Curfew**:
  - Set the time when kids should be asleep (e.g. `22:00` / 10 PM) and when Wi-Fi re-enables in the morning (e.g. `06:30` AM).
  - MicroRouter synchronizes with real-world atomic clocks (NTP) to enforce this automatically.
- **Bandwidth Quotas**:
  - Set a maximum daily or hourly limit (in MB) for guest stations.

---

## Alternative: Setting Up Single Devices (Without Touching Router)

If you live in a rented apartment, dorm, or don't have access to your main router's admin password, you can still use MicroRouter on specific devices:

### On iPhone / iPad:
1. Go to **Settings > Wi-Fi** and tap the **(i)** next to your Wi-Fi name.
2. Scroll down to **Configure DNS** and tap **Manual**.
3. Delete existing servers, tap **Add Server**, and type your ESP32's IP (e.g. `192.168.1.142`).
4. Tap **Save**.

### On Android:
1. Go to **Settings > Network & Internet > Wi-Fi**.
2. Tap your Wi-Fi network gear icon, tap **Edit / Advanced**.
3. Change IP Settings from *DHCP* to *Static*.
4. In **DNS 1**, enter your ESP32's IP (e.g. `192.168.1.142`). Tap **Save**.

### On Windows 10/11:
1. Open **Settings > Network & Internet > Wi-Fi > Hardware Properties**.
2. Next to **DNS server assignment**, click **Edit**.
3. Change to **Manual**, enable IPv4, and enter your ESP32's IP under **Preferred DNS**.

### On Mac (macOS):
1. Open **System Settings > Wi-Fi > Details... > DNS**.
2. Click **+** and add your ESP32's IP address. Click **OK**.

---

## Frequently Asked Questions & Troubleshooting

### Q: What if `http://microrouter.local` doesn't open?
**Answer**: Some Android phones and older Windows PCs don't support mDNS (`.local` addresses). Simply type the ESP32's numerical IP address into your browser's address bar instead (e.g. `http://192.168.1.142`). You can find this IP address in your router's client list.

### Q: What happens if the ESP32 is unplugged or loses power?
**Answer**: If you entered a Secondary DNS in your router (like `1.1.1.1`), your devices will automatically failover to Cloudflare after a couple of seconds, so your internet will not go down.

### Q: How do I update MicroRouter firmware in the future?
**Answer**: You don't need to connect it to your computer again! MicroRouter has built-in wireless **Dual-Bank OTA**. Simply open:
👉 `http://microrouter.local/update`
Log in with username `admin` and password `microrouter`, then upload the new `.bin` file. If the update ever fails, it automatically rolls back to the previous working version.

### Q: Does MicroRouter slow down my internet speed?
**Answer**: No! MicroRouter only handles **DNS queries** (looking up domain names like `netflix.com`), which are tiny UDP packets (less than 1 kilobyte each). Once the domain lookup is answered, all high-speed video streaming and downloads travel directly between your device and your main router at full gigabit/fiber speed.

---

## 💬 Need Help?
- Check the [Main README](README.md) for architecture details.
- View the [Walkthrough Artifact](file:///Users/kishansuthar/.gemini/antigravity-ide/brain/e6740534-3272-480f-8c14-e1628323921d/walkthrough.md) for firmware and technical specs.
