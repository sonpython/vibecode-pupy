---
phase: 7
title: "Deep sleep and button wake"
status: pending
priority: P1
effort: "1d"
dependencies: [6]
---

# Phase 07: Deep Sleep + Button Wake

## Overview
Add real ESP32-S3 deep sleep + GPIO/LP-core wake on button press. Cache WiFi BSSID/channel in RTC slow memory for sub-second reconnect. Measure actual deep-sleep current with USB power meter; iterate hardware/software until <100µA. End-state: button-press → wake → fetch → display → sleep loop, battery measurable in weeks.

## Context Links
- Phase 06 (UI complete)
- Phase 01 (button GPIO confirmed, RTC-capable verified)
- Brainstorm §5.4 power target

## Requirements
### Functional
- After 30s IDLE post-render → enter deep sleep
- Wake source: button GPIO (level trigger via `esp_sleep_enable_ext1_wakeup` or LP-core wake)
- On wake from deep sleep: detect cause (button vs first-boot vs reset), branch accordingly
- Re-connect WiFi in <2s using cached BSSID + channel (RTC slow mem)

### Non-functional
- Deep-sleep current ≤ 100µA (target; stretch ≤ 50µA)
- Wake-to-fetch latency ≤ 1.5s
- Wake-to-render latency ≤ 3s including HTTPS

## Architecture

### RTC slow memory cache layout
```cpp
RTC_DATA_ATTR struct {
    uint32_t  magic;          // 0xABCDEF01 → valid cache
    uint8_t   wifi_bssid[6];
    uint8_t   wifi_channel;
    uint8_t   wifi_auth_mode;
    uint32_t  last_render_ts; // for "refreshed Xs ago" if data is fresh enough to skip fetch
} rtc_cache;
```

### Wake flow
```
power button press
  ↓ (LP core / EXT1 wake)
boot ROM → app starts
  ↓
read rtc_cache → fast WiFi connect (BSSID + channel)
  ↓
HTTPS GET /status
  ↓
render UI (reuse render path from Phase 06)
  ↓
30s timer (or button = explicit re-sleep)
  ↓
save rtc_cache, esp_deep_sleep_start()
```

## Related Code Files
- Create: `firmware/main/power/sleep_manager.cc` + `.h`
- Modify: `firmware/main/application.cc` (integrate sleep manager into state machine)
- Modify: `firmware/main/net/wifi_fastconnect.cc` (use cached BSSID/channel)
- Create: `firmware/main/power/rtc_cache.h` (RTC_DATA_ATTR struct)

## Implementation Steps

1. **Identify wake GPIO** (from Phase 01 report). Confirm it is an RTC GPIO (ESP32-S3: GPIO0-21).

2. **Configure wake source** (in `sleep_manager.cc`):
   ```cpp
   esp_sleep_enable_ext1_wakeup_io(
       (1ULL << BUTTON_WAKE_GPIO),
       ESP_EXT1_WAKEUP_ANY_LOW   // button is active-low
   );
   ```

3. **RTC cache write** before sleep:
   ```cpp
   void save_rtc_cache(const wifi_ap_record_t& ap) {
       rtc_cache.magic = 0xABCDEF01;
       memcpy(rtc_cache.wifi_bssid, ap.bssid, 6);
       rtc_cache.wifi_channel = ap.primary;
       rtc_cache.last_render_ts = (uint32_t)time(nullptr);
   }
   ```

4. **Fast WiFi reconnect**:
   ```cpp
   wifi_config_t cfg = {/* SSID + passwd from NVS */};
   if (rtc_cache.magic == 0xABCDEF01) {
       memcpy(cfg.sta.bssid, rtc_cache.wifi_bssid, 6);
       cfg.sta.bssid_set = true;
       cfg.sta.channel   = rtc_cache.wifi_channel;
   }
   esp_wifi_set_config(WIFI_IF_STA, &cfg);
   esp_wifi_connect();
   ```
   Expect: ~800ms on warm cache vs. ~3-5s cold scan.

5. **Sleep-entry**:
   ```cpp
   void enter_deep_sleep() {
       display_->Off();          // turn off backlight + display
       wifi_stop();
       save_rtc_cache(current_ap);
       esp_deep_sleep_start();   // no return
   }
   ```

6. **Boot-cause branching** in `app_main`:
   ```cpp
   switch (esp_sleep_get_wakeup_cause()) {
       case ESP_SLEEP_WAKEUP_EXT1:  goto wake_from_button;
       case ESP_SLEEP_WAKEUP_UNDEFINED: goto cold_boot;
       default: goto cold_boot;
   }
   ```

7. **Idle-timer** in `application.cc`: 30s timer arm after render → cancel on button → on expiry call `enter_deep_sleep()`.

8. **µA measurement loop**:
   - Use USB-C power meter (cheap ~$20)
   - Boot → render → sleep
   - Observe current. If >100µA:
     - Check display backlight fully off
     - Check WiFi stopped (not just disconnected)
     - Identify any external LED / status indicator still powered
     - Check for `RTC_PERIPH` not gated → call `esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF)`
     - If hardware-side leakage (LDO Iq, charging IC pull-ups) → document as "best we can do"

9. **Battery life calc + write to plan.md success criteria**: `1200mAh × 0.7(safety) / measured_µA = expected hours`. Convert to days at expected wake count.

## Todo List
- [ ] sleep_manager.cc with wake config + sleep entry
- [ ] rtc_cache.h with persistent struct
- [ ] WiFi fast-connect using cache
- [ ] Boot-cause branching
- [ ] Idle timer triggers sleep
- [ ] µA measurement <100µA confirmed (or root-caused if not)
- [ ] Wake-to-render ≤3s measured

## Success Criteria
- [ ] Single button press: device wakes, fetches, displays in <3s
- [ ] After 30s idle: deep sleep observed (display off, current drops)
- [ ] Long-term: leave overnight in sleep, battery drop <2% (rough estimate)
- [ ] Cold boot vs. button-wake produce same final UI

## Risk Assessment
| Risk | Mitigation |
|---|---|
| Board has parasitic always-on regulator → µA stuck high | Document baseline; may require hardware mod (cut trace) — out of MVP scope |
| Display backlight stays on in sleep | Confirm backlight is GPIO-controlled (not always-on); explicit OFF before sleep |
| WiFi cache invalid (router rebooted, channel changed) | On connect timeout: clear cache + full scan retry. Add 5s budget. |
| Button bounce wakes multiple times | Software debounce 50ms on wake-cause check |
| Charging IC adds 100s of µA | Test with USB unplugged; on-USB current is irrelevant for battery life |

## Security Considerations
- RTC memory survives deep sleep but is wiped on power-off; OK to store BSSID (not creds) there
- Long sleep + cached BSSID: if attacker spoofs SSID/BSSID on same channel, device may connect — accept this risk for personal LAN

## Next Steps
→ Phase 08 (final hardening + cloudflared exposure) is independent.
