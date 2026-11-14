// ff-bgm1x 설정·세이브 옮기기
// 공식판 에뮬레이터의 설정 파일과 세이브·상태 저장을 ff-bgm1x 수정판이 읽는 곳으로 복사한다.
// 덮어쓰는 파일은 먼저 받는 쪽의 ff-bgm1x-backup-<시각> 폴더에 백업한다.
// 빌드: C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe (C# 5)
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;

namespace FfBgm1xMover
{
    class CopyItem
    {
        public string Label;
        public string Src;
        public string Dst;
        public bool IsDir;
        public long Size;
        public int Files;
    }

    class Plan
    {
        public string SrcRoot;
        public string DstRoot;
        public string Error;
        public List<CopyItem> Items = new List<CopyItem>();
        public List<string> Notes = new List<string>();
    }

    class Emu
    {
        public string Name;
        public string ExePattern;     // 폴더에서 찾을 실행 파일
        public string[] ProcessNames; // 실행 중인지 확인할 프로세스 이름(앞부분)
        public string OfficialHint;   // 공식판 폴더 칸 안내

        public Emu(string name, string exe, string[] procs, string hint)
        {
            Name = name; ExePattern = exe; ProcessNames = procs; OfficialHint = hint;
        }
    }

    static class Planner
    {
        public static readonly Emu[] Emus = new Emu[]
        {
            new Emu("mGBA (GBA, GB/GBC)", "mGBA.exe", new string[] { "mGBA" },
                "mGBA.exe 가 있는 폴더. 비워 두면 %APPDATA%\\mGBA 를 씁니다 (설치판)."),
            new Emu("VisualBoyAdvance-M (GBA, GB/GBC)", "visualboyadvance-m.exe", new string[] { "visualboyadvance-m" },
                "visualboyadvance-m.exe 가 있는 폴더. 비워 두면 AppData 의 visualboyadvance-m 을 씁니다."),
            new Emu("melonDS (NDS)", "melonDS.exe", new string[] { "melonDS" },
                "melonDS.exe 가 있는 폴더."),
            new Emu("DeSmuME (NDS)", "DeSmuME*.exe", new string[] { "DeSmuME" },
                "DeSmuME 실행 파일이 있는 폴더."),
            new Emu("Azahar (3DS)", "azahar.exe", new string[] { "azahar" },
                "azahar.exe 가 있는 폴더. 비워 두면 %APPDATA%\\Azahar 를 씁니다."),
        };

        static string AppData { get { return Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData); } }
        static string LocalAppData { get { return Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData); } }

        // 폴더(또는 그 안의 app 폴더)에서 실행 파일을 찾아 실행 파일이 있는 폴더를 돌려준다
        public static string FindExeDir(string dir, string pattern)
        {
            if (string.IsNullOrEmpty(dir) || !Directory.Exists(dir)) return null;
            string[] cands = new string[] { dir, Path.Combine(dir, "app") };
            foreach (string c in cands)
            {
                if (Directory.Exists(c) && Directory.GetFiles(c, pattern).Length > 0)
                    return Path.GetFullPath(c);
            }
            return null;
        }

        static bool SamePath(string a, string b)
        {
            if (a == null || b == null) return false;
            return string.Equals(Path.GetFullPath(a).TrimEnd('\\'), Path.GetFullPath(b).TrimEnd('\\'),
                StringComparison.OrdinalIgnoreCase);
        }

        static bool IsUnder(string path, string root)
        {
            string p = Path.GetFullPath(path).TrimEnd('\\') + "\\";
            string r = Path.GetFullPath(root).TrimEnd('\\') + "\\";
            return p.StartsWith(r, StringComparison.OrdinalIgnoreCase);
        }

        // key = value 한 줄을 찾는다 (ini, toml 공통). 따옴표와 toml 의 \\ 를 푼다
        static string ReadValue(string file, string key)
        {
            if (!File.Exists(file)) return null;
            string text = File.ReadAllText(file);
            Match m = Regex.Match(text, @"^[ \t]*" + Regex.Escape(key) + @"[ \t]*=[ \t]*(.*?)[ \t]*\r?$", RegexOptions.Multiline);
            if (!m.Success) return null;
            string v = m.Groups[1].Value;
            if (v.Length >= 2 && v[0] == '"' && v[v.Length - 1] == '"')
                v = v.Substring(1, v.Length - 2).Replace("\\\\", "\\");
            return v;
        }

        static void AddFile(Plan plan, string label, string name)
        {
            string src = Path.Combine(plan.SrcRoot, name);
            if (!File.Exists(src))
            {
                plan.Notes.Add(label + ": " + name + " 이(가) 공식판 쪽에 없어 건너뜁니다.");
                return;
            }
            CopyItem it = new CopyItem();
            it.Label = label; it.Src = src; it.Dst = Path.Combine(plan.DstRoot, name);
            it.Size = new FileInfo(src).Length; it.Files = 1;
            plan.Items.Add(it);
        }

        static void AddDir(Plan plan, string label, string rel)
        {
            string src = Path.GetFullPath(Path.Combine(plan.SrcRoot, rel));
            if (!Directory.Exists(src))
            {
                plan.Notes.Add(label + ": " + src + " 폴더가 없어 건너뜁니다.");
                return;
            }
            CopyItem it = new CopyItem();
            it.Label = label; it.Src = src; it.Dst = Path.GetFullPath(Path.Combine(plan.DstRoot, rel)); it.IsDir = true;
            foreach (string f in EnumerateFiles(src))
            {
                it.Size += new FileInfo(f).Length;
                it.Files++;
            }
            plan.Items.Add(it);
        }

        // 설정 파일에 적힌 세이브 폴더: 상대 경로면 함께 복사, 절대 경로면 두 판이 같이 쓴다
        static void AddConfiguredDir(Plan plan, string label, string value, string defaultRel, string emptyNote)
        {
            if (string.IsNullOrEmpty(value)) value = defaultRel;
            if (string.IsNullOrEmpty(value))
            {
                plan.Notes.Add(label + ": " + emptyNote);
                return;
            }
            if (Path.IsPathRooted(value))
            {
                plan.Notes.Add(label + ": 설정에 적힌 폴더 " + value + " 를 두 판이 그대로 함께 씁니다 (복사 안 함).");
                return;
            }
            AddDir(plan, label, value);
        }

        public static IEnumerable<string> EnumerateFiles(string dir)
        {
            return Directory.EnumerateFiles(dir, "*", SearchOption.AllDirectories);
        }

        public static Plan Build(int emuIndex, string officialInput, string modInput, bool settings, bool saves)
        {
            Plan plan = new Plan();
            Emu emu = Emus[emuIndex];

            string mod = FindExeDir(modInput, emu.ExePattern);
            if (mod == null)
            {
                plan.Error = "ff-bgm1x 폴더에서 " + emu.ExePattern + " 를 찾지 못했습니다.";
                return plan;
            }
            string off = null;
            if (!string.IsNullOrEmpty(officialInput))
            {
                off = FindExeDir(officialInput, emu.ExePattern);
                if (off == null)
                {
                    plan.Error = "공식판 폴더에서 " + emu.ExePattern + " 를 찾지 못했습니다.";
                    return plan;
                }
                if (SamePath(off, mod))
                {
                    plan.Error = "공식판 폴더와 ff-bgm1x 폴더가 같습니다.";
                    return plan;
                }
            }

            switch (emuIndex)
            {
                case 0: BuildMgba(plan, off, mod, settings, saves); break;
                case 1: BuildVbam(plan, off, mod, settings, saves); break;
                case 2: BuildMelonds(plan, off, mod, settings, saves); break;
                case 3: BuildDesmume(plan, off, mod, settings, saves); break;
                case 4: BuildAzahar(plan, off, mod, settings, saves); break;
            }
            if (plan.Error == null && plan.SrcRoot != null && plan.DstRoot != null && SamePath(plan.SrcRoot, plan.DstRoot))
            {
                plan.Items.Clear();
                plan.Notes.Clear();
                plan.Notes.Add("두 판이 이미 같은 곳(" + plan.SrcRoot + ")의 설정·세이브를 함께 씁니다. 옮길 것이 없습니다.");
            }
            return plan;
        }

        // mGBA: portable.ini 가 있으면 실행 파일 폴더, 없으면 %APPDATA%\mGBA
        static void BuildMgba(Plan plan, string off, string mod, bool settings, bool saves)
        {
            if (off != null && File.Exists(Path.Combine(off, "portable.ini"))) plan.SrcRoot = off;
            else plan.SrcRoot = Path.Combine(AppData, "mGBA");
            if (File.Exists(Path.Combine(mod, "portable.ini"))) plan.DstRoot = mod;
            else plan.DstRoot = Path.Combine(AppData, "mGBA");
            if (!Directory.Exists(plan.SrcRoot)) { plan.Error = "공식판 설정 폴더가 없습니다: " + plan.SrcRoot; return; }

            if (settings)
            {
                AddFile(plan, "설정", "config.ini");
                AddFile(plan, "설정(창·단축키)", "qt.ini");
            }
            if (saves)
            {
                string cfg = Path.Combine(plan.SrcRoot, "config.ini");
                string none = "따로 정한 폴더가 없어 롬 옆에 저장됩니다. 두 판이 같은 파일을 씁니다.";
                AddConfiguredDir(plan, "세이브", ReadValue(cfg, "savegamePath"), null, none);
                AddConfiguredDir(plan, "상태 저장", ReadValue(cfg, "savestatePath"), null, none);
                AddConfiguredDir(plan, "치트", ReadValue(cfg, "cheatsPath"), null, "따로 정한 폴더가 없어 롬 옆에 있습니다.");
            }
        }

        // VBA-M: 실행 파일 폴더의 vbam.ini → %APPDATA% → %LOCALAPPDATA% 순서로 찾는다
        static string VbamRoot(string exeDir)
        {
            if (exeDir != null && File.Exists(Path.Combine(exeDir, "vbam.ini"))) return exeDir;
            string roam = Path.Combine(AppData, "visualboyadvance-m");
            if (File.Exists(Path.Combine(roam, "vbam.ini"))) return roam;
            string local = Path.Combine(LocalAppData, "visualboyadvance-m");
            if (File.Exists(Path.Combine(local, "vbam.ini"))) return local;
            return null;
        }

        static void BuildVbam(Plan plan, string off, string mod, bool settings, bool saves)
        {
            plan.SrcRoot = VbamRoot(off);
            if (plan.SrcRoot == null) { plan.Error = "공식판의 vbam.ini 를 찾지 못했습니다."; return; }
            if (File.Exists(Path.Combine(mod, "vbam.ini"))) plan.DstRoot = mod;
            else if (off != null && SamePath(plan.SrcRoot, off)) plan.DstRoot = mod; // 공식판이 휴대용이면 수정판도 휴대용으로
            else plan.DstRoot = plan.SrcRoot;                                         // 둘 다 AppData 를 씀

            if (settings) AddFile(plan, "설정", "vbam.ini");
            if (saves)
            {
                string cfg = Path.Combine(plan.SrcRoot, "vbam.ini");
                string none = "따로 정한 폴더가 없어 롬 옆에 저장됩니다. 두 판이 같은 파일을 씁니다.";
                AddConfiguredDir(plan, "세이브", ReadValue(cfg, "BatteryDir"), null, none);
                AddConfiguredDir(plan, "상태 저장", ReadValue(cfg, "StateDir"), null, none);
            }
        }

        // melonDS: 실행 파일 옆 portable 폴더 → 실행 파일 폴더(휴대용 빌드) → AppData
        static string MelondsRoot(string exeDir)
        {
            if (exeDir != null)
            {
                string p = Path.Combine(exeDir, "portable");
                if (Directory.Exists(p)) return p;
                if (File.Exists(Path.Combine(exeDir, "melonDS.toml"))) return exeDir;
            }
            string local = Path.Combine(LocalAppData, "melonDS");
            if (File.Exists(Path.Combine(local, "melonDS.toml"))) return local;
            string roam = Path.Combine(AppData, "melonDS");
            if (File.Exists(Path.Combine(roam, "melonDS.toml"))) return roam;
            return null;
        }

        static void BuildMelonds(Plan plan, string off, string mod, bool settings, bool saves)
        {
            plan.SrcRoot = MelondsRoot(off);
            if (plan.SrcRoot == null) { plan.Error = "공식판의 melonDS.toml 을 찾지 못했습니다."; return; }
            string modPortable = Path.Combine(mod, "portable");
            plan.DstRoot = Directory.Exists(modPortable) ? modPortable : mod;

            if (settings) AddFile(plan, "설정", "melonDS.toml");
            if (saves)
            {
                string cfg = Path.Combine(plan.SrcRoot, "melonDS.toml");
                string none = "따로 정한 폴더가 없어 롬 옆에 저장됩니다. 두 판이 같은 파일을 씁니다.";
                AddConfiguredDir(plan, "세이브", ReadValue(cfg, "SaveFilePath"), null, none);
                AddConfiguredDir(plan, "상태 저장", ReadValue(cfg, "SavestatePath"), null, none);
                AddConfiguredDir(plan, "치트", ReadValue(cfg, "CheatFilePath"), null, "따로 정한 폴더가 없어 롬 옆에 있습니다.");
            }
        }

        // DeSmuME: 실행 파일 옆 desmume.ini, 세이브는 기본 .\Battery, 상태 저장은 .\States
        static void BuildDesmume(Plan plan, string off, string mod, bool settings, bool saves)
        {
            if (off == null) { plan.Error = "DeSmuME 는 공식판 폴더를 골라야 합니다."; return; }
            plan.SrcRoot = off;
            plan.DstRoot = mod;
            if (settings) AddFile(plan, "설정", "desmume.ini");
            if (saves)
            {
                string cfg = Path.Combine(plan.SrcRoot, "desmume.ini");
                AddConfiguredDir(plan, "세이브", ReadValue(cfg, "Battery"), ".\\Battery", null);
                AddConfiguredDir(plan, "상태 저장", ReadValue(cfg, "States"), ".\\States", null);
                string slots = ReadValue(cfg, "StateSlots");
                if (!string.IsNullOrEmpty(slots)) AddConfiguredDir(plan, "상태 저장 슬롯", slots, null, null);
                AddConfiguredDir(plan, "치트", ReadValue(cfg, "Cheats"), ".\\Cheats", null);
            }
        }

        // Azahar: 실행 파일 옆 user 폴더가 있으면 거기, 없으면 %APPDATA%\Azahar
        static void BuildAzahar(Plan plan, string off, string mod, bool settings, bool saves)
        {
            string roam = Path.Combine(AppData, "Azahar");
            string offUser = off != null ? Path.Combine(off, "user") : null;
            plan.SrcRoot = (offUser != null && Directory.Exists(offUser)) ? offUser : roam;
            if (!Directory.Exists(plan.SrcRoot)) { plan.Error = "공식판 설정 폴더가 없습니다: " + plan.SrcRoot; return; }

            string modUser = Path.Combine(mod, "user");
            if (Directory.Exists(modUser)) plan.DstRoot = modUser;
            else if (SamePath(plan.SrcRoot, offUser)) plan.DstRoot = modUser; // 공식판이 휴대용이면 수정판도 user 폴더로
            else plan.DstRoot = roam;

            if (settings) AddDir(plan, "설정", "config");
            if (saves)
            {
                AddDir(plan, "SD 카드(세이브·설치한 게임)", "sdmc");
                AddDir(plan, "NAND(시스템·세이브)", "nand");
                AddDir(plan, "시스템 데이터", "sysdata");
                AddDir(plan, "상태 저장", "states");
                AddDir(plan, "치트", "cheats");
            }
        }
    }

    class MainForm : Form
    {
        ComboBox emuBox = new ComboBox();
        TextBox offBox = new TextBox();
        TextBox modBox = new TextBox();
        Label hint = new Label();
        CheckBox settingsCheck = new CheckBox();
        CheckBox savesCheck = new CheckBox();
        Button previewButton = new Button();
        Button runButton = new Button();
        TextBox log = new TextBox();
        bool busy;

        public MainForm()
        {
            Text = "ff-bgm1x 설정·세이브 옮기기";
            Font = new Font("Malgun Gothic", 9f);
            AutoScaleMode = AutoScaleMode.Dpi;
            ClientSize = new Size(760, 560);
            MinimumSize = new Size(600, 420);

            TableLayoutPanel t = new TableLayoutPanel();
            t.Dock = DockStyle.Fill;
            t.Padding = new Padding(10);
            t.ColumnCount = 3;
            t.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            t.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            t.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            Controls.Add(t);

            emuBox.DropDownStyle = ComboBoxStyle.DropDownList;
            foreach (Emu e in Planner.Emus) emuBox.Items.Add(e.Name);
            emuBox.Dock = DockStyle.Fill;
            AddRow(t, "에뮬레이터", emuBox, null);

            offBox.Dock = DockStyle.Fill;
            AddRow(t, "공식판 폴더", offBox, BrowseButton(offBox));
            hint.AutoSize = true;
            hint.ForeColor = SystemColors.GrayText;
            AddRow(t, "", hint, null);

            modBox.Dock = DockStyle.Fill;
            AddRow(t, "ff-bgm1x 폴더", modBox, BrowseButton(modBox));

            FlowLayoutPanel opts = new FlowLayoutPanel();
            opts.AutoSize = true;
            opts.Dock = DockStyle.Fill;
            settingsCheck.Text = "설정"; settingsCheck.Checked = true; settingsCheck.AutoSize = true;
            savesCheck.Text = "세이브·상태 저장"; savesCheck.Checked = true; savesCheck.AutoSize = true;
            previewButton.Text = "미리 보기"; previewButton.AutoSize = true;
            runButton.Text = "옮기기"; runButton.AutoSize = true;
            opts.Controls.Add(settingsCheck);
            opts.Controls.Add(savesCheck);
            opts.Controls.Add(previewButton);
            opts.Controls.Add(runButton);
            AddRow(t, "옮길 것", opts, null);

            log.Multiline = true;
            log.ReadOnly = true;
            log.ScrollBars = ScrollBars.Both;
            log.WordWrap = false;
            log.Dock = DockStyle.Fill;
            log.Font = new Font("Consolas", 9f);
            t.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            t.Controls.Add(log, 0, t.RowCount);
            t.SetColumnSpan(log, 3);
            t.RowCount++;

            emuBox.SelectedIndexChanged += delegate { OnEmuChanged(); };
            previewButton.Click += delegate { Preview(); };
            runButton.Click += delegate { Run(); };
            emuBox.SelectedIndex = 0;

            Log("공식판 에뮬레이터의 설정과 세이브를 ff-bgm1x 판이 읽는 곳으로 복사합니다.");
            Log("1) 에뮬레이터를 고르고  2) 두 폴더를 고른 뒤  3) 미리 보기로 확인하고  4) 옮기기를 누르세요.");
            Log("덮어쓰는 파일은 받는 쪽의 ff-bgm1x-backup-<날짜시각> 폴더에 먼저 백업합니다.");
            Log("옮기기 전에 두 에뮬레이터를 모두 닫아 주세요.");
        }

        void AddRow(TableLayoutPanel t, string label, Control c, Control right)
        {
            t.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            Label l = new Label();
            l.Text = label;
            l.AutoSize = true;
            l.Anchor = AnchorStyles.Left;
            t.Controls.Add(l, 0, t.RowCount);
            t.Controls.Add(c, 1, t.RowCount);
            if (right != null) t.Controls.Add(right, 2, t.RowCount);
            t.RowCount++;
        }

        Button BrowseButton(TextBox target)
        {
            Button b = new Button();
            b.Text = "찾아보기";
            b.AutoSize = true;
            b.Click += delegate
            {
                using (FolderBrowserDialog d = new FolderBrowserDialog())
                {
                    if (Directory.Exists(target.Text)) d.SelectedPath = target.Text;
                    if (d.ShowDialog(this) == DialogResult.OK) target.Text = d.SelectedPath;
                }
            };
            return b;
        }

        // 이 프로그램 옆이나 한 단계 아래 폴더에 수정판이 있으면 미리 채운다
        void OnEmuChanged()
        {
            Emu emu = Planner.Emus[emuBox.SelectedIndex];
            hint.Text = emu.OfficialHint;
            modBox.Text = "";
            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
            List<string> cands = new List<string>();
            cands.Add(baseDir);
            try { cands.AddRange(Directory.GetDirectories(baseDir)); } catch { }
            foreach (string c in cands)
            {
                if (Path.GetFileName(c.TrimEnd('\\')).IndexOf("ff-bgm1x", StringComparison.OrdinalIgnoreCase) < 0) continue;
                if (Planner.FindExeDir(c, emu.ExePattern) != null) { modBox.Text = c; break; }
            }
        }

        void Log(string s)
        {
            if (log.InvokeRequired) { log.BeginInvoke(new Action<string>(Log), s); return; }
            log.AppendText(s + "\r\n");
        }

        static string FormatSize(long b)
        {
            if (b >= 1L << 30) return (b / (double)(1L << 30)).ToString("0.00") + " GB";
            if (b >= 1L << 20) return (b / (double)(1L << 20)).ToString("0.0") + " MB";
            if (b >= 1L << 10) return (b / 1024.0).ToString("0.0") + " KB";
            return b + " B";
        }

        Plan MakePlan()
        {
            if (!settingsCheck.Checked && !savesCheck.Checked)
            {
                Log("옮길 것을 하나 이상 고르세요.");
                return null;
            }
            Plan p = Planner.Build(emuBox.SelectedIndex, offBox.Text.Trim(), modBox.Text.Trim(),
                settingsCheck.Checked, savesCheck.Checked);
            log.Clear();
            Log("[" + Planner.Emus[emuBox.SelectedIndex].Name + "]");
            if (p.Error != null)
            {
                Log("문제: " + p.Error);
                return null;
            }
            Log("가져올 곳: " + p.SrcRoot);
            Log("넣을 곳:   " + p.DstRoot);
            Log("");
            long total = 0;
            foreach (CopyItem it in p.Items)
            {
                Log("- " + it.Label + ": " + it.Src + (it.IsDir ? "\\  (파일 " + it.Files + "개, " : "  (") + FormatSize(it.Size) + ")");
                Log("    → " + it.Dst);
                total += it.Size;
            }
            foreach (string n in p.Notes) Log("* " + n);
            if (p.Items.Count > 0)
            {
                Log("");
                Log("모두 " + FormatSize(total) + " 를 복사합니다.");
            }
            return p;
        }

        void Preview()
        {
            if (busy) return;
            MakePlan();
        }

        string RunningEmulator()
        {
            Emu emu = Planner.Emus[emuBox.SelectedIndex];
            foreach (Process pr in Process.GetProcesses())
            {
                foreach (string n in emu.ProcessNames)
                {
                    if (pr.ProcessName.StartsWith(n, StringComparison.OrdinalIgnoreCase)) return pr.ProcessName;
                }
            }
            return null;
        }

        void Run()
        {
            if (busy) return;
            Plan p = MakePlan();
            if (p == null || p.Items.Count == 0) return;
            string running = RunningEmulator();
            if (running != null)
            {
                MessageBox.Show(this, running + " 가 실행 중입니다. 닫은 뒤 다시 눌러 주세요.", Text,
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            if (MessageBox.Show(this, "위 목록대로 복사할까요?\n덮어쓰는 파일은 먼저 백업합니다.", Text,
                    MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK) return;

            busy = true;
            previewButton.Enabled = runButton.Enabled = false;
            Thread th = new Thread(delegate()
            {
                try { Execute(p); }
                catch (Exception ex) { Log("오류: " + ex.Message); }
                finally
                {
                    BeginInvoke(new Action(delegate
                    {
                        busy = false;
                        previewButton.Enabled = runButton.Enabled = true;
                    }));
                }
            });
            th.IsBackground = true;
            th.Start();
        }

        void Execute(Plan p)
        {
            string backupRoot = Path.Combine(p.DstRoot, "ff-bgm1x-backup-" + DateTime.Now.ToString("yyyyMMdd-HHmmss"));
            int copied = 0, backedUp = 0;
            Log("");
            Log("복사를 시작합니다...");
            foreach (CopyItem it in p.Items)
            {
                if (!it.IsDir)
                {
                    if (CopyOne(p, backupRoot, it.Src, it.Dst)) backedUp++;
                    copied++;
                    continue;
                }
                foreach (string f in Planner.EnumerateFiles(it.Src))
                {
                    string rel = f.Substring(it.Src.Length).TrimStart('\\');
                    if (CopyOne(p, backupRoot, f, Path.Combine(it.Dst, rel))) backedUp++;
                    copied++;
                    if (copied % 500 == 0) Log("  " + copied + "개 복사함...");
                }
                Log("  " + it.Label + " 끝");
            }
            Log("");
            Log("완료: 파일 " + copied + "개 복사.");
            if (backedUp > 0) Log("덮어쓴 파일 " + backedUp + "개는 " + backupRoot + " 에 백업했습니다.");
            else Log("덮어쓴 파일이 없어 백업은 만들지 않았습니다.");
        }

        // 받는 쪽에 같은 파일이 있으면 백업한 뒤 복사한다. 백업했으면 true
        static bool CopyOne(Plan p, string backupRoot, string src, string dst)
        {
            bool backed = false;
            if (File.Exists(dst))
            {
                string full = Path.GetFullPath(dst);
                string root = Path.GetFullPath(p.DstRoot).TrimEnd('\\') + "\\";
                string rel = full.StartsWith(root, StringComparison.OrdinalIgnoreCase)
                    ? full.Substring(root.Length)
                    : Path.Combine("_other", full.Replace(":", "").TrimStart('\\'));
                string bak = Path.Combine(backupRoot, rel);
                Directory.CreateDirectory(Path.GetDirectoryName(bak));
                File.Copy(dst, bak, true);
                backed = true;
            }
            Directory.CreateDirectory(Path.GetDirectoryName(dst));
            File.Copy(src, dst, true);
            return backed;
        }
    }

    static class Program
    {
        [DllImport("user32.dll")]
        static extern bool SetProcessDPIAware();

        [STAThread]
        static void Main()
        {
            try { SetProcessDPIAware(); } catch { }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm());
        }
    }
}
