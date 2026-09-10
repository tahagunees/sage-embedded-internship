using System;
using System.Globalization;

namespace BldcHallMonitor
{
    public sealed class HallFrame
    {
        public static readonly int[] Sequence = { 1, 5, 4, 6, 2, 3 };
        public uint SequenceNumber, Milliseconds, Faults, ButtonPresses;
        public int Hall, Step, Direction, RelativeSteps;
        public string Mode;
        public string Bits { get { return Convert.ToString(Hall, 2).PadLeft(3, '0'); } }
        public static int Decode(int hall) { return Array.IndexOf(Sequence, hall) + 1; }
        public static bool TryParse(string line, out HallFrame frame)
        {
            frame = null;
            string[] p = line.Trim().Split(',');
            if (p.Length != 10 || p[0] != "F1" || (p[3] != "SIM" && p[3] != "HALL")) return false;
            var f = new HallFrame { Mode = p[3] };
            if (!uint.TryParse(p[1], NumberStyles.None, CultureInfo.InvariantCulture, out f.SequenceNumber)
                || !uint.TryParse(p[2], NumberStyles.None, CultureInfo.InvariantCulture, out f.Milliseconds)
                || !int.TryParse(p[4], out f.Hall) || !int.TryParse(p[5], out f.Step)
                || !int.TryParse(p[6], out f.Direction) || !int.TryParse(p[7], out f.RelativeSteps)
                || !uint.TryParse(p[8], out f.Faults) || !uint.TryParse(p[9], out f.ButtonPresses)) return false;
            if (f.Hall < 0 || f.Hall > 7 || f.Step != Decode(f.Hall) || Math.Abs((long)f.Direction) > 1) return false;
            if (f.Step == 0 && f.Direction != 0) return false;
            frame = f; return true;
        }
    }

    public sealed class DemoMotor
    {
        private int step = 1, relative, direction;
        private uint faults, packets;
        private DateTime moved = DateTime.MinValue;
        private readonly DateTime started = DateTime.UtcNow;
        public void Command(string command)
        {
            int next = step;
            if (command == "N") next = step % 6 + 1;
            else if (command == "P") next = (step + 4) % 6 + 1;
            else if (command.Length == 2 && command[0] == 'S' && command[1] >= '1' && command[1] <= '6') next = command[1] - '0';
            else if (command == "R" || command == "M0") { relative = 0; faults = 0; direction = 0; if (command == "M0") step = 1; return; }
            if (step == next) return;
            int delta = (next - step + 6) % 6;
            direction = delta == 1 ? 1 : delta == 5 ? -1 : 0;
            if (direction == 0) ++faults;
            relative += direction; step = next; moved = DateTime.UtcNow;
        }
        public HallFrame Read()
        {
            return new HallFrame { SequenceNumber = packets++, Milliseconds = (uint)(DateTime.UtcNow - started).TotalMilliseconds,
                Mode = "SIM", Hall = HallFrame.Sequence[step - 1], Step = step,
                Direction = (DateTime.UtcNow - moved).TotalMilliseconds > 800 ? 0 : direction,
                RelativeSteps = relative, Faults = faults };
        }
    }
}
