# STM32CubeIDE ile açma

Güncel CubeIDE projesi **SageStaj/STM32_BldcHall** klasöründedir: **BldcHall_G491RE**. Eski `Firmware` çalışma alanı kaydıyla çakışmaması için ayrı konum kullanılır. `main.c` bu klasördeki tek kaynak dosyasına bağlıdır.

1. CubeIDE'de **File → Import → General → Existing Projects into Workspace** seç.
2. **Select root directory** alanına şu klasörü gir:

   `projects/bldc-hall-monitor/Firmware`

3. Listede **BldcHall_G491RE** işaretli olsun. **Copy projects into workspace** kapalı kalsın; böylece aynı kaynak dosyaları düzenlenir.
4. **Finish** ile aç. Solda projeyi genişletip **main.c** dosyasına çift tıkla.
5. **Project → Build Project** veya **Ctrl+B** ile derle.

Doğrudan `STM32_BldcHall` klasörünü seç. `BldcHallMonitor` veya `Firmware` klasörünü yeniden içeri aktarma. `.project` dosyasına metin dosyası olarak çift tıklamak projeyi içeri aktarmaz.

## Dosyalar

- `main.c`: Hall okuma, düğme simülasyonu ve USB seri haberleşme kodu.
- `startup_stm32g491xx.s`: STM32G491 başlangıç ve kesme vektörleri.
- `system_stm32g4xx.c`: ST'nin işlemci başlangıç ayarları.
- `STM32G491RE.ld`: Flash / RAM yerleşimi.
- `Drivers/CMSIS`: bu projeyle kullanılan başlıklar; orijinal lisanslar dahil.
- `.project` ve `.cproject`: hazır CubeIDE yapılandırması.

Debug çıktısı: `Debug/BldcHall_G491RE.elf`.
Release çıktısı: `Release/BldcHall_G491RE.elf`.

Bu proje CMSIS tabanlıdır; `.ioc` bulunmaz ve CubeMX ile kod üretimi gerektirmez. C# Windows arayüzü ayrı bir Visual Studio projesidir; CubeIDE'de derlenmez.

CubeIDE 2.2.0 ile Debug derlemesi doğrulandı: **0 hata, 0 uyarı**. Bu derleme kontrolü karta yeniden yazılım yüklemez.
