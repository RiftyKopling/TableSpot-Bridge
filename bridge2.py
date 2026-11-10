import json
import time
import paho.mqtt.client as mqtt
import firebase_admin
from firebase_admin import credentials, db

# ============ KONFIG ============
MQTT_BROKER  = "10.60.123.7"
MQTT_PORT    = 1883
TOPIC_SENSOR = "esp32/sensor"

FIREBASE_CRED = "serviceAccount2.json"
FIREBASE_URL  = "https://tablespot-f67fe-default-rtdb.asia-southeast1.firebasedatabase.app/"  # GANTI!

# Mapping indeks array dari ESP32 → ID dashboard
KURSI_KE_TABLE = {
    0: ("K1", "M1"),  # kursi indeks 0 → K1 di meja M1
    1: ("K2", "M1"),
    2: ("K3", "M2"),
    3: ("K4", "M2"),
    4: ("K5", "M3"),
    5: ("K6", "M4"),
    6: ("K7", "M5"),
}
MEJA_IDS = ["M1", "M2", "M3", "M4", "M5"]

# ============ FIREBASE ============
cred = credentials.Certificate(FIREBASE_CRED)
firebase_admin.initialize_app(cred, {"databaseURL": FIREBASE_URL})
ref_seats   = db.reference("seats")
ref_tables  = db.reference("tables")
ref_beat    = db.reference("system/beat")

# ============ STATE INTERNAL ============
# Simpan status terakhir untuk deteksi "since" (kapan berubah)
last_seat_status  = {}   # { "K1": "available", ... }
last_table_status = {}   # { "M1": "available", ... }
last_beat_time    = 0    # untuk throttle update beat (jangan spam)

TIMEOUT_OFFLINE = 30     # detik, kalau ESP32 diam >30s → offline

# ============ FUNGSI ============
def status_to_label(kosong_val):
    # 1 = available (kosong), 0 = occupied (terisi), -1 = offline (error)
    if kosong_val == -1:
        return "offline"
    return "available" if kosong_val == 1 else "occupied"

def update_seats(kursi_arr, ts_now):
    for idx, (k_id, m_id) in KURSI_KE_TABLE.items():
        if idx >= len(kursi_arr): continue
        
        # Ambil nilai langsung (1, 0, atau -1)
        nilai_sensor = kursi_arr[idx] 
        new_status = status_to_label(nilai_sensor)
        old_status = last_seat_status.get(k_id)

        if old_status != new_status:
            ref_seats.child(k_id).update({
                "table":  m_id,
                "status": new_status,
                "since":  ts_now,
            })
            last_seat_status[k_id] = new_status
            log_perubahan_kursi(k_id, m_id, new_status)

def update_tables(meja_arr, ts_now):
    for idx, m_id in enumerate(MEJA_IDS):
        if idx >= len(meja_arr): continue
        
        nilai_meja = meja_arr[idx]
        new_status = status_to_label(nilai_meja)
        old_status = last_table_status.get(m_id)

        if old_status != new_status:
            ref_tables.child(m_id).update({
                "status": new_status,
                "since":  ts_now,
            })
            last_table_status[m_id] = new_status

def init_all_offline():
    """Saat bridge start, tandai semua kursi/meja 'offline' dulu."""
    ts = int(time.time() * 1000)
    for _idx, (k_id, m_id) in KURSI_KE_TABLE.items():
        ref_seats.child(k_id).set({
            "table":  m_id,
            "status": "offline",
            "since":  ts,
        })
    for m_id in MEJA_IDS:
        ref_tables.child(m_id).set({
            "status": "offline",
            "since":  ts,
        })
    print("[INIT] Semua kursi/meja ditandai offline")

# ============ MQTT ============
def on_connect(client, userdata, flags, rc):
    print(f"[MQTT] connected rc={rc}")
    client.subscribe(TOPIC_SENSOR)

def on_message(client, userdata, msg):
    global last_beat_time

    try:
        data = json.loads(msg.payload.decode())
    except Exception as e:
        print("[MQTT] bad payload:", e); return

    kursi_arr = data.get("kursi", [])   # [1,0,1,0,1,0,1]
    meja_arr  = data.get("meja", [])    # [1,0,1,1,0]
    ts_now    = int(time.time() * 1000) # epoch ms

    print(f"[ESP32] kursi={kursi_arr} meja={meja_arr}")

    # Update kursi & meja
    update_seats(kursi_arr, ts_now)
    update_tables(meja_arr, ts_now)

    # Update beat (throttle: maks 1x/detik)
    if ts_now - last_beat_time > 1000:
        ref_beat.set(ts_now)
        last_beat_time = ts_now

# ============ FITUR STATISTIK (LOGGER) ============
import datetime
ref_history = db.reference("history")

def log_perubahan_kursi(k_id, m_id, status_baru):
    """
    Catat perubahan status ke /history/{YYYY-MM-DD}/{k_id}_{timestamp}
    Hanya catat saat berubah (bukan setiap detik).
    """
    if status_baru == "offline":
        return # Abaikan offline untuk statistik pemakaian

    now = datetime.datetime.now()
    tgl = now.strftime("%Y-%m-%d")
    ts = int(now.timestamp() * 1000)
    
    # Simpan log
    ref_history.child(tgl).push({
        "tipe": "kursi",
        "id": k_id,
        "meja": m_id,
        "status": status_baru,
        "ts": ts
    })
    print(f"[LOG] {k_id} -> {status_baru}")

mqtt_client = mqtt.Client(client_id="python-bridge")
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
mqtt_client.loop_start()

# ============ STARTUP ============
init_all_offline()

try:
    print("[BRIDGE] running... Ctrl+C stop")
    last_msg_time = time.time()

    # Loop untuk deteksi ESP32 offline
    while True:
        time.sleep(1)
        # Kalau ESP32 diam > TIMEOUT_OFFLINE detik → tandai offline
        if last_beat_time and (int(time.time() * 1000) - last_beat_time > TIMEOUT_OFFLINE * 1000):
            # Sudah offline — hindari spam set()
            if last_beat_time != -1:
                ts = int(time.time() * 1000)
                for _idx, (k_id, m_id) in KURSI_KE_TABLE.items():
                    if last_seat_status.get(k_id) != "offline":
                        ref_seats.child(k_id).update({"status": "offline", "since": ts})
                        last_seat_status[k_id] = "offline"
                for m_id in MEJA_IDS:
                    if last_table_status.get(m_id) != "offline":
                        ref_tables.child(m_id).update({"status": "offline", "since": ts})
                        last_table_status[m_id] = "offline"
                print("[WARN] ESP32 offline (timeout)")
                last_beat_time = -1  # tandai supaya tidak spam

except KeyboardInterrupt:
    mqtt_client.loop_stop()
    print("\n[BRIDGE] stopped")