# Vibecode Usage Monitor Firmware

Minimal ESP-IDF firmware for the Xiaozhi ESP32-S3 `sp-esp32-s3-1.54-muma` board.

It connects to WiFi, fetches `https://vibecode.sonpython.com/status`, and draws Claude/Codex usage bars on the 240x240 ST7789 display.

Secrets are local-only:

- `firmware/secrets/wifi.env`
- `usage-api/.env`
- generated `firmware/usage-monitor/main/secrets.h`

Generate `secrets.h` and build:

```bash
./prepare_secrets.sh
export IDF_PATH=$HOME/esp/esp-idf-v5.5
export IDF_TOOLS_PATH=$HOME/.espressif
. $HOME/.espressif/python_env/idf5.5_py3.13_env/bin/activate
eval "$($IDF_PATH/tools/idf_tools.py export --format shell)"
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem83101 flash monitor
```
