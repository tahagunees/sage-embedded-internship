using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace BldcHallMonitor
{
    public class RotorView : Control
    {
        public int Step;
        public bool IsLive, AllowSelection;
        public event Action<int> StepSelected;
        public RotorView() { DoubleBuffered = true; BackColor = Color.White; }
        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            Graphics g = e.Graphics; g.SmoothingMode = SmoothingMode.AntiAlias;
            float d = Math.Min(Width - 90, Height - 64);
            if (d < 80) return;
            float cx = Width / 2f, cy = (Height - 24) / 2f;
            var circle = new RectangleF(cx - d / 2, cy - d / 2, d, d);
            Color ink = Color.FromArgb(25, 44, 64), teal = Color.FromArgb(0, 145, 135);
            using (var border = new Pen(Color.White, 4))
            using (var labelFont = new Font("Segoe UI", 11, FontStyle.Bold))
            using (var smallFont = new Font("Segoe UI", 9))
            using (var format = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center })
            {
                for (int i = 0; i < 6; i++)
                {
                    bool active = Step == i + 1 && IsLive;
                    using (var brush = new SolidBrush(active ? teal : Color.FromArgb(231, 237, 243)))
                        g.FillPie(brush, circle.X, circle.Y, d, d, -90 + i * 60, 60);
                    g.DrawPie(border, circle.X, circle.Y, d, d, -90 + i * 60, 60);
                    double angle = (-60 + i * 60) * Math.PI / 180;
                    float x = cx + (float)Math.Cos(angle) * d * .37f;
                    float y = cy + (float)Math.Sin(angle) * d * .37f;
                    using (var brush = new SolidBrush(active ? Color.White : ink))
                    {
                        g.DrawString("ADIM " + (i + 1), labelFont, brush, x, y - 8, format);
                        g.DrawString(Convert.ToString(HallFrame.Sequence[i], 2).PadLeft(3, '0'), smallFont, brush, x, y + 12, format);
                    }
                }
                g.FillEllipse(Brushes.White, cx - d * .23f, cy - d * .23f, d * .46f, d * .46f);
                using (var numberFont = new Font("Segoe UI", 32, FontStyle.Bold))
                using (var brush = new SolidBrush(ink))
                {
                    g.DrawString(IsLive && Step > 0 ? Step.ToString() : "—", numberFont, brush, cx, cy - 9, format);
                    g.DrawString("elektriksel sektör", smallFont, brush, cx, cy + 26, format);
                    g.DrawString(AllowSelection ? "Test için bir sektöre tıkla" : "Hall verisine göre canlı konum", smallFont, brush, Width / 2f, Height - 15, format);
                }
            }
        }
        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e);
            if (!AllowSelection || e.Button != MouseButtons.Left) return;
            float dx = e.X - Width / 2f, dy = e.Y - (Height - 24) / 2f;
            float radius = Math.Min(Width - 90, Height - 64) / 2f;
            double distance = Math.Sqrt(dx * dx + dy * dy);
            if (distance > radius || distance < radius * .46) return;
            double degrees = (Math.Atan2(dy, dx) * 180 / Math.PI + 90 + 360) % 360;
            StepSelected?.Invoke((int)(degrees / 60) + 1);
        }
    }

    public class SignalView : Control
    {
        private class Sample { public DateTime Time; public int Hall; }
        private readonly List<Sample> samples = new List<Sample>();
        public SignalView() { DoubleBuffered = true; BackColor = Color.White; }
        public void Add(int hall) { samples.Add(new Sample { Time = DateTime.UtcNow, Hall = hall }); if (samples.Count > 1200) samples.RemoveRange(0, 200); Invalidate(); }
        public void Clear() { samples.Clear(); Invalidate(); }
        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            var g = e.Graphics;
            using (var font = new Font("Segoe UI", 9))
            using (var grid = new Pen(Color.FromArgb(233, 238, 242)))
            {
                Color[] colors = { Color.FromArgb(0, 145, 135), Color.FromArgb(55, 113, 209), Color.FromArgb(199, 123, 30) };
                DateTime now = DateTime.UtcNow;
                float left = 55, span = Math.Max(1, Width - 75);
                for (int bit = 0; bit < 3; bit++)
                {
                    float baseline = 22 + bit * 28;
                    g.DrawString("H" + (bit + 1), font, Brushes.DimGray, 10, baseline - 7);
                    g.DrawLine(grid, left, baseline + 7, Width - 20, baseline + 7);
                    using (var pen = new Pen(colors[bit], 2))
                    {
                        for (int j = 1; j < samples.Count; j++)
                        {
                            var a = samples[j - 1]; var b = samples[j];
                            float x1 = left + span * (1 - (float)(now - a.Time).TotalSeconds / 10);
                            float x2 = left + span * (1 - (float)(now - b.Time).TotalSeconds / 10);
                            if (x2 < left || (b.Time - a.Time).TotalSeconds > 1) continue;
                            float y1 = baseline + (((a.Hall >> (2 - bit)) & 1) == 1 ? -7 : 7);
                            float y2 = baseline + (((b.Hall >> (2 - bit)) & 1) == 1 ? -7 : 7);
                            g.DrawLine(pen, Math.Max(left, x1), y1, x2, y1);
                            g.DrawLine(pen, x2, y1, x2, y2);
                        }
                    }
                }
                g.DrawString("Son 10 saniye · bilgisayara ulaşan örnekler", font, Brushes.Gray, left, 97);
            }
        }
    }
}
