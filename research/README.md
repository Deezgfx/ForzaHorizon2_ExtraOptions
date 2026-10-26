# Reproducing the air ride research

`Xref.cs` is the small static-analysis helper used to find the native ride-height path. It
reads the game executable as plain bytes and never runs it. Load it in Windows PowerShell 5.1
next to MSVC `dumpbin`:

```powershell
Add-Type -Path research\Xref.cs
[Xref]::Load("F:\...\forza_durango_release_final.exe")
[Xref]::RipRefs(@(0x2016090, 0x2015DF8))   # code referencing the SpringDamper SQL / DefRideHeight
[Xref]::FuncOf(0x1061A2B)                  # function bounds from .pdata
[Xref]::Calls(@(0x12E3680))                # direct callers of the tuning resolver
[Xref]::AbsRefs(@(0x12DA500))              # vtables holding the live re-apply thunk
[Xref]::VtableInfo(0x2EB5E18 + 0x238)      # vtable start, slot and RTTI class name
[Xref]::VCalls(0x238, 0x3848, 0x100)       # slot-0x238 calls near ride-height writes
dumpbin /disasm:nobytes /range:0x1412E3A90,0x1412E3CDB F:\...\forza_durango_release_final.exe
```

The file/RVA section mapping in `Xref.cs` is hard-coded for the supported executable
(timestamp `0x565501ED`). `AIR_RIDE.md` lists every address with the evidence for it.
