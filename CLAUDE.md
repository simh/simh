# CLAUDE.md - AI Assistant Guide for SIMH

This document provides comprehensive guidance for AI assistants working with the SIMH (Simulator for Historical Computers) codebase.

## Repository Overview

SIMH is a highly portable, multi-platform framework for building and running historical computer system simulators. The repository contains 40+ different computer simulators spanning mainframes, minicomputers, and microcomputers from the 1950s-1990s, including DEC, IBM, HP, Data General, and many other vendors' systems.

**License**: Modified X11 License (very permissive)
**Primary Language**: C (ANSI C with some C99 features)
**Platforms**: Linux, macOS, Windows, *BSD, Solaris, HP-UX, AIX, OpenVMS
**Primary Maintainer**: Mark Pizzolato (simh/simh fork)

## Critical Context

### Project History
- Original author: Robert M. Supnik
- This repository (simh/simh) is Mark Pizzolato's development branch
- Separate from Open SIMH fork (different development paths)
- Contains exclusive features not in Open SIMH

### Code Philosophy
1. **Portability First**: Code must work on ancient and modern systems
2. **No Breaking Changes**: Backward compatibility is sacred
3. **Conservative C**: Uses portable C constructs, careful with C99/C11 features
4. **Self-Contained**: Minimal external dependencies (they're optional)
5. **Framework + Simulators**: Core SCP framework + individual simulator implementations

---

## Directory Structure

### Root Directory Layout

```
/home/user/simh/
├── scp.c, scp.h                    # Simulator Control Program (framework core)
├── sim_*.c, sim_*.h                # Framework libraries (disk, tape, ether, console, etc.)
├── sim_defs.h                      # Core type definitions (CRITICAL - read this first)
├── sim_rev.h                       # Version information
├── makefile                        # GNU Make build system (primary for Unix/Linux/macOS)
├── descrip.mms                     # OpenVMS MMS build file
├── README.md                       # Project documentation (comprehensive)
│
├── [SIMULATOR]/                    # Individual simulator directories (40+ total)
│   ├── <sim>_cpu.c                 # CPU implementation
│   ├── <sim>_sys.c                 # System config & device table
│   ├── <sim>_defs.h                # Simulator-specific definitions
│   └── <sim>_<device>.c            # Device implementations
│
├── display/                        # Vector display support (VT11, Type 340, etc.)
├── frontpanel/                     # Front panel API
├── slirp/, slirp_glue/             # Network NAT support
├── doc/                            # Simulator documentation (.doc files)
├── Visual Studio Projects/         # Windows Visual Studio build files
└── .github/                        # GitHub workflows and templates
```

### Key Simulator Directories

**DEC Systems**: PDP1, PDP8, PDP10, PDP11, PDP18B, VAX (largest), alpha
**IBM Systems**: I1401, I1620, I650, I7000, I7094, Ibm1130
**HP Systems**: HP2100, HP3000
**Data General**: NOVA, ECLIPSE
**Others**: 3B2, ALTAIR, AltairZ80, B5500, BESM6, CDC1700, Intel-Systems/, SEL32, and more

### Framework Files (sim_*.c/h pattern)

**Core Framework**:
- `scp.c` (730KB!) - Main simulator control program
- `scp.h` - Public SCP API
- `sim_defs.h` (57KB) - **READ THIS FIRST** - All core types, constants, error codes

**Device Libraries** (use these for device implementations):
- `sim_console.c/h` - Console I/O
- `sim_tmxr.c/h` (249KB) - Terminal multiplexer support
- `sim_disk.c/h` (364KB) - Disk device support (VHD, SIMH, RAW formats)
- `sim_tape.c/h` (212KB) - Tape device support (multiple formats)
- `sim_ether.c/h` (209KB) - Ethernet support (libpcap, NAT, UDP)
- `sim_serial.c/h` - Serial port support
- `sim_sock.c/h` - Network socket utilities
- `sim_fio.c/h` - File I/O abstraction layer
- `sim_timer.c/h` - Timing and calibration
- `sim_video.c/h` - Video display support (SDL2-based)
- `sim_frontpanel.c/h` - Front panel API
- `sim_card.c/h` - Punched card support
- `sim_scsi.c/h` - SCSI device support

---

## Code Organization Patterns

### Standard Simulator Structure

Every simulator follows this pattern:

```
<SIMULATOR>/
├── <sim>_cpu.c         # CPU implementation (required)
├── <sim>_sys.c         # System configuration (required)
├── <sim>_defs.h        # Simulator definitions (required)
├── <sim>_io.c          # I/O subsystem (common)
├── <sim>_<device>.c    # Individual devices
└── tests/              # Test scripts (.ini, .do files) and diagnostics
```

### File Naming Conventions

**Strict Patterns**:
- Framework: `sim_<subsystem>.c/h` (lowercase with underscores)
- Simulator main files: `<sim>_cpu.c`, `<sim>_sys.c`, `<sim>_defs.h`
- Device files: `<sim>_<device>.c` where device is an abbreviation
- ROM headers: `<model>_<component>_bin.h` (binary embedded as C array)

**Common Device Abbreviations**:
- `cpu` - Central Processing Unit
- `sys` - System configuration
- `fpa` - Floating Point Accelerator
- `mmu` - Memory Management Unit
- `stddev` - Standard devices (console, clock, etc.)
- `dz`, `dl`, `dh`, `vh` - Terminal multiplexers
- `rp`, `rq`, `rl`, `rk`, `hk` - Disk controllers
- `tm`, `ts`, `tu`, `tq` - Tape controllers
- `xu`, `xq` - Ethernet controllers
- `lp` - Line printer
- `cr`, `cd` - Card reader
- `pt`, `pp` - Paper tape

### Typical Source File Structure

**Device Implementation (`<sim>_<device>.c`)**:
```c
/* File header with copyright (Modified X11 License) */
/* Change history comments */

#include "<sim>_defs.h"      // Simulator definitions
#include "sim_<library>.h"    // Framework libraries as needed

/* Device state variables (static) */
static int32 dev_state;

/* Forward declarations */
static t_stat dev_rd(/* ... */);
static t_stat dev_wr(/* ... */);
static t_stat dev_svc(UNIT *uptr);
static t_stat dev_reset(DEVICE *dptr);

/* UNIT structure - device units */
UNIT dev_unit[] = {
    { UDATA (&dev_svc, UNIT_FLAGS, CAPACITY) },
    /* ... */
};

/* REG structure - visible registers for EXAMINE/DEPOSIT */
REG dev_reg[] = {
    { ORDATA (STATE, dev_state, 16) },
    /* ... */
    { NULL }  // Terminator
};

/* MTAB structure - modifiers for SET/SHOW commands */
MTAB dev_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "SETTING", "SETTING", &dev_set, &dev_show },
    /* ... */
    { 0 }  // Terminator
};

/* DEVICE structure - ties everything together */
DEVICE dev_dev = {
    "DEV", dev_unit, dev_reg, dev_mod,
    NUMUNITS, RDIX, WIDTH, 1, RDIX, WIDTH,
    NULL, NULL, &dev_reset,
    NULL, &dev_attach, &dev_detach,
    &dev_dib, DEV_FLAGS
};

/* Implementation functions */
```

**System Configuration (`<sim>_sys.c`)**:
```c
/* Includes */
#include "<sim>_defs.h"

/* External device declarations */
extern DEVICE cpu_dev;
extern DEVICE dev1_dev;
/* ... */

/* Master device table (REQUIRED - SCP looks for this) */
DEVICE *sim_devices[] = {
    &cpu_dev,
    &dev1_dev,
    /* ... */
    NULL  // Terminator
};

/* Simulator name (REQUIRED) */
char sim_name[] = "SIMULATOR-NAME";

/* Save/restore PC (REQUIRED) */
REG *sim_PC = &cpu_reg[PC_idx];

/* Implementation of required functions */
t_stat sim_load(FILE *fileref, CONST char *cptr, CONST char *fnam, int flag) { /* ... */ }
t_stat fprint_sym(FILE *of, t_addr addr, t_value *val, UNIT *uptr, int32 sw) { /* ... */ }
t_stat parse_sym(CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw) { /* ... */ }
```

---

## Coding Conventions

### Style Guidelines

1. **Indentation**: 4 spaces (NOT tabs in most files)
2. **Braces**: K&R style (opening brace on same line for functions, controls)
3. **Line Length**: Try to keep under 132 characters (old terminal width)
4. **Comments**: `/* C style */` only (not `//`)
5. **Case**:
   - Preprocessor: `UPPER_CASE`
   - Types: `lower_case` or `t_typename`
   - Functions: `lower_case_with_underscores`
   - Globals: `lower_case_with_underscores`

### Critical Types (from sim_defs.h)

```c
typedef int32 t_stat;           // Status return
typedef int32 t_bool;           // Boolean
typedef uint32 t_addr;          // Addresses (or uint64 for 64-bit systems)
typedef uint64 t_uint64;        // 64-bit unsigned
typedef int64 t_int64;          // 64-bit signed
typedef double t_svalue;        // Double precision

#define TRUE  1
#define FALSE 0
```

### Status Codes (SCPE_* constants)

Always return `t_stat` from functions. Common codes:
```c
SCPE_OK         // Success
SCPE_NXM        // Non-existent memory
SCPE_UNATT      // Unit not attached
SCPE_IOERR      // I/O error
SCPE_ARG        // Argument error
SCPE_STOP       // Simulator stopped
SCPE_EXIT       // Simulator exit
// See sim_defs.h for complete list (40+ codes)
```

### Device Structure Macros

**UNIT Flags** (common):
```c
UNIT_ATTABLE    // Can attach files
UNIT_DISABLE    // Can be disabled
UNIT_FIX        // Fixed capacity
UNIT_SEQ        // Sequential device
UNIT_IDLE       // Idle capable
UNIT_RO         // Read only
UNIT_ROABLE     // Can be set read-only
```

**Register Flags** (REG structure):
```c
REG_RO          // Read only
REG_HIDDEN      // Hidden from user
REG_FIT         // Value must fit
```

### DEVICE Structure Template

```c
DEVICE dev_dev = {
    "NAME",                 // Device name (uppercase, shown to user)
    dev_unit,               // Unit array
    dev_reg,                // Register array
    dev_mod,                // Modifier array
    NUM_UNITS,              // Number of units
    10,                     // Address radix (8, 10, 16)
    31,                     // Address width (bits)
    1,                      // Address increment
    8,                      // Data radix
    8,                      // Data width (bits)
    &dev_ex,                // Examine routine (or NULL)
    &dev_dep,               // Deposit routine (or NULL)
    &dev_reset,             // Reset routine (or NULL)
    &dev_boot,              // Boot routine (or NULL)
    &dev_attach,            // Attach routine (or NULL)
    &dev_detach,            // Detach routine (or NULL)
    &dev_dib,               // Context (DIB, etc.) (or NULL)
    DEV_DISABLE | DEV_DIS,  // Flags
    0,                      // Debug flags
    dev_deb,                // Debug table (or NULL)
    NULL,                   // Memory size routine
    NULL,                   // Logical name
    &dev_help,              // Help routine (recommended!)
    NULL,                   // Attach help
    NULL,                   // Help context
    &dev_description        // Description routine (recommended!)
};
```

---

## Build System

### Primary Build Methods

1. **GNU Make** (Linux, macOS, *BSD, Solaris, etc.) - **PREFERRED**
2. **Visual Studio** (Windows) - Full featured
3. **MMS** (OpenVMS)
4. **MinGW** (Windows) - DEPRECATED, limited functionality

### Building with GNU Make

**Location**: `/home/user/simh/makefile` (136KB - comprehensive!)

**Basic Usage**:
```bash
# Build a specific simulator
make vax

# Build all simulators
make all

# Clean build artifacts
make clean

# Build with debugging
make DEBUG=1 vax

# Build without tests
make TESTS=0 vax

# Build with separate object files (faster incremental builds)
make BUILD_SEPARATE=1 vax

# Quiet build (summary output)
make QUIET=1 vax

# Build with Link Time Optimization
make LTO=1 vax
```

**Key Make Variables**:
- `DEBUG=1` - Debug build with symbols, no optimization
- `TESTS=0` - Skip running tests after build
- `NONETWORK=1` - Disable networking support
- `NOVIDEO=1` - Disable video display support
- `BUILD_SEPARATE=1` - Compile each .c file separately (DEFAULT, faster incremental)
- `QUIET=1` - Summary build output (DEFAULT)
- `WARNINGS=ALLOWED` - Allow compiler warnings (default fails on warnings)
- `LTO=1` - Enable Link Time Optimization
- `OPTIMIZE=-O3` - Change optimization level
- `DONT_USE_ROMS=1` - Don't embed ROMs (fetch externally at runtime)

**Dependencies** (all optional, auto-detected):

*Linux (Debian/Ubuntu)*:
```bash
sudo apt-get install libpcap-dev libvdeplug-dev libpcre3-dev \
    libedit-dev libsdl2-dev libpng-dev libsdl2-ttf-dev
```

*macOS (Homebrew)*:
```bash
brew install vde pcre libedit sdl2 libpng zlib sdl2_ttf
```

*macOS (MacPorts)*:
```bash
sudo port install vde2 pcre libedit libsdl2 libpng zlib libsdl2_ttf
```

### Building with Visual Studio (Windows)

**Location**: `/home/user/simh/Visual Studio Projects/`

**Requirements**:
- Visual Studio 2008+ (Express or Community)
- Recommend: Visual Studio Community 2022
- Automatically downloads `windows-build` dependency package

**Quick Build**:
```cmd
REM From repository root
build_vstudio.bat

REM Builds all simulators to BIN/NT/Win32-Release/
```

**Manual Build**:
1. Open `Visual Studio Projects/simh.sln`
2. Select Debug or Release configuration
3. Build Solution

**Output**: `BIN/NT/Win32-Release/` or `BIN/NT/Win32-Debug/`

### Build Output Locations

- **GNU Make**: `./<simulator-name>` in repo root (e.g., `./vax`)
- **Visual Studio**: `BIN/NT/Win32-Release/<simulator>.exe`
- **Object files** (BUILD_SEPARATE=1): `BIN/<os>/<variant>/`

---

## Development Workflows

### Adding a New Device to Existing Simulator

1. **Create device file**: `<simulator>/<sim>_<device>.c`
2. **Implement required structures**:
   - UNIT array
   - REG array (registers for EXAMINE/DEPOSIT)
   - MTAB array (SET/SHOW modifiers)
   - DEVICE structure
   - Service routine (`<device>_svc`)
   - Reset routine (`<device>_reset`)
   - I/O handlers

3. **Add to system config** (`<sim>_sys.c`):
   ```c
   extern DEVICE dev_dev;

   DEVICE *sim_devices[] = {
       /* ... existing devices ... */
       &dev_dev,
       /* ... */
       NULL
   };
   ```

4. **Update build files**:
   - Add to makefile variable for simulator
   - Add to Visual Studio project
   - Add to VMS descrip.mms

5. **Add help text** (recommended):
   - Implement `dev_help()` and `dev_description()` functions
   - Or create `.txt` documentation in `doc/`

6. **Test thoroughly**:
   - Create test script in `<simulator>/tests/`
   - Test ATTACH/DETACH, SET/SHOW, BOOT (if applicable)

### Modifying Framework Code (sim_*.c)

**CAUTION**: Framework changes affect ALL simulators!

1. **Check compatibility**: Will this break any simulator?
2. **Test broadly**: Build and test multiple simulators (especially PDP11, VAX, PDP8, I1401)
3. **Platform testing**: Test on Linux, macOS, Windows if possible
4. **Preserve backward compatibility**: Never break existing behavior
5. **Document**: Update comments and relevant .doc files

### Common Development Tasks

**Add a new disk type to existing disk controller**:
1. Find controller code (e.g., `pdp11_rq.c` for MSCP)
2. Add entry to disk type table
3. Update help text
4. Test with real disk images if available

**Add new network device**:
1. Use `sim_ether.c/h` framework
2. Study existing examples: `pdp11_xu.c`, `pdp11_xq.c`
3. Implement packet send/receive
4. Support multiple transport modes (pcap, NAT, UDP)

**Add new tape device**:
1. Use `sim_tape.c/h` framework
2. Study existing examples: `pdp11_tm.c`, `pdp11_tq.c`
3. Support standard tape formats (SIMH, E11, TPC, TAR, AWS)

---

## Testing

### Test Organization

Tests located in: `<simulator>/tests/`

**Test File Types**:
- `.ini` - SIMH command scripts (main test drivers)
- `.do` - SIMH DO command files (test subroutines)
- `.bin`, `.dsk`, `.tap` - Binary images and media
- `.pdf` - Diagnostic documentation

**Example Test Structure**:
```
PDP11/tests/
├── pdp11_test.ini          # Main test script
├── diags/                  # DEC MAINDEC diagnostics
│   ├── *.bin               # Diagnostic binaries
│   └── *.pdf               # Diagnostic manuals
└── *.do                    # Individual test scripts
```

### Running Tests

**Automatic** (during build):
```bash
make vax              # Runs tests automatically
make TESTS=0 vax      # Skip tests
```

**Manual**:
```bash
./vax tests/vax-diag_test.ini
```

**Verbose test output**:
```bash
make TEST_ARG=-v vax
```

### Test Script Format (SIMH .ini files)

```ini
; Comment
echo Starting test...

; Set up device
set cpu 11/73
set cpu 4m
attach rq0 test.dsk

; Deposit values
deposit pc 1000

; Run test
run 1000

; Check results
examine r0

; Cleanup
detach rq0
echo Test complete
```

---

## Important Conventions and Best Practices

### DO:

1. **Read existing code first** - Find similar functionality, follow its patterns
2. **Use framework libraries** - Don't reinvent disk/tape/network support
3. **Maintain portability** - Test on multiple platforms when possible
4. **Add help text** - Implement device description and help routines
5. **Follow naming conventions** - Strict adherence to patterns above
6. **Include copyright headers** - Use Modified X11 License template
7. **Document changes** - Add comments at top of file with change history
8. **Test on real software** - Use actual operating systems/diagnostics
9. **Check for memory leaks** - Use valgrind or similar tools
10. **Update all build systems** - makefile, Visual Studio, VMS MMS

### DON'T:

1. **Break backward compatibility** - Existing configurations must still work
2. **Use C99/C11 features carelessly** - Not all platforms support them fully
3. **Assume Unix/Linux** - Code must work on Windows, VMS, etc.
4. **Hardcode paths** - Use sim_fio.c abstractions
5. **Use compiler-specific extensions** - Stick to portable C
6. **Ignore warnings** - Build fails on warnings by default (good!)
7. **Skip testing** - Always test changes thoroughly
8. **Modify licenses** - Keep Modified X11 License
9. **Add dependencies lightly** - External deps must be optional
10. **Use tabs inconsistently** - Follow existing file's style

### Error Handling Pattern

```c
t_stat some_function(/* args */)
{
    t_stat r;

    if (error_condition)
        return SCPE_ARG;        // Return specific error code

    r = sub_function();
    if (r != SCPE_OK)
        return r;                // Propagate errors

    /* Do work */

    return SCPE_OK;              // Success
}
```

### Debug Support Pattern

```c
// In device file
DEBTAB dev_debug[] = {
    {"CMD", DBG_CMD},
    {"DATA", DBG_DATA},
    {0}
};

// In code
if (dev_dev.dctrl & DBG_CMD)
    sim_debug(DBG_CMD, &dev_dev, "Command: %06o\n", cmd);
```

---

## Key Files Reference

### Must-Read Files

1. **`/home/user/simh/sim_defs.h`** - ALL core types, structures, constants
2. **`/home/user/simh/scp.h`** - SCP public API, function prototypes
3. **`/home/user/simh/README.md`** - Comprehensive project documentation
4. **`/home/user/simh/makefile`** - Build system (read comments at top)

### Helpful Examples

**Simple Simulator**: `ALTAIR/` - 8080 system, simple devices
**Medium Complexity**: `PDP8/` - Classic structure, good I/O examples
**Complex Simulator**: `PDP11/` - 70+ files, extensive device support
**Multi-Model**: `VAX/` - Multiple models sharing code
**Network Device**: `PDP11/pdp11_xu.c` - DEUNA Ethernet
**Disk Device**: `PDP11/pdp11_rq.c` - MSCP disk controller (used by VAX too)
**Tape Device**: `PDP11/pdp11_tq.c` - TMSCP tape controller
**Terminal MUX**: `PDP11/pdp11_dz.c` - DZ11 serial multiplexer

### Documentation Files

- `/home/user/simh/doc/simh_doc.doc` - Main SIMH manual (305KB!)
- `/home/user/simh/doc/<sim>_doc.doc` - Simulator-specific manuals
- `/home/user/simh/0readme_ethernet.txt` - Networking setup guide
- `/home/user/simh/Visual Studio Projects/0ReadMe_Projects.txt` - Windows build guide

---

## Quick Reference Commands

### SIMH Command Line (when running simulator)

```
HELP                    - Show available commands
HELP <device>           - Device-specific help
SHOW VERSION            - Version and configuration info
SHOW DEVICES            - List all devices
SHOW <device>           - Show device status
SET <device> <param>    - Configure device
ATTACH <device> <file>  - Attach file/image to device
DETACH <device>         - Detach file
BOOT <device>           - Bootstrap from device
RUN [addr]              - Run from address
EXAMINE <register>      - Display register/memory
DEPOSIT <register> <val> - Set register/memory
RESET                   - Reset all devices
QUIT                    - Exit simulator
```

### Environment Variables (Framework)

- `SIM_NAME` - Simulator name
- `SIM_OSTYPE` - Operating system type
- `SIM_MAJOR`, `SIM_MINOR`, `SIM_PATCH` - Version numbers
- See README.md for complete list

---

## Troubleshooting Build Issues

### Common Problems

**"pcap.h not found"**:
- Install libpcap-dev package OR
- Build with `make NONETWORK=1`

**"SDL.h not found"**:
- Install SDL2 development package OR
- Build with `make NOVIDEO=1`

**Warning errors**:
- Fix the warning (preferred) OR
- Build with `make WARNINGS=ALLOWED` (not recommended)

**Tests fail**:
- Check if test media is available
- Build with `make TESTS=0` to skip
- Check `<simulator>/tests/` for test requirements

**Windows build fails**:
- Ensure `windows-build` directory exists parallel to source
- Run `build_vstudio.bat` to auto-download dependencies
- Use Visual Studio 2008 or later

### Platform-Specific Notes

**macOS**:
- Use Homebrew or MacPorts for dependencies
- May need to install Xcode Command Line Tools
- vmnet networking available on macOS 10.15+

**Linux**:
- Different package names on different distros
- See makefile comments for package lists
- May need root for some network features

**Windows**:
- Use Visual Studio (not MinGW)
- Requires Visual C++ workload
- Network requires WinPcap or Npcap

**OpenVMS**:
- Use MMS or MMK
- See README.md VMS section
- Networking only on Alpha/IA64

---

## Getting Help

### Resources

1. **GitHub Issues**: https://github.com/simh/simh/issues
2. **README.md**: Comprehensive project documentation
3. **HELP command**: In simulator, type HELP
4. **Doc files**: `/home/user/simh/doc/*.doc`
5. **Source comments**: Well-commented code, read it!

### Problem Reports Should Include

- Simulator name
- Host platform and OS version
- Build method (make, Visual Studio, etc.)
- Output of `SHOW VERSION` in simulator
- Steps to reproduce
- Configuration file if applicable
- Output of build process if build fails

---

## Common Patterns and Idioms

### Device Initialization

```c
t_stat dev_reset(DEVICE *dptr)
{
    int32 i;
    UNIT *uptr;

    for (i = 0; i < dptr->numunits; i++) {
        uptr = dptr->units + i;
        sim_cancel(uptr);           // Cancel pending events
        /* Reset unit state */
    }

    /* Reset device registers */
    dev_state = 0;

    return SCPE_OK;
}
```

### Unit Service Routine

```c
t_stat dev_svc(UNIT *uptr)
{
    /* Perform scheduled action */

    /* Schedule next event if needed */
    sim_activate(uptr, dev_wait);

    /* Generate interrupt if needed */
    if (need_interrupt)
        SET_INT(DEV);

    return SCPE_OK;
}
```

### Attach/Detach

```c
t_stat dev_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r;

    r = attach_unit(uptr, cptr);    // Standard attach
    if (r != SCPE_OK)
        return r;

    /* Device-specific initialization */

    return SCPE_OK;
}

t_stat dev_detach(UNIT *uptr)
{
    /* Device-specific cleanup */

    return detach_unit(uptr);       // Standard detach
}
```

### Using sim_disk for Disk Devices

```c
#include "sim_disk.h"

t_stat dev_attach(UNIT *uptr, CONST char *cptr)
{
    return sim_disk_attach(uptr, cptr, sector_size, word_per_sector,
                          TRUE, DBG_DETAIL, "DEV", 0, 0);
}

t_stat dev_detach(UNIT *uptr)
{
    return sim_disk_detach(uptr);
}

t_stat dev_read_sector(UNIT *uptr, uint32 lba, uint8 *buf, uint32 *bc)
{
    return sim_disk_rdsect(uptr, lba, buf, bc, 1);
}
```

---

## Version Control and Contributions

### This Repository

- **Main branch**: Active development by Mark Pizzolato
- **Commits**: Should be atomic and well-described
- **Pull Requests**: Changes may be posted to Open SIMH if authored by others
- **Binaries**: Development binaries at https://github.com/simh/Development-Binaries

### Commit Message Guidelines

- First line: Brief summary (50 chars or less)
- Blank line
- Detailed description if needed
- Reference issue numbers if applicable

Example:
```
PDP11: Fix DZ11 modem control signals

The DZ11 was not properly asserting DTR on connection.
This caused issues with some terminal emulators expecting
hardware handshaking.

Fixes #1234
```

---

## Advanced Topics

### Asynchronous I/O

Enabled with `SIM_ASYNCH_IO` define. Used for:
- Disk I/O (sim_disk.c)
- Tape I/O (sim_tape.c)
- Better performance on modern systems

### Network Transports

Three modes supported:
1. **PCAP** - Direct network access (requires libpcap)
2. **NAT** - TCP/IP only, works on WiFi
3. **UDP** - Direct simulator-to-simulator connections (HECnet)

### Front Panel API

Programmatic control of simulators:
- Start/stop/step execution
- Read/write memory and registers
- Implemented in `sim_frontpanel.c/h`
- See `frontpanel/FrontPanelTest.c`

### Video Display Support

For simulators with graphics:
- Based on SDL2
- Supports monochrome and color
- Keyboard and mouse input
- Used by VAXstation, IMLAC, PDP-1, etc.

---

## Summary

**This is a mature, stable, portable codebase with 30+ years of history.**

**Key principles**:
1. Follow existing patterns religiously
2. Maintain backward compatibility
3. Keep code portable
4. Test thoroughly
5. Document well

**When in doubt**: Look at how PDP11 or VAX does it - they're the most complete and well-maintained simulators in the repository.

**Remember**: This code runs historical software that may be irreplaceable. Correctness and stability are paramount.

---

**Document Version**: 1.0
**Last Updated**: 2025-11-18
**Repository State**: Based on commit 060747a
