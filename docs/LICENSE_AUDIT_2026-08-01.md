# License Audit — ZonistationOne (2026-08-01)

Stato: audit file-per-file del codice del progetto ai fini della compatibilità con la GPLv3
indicata in `LICENSE`. Fonte della verità: intestazioni dei file, commenti di attribuzione,
storia dei submodule. Nessun parere legale — questo documento classifica i fatti e segnala i rischi.

---

## 1. Riepilogo esecutivo

- Il progetto è GPLv3 (`LICENSE`).
- **Problema centrale**: DuckStation è stato **GPL-3.0 fino al 2024-09-01**, poi **ri-licenziato a
  CC BY-NC-ND 4.0** (commit `7f4e5d55d`). Tutto il codice del progetto derivato da DuckStation è
  stato scritto **nel 2026**, cioè dopo il ri-licenziamento, copiando/o riferendo il checkout
  `duckstation_ref` (pinned `e39033c4`, 2026-07-13, era CC).
  → CC BY-NC-ND **vieta le opere derivate** ed è **incompatibile con la GPLv3**.
- **Solo `src/core/sio.c` è dichiaratamente derivato da codice DuckStation** ("based on
  DuckStation's pad.cpp") → è il file a rischio più alto, da riscrivere o rimuovere.
- Tutti gli altri riferimenti DuckStation sono comportamentali/architetturali (costanti da
  hardware, modelli di timing, "DuckStation-style") → grigio, non copia di espressione.
- I port SPU da **pcsx-redux / Pete Bernert sono GPL-2.0+** → **compatibili con la GPLv3** (safe).
- MDEC è stato riscritto da spec (`DOCS/macroblockdecodermdec.md`), nessun riferimento DuckStation
  nel codice corrente → **safe**. (Risoluzione della contraddizione: `CHANGELOG.md:106` e
  `GAP_ANALYSIS_REFACTOR_2026-07-13.md:438` dicono ancora "portato da DuckStation"; il commit
  `7ff07bf` del 2026-08-01 "implement decode stages from the hardware documentation" ha sostituito
  il port — le due righe sono **stale e vanno corrette**.)
- **Riferimenti ulteriori trovati dall'audit approfondito**:
  - La base comportamentale iniziale (`ram.c`, `bios.c`, `cpu_icache.c`, `renderer.c` + header) è
    la **guida non licenziata** di Lionel Flandrin (`simias/psx-guide`, `guide.tex` 11 250 righe,
    **nessun file LICENSE** → tutti i diritti riservati). Verificato: è un tutorial con liste di
    codice Rust banali (`store32` = `panic!`); il progetto implementa il comportamento descritto,
    non copia le liste → basso rischio, da documentare.
  - **`DOCS/` (psx-spx) NON è clean-room**: il README del repo psx-spx dichiara che parte della
    documentazione è "directly copy/pasted from the confidential code and documentation from Sony".
    Tutto il codice "scritto da DOCS/" eredita questa tara. Non è un problema GPL, ma è un rischio
    di esposizione a materiale confidenziale Sony da annotare.
  - **`maxresdefault.jpg`** (root del repo, 1024×576, committato in `a7deb7e`, mai referenziato) =
    thumbnail in formato YouTube, immagine di terzi → **da rimuovere**.
  - **17 PNG/JPG tracciati in git** = screenshot dell'emulatore con contenuti di giochi/BIOS Sony →
    ridistribuzione di immagini di terzi nel repo, rischio minore.
  - **Riferimenti DuckStation "gialli" eliminati (2026-08-01)**: `grep -rn "DuckStation|duckstation|stenzek" src/ include/` = 0.
    Ogni riferimento comportamentale è stato sostituito con citazioni spec (`DOCS/cdromdrive.md:1896-1908`,
    `DOCS/graphicsprocessingunitgpu.md:1305-1306,1325-1335`, `DOCS/timers.md`, `DOCS/soundprocessingunitspu.md:534`)
    o con la descrizione del fatto hardware; restano solo i riferimenti PCSX-Redux (GPL-2.0+, compatibili) nei
    port SPU e nei commenti bus.c. Sezione 3.2 chiusa.
- Lacuna non risolutiva: `third_party/imgui` e `third_party/lua` sono vendored **senza file
  LICENSE** (MIT entrambi, ma l'avviso non è materialmente presente).
- Inconsistenza interna: `THIRD-PARTY.md` dichiara "GPL-3.0-**or-later**"; il `LICENSE` è testo
  GPLv3 senza clausola "or later" (0 occorrenze). Allineare.

### Cronologia dei fatti accertata

| Fonte | Licenza | Data |
|-------|---------|------|
| DuckStation `2149ab4d6` (initial commit) | GPL-3.0 | 2019-09-11 |
| DuckStation `7f4e5d55d` (cambio headers) | **CC BY-NC-ND 4.0** | 2024-09-01 |
| `duckstation_ref` pinned `e39033c4` | CC BY-NC-ND 4.0 | 2026-07-13 |
| pcsx-redux `LICENSE` (repo) | GPL-2.0 | — |
| pcsx-redux file SPU (eredità Peops, es. `src/spu/adsr.cc`) | GPL-2.0-or-later (header di file) | — |
| Guida Flandrin `simias/psx-guide` (`guide.tex`) | **nessuna licenza** (all rights reserved) | (storica, ~2015) |
| psx-spx / nocash `DOCS/` | nessuna licenza acquisita; **non clean-room** (contiene materiale confidenziale Sony) | — |
| Inizio del progetto (commit `a9de08d`) | GPLv3 | 2025-04-20 |
| Scrittura file derivati DuckStation (sio/spu/decode) | — | 2026-01 → 2026-07 |
| Riscrittura MDEC da spec (commit `7ff07bf`) | — | 2026-08-01 |

Implicazione: qualunque derivazione da DuckStation fatta nel 2026 ha avuto come sorgente materiale
in CC BY-NC-ND. La licenza GPL-3.0 storica di DuckStation **non sana** il prelievo avvenuto dopo il
2024-09-01.

---

## 2. Classificazione dei rischi

- **RED** — derivazione diretta da DuckStation (CC BY-NC-ND): non distribuire, riscrivere.
- **YELLOW** — riferimento comportamentale DuckStation (costanti hardware, modelli di timing,
  architettura): non copia di espressione ma va verificato; pulizia totale richiede base spec
  (PSX-SPX).
- **GREEN** — pcsx-redux / Peops GPL-2.0+ (compatibile con GPLv3), rxi MIT. Safe.
- **CLEAN** — originale / da spec (PSX-SPX, nocash), nessun riferimento a emulatori esterni. Safe.

---

## 3. Tabella file-per-file

### 3.1 RED — derivazione diretta DuckStation (rischio massimo)

| File | Riferimento | Azione |
|------|-------------|--------|
| `src/core/sio.c` | "Implementation based on DuckStation"; "based on DuckStation's pad.cpp"; 20+ commenti di comportamento | **Riscrivere** da PSX-SPX (controllersandmemorycards.md) o rimuovere. È l'unico file che dichiara port diretto |

### 3.2 YELLOW — riferimenti comportamentali DuckStation — **CHIUSA 2026-08-01**

Tutti i riferimenti elencati sotto sono stati rimossi da `src/` e `include/` (verifica:
`grep -rn "DuckStation\|duckstation\|stenzek" src/ include/` = 0) e sostituiti con citazioni
spec (vedi sezione 6) o con la descrizione neutra del fatto hardware. La tabella registra lo
stato pre-risoluzione.

| File | Riferimento | Esito |
|------|-------------|-------|
| `src/core/bus.c` | Tabella dispatch "DuckStation-style", modelli `CalculateMemoryTiming`, `GetDMARAMTickCount`, `MDEC_SLICE_WORDS`/`DEFAULT_DMA_HALT_TICKS` | Rimossi; resta solo il cite PCSX-Redux per il DMA sub-word (GPL-2.0+) |
| `src/core/dma.c` | Modello `DMA::UpdateIRQ` (master flag, DICR) | Rimossi; riscritto come fatto HW |
| `src/core/bios.c` | Fast boot "DuckStation PatchBIOSFastBoot Type1" | Rimossi |
| `src/core/pcdrv.c` | Estratto di istruzione "come in DuckStation" | Rimossi; solo decodifica MIPS break |
| `src/core/timers.c` | `UpdateCountingEnabled`, sync su scrittura registri | Rimossi; rimandati a `DOCS/timers.md` |
| `src/cpu/cpu_execution.c` | Ciclo downcount "DuckStation-style" | Rimossi; "event-scheduler downcount" |
| `src/cpu/cpu_instructions.c` | Costanti hardware (WRITE_MASK `0xF27FFFFF`, latenze mul/div 7/37) + stall model | Rimossi; 7/37 citati da `DOCS/cpu.md` (Guide Table 7), write mask come fatto HW |
| `src/cpu/cpu_icache.c` | Modello timing memoria (MIN_SEEK_TICKS etc.) | Rimossi; descrizione MEMCTRL |
| `src/cpu/cpu_bios.c` | "DuckStation-style LLE" | Rimossi; "LLE" |
| `src/gpu/gpu.c` | Costanti video hardware (3413/3406 ticks per linea, dot clock dividers) | Rimossi; citati `DOCS/graphicsprocessingunitgpu.md:1305-1306,1325-1335` |
| `src/gpu/gpu_commands.c` | "DuckStation-inspired architecture" (dispatch); `HandleInterruptRequestCommand` | Rimossi |
| `src/gpu/renderer.c` | Pattern GPU_HW unified VRAM; `SampleVRAM24` | Rimossi; matematica 24bpp come fatto HW |
| `include/bus.h`, `include/cdrom.h`, `include/timers.h`, `include/mdec.h`, `include/renderer.h`, `include/cpu.h`, `include/interconnect.h`, `include/gpu_helpers.h` | Stessi riferimenti a livello di header | Rimossi tutti (8 file) |
| `src/gte/gte_internal.h` | Commento RGB FIFO "Matches DuckStation..." | Rimossi |
| `src/spu/spu_mixing.c` | "matches DuckStation's and PCSX-Redux's tables" | Rimossi; resta solo PCSX-Redux (GPL-2.0+) |
| `src/spu/spu_voice.c`, `src/spu/spu_adsr.c` | Saturate/precision "DuckStation" | Rimossi; resta il port PCSX-Redux dichiarato in header (GPL-2.0+) |

Nota: la maggior parte di questi riferimenti era **costanti di fatto hardware o modelli comportamentali**
(non protetti da copyright come espressione). Il rischio era concreto solo dove si copia struttura o
sequenza del codice sorgente DuckStation — assente, come confermato dalla verifica riga-per-riga.
Classificazione prudenziale: YELLOW, non RED; ora chiusa.

### 3.3 GREEN — pcsx-redux / Peops GPL-2.0+ (compatibile con GPLv3)

| File | Riferimento | Verdetto |
|------|-------------|----------|
| `src/spu/spu_voice.c` | "ported 1:1 from pcsx-redux spu.cc (GPL-2.0+)" | Safe (GPL-2.0-or-later → GPLv3) |
| `src/spu/spu_adsr.c` | "ported 1:1 from pcsx-redux adsr.cc (GPL-2.0+)" | Safe (idem; originale Pete Bernert 2002, header "or any later version") |
| `src/spu/spu.c` | "pcsx-redux 1:1 init" | Safe (idem) |
| `src/spu/spu_mixing.c` | Tabelle che "match entry-for-entry DuckStation and PCSX-Redux" | Safe (eredità Peops; tabelle di mix) |
| `src/cpu/cpu_decode.c` | "Mirrors pcsx-redux s_psxSPC / s_psxBSC" | Safe (tabelle di dispatch, GPL-2.0+) |
| `src/core/timers.c` | "Structure inspired by PCSX ReARMed's timer handling (GPL-2.0-or-later, Copyright (c) PCSX ReARMed authors)" | Safe (attribuzione in-file presente; solo forma dello scheduling, non aritmetica) |
| `src/core/savestate.c` | Riferimento comportamentale "same reasoning as pcsx-redux" | Safe (nota, non copia) |

Attenzione: il `LICENSE` di pcsx-redux è GPL-2.0 **only**; il port del progetto dichiara "GPL-2.0+"
basandosi sugli header dei singoli file (eredità Peops con clausola "or any later version"). Per
prudenza, mantenere l'attribuzione esplicita nei file SPU (già presente).

### 3.4 CLEAN — originale o da spec (nessun riferimento a emulatori esterni)

| File | Note |
|------|------|
| `src/main.c`, `src/core/interconnect.c`, `src/core/system.c`, `src/core/frame_events.c`, `src/core/event_scheduler.c` (header: "naming and structure are original") | Originali |
| `src/core/ram.c`, `src/core/bus_irq.c`, `src/core/controller.c`, `src/core/debugger.c`, `src/core/lua_debug.c`, `src/gpu/vram.c`, `src/spu/spu_dma.c`, `src/spu/spu_irq.c` | Originali (nota: `ram.c` cita "Guide Section 2.34/2.49/2.80/2.82" — guida Flandrin non licenziata; implementazione = comportamento, non copia delle liste Rust) |
| `src/core/mdec.c` | Riscritto da `DOCS/macroblockdecodermdec.md` (spec) — FIFO/state machine originale; commit `7ff07bf` (2026-08-01). Non confondere con CHANGELOG/GAP_ANALYSIS stale |
| `src/gpu/gpu_helpers.c` | LUT dithering da PSX-SPX; nessun codice DuckStation (ref in header solo come nota) |
| `src/gte/gte.c`, `src/gte/gte_ops.c`, `src/gte/gte_internal.h` | Da spec PSX-SPX (28 riferimenti spec in tutto il tree) |
| `src/cpu/cpu_exceptions.c`, `src/cpu/cpu_init.c`, `src/cpu/cpu_registers.c` | Originali/da spec |
| `src/core/bios.c`, `src/cpu/cpu_icache.c`, `src/gpu/renderer.c` (parziale) | Citate "Guide Section 2.7 / 8.1-8.2" (Flandrin, non licenziata) + riferimenti DuckStation comportamentali; implementazioni in C indipendenti → comportamento, verificare le singole sezioni citate |
| `src/cdrom/cdrom_audio.c` | Da spec |
| `src/utils/rxi_log.c`, `include/rxi_log.h` | rxi/log.c — **MIT**, attribuzione e testo completo presenti. Safe |
| `src/debug_ui.cpp` | Originale (ImGui MIT); 2 commenti "PCSX-Redux style" solo come riferimento UI, incidenziale |

---

## 4. Terze parti vendored

| Path | Progetto | Licenza | Stato |
|------|----------|---------|-------|
| `third_party/imgui/` | Dear ImGui (ocornut) | MIT | **Avviso LICENSE mancante** — aggiungere `LICENSE.txt` |
| `third_party/lua/` | Lua (PUC-Rio) | MIT | Avviso presente inline in `lua.h`; nessun file LICENSE dedicato — aggiungere per igiene |
| `DOCS/` | psx-spx (Martin "nocash" Korth) | nessuna licenza acquisita | Il repo psx-spx stesso dichiara "fair use / derivative work doctrine" **e di non essere clean-room** (copie da materiale confidenziale Sony); ridistribuzione di documentazione non licenziata — grigio |
| `duckstation_ref/` | DuckStation | CC BY-NC-ND 4.0 | **Riferimento consultivo soltanto** — non distribuire; non copiarne codice nel progetto |
| `pcsx-redux/` | PCSX-Redux | GPL-2.0 (repo) / GPL-2.0+ (file SPU) | Riferimento consultivo; port GPL-2.0+ compatibili |
| `maxresdefault.jpg` (root) | immagine YouTube di terzi | nessuna | **Rimuovere dal repo** |
| `*.png`/`*.jpg` (17 file tracciati) | screenshot emulatore (giochi/BIOS Sony) | n/a | Ridistribuzione di contenuti di terzi; valutare spostamento fuori git (es. `docs/screenshots` + `.gitignore`) |

Nota: nessun `.o` è tracciato in git (`git ls-files '*.o'` = 0); gli artefatti visti su disco sono
non-tracciati, nessun problema di distribuzione. Idem per i `.wav` in `logs/` (non tracciati).

---

## 5. Azioni raccomandate (in ordine)

1. **Riscrivere `src/core/sio.c`** da spec PSX-SPX (protocollo controller/memcard), eliminandene
   ogni riferimento DuckStation. È l'unico blocco RED.
2. **Verificare (non solo declassare)** i file YELLOW: confronto riga-per-riga contro
   `duckstation_ref` per escludere copia di espressione. Se compare struttura identica → riscrivere
   da spec.
3. **Rimuovere `maxresdefault.jpg`** dalla root (immagine di terzi non referenziata).
4. **Correggere le righe stale**: `CHANGELOG.md:106` e `GAP_ANALYSIS_REFACTOR_2026-07-13.md:438`
   ("MDEC portato da DuckStation") — il codice attuale è da spec (commit `7ff07bf`).
5. **Aggiungere `LICENSE.txt`** (MIT) per imgui e lua in `third_party/`.
6. **Non importare altro codice da `duckstation_ref/`**: materiale in CC BY-NC-ND. Tenere il
   submodule solo come riferimento comportamentale, mai come sorgente di implementazione.
7. Documentare in `CHANGELOG.md`/`CLAUDE.md` il vincolo: nessun codice da DuckStation; base spec =
   PSX-SPX (con nota non-clean-room) + pcsx-redux (GPL-2.0+) + rxi (MIT) + guida Flandrin
   (non licenziata, solo comportamento).
8. **Allineare la licenza dichiarata**: `THIRD-PARTY.md` dice "GPL-3.0-or-later" ma `LICENSE` è GPLv3
   senza clausola "or later" — decidere e uniformare (es. aggiungere SPDX `GPL-3.0-or-later` o
   correggere la dicitura).

---

## 6. Fonti

- `LICENSE` (progetto): GPLv3.
- `duckstation_ref/LICENSE`: CC BY-NC-ND 4.0; storia `git log -- LICENSE` → `7f4e5d55d` (2024-09-01)
  è il commit del ri-licenziamento (prima: GPL-3.0).
- `pcsx-redux/LICENSE`: GPL-2.0; `pcsx-redux/src/spu/adsr.cc` header: GPL-2.0-or-later (Peops 2002).
- `third_party/lua/lua.h`: avviso MIT inline.
- `DOCS/README.md`: dichiarazione fair use psx-spx (nocash) **e non-clean-room** (materiale Sony).
- `simias/psx-guide` (clone verificato in `/tmp`): nessun file LICENSE; `guide.tex` 11 250 righe.
- `pcsx-redux/src/spu/spu.cc` header: GPL-2.0-or-later (Peops 2002) — verificato.
- `git log` progetto: `7ff07bf` riscrittura MDEC da spec (2026-08-01); `a7deb7e` aggiunge
  `maxresdefault.jpg`.
- Header dei file del progetto elencati in tabella (verificati 2026-08-01).
