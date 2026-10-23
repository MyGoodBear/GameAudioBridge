using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Windows.Forms;
using Microsoft.Win32;

namespace GameAudioBridgeGui
{
    internal sealed class GameProfile
    {
        public string Name;
        public Dictionary<string, string> Values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        public string Get(string key, string fallback = "") { string v; return Values.TryGetValue(key, out v) ? v : fallback; }
        public void Set(string key, string value) { Values[key] = value ?? ""; }
        public bool Enabled { get { return String.Equals(Get("Enabled", "FALSE"), "TRUE", StringComparison.OrdinalIgnoreCase) || String.Equals(Get("Intercept", "FALSE"), "TRUE", StringComparison.OrdinalIgnoreCase); } set { Set("Enabled", value ? "TRUE" : "FALSE"); } }
    }

    internal sealed class IniDocument
    {
        public readonly List<GameProfile> Profiles = new List<GameProfile>();
        public readonly Dictionary<string, string> Settings = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        public static IniDocument Load(string path)
        {
            IniDocument doc = new IniDocument();
            if (!File.Exists(path)) return doc;
            GameProfile current = null;
            foreach (string raw in File.ReadAllLines(path, Encoding.UTF8))
            {
                string line = raw.Trim();
                if (line.Length == 0 || line.StartsWith(";") || line.StartsWith("#")) continue;
                if (line.StartsWith("[") && line.EndsWith("]") && line.Length > 2)
                {
                    string name = line.Substring(1, line.Length - 2).Trim();
                    if (name.Equals("Settings", StringComparison.OrdinalIgnoreCase)) { current = null; continue; }
                    current = doc.Profiles.FirstOrDefault(p => p.Name.Equals(name, StringComparison.OrdinalIgnoreCase));
                    if (current == null) { current = new GameProfile { Name = name }; doc.Profiles.Add(current); }
                    continue;
                }
                int equals = line.IndexOf('=');
                if (equals <= 0) continue;
                string key = line.Substring(0, equals).Trim();
                string value = line.Substring(equals + 1).Trim();
                if (current == null) doc.Settings[key] = value;
                else current.Set(key, value);
            }
            return doc;
        }
        public void Save(string path)
        {
            StringBuilder sb = new StringBuilder();
            sb.AppendLine("; Game Audio Bridge: only game profiles are stored here.");
            sb.AppendLine("; Enabled=TRUE means interception is enabled for this game.");
            foreach (GameProfile p in Profiles)
            {
                sb.AppendLine(); sb.Append('[').Append(p.Name).AppendLine("]");
                foreach (KeyValuePair<string, string> kv in p.Values.OrderBy(x => x.Key, StringComparer.OrdinalIgnoreCase))
                    sb.Append(kv.Key).Append('=').AppendLine(kv.Value ?? "");
            }
            File.WriteAllText(path, sb.ToString(), new UTF8Encoding(false));
        }
    }

    internal sealed class GameAudioBridgeForm : Form
    {
        private readonly string root;
        private readonly string iniPath;
        private readonly IniDocument config;
        private readonly ListBox disabledList = new ListBox();
        private readonly ListBox enabledList = new ListBox();
        private readonly Label status = new Label();
        private readonly ComboBox renderMode = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList };
        private string selectedRenderMode = "OpenALCreative";
        private readonly Dictionary<string, GameProfile> rowProfiles = new Dictionary<string, GameProfile>(StringComparer.OrdinalIgnoreCase);

        public GameAudioBridgeForm()
        {
            root = AppDomain.CurrentDomain.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            iniPath = Path.Combine(root, "GameAudioBridge.ini");
            config = IniDocument.Load(iniPath);
            Text = "Game Audio Bridge 1.0 — управление играми";
            StartPosition = FormStartPosition.CenterScreen; Size = new Size(1100, 690); MinimumSize = new Size(930, 570);
            Font = new Font("Segoe UI", 9F); AllowDrop = false;

            Label intro = new Label { Text = "Добавляй EXE-файлы или папки. Разрядность определяется автоматически. Настройки хранятся рядом с GameAudioBridge.exe.", Left = 14, Top = 12, Width = 1050, Height = 38, Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right };
            Controls.Add(intro);
            Label modeLabel = new Label { Text = "Режим вывода:", Left = 14, Top = 57, Width = 110, Height = 24 };
            renderMode.SetBounds(124, 52, 300, 28);
            renderMode.Items.Add("Аппаратный OpenAL Creative");
            renderMode.Items.Add("ASIO (драйвер звуковой карты)");
            renderMode.Items.Add("WASAPI Exclusive");
            renderMode.SelectedIndex = 0;
            renderMode.SelectedIndexChanged += (s, e) =>
            {
                selectedRenderMode = renderMode.SelectedIndex == 1 ? "ASIO" : renderMode.SelectedIndex == 2 ? "WASAPIExclusive" : "OpenALCreative";
                if (selectedRenderMode == "OpenALCreative") status.Text = "Выбран OpenAL Creative. Проверка драйвера доступна кнопкой выше.";
                else status.Text = "Режим " + selectedRenderMode + " выбран только в интерфейсе; аудиобэкенд ещё не реализован в этой сборке. Запуск в этом режиме будет заблокирован.";
            };
            Button probe = MakeButton("Проверить Creative OpenAL", 440, 50, 220); probe.Anchor = AnchorStyles.Top | AnchorStyles.Left; probe.Click += (s, e) => ProbeCreativeOpenAL(true);
            Label leftTitle = new Label { Text = "Перехват выключен", Left = 14, Top = 86, Width = 450, Height = 24 };
            Label rightTitle = new Label { Text = "Перехват включён", Left = 620, Top = 86, Width = 450, Height = 24 };
            Controls.Add(modeLabel); Controls.Add(renderMode); Controls.Add(probe); Controls.Add(leftTitle); Controls.Add(rightTitle);
            disabledList.SetBounds(14, 112, 455, 408); disabledList.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left; disabledList.HorizontalScrollbar = true; disabledList.SelectionMode = SelectionMode.MultiExtended;
            enabledList.SetBounds(620, 112, 455, 408); enabledList.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Right; enabledList.HorizontalScrollbar = true; enabledList.SelectionMode = SelectionMode.MultiExtended;
            Controls.Add(disabledList); Controls.Add(enabledList);
            disabledList.MouseDoubleClick += (s, e) => ToggleRowOnDoubleClick(disabledList, true, e);
            enabledList.MouseDoubleClick += (s, e) => ToggleRowOnDoubleClick(enabledList, false, e);
            Button addExe = MakeButton("Добавить EXE…", 14, 532, 130); addExe.Anchor = AnchorStyles.Bottom | AnchorStyles.Left; addExe.Click += AddExe;
            Button addFolder = MakeButton("Добавить папку…", 152, 532, 145); addFolder.Anchor = AnchorStyles.Bottom | AnchorStyles.Left; addFolder.Click += AddFolder;
            Button remove = MakeButton("Удалить", 14, 570, 130); remove.Anchor = AnchorStyles.Bottom | AnchorStyles.Left; remove.Click += RemoveSelected;
            Button on = MakeButton("Включить →", 482, 220, 122); on.Anchor = AnchorStyles.Top; on.Click += EnableSelected;
            Button off = MakeButton("← Выключить", 482, 266, 122); off.Anchor = AnchorStyles.Top; off.Click += DisableSelected;
            Button launch = MakeButton("Запустить выбранную игру", 620, 532, 455); launch.Anchor = AnchorStyles.Bottom | AnchorStyles.Right; launch.Click += LaunchSelected;
            status.SetBounds(152, 573, 900, 24); status.Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right; status.Text = "INI: " + iniPath;
            Controls.Add(addExe); Controls.Add(addFolder); Controls.Add(remove); Controls.Add(on); Controls.Add(off); Controls.Add(launch); Controls.Add(status);
            RefreshLists();
            try { config.Save(iniPath); } catch (Exception ex) { status.Text = "Не удалось сохранить INI: " + ex.Message; }
        }

        private Button MakeButton(string text, int x, int y, int width) { return new Button { Text = text, Left = x, Top = y, Width = width, Height = 32 }; }
        private static string Quote(string s) { return "\"" + s.Replace("\"", "\\\"") + "\""; }
        private string ResolveInstallRoot(GameProfile p)
        {
            string gamePath = p.Get("GamePath").Trim().Trim('"');
            if (!String.IsNullOrWhiteSpace(gamePath)) return Environment.ExpandEnvironmentVariables(gamePath);
            string reg = p.Get("RegPath").Trim();
            if (String.IsNullOrWhiteSpace(reg)) return "";
            int split = reg.LastIndexOf('\\'); if (split <= 0 || split == reg.Length - 1) return "";
            string keyPath = reg.Substring(0, split); string valueName = reg.Substring(split + 1);
            string hivePart = ""; string subKey = keyPath;
            if (keyPath.StartsWith("HKEY_LOCAL_MACHINE\\", StringComparison.OrdinalIgnoreCase)) { hivePart = "HKLM"; subKey = keyPath.Substring("HKEY_LOCAL_MACHINE\\".Length); }
            else if (keyPath.StartsWith("HKLM\\", StringComparison.OrdinalIgnoreCase)) { hivePart = "HKLM"; subKey = keyPath.Substring(5); }
            else if (keyPath.StartsWith("HKEY_CURRENT_USER\\", StringComparison.OrdinalIgnoreCase)) { hivePart = "HKCU"; subKey = keyPath.Substring("HKEY_CURRENT_USER\\".Length); }
            else if (keyPath.StartsWith("HKCU\\", StringComparison.OrdinalIgnoreCase)) { hivePart = "HKCU"; subKey = keyPath.Substring(5); }
            if (hivePart.Length == 0) return "";
            RegistryHive hive = hivePart == "HKLM" ? RegistryHive.LocalMachine : RegistryHive.CurrentUser;
            RegistryView[] views = { RegistryView.Registry64, RegistryView.Registry32, RegistryView.Default };
            foreach (RegistryView view in views)
            {
                try { using (RegistryKey baseKey = RegistryKey.OpenBaseKey(hive, view)) using (RegistryKey key = baseKey.OpenSubKey(subKey)) { if (key != null) { object val = key.GetValue(valueName); if (val != null && Directory.Exists(val.ToString())) return val.ToString(); } } }
                catch { }
            }
            return "";
        }
        private string ResolveSearchRoot(GameProfile p)
        {
            string path = ResolveInstallRoot(p);
            if (String.IsNullOrWhiteSpace(path)) return "";
            string sub = p.Get("SubDir").Trim().Trim('"');
            if (!String.IsNullOrWhiteSpace(sub)) path = Path.Combine(path, Environment.ExpandEnvironmentVariables(sub));
            return path;
        }
        private static string DetectArchitecture(string path)
        {
            try
            {
                using (FileStream fs = File.Open(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
                using (BinaryReader br = new BinaryReader(fs))
                {
                    if (fs.Length < 64) return "?"; fs.Position = 0x3c; int pe = br.ReadInt32(); if (pe < 0 || pe > fs.Length - 6) return "?";
                    fs.Position = pe; if (br.ReadUInt32() != 0x4550) return "?"; ushort machine = br.ReadUInt16();
                    if (machine == 0x14c) return "x86"; if (machine == 0x8664) return "x64"; if (machine == 0xaa64) return "ARM64"; return "?";
                }
            } catch { return "?"; }
        }
        private List<string> FindExecutables(GameProfile p)
        {
            string exact = p.Get("ExePath").Trim().Trim('"');
            if (!String.IsNullOrWhiteSpace(exact) && File.Exists(exact)) return new List<string> { Path.GetFullPath(exact) };
            string rootPath = ResolveSearchRoot(p);
            if (String.IsNullOrWhiteSpace(rootPath) || !Directory.Exists(rootPath)) return new List<string>();
            try { return Directory.GetFiles(rootPath, "*.exe", SearchOption.AllDirectories).Where(f => !f.Contains("\\_CommonRedist\\") && !f.Contains("\\redist\\")).Take(3000).OrderBy(f => f).ToList(); }
            catch { try { return Directory.GetFiles(rootPath, "*.exe", SearchOption.TopDirectoryOnly).OrderBy(f => f).ToList(); } catch { return new List<string>(); } }
        }
        private string Display(GameProfile p)
        {
            string exe = p.Get("ExePath"); string arch = File.Exists(exe) ? DetectArchitecture(exe) : "?";
            if (arch == "?")
            {
                List<string> files = FindExecutables(p);
                List<string> architectures = files.Select(DetectArchitecture).Where(a => a != "?").Distinct(StringComparer.OrdinalIgnoreCase).ToList();
                if (architectures.Count == 1) arch = architectures[0];
                else if (architectures.Count > 1) arch = String.Join("/", architectures.OrderBy(a => a));
            }
            string location = String.IsNullOrWhiteSpace(exe) ? (p.Get("GamePath") + (p.Get("SubDir").Length > 0 ? "\\" + p.Get("SubDir") : "")) : exe;
            return "[" + arch + "]  " + p.Name + "  —  " + location;
        }
        private void RefreshLists()
        {
            disabledList.BeginUpdate(); enabledList.BeginUpdate(); disabledList.Items.Clear(); enabledList.Items.Clear(); rowProfiles.Clear();
            foreach (GameProfile p in config.Profiles)
            {
                string row = Display(p); int suffix = 2; string unique = row; while (rowProfiles.ContainsKey(unique)) unique = row + " (" + suffix++ + ")";
                rowProfiles[unique] = p; if (p.Enabled) enabledList.Items.Add(unique); else disabledList.Items.Add(unique);
            }
            disabledList.EndUpdate(); enabledList.EndUpdate(); status.Text = "INI: " + iniPath + "    |    Игр: " + config.Profiles.Count;
        }
        private string UniqueName(string desired)
        {
            string name = desired; int i = 2;
            while (config.Profiles.Any(p => p.Name.Equals(name, StringComparison.OrdinalIgnoreCase))) name = desired + " (" + i++ + ")";
            return name;
        }
        private void AddProfileFromExe(string path)
        {
            if (!File.Exists(path) || DetectArchitecture(path) == "?") return;
            string full = Path.GetFullPath(path);
            if (config.Profiles.Any(p => p.Get("ExePath").Equals(full, StringComparison.OrdinalIgnoreCase))) return;
            GameProfile profile = new GameProfile(); profile.Name = UniqueName(Path.GetFileNameWithoutExtension(full));
            profile.Set("RegPath", ""); profile.Set("GamePath", Path.GetDirectoryName(full)); profile.Set("ExePath", full); profile.Set("SubDir", "");
            profile.Set("Enabled", "FALSE"); profile.Set("Preset", "1"); profile.Set("RootDirInstallOption", "FALSE"); profile.Set("Buffers", "4"); profile.Set("Duration", "25"); profile.Set("MaxVoiceCount", "128"); profile.Set("DisableDirectMusic", "FALSE");
            config.Profiles.Add(profile);
        }
        private void AddExe(object sender, EventArgs e)
        {
            using (OpenFileDialog dlg = new OpenFileDialog()) { dlg.Title = "Выбери исполняемые файлы игр"; dlg.Filter = "Программы (*.exe)|*.exe|Все файлы (*.*)|*.*"; dlg.Multiselect = true; if (dlg.ShowDialog(this) != DialogResult.OK) return; int before = config.Profiles.Count; foreach (string f in dlg.FileNames) AddProfileFromExe(f); SaveAndRefresh("Добавлено игр: " + (config.Profiles.Count - before)); }
        }
        private void AddFolder(object sender, EventArgs e)
        {
            using (FolderBrowserDialog dlg = new FolderBrowserDialog()) { dlg.Description = "Выбери папку с играми — будут найдены EXE во вложенных папках"; if (dlg.ShowDialog(this) != DialogResult.OK) return; int before = config.Profiles.Count; try { foreach (string f in Directory.GetFiles(dlg.SelectedPath, "*.exe", SearchOption.AllDirectories).Take(3000)) AddProfileFromExe(f); } catch (Exception ex) { MessageBox.Show(this, ex.Message, "Ошибка сканирования", MessageBoxButtons.OK, MessageBoxIcon.Error); } SaveAndRefresh("Добавлено игр: " + (config.Profiles.Count - before)); }
        }
        private void ToggleRowOnDoubleClick(ListBox list, bool enable, MouseEventArgs e)
        {
            int index = list.IndexFromPoint(e.Location);
            if (index < 0 || index >= list.Items.Count) return;
            list.SelectedIndex = index;
            GameProfile p;
            if (!rowProfiles.TryGetValue(list.Items[index].ToString(), out p)) return;
            p.Enabled = enable;
            SaveAndRefresh(enable ? "Перехват включён двойным щелчком." : "Перехват выключен двойным щелчком.");
        }

        private void MoveSelected(ListBox from, bool enable)
        {
            foreach (object item in from.SelectedItems) { GameProfile p; if (rowProfiles.TryGetValue(item.ToString(), out p)) p.Enabled = enable; }
            SaveAndRefresh(enable ? "Перехват включён для выбранных игр." : "Перехват выключен для выбранных игр.");
        }
        private void EnableSelected(object sender, EventArgs e) { MoveSelected(disabledList, true); }
        private void DisableSelected(object sender, EventArgs e) { MoveSelected(enabledList, false); }
        private void RemoveSelected(object sender, EventArgs e)
        {
            List<GameProfile> remove = new List<GameProfile>(); foreach (object item in disabledList.SelectedItems) { GameProfile p; if (rowProfiles.TryGetValue(item.ToString(), out p)) remove.Add(p); } foreach (object item in enabledList.SelectedItems) { GameProfile p; if (rowProfiles.TryGetValue(item.ToString(), out p)) remove.Add(p); }
            foreach (GameProfile p in remove) config.Profiles.Remove(p); SaveAndRefresh("Удалено записей: " + remove.Count);
        }
        private string ChooseExecutable(GameProfile p)
        {
            List<string> candidates = FindExecutables(p);
            if (candidates.Count == 1) return candidates[0];
            if (candidates.Count == 0) { MessageBox.Show(this, "Не удалось найти EXE. Проверь GamePath, RegPath и SubDir в GameAudioBridge.ini либо добавь EXE через кнопку «Добавить EXE».", "Файл игры не найден", MessageBoxButtons.OK, MessageBoxIcon.Warning); return null; }
            using (Form chooser = new Form())
            {
                chooser.Text = "Выбери EXE для " + p.Name; chooser.StartPosition = FormStartPosition.CenterParent; chooser.Size = new Size(850, 500); chooser.MinimizeBox = false; chooser.MaximizeBox = true;
                ListBox list = new ListBox { Dock = DockStyle.Fill, HorizontalScrollbar = true };
                foreach (string f in candidates) list.Items.Add("[" + DetectArchitecture(f) + "]  " + f);
                Button ok = new Button { Text = "Выбрать", Dock = DockStyle.Bottom, Height = 34, DialogResult = DialogResult.OK }; chooser.Controls.Add(list); chooser.Controls.Add(ok); chooser.AcceptButton = ok;
                if (chooser.ShowDialog(this) != DialogResult.OK || list.SelectedIndex < 0) return null;
                return candidates[list.SelectedIndex];
            }
        }
        private bool ProbeCreativeOpenAL(bool showMessage, string arch = null)
        {
            if (String.IsNullOrEmpty(arch)) arch = Environment.Is64BitOperatingSystem ? "x64" : "x86";
            string launcher = Path.Combine(root, arch, "GameAudioBridgeLauncher.exe");
            if (!File.Exists(launcher))
            {
                string missing = "Не найден диагностический лаунчер: " + launcher;
                if (showMessage) MessageBox.Show(this, missing, "Creative OpenAL недоступен", MessageBoxButtons.OK, MessageBoxIcon.Error);
                status.Text = missing; return false;
            }
            try
            {
                ProcessStartInfo psi = new ProcessStartInfo(launcher, "--probe-openal") { UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true, CreateNoWindow = true, WorkingDirectory = root };
                using (Process proc = Process.Start(psi))
                {
                    string stdout = proc.StandardOutput.ReadToEnd(); string stderr = proc.StandardError.ReadToEnd(); proc.WaitForExit();
                    string report = (stdout + Environment.NewLine + stderr).Trim();
                    if (proc.ExitCode == 0)
                    {
                        status.Text = "Creative OpenAL обнаружен; аппаратное ускорение требует отдельной проверки.";
                        if (showMessage) MessageBox.Show(this, report + "\r\n\r\nПроверка подтверждает обнаружение реализации/устройства Creative, но не может гарантировать аппаратное ускорение для каждого игрового аудиопути.", "Creative OpenAL обнаружен", MessageBoxButtons.OK, MessageBoxIcon.Information);
                        return true;
                    }
                    status.Text = "Creative OpenAL не подтверждён; режим перехвата не запущен.";
                    if (showMessage) MessageBox.Show(this, report + "\r\n\r\nGame Audio Bridge не будет заявлять, что Creative OpenAL работает, и не запустит игру в этом режиме.", "Creative OpenAL не поддерживается", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return false;
                }
            }
            catch (Exception ex)
            {
                if (showMessage) MessageBox.Show(this, ex.Message, "Ошибка проверки OpenAL", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return false;
            }
        }

        private void LaunchSelected(object sender, EventArgs e)
        {
            if (enabledList.SelectedItems.Count != 1) { MessageBox.Show(this, "Выбери одну игру в списке «Перехват включён».", "Game Audio Bridge", MessageBoxButtons.OK, MessageBoxIcon.Information); return; }
            GameProfile p; if (!rowProfiles.TryGetValue(enabledList.SelectedItem.ToString(), out p)) return;
            string exe = ChooseExecutable(p); if (String.IsNullOrEmpty(exe)) return;
            string mode = selectedRenderMode;
            if (mode == "ASIO" || mode == "WASAPIExclusive")
            {
                MessageBox.Show(this,
                    "Режим " + mode + " пока не реализован в аудиодвижке этой версии. Игра не запущена, чтобы не создавать ложное впечатление, что выбранный вывод работает.\r\n\r\nASIO требует отдельного ASIO-host/backend, а WASAPI Exclusive — отдельного эксклюзивного output backend.",
                    "Режим вывода пока недоступен", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            string arch = DetectArchitecture(exe); if (arch != "x86" && arch != "x64") { MessageBox.Show(this, "Архитектура EXE не поддерживается: " + arch, "Game Audio Bridge", MessageBoxButtons.OK, MessageBoxIcon.Warning); return; }
            if (!ProbeCreativeOpenAL(true, arch)) return;
            string launcher = Path.Combine(root, arch, "GameAudioBridgeLauncher.exe"); string dll = Path.Combine(root, arch, "GameAudioBridgeHook.dll");
            if (!File.Exists(launcher) || !File.Exists(dll)) { MessageBox.Show(this, "Не найдены файлы перехвата " + arch + " в папке:\r\n" + Path.Combine(root, arch), "Game Audio Bridge", MessageBoxButtons.OK, MessageBoxIcon.Error); return; }
            DialogResult answer = MessageBox.Show(this, "Игра:\r\n" + exe + "\r\n\r\nРазрядность: " + arch + "\r\n\r\nЗапустить с экспериментальным перехватом? Не используйте с античитом или защищёнными процессами.", "Game Audio Bridge", MessageBoxButtons.OKCancel, MessageBoxIcon.Warning);
            if (answer != DialogResult.OK) return;
            try { ProcessStartInfo psi = new ProcessStartInfo(launcher, "--launch " + Quote(exe) + " " + Quote(dll)); psi.WorkingDirectory = Path.GetDirectoryName(exe); Process.Start(psi); status.Text = "Запущено: " + p.Name + " (" + arch + ")"; }
            catch (Exception ex) { MessageBox.Show(this, ex.Message, "Не удалось запустить игру", MessageBoxButtons.OK, MessageBoxIcon.Error); }
        }
        private void SaveAndRefresh(string message)
        {
            try { config.Save(iniPath); RefreshLists(); status.Text = message + "  |  INI: " + iniPath; }
            catch (Exception ex) { MessageBox.Show(this, "Не удалось сохранить INI рядом с EXE. Если программа установлена в защищённую папку, перенеси её в папку с правом записи.\r\n" + ex.Message, "Ошибка сохранения", MessageBoxButtons.OK, MessageBoxIcon.Error); }
        }
    }

    internal static class Program
    {
        [STAThread]
        private static void Main()
        {
            Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
            try { Application.Run(new GameAudioBridgeForm()); }
            catch (Exception ex) { MessageBox.Show(ex.ToString(), "Game Audio Bridge — ошибка запуска", MessageBoxButtons.OK, MessageBoxIcon.Error); }
        }
    }
}
