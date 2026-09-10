using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

namespace ImperxCameraTester
{
    internal sealed class MainForm : Form
    {
        private readonly Color _background = Color.FromArgb(18, 23, 31);
        private readonly Color _panel = Color.FromArgb(28, 35, 46);
        private readonly Color _accent = Color.FromArgb(43, 156, 255);

        private readonly Panel _previewHost = new Panel();
        private readonly PictureBox _demoPreview = new PictureBox();
        private readonly ComboBox _modeBox = new ComboBox();
        private readonly Button _connectButton = new Button();
        private readonly Button _disconnectButton = new Button();
        private readonly Button _startButton = new Button();
        private readonly Button _stopButton = new Button();
        private readonly Button _captureButton = new Button();
        private readonly Button _parametersButton = new Button();
        private readonly CheckBox _cropEnabled = new CheckBox();
        private readonly CheckBox _autoScan = new CheckBox();
        private readonly NumericUpDown _cropSize = new NumericUpDown();
        private readonly NumericUpDown _scanDelay = new NumericUpDown();
        private readonly TrackBar _cropXBar = new TrackBar();
        private readonly TrackBar _cropYBar = new TrackBar();
        private readonly Label _cropPositionValue = new Label();
        private readonly Label _statusLabel = new Label();
        private readonly Label _resolutionValue = new Label();
        private readonly Label _fpsValue = new Label();
        private readonly Label _frameValue = new Label();
        private readonly Label _droppedValue = new Label();
        private readonly Label _cameraValue = new Label();
        private readonly System.Windows.Forms.Timer _uiTimer = new System.Windows.Forms.Timer();
        private readonly System.Windows.Forms.Timer _demoTimer = new System.Windows.Forms.Timer();

        private Imperx.IpxCam.SystemGui _system;
        private Imperx.IpxCam.Device _device;
        private Imperx.IpxCam.Stream _stream;
        private Imperx.IpxGenParam.Array _parameters;
        private List<Imperx.IpxCam.Buffer> _buffers;
        private Thread _grabThread;
        private volatile bool _stopRequested = true;
        private volatile bool _streaming;
        private long _frameCount;
        private long _incompleteFrameCount;
        private long _lastMeasuredFrames;
        private double _fps;
        private int _imageWidth;
        private int _imageHeight;
        private int _sensorWidth;
        private int _sensorHeight;
        private bool _hardwareRoiSupported;
        private volatile bool _hardwareRoiActive;
        private volatile bool _preserveScanPointOnStart;
        private volatile bool _preserveFrameCounterOnStart;
        private readonly Stopwatch _fpsWatch = Stopwatch.StartNew();
        private readonly object _captureLock = new object();
        private string _pendingCapturePath;
        private bool _demoConnected;
        private int _demoPhase;
        private int _cropX;
        private int _cropY;
        private int _scanPointIndex;
        private string _scanPointName = "Sağ alt";
        private volatile bool _demoMode = true;
        private volatile bool _cropModeActive;
        private volatile bool _autoScanActive = true;
        private volatile int _cropSizeValue = 640;
        private volatile bool _cropRefreshRequested = true;
        private int _cropPreviewUpdatePending;
        private volatile bool _fullPreviewRefreshRequested = true;
        private int _fullPreviewUpdatePending;
        private readonly Stopwatch _scanWatch = Stopwatch.StartNew();
        private readonly Stopwatch _cropPreviewWatch = Stopwatch.StartNew();
        private readonly Stopwatch _fullPreviewWatch = Stopwatch.StartNew();
        private readonly object _streamLifecycleLock = new object();
        private Thread _roiRestartThread;
        private int _roiRestartPending;
        private volatile bool _closing;

        public MainForm()
        {
            Text = "Imperx Kamera Test Arayüzü";
            StartPosition = FormStartPosition.CenterScreen;
            MinimumSize = new Size(1100, 700);
            Size = new Size(1280, 800);
            BackColor = _background;
            ForeColor = Color.White;
            Font = new Font("Segoe UI", 10F);

            BuildInterface();
            UpdateControls();

            _uiTimer.Interval = 100;
            _uiTimer.Tick += delegate
            {
                AdvanceAutomaticScan();
                RefreshStatistics();
            };
            _uiTimer.Start();

            _demoTimer.Interval = 33;
            _demoTimer.Tick += delegate { RenderDemoFrame(); };

            Load += OnFormLoad;
            FormClosing += OnFormClosing;
        }

        private bool DemoMode
        {
            get { return _demoMode; }
        }

        private void BuildInterface()
        {
            var root = new TableLayoutPanel
            {
                Dock = DockStyle.Fill,
                ColumnCount = 2,
                RowCount = 2,
                Padding = new Padding(18),
                BackColor = _background
            };
            root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 73));
            root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 27));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 68));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            Controls.Add(root);

            var header = new Panel { Dock = DockStyle.Fill, BackColor = _background };
            root.Controls.Add(header, 0, 0);
            root.SetColumnSpan(header, 2);

            var title = new Label
            {
                Text = "IMPERX  /  CAMERA TESTER",
                AutoSize = true,
                Font = new Font("Segoe UI Semibold", 19F),
                ForeColor = Color.White,
                Location = new Point(2, 4)
            };
            header.Controls.Add(title);

            _statusLabel.AutoSize = true;
            _statusLabel.Font = new Font("Segoe UI", 10F);
            _statusLabel.Location = new Point(5, 43);
            header.Controls.Add(_statusLabel);

            _previewHost.Dock = DockStyle.Fill;
            _previewHost.Margin = new Padding(0, 0, 16, 0);
            _previewHost.BackColor = Color.Black;
            _previewHost.BorderStyle = BorderStyle.FixedSingle;
            root.Controls.Add(_previewHost, 0, 1);

            _demoPreview.Dock = DockStyle.Fill;
            _demoPreview.BackColor = Color.Black;
            _demoPreview.SizeMode = PictureBoxSizeMode.Zoom;
            _previewHost.Controls.Add(_demoPreview);

            var side = new Panel { Dock = DockStyle.Fill, BackColor = _panel, Padding = new Padding(18) };
            root.Controls.Add(side, 1, 1);

            var sideFlow = new FlowLayoutPanel
            {
                Dock = DockStyle.Fill,
                FlowDirection = FlowDirection.TopDown,
                WrapContents = false,
                AutoScroll = true,
                BackColor = _panel
            };
            side.Controls.Add(sideFlow);

            sideFlow.Controls.Add(SectionLabel("ÇALIŞMA MODU"));
            _modeBox.DropDownStyle = ComboBoxStyle.DropDownList;
            _modeBox.Items.AddRange(new object[] { "Demo (kamera olmadan)", "Gerçek Imperx kamera" });
            _modeBox.SelectedIndex = 0;
            _modeBox.Width = 270;
            _modeBox.Height = 32;
            _modeBox.SelectedIndexChanged += delegate
            {
                _demoMode = _modeBox.SelectedIndex == 0;
                Disconnect();
                SetStatus(DemoMode ? "Demo modu hazır" : "SDK hazır; kamera seçilebilir", false);
            };
            sideFlow.Controls.Add(_modeBox);

            AddGap(sideFlow, 10);
            _connectButton.Text = "Kameraya Bağlan";
            _connectButton.Click += delegate { Connect(); };
            StyleButton(_connectButton, true);
            sideFlow.Controls.Add(_connectButton);

            _disconnectButton.Text = "Bağlantıyı Kes";
            _disconnectButton.Click += delegate { Disconnect(); };
            StyleButton(_disconnectButton, false);
            sideFlow.Controls.Add(_disconnectButton);

            AddGap(sideFlow, 10);
            _startButton.Text = "▶ Başlat";
            _startButton.Click += delegate { StartStream(); };
            StyleButton(_startButton, true);
            sideFlow.Controls.Add(_startButton);
            _stopButton.Text = "■ Durdur";
            _stopButton.Click += delegate { StopStream(); };
            StyleButton(_stopButton, false);
            sideFlow.Controls.Add(_stopButton);

            _captureButton.Text = "Anlık Görüntü Kaydet";
            _captureButton.Click += delegate { CaptureFrame(); };
            StyleButton(_captureButton, false);
            sideFlow.Controls.Add(_captureButton);

            _parametersButton.Text = "GenICam Parametreleri";
            _parametersButton.Click += delegate { ShowParameters(); };
            StyleButton(_parametersButton, false);
            sideFlow.Controls.Add(_parametersButton);

            AddGap(sideFlow, 14);
            sideFlow.Controls.Add(SectionLabel("CROP / ZOOM"));
            _cropEnabled.Text = "640 × 640 crop görünümünü aç";
            _cropEnabled.Width = 270;
            _cropEnabled.Height = 30;
            _cropEnabled.ForeColor = Color.White;
            _cropEnabled.CheckedChanged += delegate { ApplyCropMode(); };
            sideFlow.Controls.Add(_cropEnabled);

            _autoScan.Text = "Dört köşeyi otomatik kontrol et";
            _autoScan.Width = 270;
            _autoScan.Height = 30;
            _autoScan.ForeColor = Color.White;
            _autoScan.Checked = true;
            _autoScan.CheckedChanged += delegate
            {
                _autoScanActive = _autoScan.Checked;
                if (_autoScan.Checked)
                {
                    _scanPointIndex = 0;
                    ConfigureCropSliders();
                }
                _cropRefreshRequested = true;
                _scanWatch.Restart();
                UpdateCropControls();
                RestartRealStreamForRoiChange();
            };
            sideFlow.Controls.Add(_autoScan);

            sideFlow.Controls.Add(SmallLabel("Crop boyutu (px)"));
            _cropSize.Minimum = 64;
            _cropSize.Maximum = 2048;
            _cropSize.Increment = 64;
            _cropSize.Value = 640;
            _cropSize.Width = 270;
            _cropSize.ValueChanged += delegate
            {
                _cropSizeValue = (int)_cropSize.Value;
                ClampCropPosition();
                ConfigureCropSliders();
                _cropRefreshRequested = true;
                RestartRealStreamForRoiChange();
            };
            sideFlow.Controls.Add(_cropSize);

            sideFlow.Controls.Add(SmallLabel("Her konumda bekleme süresi (ms)"));
            _scanDelay.Minimum = 1000;
            _scanDelay.Maximum = 10000;
            _scanDelay.Increment = 500;
            _scanDelay.Value = 3000;
            _scanDelay.Width = 270;
            _scanDelay.ValueChanged += delegate { _scanWatch.Restart(); };
            sideFlow.Controls.Add(_scanDelay);

            sideFlow.Controls.Add(SmallLabel("X konumu"));
            ConfigureTrackBar(_cropXBar);
            _cropXBar.ValueChanged += delegate
            {
                if (!_autoScan.Checked) _cropX = _cropXBar.Value;
                if (!_autoScan.Checked) _cropRefreshRequested = true;
                UpdateCropPositionLabel();
            };
            sideFlow.Controls.Add(_cropXBar);

            sideFlow.Controls.Add(SmallLabel("Y konumu"));
            ConfigureTrackBar(_cropYBar);
            _cropYBar.ValueChanged += delegate
            {
                if (!_autoScan.Checked) _cropY = _cropYBar.Value;
                if (!_autoScan.Checked) _cropRefreshRequested = true;
                UpdateCropPositionLabel();
            };
            sideFlow.Controls.Add(_cropYBar);

            _cropPositionValue.Width = 270;
            _cropPositionValue.Height = 28;
            _cropPositionValue.ForeColor = Color.FromArgb(135, 205, 255);
            sideFlow.Controls.Add(_cropPositionValue);

            // Kamera testinde istenen ana çalışma biçimi crop + otomatik taramadır.
            // Kullanıcı isterse kutuyu kapatıp yeniden tam görüntüye dönebilir.
            _cropEnabled.Checked = true;
            ApplyCropMode();

            AddGap(sideFlow, 14);
            sideFlow.Controls.Add(SectionLabel("KAMERA BİLGİLERİ"));
            sideFlow.Controls.Add(InfoRow("Kamera", _cameraValue));
            sideFlow.Controls.Add(InfoRow("Çözünürlük", _resolutionValue));
            sideFlow.Controls.Add(InfoRow("FPS", _fpsValue));
            sideFlow.Controls.Add(InfoRow("Kare", _frameValue));
            sideFlow.Controls.Add(InfoRow("Kayıp / eksik", _droppedValue));

            AddGap(sideFlow, 14);
            var note = new Label
            {
                Width = 270,
                Height = 90,
                ForeColor = Color.FromArgb(165, 177, 194),
                Text = "Gerçek kamera modunda çözünürlük sabit değildir; ilk görüntüden otomatik okunur. 4096 px genişliğindeki kameralar desteklenir."
            };
            sideFlow.Controls.Add(note);
        }

        private Label SectionLabel(string text)
        {
            return new Label
            {
                Text = text,
                Width = 270,
                Height = 28,
                ForeColor = Color.FromArgb(135, 164, 195),
                Font = new Font("Segoe UI Semibold", 9F)
            };
        }

        private Label SmallLabel(string text)
        {
            return new Label
            {
                Text = text,
                Width = 270,
                Height = 22,
                ForeColor = Color.FromArgb(165, 177, 194)
            };
        }

        private static void ConfigureTrackBar(TrackBar bar)
        {
            bar.Width = 270;
            bar.Height = 36;
            bar.Minimum = 0;
            bar.Maximum = 1;
            bar.TickStyle = TickStyle.None;
        }

        private Control InfoRow(string name, Label value)
        {
            var row = new TableLayoutPanel { Width = 270, Height = 38, ColumnCount = 2 };
            row.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 42));
            row.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 58));
            row.Controls.Add(new Label { Text = name, Dock = DockStyle.Fill, ForeColor = Color.FromArgb(165, 177, 194), TextAlign = ContentAlignment.MiddleLeft }, 0, 0);
            value.Dock = DockStyle.Fill;
            value.ForeColor = Color.White;
            value.TextAlign = ContentAlignment.MiddleRight;
            row.Controls.Add(value, 1, 0);
            return row;
        }

        private static void AddGap(Control parent, int height)
        {
            parent.Controls.Add(new Panel { Width = 1, Height = height });
        }

        private void StyleButton(Button button, bool primary)
        {
            button.Width = 270;
            button.Height = 38;
            button.FlatStyle = FlatStyle.Flat;
            button.FlatAppearance.BorderSize = primary ? 0 : 1;
            button.FlatAppearance.BorderColor = Color.FromArgb(76, 91, 110);
            button.BackColor = primary ? _accent : Color.FromArgb(38, 47, 60);
            button.ForeColor = Color.White;
            button.Cursor = Cursors.Hand;
            button.Margin = new Padding(0, 3, 0, 3);
        }

        private void OnFormLoad(object sender, EventArgs e)
        {
            SetStatus("Test", false);
            UpdateCropPositionLabel();
        }

        private void ApplyCropMode()
        {
            bool crop = _cropEnabled.Checked;
            _cropModeActive = crop;
            _cropRefreshRequested = crop;
            _fullPreviewRefreshRequested = !crop;
            _autoScan.Enabled = crop;
            _cropSize.Enabled = crop;
            _scanDelay.Enabled = crop && _autoScan.Checked;
            ConfigureCropSliders();
            UpdateCropControls();
            _scanWatch.Restart();

            _demoPreview.Visible = true;
            _demoPreview.BringToFront();
            RestartRealStreamForRoiChange();
        }

        private void UpdateCropControls()
        {
            bool manual = _cropEnabled.Checked && !_autoScan.Checked;
            _cropXBar.Enabled = manual;
            _cropYBar.Enabled = manual;
            _scanDelay.Enabled = _cropEnabled.Checked && _autoScan.Checked;
            UpdateCropPositionLabel();
        }

        private void ConfigureCropSliders()
        {
            int size = (int)_cropSize.Value;
            int scanWidth = GetScanWidth();
            int scanHeight = GetScanHeight();
            int maxX = Math.Max(0, scanWidth - size);
            int maxY = Math.Max(0, scanHeight - size);
            _cropXBar.Maximum = Math.Max(1, maxX);
            _cropYBar.Maximum = Math.Max(1, maxY);
            if (_autoScan.Checked && scanWidth > 0 && scanHeight > 0)
            {
                ApplyScanPoint(_scanPointIndex);
            }
            else
            {
                ClampCropPosition();
            }
            _cropXBar.Value = Math.Min(_cropXBar.Maximum, _cropX);
            _cropYBar.Value = Math.Min(_cropYBar.Maximum, _cropY);
            UpdateCropPositionLabel();
        }

        private void ClampCropPosition()
        {
            int size = (int)_cropSize.Value;
            _cropX = Math.Max(0, Math.Min(_cropX, Math.Max(0, GetScanWidth() - size)));
            _cropY = Math.Max(0, Math.Min(_cropY, Math.Max(0, GetScanHeight() - size)));
        }

        private void AdvanceAutomaticScan()
        {
            if (!_streaming || !_cropEnabled.Checked || !_autoScan.Checked || GetScanWidth() <= 0 || GetScanHeight() <= 0)
            {
                return;
            }
            if (_scanWatch.ElapsedMilliseconds < (int)_scanDelay.Value)
            {
                return;
            }
            _scanWatch.Restart();

            _scanPointIndex = (_scanPointIndex + 1) % 4;
            ApplyScanPoint(_scanPointIndex);
            _cropRefreshRequested = true;

            _cropXBar.Value = Math.Min(_cropXBar.Maximum, _cropX);
            _cropYBar.Value = Math.Min(_cropYBar.Maximum, _cropY);
            UpdateCropPositionLabel();
            RestartRealStreamForRoiChange();
        }

        private void ApplyScanPoint(int pointIndex)
        {
            int scanWidth = GetScanWidth();
            int scanHeight = GetScanHeight();
            int size = Math.Min((int)_cropSize.Value, Math.Min(scanWidth, scanHeight));
            int maxX = Math.Max(0, scanWidth - size);
            int maxY = Math.Max(0, scanHeight - size);

            switch (pointIndex)
            {
                case 0:
                    _cropX = maxX;
                    _cropY = maxY;
                    _scanPointName = "Sağ alt";
                    break;
                case 1:
                    _cropX = 0;
                    _cropY = maxY;
                    _scanPointName = "Sol alt";
                    break;
                case 2:
                    _cropX = 0;
                    _cropY = 0;
                    _scanPointName = "Sol üst";
                    break;
                default:
                    _cropX = maxX;
                    _cropY = 0;
                    _scanPointName = "Sağ üst";
                    break;
            }
        }

        private int GetScanWidth()
        {
            return _sensorWidth > 0 ? _sensorWidth : _imageWidth;
        }

        private int GetScanHeight()
        {
            return _sensorHeight > 0 ? _sensorHeight : _imageHeight;
        }

        private bool ShouldUseHardwareRoi()
        {
            return !DemoMode && _hardwareRoiSupported && _cropModeActive && _autoScanActive;
        }

        private void DetectHardwareRoiSupport()
        {
            _hardwareRoiSupported = false;
            _hardwareRoiActive = false;
            try
            {
                Imperx.IpxGenParam.IntParam width = _parameters.GetInt("Width");
                Imperx.IpxGenParam.IntParam height = _parameters.GetInt("Height");
                Imperx.IpxGenParam.IntParam offsetX = _parameters.GetInt("OffsetX");
                Imperx.IpxGenParam.IntParam offsetY = _parameters.GetInt("OffsetY");
                if (width == null || height == null || offsetX == null || offsetY == null ||
                    !width.IsWritable() || !height.IsWritable() || !offsetX.IsWritable() || !offsetY.IsWritable())
                {
                    return;
                }

                _sensorWidth = ReadSensorDimension("SensorWidth", width);
                _sensorHeight = ReadSensorDimension("SensorHeight", height);
                _imageWidth = _sensorWidth;
                _imageHeight = _sensorHeight;
                _hardwareRoiSupported = _sensorWidth >= 640 && _sensorHeight >= 640;
                ConfigureCropSliders();
            }
            catch
            {
                _hardwareRoiSupported = false;
                _hardwareRoiActive = false;
            }
        }

        private int ReadSensorDimension(string sensorParameterName, Imperx.IpxGenParam.IntParam fallbackParameter)
        {
            try
            {
                Imperx.IpxGenParam.IntParam sensorParameter = _parameters.GetInt(sensorParameterName);
                if (sensorParameter != null)
                {
                    int value = checked((int)sensorParameter.GetValue());
                    if (value > 0) return value;
                }
            }
            catch { }

            return checked((int)fallbackParameter.GetMax());
        }

        private static long AlignGenicamValue(long value, Imperx.IpxGenParam.IntParam parameter)
        {
            long minimum = parameter.GetMin();
            long maximum = parameter.GetMax();
            long increment = Math.Max(1L, parameter.GetIncrement());
            long clamped = Math.Max(minimum, Math.Min(value, maximum));
            return minimum + ((clamped - minimum) / increment) * increment;
        }

        private void ConfigureCameraRoiForCurrentMode()
        {
            if (!_hardwareRoiSupported)
            {
                _hardwareRoiActive = false;
                return;
            }

            try
            {
                if (!ShouldUseHardwareRoi())
                {
                    RestoreFullFrameRoi();
                    return;
                }

                Imperx.IpxGenParam.IntParam width = _parameters.GetInt("Width");
                Imperx.IpxGenParam.IntParam height = _parameters.GetInt("Height");
                Imperx.IpxGenParam.IntParam offsetX = _parameters.GetInt("OffsetX");
                Imperx.IpxGenParam.IntParam offsetY = _parameters.GetInt("OffsetY");

                offsetX.SetValue(AlignGenicamValue(0, offsetX));
                offsetY.SetValue(AlignGenicamValue(0, offsetY));
                long roiWidth = AlignGenicamValue(_cropSizeValue, width);
                long roiHeight = AlignGenicamValue(_cropSizeValue, height);
                width.SetValue(roiWidth);
                height.SetValue(roiHeight);
                offsetX.SetValue(AlignGenicamValue(_cropX, offsetX));
                offsetY.SetValue(AlignGenicamValue(_cropY, offsetY));

                TrySetCameraFrameRate(20.0);
                _hardwareRoiActive = true;
            }
            catch
            {
                _hardwareRoiActive = false;
                try { RestoreFullFrameRoi(); } catch { }
                _hardwareRoiSupported = false;
            }
        }

        private void TrySetCameraFrameRate(double requestedFps)
        {
            try
            {
                Imperx.IpxGenParam.BooleanParam enabled = _parameters.GetBoolean("AcquisitionFrameRateEnable");
                if (enabled != null && enabled.IsWritable()) enabled.SetValue(true);
            }
            catch { }

            try
            {
                Imperx.IpxGenParam.FloatParam frameRate = _parameters.GetFloat("AcquisitionFrameRate");
                if (frameRate != null && frameRate.IsWritable())
                {
                    frameRate.SetValue(Math.Max(frameRate.GetMin(), Math.Min(requestedFps, frameRate.GetMax())));
                }
            }
            catch { }
        }

        private void RestoreFullFrameRoi()
        {
            if (_parameters == null || _sensorWidth <= 0 || _sensorHeight <= 0)
            {
                _hardwareRoiActive = false;
                return;
            }

            Imperx.IpxGenParam.IntParam width = _parameters.GetInt("Width");
            Imperx.IpxGenParam.IntParam height = _parameters.GetInt("Height");
            Imperx.IpxGenParam.IntParam offsetX = _parameters.GetInt("OffsetX");
            Imperx.IpxGenParam.IntParam offsetY = _parameters.GetInt("OffsetY");
            offsetX.SetValue(AlignGenicamValue(0, offsetX));
            offsetY.SetValue(AlignGenicamValue(0, offsetY));
            width.SetValue(AlignGenicamValue(_sensorWidth, width));
            height.SetValue(AlignGenicamValue(_sensorHeight, height));
            _hardwareRoiActive = false;
        }

        private void RestartRealStreamForRoiChange()
        {
            if (DemoMode || !_streaming || !_hardwareRoiSupported || _closing)
            {
                return;
            }
            if (!_hardwareRoiActive && !ShouldUseHardwareRoi())
            {
                return;
            }

            if (Interlocked.CompareExchange(ref _roiRestartPending, 1, 0) != 0)
            {
                return;
            }

            _preserveScanPointOnStart = true;
            _preserveFrameCounterOnStart = true;
            UpdateControls();

            _roiRestartThread = new Thread(new ThreadStart(delegate
            {
                try
                {
                    lock (_streamLifecycleLock)
                    {
                        if (StopStream() && !_closing && _device != null)
                        {
                            StartStream();
                        }
                    }
                }
                finally
                {
                    Interlocked.Exchange(ref _roiRestartPending, 0);
                    UpdateControls();
                }
            }))
            {
                IsBackground = true,
                Name = "ImperxRoiRestartThread"
            };
            _roiRestartThread.Start();
        }

        private void UpdateCropPositionLabel()
        {
            string point = _autoScan.Checked ? _scanPointName + " — " : string.Empty;
            _cropPositionValue.Text = "ROI: " + point + "X=" + _cropX + "  Y=" + _cropY + "  " + (int)_cropSize.Value + "×" + (int)_cropSize.Value;
        }

        private void EnsureSdkSystem()
        {
            if (_system != null)
            {
                return;
            }

            if (!Directory.Exists(Program.SdkBin))
            {
                throw new DirectoryNotFoundException("Imperx SDK bulunamadı: " + Program.SdkBin);
            }

            _system = new Imperx.IpxCam.SystemGui();
            if (!_system.CreateDisplay(_previewHost.Handle))
            {
                _system.Dispose();
                _system = null;
                throw new InvalidOperationException("Imperx görüntüleme penceresi oluşturulamadı.");
            }
        }

        private void Connect()
        {
            try
            {
                if (DemoMode)
                {
                    _demoConnected = true;
                    _cameraValue.Text = "Sanal Imperx Kamera";
                    _imageWidth = 4096;
                    _imageHeight = 2160;
                    _sensorWidth = _imageWidth;
                    _sensorHeight = _imageHeight;
                    ConfigureCropSliders();
                    SetStatus("Demo kameraya bağlandı", false);
                    UpdateControls();
                    return;
                }

                EnsureSdkSystem();
                Imperx.IpxCam.DeviceInfo info = _system.SelectCamera(Handle);
                if (info == null)
                {
                    SetStatus("Kamera seçilmedi", true);
                    return;
                }

                _device = new Imperx.IpxCam.Device(info);
                if (_device.GetNumStreams() == 0)
                {
                    throw new InvalidOperationException("Seçilen kamerada görüntü akışı bulunamadı.");
                }

                _stream = _device.GetStreamByIndex(0);
                _parameters = _device.GetCameraParameters();
                DetectHardwareRoiSupport();
                _cameraValue.Text = info.GetModel() + " / " + info.GetSerialNumber();
                ApplyCropMode();
                SetStatus("Kameraya bağlandı", false);
                UpdateControls();
            }
            catch (Exception ex)
            {
                SetStatus("Bağlantı hatası: " + ex.Message, true);
                MessageBox.Show(this, ex.Message, "Imperx bağlantı hatası", MessageBoxButtons.OK, MessageBoxIcon.Error);
                ReleaseRealCamera();
            }
        }

        private void CreateStreamBuffers()
        {
            _buffers = new List<Imperx.IpxCam.Buffer>();
            int size = _stream.GetBufferSize();
            int count = Math.Max(3, _stream.GetMinNumBuffers());
            for (int index = 0; index < count; index++)
            {
                Imperx.IpxCam.Buffer buffer = _stream.CreateBuffer(size, null);
                _buffers.Add(buffer);
                _stream.QueueBuffer(buffer);
            }
        }

        private void StartStream()
        {
            lock (_streamLifecycleLock)
            {
                if (_streaming || !IsConnected() || _closing)
                {
                    return;
                }

                try
                {
                    if (!_preserveFrameCounterOnStart)
                    {
                        _frameCount = 0;
                        _incompleteFrameCount = 0;
                        _lastMeasuredFrames = 0;
                        _fpsWatch.Restart();
                    }
                    if (!_preserveScanPointOnStart && _cropModeActive && _autoScanActive && GetScanWidth() > 0 && GetScanHeight() > 0)
                    {
                        _scanPointIndex = 0;
                        ApplyScanPoint(_scanPointIndex);
                        _cropXBar.Value = Math.Min(_cropXBar.Maximum, _cropX);
                        _cropYBar.Value = Math.Min(_cropYBar.Maximum, _cropY);
                        UpdateCropPositionLabel();
                    }
                    _scanWatch.Restart();
                    _cropRefreshRequested = true;
                    _stopRequested = false;
                    _streaming = true;

                    if (DemoMode)
                    {
                        _demoTimer.Start();
                    }
                    else
                    {
                        ConfigureCameraRoiForCurrentMode();
                        CreateStreamBuffers();
                        _parameters.SetIntegerValue("TLParamsLocked", 1);
                        _stream.StartAcquisition();
                        _parameters.ExecuteCommand("AcquisitionStart");
                        _grabThread = new Thread(GrabLoop) { IsBackground = true, Name = "ImperxGrabThread" };
                        _grabThread.Start();
                    }

                    SetStatus(_hardwareRoiActive ? "Canlı görüntü çalışıyor — donanımsal ROI etkin" : "Canlı görüntü çalışıyor", false);
                    UpdateControls();
                    _preserveScanPointOnStart = false;
                    _preserveFrameCounterOnStart = false;
                }
                catch (Exception ex)
                {
                    CleanupFailedStreamStart();
                    _streaming = false;
                    _stopRequested = true;
                    _preserveScanPointOnStart = false;
                    _preserveFrameCounterOnStart = false;
                    SetStatus("Akış başlatılamadı: " + ex.Message, true);
                    ShowError(ex.Message, "Görüntü akışı hatası");
                    UpdateControls();
                }
            }
        }

        private void GrabLoop()
        {
            while (!_stopRequested)
            {
                Imperx.IpxCam.Buffer buffer = null;
                string operation = "buffer bekleme";
                try
                {
                    buffer = _stream.GetBuffer(1000);
                    if (buffer == null)
                    {
                        continue;
                    }
                    if (buffer.IsIncomplete())
                    {
                        Interlocked.Increment(ref _incompleteFrameCount);
                        continue;
                    }

                    int newWidth = (int)buffer.GetWidth();
                    int newHeight = (int)buffer.GetHeight();
                    if (newWidth != _imageWidth || newHeight != _imageHeight)
                    {
                        _imageWidth = newWidth;
                        _imageHeight = newHeight;
                        if (!_hardwareRoiActive) BeginInvoke(new Action(ConfigureCropSliders));
                    }
                    Interlocked.Increment(ref _frameCount);
                    var image = buffer.GetImage();
                    if (_cropModeActive)
                    {
                        bool shouldRefresh = _cropRefreshRequested || _cropPreviewWatch.ElapsedMilliseconds >= 50;
                        if (shouldRefresh && Interlocked.CompareExchange(ref _cropPreviewUpdatePending, 1, 0) == 0)
                        {
                            _cropRefreshRequested = false;
                            _cropPreviewWatch.Restart();
                            operation = "crop Bitmap dönüşümü";
                            Bitmap crop = null;
                            try
                            {
                                int cropX = _hardwareRoiActive ? 0 : _cropX;
                                int cropY = _hardwareRoiActive ? 0 : _cropY;
                                crop = CreateCropBitmap(buffer, image, cropX, cropY, _cropSizeValue);
                                if (crop == null)
                                {
                                    throw new InvalidOperationException("SDK görüntüyü crop için Bitmap'e dönüştüremedi.");
                                }

                                BeginInvoke(new Action(delegate
                                {
                                    try
                                    {
                                        if (_cropModeActive)
                                        {
                                            SetPreviewImage(crop);
                                        }
                                        else
                                        {
                                            crop.Dispose();
                                        }
                                    }
                                    finally
                                    {
                                        Interlocked.Exchange(ref _cropPreviewUpdatePending, 0);
                                    }
                                }));
                            }
                            catch
                            {
                                if (crop != null) crop.Dispose();
                                Interlocked.Exchange(ref _cropPreviewUpdatePending, 0);
                                throw;
                            }
                        }
                    }
                    else
                    {
                        bool shouldRefreshFullPreview = _fullPreviewRefreshRequested || _fullPreviewWatch.ElapsedMilliseconds >= 125;
                        if (shouldRefreshFullPreview && Interlocked.CompareExchange(ref _fullPreviewUpdatePending, 1, 0) == 0)
                        {
                            _fullPreviewRefreshRequested = false;
                            _fullPreviewWatch.Restart();
                            operation = "tam görüntü Bitmap dönüşümü";
                            Bitmap fullPreview = null;
                            try
                            {
                                fullPreview = CreateFullPreviewBitmap(image);
                                if (fullPreview == null)
                                {
                                    throw new InvalidOperationException("Imperx SDK tam görüntüyü Bitmap'e dönüştüremedi.");
                                }

                                BeginInvoke(new Action(delegate
                                {
                                    try
                                    {
                                        if (!_cropModeActive)
                                        {
                                            SetPreviewImage(fullPreview);
                                        }
                                        else
                                        {
                                            fullPreview.Dispose();
                                        }
                                    }
                                    finally
                                    {
                                        Interlocked.Exchange(ref _fullPreviewUpdatePending, 0);
                                    }
                                }));
                            }
                            catch
                            {
                                if (fullPreview != null) fullPreview.Dispose();
                                Interlocked.Exchange(ref _fullPreviewUpdatePending, 0);
                                throw;
                            }
                        }
                    }

                    string capturePath = TakePendingCapturePath();
                    if (capturePath != null)
                    {
                        operation = "görüntü kaydı";
                        SaveCameraImage(image, capturePath);
                        BeginInvoke(new Action(delegate { SetStatus("Görüntü kaydedildi: " + capturePath, false); }));
                    }
                }
                catch (Imperx.IpxCameraException ex)
                {
                    int errorCode = (int)ex.err;
                    if (!_stopRequested && errorCode != -1011 && errorCode != -1008)
                    {
                        string detail = ex.GetIpxCamErrString();
                        BeginInvoke(new Action(delegate
                        {
                            SetStatus("Imperx hatası [" + operation + "]: " + detail + " (kod " + errorCode + ")", true);
                        }));
                    }
                }
                catch (Exception ex)
                {
                    if (!_stopRequested)
                    {
                        BeginInvoke(new Action(delegate { SetStatus("Görüntü alma hatası [" + operation + "]: " + ex.Message, true); }));
                    }
                }
                finally
                {
                    if (buffer != null)
                    {
                        try { _stream.QueueBuffer(buffer); } catch { }
                    }
                }
            }
        }

        private bool StopStream()
        {
            lock (_streamLifecycleLock)
            {
                if (!_streaming)
                {
                    return true;
                }

                _stopRequested = true;
                if (DemoMode)
                {
                    StopDemoTimer();
                }

                if (!DemoMode && _stream != null)
                {
                    try { _parameters.ExecuteCommand("AcquisitionStop"); } catch { }
                    try { _stream.CancelBuffer(); } catch { }
                    try { _stream.StopAcquisition(1); } catch { }
                    try { _parameters.SetIntegerValue("TLParamsLocked", 0); } catch { }
                    if (!WaitForGrabThreadToStop())
                    {
                        SetStatus("Görüntü alma iş parçacığı durmadı; SDK kaynakları korunuyor", true);
                        UpdateControls();
                        return false;
                    }
                    ReleaseBuffers();
                }

                _streaming = false;
                SetStatus("Canlı görüntü durduruldu", false);
                UpdateControls();
                return true;
            }
        }

        private bool WaitForGrabThreadToStop()
        {
            Thread thread = _grabThread;
            if (thread != null && thread.IsAlive && !thread.Join(5000))
            {
                return false;
            }

            _grabThread = null;
            return true;
        }

        private void StopDemoTimer()
        {
            if (InvokeRequired)
            {
                if (IsHandleCreated && !IsDisposed) BeginInvoke(new Action(StopDemoTimer));
                return;
            }
            _demoTimer.Stop();
        }

        private void CleanupFailedStreamStart()
        {
            _stopRequested = true;
            StopDemoTimer();

            if (_stream == null)
            {
                return;
            }

            try { if (_parameters != null) _parameters.ExecuteCommand("AcquisitionStop"); } catch { }
            try { _stream.CancelBuffer(); } catch { }
            try { _stream.StopAcquisition(1); } catch { }
            try { if (_parameters != null) _parameters.SetIntegerValue("TLParamsLocked", 0); } catch { }

            if (!WaitForGrabThreadToStop())
            {
                SetStatus("Başlatma hatasından sonra görüntü alma iş parçacığı durmadı", true);
                return;
            }
            ReleaseBuffers();
        }

        private bool Disconnect()
        {
            lock (_streamLifecycleLock)
            {
                if (!StopStream())
                {
                    return false;
                }
                _demoConnected = false;
                ReleaseRealCamera();
                ResetDisconnectedUi();
                _imageWidth = 0;
                _imageHeight = 0;
                SetStatus("Bağlantı yok", false);
                UpdateControls();
                return true;
            }
        }

        private void ResetDisconnectedUi()
        {
            if (InvokeRequired)
            {
                BeginInvoke(new Action(ResetDisconnectedUi));
                return;
            }
            _demoPreview.Visible = true;
            _cameraValue.Text = "—";
            _resolutionValue.Text = "—";
            _fpsValue.Text = "0.0";
            _frameValue.Text = "0";
            _droppedValue.Text = "0 / 0";
        }

        private void ReleaseBuffers()
        {
            if (_stream == null || _buffers == null)
            {
                return;
            }

            try { _stream.FlushBuffers(Imperx.IpxCam.FlushOperation.AllDiscard); } catch { }
            foreach (Imperx.IpxCam.Buffer buffer in _buffers)
            {
                try { _stream.RevokeBuffer(buffer); } catch { }
            }
            _buffers = null;
        }

        private void ReleaseRealCamera()
        {
            ReleaseBuffers();
            if (_hardwareRoiActive)
            {
                try { RestoreFullFrameRoi(); } catch { }
            }
            if (_system != null)
            {
                try { _system.DestroyGenParamTreeView(); } catch { }
            }
            if (_stream != null)
            {
                try { _stream.Dispose(); } catch { }
                _stream = null;
            }
            if (_device != null)
            {
                try { _device.Dispose(); } catch { }
                _device = null;
            }
            _parameters = null;
            _hardwareRoiSupported = false;
            _hardwareRoiActive = false;
            _sensorWidth = 0;
            _sensorHeight = 0;
        }

        private void CaptureFrame()
        {
            using (var dialog = new SaveFileDialog())
            {
                dialog.Title = "Anlık görüntüyü kaydet";
                dialog.Filter = "PNG görüntüsü|*.png|JPEG görüntüsü|*.jpg|Bitmap görüntüsü|*.bmp|TIFF görüntüsü|*.tiff";
                dialog.FileName = "imperx_" + DateTime.Now.ToString("yyyyMMdd_HHmmss") + ".png";
                if (dialog.ShowDialog(this) != DialogResult.OK)
                {
                    return;
                }

                if (DemoMode)
                {
                    if (_demoPreview.Image != null)
                    {
                        SaveBitmapByExtension(_demoPreview.Image, dialog.FileName);
                        SetStatus("Görüntü kaydedildi: " + dialog.FileName, false);
                    }
                }
                else
                {
                    lock (_captureLock)
                    {
                        _pendingCapturePath = dialog.FileName;
                    }
                    SetStatus("Sonraki kare kaydedilecek", false);
                }
            }
        }

        private string TakePendingCapturePath()
        {
            lock (_captureLock)
            {
                string result = _pendingCapturePath;
                _pendingCapturePath = null;
                return result;
            }
        }

        private void ShowParameters()
        {
            if (DemoMode)
            {
                MessageBox.Show(this, "GenICam parametre ağacı yalnızca gerçek kamera bağlıyken kullanılabilir.", "Demo modu", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            try
            {
                _system.DestroyGenParamTreeView();
                _system.CreateGenParamTreeViewForArray(Handle, _parameters);
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, ex.Message, "Parametre hatası", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void RenderDemoFrame()
        {
            const int logicalWidth = 4096;
            const int logicalHeight = 2160;
            bool cropMode = _cropEnabled.Checked;
            int cropSize = (int)_cropSize.Value;
            int width = cropMode ? cropSize : 960;
            int height = cropMode ? cropSize : 540;
            var bitmap = new Bitmap(width, height);
            using (Graphics graphics = Graphics.FromImage(bitmap))
            {
                graphics.SmoothingMode = SmoothingMode.AntiAlias;
                if (cropMode)
                {
                    graphics.TranslateTransform(-_cropX, -_cropY);
                }
                else
                {
                    graphics.ScaleTransform(width / (float)logicalWidth, height / (float)logicalHeight);
                }

                DrawDemoScene(graphics, logicalWidth, logicalHeight);
                graphics.ResetTransform();

                using (var shade = new SolidBrush(Color.FromArgb(175, 7, 12, 20)))
                {
                    graphics.FillRectangle(shade, 20, 20, cropMode ? 330 : 540, 78);
                }
                using (var font = new Font("Segoe UI Semibold", cropMode ? 18F : 24F))
                using (var small = new Font("Consolas", cropMode ? 11F : 13F))
                using (var white = new SolidBrush(Color.White))
                {
                    graphics.DrawString(cropMode ? cropSize + " × " + cropSize + " CROP / ZOOM" : "IMPERX DEMO STREAM", font, white, 34, 27);
                    string detail = cropMode
                        ? (_autoScan.Checked ? _scanPointName + "   " : string.Empty) + "X: " + _cropX + "  Y: " + _cropY
                        : "Kaynak: 4096 × 2160   |   30 FPS";
                    graphics.DrawString(detail, small, white, 38, 66);
                }
            }

            SetPreviewImage(bitmap);
            _demoPhase++;
            Interlocked.Increment(ref _frameCount);
        }

        private void DrawDemoScene(Graphics graphics, int width, int height)
        {
            using (var brush = new LinearGradientBrush(new Rectangle(0, 0, width, height), Color.FromArgb(15, 32, 50), Color.FromArgb(15, 87, 123), (_demoPhase * 2) % 360))
            {
                graphics.FillRectangle(brush, 0, 0, width, height);
            }

            int x = (_demoPhase * 25) % (width + 300) - 150;
            int y = height / 2 + (int)(Math.Sin(_demoPhase / 13.0) * 650);
            using (var glow = new SolidBrush(Color.FromArgb(220, 43, 156, 255)))
            {
                graphics.FillEllipse(glow, x - 120, y - 120, 240, 240);
            }
            using (var pen = new Pen(Color.FromArgb(80, 255, 255, 255), 5))
            {
                for (int gx = 0; gx < width; gx += 320) graphics.DrawLine(pen, gx, 0, gx, height);
                for (int gy = 0; gy < height; gy += 320) graphics.DrawLine(pen, 0, gy, width, gy);
            }
            using (var font = new Font("Consolas", 38F))
            using (var white = new SolidBrush(Color.FromArgb(210, Color.White)))
            {
                for (int gy = 160; gy < height; gy += 640)
                    for (int gx = 160; gx < width; gx += 640)
                        graphics.DrawString(gx + "," + gy, font, white, gx, gy);
            }
        }

        private void SetPreviewImage(Bitmap bitmap)
        {
            Image previous = _demoPreview.Image;
            _demoPreview.Image = bitmap;
            _demoPreview.Visible = true;
            _demoPreview.BringToFront();
            if (previous != null) previous.Dispose();
        }

        private Bitmap CreateCropBitmap(Imperx.IpxCam.Buffer buffer, Imperx.IpxCam.Image image, int requestedX, int requestedY, int requestedSize)
        {
            Bitmap fastCrop = CreateFastCropBitmap(buffer, requestedX, requestedY, requestedSize);
            return fastCrop ?? CreateSdkCropBitmap(image, requestedX, requestedY, requestedSize);
        }

        private Bitmap CreateFastCropBitmap(Imperx.IpxCam.Buffer buffer, int requestedX, int requestedY, int requestedSize)
        {
            int sourceWidth = buffer.GetWidth();
            int sourceHeight = buffer.GetHeight();
            int size = Math.Min(requestedSize, Math.Min(sourceWidth, sourceHeight));
            int startX = Math.Max(0, Math.Min(requestedX, sourceWidth - size));
            int startY = Math.Max(0, Math.Min(requestedY, sourceHeight - size));
            ulong format = buffer.GetPixelFormat();
            int bitsPerPixel = (int)((format >> 16) & 0xFF);
            bool rgb = format == 0x02180014;
            bool bgr = format == 0x02180015;
            int bytesPerPixel;

            if (rgb || bgr)
            {
                bytesPerPixel = 3;
            }
            else if (bitsPerPixel == 8)
            {
                bytesPerPixel = 1;
            }
            else if (bitsPerPixel == 16)
            {
                bytesPerPixel = 2;
            }
            else
            {
                return null;
            }

            int sourceStride = sourceWidth * bytesPerPixel + buffer.GetXPadding();
            IntPtr sourceBase = IntPtr.Add(buffer.GetBufferPtr(), (int)buffer.GetImageOffset());
            var result = new Bitmap(size, size, PixelFormat.Format24bppRgb);
            BitmapData data = result.LockBits(new Rectangle(0, 0, size, size), ImageLockMode.WriteOnly, PixelFormat.Format24bppRgb);
            try
            {
                var sourceRow = new byte[size * bytesPerPixel];
                var destination = new byte[data.Stride * size];
                int pixelId = (int)(format & 0xFFFF);
                int componentBits = 16;
                if (pixelId == 3 || (pixelId >= 0x0C && pixelId <= 0x0F)) componentBits = 10;
                else if (pixelId == 5 || (pixelId >= 0x10 && pixelId <= 0x13)) componentBits = 12;
                else if (pixelId == 0x25) componentBits = 14;

                for (int row = 0; row < size; row++)
                {
                    int sourceOffset = (startY + row) * sourceStride + startX * bytesPerPixel;
                    Marshal.Copy(IntPtr.Add(sourceBase, sourceOffset), sourceRow, 0, sourceRow.Length);
                    int destinationOffset = row * data.Stride;
                    for (int column = 0; column < size; column++)
                    {
                        byte blue;
                        byte green;
                        byte red;
                        int sourcePixel = column * bytesPerPixel;
                        if (rgb || bgr)
                        {
                            blue = bgr ? sourceRow[sourcePixel] : sourceRow[sourcePixel + 2];
                            green = sourceRow[sourcePixel + 1];
                            red = bgr ? sourceRow[sourcePixel + 2] : sourceRow[sourcePixel];
                        }
                        else if (bytesPerPixel == 2)
                        {
                            ushort value = (ushort)(sourceRow[sourcePixel] | (sourceRow[sourcePixel + 1] << 8));
                            byte gray = (byte)(value >> Math.Max(0, componentBits - 8));
                            blue = green = red = gray;
                        }
                        else
                        {
                            blue = green = red = sourceRow[sourcePixel];
                        }

                        int destinationPixel = destinationOffset + column * 3;
                        destination[destinationPixel] = blue;
                        destination[destinationPixel + 1] = green;
                        destination[destinationPixel + 2] = red;
                    }
                }
                Marshal.Copy(destination, 0, data.Scan0, destination.Length);
            }
            catch
            {
                result.UnlockBits(data);
                result.Dispose();
                throw;
            }
            result.UnlockBits(data);
            return result;
        }

        private Bitmap CreateSdkCropBitmap(Imperx.IpxCam.Image image, int requestedX, int requestedY, int requestedSize)
        {
            using (Bitmap source = _system.ConvertToBitmap(image))
            {
                if (source == null)
                {
                    return null;
                }

                int size = Math.Min(requestedSize, Math.Min(source.Width, source.Height));
                int startX = Math.Max(0, Math.Min(requestedX, source.Width - size));
                int startY = Math.Max(0, Math.Min(requestedY, source.Height - size));
                return source.Clone(new Rectangle(startX, startY, size, size), PixelFormat.Format24bppRgb);
            }
        }

        private Bitmap CreateFullPreviewBitmap(Imperx.IpxCam.Image image)
        {
            using (Bitmap source = _system.ConvertToBitmap(image))
            {
                if (source == null)
                {
                    return null;
                }

                const int maxPreviewWidth = 1280;
                const int maxPreviewHeight = 720;
                double scale = Math.Min(maxPreviewWidth / (double)source.Width, maxPreviewHeight / (double)source.Height);
                scale = Math.Min(1.0, scale);
                int width = Math.Max(1, (int)Math.Round(source.Width * scale));
                int height = Math.Max(1, (int)Math.Round(source.Height * scale));
                var preview = new Bitmap(width, height, PixelFormat.Format24bppRgb);
                using (Graphics graphics = Graphics.FromImage(preview))
                {
                    graphics.CompositingMode = CompositingMode.SourceCopy;
                    graphics.InterpolationMode = InterpolationMode.Bilinear;
                    graphics.PixelOffsetMode = PixelOffsetMode.HighSpeed;
                    graphics.SmoothingMode = SmoothingMode.None;
                    graphics.DrawImage(source, new Rectangle(0, 0, width, height));
                }
                return preview;
            }
        }

        private void SaveCameraImage(Imperx.IpxCam.Image image, string path)
        {
            if (string.Equals(Path.GetExtension(path), ".png", StringComparison.OrdinalIgnoreCase))
            {
                using (Bitmap bitmap = _system.ConvertToBitmap(image))
                {
                    if (bitmap == null)
                    {
                        throw new InvalidOperationException("Imperx SDK görüntüyü PNG için Bitmap'e dönüştüremedi.");
                    }
                    bitmap.Save(path, ImageFormat.Png);
                }
                return;
            }

            if (!_system.SaveImage(image, path))
            {
                throw new IOException("Imperx SDK görüntüyü kaydedemedi: " + path);
            }
        }

        private static void SaveBitmapByExtension(Image image, string path)
        {
            string extension = Path.GetExtension(path).ToLowerInvariant();
            ImageFormat format = extension == ".jpg" || extension == ".jpeg"
                ? ImageFormat.Jpeg
                : extension == ".bmp"
                    ? ImageFormat.Bmp
                    : extension == ".tif" || extension == ".tiff"
                        ? ImageFormat.Tiff
                        : ImageFormat.Png;
            image.Save(path, format);
        }

        private void RefreshStatistics()
        {
            long frames = Interlocked.Read(ref _frameCount);
            long incompleteFrames = Interlocked.Read(ref _incompleteFrameCount);
            double seconds = _fpsWatch.Elapsed.TotalSeconds;
            if (seconds >= 0.5)
            {
                _fps = (frames - _lastMeasuredFrames) / seconds;
                _lastMeasuredFrames = frames;
                _fpsWatch.Restart();
            }

            _frameValue.Text = frames.ToString("N0");
            _fpsValue.Text = _streaming ? _fps.ToString("0.0") : "0.0";
            _resolutionValue.Text = _imageWidth > 0 ? _imageWidth + " × " + _imageHeight : "—";
            int underruns = 0;
            if (_streaming && !DemoMode && Monitor.TryEnter(_streamLifecycleLock))
            {
                try
                {
                    if (_stream != null) underruns = _stream.GetNumUnderrun();
                }
                catch { }
                finally
                {
                    Monitor.Exit(_streamLifecycleLock);
                }
            }
            _droppedValue.Text = underruns.ToString("N0") + " / " + incompleteFrames.ToString("N0");
        }

        private bool IsConnected()
        {
            return DemoMode ? _demoConnected : _device != null;
        }

        private void UpdateControls()
        {
            if (InvokeRequired)
            {
                if (IsHandleCreated && !IsDisposed) BeginInvoke(new Action(UpdateControls));
                return;
            }
            bool connected = IsConnected();
            bool restarting = Volatile.Read(ref _roiRestartPending) != 0;
            _modeBox.Enabled = !connected && !restarting;
            _connectButton.Enabled = !connected && !restarting;
            _disconnectButton.Enabled = connected && !restarting;
            _startButton.Enabled = connected && !_streaming && !restarting;
            _stopButton.Enabled = connected && _streaming && !restarting;
            _captureButton.Enabled = connected && _streaming && !restarting;
            _parametersButton.Enabled = connected && !DemoMode && !restarting;
        }

        private void SetStatus(string message, bool error)
        {
            if (InvokeRequired)
            {
                if (IsHandleCreated && !IsDisposed)
                {
                    BeginInvoke(new Action(delegate { SetStatus(message, error); }));
                }
                return;
            }
            _statusLabel.Text = "●  " + message;
            _statusLabel.ForeColor = error ? Color.FromArgb(255, 104, 104) : Color.FromArgb(74, 222, 128);
        }

        private void ShowError(string message, string title)
        {
            if (_closing || IsDisposed)
            {
                return;
            }
            if (InvokeRequired)
            {
                if (IsHandleCreated) BeginInvoke(new Action(delegate { ShowError(message, title); }));
                return;
            }
            MessageBox.Show(this, message, title, MessageBoxButtons.OK, MessageBoxIcon.Error);
        }

        private void OnFormClosing(object sender, FormClosingEventArgs e)
        {
            _closing = true;
            Disconnect();
            _uiTimer.Stop();
            if (_demoPreview.Image != null) _demoPreview.Image.Dispose();
            if (_system != null)
            {
                try { _system.Dispose(); } catch { }
                _system = null;
            }
        }
    }
}
