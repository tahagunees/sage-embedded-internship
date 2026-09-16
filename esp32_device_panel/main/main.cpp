#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/touch_sensor.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

// Klasik ESP32'nin dahili sicaklik okuma fonksiyonu librtc icindedir.
extern "C" uint8_t temprature_sens_read(void);

static const char *TAG = "DOKUNMATIK_DEMO";
static constexpr char WIFI_SSID[] = "ESP32-Panel";
static constexpr char WIFI_PASSWORD[] = "esp32panel";


class InternalTemperatureSensor {
public:
    void init() const {}

    float readCelsius() const {
        // Fonksiyon Fahrenheit dondurur; Celsius'a ceviriyoruz.
        const uint8_t fahrenheit = temprature_sens_read();
        return (static_cast<float>(fahrenheit) - 32.0F) / 1.8F;
    }
};

class Potentiometer {
private:
    adc_oneshot_unit_handle_t m_adc = nullptr;

public:
    void init() {
        // GPIO34 = ADC1 kanal 6; ADC1, Wi-Fi acikken de okunabilir.
        adc_oneshot_unit_init_cfg_t unitConfig{};
        unitConfig.unit_id = ADC_UNIT_1;
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&unitConfig, &m_adc));

        adc_oneshot_chan_cfg_t channelConfig{};
        channelConfig.atten = ADC_ATTEN_DB_12;
        channelConfig.bitwidth = ADC_BITWIDTH_12;
        ESP_ERROR_CHECK(adc_oneshot_config_channel(m_adc, ADC_CHANNEL_6, &channelConfig));
    }

    bool read(int &raw, int &percent) const {
        int total = 0;
        for (int sample = 0; sample < 8; ++sample) {
            int value = 0;
            if (adc_oneshot_read(m_adc, ADC_CHANNEL_6, &value) != ESP_OK) {
                return false;
            }
            total += value;
        }
        raw = total / 8;
        percent = (raw * 100 + 2047) / 4095;
        return true;
    }
};

class DimmableLed {
public:
    void init() const {
        ledc_timer_config_t timer{};
        timer.speed_mode = LEDC_LOW_SPEED_MODE;
        timer.duty_resolution = LEDC_TIMER_10_BIT;
        timer.timer_num = LEDC_TIMER_0;
        timer.freq_hz = 5000;
        timer.clk_cfg = LEDC_AUTO_CLK;
        ESP_ERROR_CHECK(ledc_timer_config(&timer));

        ledc_channel_config_t channel{};
        channel.gpio_num = GPIO_NUM_2;
        channel.speed_mode = LEDC_LOW_SPEED_MODE;
        channel.channel = LEDC_CHANNEL_0;
        channel.intr_type = LEDC_INTR_DISABLE;
        channel.timer_sel = LEDC_TIMER_0;
        channel.duty = 0;
        channel.hpoint = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&channel));
    }

    void setFromRaw(int raw) const {
        const uint32_t duty = static_cast<uint32_t>(raw) * 1023U / 4095U;
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
    }
};

static portMUX_TYPE g_dhtTimingMux = portMUX_INITIALIZER_UNLOCKED;

class Dht11Sensor {
private:
    gpio_num_t m_pin;

    bool waitWhileLevel(int level, uint32_t timeoutUs, uint32_t *elapsedUs = nullptr) const {
        const int64_t startedAt = esp_timer_get_time();
        while (gpio_get_level(m_pin) == level) {
            if ((esp_timer_get_time() - startedAt) > timeoutUs) {
                return false;
            }
        }
        if (elapsedUs != nullptr) {
            *elapsedUs = static_cast<uint32_t>(esp_timer_get_time() - startedAt);
        }
        return true;
    }

public:
    explicit Dht11Sensor(gpio_num_t pin) : m_pin(pin) {}

    void init() const {
        gpio_reset_pin(m_pin);
        gpio_set_direction(m_pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(m_pin, GPIO_PULLUP_ONLY);
    }

    bool read(float &temperatureCelsius, float &humidityPercent) const {
        uint8_t data[5]{};

        // DHT11 veri sayfasindaki baslatma dizisi: en az 18 ms LOW,
        // ardindan 20-40 us HIGH ve pini sensore birak.
        gpio_set_direction(m_pin, GPIO_MODE_OUTPUT);
        gpio_set_level(m_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(25));
        gpio_set_level(m_pin, 1);
        esp_rom_delay_us(40);
        gpio_set_direction(m_pin, GPIO_MODE_INPUT);

        bool ok = true;
        const char *errorStage = nullptr;
        int failedBit = -1;
        portENTER_CRITICAL(&g_dhtTimingMux);

        ok = waitWhileLevel(1, 200);
        if (!ok) errorStage = "sensor cevap vermedi (hat HIGH kaldi)";
        if (ok) {
            ok = waitWhileLevel(0, 200);
            if (!ok) errorStage = "sensor cevap LOW suresi doldu";
        }
        if (ok) {
            ok = waitWhileLevel(1, 200);
            if (!ok) errorStage = "sensor cevap HIGH suresi doldu";
        }

        for (int bit = 0; bit < 40 && ok; ++bit) {
            uint32_t highDuration = 0;
            ok = waitWhileLevel(0, 150);
            if (!ok) {
                errorStage = "veri biti LOW zaman asimi";
                failedBit = bit;
            }
            if (ok) {
                ok = waitWhileLevel(1, 150, &highDuration);
                if (!ok) {
                    errorStage = "veri biti HIGH zaman asimi";
                    failedBit = bit;
                }
            }
            if (ok && highDuration > 50) {
                data[bit / 8] |= static_cast<uint8_t>(1U << (7 - (bit % 8)));
            }
        }
        portEXIT_CRITICAL(&g_dhtTimingMux);

        const uint8_t checksum = static_cast<uint8_t>(data[0] + data[1] + data[2] + data[3]);
        if (!ok) {
            if (failedBit >= 0) {
                ESP_LOGW(TAG, "DHT11 hata: %s | bit=%d | GPIO seviyesi=%d",
                         errorStage, failedBit, gpio_get_level(m_pin));
            } else {
                ESP_LOGW(TAG, "DHT11 hata: %s | GPIO seviyesi=%d",
                         errorStage != nullptr ? errorStage : "bilinmeyen zaman asimi",
                         gpio_get_level(m_pin));
            }
            return false;
        }
        if (checksum != data[4]) {
            ESP_LOGW(TAG,
                     "DHT11 checksum hatasi: %02X %02X %02X %02X | gelen=%02X beklenen=%02X",
                     data[0], data[1], data[2], data[3], data[4], checksum);
            return false;
        }

        humidityPercent = static_cast<float>(data[0]) + static_cast<float>(data[1]) / 10.0F;
        temperatureCelsius = static_cast<float>(data[2] & 0x7F) + static_cast<float>(data[3]) / 10.0F;
        if ((data[2] & 0x80) != 0) {
            temperatureCelsius = -temperatureCelsius;
        }
        return true;
    }
};

class TouchButton {
private:
    touch_pad_t m_pad;
    uint16_t m_baseline;
    uint16_t m_touchThreshold;
    uint16_t m_releaseThreshold;
    bool m_touched;
    bool m_available;
    bool m_errorLogged;

public:
    // TOUCH_PAD_NUM0 = GPIO 4 pini
    explicit TouchButton(touch_pad_t pad)
        : m_pad(pad),
          m_baseline(0),
          m_touchThreshold(0),
          m_releaseThreshold(0),
          m_touched(false),
          m_available(true),
          m_errorLogged(false) {}

    void init() {
        // Dokunmatik donanımını başlat
        ESP_ERROR_CHECK(touch_pad_init());
        // Eşik 0: kesme kullanmadan değeri yazılımla okuyacağız.
        ESP_ERROR_CHECK(touch_pad_config(m_pad, 0));
    }

    uint16_t readRaw() {
        uint16_t val = 0;
        const esp_err_t result = touch_pad_read(m_pad, &val);
        if (result != ESP_OK) {
            m_available = false;
            if (!m_errorLogged) {
                ESP_LOGW(TAG,
                         "Dokunmatik okunamadi (%s). Web sunucusu calismaya devam edecek.",
                         esp_err_to_name(result));
                m_errorLogged = true;
            }
            return 0;
        }
        m_available = true;
        return val;
    }

    void calibrate(uint16_t sampleCount = 50) {
        ESP_LOGI(TAG, "Kalibrasyon basliyor. GPIO 4'e dokunmayin...");
        vTaskDelay(pdMS_TO_TICKS(1000));

        uint32_t total = 0;
        for (uint16_t i = 0; i < sampleCount; ++i) {
            total += readRaw();
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        m_baseline = static_cast<uint16_t>(total / sampleCount);
        if (m_baseline == 0) {
            m_touchThreshold = 0;
            m_releaseThreshold = 0;
            ESP_LOGW(TAG, "Dokunmatik devre disi; GPIO 4 baglantisini daha sonra kontrol edin.");
            return;
        }
        m_touchThreshold = static_cast<uint16_t>(m_baseline * 75U / 100U);
        m_releaseThreshold = static_cast<uint16_t>(m_baseline * 85U / 100U);

        ESP_LOGI(TAG,
                 "Kalibrasyon tamam: normal=%u dokunma_esigi=%u birakma_esigi=%u",
                 m_baseline,
                 m_touchThreshold,
                 m_releaseThreshold);
    }

    bool update(uint16_t rawValue) {
        if (!m_available || m_touchThreshold == 0) {
            m_touched = false;
            return false;
        }
        if (!m_touched && rawValue < m_touchThreshold) {
            m_touched = true;
        } else if (m_touched && rawValue > m_releaseThreshold) {
            m_touched = false;
        }

        return m_touched;
    }

    uint16_t getThreshold() const {
        return m_touchThreshold;
    }
};

struct DashboardState {
    uint16_t touchRaw;
    uint16_t touchThreshold;
    bool touched;
    bool ledOn;
    float chipTemperature;
    float ambientTemperature;
    float humidity;
    bool dhtValid;
    int potRaw;
    int potPercent;
    bool potValid;
};

static DashboardState g_dashboard{};
static portMUX_TYPE g_dashboardMux = portMUX_INITIALIZER_UNLOCKED;
static bool g_ledEnabled = true;

static const char WEB_PAGE[] = R"html(
<!doctype html>
<html lang="tr">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="theme-color" content="#0b1220">
  <title>ESP32 | Kontrol Merkezi</title>
  <style>
    :root { color-scheme: dark; font-family: Inter, ui-sans-serif, system-ui, -apple-system, "Segoe UI", sans-serif;
      --bg:#0b1220; --panel:#151f31; --line:#2b3a50; --text:#f3f7fd; --muted:#9aabc2;
      --cyan:#58d4e8; --green:#68d5a7; --blue:#6b8dff; }
    * { box-sizing:border-box; }
    body { margin:0; min-height:100vh; color:var(--text); background:radial-gradient(circle at 85% 0%,#1b3152 0,transparent 35%),var(--bg); }
    .shell { width:min(1120px,calc(100% - 40px)); margin:0 auto; padding:34px 0 44px; }
    .topbar,.brand,.live,.section-head,.card-head,.led-bottom,.footer { display:flex; align-items:center; justify-content:space-between; gap:12px; }
    .brand { justify-content:flex-start; font-size:14px; font-weight:800; letter-spacing:.11em; text-transform:uppercase; }
    .brand-icon { width:37px; height:37px; border:1px solid #4c6884; border-radius:12px; display:grid; place-items:center; color:var(--cyan); background:#20344d; font-size:20px; }
    .live { justify-content:flex-start; padding:9px 13px; border:1px solid var(--line); border-radius:999px; color:var(--muted); font-size:12px; background:#142035; }
    .dot { width:8px; height:8px; border-radius:50%; background:#f0ad65; box-shadow:0 0 0 4px #f0ad6526; }
    .live.online .dot { background:var(--green); box-shadow:0 0 0 4px #68d5a72a; }
    .intro { margin:52px 0 30px; }
    .eyebrow { color:var(--cyan); font-size:12px; font-weight:800; letter-spacing:.16em; text-transform:uppercase; }
    h1 { margin:11px 0 8px; font-size:clamp(33px,5vw,54px); line-height:1.08; letter-spacing:-.045em; }
    .intro p,.section-head p,.note,.footnote { color:var(--muted); }
    .intro p { margin:0; font-size:16px; }
    .section-head { margin:30px 0 13px; align-items:end; }
    h2 { font-size:17px; letter-spacing:-.02em; margin:0; }
    .section-head p { font-size:12px; margin:0; }
    .grid { display:grid; grid-template-columns:repeat(3,minmax(0,1fr)); gap:14px; }
    .card { min-width:0; padding:22px; border:1px solid var(--line); border-radius:20px; background:linear-gradient(145deg,#19263a,#141e2e); box-shadow:0 16px 40px #050b1660; }
    .card-head { align-items:flex-start; }
    .label { font-size:13px; font-weight:650; color:#c8d5e5; }
    .pin { color:var(--muted); font-size:11px; white-space:nowrap; }
    .icon { width:36px; height:36px; border:1px solid #344966; border-radius:11px; display:grid; place-items:center; font-size:19px; color:var(--cyan); background:#20334b; }
    .value { display:block; margin:23px 0 3px; font-size:39px; line-height:1; font-weight:780; letter-spacing:-.045em; font-variant-numeric:tabular-nums; }
    .unit { font-size:19px; font-weight:600; color:var(--muted); letter-spacing:0; }
    .note { font-size:12px; margin-top:11px; min-height:18px; }
    .track { height:9px; overflow:hidden; border-radius:99px; background:#2a3b50; margin-top:20px; }
    .fill { display:block; width:0; height:100%; border-radius:inherit; background:linear-gradient(90deg,#5088fa,#62d6e5); transition:width .3s ease; }
    .fill.green { background:linear-gradient(90deg,#41b38b,#7bddaa); }
    .wide { grid-column:span 2; }
    .led-card { background:linear-gradient(115deg,#1c2b45,#18243a 55%,#192840); }
    .led-status { display:flex; align-items:center; gap:10px; margin:20px 0 4px; font-size:30px; font-weight:780; letter-spacing:-.035em; }
    .lamp { width:16px; height:16px; border-radius:50%; background:#68788d; box-shadow:0 0 0 6px #68788d26; }
    .lamp.on { background:var(--green); box-shadow:0 0 0 6px #68d5a726,0 0 22px #68d5a766; }
    .led-bottom { align-items:end; margin-top:18px; }
    .led-bottom .note { margin:0; max-width:270px; line-height:1.5; }
    button { border:0; border-radius:12px; padding:12px 20px; min-height:44px; background:#6688f7; color:#071229; font:inherit; font-size:13px; font-weight:800; cursor:pointer; transition:background .2s,transform .2s; }
    button:hover { background:#8aa5ff; }
    button:active { transform:scale(.97); }
    button:disabled { cursor:wait; opacity:.6; }
    button:focus-visible { outline:3px solid var(--cyan); outline-offset:3px; }
    .touch-value { font-size:29px; }
    .touch-value.active { color:var(--green); }
    .footer { border-top:1px solid var(--line); margin-top:34px; padding-top:18px; color:var(--muted); font-size:12px; }
    .footer strong { color:#c8d5e5; font-weight:600; }
    @media(max-width:760px) { .grid { grid-template-columns:repeat(2,minmax(0,1fr)); } .wide { grid-column:span 2; } }
    @media(max-width:540px) { .shell { width:calc(100% - 28px); padding-top:20px; } .intro { margin:38px 0 25px; } .grid { grid-template-columns:1fr; } .wide { grid-column:auto; } .card { padding:19px; } .led-bottom { align-items:stretch; flex-direction:column; } button { width:100%; } .section-head p { display:none; } }
    @media(prefers-reduced-motion:reduce) { .fill,button { transition:none; } }
  </style>
</head>
<body>
<main class="shell">
  <header class="topbar">
    <div class="brand"><span class="brand-icon">⌁</span><span>ESP32 LAB</span></div>
    <div id="connection" class="live" role="status"><span class="dot"></span><span id="connectionText">Bağlanıyor</span></div>
  </header>
  <div class="intro"><span class="eyebrow">Cihaz kontrol merkezi</span><h1>Her şey kontrol altında.</h1><p>Sensörleri izle, dahili LED'i anında yönet.</p></div>

  <div class="section-head"><h2>Canlı ölçümler</h2><p>Veriler otomatik yenilenir</p></div>
  <section class="grid" aria-label="Canlı ölçümler">
    <article class="card"><div class="card-head"><div><div class="label">Potansiyometre</div><div class="pin">GPIO 34 · ADC</div></div><span class="icon">◉</span></div><span class="value"><span id="pot">--</span><span class="unit"> %</span></span><div id="potRaw" class="note">Ham değer: --</div><div class="track"><span id="potFill" class="fill"></span></div></article>
    <article class="card"><div class="card-head"><div><div class="label">LED parlaklığı</div><div class="pin">GPIO 2 · PWM</div></div><span class="icon">✦</span></div><span class="value"><span id="brightness">--</span><span class="unit"> %</span></span><div class="note">Potansiyometreyle ayarlanır</div><div class="track"><span id="brightnessFill" class="fill green"></span></div></article>
    <article class="card"><div class="card-head"><div><div class="label">Çip sıcaklığı</div><div class="pin">Dahili sensör</div></div><span class="icon">°</span></div><span class="value"><span id="temp">--.-</span><span class="unit"> °C</span></span><div class="note">Ortam sıcaklığı değildir</div></article>
  </section>

  <div class="section-head"><h2>Kontroller</h2><p>Dokunmatik pin veya panel üzerinden</p></div>
  <section class="grid" aria-label="Kontroller">
    <article class="card wide led-card"><div class="card-head"><div><div class="label">Dahili LED</div><div class="pin">GPIO 2 · Parlaklık: potansiyometre</div></div><span class="icon">✦</span></div><div class="led-status"><span id="lamp" class="lamp"></span><span id="led">Bekleniyor</span></div><div class="led-bottom"><p class="note">GPIO 4'e dokunarak da LED'i açıp kapatabilirsin.</p><button id="ledButton" type="button" onclick="toggleLed()">LED'i aç / kapat</button></div></article>
    <article class="card"><div class="card-head"><div><div class="label">Dokunmatik giriş</div><div class="pin">GPIO 4 · Touch 0</div></div><span class="icon">◌</span></div><span id="touch" class="value touch-value">Bekleniyor</span><div id="raw" class="note">Ham değer: -- · Eşik: --</div></article>
  </section>
  <footer class="footer"><span><strong>ESP32 LAB</strong> · Yerel kontrol paneli</span><span>192.168.4.1</span></footer>
</main>
<script>
const $ = id => document.getElementById(id);
let busy = false;
async function update() {
  try {
    const r = await fetch('/api/status', {cache:'no-store'});
    if (!r.ok) throw new Error('status');
    const s = await r.json();
    const percent = s.potValid ? Math.max(0, Math.min(100, s.potPercent)) : 0;
    const brightness = s.led ? percent : 0;
    $('pot').textContent = s.potValid ? percent : '--';
    $('potRaw').textContent = s.potValid ? 'Ham değer: ' + s.potRaw + ' / 4095' : 'GPIO 34 okunamadı';
    $('potFill').style.width = percent + '%';
    $('brightness').textContent = s.potValid ? brightness : '--';
    $('brightnessFill').style.width = brightness + '%';
    $('temp').textContent = Number(s.temperature).toFixed(1);
    $('touch').textContent = s.touched ? 'Dokunuldu' : 'Boşta';
    $('touch').classList.toggle('active', s.touched);
    $('raw').textContent = 'Ham değer: ' + s.raw + ' · Eşik: ' + s.threshold;
    $('led').textContent = s.led ? 'Etkin' : 'Kapalı';
    $('lamp').classList.toggle('on', s.led);
    $('connection').classList.add('online');
    $('connectionText').textContent = 'Canlı bağlantı';
  } catch (_) {
    $('connection').classList.remove('online');
    $('connectionText').textContent = 'Bağlantı bekleniyor';
  }
}
async function toggleLed() {
  if (busy) return;
  busy = true;
  $('ledButton').disabled = true;
  try {
    const r = await fetch('/api/led/toggle', {method:'POST'});
    if (!r.ok) throw new Error('toggle');
    await update();
  } catch (_) {
    $('connection').classList.remove('online');
    $('connectionText').textContent = 'Komut gönderilemedi';
  } finally {
    busy = false;
    $('ledButton').disabled = false;
  }
}
setInterval(update, 1000);
update();
</script>
</body>
</html>
)html";

static esp_err_t pageHandler(httpd_req_t *request) {
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(request, "Pragma", "no-cache");
    return httpd_resp_send(request, WEB_PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t statusHandler(httpd_req_t *request) {
    DashboardState snapshot;
    portENTER_CRITICAL(&g_dashboardMux);
    snapshot = g_dashboard;
    portEXIT_CRITICAL(&g_dashboardMux);

    char json[320];
    snprintf(json,
             sizeof(json),
             "{\"temperature\":%.2f,\"ambient\":%.2f,\"humidity\":%.2f,\"dhtValid\":%s,\"potRaw\":%d,\"potPercent\":%d,\"potValid\":%s,\"raw\":%u,\"threshold\":%u,\"touched\":%s,\"led\":%s}",
             snapshot.chipTemperature,
             snapshot.ambientTemperature,
             snapshot.humidity,
             snapshot.dhtValid ? "true" : "false",
             snapshot.potRaw,
             snapshot.potPercent,
             snapshot.potValid ? "true" : "false",
             snapshot.touchRaw,
             snapshot.touchThreshold,
             snapshot.touched ? "true" : "false",
             snapshot.ledOn ? "true" : "false");

    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, json);
}

static esp_err_t ledToggleHandler(httpd_req_t *request) {
    portENTER_CRITICAL(&g_dashboardMux);
    g_ledEnabled = !g_ledEnabled;
    g_dashboard.ledOn = g_ledEnabled;
    portEXIT_CRITICAL(&g_dashboardMux);

    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"ok\":true}");
}

static void startAccessPoint() {
    esp_err_t nvsResult = nvs_flash_init();
    if (nvsResult == ESP_ERR_NVS_NO_FREE_PAGES || nvsResult == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(nvsResult);
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t initConfig = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&initConfig));

    wifi_config_t wifiConfig{};
    memcpy(wifiConfig.ap.ssid, WIFI_SSID, sizeof(WIFI_SSID));
    memcpy(wifiConfig.ap.password, WIFI_PASSWORD, sizeof(WIFI_PASSWORD));
    wifiConfig.ap.ssid_len = strlen(WIFI_SSID);
    wifiConfig.ap.channel = 1;
    wifiConfig.ap.max_connection = 4;
    wifiConfig.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifiConfig.ap.pmf_cfg.capable = true;
    wifiConfig.ap.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifiConfig));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi hazir: %s | Sifre: %s", WIFI_SSID, WIFI_PASSWORD);
    ESP_LOGI(TAG, "Web paneli: http://192.168.4.1");
}

static httpd_handle_t startWebServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = nullptr;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    const httpd_uri_t pageUri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = pageHandler,
        .user_ctx = nullptr,
    };
    const httpd_uri_t statusUri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = statusHandler,
        .user_ctx = nullptr,
    };
    const httpd_uri_t ledToggleUri = {
        .uri = "/api/led/toggle",
        .method = HTTP_POST,
        .handler = ledToggleHandler,
        .user_ctx = nullptr,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &pageUri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &statusUri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ledToggleUri));
    return server;
}

// ==========================================
// 3. ANA UYGULAMA (Giriş Noktası)
// ==========================================
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Sistem Baslatiliyor...");

    InternalTemperatureSensor temperatureSensor;
    temperatureSensor.init();
    ESP_LOGI(TAG, "Dahili sicaklik sensoru hazir.");

    // DHT11 baglantisi incelenene kadar okumayi durduruyoruz.
    Potentiometer potentiometer;
    potentiometer.init();
    ESP_LOGI(TAG, "Potansiyometre hazir: GPIO 34");

    DimmableLed dimmableLed;
    dimmableLed.init();
    ESP_LOGI(TAG, "PWM dahili LED hazir: GPIO 2");

    // TOUCH_PAD_NUM0 = GPIO 4 pini
    TouchButton touch0(TOUCH_PAD_NUM0);
    touch0.init();
    touch0.calibrate();

    ESP_LOGI(TAG, "Dokunmatik hazir! GPIO 4 pinine dokunabilirsiniz.");

    startAccessPoint();
    startWebServer();

    bool lastState = false;
    uint8_t logCounter = 0;

    while (true) {
        uint16_t rawVal = touch0.readRaw();
        bool touched = touch0.update(rawVal);
        int potRaw = 0;
        int potPercent = 0;
        const bool potValid = potentiometer.read(potRaw, potPercent);
        portENTER_CRITICAL(&g_dashboardMux);
        const bool ledEnabled = g_ledEnabled;
        portEXIT_CRITICAL(&g_dashboardMux);
        dimmableLed.setFromRaw(potValid && ledEnabled ? potRaw : 0);

        // Her 500 ms'de bir ham değeri ve otomatik eşiği göster.
        if (++logCounter >= 10) {
            logCounter = 0;
            float chipTemperature = temperatureSensor.readCelsius();
            portENTER_CRITICAL(&g_dashboardMux);
            g_dashboard.touchRaw = rawVal;
            g_dashboard.touchThreshold = touch0.getThreshold();
            g_dashboard.touched = touched;
            g_dashboard.ledOn = g_ledEnabled;
            g_dashboard.chipTemperature = chipTemperature;
            g_dashboard.potRaw = potRaw;
            g_dashboard.potPercent = potPercent;
            g_dashboard.potValid = potValid;
            portEXIT_CRITICAL(&g_dashboardMux);
            ESP_LOGI(TAG,
                     "Ham=%u | Esik=%u | Durum=%s | Cip=%.2f C | Pot=%d (%d%%)",
                     rawVal,
                     touch0.getThreshold(),
                     touched ? "DOKUNULDU" : "BOSTA",
                     chipTemperature,
                     potRaw,
                     potPercent);
        }

        // Dokunma algılandığında LED durumunu tersle
        if (touched && !lastState) {
            portENTER_CRITICAL(&g_dashboardMux);
            g_ledEnabled = !g_ledEnabled;
            g_dashboard.ledOn = g_ledEnabled;
            const bool nowEnabled = g_ledEnabled;
            portEXIT_CRITICAL(&g_dashboardMux);
            ESP_LOGW(TAG, ">>> DOKUNULDU! Deger: %u | LED: %s",
                     rawVal, nowEnabled ? "ETKIN" : "KAPALI");
        }

        lastState = touched;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
