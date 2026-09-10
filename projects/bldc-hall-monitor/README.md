# BLDC Hall İzleyici

NUCLEO-G491RE ile motor olmadan altı Hall sektörünü denemek ve ileride gerçek sensörlerle elle çevrilen motorun sektörünü canlı izlemek için hazırlanmıştır. Kart motor sürmez; PWM veya faz sürme çıkışı üretmez.

## Hemen deneme

1. `Desktop/bin/Release/BldcHallMonitor.exe` uygulamasını aç. Yeni proje oluşturman gerekmez.
2. **Bilgisayarda dene** ile kart olmadan arayüzü çalıştırabilirsin. **İleri**, **Geri** ve **Otomatik test** ile sektör ve Hall bitleri değişir.
3. Kart yazılımı yüklüyken Nucleo'yu ST-LINK USB bağlantısıyla bilgisayara bağla. Bu bilgisayarda tespit edilen port **COM3**.
4. **COM3 → Karta bağlan** seç. Yeşil **NUCLEO CANLI** yazısı geldiğinde gerçek kart verisi alınıyor demektir.
5. Kartın **mavi B1 kullanıcı düğmesine** basıp bırak. Her basışta bir sonraki sektör beklenir: `1 → 2 → 3 → 4 → 5 → 6 → 1`. Basılı tutmak tekrar üretmez. Siyah reset düğmesi bu işlev için kullanılmaz.
6. Arayüzdeki **İleri / Geri** komutları da karta gider. Görüntü yalnızca kartın gönderdiği cevapla güncellenir.

Motor olmadığında B1 düğmesi, motor milini bir sonraki Hall bölgesine elle çevirmiş olmanı temsil eder. Bu bir mekanik hareket ölçümü değildir.

## Teslim durumu — 3 Eylül 2026

- Windows arayüzü ve STM32 firmware başarıyla derlendi.
- Arayüz açıldı; bilgisayar demosunda Adım 1 / 001 → Adım 2 / 101 geçişi ve grafik görsel olarak doğrulandı.
- Veri ayrıştırıcısının geçerli/geçersiz satır kontrolleri geçti.
- Bağlı kartın 512 KiB Flash yedeği `Firmware/backups/before-hall-20260903-141246-519.bin` dosyasına alındı. SHA-256 dosyası aynı klasördedir.
- `Firmware/build/hall-monitor.bin` karta yüklendi; STM32CubeProgrammer yazılan veriyi okuyarak doğruladı ve kartı resetledi.
- **USB üzerinden canlı telemetri ve fiziksel B1 basışı henüz doğrulanmadı.** Otomatik COM3 testinin Windows erişim isteğine izin verilmedi. Flash doğrulaması, uygulamanın çalışırken seri veri gönderdiğini tek başına kanıtlamaz.

## Ekrandaki bilgiler

- Altı bölümlü elektriksel sektör göstergesi; H1/H2/H3 bitleri.
- Son geçerli komşu geçişten belirlenen ileri/geri yönü; 800 ms hareketsizlikte bekliyor.
- Oturum başlangıcına veya sıfırlamaya göre göreli adım sayısı.
- Geçersiz Hall kodu, atlanan sektör ve kartın seri alım hataları için birleşik sayaç.
- Karttaki düğme basış sayısı.
- Son 10 saniyede bilgisayara ulaşan Hall örnekleri. Bu çizim osiloskop veya yüksek hız ölçüm kaydı değildir.
- Son 250 değişiklik ekranda; son 10.000 değişiklik CSV olarak kaydedilebilir.
- 1,5 saniye veri alınmazsa canlı konum kaldırılır ve otomatik komut gönderimi durur.

Göstergedeki açı aralıkları örnek elektriksel referansa göredir. Hall sensörleri mekanik milin mutlak açısını veya enerjisizken yapılan tur sayısını vermez. `p` kutup çiftli motorda bir sektörün mekanik genişliği `60° / p` olur; elektriksel kodlar mekanik turda `p` kez tekrar eder.

## Örnek Hall sırası

120° elektriksel yerleşim için kullanılan başlangıç eşleştirmesi:

| Adım | H1 H2 H3 | Örnek elektriksel bölge |
|---|---|---|
| 1 | 001 | 0–60° |
| 2 | 101 | 60–120° |
| 3 | 100 | 120–180° |
| 4 | 110 | 180–240° |
| 5 | 010 | 240–300° |
| 6 | 011 | 300–360° |

`000` ve `111` bu düzen için geçersizdir. Ardışık olmayan geçişlerde yön veya kaç adım atlandığı tahmin edilmez; hata sayılır. Sektöre tıklayarak doğrudan uzak bir adıma geçmek bu durumu test eder.

Gerçek motorun kablo sırası ve Hall yerleşimi doğrulanmalıdır. Sıra farklıysa firmware `sequence` ve arayüz `HallFrame.Sequence` dizileri birlikte güncellenmelidir. Bu tablo motor fazlarını sürme tablosu değildir.

## Gerçek sensör veya jumper ile test

| İşlev | Nucleo / MCU pini |
|---|---|
| H1 | PA0 |
| H2 | PA1 |
| H3 | PA4 |
| Ortak toprak | GND |
| Kullanıcı düğmesi | B1 / PC13 |
| ST-LINK seri veri | LPUART1, PA2 TX / PA3 RX, AF12 |

**Gerçek Hall girişleri** düğmesi `M1` modunu seçer. Sensör girişleri aynı GPIO portundan birlikte okunur. 2 ms sabit kalan yeni kod kabul edilir. Açık girişler dahili pull-down nedeniyle 0 okunur; bağlı sensör yoksa `000 / Geçersiz durum` beklenir. Bu sürüm düşük hızlı elle hareket içindir; yüksek devirde kayıpsız ölçüm için timer capture/EXTI ve uygun tamponlama gerekir.

Motor yerine PA0, PA1 ve PA4 girişlerine tek tek **3,3 V veya GND** uygulanarak tablo denenebilir. Girişlerin pin etiketlerini kart üzerinde doğrula. Gerçek sensörün besleme gerilimi, çıkış tipi ve gerekli pull-up/gerilim uyumu sensör datasheet'ine göre belirlenmelidir; bu girişlere doğrudan 5 V sinyal bağlama. Sensör toprak hattı Nucleo ile ortak olmalıdır.

## Kaynaklar ve derleme

- `BldcHallMonitor.sln`: Visual Studio'da açılabilen hazır Windows çözümü.
- `Desktop/`: C# WinForms / .NET Framework 4.8 arayüzü.
- `Firmware/main.c`: CMSIS ile STM32G491 kart yazılımı.
- `Firmware/STM32G491RE.ld`: bellek yerleşimi.
- `Firmware/build.ps1`: kurulu Cube G4 paketi ve CubeIDE ARM derleyicisiyle ELF/BIN üretir.
- `Firmware/flash.ps1`: belirli ST-LINK seri numarasını ve G491 modelini doğrular; mevcut Flash'ı yedekledikten sonra yazıp doğrular.
- `verify.ps1`: veri ayrıştırıcı ve isteğe bağlı kart iletişimi kontrolleri.

Hazır **BldcHall_G491RE** CubeIDE projesi yan taraftaki `SageStaj/STM32_BldcHall` klasöründedir. **File → Import → General → Existing Projects into Workspace** yoluyla `STM32_BldcHall` klasörünü seç. Bu konum, eski `Firmware` çalışma alanı kaydıyla çakışmaz. Ana `main.c` dosyası bağlantıyla paylaşılır. Ayrıntılar: `Firmware/CUBEIDE-ACILIS.md`. STM32CubeIDE 2.2.0 ile Debug derlemesi doğrulandı.

Bu sürüm CMSIS tabanlıdır; `.ioc` içermez ve CubeMX ile kod üretimi gerektirmez. Gerekli CMSIS başlıkları ve ST başlangıç dosyaları lisanslarıyla birlikte projeye eklendi. Windows arayüzü ayrı Visual Studio çözümüdür.

Proje klasöründe PowerShell ile:

```powershell
# Arayüzü kapattıktan sonra yeniden derle:
.\build.ps1

# Yalnızca firmware:
.\Firmware\build.ps1

# Kart üzerindeki mevcut programı yedekleyip firmware yükle:
.\Firmware\flash.ps1

# Seri porta erişmeden ayrıştırıcı testi:
.\verify.ps1 -OfflineOnly

# Uygulamanın kart bağlantısını kapattıktan sonra uçtan uca kart testi:
.\verify.ps1 -PortName COM3
```

Kart testi SIM modunu değiştirir, ileri/geri birer elektriksel tur dener, adım atlama kontrolü yapar, HALL modunu kontrol eder ve sonunda SIM / Adım 1'e döner. Bu test fiziksel düğmeye basmaz.

`flash.ps1` bu oturumda tespit edilen ST-LINK'i hedefler. Başka kartta `-Serial` kullanılmalıdır. Flash yedeği option byte ayarlarının veya çalışma anındaki RAM'in yedeği değildir. Betik option byte değiştirmez.

## Seri protokol

115200 baud, 8 veri biti, parite yok, 1 stop biti. Her komut `\n` ile biter.

| Komut | İşlev |
|---|---|
| `?` | Durumu gönder |
| `N` / `P` | SIM modunda ileri / geri |
| `S1` … `S6` | SIM modunda doğrudan sektör seç |
| `M0` | Kart simülasyonu; başlangıç sektörü 1 |
| `M1` | Gerçek GPIO Hall girişleri |
| `R` | Göreli adımı, hata ve düğme sayaçlarını sıfırla |

Veri, değişikliklerde ve sabit konumdayken yaklaşık 100 ms aralıklarla gönderilir:

```text
F1,paket,kart_ms,mod,hall_sayisi,adim,yon,goreli_adim,hatalar,dugme
F1,12,1200,SIM,5,2,1,1,0,1
```

`hall_sayisi` 0–7 arasında onluk sayıdır; arayüz bunu üç bit olarak gösterir. Örneğin 5 = 101. Paket numarası ve zaman kart resetlenince sıfırlanır; uzun süre çalışınca 32 bit sayaçlar taşabilir. Seri bağlantı fiziksel olarak açık olsa bile yalnızca geçerli F1 satırları canlı bağlantı sayılır.

## Bağlantı sorunu olursa

- COM3 başka bir seri terminal veya uygulama tarafından açılmışsa o bağlantıyı kapat.
- Yeşil NUCLEO CANLI yazısı yoksa görünen ekranı gerçek sensör ölçümü olarak kabul etme.
- Kart resetlendiğinde veya bağlantı koptuğunda gerekiyorsa Bağlantıyı kes → Karta bağlan ile yeniden bağlan.
- 120° Hall düzeni için 000/111 kodlarında sinyal seviyelerini, toprak hattını ve kablo sırasını kontrol et.

## Teknik dayanak

- [ST UM2505 — Nucleo G4 bağlantıları](https://www.st.com/resource/en/user_manual/um2505-stm32g4-nucleo64-boards-mb1367-stmicroelectronics.pdf)
- [ST six-step algoritması — 60 elektriksel derecelik sektörler](https://wiki.st.com/stm32mcu/wiki/STM32MotorControl%3A6-step_Firmware_Algorithm)
- Kullanılan yerel paket: STM32Cube_FW_G4_V1.6.3; G491 CMSIS tanımları ve G4 Nucleo BSP pin tanımları.
