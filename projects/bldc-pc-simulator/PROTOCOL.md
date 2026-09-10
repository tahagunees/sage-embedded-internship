# PC motor simülatörü UART v3

Eski ESP32 v1 ve mevcut STM32 ASCII DATA protokolüyle uyumlu değildir.
8N1, flow control yok; arayüzde baud seçilir, varsayılan 4 Mbaud.
4 Mbaud desteklemeyen dönüştürücü için iki uçta aynı düşük baud seçilebilir;
senkron simülasyon daha yavaş çalışır.

Başlık 6 bayt: sync uint16=0xA55A (telde 5A A5), version uint8=3,
type uint8, payload_length uint16. Sonunda uint16 CRC bulunur.
Çok baytlı alanlar little-endian. CRC-16/CCITT-FALSE, poly1021/initFFFF,
refin/refout=false/xorout0. Sync hariç, version'dan payload sonuna kadar.
CRC de little-endian. Referans `123456789` → 29B1.

## Komut: type=0x10, payload=22, toplam=30 bayt

| Paket ofseti | Tip | Alan |
|---:|---|---|
| 0..5 | başlık | sync/version/type/length |
| 6 | uint32 | sequence |
| 10 | uint32 | session (PC feedback'ten alınır) |
| 14 | uint32 | step_us = 1000 |
| 18 | uint16 | duty_x100, 0..10000 |
| 20 | uint8 | uygulanan sektör 0..6; 0=boş |
| 21 | uint8 | flags: bit0 enabled, bit1 reverse, bit2 brake |
| 22 | uint8 | PWM mode: 0 ortalama, 1 anahtarlamalı |
| 23 | uint8 | reserved=0 |
| 24 | int32 | STM32 hedef akımı (mA), 0..20000 |
| 28 | uint16 | CRC |

Sektörü PC değil **STM32** seçer. Reverse, aynı sektörün pozitif/negatif
fazlarını takas eder. Brake, enabled=1 iken tüm terminalleri low'a bağlar.
Flags'ın diğer bitleri ve geçersiz alanlar reddedilir.

## Feedback: type=0x11, payload=60, toplam=68 bayt

| Paket ofseti | Tip | Alan |
|---:|---|---|
| 0..5 | başlık | sync/version/type/length |
| 6 | uint32 | kabul edilen sequence; başlangıçta FFFFFFFF |
| 10 | uint32 | PC session kimliği |
| 14 | uint64 | simülasyon zamanı (us) |
| 22 | int32 | ia_ma |
| 26 | int32 | ib_ma |
| 30 | int32 | ic_ma |
| 34 | int32 | current_ma: PI için filtreli aktif çift ölçümü |
| 38 | int32 | DC bara akımı (mA) |
| 42 | int32 | mekanik rpm_x10, işaretli |
| 46 | int32 | encoder toplam sayımı, modulo 2^32 |
| 50 | int32 | elektromanyetik tork (mikro Nm) |
| 54 | uint8 | Hall raw: 001,101,100,110,010,011 |
| 55 | uint8 | rotorun gerçek sanal sektörü 1..6 |
| 56 | uint8 | encoder AB: 00,01,11,10; A bit1 / B bit0 |
| 57 | uint8 | status |
| 58 | uint32 | rx_error_count |
| 62 | uint32 | duplicate_count |
| 66 | uint16 | CRC |

Status: bit0 geçerli komut kabul edildi ve bağlantı timeout değil;
bit1 son komutta enabled; bit2 motor koruma fault'u;
bit3 500 ms duvar saati bağlantı timeout; bit4 lockstep (daima1);
bit6 rotor kilitli; diğerleri0. Enabled biti fault sırasında çıkışın
gerçekten sürüldüğünü garanti etmez. Sayısal mA/RPM/tork alanları int32'ye
doyurulur; yalnız encoder modüler sayımdır. İç model değerleri kırpılmaz.

İlk bağlantıda PC status bit0=0 ve başlangıç sensörleriyle READY feedback'i
yollar; komut yokken saniyede bir tekrarlar. STM32 yeni session gördüğünde
PI integralini ve bekleyen komutu sıfırlamalı, yeni session ile sequence=0
komutunu başlatmalıdır. PC ilk sequence'i herhangi bir uint32 olarak kabul eder.

Sonrasında yalnız `next = last+1 (mod 2^32)` kabul edilir. Aynı sequence ve
aynı payload tekrarında önceki cevap **aynen** tekrar gönderilir, model ikinci
kez ilerlemez. Dolayısıyla duplicate sayacındaki artış bir sonraki yeni
feedback'te görünür. Aynı sequence/farklı payload reddedilir. Eski sequence ve
ileri sıçrama reddedilir; lockstep'te fizik adımları sessizce atlanmaz.
Bozuk CRC veya eksik çerçeve modeli ilerletmez. UART parser'ı en fazla 68 bayt tutar.

STM32 bir cevabı kaçırırsa aynı komutu değiştirmeden tekrar yollamalıdır.
PC 500 ms yeni komut görmeyince modeli dondurur; timeout feedback'i periyodik
gönderir. STM32 timeout status'undaki ölçümü yeni PI adımı saymamalıdır.
Devam etmek için sıradaki sequence veya oturumu kapat/aç ile yeni session gerekir.
PC modelini sıfırlamak UART'ı kapatır; yeniden bağlantı yeni session üretir.

1 ms model adımı her feedback'te ilerler; mevcut STM32 PI'si her 10 **yeni**
feedback'te sabit dt=10 ms çalıştırılmalıdır. Kontrol loop'u HAL_GetTick
duvar saatine bağlı bırakılırsa bu senkron testin amacı bozulur.
