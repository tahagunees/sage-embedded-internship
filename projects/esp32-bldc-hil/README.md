# ESP32 BLDC elektrik modeli / HIL

ESP32 **motor sürücüsü değildir**. Yalnızca STM32 üzerinde çalışan PI kontrolcüsüne
sanal akım geri beslemesi üretir. ESP32 hiçbir motor fazını, MOSFET'i, gate driver'ı
veya PWM çıkışını sürmez. **ESP GPIO'ları motor fazlarına bağlanmamalıdır.**

Hall ve encoder RPM bilgisi gerçek motordan STM32'ye gelir. UI yalnızca STM32
LPUART1 PA2/PA3 üzerinden bağlı kalır. STM32–ESP32 haberleşmesi ayrı USART1
PC4/PC5 hattındadır. ESP32 UI ile haberleşmez ve telemetri köprüsü değildir.

```text
Masaüstü UI <--> STM32 LPUART1 PA2/PA3
                     |
               STM32 PI kontrolcüsü <--- gerçek encoder / Hall
                     |
               USART1 PC4/PC5 <--> ESP32 UART2 GPIO16/17
                                    elektrik modeli
```

## Bağlantı

| STM32 | ESP32 |
|---|---|
| PC4 / USART1_TX / Arduino D1 | GPIO16 / UART2_RX |
| PC5 / USART1_RX / Arduino D0 | GPIO17 / UART2_TX |
| GND | GND |

4,000,000 baud, 8N1, flow control yok; iki uç da 3.3 V UART lojik seviyesi kullanır.
Kartlar ayrı USB bağlantılarından besleniyorsa **3V3 pinlerini birbirine bağlamayın**;
yalnız GND ortaklanmalıdır. UART tellerini kısa tutun.
Hedef klasik ESP32, örneğin GPIO16/17'si serbest ESP32-WROOM-32 kartıdır.
PSRAM için GPIO16/17 kullanan modüller bu pin düzenine uygun değildir.
STM32 pin alternate-function ve USART saatinin 4 Mbaud için uygunluğu kartta kontrol edilmelidir.

## Derleme ve flash

ESP-IDF **5.4.x**, C ve klasik `esp32` hedefi esas alınmıştır. Arduino kullanılmaz.
ESP-IDF terminalinde bu klasörden:

```sh
idf.py set-target esp32
idf.py build
idf.py -p COM7 flash monitor
```

`COM7` yerine ESP32 portunu yazın; STM32'nin UI portunu seçmeyin.
Uygulama kaynaklarında `-Wall -Wextra -Werror` etkin. Wi-Fi/Bluetooth başlatılmaz;
dinamik güç yönetimi ve PSRAM kapalıdır. UART0 yalnız ESP-IDF boot/başlangıç
hata mesajlarını taşır. UART2'de yalnız binary akım paketleri vardır.

UART sürücüsü ve GPTimer kendi kaynaklarını başlangıçta ayırır. Görev yığınları,
TX kuyruğu, parser ve uygulama tamponları statiktir. Timer başladıktan sonra
uygulama bellek ayırmaz; 1 kHz yolunda `printf`/`ESP_LOG` çağrısı bulunmaz.

## Model ve zamanlama

Tüm motor parametreleri `main/config.h` içindedir. R_EQ ve L_EQ aktif iki fazın
eşdeğer hat direnci ve endüktansıdır. Sonuçların doğruluğu **R, L, Ke ve Vbus'ın
gerçek motora uygunluğuna bağlıdır**. Bu tek yönlü, ortalama elektrik modelidir;
anahtarlama ripple'ı, faz komütasyon geçişleri, mekanik dinamik ve rejeneratif
negatif akım modellenmez. Hall sektörünün geçerliliği kontrol edilir; sektöre
bağlı ayrı faz akımları üretilmez. Ham Hall değeri tanısal bilgidir; Hall-bit/sector
eşlemesi STM32'ye aittir ve ESP tarafında ikinci bir eşleme varsayılmaz.

Her GPTimer alarmı 1000 us'de yalnız model task'ına notification verir. ISR'de
model hesabı veya UART çağrısı yoktur. Görev öncelikleri model=20, RX=19, TX=18;
üç görev core 0'a sabitlenmiştir. Floating-point işlemleri görev bağlamındadır.

Her uyanışta güncel komutun thread-safe yerel kopyası alınır, yerel monotonic
saat ile timeout denetlenir, sabit dt=0.001 s adımı çalışır, sensör çıkışı
hesaplanır, statik paket tamponu doldurulur ve TX kuyruğuna sıfır bekleme süresiyle
kopyalanır. UART üzerinde bekleyebilen tek gönderim yolu düşük öncelikli TX
görevidir. Kuyruk kapasitesi 4 pakettir; doluysa yeni paket atılır. Artan drop
sayısı bir sonraki başarıyla gönderilebilen pakette görünür.

```text
omega = abs(rpm_x10 / 10) * 2*pi/60
back_emf = Ke * omega
applied = Vbus * duty_x100 / 10000
dl = dt / L
i_next = clamp((i_previous + dl*(applied - back_emf))/(1 + dl*R), 0, Imax)
alpha = dt / (tau + dt)
measured_next = measured_previous + alpha*(i_next + offset + noise - measured_previous)
```

`duty_x100=4025` → %40.25 → 24 V beslemede 9.66 V. Sensör filtresi de geri
Euler ile uygulanır; tau=0 filtresiz davranıştır. Gürültü ±noise aralığında,
sabit seed'li xorshift32 ile tekrarlanabilir uniform örnektir. Sensör durumu
ve iletilen mA değeri int16 aralığına doyurulur; offset negatif ölçüm üretebilir.
Ölçekleme en yakın mA/mV'ye yuvarlanır, taşma yerine doyum kullanılır.

Başlangıçta model ve ölçüm sıfır, applied=0'dır. İlk geçerli komut öncesinde,
enabled=0, Hall valid=0, sector∉[1,6] veya son kabul edilen komutun yaşı
**>=5000 us** ise applied=0 olur. Mevcut akım doğrudan sıfırlanmaz; aynı R/L
denklemi çalışır. RPM sıfır değilken geri-EMK de akımın azalmasını hızlandırır.
Timeout sırasında son RPM korunur. CRC hatasında henüz süresi dolmamış eski
geçerli komut kullanılabilir. Hiç komut yoksa model gerilim uygulamaz.

NaN/Inf parametreleri, R/L<=0, negatif Vbus/Ke/noise/tau ve pozitif olmayan
Imax başlangıçta reddedilir. int16 mA protokolü nedeniyle Imax<=32.767 A gerekir.
Aşırı sonlu parametrelerde ara taşmayı önlemek için denklem double ara değerler
kullanır; durum değerleri float'tır. Gerçek donanımda 1 ms bütçe ölçülmelidir.

Alarm birikirse bir uyanışta tek sabit-dt adımı yapılır; eski adımlar art arda
UART'a püskürtülmez. Atlanan tick sayısı debugger'da `hil_missed_ticks` ile
izlenir. Böyle bir overrun'da simülasyon zamanı geriden gelir; bu koşul geçerli
gerçek zamanlı HIL testi sayılmaz. Mevcut sabit paket formatında ek overrun biti
yoktur. Kart kabul testinde `hil_missed_ticks == 0` şartı aranmalıdır.

## Binary protokol, sürüm 1

Tüm çok baytlı alanlar little-endian. Struct packing/cast kullanılmaz.
Sync değeri 0xA55A; telde **5A A5**. `payload_length` yalnız payload'ı kapsar;
6 bayt başlık ve 2 bayt CRC'yi kapsamaz. CRC-16/CCITT-FALSE: poly=0x1021,
init=0xFFFF, refin=false, refout=false, xorout=0; `123456789` → 0x29B1.
CRC'ye sync dahil edilmez. Version'dan payload sonuna kadar hesaplanır ve
CRC'nin kendisi little-endian gönderilir.

### STM32 → ESP32 (26 bayt)

| Ofset | Tip | Alan |
|---:|---|---|
| 0 | uint16 | sync=0xA55A |
| 2 | uint8 | version=1 |
| 3 | uint8 | type=0x01 |
| 4 | uint16 | payload_length=18 |
| 6 | uint32 | sequence |
| 10 | uint32 | timestamp_ms |
| 14 | int32 | rpm_x10 |
| 18 | uint16 | duty_x100, 0…10000 |
| 20 | uint8 | hall |
| 21 | uint8 | sector |
| 22 | uint8 | flags: bit0 enabled, bit1 Hall valid |
| 23 | uint8 | reserved=0 |
| 24 | uint16 | CRC, byte 2…23 üzerinden |

Diğer flags bitleri sıfır olmalıdır. Duty>10000, reserved!=0 veya bilinmeyen
flags bitleri geçersiz paket sayılır; snapshot ve timeout yenilenmez.
Hall invalid / sector invalid / disabled komutları ise geçerli komut olarak
kabul edilir ve elektrik girişini sıfıra indirir.

### ESP32 → STM32 (36 bayt)

| Ofset | Tip | Alan |
|---:|---|---|
| 0 | uint16 | sync=0xA55A |
| 2 | uint8 | version=1 |
| 3 | uint8 | type=0x02 |
| 4 | uint16 | payload_length=28 |
| 6 | uint32 | received_sequence |
| 10 | uint32 | esp_timestamp_ms |
| 14 | int16 | measured_current_ma |
| 16 | int16 | model_current_ma |
| 18 | int32 | back_emf_mv |
| 22 | int32 | applied_voltage_mv |
| 26 | uint16 | status |
| 28 | uint16 | rx_error_count |
| 30 | uint16 | rx_lost_count |
| 32 | uint16 | tx_drop_count |
| 34 | uint16 | CRC, byte 2…33 üzerinden |

| Status biti | Anlam |
|---:|---|
| 0 | Kabul edilmiş ve henüz timeout olmamış STM32 komutu var |
| 1 | Son kabul edilen komutun Hall valid biti ve sektörü geçerli |
| 2 | Son kabul edilen komutta enabled=1 |
| 3 | Komut yok veya komut timeout oldu |
| 4 | Bu ESP boot'unda en az bir CRC hatası görüldü (kalıcı) |
| 5 | Bu ESP boot'unda en az bir RX overflow görüldü (kalıcı) |

Bit1/2 timeout sırasında son komutun değerlerini yansıtır; uygulanan gerilimin
etkin olduğunu tek başına göstermez. İlk komuttan önce sequence=0'dır, bit0
olmadığından gerçek sequence=0 komutuyla karışmaz. Timeout sırasında son kullanılan
sequence geri gönderilmeye devam eder.

`rx_error_count`: CRC, başlık/alan doğrulama, UART frame/parity/break ve overflow
olayları toplamıdır; bozuk akışta yeniden senkronizasyon birden fazla hata
sayabilir. `rx_lost_count`: yalnız kabul edilen sequence sıçramalarındaki eksik
komut sayısıdır, kayıp byte sayısı değildir. Duplicate ve eski paket sayaçları
`hil_uart_snapshot()` ile ayrı uint32 alanlar olarak okunabilir; belirtilen
protokolde bunlar için alan yoktur. İç sayaçlar uint32, paket sayaçları uint16
üst sınırında doyar. CRC/overflow bitleri reset'e kadar korunur.

RX sürücü ring buffer'ı 2048 bayt, parser en fazla 36 bayt tutar. Ayrıştırıcı
byte bazlıdır; eksik paketleri birleştirir, yanlış CRC/başlıkta bir byte kaydırıp
sync arar. UART FIFO/ring overflow durumunda sürücü buffer'ı ve kısmi paket
atılır; son kabul edilmiş komutun yaşı değişmez.

## Sequence, saatler ve STM32 entegrasyonu

İlk geçerli sequence koşulsuz kabul edilir. Sonrasında unsigned
`delta = new - last`: 0 duplicate, 1…0x7FFFFFFF ileri, >=0x80000000 eski/belirsiz
kabul edilir. İleri sıçramada delta-1 kayıp sayılır. FFFFFFFF→00000000 normal
devamdır. Duplicate/eski paket hiçbir zaman snapshot'ı veya timeout'u yenilemez.
İki karşılaştırma arasındaki sequence ilerlemesi 2^31'den küçük olmalıdır.

Timeout, ESP'nin 64-bit monotonic kabul zamanı ile ölçülür; STM32 timestamp'iyle
saat senkronizasyonu varsayılmaz. Gönderilen ESP timestamp'i boot'tan itibaren
ms'dir ve yaklaşık 49.7 günde uint32 olarak taşar. RX zamanı parser kabul
zamanıdır; STM32'nin ölçüm üretme yaşı bu protokolle bağımsız doğrulanamaz.

STM32 tarafında uygulanacak akış:

1. Encoder/Hall değerlerini ve son PI duty çıkışını örnekleyin. Her yeni komutta
   sequence'i artırın; fixed-size 0x01 paketini USART1 üzerinden gönderin.
2. USART1 RX'te akım paketinin sync/version/type/length/CRC'sini kontrol edin.
   `measured_current_ma / 1000.0f` ölçümünü **bir sonraki** PI adımına verin.
3. ESP timestamp ve echoed sequence ilerlemesini, bit0/bit3'ü ve yerel son
   cevap yaşını izleyin. Eski, eksik veya bozuk cevabı yeni measurement saymayın.
4. ESP cevap timeout'unda STM32 PI için güvenli durumu uygulayın; bu ESP projesi
   STM32'nin gerçek güç çıkışlarını kapatamaz. UI yolu LPUART1'de kalmalıdır.

ESP resetinde elektrik/sensör durumu ve sayaçlar sıfırlanır. Cevap kesintisi,
ESP timestamp gerilemesi ve ilk komut öncesi bit0=0 reset için gözlenebilir
işaretlerdir. Normal timestamp taşması modulo karşılaştırmayla ayırt edilmelidir.
**Verilen protokolde boot/session kimliği olmadığından her hızlı resetin yalnız
echo sequence ile kesin saptanması mümkün değildir**: ESP ilk yeni komutu
hemen alıp aynı sequence akışını sürdürebilir. STM32 mutlaka yerel cevap
timeout'u ve ESP timestamp'ini birlikte izlemelidir; kesin reset tespiti
gerekiyorsa yeni protokol sürümünde boot kimliği eklenmelidir.

STM32 tek başına resetlenip sequence'i sıfırlarsa ESP bunu eski paket sayabilir;
yeniden bağlanmada iki kartı birlikte resetleyin veya STM32 sequence sürekliliğini
koruyun. Timeout'ta sequence kilidi kendiliğinden kaldırılmaz; aksi halde
gecikmiş eski komutlar yeniden etkinleşebilir.

4 Mbaud'da 26 bayt yaklaşık 65 us, 36 bayt yaklaşık 90 us sürer (8N1).
UART hatları full-duplex'tir. Görev gecikmesi, STM32 tarafı zamanlama ve USB/UI
yükünden kaynaklanan etkiler ayrıca kart üzerinde ölçülmelidir.

## Testler

Gerçek ESP-IDF Unity test uygulaması ayrı `test_app` klasöründedir:

```sh
cd test_app
idf.py set-target esp32
idf.py build
idf.py -p COM7 flash monitor
```

Test firmware'i normal HIL firmware'inin yerini alır. Motor veya STM32 bağlantısı
gerektirmez. Gerçek statik FreeRTOS TX kuyruğunun dolması da test edilir;
UART görevleri bu testte başlatılmadığından tüketici kuyruğu boşaltmaz.

Aynı 25 taşınabilir C testi bilgisayarda basit assertion adaptörüyle çalışır;
bu adaptör ESP-IDF Unity veya FreeRTOS emülatörü değildir:

```sh
cmake -S tests/host -B tests/host/build
cmake --build tests/host/build
ctest --test-dir tests/host/build --output-on-failure -C Debug
```

Kapsam: başlangıç, 0→3 A test-PI duty cevabı, 0/1500/negatif RPM, duty sınırları,
Hall/sektör invalid, disabled, tam 5 ms timeout, decay, sensör filtresi/offset/noise,
geçersiz parametreler, CRC referansı, iki paket tipi serialization, bozuk/eksik
paket ve yeniden sync, overflow olay işleyicisi, duplicate/eski/kayıp/wrap sequence,
geçersiz alanlar, 100.000 rastgele byte ile parser sınırları, sayısal doyum,
Unity üzerinde TX kuyruğu dolması. Test-PI yalnız test dosyasındadır.

Overflow testi gerçek FIFO'yu fiziksel olarak taşırmaz; üretimde kullanılan
parser overflow işleyicisini çağırır. Fiziksel UART taşması, 4 Mbaud sinyal
bütünlüğü, ISR/task gecikmesi ve STM32 PI entegrasyonu kart kabul testidir.
HIL çalışırken debugger ile `hil_missed_ticks`, sayaçlar ve yığın payı izlenmeli;
komut kesilince 5 ms eşiğinde applied=0, akımın ise doğal decay göstermesi
doğrulanmalıdır.

Derleme ve test oturumunun durumu `VALIDATION.md` dosyasında tutulur.

## Resmî API kaynakları

- [ESP-IDF 5.4 GPTimer](https://docs.espressif.com/projects/esp-idf/en/release-v5.4/esp32/api-reference/peripherals/gptimer.html)
- [ESP-IDF 5.4 UART](https://docs.espressif.com/projects/esp-idf/en/release-v5.4/esp32/api-reference/peripherals/uart.html)
