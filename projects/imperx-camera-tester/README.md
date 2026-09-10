# Imperx Kamera Test Arayüzü

Imperx Camera SDK ile çalışan basit bir Windows kamera test uygulamasıdır.

## Özellikler

- Kamera olmadan arayüzü sınamak için 4096 × 2160 kaynak çözünürlüğünü temsil eden demo modu
- Imperx SDK kamera seçicisi üzerinden gerçek kameraya bağlanma
- Canlı görüntüyü başlatma ve durdurma
- Gerçek çözünürlüğü ilk görüntü tamponundan otomatik okuma
- FPS ve toplam kare sayacı
- SDK kuyruk taşması ve eksik kare sayaçları
- Varsayılan 640 × 640 yazılımsal crop/zoom görünümü
- Kamerada 640 × 640 donanımsal ROI uygulayan; sağ alt, sol alt, sol üst ve sağ üst sırasıyla ilerleyen otomatik crop testi
- Tam görüntüde 8 FPS, crop görünümünde 20 FPS hedefli önizleme
- Manuel X/Y crop konumu, ayarlanabilir crop boyutu ve 1–10 saniye tarama aralığı
- PNG, JPG, BMP veya TIFF anlık görüntü kaydı
- GenICam parametre ağacını açma

## Gereksinimler

- Windows x64
- .NET Framework 4.8
- `C:\Program Files\Imperx\Imperx Camera SDK`

## Derleme ve çalıştırma

PowerShell içinde:

```powershell
dotnet build .\ImperxCameraTester.csproj -c Release
.\bin\Release\ImperxCameraTester.exe
```

Uygulama ilk açıldığında **Demo (kamera olmadan)** modundadır. Gerçek kamera geldiğinde bağlantı türüne uygun Imperx sürücüsünün kurulu olduğundan emin olun, **Gerçek Imperx kamera** modunu seçin ve **Kameraya Bağlan** düğmesine basın.

Crop testi için kameraya bağlandıktan sonra **640 × 640 crop görünümünü aç** seçeneğini işaretleyin. **Dört köşeyi otomatik kontrol et** açıkken ROI sırasıyla sağ alt, sol alt, sol üst ve sağ üst noktalarına gider. Her konumdaki bekleme süresi 1–10 saniye arasında ayarlanabilir ve varsayılan değer 3 saniyedir. Otomatik kontrolü kapatarak X/Y sürgüleriyle manuel inceleme yapılabilir.

GigE kamera kullanılıyorsa bilgisayarın ağ adaptörü ve kamera aynı alt ağda olmalıdır. Gerekirse SDK ile gelen `IpxGevConsole.exe` üzerinden cihazın görülüp görülmediği kontrol edilebilir.
