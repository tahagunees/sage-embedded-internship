# BLDC PC simülatörü

`Baslat.bat` dosyasını açarak uygulamayı çalıştırın. PC demo modu kart olmadan,
STM32 UART modu ise UART v3 uyumlu kart yazılımıyla kullanılır. V3 komutu,
STM32 hedef akımını da taşıdığı için Akım Kontrolü grafiğinde ölçüm ve hedef
aynı anda gösterilir.

## Grafikleri ayrıntılı inceleme

Ana ekrandaki herhangi bir grafiğe çift tıklayın. Açılan büyük pencerede:

- `100 ms`, `500 ms`, `1 s`, `5 s` ve `20 s` düğmeleri zaman aralığını seçer.
- `Tüm kayıt`, bellekte tutulan bütün örnekleri gösterir.
- Fare tekerleği imlecin bulunduğu noktaya yakınlaştırır veya uzaklaştırır.
- Sol tuşla sürüklemek geçmiş zamanda gezinir ve canlı takibi durdurur.
- `Canlıyı izle` düğmesi grafiği yeniden son örneğe bağlar.
- Fareyi eğrinin üzerinde gezdirince dikey imleç ve o andaki kesin değerler görünür.
- Alt satır, seçili aralıktaki minimum, maksimum ve ortalama değerleri gösterir.

Ana grafikler son 600 örneğin hızlı özetidir. Detay penceresi CSV kaydında da
kullanılan son 100.000 örneği inceler. 1 kHz UART akışında bu yaklaşık 100 saniyedir.
