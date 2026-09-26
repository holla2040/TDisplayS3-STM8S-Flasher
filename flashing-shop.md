# Flashing the jig from the shop (Windows + WSL)

The jig sits on the shop LAN (192.168.0.240). The firmware is built on
hendrix (`make bin`, output in `/tmp/arduino-build/`). These steps push that
build to the jig from the shop PC (msi) over WiFi.

## Easiest: the jig's web page

Firmware from this build on has an update section on its web page. Browse to
http://192.168.0.240/, and under **Flasher firmware** drop in
`TDisplayS3-STM8S-Flasher.ino.bin`, then click **Update flasher**. Use the
`.ino.bin`, not `.merged.bin` or `.bootloader.bin`. The jig restarts, and the
page reports when it is running the new firmware.

Get the file from hendrix:

```bash
scp holla@hendrix:/tmp/arduino-build/TDisplayS3-STM8S-Flasher.ino.bin .
```

## Fallback: ArduinoOTA (espota.py) from WSL

Use this if the web page is unavailable, for example on older firmware that
lacks the update section.

### One-time setup

1. **Python and espota.py** (WSL):
   ```bash
   sudo apt update && sudo apt install -y python3
   scp holla@hendrix:/home/holla/.arduino15/packages/esp32/hardware/esp32/3.3.0/tools/espota.py .
   ```

2. **Mirrored networking.** In WSL2's default NAT mode, WSL sits on a private
   network (e.g. 192.168.83.x/20), so the jig can't reach it. In PowerShell:
   ```powershell
   notepad $env:USERPROFILE\.wslconfig
   ```
   ```
   [wsl2]
   networkingMode=mirrored
   ```
   Then run `wsl --shutdown` and reopen WSL. `ip -4 addr` should now show the
   PC's 192.168.0.x address.

3. **Firewall rules** (PowerShell as Administrator). The jig connects back to
   the PC for the image, so allow inbound TCP 3233 through both the WSL
   (Hyper-V) firewall and the Windows firewall:
   ```powershell
   $wsl = '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}'
   New-NetFirewallHyperVRule -Name espota -DisplayName espota -Direction Inbound -VMCreatorId $wsl -Protocol TCP -LocalPorts 3233
   New-NetFirewallRule -DisplayName espota -Direction Inbound -Protocol TCP -LocalPort 3233 -Action Allow
   ```

### Each update

```bash
scp holla@hendrix:/tmp/arduino-build/TDisplayS3-STM8S-Flasher.ino.bin .
python3 espota.py -r -i 192.168.0.240 -p 3232 -P 3233 -f TDisplayS3-STM8S-Flasher.ino.bin
```

`-P 3233` pins the port the jig connects back to (the one the firewall rules
open). If Tailscale causes trouble, add `-I 192.168.0.200` to bind to the LAN address.

### Errors

| Message | Cause |
|---|---|
| `No response from the ESP` (after dots) | Invitation (UDP 3232) never answered: wrong IP, or jig not on WiFi. |
| `No response from device` | Jig accepted the invitation but couldn't connect back: NAT mode or firewall (steps 2 and 3). |

## Viewing the jig from hendrix

An ssh reverse tunnel from the shop exposes the jig's web page on hendrix.
Port 8080 on hendrix is taken by Open WebUI, so use 18080:

```bash
ssh -N -R 18080:192.168.0.240:80 holla@hendrix
```

Then http://localhost:18080/ on hendrix is the jig. ArduinoOTA can't go through
this tunnel (UDP invitation and a callback connection), but the web page's
firmware update can.
