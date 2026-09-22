# analysis/

Reserved by `Trinity_Binary_Ninja_Menu_Analysis_Plan.md` for the annotated
Binary Ninja working database (for example `CrimsonDesert-PE2944.bndb`).

**The database is not committed here.** The PE 2944 database is **3.35 GiB** and
lives next to the executable it describes:

```text
E:\Steam\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe.bndb
```

together with the executable it was built from:

```text
E:\Steam\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe
SHA-256 6D348BE9D52F81BD35CF7C55E73A5DBFC96CC8268438387C91F7F62C82381FA7
```

Notes for a future update:

- Keep the versioned name when a new executable arrives
  (`CrimsonDesert-PE<revision>.bndb`), so an old and a new database can be
  diffed side by side.
- The executable sits in a Steam-managed directory. Steam "verify integrity"
  or a game update can replace it — record the hash before trusting a database
  that was built against it.
- The portable, loss-proof record is `docs/binary-ninja/`: the Markdown
  dossiers must remain sufficient even if this database is lost or a different
  disassembler is used later.
