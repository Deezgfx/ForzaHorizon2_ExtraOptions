using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

public static class Xref
{
    static byte[] img;
    // .text: file 0x600 -> rva 0x1000, size 0x1DB3200
    const int TextFile = 0x600, TextRva = 0x1000, TextSize = 0x1DB3200;

    public static void Load(string path) { if (img == null) img = File.ReadAllBytes(path); }

    public static int FileToRva(int off)
    {
        if (off >= 0x600 && off < 0x1DB3800) return off + 0xA00;
        if (off >= 0x1DCD400 && off < 0x2C44400) return off + 0x2C00;
        if (off >= 0x2C44400 && off < 0x34A8E00) return off + 0x2C00;
        return -1;
    }

    public static int RvaToFile(int rva)
    {
        if (rva >= 0x1000 && rva < 0x1000 + TextSize) return rva - 0xA00;
        if (rva >= 0x1DD0000 && rva < 0x2C47000) return rva - 0x2C00;
        if (rva >= 0x2C47000 && rva < 0x2C47000 + 0x864A00) return rva - 0x2C00;
        return -1;
    }

    // RIP-relative references (disp32 directly followed by end of instruction or imm8/imm32)
    public static string RipRefs(int[] targets)
    {
        var set = new HashSet<int>(targets);
        var sb = new StringBuilder();
        for (int i = TextFile; i < TextFile + TextSize - 4; i++)
        {
            int disp = BitConverter.ToInt32(img, i);
            int rva = i + 0xA00;
            for (int extra = 0; extra <= 4; extra += 1)
            {
                if (extra != 0 && extra != 1 && extra != 4) continue;
                int t = rva + 4 + extra + disp;
                if (set.Contains(t))
                {
                    byte modrm = img[i - 1];
                    if ((modrm & 0xC7) != 0x05) continue;
                    sb.AppendFormat("{0:X8} -> {1:X8} extra={2} bytes={3:X2} {4:X2} {5:X2}\n", rva - 3, t, extra, img[i - 3], img[i - 2], img[i - 1]);
                }
            }
        }
        return sb.ToString();
    }

    // Find 8-byte absolute pointers (image base 0x140000000) to given RVAs anywhere in file
    public static string AbsRefs(int[] targets)
    {
        var set = new HashSet<long>();
        foreach (var t in targets) set.Add(0x140000000L + t);
        var sb = new StringBuilder();
        for (int i = 0; i < img.Length - 8; i += 8)
        {
            long v = BitConverter.ToInt64(img, i);
            if (set.Contains(v)) sb.AppendFormat("file {0:X8} rva {1:X8} -> {2:X}\n", i, FileToRva(i), v - 0x140000000L);
        }
        return sb.ToString();
    }

    // E8 call rel32 to target
    public static string Calls(int[] targets)
    {
        var set = new HashSet<int>(targets);
        var sb = new StringBuilder();
        for (int i = TextFile; i < TextFile + TextSize - 5; i++)
        {
            if (img[i] != 0xE8 && img[i] != 0xE9) continue;
            int rva = i + 0xA00;
            int t = rva + 5 + BitConverter.ToInt32(img, i + 1);
            if (set.Contains(t)) sb.AppendFormat("{0:X8} {1} {2:X8}\n", rva, img[i] == 0xE8 ? "call" : "jmp", t);
        }
        return sb.ToString();
    }

    static int[] pBegin, pEnd;
    static void LoadPdata()
    {
        if (pBegin != null) return;
        int f = 0x34A8E00, n = 0x1F2E28 / 12;
        pBegin = new int[n]; pEnd = new int[n];
        for (int k = 0; k < n; k++) { pBegin[k] = BitConverter.ToInt32(img, f + k * 12); pEnd[k] = BitConverter.ToInt32(img, f + k * 12 + 4); }
    }
    public static int FuncStart(int rva)
    {
        LoadPdata();
        int lo = 0, hi = pBegin.Length - 1;
        while (lo <= hi) { int m = (lo + hi) / 2; if (pBegin[m] <= rva) { if (rva < pEnd[m]) return pBegin[m]; lo = m + 1; } else hi = m - 1; }
        return -1;
    }

    // Functions referencing every displacement group (mod=10 disp32 memory operands)
    public static string DispFunctions(int[][] groups, int minGroups)
    {
        var hits = new Dictionary<int, HashSet<int>>();
        for (int i = TextFile + 1; i < TextFile + TextSize - 4; i++)
        {
            byte modrm = img[i - 1];
            int disp = BitConverter.ToInt32(img, i);
            bool direct = (modrm & 0xC0) == 0x80 && (modrm & 7) != 4 && (modrm & 7) != 5;
            if (!direct) continue;
            for (int g = 0; g < groups.Length; g++)
                foreach (int d in groups[g])
                    if (disp == d)
                    {
                        int fs = FuncStart(i + 0xA00);
                        if (fs < 0) continue;
                        HashSet<int> s; if (!hits.TryGetValue(fs, out s)) hits[fs] = s = new HashSet<int>();
                        s.Add(g);
                    }
        }
        var sb = new StringBuilder();
        foreach (var kv in hits) if (kv.Value.Count >= minGroups) sb.AppendFormat("{0:X8} groups={1}\n", kv.Key, kv.Value.Count);
        return sb.ToString();
    }

    public static string DispSites(int funcStart, int[] disps)
    {
        LoadPdata();
        int idx = Array.IndexOf(pBegin, funcStart);
        int end = idx >= 0 ? pEnd[idx] : funcStart + 0x1000;
        var sb = new StringBuilder();
        for (int r = funcStart; r < end - 4; r++)
        {
            int disp = BitConverter.ToInt32(img, RvaToFile(r));
            foreach (int d in disps) if (disp == d) sb.AppendFormat("{0:X8} disp {1:X}\n", r, d);
        }
        return sb.ToString();
    }

    public static string Hex(int rva, int count)
    {
        int f = RvaToFile(rva);
        var sb = new StringBuilder();
        for (int i = 0; i < count; i++) sb.AppendFormat("{0:x2}", img[f + i]);
        return sb.ToString();
    }

    // Indirect calls "call [reg+disp32]" with disp == slot; report those with marker disp32 in the preceding window
    public static string VCalls(int slot, int marker, int window)
    {
        var sb = new StringBuilder();
        for (int i = TextFile; i < TextFile + TextSize - 6; i++)
        {
            if (img[i] != 0xFF) continue;
            byte m = img[i + 1];
            if ((m & 0xF8) != 0x90 || (m & 7) == 4 || (m & 7) == 5) continue;
            if (BitConverter.ToInt32(img, i + 2) != slot) continue;
            bool found = marker == 0;
            for (int k = i - window; k < i && !found; k++) if (BitConverter.ToInt32(img, k) == marker) found = true;
            if (found) sb.AppendFormat("{0:X8} in {1}\n", i + 0xA00, FuncOf(i + 0xA00));
        }
        return sb.ToString();
    }

    public static long Q(int rva) { return BitConverter.ToInt64(img, RvaToFile(rva)); }
    public static int D(int rva) { return BitConverter.ToInt32(img, RvaToFile(rva)); }

    // Walk backwards from a vtable slot to the vtable start (previous qword is a COL pointer into .rdata)
    public static string VtableInfo(int slotRva)
    {
        var sb = new StringBuilder();
        int r = slotRva;
        while (true)
        {
            long prev = Q(r - 8);
            long rel = prev - 0x140000000L;
            if (rel >= 0x1000 && rel < 0x1DB4000) { r -= 8; continue; }
            break;
        }
        long colPtr = Q(r - 8) - 0x140000000L;
        sb.AppendFormat("vtable {0:X8} slot index {1} (offset 0x{2:X})\n", r, (slotRva - r) / 8, slotRva - r);
        try
        {
            int td = D((int)colPtr + 12);
            sb.AppendFormat("COL {0:X8} TD {1:X8} name {2}\n", colPtr, td, Str(td + 16));
        }
        catch { sb.Append("no RTTI\n"); }
        return sb.ToString();
    }

    public static float F32(int rva) { return BitConverter.ToSingle(img, RvaToFile(rva)); }
    public static string Str(int rva) { int f = RvaToFile(rva); int e = f; while (img[e] != 0) e++; return Encoding.ASCII.GetString(img, f, e - f); }

    // Find function start using .pdata (RUNTIME_FUNCTION entries at rva 0x373A000, file 0x34A8E00, size 0x1F2E28)
    public static string FuncOf(int rva)
    {
        int f = 0x34A8E00;
        int n = 0x1F2E28 / 12;
        for (int k = 0; k < n; k++)
        {
            int b = BitConverter.ToInt32(img, f + k * 12);
            int e = BitConverter.ToInt32(img, f + k * 12 + 4);
            if (rva >= b && rva < e) return string.Format("{0:X8}-{1:X8}", b, e);
        }
        return "?";
    }
}
