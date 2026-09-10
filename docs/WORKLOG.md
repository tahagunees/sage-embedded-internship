# BLDC akım kontrolü ve HIL simülasyonu çalışma geçmişi

**Güncel durum:** 10 Eylül 2026  
**Amaç:** Gerçek motoru döndürmeden STM32 üzerindeki PI akım kontrolcüsünü, sanal bir üç fazlı BLDC motora karşı güvenli ve tekrarlanabilir biçimde sınamak.

## 1 Kısa cevapla şu anda neredeyiz

Çalışmanın güncel çözümü şöyledir:

```text
STM32 üzerinde çalışan PI akım kontrolcüsü
        ↓ duty, sektör, enable ve hedef akım
Windows PC üzerinde çalışan üç fazlı BLDC modeli
        ↓ akım, Hall, encoder, RPM ve tork
STM32 üzerindeki bir sonraki PI adımı
```

PC modelinin çekirdeği ve görsel arayüzü oluşturuldu. Model için 23 otomatik test 10 Eylül 2026 tarihinde yeniden çalıştırıldı ve tamamı geçti. Seçilen STM32 projesinde PC simülasyon modu ve UART v3 bağlantı kodları bulunuyor; 9 Eylül tarihli ARM çıktı dosyası da mevcut. Ancak gerçek STM32 kartı ile PC arasındaki fiziksel UART kapalı çevrim testi henüz doğrulanmış son aşama değildir.

ESP32 ile hazırlanan önceki sanal akım çözümü çalışır ve kart üzerinde 26 testten geçti. Fakat hedef sonradan değiştiği için artık ana çözüm değildir. Çünkü o çözüm gerçek Hall ve RPM bilgisini STM32'den alıyor, kendi rotorunu hareket ettirip Hall ve encoder üretmiyordu.

## 2 Projelerin birbirinden farkı

| Proje veya klasör | Rolü | Bugünkü durumu |
|---|---|---|
| `projects/stm32-pc-hil` | Güncel STM32 projesi ve PI kontrolcüsü | Esas STM32 projesi. PC simülasyon modu varsayılan olarak açık. |
| `projects/bldc-pc-simulator` | PC üzerinde üç fazlı BLDC motor modeli ve arayüz | Güncel sanal motor çözümü. 23 test geçti. |
| `projects/esp32-bldc-hil` | ESP32 üzerinde basit sanal akım geri beslemesi | Tamamlanmış ara çözüm. 26 kart testi geçti; yeni PC mimarisinde kullanılmıyor. |
| `current_sim_work` | Önceki çalışma kopyaları ve yardımcı doğrulamalar | Güncel STM32 kaynağı olarak kullanılmamalı. |

Önemli kural: PC simülatörü ile ESP32 simülatörü aynı testte birlikte kullanılmaz. Bunlar farklı iki HIL yaklaşımıdır.

## 3 Başlangıç noktası

İlk STM32 yazılımının temel amacı gerçek Hall sensörlerini ve encoder'ı okumak, bunlardan sektör ve RPM üretmek ve akım kontrolü için bir PI yapısı kurmaktı.

STM32 tarafındaki temel donanım tanımları:

| İşlev | STM32 kaynağı |
|---|---|
| Hall girişleri | PC0, PC1 ve PB0 |
| Encoder | TIM2, PA0 ve PA1 |
| Encoder çözünürlüğü | 4096 PPR ve x4 sayım ile 16384 sayım devir |
| PI adımı | 10 ms |
| Seri hat | LPUART1, PA2 TX ve PA3 RX, 4 Mbaud |

İlk akım modeli tek bir eşdeğer R ve L yüküydü. Duty gerilimi, geri elektromotor kuvveti ve direnç düşümüyle tek bir sanal akım hesaplanıyordu:

```text
L × akım değişim hızı = uygulanan gerilim - geri EMK - R × akım
```

Bu yöntem PI algoritmasının temel hata, integral, duty ve doyum davranışını görmek için yararlıydı. Ancak üç ayrı faz akımı, elektromanyetik tork, rotor ataleti ve sanal Hall veya encoder üretmiyordu. Bu nedenle tam sanal motor ihtiyacını karşılamıyordu.

## 4 İlk HIL yaklaşımı olarak ESP32

İlk kapsamda UI doğrudan STM32'ye bağlı kalacak, STM32 gerçek motordan Hall ve encoder RPM okuyacak, ESP32 ise yalnız sanal akım hesaplayacaktı.

```text
Gerçek motor Hall ve encoder → STM32 PI → duty
                                  ↓
                         ESP32 elektrik modeli
                                  ↓
                         sanal akım → STM32 PI
```

ESP32 için ESP-IDF ve C ile şu yapı hazırlandı:

- UART2, GPIO16 RX ve GPIO17 TX, 4 Mbaud, 8N1.
- GPTimer ile 1 kHz model görevi.
- ISR içinde yalnız görev bildirimi; kayan noktalı motor hesabı görev içinde.
- UART RX parser, sabit boyutlu little-endian paketler ve CRC16 CCITT FALSE.
- Ayrı yüksek öncelikli model görevi ve daha düşük öncelikli TX görevi.
- Komut zaman aşımı, Hall veya sektör geçersizliği, sequence kaybı, tekrar paket ve taşma kontrolleri.
- Çalışma yolunda uygulama tarafından dinamik bellek ayırmama ve log kullanmama.

ESP-IDF 5.4.4 kuruldu. Normal uygulama ve Unity test uygulaması uyarısız derlendi. ESP32 kartı COM4 üzerinde ESP32-D0WD-V3 olarak tanındı. Test yazılımı karta yüklendi ve sonuç şu oldu:

```text
26 Tests 0 Failures 0 Ignored
OK
```

Daha sonra normal ESP32 HIL yazılımı karta yüklendi ve açılışta çökme, watchdog veya tekrar reset görülmedi.

## 5 Neden ESP32 çözümünden PC modeline geçildi

İhtiyaç daha sonra netleşti: Gerçek motor hiç hareket etmeyecek; sanal sistem akımın yanında Hall ve encoder değerlerini de üretmeliydi. Böylece STM32, fiziksel motor varmış gibi PI akım kontrolü ve komütasyon mantığını sınayabilecekti.

ESP32'deki ilk model bunu yapmıyordu. Gerçek RPM ve Hall değerlerini giriş olarak bekliyordu. Bu nedenle hedef aşağıdaki yapıya dönüştü:

```text
STM32 PI ve komütasyon kararı
        ↓
PC üç fazlı sanal motor
        ↓
ia, ib, ic, ölçülen akım, RPM, Hall, encoder ve tork
        ↓
STM32 PI ve komütasyon kararı
```

PC seçiminin temel nedeni modelin daha ayrıntılı hesaplanabilmesi, grafiklerle izlenebilmesi, parametrelerin kolay değiştirilebilmesi ve test verilerinin CSV olarak kaydedilebilmesidir.

## 6 Güncel STM32 projesinin seçilmesi

Bir süre çalışma klasöründeki eski kopyalar ile masaüstündeki gerçek STM32 projesi karıştı. Güncel ve esas proje şu olarak belirlendi:

`projects/stm32-pc-hil`

Bu projede iki mod korunuyor:

- `PC_SIMULATION=1`: PI STM32'de, üç faz motor ve sanal sensörler PC'de.
- `PC_SIMULATION=0`: Fiziksel Hall ve TIM2 encoder ile eski tek akım modeli.

Güncel varsayılan PC simülasyon modudur. `pc_sim.c`, `pc_sim_link.c` ve `pc_sim.h` dosyaları bu amaçla projede bulunuyor. `Debug/Real_motor_read_hall.elf` dosyası 9 Eylül 2026 tarihinde oluşturulmuş durumda.

## 7 PC üzerinde üç fazlı BLDC modelinin kurulması

PC modeli için yaygın mühendislik yaklaşımı olan yıldız bağlı, trapez geri EMK'li, altı adımlı üç faz BLDC modeli seçildi. Belirli bir ticari motorun sektörde en çok kullanılan motor olduğu varsayılmadı. Varsayılan sayılar örnek değerlerdir ve gerçek motor ölçümleriyle kalibre edilmelidir.

Modelin ana parçaları şunlardır:

1. A, B ve C fazlarının ayrı akım hesapları.
2. Yıldız noktası ve `ia + ib + ic = 0` elektriksel koşulu.
3. Rotor açısına bağlı üç faz trapez geri EMK.
4. Altı adımlı inverter ve boşta kalan fazın diyotlar üzerinden doğal akım sönümü.
5. Akımlardan elektromanyetik tork üretimi.
6. Atalet, yük ve sürtünme üzerinden hız ve rotor konumu.
7. Rotor konumundan Hall sektörü ve encoder sayımı.
8. Filtreli akım sensörü çıkışı ve aşırı akım koruması.

Modelde enerji tutarlılığı aynı motor sabiti kullanılarak korunur:

```text
elektriksel dönüşüm gücü = elektromanyetik tork × mekanik açısal hız
```

İki çalışma modu vardır:

- Ortalama PWM: Faz terminaline `duty × Vbus` uygulanır. Kontrol tasarımı için hızlıdır.
- Anahtarlamalı PWM: Faz gerilimi 20 kHz taşıyıcıyla anahtarlanır. Akım dalgalanmasını daha ayrıntılı gösterir fakat daha yavaştır.

## 8 PC arayüzünün hazırlanması

`bldc_pc_sim` içinde Windows'ta çalışan bir arayüz oluşturuldu. `Baslat.bat` ile açılır.

Arayüzde:

- Faz akımları, ölçülen akım, hedef akım, mekanik RPM, Hall sektörü ve encoder izlenir.
- PC demo modu ile STM32 olmadan örnek PI davranışı görülebilir.
- STM32 UART modu ile gerçek PI kartta çalışır.
- Rotor kilidi, ters yön ve anahtarlamalı PWM seçilebilir.
- Vbus, faz R ve L, Ke, kutup çifti, atalet, sürtünme, yük ve encoder çözünürlüğü değiştirilebilir.
- Parametreler JSON olarak saklanabilir, sonuçlar CSV olarak dışarı alınabilir.
- Grafikler çift tıklanarak büyütülebilir ve zaman aralığında gezilebilir.

PC demo modundaki PI yalnız arayüzü ve motor modelini tek başına göstermek içindir. STM32 PI kodunun geçtiğini kanıtlamaz. Asıl doğrulama STM32 UART modunda yapılacaktır.

## 9 UART protokolünün gelişimi

İlk PC paket tasarımı v2 idi. Daha sonra STM32 hedef akımının da PC grafiğinde gösterilmesi gerektiği için protokol v3'e çıkarıldı.

Güncel v3 yapısı:

| Yön | Paket | Boyut | Temel içerik |
|---|---|---:|---|
| STM32 → PC | Komut 0x10 | 30 bayt | sequence, session, 1 ms adım, duty, sektör, enable, yön, PWM modu ve hedef akım |
| PC → STM32 | Feedback 0x11 | 68 bayt | üç faz akımı, PI ölçüm akımı, bara akımı, RPM, encoder, tork, Hall, sektör ve durum sayaçları |

Paketler little-endian ve CRC16 CCITT FALSE korumalıdır. PC yeni bir bağlantıda rastgele session kimliği üretir. Sequence her yeni model adımında artar. Aynı sequence ve aynı içerik tekrar gelirse önceki cevap yeniden gönderilir fakat motor ikinci kez ilerlemez.

Windows ve USB UART kesin 1 ms gerçek zaman garantisi vermediği için lockstep yöntemi seçildi:

```text
STM32 komut gönderir
PC tam 1 ms model zamanı hesaplar
PC feedback gönderir
STM32 bir sonraki adımı üretir
```

Bu sayede bilgisayar yavaşlasa bile model denklemlerindeki zaman adımı değişmez. Mevcut STM32 PI kontrolü her 10 yeni feedback sonrasında, yani 10 ms model zamanında çalışır.

## 10 Güncel bağlantının önemli farkı

İlk ESP32 mimarisinde UI için LPUART1, ESP32 için ayrı USART1 düşünülmüştü. Güncel PC simülasyon açıklamasında PC HIL bağlantısı LPUART1 PA2 ve PA3 üzerinden yapılmıştır.

Bu nedenle güncel durumda aynı seri portu hem eski UI hem PC motor simülatörü aynı anda açamaz. UI telemetrisi de aynı anda isteniyorsa gelecekte iki seçenekten biri uygulanmalıdır:

1. PC simülasyon protokolünü USART1 PC4 ve PC5'e taşımak, LPUART1'i UI için bırakmak.
2. Tek PC uygulamasında HIL ve UI işlevlerini birleştirmek.

İlk fiziksel test için en basit yol LPUART1'i yalnız PC simülatörüne ayırmaktır.

## 11 Yapılan testler

### ESP32 ara çözümü

- ESP-IDF 5.4.4 ile normal uygulama derlendi.
- Unity test uygulaması derlendi.
- ESP32 kartında 26 test geçti.
- Normal yazılım karta geri yüklendi ve açılışı gözlendi.

### PC üç faz motor modeli

10 Eylül 2026 tarihinde testler yeniden çalıştırıldı:

```text
Ran 23 tests
OK
```

Bu testler; faz akımı toplamını, analitik R L cevabını, geri EMK tork güç bağıntısını, ileri ve ters mekanik hareketi, Hall ve encoder üretimini, akım filtresini, akım sönümünü, PWM karşılaştırmasını, sayısal çözüm yakınsamasını, 3 A demo PI cevabını, CRC'yi, bozuk veya eksik paketleri, sequence taşmasını ve paketler üzerinden yazılımsal kapalı çevrimi kapsıyor.

### STM32

Güncel STM32 proje klasöründe 9 Eylül tarihli ARM ELF, MAP ve LIST çıktıları bulunuyor. Proje belgeleri PC simulation kodunun tam ARM derlemesine dahil edildiğini belirtiyor.

Henüz fiziksel olarak doğrulanması gerekenler:

- STM32 ile PC arasında gerçek 4 Mbaud UART iletişimi.
- Rotor kilitli 0 A → 3 A kapalı çevrim cevabı.
- PI yükselme süresi, aşım, yerleşme süresi ve kalıcı hata.
- Duty doyumu ve anti windup davranışı.
- Rotor serbestken doğru Hall sırası, encoder yönü ve komütasyon.
- Paket kaybı, tekrar paket, CRC hatası ve timeout güvenliği.
- Gerçek motor parametreleriyle kalibrasyon.

## 12 Şu anda yapılması gereken deney

İlk deney rotor kilitli akım kontrol testidir. Bu deney mekanik hareketi devreden çıkararak yalnız PI ve elektrik modelini değerlendirir.

1. STM32'nin güncel `Real_motor_read_hall` projesini derleyip karta yükleyin.
2. PC simülatörünü `bldc_pc_sim/Baslat.bat` ile açın.
3. Bağlanmadan önce Rotor kilitli seçeneğini açın.
4. STM32 PI hedefini önce 0 A yapın.
5. UART bağlantısını kurun ve bağlantının senkron model adımlarına geçtiğini görün.
6. Hedefi 3 A yapın.
7. Akım, hedef ve duty grafiklerini kaydedin.
8. Yükselme süresi, aşım, yerleşme süresi, kalıcı hata ve duty doyumunu değerlendirin.
9. Kp ve Ki değerlerini kontrollü biçimde değiştirip sonuçları karşılaştırın.

Bu test geçtikten sonra rotor kilidi kaldırılır. İkinci deneyde rotorun hızlanması, Hall dizisinin `001, 101, 100, 110, 010, 011` sırasını izlemesi ve encoder sayımının doğru yönde ilerlemesi kontrol edilir.

## 13 Bu simülasyon neyi kanıtlar ve neyi kanıtlamaz

Simülasyon şu konularda güçlü kanıt sağlar:

- PI işareti ve temel kazanç davranışı.
- Akım referansını takip, doyum ve anti windup.
- Sektör seçimi ile faz akımları arasındaki mantıksal ilişki.
- Hall, encoder ve RPM verisinin kontrol akışına etkisi.
- UART hata ve timeout yönetimi.
- Farklı R, L, Ke, yük ve atalet değerlerinde yazılım davranışı.

Tek başına şu fiziksel özellikleri kanıtlamaz:

- MOSFET ve gate driver anahtarlaması.
- Dead time, shoot through ve gerçek faz gerilimleri.
- ADC zamanlaması, shunt yerleşimi, ölçüm gürültüsü ve kalibrasyon.
- Motor doygunluğu, sıcaklığa bağlı direnç, demir kayıpları ve cogging.
- EMI, termal sınırlar ve gerçek güç katı korumaları.

Bu nedenle HIL testi, gerçek motora geçmeden önce güçlü bir yazılım doğrulama aşamasıdır; gerçek güç katı kabul testinin yerine geçmez.

## 14 Kısa sözlük

| Terim | Basit anlamı |
|---|---|
| PI | Akım hatasına göre duty üreten oransal ve integral kontrolcü. |
| Duty | PWM'in açık kalma oranı. Ortalama faz gerilimini belirler. |
| HIL | Gerçek kontrol kartının sanal bir fiziksel sisteme bağlanarak sınanması. |
| Geri EMK | Rotor döndükçe motor sargılarında oluşan ve uygulanan gerilime karşı koyan gerilim. |
| Hall sektörü | Elektriksel turun altı adet 60 derecelik konum bölgesinden biri. |
| Encoder | Rotor konumunu ve hızını sayısal darbelerle bildiren sensör. |
| Lockstep | Her komuta tam bir model adımı ve bir cevap verilerek iki tarafın sırayla ilerlemesi. |
| CRC | Seri paketin bozulup bozulmadığını kontrol eden hata denetim değeri. |
| Session | PC yeniden bağlandığında eski paketleri yeni çalışmadan ayıran oturum kimliği. |

## 15 Son durum özeti

Çalışmanın yönü artık nettir: PI kontrolcüsü STM32 üzerinde kalacak, ayrıntılı üç faz motor ve sanal sensörler PC'de çalışacaktır. ESP32 çözümü başarılı bir ara çalışma olarak saklanmaktadır ancak güncel mimarinin parçası değildir. Yazılım modelleri ve protokoller hazırlanmış, otomatik testler geçmiştir. Sıradaki kritik kilometre taşı fiziksel STM32 PC UART bağlantısını kurup rotor kilitli 0 A → 3 A deneyini kaydetmektir.
