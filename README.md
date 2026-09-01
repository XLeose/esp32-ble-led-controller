# ESP32-C6 BLE NimBLE RGB LED & Kesme Kontrolcüsü

Bu proje; **ESP32-C6** mikrodenetleyicisi üzerinde **NimBLE Bluetooth Low Energy (BLE)** yığını kullanılarak geliştirilmiş bir adreslenebilir RGB LED (WS2812 / NeoPixel) kontrolcüsüdür. Akıllı telefon veya herhangi bir BLE istemcisi üzerinden özel GATT karakteristiklerine gönderilen RGB metin komutları ile LED rengi değiştirilebilir ve harici donanım butonu (kesme / interrupt) ile LED anında kapatılabilir.

---

## 📑 İçindekiler
1. [Proje Mimarisi ve Özellikler](#-proje-mimarisi-ve-özellikler)
2. [GATT Servis ve Karakteristik Tablosu](#-gatt-servis-ve-karakteristik-tablosu)
3. [Donanım Bağlantıları ve Pinout](#-donanım-bağlantıları-ve-pinout)
4. [Yazılım Mimarisi (FreeRTOS & NimBLE)](#-yazılım-mimarisi-freertos--nimble)
5. [Derleme ve Yükleme Adımları](#-derleme-ve-yükleme-adımları)
6. [Mobil Uygulama (nRF Connect / LightBlue) ile Test](#-mobil-uygulama-nrf-connect--lightblue-ile-test)

---

## 🚀 Proje Mimarisi ve Özellikler

- **Hafif ve Hızlı Bluetooth Yığını**: ESP-IDF standart Bluedroid yerine çok daha az bellek (RAM) tüketen ve hızlı çalışan **NimBLE** yığını kullanılmıştır.
- **RMT Tabanlı WS2812 Sürücü**: Adreslenebilir LED zamanlama hassasiyeti ESP-IDF RMT (Remote Control) çevre birimiyle donanımsal olarak sağlanır.
- **Kesme (ISR) ve Kuyruk Güvenliği**: Buton tetiklemeleri donanımsal kesme (`GPIO_INTR_POSEDGE`) ile yakalanır ve FreeRTOS Kuyrukları (`xQueueSendFromISR`) üzerinden güvenli bir şekilde görevlere (`task`) aktarılır.
- **Esnek Renk Protokolü**: `rgb(R,G,B)` formatında doğrudan metin tabanlı renk ayrıştırması yapılır.

```mermaid
flowchart LR
    Phone["📱 Akıllı Telefon<br/>(nRF Connect / LightBlue)"] -->|BLE Write:<br/>'rgb(255,0,128)'| GattCb["ble_write_callback()"]
    GattCb -->|xQueueSend| BleQueue[("ble_data_queue")]
    BleQueue -->|xQueueReceive| LedTask["led_control_task()"]
    LedTask -->|RMT Driver (GPIO 8)| Led["🔴🟢🔵 WS2812 LED"]

    Button["🔘 Fiziksel Buton (GPIO 4)"] -->|Donanım Kesmesi| ISR["gpio_isr_handler()"]
    ISR -->|xQueueSendFromISR| GpioQueue[("gpio_evt_queue")]
    GpioQueue -->|xQueueReceive| BtnTask["button_task()"]
    BtnTask -->|led_strip_clear()| Led
```

---

## 📡 GATT Servis ve Karakteristik Tablosu

| Öznitelik | UUID | Yetki / İzin | Açıklama |
| :--- | :--- | :--- | :--- |
| **Primary Service** | `0xABCD` | Okunabilir | Özel LED Kontrol Servisi |
| **Characteristic** | `0x1234` | **Write** (Yazma) | Renk Verisi Alma Karakteristiği |

### Renk Komutu Formatı:
Karakteristiğe string (metin) formatında şu değerler yazılır:
- `rgb(255,0,0)` $\rightarrow$ Kırmızı
- `rgb(0,255,0)` $\rightarrow$ Yeşil
- `rgb(0,0,255)` $\rightarrow$ Mavi
- `rgb(255,255,255)` $\rightarrow$ Beyaz
- `rgb(0,0,0)` $\rightarrow$ LED'i Kapat

---

## 🔌 Donanım Bağlantıları ve Pinout

| Çevre Birimi | ESP32-C6 Pini | Açıklama |
| :--- | :--- | :--- |
| **WS2812 RGB LED (DI)** | `GPIO 8` | Geliştirme kartı üzerindeki dahili adreslenebilir LED veya harici şerit LED veri pini |
| **Fiziksel Buton** | `GPIO 4` | Basıldığında 3.3V (Yükselen Kenar / Rising Edge) tetikleyen buton pini |
| **GND** | `GND` | Ortak toprak hattı |

> [!TIP]
> Cihaz adını değiştirmek için `main/ble_led.c` dosyasındaki `DEVICE_NAME` tanımını düzenleyebilirsiniz:
> ```c
> #define DEVICE_NAME "YOUR_BLE_DEVICE_NAME" // Örn: "ESP32_C6_LED"
> ```

---

## 🛠 Derleme ve Yükleme Adımları

1. **ESP-IDF Ortamını Etkinleştirin:**
   ```bash
   . $HOME/esp/esp-idf/export.sh
   ```

2. **Hedef Çipi Belirleyin:**
   ```bash
   idf.py set-target esp32c6
   ```

3. **Projeyi Derleyin:**
   ```bash
   idf.py build
   ```

4. **Karta Yükleyin ve Seri Portu Dinleyin:**
   ```bash
   idf.py -p /dev/tty.usbserial-XXXX flash monitor
   ```

---

## 📱 Mobil Uygulama (nRF Connect / LightBlue) ile Test

1. Akıllı telefonunuza **nRF Connect for Mobile** (veya **LightBlue**) uygulamasını indirin.
2. Bluetooth'u açıp tarama başlatın ve listeden `YOUR_BLE_DEVICE_NAME` (veya belirlediğiniz isim) adlı cihaza **Connect** butonuna basarak bağlanın.
3. Servis listesinde `0xABCD` UUID'li servisi bulun ve genişletin.
4. `0x1234` UUID'li karakteristiğin yanındaki **Yukarı Ok (Yazma)** butonuna dokunun.
5. Veri tipini **UTF-8 (Text / String)** olarak seçin ve kutucuğa `rgb(0,255,120)` yazarak gönderin.
6. LED'in anında belirlediğiniz renkte yandığını göreceksiniz.
7. GPIO 4 pinine bağlı butona bastığınızda LED anında sönecektir.

---

## 📄 Lisans
Bu proje açık kaynaklıdır.
