# Son iki hafta yol haritası

## Öncelik 1 — Fiziksel kapalı çevrim

- [ ] STM32 firmware'ini güncel kaynakla derle ve karta yükle
- [ ] PC simülatörüyle 4 Mbaud UART bağlantısını doğrula
- [ ] Rotor kilitli, 0 A başlangıç deneyini kaydet
- [ ] `0 A → 3 A` referans adımını uygula
- [ ] Yükselme süresi, aşım, yerleşme süresi ve kalıcı hatayı ölç
- [ ] Duty doyumu ve anti-windup davranışını kontrol et

## Öncelik 2 — Sanal mekanik hareket

- [ ] Rotor kilidini kaldır ve ileri hızlanmayı doğrula
- [ ] Hall sırasını `001 → 101 → 100 → 110 → 010 → 011` olarak gözle
- [ ] Encoder yönünü ve RPM hesabını doğrula
- [ ] Ters yön senaryosunu çalıştır

## Öncelik 3 — Hata senaryoları

- [ ] CRC hatası enjekte et
- [ ] Paket kaybı ve tekrar paket davranışını kaydet
- [ ] UART timeout sonrasında güvenli duruşu doğrula
- [ ] Bağlantı yeniden kurulduğunda session sıfırlamasını doğrula

## Öncelik 4 — Sunum ve teslim

- [ ] En iyi grafiklerden ekran görüntüleri ekle
- [ ] Gerçek motor parametreleri biliniyorsa modeli kalibre et
- [ ] Test sonuçlarını kısa bir tablo halinde README'ye işle
- [ ] Staj defterinin son iki haftasını tamamla
- [ ] Depoda şirket içi veya kişisel bilgi taraması yap

## Tamamlanma ölçütü

Depo; temiz bir bilgisayarda README yönergeleriyle kurulabiliyor, otomatik testler geçiyor ve en az bir gerçek STM32 ↔ PC kapalı çevrim deneyinin sonucu belgeleniyorsa ana hedef tamamlanmış kabul edilir.

