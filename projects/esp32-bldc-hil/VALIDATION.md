# Doğrulama durumu — 9 Eylül 2026

- İstenen proje dosyaları, ayrı ESP-IDF Unity test uygulaması ve bilgisayar test
  hedefi oluşturuldu.
- Kaynak incelemesinde paket boyutları/ofsetleri, CRC kapsamı, timeout sınırı,
  unsigned sequence karşılaştırması ve sabit tamponlar kontrol edildi.
- Üretim C kaynaklarında açık malloc/calloc/realloc, printf/ESP_LOG veya
  Wi-Fi/Bluetooth başlatma çağrısı bulunmadığı metin taramasıyla kontrol edildi.
- 25 taşınabilir test ve ilave 1 gerçek FreeRTOS TX kuyruğu Unity testi mevcut.

## ESP-IDF kurulumu sonrası derleme doğrulaması

ESP-IDF 5.4.4, `C:/Espressif/frameworks/esp-idf-v5.4.4` altında bulundu.
Python 3.11.2 ve Xtensa GCC 14.2.0 ile klasik ESP32 hedefinde:

| Hedef | Sonuç | Uygulama binary boyutu |
|---|---|---:|
| Normal HIL firmware: `idf.py build` | Başarılı, derleyici uyarısı yok | 191952 bayt |
| Unity uygulaması: `test_app` içinde `idf.py build` | Başarılı, derleyici uyarısı yok | 172512 bayt |

Üretim kaynakları `-Wall -Wextra -Werror` ile derlendi. Bootloader, partition
table ve uygulama binary'leri oluşturuldu; uygulamalar 1 MB bölüme sığıyor.
Çıktılar `build/esp32_bldc_hil.bin` ve
`test_app/build/esp32_bldc_hil_tests.bin` dosyalarıdır. Yüklerken yalnız uygulama
binary'si yerine README'deki `idf.py flash` komutunu kullanın; bu komut
bootloader ve partition table'ı da doğru adreslere yazar.

Unity'nin varsayılan yapılandırmasında 64-bit assertion desteği kapalı olduğu
için timestamp eşitlikleri C'nin uint64 karşılaştırması ve `TEST_ASSERT_TRUE`
ile denetleniyor. Böylece ek Unity ayarı gerekmiyor.

İlk sandbox denemesi kurulum dosyalarının kullanıcı sahipliği ve araç erişimi
nedeniyle başarısız oldu; izin verilen kullanıcı ortamındaki derlemeler tamamlandı.
Başarılı üretim derleme kayıtları `build/log/idf_py_stdout_output_8064` ve
`build/log/idf_py_stdout_output_15040`; test kayıtları `test_app/build/log` altında.
Git geçmişi olmayan çalışma klasörü nedeniyle CMake varsayılan proje sürümü
`1` kullanıldı; bu durum derlemeyi engellemedi.

## Kart üzerinde test ve yükleme

Kullanıcı kartı bağladıktan sonra COM4 üzerinde CP210x USB–UART ve
ESP32-D0WD-V3 rev3.1 doğrulandı. Unity test firmware'i yüklendi; flash hash
doğrulamaları başarılı oldu. Kart resetlenip seri konsol çıktısı kaydedildi:

```text
26 Tests 0 Failures 0 Ignored
OK
```

Kayıt: `unity-board-output.txt`. Tüm 25 model/protokol testi ve gerçek statik
FreeRTOS TX kuyruğu doluluk testi kart üzerinde geçti.

Ardından normal `esp32_bldc_hil` firmware'i COM4'e yüklendi, flash hash'leri
doğrulandı. Reset sonrası 12 saniyelik konsol gözleminde uygulama açıldı,
UART başlangıcı tamamlandı; panic, watchdog veya tekrarlayan reset görülmedi.
Kayıt: `hil-board-boot-output.txt`. Kartta bırakılan yazılım normal HIL
firmware'idir; test firmware'i değildir. Seri port gözlem sonrası kapatıldı.

Kartın fiziksel flash'ı 4 MB, mevcut genel proje yapılandırması 2 MB'dir.
ESP-IDF açılışta bu farkı uyarı olarak bildirir ve yalnız tanımlı 2 MB'ı
kullanır. Uygulama bu alana sığar; bu bir derleyici uyarısı veya test hatası
değildir. Kartın üst 2 MB alanı bu yapılandırmada kullanılmaz.

## Henüz doğrulanmamış kontroller

Fiziksel STM32–ESP32 4 Mbaud haberleşmesi, 1 kHz gerçek zaman bütçesi,
`hil_missed_ticks == 0`, fiziksel RX overflow ve uçtan uca reset senaryoları
henüz ölçülmedi. Açılış gözlemi bu kontrollerin yerine geçmez. README'deki
STM32 bağlantısı ve kart kabul testleriyle devam edilmelidir.
Bilgisayar için hazırlanan assertion adaptörü ayrıca çalıştırılmadı; aynı
taşınabilir testler gerçek ESP32 üzerinde Unity ile çalıştırıldı.
