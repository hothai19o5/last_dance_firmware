# ESP32 Health Monitor - BLE-Only Architecture

## Overview
This firmware now operates in **BLE-only mode**, eliminating WiFi and MQTT dependencies. The mobile app acts as the sole gateway for data synchronization to the backend server.

## Architecture Changes

### Previous (Dual-mode)
```
ESP32 ──BLE──> Mobile App ──HTTPS──> Backend
      └─WiFi──> MQTT ──────────────> Backend
```

### Current (BLE-only)
```
ESP32 ──BLE──> Mobile App ──HTTPS──> Backend
```

## Benefits

### 1. **Power Savings**
- **No WiFi radio**: Saves ~120-200mA during active transmission
- **No MQTT keepalive**: Eliminates periodic network traffic
- **No NTP sync**: Removes time synchronization overhead
- **BLE-only consumption**: Typically 10-50mA vs 150-300mA for WiFi/MQTT

**Expected battery life improvement: 3-5x**

### 2. **Simplified Firmware**
- Removed dependencies: `WiFiManager`, `MQTTClientManager`
- No TLS handshake complexity
- No network reconnection logic
- Faster boot time (no WiFi connection wait)

### 3. **Better Reliability**
- BLE has much better reliability at close range (<10m)
- No dependency on WiFi network availability
- Simpler error recovery (just BLE reconnect)

## Code Changes Summary

### Disabled Components
```cpp
// #include "wifi_manager.h"   // Disabled
// #include "mqtt_client.h"    // Disabled
// WiFiManager wifiManager;    // Disabled
// MQTTClientManager mqttManager; // Disabled
```

### Modified Loop
- Removed WiFi connection monitoring
- Removed MQTT maintain/loop calls
- Removed MQTT publish calls
- All data now flows through BLE notifications

### Time Display
- Changed from NTP time to `millis()` counter
- Format: Hours and minutes since device boot
- Mobile app can provide real time via BLE if needed

### Alert Handling
Enhanced BLE JSON payload to include alert score:

**Normal data:**
```json
{
  "hr": 75,
  "spo2": 98,
  "steps": 1523,
  "cal": 142.3,
  "ts": 12345
}
```

**Alert data:**
```json
{
  "hr": 120,
  "spo2": 92,
  "steps": 1523,
  "cal": 142.3,
  "ts": 12345,
  "alert": 0.8547
}
```

## Mobile App Responsibilities

The React Native mobile app now must:

### 1. **Maintain Persistent Connection**
```javascript
// Keep BLE connection alive
device.monitorCharacteristicForService(
  HEALTH_DATA_SERVICE,
  HEALTH_BATCH_CHAR,
  (error, characteristic) => {
    if (characteristic) {
      const data = JSON.parse(base64.decode(characteristic.value));
      handleHealthData(data);
    }
  }
);
```

### 2. **Store Data Locally**
```javascript
// Use AsyncStorage or SQLite for offline buffering
await AsyncStorage.setItem(
  `health_data_${timestamp}`,
  JSON.stringify(bleData)
);
```

### 3. **Sync to Backend**
```javascript
// Batch sync when online
const syncQueue = await getUnsynced Data();
for (const batch of syncQueue) {
  await fetch('https://api.yourdomain.com/api/v1/sync/health-data', {
    method: 'POST',
    headers: {
      'Authorization': `Bearer ${jwt}`,
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      deviceUuid: device.id,
      dataPoints: formatDataPoints(batch)
    })
  });
}
```

### 4. **Handle Alerts**
```javascript
const handleHealthData = (data) => {
  // Check for alert field
  if (data.alert && data.alert > 0.75) {
    // Trigger local notification
    PushNotification.localNotification({
      title: 'Health Alert',
      message: `Abnormal vitals detected! Score: ${data.alert.toFixed(2)}`,
      playSound: true,
      vibrate: true
    });
    
    // Mark as high priority for sync
    saveToPriorityQueue(data);
  } else {
    saveToNormalQueue(data);
  }
};
```

## Serial Monitor Output

### Startup
```
=== ESP32 Health Monitor ===
[BLE] Initializing BLE...
[BLE] BLE initialized and advertising started.
[BLE] Device Name: ESP32-Health-Watch
[MPU6050] Init...
MAX30102 initialized.
[System] Running in BLE-only mode. Mobile app is the gateway.
Setup complete. Reading sensor...
```

### Running
```
[Sensor] HR=75, SPO2=98, IR=105432
[BLE] Client connected!
[BLE] Weight updated: 70 kg
[BLE] BMR calculated: 1680.0 kcal/day
[Main] Sensor data sent to ML queue
[BLE] Notified health data: {"hr":75,"spo2":98,"steps":1523,"cal":142.3,"ts":12345}
```

### Alert Condition
```
[ML] Inference #42: Score=0.8547
[Main] ALERT: Abnormal vitals detected! Score=0.8547, HR=120.0, SPO2=92.0
[BLE] Notified health data WITH ALERT: {"hr":120,"spo2":92,"steps":1650,"cal":158.2,"ts":12567,"alert":0.8547}
```

## Power Consumption Estimates

| Component | WiFi+MQTT Mode | BLE-only Mode | Savings |
|-----------|----------------|---------------|---------|
| **Idle** | 80mA | 20mA | **75%** |
| **Active (sensing)** | 180mA | 45mA | **75%** |
| **Transmit** | 250mA | 50mA | **80%** |
| **Average (typical use)** | 120mA | 35mA | **71%** |

**Battery Life (1000mAh LiPo):**
- WiFi+MQTT mode: ~8 hours
- BLE-only mode: **~28 hours** (3.5x improvement)

## Testing

### 1. Verify BLE Advertising
Use **nRF Connect** (Android) or **LightBlue Explorer** (iOS):
- Device should appear as: `ESP32-Health-Watch`
- Services visible: `0000181C...` and `0000180D...`

### 2. Test Configuration Flow
Write user profile via BLE:
- Weight: 70kg → Characteristic `00002A98...`
- Height: 175cm → Characteristic `00002A8E...`
- Check Serial: `[BLE] BMR calculated: 1680.0 kcal/day`

### 3. Test Data Stream
Subscribe to Health Data Batch (`00002A37...`):
- Should receive JSON every ~1 second
- Verify fields: `hr`, `spo2`, `steps`, `cal`, `ts`

### 4. Test Alert
Simulate abnormal vitals (modify ML threshold if needed):
- Look for `"alert"` field in JSON
- Value should be >0.75 for triggered alerts

## Migration from WiFi/MQTT

If you need to re-enable WiFi/MQTT temporarily:

1. Uncomment includes in `last_dance.ino`:
```cpp
#include "wifi_manager.h"
#include "mqtt_client.h"
```

2. Uncomment global objects:
```cpp
WiFiManager wifiManager;
MQTTClientManager mqttManager;
```

3. Uncomment setup and loop WiFi/MQTT code

4. Flash firmware

## Next Steps

### Optional Enhancements

1. **Add Battery Monitor**
   - Read ADC from battery voltage divider
   - Include in BLE JSON: `"bat": 85`
   - Update OLED display with real percentage

2. **Add Time Sync via BLE**
   - Create a Time Service
   - Mobile app writes current timestamp
   - Use for accurate time display

3. **Implement Data Buffering**
   - Store data in ESP32 flash when BLE disconnected
   - Sync backlog when reconnected
   - Prevents data loss during temporary disconnections

4. **Deep Sleep Mode**
   - Enter deep sleep between measurements
   - Wake every 30-60 seconds
   - Further reduce average power to <10mA

## Support

For BLE integration issues, check:
1. Serial monitor for `[BLE]` prefixed logs
2. Mobile app BLE permissions (Android 12+ needs `BLUETOOTH_CONNECT`)
3. BLE range (<10m for reliable connection)
4. No interference from other BLE devices

---

**Architecture Status:** ✅ BLE-only mode active  
**WiFi/MQTT:** ❌ Disabled  
**Power Optimization:** ✅ Maximum efficiency  
**Mobile Gateway:** ✅ Required for backend sync
