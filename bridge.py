import json
import paho.mqtt.client as mqtt
import firebase_admin
from firebase_admin import credentials, db

# ============ KONFIG ============
MQTT_BROKER   = "10.60.123.7"
MQTT_PORT     = 1883
TOPIC_SENSOR  = "esp32/sensor"
TOPIC_COMMAND = "esp32/command"

FIREBASE_CRED = "serviceAccount.json"
FIREBASE_URL  = "https://monitor-meja-default-rtdb.asia-southeast1.firebasedatabase.app/"  # GANTI!

# ============ FIREBASE ============
cred = credentials.Certificate(FIREBASE_CRED)
firebase_admin.initialize_app(cred, {"databaseURL": FIREBASE_URL})
ref_status  = db.reference("status")
ref_command = db.reference("command")

# ============ MQTT ============
def on_connect(client, userdata, flags, rc):
    print(f"[MQTT] connected rc={rc}")
    client.subscribe(TOPIC_SENSOR)

def on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode())
    except Exception as e:
        print("[MQTT] bad payload:", e); return

    print(f"[ESP32] kursi={data.get('kursi')} meja={data.get('meja')}")

    ref_status.set({
        "kursi" : data.get("kursi", []),
        "meja"  : data.get("meja",  []),
        "kosong": data.get("kosong", 0),
        "terisi": data.get("terisi", 0),
        "ts"    : data.get("ts", 0),
    })

mqtt_client = mqtt.Client(client_id="python-bridge")
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
mqtt_client.loop_start()

# ============ FIREBASE → MQTT ============
def on_command_change(event):
    if event.data is None:
        return
    payload = json.dumps(event.data)
    print(f"[DASH->ESP] {payload}")
    mqtt_client.publish(TOPIC_COMMAND, payload)

ref_command.listen(on_command_change)

try:
    print("[BRIDGE] running... Ctrl+C stop")
    import time
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    mqtt_client.loop_stop()
    print("\n[BRIDGE] stopped")