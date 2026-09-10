using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Ports;
using System.Linq;
using System.Text;
using System.Windows.Forms;

namespace BldcHallMonitor
{
    public sealed class MainForm : Form
    {
        private readonly Color ink = Color.FromArgb(26, 44, 63), muted = Color.FromArgb(97, 115, 134), teal = Color.FromArgb(0, 139, 129);
        private readonly ComboBox portChoice = new ComboBox { Width = 92, DropDownStyle = ComboBoxStyle.DropDownList };
        private readonly Button connect = new Button(), demo = new Button(), previous = new Button(), next = new Button(), auto = new Button(), mode = new Button();
        private readonly Label banner = new Label(), stepLabel = new Label(), angleLabel = new Label(), hallLabel = new Label(), directionLabel = new Label(), countsLabel = new Label(), footer = new Label();
        private readonly Label[] hallLights = new Label[3];
        private readonly RotorView rotor = new RotorView { Dock = DockStyle.Fill };
        private readonly SignalView signals = new SignalView { Dock = DockStyle.Fill };
        private readonly ListView events = new ListView { Dock = DockStyle.Fill, View = View.Details, FullRowSelect = true, GridLines = false, BorderStyle = BorderStyle.None };
        private readonly Timer timer = new Timer { Interval = 30 };
        private readonly Stopwatch clock = Stopwatch.StartNew();
        private readonly List<string> csv = new List<string>();
        private SerialPort port;
        private DemoMotor demoMotor;
        private HallFrame frame;
        private string receive = "";
        private bool discardLine, automatic;
        private long lastData = -10000, lastDemo, lastAuto;
        private uint malformed;
        private bool Live { get { return frame != null && clock.ElapsedMilliseconds - lastData < 1500; } }

        public MainForm()
        {
            Text = "BLDC Hall İzleyici · NUCLEO-G491RE";
            Font = new Font("Segoe UI", 10);
            ForeColor = ink; BackColor = Color.FromArgb(241, 245, 248);
            ClientSize = new Size(1120, 850); MinimumSize = new Size(1000, 790);
            StartPosition = FormStartPosition.CenterScreen;
            AutoScaleMode = AutoScaleMode.Dpi;
            var root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 8, Padding = new Padding(20, 12, 20, 8) };
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 74));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 48));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 68));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 48));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 122));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 32));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));
            Controls.Add(root);
            var heading = new Panel { Dock = DockStyle.Fill };
            heading.Controls.Add(new Label { Text = "BLDC / HALL İZLEYİCİ", Font = new Font("Segoe UI", 23, FontStyle.Bold), AutoSize = true, Location = new Point(0, 0) });
            heading.Controls.Add(new Label { Text = "6 adım konum takibi   ·   NUCLEO-G491RE   ·   Canlı sensör geri bildirimi", AutoSize = true, ForeColor = muted, Location = new Point(2, 44) });
            root.Controls.Add(heading, 0, 0);

            var connectionRow = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, Padding = new Padding(0, 4, 0, 0) };
            connectionRow.Controls.Add(new Label { Text = "Kart portu", AutoSize = true, Margin = new Padding(0, 7, 10, 0) });
            connectionRow.Controls.Add(portChoice);
            AddButton(connectionRow, new Button(), "Yenile", 80, (s, e) => RefreshPorts());
            AddButton(connectionRow, connect, "Karta bağlan", 145, (s, e) => ToggleConnection());
            AddButton(connectionRow, demo, "Bilgisayarda dene", 168, (s, e) => StartDemo());
            AddButton(connectionRow, new Button(), "Kullanım kılavuzu", 165, (s, e) => ShowHelp());
            root.Controls.Add(connectionRow, 0, 1);
            banner.Dock = DockStyle.Fill; banner.TextAlign = ContentAlignment.MiddleLeft;
            banner.Padding = new Padding(12, 0, 0, 0); banner.Margin = new Padding(0, 3, 0, 5);
            root.Controls.Add(banner, 0, 2);

            var central = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1, Margin = new Padding(0) };
            central.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 53)); central.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 47));
            central.Controls.Add(rotor, 0, 0);
            var stats = new TableLayoutPanel { Dock = DockStyle.Fill, BackColor = Color.White, ColumnCount = 1, RowCount = 7, Padding = new Padding(18, 10, 10, 8) };
            stats.RowStyles.Add(new RowStyle(SizeType.Absolute, 21));
            stats.RowStyles.Add(new RowStyle(SizeType.Percent, 35));
            stats.RowStyles.Add(new RowStyle(SizeType.Absolute, 29));
            stats.RowStyles.Add(new RowStyle(SizeType.Absolute, 45));
            stats.RowStyles.Add(new RowStyle(SizeType.Absolute, 26));
            stats.RowStyles.Add(new RowStyle(SizeType.Absolute, 27));
            stats.RowStyles.Add(new RowStyle(SizeType.Percent, 65));
            stats.Controls.Add(new Label { Text = "ANLIK KONUM", ForeColor = muted, AutoSize = true }, 0, 0);
            StyleLabel(stepLabel, 27, true); stats.Controls.Add(stepLabel, 0, 1);
            StyleLabel(angleLabel, 11, false); stats.Controls.Add(angleLabel, 0, 2);
            var lights = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false };
            for (int i = 0; i < 3; i++)
            {
                hallLights[i] = new Label { Text = "H" + (i + 1) + "  —", Width = 100, Height = 34, TextAlign = ContentAlignment.MiddleCenter, Margin = new Padding(0, 0, 10, 0), Font = new Font("Segoe UI", 12, FontStyle.Bold) };
                lights.Controls.Add(hallLights[i]);
            }
            stats.Controls.Add(lights, 0, 3);
            StyleLabel(hallLabel, 11, false); stats.Controls.Add(hallLabel, 0, 4);
            StyleLabel(directionLabel, 11, true); stats.Controls.Add(directionLabel, 0, 5);
            StyleLabel(countsLabel, 10, false); stats.Controls.Add(countsLabel, 0, 6);
            central.Controls.Add(stats, 1, 0); root.Controls.Add(central, 0, 3);
            rotor.StepSelected += selected => Send("S" + selected);

            var controls = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, Padding = new Padding(0, 6, 0, 0) };
            AddButton(controls, previous, "← Geri", 100, (s, e) => Send("P"));
            AddButton(controls, next, "İleri →", 100, (s, e) => Send("N"));
            AddButton(controls, auto, "Otomatik test", 145, (s, e) => { automatic = !automatic; lastAuto = clock.ElapsedMilliseconds; UpdateStatus(); });
            AddButton(controls, new Button(), "Sayacı sıfırla", 135, (s, e) => Send("R"));
            AddButton(controls, mode, "Gerçek Hall girişleri", 183, (s, e) => { automatic = false; Send(frame != null && frame.Mode == "HALL" ? "M0" : "M1"); });
            AddButton(controls, new Button(), "CSV kaydet", 123, (s, e) => SaveCsv());
            root.Controls.Add(controls, 0, 4);
            root.Controls.Add(signals, 0, 5);
            events.Columns.Add("Zaman", 115); events.Columns.Add("Kaynak", 140); events.Columns.Add("Hall", 70); events.Columns.Add("Adım", 65); events.Columns.Add("Değişiklik", 570);
            events.Margin = new Padding(0, 8, 0, 0); root.Controls.Add(events, 0, 6);
            footer.Dock = DockStyle.Fill; footer.TextAlign = ContentAlignment.MiddleLeft; footer.ForeColor = muted; footer.Font = new Font("Segoe UI", 9);
            root.Controls.Add(footer, 0, 7);
            RefreshPorts(); UpdateStatus();
            timer.Tick += (s, e) => Tick(); timer.Start();
            FormClosing += (s, e) => { timer.Stop(); ClosePort(); };
        }

        private void StyleLabel(Label label, float size, bool bold)
        {
            label.Dock = DockStyle.Fill; label.TextAlign = ContentAlignment.MiddleLeft;
            label.Font = new Font("Segoe UI", size, bold ? FontStyle.Bold : FontStyle.Regular); label.Margin = new Padding(0);
        }
        private void AddButton(Control parent, Button button, string title, int width, EventHandler click)
        {
            button.Text = title; button.Size = new Size(width, 32); button.FlatStyle = FlatStyle.Flat;
            button.BackColor = Color.White; button.FlatAppearance.BorderColor = Color.FromArgb(204, 217, 226);
            button.Margin = new Padding(0, 0, 8, 0); button.Click += click; parent.Controls.Add(button);
        }
        private void RefreshPorts()
        {
            string selected = portChoice.Text;
            portChoice.Items.Clear(); portChoice.Items.AddRange(SerialPort.GetPortNames().OrderBy(x => x).ToArray());
            if (portChoice.Items.Contains(selected)) portChoice.SelectedItem = selected;
            else if (portChoice.Items.Contains("COM3")) portChoice.SelectedItem = "COM3";
            else if (portChoice.Items.Count > 0) portChoice.SelectedIndex = 0;
        }
        private void ResetSession()
        {
            automatic = false; frame = null; lastData = -10000; receive = ""; discardLine = false; malformed = 0; signals.Clear();
        }
        private void ToggleConnection()
        {
            if (port != null) { ClosePort(); ResetSession(); UpdateStatus(); return; }
            if (string.IsNullOrWhiteSpace(portChoice.Text)) { MessageBox.Show("Nucleo'yu USB ile bağla ve Yenile'ye bas.", "Port bulunamadı"); return; }
            demoMotor = null; ResetSession();
            try
            {
                port = new SerialPort(portChoice.Text, 115200, Parity.None, 8, StopBits.One) { NewLine = "\n", DtrEnable = false, RtsEnable = false, ReadTimeout = 50, WriteTimeout = 200, Encoding = Encoding.ASCII };
                port.Open(); port.DiscardInBuffer(); port.Write("?\n");
                Log("Bağlantı", "—", "—", port.PortName + " açıldı; kart verisi bekleniyor.");
            }
            catch (Exception ex) { ClosePort(); MessageBox.Show(ex.Message, "Bağlantı açılamadı"); }
            UpdateStatus();
        }
        private void StartDemo()
        {
            ClosePort(); ResetSession(); demoMotor = new DemoMotor();
            Log("Bilgisayar demo", "—", "—", "Kart kullanılmadan arayüz denemesi başlatıldı.");
            Accept(demoMotor.Read()); UpdateStatus();
        }
        private void ClosePort()
        {
            var closing = port; port = null; automatic = false;
            if (closing != null) { try { closing.Close(); } catch { } closing.Dispose(); }
        }
        private void Send(string command)
        {
            if (!Live) return;
            if (demoMotor != null) { demoMotor.Command(command); Accept(demoMotor.Read()); }
            else if (port != null)
            {
                try { port.Write(command + "\n"); }
                catch (Exception ex) { ConnectionError(ex); }
            }
        }
        private void ConnectionError(Exception ex)
        {
            Log("Bağlantı", "—", "—", "Bağlantı kesildi: " + ex.Message);
            ClosePort(); lastData = -10000; UpdateStatus();
        }
        private void Tick()
        {
            long now = clock.ElapsedMilliseconds;
            if (port != null)
            {
                try
                {
                    int pending = port.BytesToRead;
                    if (pending > 0)
                    {
                        var buffer = new byte[Math.Min(pending, 8192)];
                        int received = port.Read(buffer, 0, buffer.Length);
                        for (int i = 0; i < received; i++)
                        {
                            char c = (char)buffer[i];
                            if (c == '\n')
                            {
                                HallFrame incoming;
                                if (!discardLine && HallFrame.TryParse(receive, out incoming)) Accept(incoming);
                                else ++malformed;
                                receive = ""; discardLine = false;
                            }
                            else if (!discardLine && receive.Length < 200) receive += c;
                            else { receive = ""; discardLine = true; }
                        }
                    }
                }
                catch (TimeoutException) { }
                catch (Exception ex) { ConnectionError(ex); }
            }
            if (demoMotor != null && now - lastDemo >= 100) { Accept(demoMotor.Read()); lastDemo = now; }
            if (automatic && Live && now - lastAuto >= 500) { Send("N"); lastAuto = now; }
            if (!Live) automatic = false;
            UpdateStatus(); signals.Invalidate();
        }
        private void Accept(HallFrame incoming)
        {
            bool changed = frame == null || incoming.Hall != frame.Hall || incoming.Mode != frame.Mode || incoming.RelativeSteps != frame.RelativeSteps || incoming.Faults != frame.Faults || incoming.ButtonPresses != frame.ButtonPresses;
            bool restarted = frame != null && incoming.SequenceNumber < frame.SequenceNumber && frame.SequenceNumber - incoming.SequenceNumber < uint.MaxValue / 2;
            if (restarted) { signals.Clear(); Log("Kart", "—", "—", "Kart yeniden başlatıldı; göreli konum referansı yenilendi."); }
            frame = incoming; lastData = clock.ElapsedMilliseconds; signals.Add(incoming.Hall);
            if (changed || restarted)
            {
                string note = incoming.Step == 0 ? "Geçersiz Hall durumu: bağlantıları kontrol et." : incoming.Direction > 0 ? "İleri yönde geçiş" : incoming.Direction < 0 ? "Geri yönde geçiş" : "Konum / durum güncellendi";
                Log(demoMotor != null ? "Bilgisayar demo" : incoming.Mode == "SIM" ? "Nucleo test" : "Gerçek Hall", incoming.Bits, incoming.Step.ToString(), note);
            }
        }
        private void UpdateStatus()
        {
            bool live = Live, isDemo = demoMotor != null;
            bool sim = live && frame.Mode == "SIM";
            next.Enabled = previous.Enabled = auto.Enabled = sim;
            mode.Enabled = live && !isDemo;
            mode.Text = live && frame.Mode == "HALL" ? "Kart testine dön" : "Gerçek Hall girişleri";
            auto.Text = automatic ? "Testi durdur" : "Otomatik test";
            connect.Text = port != null ? "Bağlantıyı kes" : "Karta bağlan";
            portChoice.Enabled = port == null;
            rotor.Step = frame == null ? 0 : frame.Step; rotor.IsLive = live; rotor.AllowSelection = sim; rotor.Invalidate();
            banner.BackColor = live ? Color.FromArgb(221, 243, 237) : Color.FromArgb(255, 241, 213);
            banner.ForeColor = live ? Color.FromArgb(0, 103, 89) : Color.FromArgb(137, 93, 22);
            banner.Text = live ? isDemo ? "BİLGİSAYAR DEMOSU · İleri / Geri ile dene. Bu modda kart kullanılmıyor."
                : frame.Mode == "SIM" ? "NUCLEO CANLI · Kartın mavi B1 düğmesine bas: her basış bir sonraki adım."
                : "GERÇEK HALL · PA0 / PA1 / PA4 girişleri okunuyor. Test komutları devre dışı."
                : port != null ? "VERİ BEKLENİYOR · Kart yazılımını ve bağlantıyı kontrol et; canlı konum henüz doğrulanmadı."
                : "BAĞLANTI YOK · Karta bağlan veya Bilgisayarda dene ile arayüzü test et.";
            stepLabel.Text = live ? frame.Step == 0 ? "Geçersiz durum" : "Adım " + frame.Step + " / 6" : "Veri bekleniyor";
            angleLabel.Text = live && frame.Step > 0 ? ((frame.Step - 1) * 60) + "° – " + (frame.Step * 60) + "° elektriksel bölge*" : "Elektriksel konum: —";
            hallLabel.Text = live ? "Hall kodu: " + frame.Bits + "   ·   H1 H2 H3" : "Hall kodu: —";
            directionLabel.Text = live ? "Son hareket: " + (frame.Direction > 0 ? "İleri →" : frame.Direction < 0 ? "← Geri" : "Yok / bekliyor") : "Son hareket: —";
            countsLabel.Text = live ? "Göreli adım: " + frame.RelativeSteps + "    |    Düğme: " + frame.ButtonPresses + "\nHata / atlanan geçiş: " + frame.Faults + "\n* Örnek sıra; mekanik mil açısı değildir." : "Veriler bağlantı kurulduğunda görünür.\nKart testinde motor gerekmez.";
            for (int i = 0; i < 3; i++)
            {
                bool high = live && ((frame.Hall >> (2 - i)) & 1) != 0;
                hallLights[i].Text = "H" + (i + 1) + "   " + (live ? high ? "1" : "0" : "—");
                hallLights[i].BackColor = high ? teal : Color.FromArgb(233, 239, 245); hallLights[i].ForeColor = high ? Color.White : muted;
            }
            footer.Text = live ? "Son veri: " + (clock.ElapsedMilliseconds - lastData) + " ms önce   ·   Paket: " + frame.SequenceNumber + "   ·   Bozuk satır: " + malformed + "   ·   USB 115200 baud"
                : "Hall sensörleri konumu 6 elektriksel bölgede gösterir. Örnek sıra: 001 → 101 → 100 → 110 → 010 → 011";
        }
        private void Log(string source, string hall, string step, string note)
        {
            string time = DateTime.Now.ToString("HH:mm:ss.fff");
            events.Items.Insert(0, new ListViewItem(new[] { time, source, hall, step, note }));
            if (events.Items.Count > 250) events.Items.RemoveAt(events.Items.Count - 1);
            csv.Add(string.Join(",", new[] { DateTime.Now.ToString("o"), source, hall, step, note }.Select(x => "\"" + x.Replace("\"", "\"\"") + "\"")));
            if (csv.Count > 10000) csv.RemoveRange(0, 1000);
        }
        private void SaveCsv()
        {
            using (var dialog = new SaveFileDialog { Filter = "CSV dosyası|*.csv", FileName = "hall-kayit-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") + ".csv" })
                if (dialog.ShowDialog(this) == DialogResult.OK)
                    try { File.WriteAllLines(dialog.FileName, new[] { "Zaman,Kaynak,Hall,Adim,Aciklama" }.Concat(csv), new UTF8Encoding(true)); }
                    catch (Exception ex) { MessageBox.Show(ex.Message, "Kayıt yapılamadı"); }
        }
        private void ShowHelp()
        {
            MessageBox.Show("1. Arayüzü tek başına denemek için Bilgisayarda dene'ye bas.\n2. Kart yazılımı yüklendikten sonra COM portunu seç ve Karta bağlan'a bas.\n3. Mavi B1 düğmesi her basışta bir sonraki Hall adımına geçer. Basılı tutmak tekrar üretmez.\n4. İleri / Geri komutları kartta işlenir; ekran kartın cevabıyla güncellenir.\n5. Otomatik test her 500 ms'de bir adım ilerler.\n6. Gerçek Hall girişleri: H1=PA0, H2=PA1, H3=PA4 ve ortak GND. Yalnızca 3,3 V uyumlu sinyal bağla.\n\nBu sürüm elle hareket testi içindir. Motor sürmez. Hall sırası motora göre doğrulanmalıdır. 000 ve 111 bu 120° Hall düzeninde geçersizdir. Sektöre doğrudan tıklayarak adım atlarsan hata sayacı artabilir.\n\nCSV son 10.000 değişiklik kaydını içerir; tam hız veri kaydı değildir.", "Kullanım", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
    }
}
