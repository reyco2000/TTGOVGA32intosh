# Plan: SCSI (NCR 5380) support for TTGOVGA32intosh

Status: **plan only, nothing implemented yet.**

Goal: let the emulated Mac Plus see SCSI hard disks stored as image files on
the SD card (`scsi0.img` … `scsi6.img`), mount them next to the existing floppy
`disk.img`, and boot from them.

---

## 1. What Mini vMac 36.04 has (and why we can't just port it)

I downloaded and read `minivmac-36.04.src.tgz`. The SCSI pieces are:

| File | What it does |
| --- | --- |
| `src/SCSIEMDV.c` / `.h` (~170 lines) | NCR 5380 register file for the Mac Plus |
| `src/GLOBGLUE.c` | Maps `0x580000–0x5FFFFF` (`kSCSI_Block_Base`, `kSCSI_ln2Spc = 19`) to `SCSI_Access()`. Register = `(addr >> 4) & 7`. Reads must be at even addresses, writes at odd; word accesses are flagged as abnormal. |
| `src/PROGMAIN.c` | Calls `SCSI_Reset()` at power-on |

`SCSIEMDV.c` is a **stub**. Its own comment says *"stub.. doesn't really work..."*.
It does just enough that the ROM sees an empty bus:

- it keeps 8 read registers and 8 write registers;
- `SCSI_Check()` fakes "arbitration won", then never answers selection, so the
  ROM times out on every ID;
- `SCSI_BusReset()` sets bit 15 of low-memory word `$0B22`. The source calls
  this *"the missing piece of the puzzle"*. It is a hack to make the ROM skip
  SCSI.

**Mini vMac does not emulate any SCSI device.** All its disks go through its
replacement `.Sony` driver (`SONYEMDV.c`). umac does the same thing (`disc.c` +
`sonydrv.h` + the `PV_SONY_ADDR` hook). So from Mini vMac we reuse only:

- the address decoding (base `0x580000`, A4–A6 select the register, reads even /
  writes odd);
- the register names and offsets (`sCDR`, `sICR`, `sMR`, `sTCR`, `sCSR`, `sBSR`,
  `sIDR`, `sRESET`, …);
- where in the boot sequence the reset hook goes.

We need to write the working 5380 and the disk-side code ourselves.

**Licence.** Mini vMac is GPLv2. The repo already ships GPLv2 code (`disc.c`
from Basilisk II), but the new files should be written from scratch under MIT,
like the rest of umac, using the NCR 5380 datasheet as the spec. Mini vMac, PCE
and MAME are only for checking behaviour. Don't copy code from them.

Better behaviour references than Mini vMac:

- **PCE/macplus** (`src/arch/macplus/scsi.c`, GPL): a complete, working Mac Plus
  5380 + disk emulation. Best source for checking what the Plus ROM expects.
- **MAME** `ncr5380` + `nscsi_hd` (BSD-3): register and phase behaviour.
- **BlueSCSI / ZuluSCSI**: which INQUIRY and MODE SENSE answers Apple's
  HD SC Setup and the Apple driver accept.
- *NCR 5380 datasheet*, *Guide to the Macintosh Family Hardware* (2nd ed., SCSI
  chapter), *Inside Macintosh vol. IV / V: SCSI Manager*.

---

## 2. What the emulator does with this address range today

From `src/emu/machw.h` and `src/emu/umac_main.c`:

- `IS_DUMMY(x)` matches all of `0x500000–0x5FFFFF`, so **SCSI byte reads return
  0 and byte writes are dropped**. The ROM sees a bus with nothing on it and
  never gets BSY back, so every selection times out. Boot still works because
  the Sony floppy path comes first.
- `cpu_read_word` / `cpu_read_long` to `0x58xxxx` fall through to
  `exit_error()`, which crashes the emulator. The Plus ROM only does byte
  accesses there, so this doesn't happen today. The new code must still handle
  word accesses safely.
- `cpu_pulse_reset()` (68k `RESET` instruction) is an empty hook. It is the
  right place to reset the SCSI bus.
- The Mac Plus has **no SCSI IRQ and no DRQ wiring**. The ROM SCSI Manager only
  polls. So there are no VIA or interrupt changes, which keeps the job smaller.
- Disk I/O goes through `disc_descr_t { op_ctx, op_read, op_write }` with
  `disc_sd_read`/`disc_sd_write` (FATFS `fseek`/`fread`). The SCSI targets can
  use the same callbacks.
- `disc_sd_open()` mounts the SD card **and** opens one file. It needs to be
  split so several images can be opened. The mount also uses `max_files = 4`,
  which is too low for 7 targets.

---

## 3. Architecture

```
68k core (Musashi)
   │ byte access 0x580000–0x5FFFFF
   ▼
umac_main.c  cpu_read_byte / cpu_write_byte   (+ safe word/long fallbacks)
   │ IS_SCSI(addr)
   ▼
ncr5380.c   ── register file, bus signals, arbitration/selection,
   │           programmed-I/O handshake (ACK/REQ), pseudo-DMA via DACK (A9)
   │ bus-level calls: select(id), req/ack byte transfers, phase query, reset
   ▼
scsi_disk.c ── per-target state machine: COMMAND → DATA → STATUS → MSG IN,
   │           CDB decode, sense data, sector buffer / read-ahead
   │ disc_op_read / disc_op_write (offset, len)
   ▼
disc_sd.c   ── SD/FATFS file I/O (already exists; refactor for N images)
```

There are two layers on purpose:

- `ncr5380.c` knows registers and signals but not commands.
- `scsi_disk.c` knows commands but not registers. Later it can gain a CD-ROM
  target, or a host-side unit-test harness can drive it directly.

### New / changed files

| File | Change |
| --- | --- |
| `src/emu/ncr5380.c`, `src/emu/ncr5380.h` | **New.** 5380 chip model |
| `src/emu/scsi_disk.c`, `src/emu/scsi_disk.h` | **New.** SCSI direct-access target (hard disk) |
| `src/emu/machw.h` | Add `SCSI_BASE`, `IS_SCSI(x)`. Take the `0x5xxxxx` term out of `IS_DUMMY` (or test `IS_SCSI` first) |
| `src/emu/umac_main.c` | Dispatch SCSI byte reads/writes. Map word/long accesses to the high byte instead of crashing. Call `ncr5380_reset()` from `cpu_pulse_reset()`, `umac_init()` and `umac_reset()` |
| `src/emu/umac.h` | `umac_scsi_attach(int id, disc_descr_t *d)`, or a separate init that takes an array of `SCSI_MAX_TARGETS` descriptors. Keep the `umac_init()` signature as it is |
| `src/disc_sd.c`, `src/disc_sd.h` | Split into `disc_sd_mount()` and `disc_sd_open_file(disc, path, ro)`. Raise `max_files` to 10. Add a `disc_sd_flush()` hook. Do **not** auto-create missing SCSI images |
| `src/main.cpp` | After mounting: probe `/sdcard/scsi0.img … scsi6.img`, open each one found, attach it to that ID, and log ID/size/RO |
| `src/user_config.h` | `ENABLE_SCSI` (default 1), `SCSI_MAX_TARGETS` (7), `SCSI_READAHEAD_SECTORS` (8), `SCSI_DEBUG` (0), `SCSI_VENDOR`/`SCSI_PRODUCT` strings |
| `README.md`, `src/emu/README.md` | User docs, local-changes list, licences |
| `tools/scsi_host_test.c` (optional) | Host-side harness (see Phase 5) |

---

## 4. NCR 5380 model (`ncr5380.c`)

### 4.1 Address decoding (Mac Plus)

| Address | Meaning |
| --- | --- |
| `0x580000 + (reg << 4)` | read register `reg` (even address) |
| `0x580001 + (reg << 4)` | write register `reg` (odd address) |
| `addr & 0x200` (A9) set | DACK, the pseudo-DMA data port. A read returns the next DMA input byte; a write sends the next DMA output byte. The register bits are ignored |

`IS_SCSI(x) = ((ADR24(x) & 0xF80000) == 0x580000)`. The whole 512 KB mirror is
decoded, as Mini vMac does (`kSCSI_ln2Spc = 19`).

Reads at odd addresses and writes at even addresses return/ignore, with a
debug log under `SCSI_DEBUG`. Pick the read/write direction from the CPU
access type, not from A0, so a ROM quirk can't break it.

### 4.2 Registers to model

| # | Read | Write |
| --- | --- | --- |
| 0 | CDR: current SCSI data bus | ODR: output data |
| 1 | ICR: initiator command (with AIP/LA status bits) | ICR: `RST(7) AIP/–(6) LA/–(5) ACK(4) BSY(3) SEL(2) ATN(1) DBUS(0)` |
| 2 | MR: mode | MR: `BLK(7) TARG(6) EPC(5) EPI(4) EEOP(3) MBSY(2) DMA(1) ARB(0)` |
| 3 | TCR: target command | TCR: `REQ(3) MSG(2) C/D(1) I/O(0)` (expected phase) |
| 4 | CSR: `RST BSY REQ MSG C/D I/O SEL DBP` | SER: select enable (store it, nothing else) |
| 5 | BSR: `EODMA DRQ PERR IRQ PHASEMATCH BUSYERR ATN ACK` | start DMA send |
| 6 | IDR: input data latch | start DMA target receive (store it, nothing else) |
| 7 | reset parity/IRQ (clears the IRQ/PERR/BUSYERR bits) | start DMA initiator receive |

State: `odr, icr, mr, tcr, ser`, plus the bus signals driven by the initiator
(the Mac) and by the target (our disk). The values the CPU reads are **worked
out when it reads them** from that state: CDR = the bus data (initiator data if
DBUS is set, otherwise target data). CSR and BSR come from the OR of both
sides' signals. Phase match = `(TCR & 7) == (target MSG,C/D,I/O)`.

### 4.3 Bus behaviour to implement

1. **Bus reset.** ICR.RST set → CSR.RST reads 1 while it stays asserted;
   `scsi_disk_bus_reset()` on every target; target signals cleared; arbitration
   cancelled. Set BSR.IRQ (the Plus ignores it, but keep the chip accurate).
2. **Arbitration.** MR.ARB set with the bus free → set ICR.AIP right away, put
   ODR (the Mac's ID bit 7) on the bus, LA = 0. Arbitration is never lost:
   there is one initiator.
3. **Selection.** On an ICR write with SEL=1, BSY(initiator)=0 and DBUS=1: take
   the target bit from `ODR & ~0x80`. If exactly one bit is set and that ID has
   an image attached → the target asserts BSY. If not → no response, and the
   ROM times out as it does today. ATN at selection time → the target starts in
   MESSAGE OUT, otherwise in COMMAND.
4. **Programmed-I/O handshake** (used for command, status and message bytes,
   and for some data):
   - target asserts REQ with phase bits (and data on the bus for IN phases);
   - Mac reads CDR/IDR (IN) or writes ODR + DBUS (OUT), then sets ICR.ACK;
   - **when ACK goes 0→1:** for OUT phases, give the bus byte to the target; for
     IN phases, tell the target the byte was taken. The target drops REQ;
   - **when ACK goes 1→0:** the target moves on, which sets REQ for the next
     byte or changes phase.
   Work only on the ACK edges, so the timing doesn't matter. The ROM busy-waits,
   and we answer straight away.
5. **Pseudo-DMA** (blind and polled block transfers):
   - MR.DMA = 1, plus a write to reg 5 (send) or reg 7 (initiator receive) →
     DMA is armed;
   - BSR.DRQ = 1 whenever the target has REQ asserted, DMA is armed and the
     phase matches;
   - each DACK read gives the next byte and does a full REQ/ACK cycle inside;
     each DACK write takes a byte the same way;
   - when the target leaves the data phase, phase match drops and DRQ clears.
     When the target runs out of bytes, set BSR.EODMA. Clearing MR.DMA disarms.
   - There is no DRQ wire on the Plus, so blind transfers assume the device
     keeps up. Ours always does, because the data is buffered.
6. **Bus free.** After MESSAGE IN (Command Complete, `0x00`) is ACKed, the
   target releases BSY and all phase lines.

### 4.4 Interface to the target layer (`scsi_disk.h`)

```c
typedef struct {            /* signals the target drives */
    uint8_t bsy, req, msg, cd, io;
    uint8_t data;           /* valid while io=1 */
} scsi_tgt_signals_t;

int  scsi_disk_attach(int id, disc_descr_t *d);   /* 0..6 */
int  scsi_disk_present(int id);
void scsi_disk_bus_reset(void);
void scsi_disk_select(int id, int atn);           /* -> MSG OUT or COMMAND */
void scsi_disk_ack(uint8_t data_from_initiator);  /* ACK rising edge */
void scsi_disk_ack_release(void);                 /* ACK falling edge */
const scsi_tgt_signals_t *scsi_disk_signals(void);
```

Only one target is active at a time (the one currently selected). Disconnect
and reselect are **not** supported: the Plus ROM doesn't use them.

---

## 5. Disk target (`scsi_disk.c`)

### 5.1 Phase state machine

```
BUS FREE ──select──▶ [MSG OUT if ATN: accept IDENTIFY 0x80|lun, ignore others]
          ──▶ COMMAND (CDB length from group code: 0→6, 1/2→10, 5→12)
          ──▶ DATA IN / DATA OUT (if the command has a transfer length)
          ──▶ STATUS (0x00 GOOD / 0x02 CHECK CONDITION)
          ──▶ MESSAGE IN (0x00 COMMAND COMPLETE) ──▶ BUS FREE
```

LUN ≠ 0 → INQUIRY reports peripheral qualifier `0x7F`; every other command
returns CHECK CONDITION / ILLEGAL REQUEST / LUN NOT SUPPORTED (ASC `0x25`).

### 5.2 Commands

| Op | Command | Notes |
| --- | --- | --- |
| `00` | TEST UNIT READY | GOOD |
| `01` | REZERO UNIT | GOOD |
| `03` | REQUEST SENSE | Return the stored sense (key/ASC/ASCQ), then clear it. 18-byte format, but cut to the allocation length (old drivers ask for 4) |
| `04` | FORMAT UNIT | GOOD, do nothing. Optionally zero the image if `SCSI_ALLOW_FORMAT` is set |
| `08` / `28` | READ(6) / READ(10) | READ(6): 21-bit LBA, and length 0 means 256 blocks |
| `0A` / `2A` | WRITE(6) / WRITE(10) | Read-only image → DATA PROTECT (key 7, ASC `0x27`) |
| `0B` / `2B` | SEEK | GOOD |
| `12` | INQUIRY | Type 0 (direct access), ANSI version 1 or 2, response format 1. Vendor/product from config (default `SEAGATE ` / `ST225N`, which old HD SC Setup versions accept) |
| `15` | MODE SELECT(6) | Read the data in and throw it away |
| `1A` | MODE SENSE(6) | Pages 0x01 (error recovery), 0x03 (format: 512-byte sectors), 0x04 (geometry: made up so C×H×S ≥ capacity), 0x30 (Apple vendor page: `"APPLE COMPUTER, INC   "`, which HD SC Setup checks), 0x3F (all pages). Block descriptor: block count and length 512 |
| `1B` | START/STOP UNIT | GOOD |
| `1D` | SEND DIAGNOSTIC | GOOD |
| `1E` | PREVENT/ALLOW REMOVAL | GOOD |
| `25` | READ CAPACITY | Last LBA and 512 |
| `2F` | VERIFY | GOOD |
| `3B` / `3C` | WRITE/READ BUFFER | Optional. Data mode on a 512-byte scratch buffer |
| other | | CHECK CONDITION, ILLEGAL REQUEST, ASC `0x20` |

Range errors: an LBA past the end → ILLEGAL REQUEST, ASC `0x21`.
An SD I/O error → MEDIUM ERROR (key 3, ASC `0x11` for reads, `0x0C` for writes).

### 5.3 Data path and buffering (ESP32-specific)

- Block size is fixed at 512. The image must be a multiple of 512; round down
  and warn if it isn't.
- **Reads:** keep a buffer of `SCSI_READAHEAD_SECTORS × 512` bytes (4 KB by
  default, in internal DRAM if there's room, otherwise PSRAM). When the
  DATA IN cursor reaches the end of the buffer, refill it with **one**
  `op_read` call for up to N sectors. This makes far fewer FATFS `fseek`/`fread`
  calls than going one sector at a time; each one costs about 1 ms over SPI.
- **Writes:** collect bytes into the same buffer and call `op_write` when it is
  full and at the end of the command. Call `fflush` **once per command**, not
  once per sector. (Today `disc_sd_write` flushes on every call. Add an
  `op_flush`, or a flag, so SCSI can batch.)
- Offsets: `disc_op_read` takes an `unsigned int` byte offset, and newlib's
  `fseek` takes a 32-bit `long`. That limits images to **< 2 GB**. The Plus
  driver mostly uses READ(6) anyway (21-bit LBA → 1 GB), and System 6 HFS has
  a 2 GB limit. Refuse images ≥ 2 GB at attach time with a clear log message.
- SD access happens synchronously inside `cpu_read_byte`, on the emulator task,
  the same way the Sony driver does it. No new tasks or locks. The SD card
  shares nothing with the VGA DMA (it is on SPI3), so no new conflicts.

### 5.4 Memory budget

About 64 bytes of state per target × 7, one shared 4 KB transfer buffer, and an
18-byte sense block per target. That's well under 5 KB in total. No change to
the 1 MB PSRAM Mac RAM, the internal-RAM framebuffer, or the 32 KB umac task
stack.

---

## 6. Disk images and the boot flow

The emulation works at the hardware level, so the Mac runs **the SCSI driver
stored on the disk image itself**:

1. The ROM selects IDs 6 → 0, sends TEST UNIT READY / INQUIRY, then reads block 0.
2. Block 0 must be a **Driver Descriptor Map** (signature `ER`, `0x4552`).
   It points to the driver partition. The partition map starts at block 1 with
   `PM` entries.
3. The ROM loads that driver into the system heap and runs it. The driver
   installs a drive-queue entry and mounts the HFS partition.

So **a bare HFS volume (today's `disk.img`) will not boot or mount as a SCSI
disk.** Images must be full, partitioned SCSI disk images:

- images made for BlueSCSI / ZuluSCSI / SCSI2SD (e.g. the well-known
  "HD20SC"/"System 6 Plus" images): these just work;
- or a blank file (`dd if=/dev/zero of=scsi0.img bs=1M count=100`) set up from
  inside the emulator with **HD SC Setup** (vendor/product answers as above, or
  a patched HD SC Setup), or with a third-party tool such as Lido or
  SilverLining.

Safety check at attach time: read block 0. If it isn't `ER`, log
`scsiN.img has no Apple partition map: it will not mount (use it as disk.img instead)`.
Still attach it, so HD SC Setup can initialise it.

Boot order: the Plus boots from the floppy (Sony `disk.img`) first, then SCSI
from the highest ID. umac doesn't emulate PRAM/RTC, so the Startup Disk setting
can't be saved.

- **Floppy + SCSI:** boot System 6 from `disk.img`, and the SCSI volumes show up
  on the desktop.
- **SCSI only:** with no `disk.img`, the existing 2-byte fallback descriptor
  gives "no disk", and the ROM moves on to SCSI. Check that the ROM doesn't
  stop at the blinking question mark first. If it does, add a `main.cpp`
  option `SCSI_BOOT_ONLY` that leaves the Sony drive empty.

The `$0B22` bit-15 hack from Mini vMac must **not** be added. It tells the ROM to
skip SCSI.

---

## 7. Implementation phases

Each phase can be built and tested on the board before starting the next.

### Phase 0: Groundwork (no behaviour change)
- [ ] `disc_sd.c`: split mount and open; `max_files = 10`; `disc_sd_open_file()`;
      add a no-flush write option. `disk.img` behaviour stays the same.
- [ ] `user_config.h`: add the `ENABLE_SCSI`, `SCSI_*` options.
- [ ] `machw.h` / `umac_main.c`: add `IS_SCSI`, send its accesses to a new
      `ncr5380_read/write` that, for now, reproduces today's "empty bus"
      (reads 0). Word/long accesses no longer `exit_error`.
- ✅ Check: boots exactly as before, and the serial log shows SCSI accesses
  during boot (`SCSI_DEBUG=1`).

### Phase 1: 5380 register model, empty bus
- [ ] Full register file, CSR/BSR built from the signals, bus reset,
      arbitration, selection timeout (no targets attached).
- [ ] Hook `ncr5380_reset()` into `cpu_pulse_reset`, `umac_init`, `umac_reset`.
- ✅ Check: boots normally. A `SCSI_DEBUG` trace shows the ROM arbitrating and
  selecting IDs 6..0 and giving up on each one cleanly (no hang, and boot time
  is unchanged within about 1 s).

### Phase 2: Target bring-up (programmed I/O)
- [ ] `scsi_disk.c` with the phase machine, TEST UNIT READY, INQUIRY,
      REQUEST SENSE, READ CAPACITY, READ(6/10).
- [ ] `main.cpp` attaches `scsi0..6.img` read-only first.
- ✅ Check: the trace shows selection → CDB → data → status → message for
  ID N. With a BlueSCSI-style System 6 image as `scsi6.img`, the volume appears
  on the desktop after booting from the floppy.

### Phase 3: Pseudo-DMA and blind transfers
- [ ] DACK (A9) port, DRQ, EODMA, MR.DMA arm/disarm, start-DMA registers.
- ✅ Check: the Apple driver's blind reads work (it uses them for 512-byte
  blocks). Copying a big file shows no data corruption: compare the image's
  CRC before and after a read-only session, and compare checksums of the files
  copied.

### Phase 4: Writes, MODE SENSE/SELECT, and the rest of the command set
- [ ] WRITE(6/10), per-command flush, read-only enforcement, MODE SENSE pages,
      FORMAT/VERIFY/etc. no-ops, sense data for errors.
- ✅ Check: create a folder and copy files; restart → still there; the image
  mounts cleanly in Mini vMac/Basilisk/BlueSCSI on a real Mac. Initialise a
  blank `scsi0.img` with HD SC Setup. Boot with no floppy (SCSI only).

### Phase 5: Hardening and tests
- [ ] **Host test harness** `tools/scsi_host_test.c`: build `ncr5380.c` +
      `scsi_disk.c` with gcc against an in-memory `disc_descr_t`. Script the
      same register sequences the Plus SCSI Manager uses (arbitrate, select,
      send CDB with the ACK handshake, blind-read 512 bytes over DACK, status,
      message), and assert on the data. It runs in seconds, without the board.
- [ ] Edge cases: missing ID, LUN ≠ 0, LBA out of range, image not a multiple
      of 512, image ≥ 2 GB, SD removed mid-session (MEDIUM ERROR, no crash),
      bus reset in the middle of a command, a write to a read-only image.
- [ ] Measure throughput: time a 1 MB Finder copy from SCSI and from the Sony
      `disk.img`, and record both in the README.

### Phase 6 (optional): performance fast path
Every SCSI byte goes through `cpu_read_byte` and the 68k blind-transfer loop,
so SCSI will likely be **slower than the paravirtual Sony path**, which does one
`memcpy` per request. If Phase 5 shows it's too slow:
- **6a.** Order the checks in `cpu_read_byte` so `IS_SCSI` comes right after
  RAM/ROM; make the DACK path a tight inline function; consider `IRAM_ATTR`.
- **6b.** Paravirtual shortcut: patch the ROM SCSI Manager's blind-read/write
  routines (`_SCSIRBlind`/`_SCSIWBlind` selectors of `_SCSIDispatch`, trap
  `$A815`) to jump to a PV address, like `PV_SONY_ADDR`. That handler copies
  the whole TIB transfer between the target buffer and Mac RAM in one go.
  The hardware model stays the fallback for everything else.

### Phase 7: Docs
- [ ] README: a "SCSI hard disks" section (file names, where to get images,
      HD SC Setup notes, the 2 GB limit, boot order).
- [ ] `src/emu/README.md`: the new files and the local changes.

---

## 8. Risks and open questions

| Risk | Mitigation |
| --- | --- |
| ROM timing assumptions (selection timeout loops, REQ wait loops) | We answer instantly, which is always "fast enough". Check that the empty-bus scan time hasn't grown in Phase 1 |
| HD SC Setup refuses non-Apple drives | Vendor/product strings and the Apple mode page 0x30 can be changed in `user_config.h` |
| Throughput lower than the Sony path | Phase 6. Users can keep the system on `disk.img` and use SCSI for extra storage |
| SD write latency stalls the emulated Mac | Flush once per command; the read-ahead buffer is also used for writes; maybe a write-back task later |
| Image corruption on power loss | `fflush` per command plus `fsync` at the end of WRITE. Document that the board shouldn't be powered off while the disk is active |
| `max_files` / FATFS handles | Raised to 10; log a clear error if opening fails |
| Licence mixing | New files are written from scratch under MIT; GPL sources (Mini vMac, PCE, BlueSCSI) are only for checking behaviour |

Questions for the maintainer before coding:
1. Image naming: fixed `scsi0.img…scsi6.img`, or a `scsi.cfg` mapping file?
2. Should SCSI images ever be created automatically? (The plan says no, unlike
   `disk.img`.)
3. Is SCSI-only boot (no floppy) a must-have for the first release, or is
   "boot from floppy, mount SCSI" enough?
4. Should the CD-ROM target (Apple CD-ROM, 2048-byte blocks) be planned in
   later? The target layer is designed to allow it.
