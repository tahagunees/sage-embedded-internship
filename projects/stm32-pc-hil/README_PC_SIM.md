# STM32 + bilgisayarda BLDC motor simülasyonu

Varsayılan mod artık `Core/Inc/pc_sim.h` içindeki `PC_SIMULATION=1` ayarıdır.
PI STM32 üzerinde; üç faz elektrik modeli, tork, rotor, Hall ve encoder PC üzerinde
çalışır. Fiziksel motor/PWM/gate çıkışı üretilmez. Kartın Hall/encoder girişleri bu
modda kontrol döngüsünde kullanılmaz.

## Başlatma

1. STM32CubeIDE içinde bu projeyi Refresh (F5) yapın; ardından Clean Project ve
   Build Project uygulayın. Yeni `pc_sim.c` ve `pc_sim_link.c` dosyalarının
   `Core/Src` altında göründüğünü kontrol edin. Derlenen yazılımı karta yükleyin.
2. Haberleşme **LPUART1, PA2=TX, PA3=RX, 4.000.000 baud, 8N1** kullanır.
   USB-UART kullanılıyorsa STM32 PA2 → adaptör RX, PA3 ← adaptör TX, GND ↔ GND.
   Adaptör 3,3 V UART seviyelerini ve seçilen baud hızını desteklemelidir.
   ST-LINK sanal COM ancak kartın donanım bağlantıları PA2/PA3'e yönlendirilmişse
   kullanılabilir; USB bağlantısının tek başına bu yönlendirmeyi sağladığını varsaymayın.
3. `bldc_pc_sim/Baslat.bat` ile PC uygulamasını açın.
4. `STM32 — PI kartta / UART` seçin; `Tara`, ilgili COM portu, `4000000` ve
   `Bağlan / Ayır` düğmesini kullanın. Aynı COM portunu açan diğer uygulamaları kapatın.
5. İlk akım testi için **bağlanmadan önce** `Rotor kilitli` seçin.
   Bağlantı kurulunca model otomatik ilerler; UART modunda Başlat düğmesi gerekmez.
   Durum satırı `STM32 PI · senkron model adımları` olmalıdır.
6. Serbest rotor denemesinde bağlantıyı ayırın, `Sıfırla` yapın, rotor kilidini
   kaldırıp yeniden bağlanın. UART modunda rotor kilidi bağlantı anında uygulanır.

## Hedef ve kazançlar

CubeIDE debugger içindeki Live Expressions üzerinden:

| Değişken | Varsayılan | İşlev |
|---|---:|---|
| `g_control_inputs.target_current_a` | 3 | Hedef akım, A |
| `g_control_inputs.kp_percent_per_a` | 2 | Kp, %/A |
| `g_control_inputs.ki_percent_per_a_s` | 20 | Ki, %/(A·s) |
| `g_control_inputs.enabled` | 1 | 0: duty ve integral sıfır; 1: kontrol açık |
| `g_pc_reverse` | 0 | 1: ters yön |
| `g_pc_switched` | 0 | 1: PC modelinde anahtarlamalı PWM |

Kalıcı varsayılan hedef/kazançlar `CURRENT_PI_DEFAULT_INPUTS` içinden değiştirilebilir.
PC penceresindeki hedef/Kp/Ki/ters yön/PWM kontrolleri PC demo moduna aittir;
UART modunda bu ayarları STM32 tarafında yapın. PC'deki Motor parametreleri ise
gerçekten kullanılan sanal motoru belirler; STM32'deki R/L/Ke ayarları PC modelini
değiştirmez. `g_control_state.fake_current_a` alanı isim uyumluluğu için korunmuştur,
bu modda **PC'den gelen ölçüm akımını** gösterir. `real_rpm` de PC rotor hızıdır.
`back_emf_v` ve `applied_voltage_v` PI modülünün yaklaşık tanı değerleridir;
PC'nin üç faz motor çözümünün çıktıları olarak yorumlanmamalıdır.

## Zamanlama ve bağlantı

- UART v3, `5A A5` başlangıç, little-endian alanlar, CRC16-CCITT (başlangıç FFFF).
- Komut: 30 bayt; geri bildirim: 68 bayt. STM32 hedef akımı komutta PC'ye
  gönderilir ve Akım Kontrolü grafiğinde yeşil hedef çizgisi olarak gösterilir.
  PC `simulator/protocol.py` ile uyumludur.
- Her onaylanan komut 1 ms model zamanı ilerletir; PI her 10 ms **model zamanı**
  çalışır. Gerçek zaman hızı bilgisayar ve USB gecikmesine bağlıdır.
- 50 ms yanıt yoksa aynı komut aynen gönderilir. Yinelenen komut modeli ikinci
  kez ilerletmez; yinelenen geri bildirim PI'yi tekrar çalıştırmaz.
- 500 ms yeni geri bildirim yoksa STM32 duty/integrali sıfırlar ve yeni komut
  üretimini durdurur. PC de komut gelmediğinde model zamanını dondurur.
- Devam etmek için PC'de bağlantıyı ayırıp yeniden bağlayın (yeni session).
  Motor durumunu da temizlemek için yeniden bağlamadan önce `Sıfırla` kullanın.
  Debugger'da uzun süre duraklatmak da bu zaman aşımını tetikleyebilir.
- PC aşırı akım hatasında enable/duty sıfırlanır; PC model hatası için `Sıfırla` gerekir.
- UART RX, boş DMA1 Channel 4 üzerinden dairesel tampon kullanır. Bu kanal başka
  bir çevre birimine verilmemelidir. Kullanıcı kodunda başlatıldığı için `.ioc`
  dosyasına yeni DMA ataması yapmak gerekmez.

`g_pc_sim` içinde session, sıra numarası, model zamanı, encoder, Hall, sektör,
durum, hata ve tekrar gönderim sayacı görülebilir. `stopped=1` zaman aşımıdır.

## Eski fiziksel Hall modu

`pc_sim.h` içindeki varsayılanı `0` yapın veya derleyiciye `PC_SIMULATION=0`
tanımlayın ve yeniden derleyin. Önceki fiziksel Hall/TIM2 okuma, kart içindeki
sanal akım modeli ve `DATA ...` metin telemetrisi korunmuştur.
Bu mod PC motor simülatörünün UART moduyla uyumlu değildir.
Eski akışın ayrıntıları `README_CURRENT_PI_SIM.md` içindedir.

## Doğrulama kapsamı

İki mod için tam ARM firmware derlemesi ve link kontrolü yapılır.
`tests/pc_sim_harness.c`, donanımdan bağımsız protokol ve PI testi içindir;
firmware'in `Core/Src` dizinine taşınmamalıdır. Testte derlenen ARM C kodu
emülatörde çalıştırılıp PC'nin gerçek Python motor/protokol koduna bağlanır.
Fiziksel STM32, USB-UART ve kablo üzerinde bağlantı testi ayrıca gereklidir.
