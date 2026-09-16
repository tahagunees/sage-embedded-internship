# ESP32 Canlı Kontrol Paneli

ESP-IDF 5.4 ile C++ olarak geliştirilmiş, ESP32'nin kendi Wi-Fi ağı üzerinden
çalışan yerel bir kontrol paneli. Harici sunucu veya internet bağlantısı gerekmez.

## Özellikler

- GPIO34'e bağlı potansiyometrenin ham ADC değerini ve yüzdesini gösterir.
- Potansiyometreyle GPIO2'deki dahili LED'in parlaklığını ayarlar.
- LED'i web panelindeki düğmeyle veya GPIO4 dokunmatik girişle açıp kapatır.
- ESP32'nin dahili çip sıcaklığını ve dokunmatik girişin ham değer/eşiğini gösterir.
- Telefon ekranına uyumlu panelde verileri otomatik yeniler.

> Dahili çip sıcaklığı ortam sıcaklığı değildir. DHT11 kodu dosyada deneysel
> çalışma olarak bulunur; sensör güvenilir veri vermediği için şu anda
> başlatılmaz ve panelde gösterilmez.

## Bağlantı

Potansiyometrenin bir dış bacağını ESP32 `3V3` pinine, diğer dış bacağını
`GND` pinine, orta bacağını `GPIO34` pinine bağlayın. Potansiyometreye
**5 V vermeyin**. Dahili LED ve GPIO4 dokunmatik giriş için ek parça gerekmez;
dokunmatik giriş kart üzerindeki GPIO4 pininden kullanılır.

## Kullanım

1. Telefonu veya bilgisayarı `ESP32-Panel` Wi-Fi ağına bağlayın.
2. Parola: `esp32panel`.
3. Tarayıcıda `http://192.168.4.1` adresini açın.

Panel `GET /api/status` ile güncel değerleri okur. LED düğmesi
`POST /api/led/toggle` isteği gönderir. Aynı LED, GPIO4'e dokunarak da
açılıp kapatılabilir.

## Derleme ve karta yükleme

ESP-IDF 5.4 ortamında bu klasörde:

```powershell
idf.py set-target esp32
idf.py build
idf.py -p COM4 flash monitor
```

`COM4` yerine kendi kartınızın seri portunu yazın. Seri monitörden çıkmak için
`Ctrl+]` kullanın. Derleme klasörü ve yerel `sdkconfig` dosyaları Git'e dahil
edilmez; `sdkconfig.defaults` gerekli proje ayarlarını taşır.
