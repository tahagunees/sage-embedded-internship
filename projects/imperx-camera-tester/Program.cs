using System;
using System.IO;
using System.Reflection;
using System.Windows.Forms;

namespace ImperxCameraTester
{
    internal static class Program
    {
        internal static readonly string SdkRoot = ResolveSdkRoot();
        internal static readonly string SdkBin = Path.Combine(SdkRoot, "bin", "win64_x64");

        [STAThread]
        private static void Main()
        {
            AppDomain.CurrentDomain.AssemblyResolve += ResolveSdkAssembly;
            ConfigureSdkEnvironment();

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm());
        }

        private static string ResolveSdkRoot()
        {
            string configured = Environment.GetEnvironmentVariable("IPX_CAMSDK_ROOT");
            return string.IsNullOrWhiteSpace(configured)
                ? @"C:\Program Files\Imperx\Imperx Camera SDK"
                : configured;
        }

        private static void ConfigureSdkEnvironment()
        {
            if (!Directory.Exists(SdkBin))
            {
                return;
            }

            Environment.SetEnvironmentVariable("IPX_CAMSDK_ROOT", SdkRoot);
            Environment.SetEnvironmentVariable(
                "PATH",
                SdkBin + Path.PathSeparator + Environment.GetEnvironmentVariable("PATH"));
            Environment.SetEnvironmentVariable(
                "QT_QPA_PLATFORM_PLUGIN_PATH",
                Path.Combine(SdkBin, "platforms"));
            Directory.SetCurrentDirectory(SdkBin);
        }

        private static Assembly ResolveSdkAssembly(object sender, ResolveEventArgs args)
        {
            string name = new AssemblyName(args.Name).Name + ".dll";
            string candidate = Path.Combine(SdkBin, name);
            return File.Exists(candidate) ? Assembly.LoadFrom(candidate) : null;
        }
    }
}
