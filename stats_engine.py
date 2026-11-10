import datetime
import time
import firebase_admin
from firebase_admin import credentials, db

# ============ KONFIG ============
FIREBASE_CRED = "serviceAccount2.json"
FIREBASE_URL  = "https://tablespot-f67fe-default-rtdb.asia-southeast1.firebasedatabase.app" 

cred = credentials.Certificate(FIREBASE_CRED)
firebase_admin.initialize_app(cred, {"databaseURL": FIREBASE_URL})

def hitung_statistik():
    now = datetime.datetime.now()
    tgl_hari_ini = now.strftime("%Y-%m-%d")
    # Timestamp saat script ini dijalankan (untuk menghitung yang masih terisi)
    ts_sekarang = int(now.timestamp() * 1000) 
    
    print(f"[STATS] Menghitung statistik untuk {tgl_hari_ini}...")
    
    logs_snap = db.reference(f"history/{tgl_hari_ini}").get()
    if not logs_snap:
        print("[STATS] Tidak ada log hari ini di database.")
        return

    logs = list(logs_snap.values())
    logs.sort(key=lambda x: x.get('ts', 0)) 

    total_durasi_menit = 0
    jumlah_giliran = 0
    durasi_per_meja = {"M1": 0, "M2": 0, "M3": 0, "M4": 0, "M5": 0}
    giliran_per_meja = {"M1": 0, "M2": 0, "M3": 0, "M4": 0, "M5": 0}
    
    waktu_mulai_terisi = {} 
    
    for log in logs:
        k_id = log.get('id')
        m_id = log.get('meja')
        status = log.get('status')
        ts = log.get('ts')
        
        if not k_id or not ts: continue

        if status == 'occupied':
            waktu_mulai_terisi[k_id] = ts
            jumlah_giliran += 1
            giliran_per_meja[m_id] += 1
            
        elif status == 'available' and k_id in waktu_mulai_terisi:
            mulai = waktu_mulai_terisi[k_id]
            selesai = ts
            durasi_menit = (selesai - mulai) / 60000
            
            total_durasi_menit += durasi_menit
            durasi_per_meja[m_id] += durasi_menit
            del waktu_mulai_terisi[k_id]

    # --- PERBAIKAN: Hitung kursi yang MASIH TERISI sampai detik ini ---
    for k_id, mulai in waktu_mulai_terisi.items():
        # Cari meja dari log sebelumnya
        m_id = next((log['meja'] for log in logs if log.get('id') == k_id), "M1")
        durasi_menit = (ts_sekarang - mulai) / 60000
        
        print(f"[STATS] Kursi {k_id} masih terisi (berjalan {round(durasi_menit,1)} menit).")
        total_durasi_menit += durasi_menit
        durasi_per_meja[m_id] += durasi_menit

    # 4. Simpan Ringkasan ke Firebase
    rata_lama = total_durasi_menit / jumlah_giliran if jumlah_giliran > 0 else 0
    
    print(f"[STATS] Total giliran : {jumlah_giliran}")
    print(f"[STATS] Total durasi  : {round(total_durasi_menit, 1)} menit")
    print(f"[STATS] Rata-rata     : {round(rata_lama, 1)} menit")
    
    db.reference("stats/hari_ini").set({
        "giliran": jumlah_giliran,
        "rata_lama_menit": round(rata_lama, 1),
        "puncak_jam": now.strftime("%H") 
    })
    
    # 5. Simpan per meja
    for m_id in ["M1", "M2", "M3", "M4", "M5"]:
        total_dur = durasi_per_meja[m_id]
        giliran_m = giliran_per_meja[m_id]
        
        # Rata-rata per meja: total durasi meja dibagi jumlah giliran di meja itu
        rata_meja = total_dur / giliran_m if giliran_m > 0 else 0
        
        db.reference(f"stats/meja/{m_id}").update({
            "giliran": giliran_m,
            "rata_lama_menit": round(rata_meja, 1),
            "persen_terisi": round(total_dur / 840, 3) # Asumsi 14 jam (840 mnt)
        })

    db.reference("stats/updated").set(ts_sekarang)
    print("[STATS] Selesai dihitung dan dikirim ke Firebase.")

if __name__ == "__main__":
    hitung_statistik()