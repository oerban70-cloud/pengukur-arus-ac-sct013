/*
  Pengukur Arus AC Rumah dengan SCT-013 + LCD I2C
  =================================================
  Alat ini mengukur arus AC yang mengalir di satu kabel rumah (mis.
  kabel ke kulkas atau AC) dan menampilkannya di LCD 16x2, lengkap
  dengan estimasi daya (arus x 220 volt). Hasilnya juga tampil di
  Serial Monitor (115200 baud).

  Cara kerja:
  1. Sensor SCT-013-000 adalah trafo arus (current transformer) yang
     dijepit di SATU kabel AC — tanpa memotong kabel. Arus primer
     100 ampere menghasilkan arus sekunder 50 miliampere (rasio
     2000 lilitan).
  2. Arus sekunder mengalir lewat resistor beban 33 ohm, sehingga
     muncul tegangan AC kecil di pin ADC ESP32.
  3. Pembagi tegangan 2 x resistor 10k dari 3V3 ke GND memberi bias
     1,65 volt (titik tengah), supaya sinyal AC yang naik-turun di
     sekitar nol bisa dibaca ADC yang hanya menerima 0-3,3 volt.
  4. ESP32 membaca pin analog ribuan kali selama 100 milidetik
     (5 siklus listrik 50 Hz), menghitung nilai RMS-nya, lalu
     mengubahnya menjadi arus dalam ampere.
  5. Arus tampil di LCD tiap 0,5 detik, dirata-rata dari 5
     pengukuran terakhir supaya angkanya stabil. Daya dihitung
     sebagai arus x 220 volt.

  !!! PERINGATAN KESELAMATAN — BACA SEBELUM MERANGKAI !!!
  - SCT-013 adalah sensor clamp NON-INVASIF: jepit clamp HANYA di
    SATU kabel (kabel fase L saja). JANGAN PERNAH menyentuh kabel
    PLN telanjang dan JANGAN membuka atau memasang clamp sambil
    memegang kabel bertegangan.
  - JANGAN jepit DUA kabel sekaligus (fase + netral): medan magnet
    kedua kabel saling menghapus dan hasilnya nol — alat tidak
    mengukur apa pun.
  - JANGAN PERNAH membiarkan output SCT-013 terbuka tanpa resistor
    beban 33 ohm saat clamp terpasang di kabel berarus: trafo arus
    tanpa beban bisa menghasilkan tegangan tinggi di terminalnya.
  - Proyek ini HANYA untuk yang sudah paham listrik AC. Kalau ragu,
    minta bantuan teknisi listrik.

  Kalibrasi (WAJIB sekali di awal):
  1. Jepit clamp di kabel alat yang arusnya diketahui (mis. setrika
     1000 watt = sekitar 4,55 ampere di 220 volt), atau ukur dengan
     tang ampere.
  2. Lihat angka di LCD (mis. tampil 4,10 ampere).
  3. FAKTOR_KALIBRASI baru = FAKTOR_KALIBRASI lama x 4,55 / 4,10.
  4. Ubah FAKTOR_KALIBRASI di bawah, upload ulang, cek lagi sampai
     angka LCD sama dengan alat ukur pembanding (selisih di bawah
     0,1 ampere).

  Jangkauan: SCT-013-000 + beban 33 ohm akurat sampai sekitar 30
  ampere untuk pemakaian rumah biasa (6600 watt di 220 volt);
  secara teori sinyal baru mentok di sekitar 70 ampere.

  Video penjelasan (60 detik): link menyusul setelah upload
  Repo: https://github.com/oerban70-cloud/pengukur-arus-ac-sct013
*/

// ---------- Konfigurasi pin ----------
const uint8_t PIN_SCT_OUT = 34;  // output SCT-013 via beban 33 ohm (ADC1, input-only)
const uint8_t PIN_LCD_SDA  = 21;  // SDA bus I2C LCD (default ESP32)
const uint8_t PIN_LCD_SCL  = 22;  // SCL bus I2C LCD (default ESP32)

// ---------- Konfigurasi sensor ----------
const float   FAKTOR_KALIBRASI      = 60.606;  // ampere per 1 volt RMS di ADC (= 2000 lilitan / 33 ohm beban)
const uint8_t RESOLUSI_ADC_BIT      = 12;      // ADC ESP32 12 bit (0-4095)
const float   TEGANGAN_ADC_MAKS_V   = 3.3;     // ADC 11 dB: rentang 0-3,3 volt
const float   TEGANGAN_PLN_V        = 220.0;   // tegangan PLN nominal (untuk estimasi daya)
const unsigned long JENDELA_SAMPEL_MS = 100;   // 100 ms = 5 siklus listrik 50 Hz

// ---------- Konfigurasi tampilan ----------
const unsigned long INTERVAL_TAMPIL_MS = 500;  // LCD + Serial diperbarui tiap 0,5 detik
const uint8_t JUMLAH_RATA_RATA = 5;      // rata-rata 5 pengukuran terakhir (angka stabil)
const float   ARUS_BEBAN_BESAR_A = 16.0;  // di atas ini LCD menampilkan "BEBAN BESAR" (MCB rumah umum 16 A)
const unsigned long BAUD_SERIAL = 115200;

// ---------- Library ----------
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);  // LCD 16x2, alamat I2C 0x27

// ---------- Buffer rata-rata ----------
float riwayatArusA[JUMLAH_RATA_RATA] = {0, 0, 0, 0, 0};
uint8_t indeksRiwayat = 0;
uint8_t jumlahRiwayat = 0;
unsigned long waktuTampilTerakhirMs = 0;

// ---------- Baca arus RMS dari SCT-013 ----------
// Membaca pin analog selama JENDELA_SAMPEL_MS, menghitung rata-rata
// (titik tengah sinyal AC, sekitar 1,65 volt dari pembagi bias) lalu
// simpangan RMS terhadap rata-rata itu. Mengembalikan arus primer
// dalam ampere (sudah dikali FAKTOR_KALIBRASI).
// Mengisi jumlahSampelJenuh dengan banyaknya sampel yang mentok di 0
// atau 4095 (tanda arus terlalu besar untuk beban 33 ohm).
float bacaArusRmsA(unsigned long &jumlahSampelJenuh) {
  const uint16_t adcMaksimum = (1 << RESOLUSI_ADC_BIT) - 1;  // 4095
  unsigned long jumlahSampel = 0;
  unsigned long jumlahNilaiAdc = 0;
  jumlahSampelJenuh = 0;

  // Tahap 1: kumpulkan sampel selama satu jendela (100 ms).
  // Simpan jumlah kuadratnya nanti di tahap 2 — karena rata-rata
  // (bias DC) baru diketahui setelah semua sampel terkumpul, kita
  // simpan dulu sampel mentahnya di buffer kecil.
  const uint16_t KAPASITAS_BUFFER = 600;  // 100 ms / ~0,16 ms per sampel
  uint16_t bufferSampel[KAPASITAS_BUFFER];
  unsigned long waktuMulaiMs = millis();
  while (millis() - waktuMulaiMs < JENDELA_SAMPEL_MS) {
    if (jumlahSampel >= KAPASITAS_BUFFER) {
      break;
    }
    uint16_t nilaiAdc = analogRead(PIN_SCT_OUT);
    bufferSampel[jumlahSampel] = nilaiAdc;
    jumlahNilaiAdc += nilaiAdc;
    if (nilaiAdc == 0 || nilaiAdc == adcMaksimum) {
      jumlahSampelJenuh++;
    }
    jumlahSampel++;
  }

  // Tahap 2: rata-rata = titik tengah sinyal AC (sekitar 1,65 volt).
  float rataRataAdc = (float)jumlahNilaiAdc / (float)jumlahSampel;

  // Tahap 3: RMS = akar rata-rata kuadrat simpangan dari titik tengah.
  double jumlahKuadrat = 0.0;
  for (unsigned long i = 0; i < jumlahSampel; i++) {
    float simpangan = (float)bufferSampel[i] - rataRataAdc;
    jumlahKuadrat += (double)(simpangan * simpangan);
  }
  float rmsAdc = sqrt(jumlahKuadrat / (double)jumlahSampel);

  // Tahap 4: ubah hitungan ADC menjadi volt RMS, lalu kali faktor
  // kalibrasi menjadi ampere (faktor = 2000 lilitan / 33 ohm beban).
  float rmsVoltAdc = rmsAdc * TEGANGAN_ADC_MAKS_V / (float)adcMaksimum;
  return rmsVoltAdc * FAKTOR_KALIBRASI;
}

// ---------- Tampilkan arus + daya di LCD ----------
// Baris 1: "Arus 2.35 A" (normal) atau "BEBAN BESAR!" (di atas 16 A).
// Baris 2: "Daya 517 W" (arus x 220 volt).
void tampilkanLcd(float arusA) {
  float dayaW = arusA * TEGANGAN_PLN_V;
  lcd.setCursor(0, 0);
  if (arusA >= ARUS_BEBAN_BESAR_A) {
    lcd.print("BEBAN BESAR!    ");
  } else {
    lcd.print("Arus ");
    lcd.print(arusA, 2);
    lcd.print(" A    ");
  }
  lcd.setCursor(0, 1);
  lcd.print("Daya ");
  lcd.print((int)(dayaW + 0.5));
  lcd.print(" W      ");
}

// ---------- Tampilkan arus + daya di Serial Monitor ----------
void tampilkanSerial(float arusA, unsigned long jumlahSampelJenuh) {
  float dayaW = arusA * TEGANGAN_PLN_V;
  Serial.print("Arus AC: ");
  Serial.print(arusA, 2);
  Serial.print(" A  |  Daya: ");
  Serial.print((int)(dayaW + 0.5));
  Serial.print(" W");
  if (arusA >= ARUS_BEBAN_BESAR_A) {
    Serial.print("  <-- BEBAN BESAR (di atas 16 A)");
  }
  if (jumlahSampelJenuh > 0) {
    Serial.print("  [ADC jenuh: ");
    Serial.print(jumlahSampelJenuh);
    Serial.print(" sampel — arus melebihi jangkauan beban 33 ohm]");
  }
  Serial.println();
}

void setup() {
  Serial.begin(BAUD_SERIAL);

  // ADC: rentang penuh 0-3,3 volt di pin SCT-013.
  analogSetPinAttenuation(PIN_SCT_OUT, ADC_11db);
  analogReadResolution(RESOLUSI_ADC_BIT);

  // LCD I2C di pin SDA/SCL standar ESP32.
  Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Arus AC SCT-013");
  lcd.setCursor(0, 1);
  lcd.print("mengukur...");

  Serial.println("=== PENGUKUR ARUS AC SCT-013 ===");
  Serial.println("Sensor SCT-013 -> GPIO34, LCD I2C 0x27");
  Serial.println("AWAS: jepit clamp HANYA di SATU kabel, jangan sentuh kabel PLN telanjang!");
}

void loop() {
  unsigned long sekarangMs = millis();
  if (sekarangMs - waktuTampilTerakhirMs < INTERVAL_TAMPIL_MS) {
    return;
  }
  waktuTampilTerakhirMs = sekarangMs;

  // Ukur arus RMS sekali (100 ms), lalu masukkan ke rata-rata.
  unsigned long jumlahSampelJenuh = 0;
  float arusBaruA = bacaArusRmsA(jumlahSampelJenuh);
  riwayatArusA[indeksRiwayat] = arusBaruA;
  indeksRiwayat = (indeksRiwayat + 1) % JUMLAH_RATA_RATA;
  if (jumlahRiwayat < JUMLAH_RATA_RATA) {
    jumlahRiwayat++;
  }

  float jumlahArusA = 0.0;
  for (uint8_t i = 0; i < jumlahRiwayat; i++) {
    jumlahArusA += riwayatArusA[i];
  }
  float arusRataA = jumlahArusA / (float)jumlahRiwayat;

  tampilkanLcd(arusRataA);
  tampilkanSerial(arusRataA, jumlahSampelJenuh);
}
