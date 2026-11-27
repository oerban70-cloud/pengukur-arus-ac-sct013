# Pengukur Arus AC Rumah — ESP32 + SCT-013 + LCD I2C

Kulkas atau AC, mana yang paling rakus listrik di rumahmu? Alat mungil
ini mengukur arus AC yang mengalir di satu kabel rumah dan
menampilkannya di LCD — lengkap dengan estimasi daya dalam watt, supaya
kamu tahu angka pastinya, bukan cuma menebak alat mana yang bikin
tagihan membengkak. Sensor SCT-013 dijepit di satu kabel saja, tanpa
memotong kabel apa pun. ESP32 membaca sensor ribuan kali, menghitung
arus RMS-nya, lalu menampilkan arus dan daya di LCD tiap setengah detik.

Video penjelasan (60 detik):
link menyusul setelah upload

## PERINGATAN KESELAMATAN — BACA DULU

SCT-013 adalah sensor clamp NON-INVASIF — cara pakainya aman, tapi
kabel yang diukur tetap kabel PLN bertegangan. Patuhi semua poin ini:

- Jepit clamp HANYA di SATU kabel (kabel fase L saja).
- JANGAN PERNAH menyentuh kabel PLN telanjang dan jangan membuka atau
  memasang clamp sambil memegang kabel bertegangan.
- JANGAN jepit DUA kabel sekaligus (fase + netral): medan magnet kedua
  kabel saling menghapus dan hasilnya nol — alat tidak mengukur apa pun.
- JANGAN PERNAH membiarkan output SCT-013 terbuka tanpa resistor beban
  33 ohm saat clamp terpasang di kabel berarus: trafo arus tanpa beban
  bisa menghasilkan tegangan tinggi di terminalnya.
- Proyek ini HANYA untuk yang sudah paham listrik AC. Kalau ragu, minta
  bantuan teknisi listrik.

## Komponen

| Bagian | Keterangan |
|---|---|
| ESP32 DevKit | Otak alat, membaca ADC + menghitung RMS |
| SCT-013-000 | Sensor arus AC clamp, 100 A : 50 mA (2000 lilitan) |
| Resistor 33 ohm | Resistor beban di output SCT-013 (jangan dilepas) |
| Resistor 10k x 2 | Pembagi tegangan, memberi bias 1,65 V |
| LCD 16x2 I2C (0x27) | Menampilkan arus + daya, alamat I2C 0x27 |
| Adaptor USB 5V 2A | Catu daya ESP32 + LCD |
| Kabel PLN (L) | Satu kabel fase yang dijepit clamp |

## Wiring

| Dari | Ke |
|---|---|
| Kabel PLN fase (L) | Dijepit clamp SCT-013 (kabel lewat di dalam rahang) |
| SCT-013 OUT | ESP32 GPIO34 (ADC) via resistor beban 33 ohm |
| SCT-013 GND | GND |
| Pembagi bias 2x 10k | 3V3 ESP32 - 10k - node OUT - 10k - GND |
| LCD SDA | ESP32 GPIO21 |
| LCD SCL | ESP32 GPIO22 |
| LCD VCC | ESP32 VIN 5V |
| LCD GND | GND |
| Adaptor USB 5V | ESP32 VIN |
| Semua GND | Digabung |

Skema lengkap: [schematic.png](schematic.png)

Catatan: node OUT (pertemuan SCT-013, beban 33 ohm, dan pembagi bias)
berada di sekitar 1,65 volt saat tidak ada arus — sinyal AC naik-turun
di sekitar titik tengah itu, dan ESP32 menghitung RMS dari simpangannya.

## Cara kerja

1. **Jepit** — clamp SCT-013 dijepit di satu kabel fase. Arus AC di
   kabel menimbulkan arus sekunder di SCT-013: 100 ampere primer
   menjadi 50 miliampere sekunder (rasio 2000 lilitan).
2. **Ubah jadi tegangan** — arus sekunder mengalir lewat resistor
   beban 33 ohm, sehingga muncul tegangan AC kecil di pin ADC ESP32.
   Pembagi 2x 10k memberi bias 1,65 volt supaya sinyal yang naik-turun
   di sekitar nol bisa dibaca ADC yang hanya menerima 0–3,3 volt.
3. **Baca sinyal** — ESP32 membaca pin GPIO34 ribuan kali selama
   100 milidetik (5 siklus listrik 50 Hz).
4. **Hitung RMS** — dari semua sampel dihitung rata-rata (titik tengah
   sinyal AC, sekitar 1,65 volt), lalu akar rata-rata kuadrat
   simpangannya. Hasilnya adalah tegangan RMS di sisi ADC.
5. **Kalibrasi** — nilai RMS ADC dikali `FAKTOR_KALIBRASI` (2000
   lilitan / 33 ohm = 60,606) menjadi arus primer dalam ampere.
6. **Rata-rata tampil** — 5 pengukuran terakhir dirata-rata supaya
   angka di LCD stabil, lalu LCD + Serial diperbarui tiap 0,5 detik.
   Daya dihitung sebagai arus x 220 volt.
7. **Peringatan** — kalau arus di atas 16 ampere (batas MCB rumah
   umum), baris pertama LCD berubah menjadi "BEBAN BESAR!" dan Serial
   Monitor ikut memberi peringatan. Kalau banyak sampel ADC mentok di
   0 atau 4095, Serial memberi tahu bahwa arus melebihi jangkauan
   beban 33 ohm.

Contoh angka: arus 2,35 ampere berarti daya sekitar 517 watt
(2,35 x 220 volt). Arus rumah biasa jarang lewat 30 ampere
(6600 watt di 220 volt); sensor ini akurat sampai sekitar 30 ampere,
dan sinyal baru mentok di sekitar 70 ampere.

## Kalibrasi (wajib sekali di awal)

Angka faktor kalibrasi bawaan (`FAKTOR_KALIBRASI = 60.606`) adalah
perkiraan teori — tiap sensor dan resistor beban sedikit berbeda.

1. Jepit clamp di kabel alat yang arusnya diketahui (mis. setrika
   1000 watt = sekitar 4,55 ampere di 220 volt), atau ukur dengan
   tang ampere.
2. Lihat angka di LCD (mis. tampil 4,10 ampere).
3. Faktor baru = faktor lama x 4,55 / 4,10.
4. Ubah `FAKTOR_KALIBRASI` di sketch, upload ulang, cek lagi sampai
   angka LCD sama dengan alat ukur pembanding (selisih di bawah
   0,1 ampere).

Kalau angka LCD 0 ampere padahal alat menyala, periksa: clamp
benar-benar dijepit di SATU kabel saja (bukan dua kabel), kabel OUT
ke GPIO34 tersambung, dan beban 33 ohm terpasang.

## Konfigurasi di sketch

| Konstanta | Nilai bawaan | Arti |
|---|---|---|
| `PIN_SCT_OUT` | 34 | Output SCT-013 via beban 33 ohm (ADC1) |
| `PIN_LCD_SDA` | 21 | SDA bus I2C LCD |
| `PIN_LCD_SCL` | 22 | SCL bus I2C LCD |
| `FAKTOR_KALIBRASI` | 60.606 | Ampere per 1 volt RMS di ADC (2000 / 33) |
| `RESOLUSI_ADC_BIT` | 12 | ADC ESP32 12 bit (0-4095) |
| `TEGANGAN_ADC_MAKS_V` | 3.3 | Rentang ADC 0-3,3 volt |
| `TEGANGAN_PLN_V` | 220.0 | Tegangan PLN nominal (untuk estimasi daya) |
| `JENDELA_SAMPEL_MS` | 100 | Jendela baca (5 siklus 50 Hz) |
| `INTERVAL_TAMPIL_MS` | 500 | LCD + Serial diperbarui tiap 0,5 detik |
| `JUMLAH_RATA_RATA` | 5 | Pengukuran yang dirata-rata |
| `ARUS_BEBAN_BESAR_A` | 16.0 | Ambang peringatan "BEBAN BESAR!" |
| `BAUD_SERIAL` | 115200 | Kecepatan Serial Monitor |

## Compile (terverifikasi)

- Board: ESP32 DevKit (esp32:esp32:esp32), ESP32 core 4.0.0, arduino-cli 1.6.0
- Sketch: 325.713 byte (24%) dari program storage; RAM 24.728 byte (7%)
- Library: LiquidCrystal_I2C 1.1.2 (lihat folder `libraries/`)

## Catatan

- SCT-013-000 (tanpa beban internal) yang dipakai di sini — resistor
  beban 33 ohm WAJIB terpasang. Varian SCT-013-030 sudah punya beban
  internal dan keluarannya 1 volt — jangan pakai resistor 33 ohm
  tambahan untuk varian itu, dan faktor kalibrasinya berbeda.
- GPIO34 adalah pin input-only ADC1 — tidak bisa dipakai sebagai
  output, tapi memang hanya dipakai untuk membaca sensor.
- LCD I2C 16x2 umumnya beralamat 0x27; kalau LCD tidak menampilkan
  apa-apa, cek alamatnya dengan I2C scanner (ada yang 0x3F).
- Angka di LCD adalah arus RMS hasil hitung, bukan arus puncak — jadi
  langsung bisa dibandingkan dengan tang ampere.
- Daya yang tampil adalah estimasi (arus x 220 volt, menganggap
  tegangan PLN tepat 220 volt dan beban resistif). Untuk daya yang
  akurat, ukur juga tegangan PLN saat itu.
