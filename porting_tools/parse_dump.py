#!/usr/bin/env python3
"""
Vita Crash Dump Analyzer (vita-parse-core Wrapper & Dynamic Symbol Resolver)
-----------------------------------------------------------------------------
Herramienta universal y dinámica para análisis de crash dumps (.psp2dmp / psp2core-*)
en ports de PS Vita (so-loader y ejecutables nativos Vita).

Autor: Antigravity / Porting Tools Suite
"""

import os
import sys
import subprocess
import argparse
import glob
from collections import defaultdict, Counter

# 1. Asegurar herramientas del VitaSDK en el PATH
VITASDK_BIN = os.environ.get("VITASDK", "/Users/metalsyntax/vitasdk") + "/bin"
if os.path.exists(VITASDK_BIN) and VITASDK_BIN not in os.environ.get("PATH", ""):
    os.environ["PATH"] = f"{VITASDK_BIN}:{os.environ.get('PATH', '')}"

# 2. Localizar e importar vita-parse-core
VITA_PARSE_CORE_DIR = os.environ.get("VITA_PARSE_CORE_DIR", "/Users/metalsyntax/vita-tools/vita-parse-core")
if os.path.exists(VITA_PARSE_CORE_DIR) and VITA_PARSE_CORE_DIR not in sys.path:
    sys.path.insert(0, VITA_PARSE_CORE_DIR)

try:
    from core import CoreParser
    from elf import ElfParser
    from util import u32
except ImportError as e:
    print(f"[-] Error: No se pudo cargar 'vita-parse-core' desde '{VITA_PARSE_CORE_DIR}': {e}")
    print("    Asegúrate de clonar/instalar vita-parse-core o definir VITA_PARSE_CORE_DIR.")
    sys.exit(1)

STR_STOP_REASON = defaultdict(str, {
    0: "No reason (Ejecución normal)",
    0x30002: "Undefined instruction exception (Instrucción no válida / corrupta)",
    0x30003: "Prefetch abort exception (Fallo al buscar instrucción en memoria)",
    0x30004: "Data abort exception (Fallo de acceso a memoria / Puntero nulo o inválido)",
    0x60080: "Division by zero (División por cero en operación entera)",
})

def demangle(name):
    if not name or not name.startswith("_Z"):
        return name
    for cmd_name in ["arm-vita-eabi-c++filt", "c++filt"]:
        try:
            res = subprocess.run([cmd_name, name], capture_output=True, text=True)
            if res.returncode == 0 and res.stdout.strip():
                return res.stdout.strip()
        except Exception:
            continue
    return name

class SymbolTable:
    def __init__(self, so_path):
        self.so_path = so_path
        self.symbols = []  # list of (start_addr, end_addr, demangled_name, raw_name)
        self.load_symbols()

    def load_symbols(self):
        if not self.so_path or not os.path.exists(self.so_path):
            return
        cmd = ["arm-vita-eabi-objdump", "-T", self.so_path]
        try:
            res = subprocess.run(cmd, capture_output=True, text=True)
            if res.returncode != 0:
                return
            for line in res.stdout.splitlines():
                parts = line.split()
                if len(parts) >= 6 and parts[1] in ['g', 'l'] and 'F' in parts[2]:
                    try:
                        addr = int(parts[0], 16)
                        size = int(parts[4], 16)
                        raw_name = parts[-1]
                        demangled = demangle(raw_name)
                        self.symbols.append((addr, addr + size, demangled, raw_name))
                    except ValueError:
                        continue
            self.symbols.sort(key=lambda x: x[0])
        except Exception as e:
            print(f"[!] Warning: No se pudieron extraer símbolos de {self.so_path}: {e}")

    def lookup(self, offset):
        for start, end, demangled, raw in self.symbols:
            if start <= offset < end or (start <= offset <= end + 4 and end > start):
                return f"{demangled} + 0x{offset - start:x}"
        prev = None
        for start, end, demangled, raw in self.symbols:
            if start <= offset:
                prev = (start, demangled)
            else:
                break
        if prev:
            return f"{prev[1]} + 0x{offset - prev[0]:x}"
        return f"0x{offset:x}"

def auto_detect_so_base(dump_addrs, so_syms):
    """
    Auto-detecta la dirección base de memoria asignada dinámicamente al .so en la Vita
    (ej: 0x98000000, 0x90000000) votando según coincidencia de símbolos dinámicos.
    """
    if not so_syms or not so_syms.symbols:
        return None
    candidates = Counter()
    for raw_addr in dump_addrs:
        val = raw_addr & ~1
        if not (0x80000000 <= val <= 0x9fffffff):
            continue
        for sym_start, sym_end, _, _ in so_syms.symbols:
            size = max(sym_end - sym_start, 0x100)
            # Check if val falls within symbol if base is page-aligned
            diff = (val - sym_start)
            base_cand = diff & ~0xFFF  # 4KB boundary
            if 0x80000000 <= base_cand <= 0x9fffffff:
                if 0 <= (val - base_cand - sym_start) <= size:
                    candidates[base_cand] += 1

    if candidates:
        best_base, count = candidates.most_common(1)[0]
        if count >= 1:
            return best_base
    return None

def disassemble_around(bin_path, offset, is_thumb=True):
    if not bin_path or not os.path.exists(bin_path):
        return []
    addr = offset & ~1 if is_thumb else offset
    start = max(0, addr - 0x18)
    end = addr + 0x18
    cmd = [
        "arm-vita-eabi-objdump", "-d",
        f"--start-address=0x{start:x}",
        f"--stop-address=0x{end:x}",
        bin_path
    ]
    if is_thumb:
        cmd.append("-Mforce-thumb")
    try:
        res = subprocess.run(cmd, capture_output=True, text=True)
        lines = []
        in_text = False
        for line in res.stdout.splitlines():
            if "Disassembly of section" in line:
                in_text = True
                continue
            if in_text and line.strip():
                if f"{addr:x}:" in line.lower() or f"{addr:08x}:" in line.lower():
                    lines.append(f"  ==> {line:<52} <== [INSTRUCCIÓN DEL CRASH]")
                else:
                    lines.append(f"      {line}")
        return lines
    except Exception as e:
        return [f"Error al desensamblar: {e}"]

def auto_find_files(root_dir):
    """ Busca automáticamente archivos .elf y .so en el directorio del proyecto. """
    elf_file = None
    so_file = None

    # Search ELF
    elf_candidates = glob.glob(os.path.join(root_dir, "build", "*.elf")) + glob.glob(os.path.join(root_dir, "*.elf"))
    if elf_candidates:
        elf_file = elf_candidates[0]

    # Search SO
    so_candidates = (
        glob.glob(os.path.join(root_dir, "**", "*.so"), recursive=True)
    )
    if so_candidates:
        # Prioritize libgame.so or main .so
        so_candidates.sort(key=lambda x: 0 if "libgame" in x or "libmain" in x else 1)
        so_file = so_candidates[0]

    return elf_file, so_file

def analyze_dump(dump_path, elf_path=None, so_path=None, forced_so_base=None, stack_depth=36):
    report_lines = []

    def log(msg=""):
        print(msg)
        report_lines.append(msg)

    log("================================================================================")
    log("           PS VITA CRASH DUMP ANALYSIS REPORT (vita-parse-core)                ")
    log("================================================================================")
    log(f" Core Dump (.psp2dmp): {dump_path}")
    log(f" Ejecutable ELF (Vita): {elf_path or 'No detectado/proporcionado'}")
    log(f" Librería SO (Android): {so_path or 'No detectada/proporcionada'}")
    log("================================================================================")

    if not os.path.exists(dump_path):
        log(f"[-] Error: Archivo dump no encontrado: {dump_path}")
        return

    core = CoreParser(dump_path)
    elf = ElfParser(elf_path) if elf_path and os.path.exists(elf_path) else None
    so_syms = SymbolTable(so_path) if so_path and os.path.exists(so_path) else None

    crashed_threads = [t for t in core.threads if t.stop_reason != 0]
    if not crashed_threads:
        log("[*] No se detectó hilo en estado de crash explícito. Analizando el primer hilo.")
        crashed_threads = core.threads[:1]

    # Recopilar direcciones del dump para auto-detectar so_base si no fue forzado
    all_dump_addrs = []
    for t in crashed_threads:
        all_dump_addrs.append(t.pc)
        all_dump_addrs.append(t.regs.gpr[14])
        sp = t.regs.gpr[13]
        for x in range(-4, stack_depth):
            d = core.read_vaddr(sp + (4 * x), 4)
            if d:
                all_dump_addrs.append(u32(d, 0))

    so_base = forced_so_base
    if not so_base and so_syms:
        so_base = auto_detect_so_base(all_dump_addrs, so_syms)
        if so_base:
            log(f"[+] Base de memoria para '{os.path.basename(so_path)}' AUTO-DETECTADA en: 0x{so_base:x}")
        else:
            so_base = 0x98000000  # Default fallback para so-loader
            log(f"[*] Base de memoria SO usando fallback por defecto: 0x{so_base:x}")

    def resolve_address(addr):
        notation = core.get_address_notation("", addr)
        if notation.is_located():
            mod_name = notation._VitaAddress__module.name
            seg_num = notation._VitaAddress__segment.num
            offset = notation._VitaAddress__offset
            if elf and (mod_name.endswith(".elf") or mod_name == "dungeon_hunter_2") and seg_num == 1:
                line_info = elf.addr2line(offset)
                line_str = line_info.decode('utf-8', errors='ignore') if isinstance(line_info, bytes) else str(line_info)
                return f"0x{addr:x} [{mod_name} + 0x{offset:x}] ({line_str})"
            return f"0x{addr:x} [{mod_name} seg{seg_num} + 0x{offset:x}]"

        if so_base and (so_base <= addr <= so_base + 0x2000000 or 0x90000000 <= addr <= 0x9fffffff):
            so_offset = addr - so_base
            so_name = os.path.basename(so_path) if so_path else "lib.so"
            if so_syms:
                sym_info = so_syms.lookup(so_offset)
                return f"0x{addr:x} [{so_name} + 0x{so_offset:x} -> {sym_info}]"
            return f"0x{addr:x} [{so_name} + 0x{so_offset:x}]"

        return f"0x{addr:x}"

    for thread in crashed_threads:
        log(f"\n[!] HILO EN CRASH: '{thread.name}' (ID: 0x{thread.uid:x})")
        reason_str = STR_STOP_REASON[thread.stop_reason]
        log(f"    Razón de parada: 0x{thread.stop_reason:x} ({reason_str})")
        log(f"    PC (Program Counter):  {resolve_address(thread.pc)}")
        lr_val = thread.regs.gpr[14]
        log(f"    LR (Link Register):    {resolve_address(lr_val)}")

        log("\n--- REGISTROS CPU ---")
        for i in range(13):
            reg_val = thread.regs.gpr[i]
            res_info = resolve_address(reg_val) if reg_val > 0x10000 else ""
            log(f"    R{i:<2}: 0x{reg_val:08x}  {res_info}")
        sp_val = thread.regs.gpr[13]
        log(f"    SP : 0x{sp_val:08x}")
        log(f"    LR : 0x{lr_val:08x}  ({resolve_address(lr_val)})")
        log(f"    PC : 0x{thread.pc:08x}  ({resolve_address(thread.pc)})")

        # Diagnóstico de Causa Raíz
        log("\n================================================================================")
        log("                   DIAGNÓSTICO AUTOMÁTICO DE CAUSA RAÍZ                         ")
        log("================================================================================")
        if thread.stop_reason == 0x30004:
            log(" [*] Excepción: Data Abort (Acceso a memoria no válida / Violación de segmento).")
        elif thread.stop_reason == 0x30002:
            log(" [*] Excepción: Undefined Instruction (Intento de ejecutar código corrupto/inválido).")
        elif thread.stop_reason == 0x60080:
            log(" [*] Excepción: Division by zero (División entre cero).")

        log(f" [*] Ubicación del crash: PC = {resolve_address(thread.pc)}")

        if so_base and 0x80000000 <= thread.pc <= 0x9fffffff and so_path:
            pc_off = thread.pc - so_base
            dis_lines = disassemble_around(so_path, pc_off, is_thumb=True)
            for line in dis_lines:
                if "[INSTRUCCIÓN DEL CRASH]" in line:
                    log(f" [*] Instrucción causante: {line.strip()}")
                    for reg_i in range(13):
                        if f"r{reg_i}" in line.lower() and thread.regs.gpr[reg_i] == 0:
                            log(f" [*] CAUSA PROBABLE: El registro R{reg_i} es 0x00000000 (Puntero NULO dereferenciado).")

        # Desensamblado en PC
        log("\n--- DESENSAMBLADO EN PC (Punto del Crash) ---")
        if so_base and 0x80000000 <= thread.pc <= 0x9fffffff and so_path:
            pc_off = thread.pc - so_base
            for dl in disassemble_around(so_path, pc_off, is_thumb=True):
                log(dl)
        elif elf:
            notation = core.get_address_notation("PC", thread.pc)
            if notation.is_located():
                elf.disas_around_addr(notation._VitaAddress__offset)

        # Desensamblado en LR
        log("\n--- DESENSAMBLADO EN LR (Dirección de Retorno) ---")
        if so_base and 0x80000000 <= lr_val <= 0x9fffffff and so_path:
            lr_off = (lr_val & ~1) - so_base
            for dl in disassemble_around(so_path, lr_off, is_thumb=True):
                log(dl)

        # Análisis de Pila y Reconstrucción de Llamadas
        log("\n--- CONTENIDO DE LA PILA (STACK BACKTRACE) ---")
        call_chain = [f"PC: {resolve_address(thread.pc)}", f"LR: {resolve_address(lr_val)}"]
        for x in range(-4, stack_depth):
            addr = sp_val + (4 * x)
            data = core.read_vaddr(addr, 4)
            if data:
                val = u32(data, 0)
                prefix = "SP => " if addr == sp_val else "      "
                resolved = resolve_address(val)
                if ".so" in resolved or ".elf" in resolved or "dungeon_hunter_2" in resolved:
                    call_chain.append(f"Stack 0x{addr:x}: {resolved}")
                log(f"    {prefix}0x{addr:08x}: 0x{val:08x}  -> {resolved}")

        log("\n--- SECUENCIA DE LLAMADAS RECONSTRUIDA (CALL CHAIN) ---")
        seen = set()
        for frame in call_chain:
            if frame not in seen:
                log(f"  -> {frame}")
                seen.add(frame)

    log("\n================================================================================")
    log("                       MÓDULOS CARGADOS EN LA VITA                              ")
    log("================================================================================")
    for mod in core.modules:
        segs_info = ", ".join([f"Seg{s.num}: 0x{s.start:x} (size: 0x{s.size:x})" for s in mod.segments])
        log(f" - {mod.name:<24} | {segs_info}")

    analysis_file = f"{dump_path}.analysis.txt"
    try:
        with open(analysis_file, "w", encoding="utf-8") as f:
            f.write("\n".join(report_lines))
        print(f"\n[+] Reporte de análisis guardado exitosamente en:\n    {analysis_file}")
    except Exception as e:
        print(f"[-] No se pudo guardar el archivo de reporte: {e}")

def main():
    parser = argparse.ArgumentParser(description="Analizador universal de crash dumps de PS Vita (.psp2dmp) para ports")
    parser.add_argument("dump", nargs="?", help="Ruta al archivo .psp2dmp / psp2core (si se omite, busca el más reciente en logs/)")
    parser.add_argument("elf", nargs="?", help="Ruta al archivo .elf compilado de Vita (auto-detectado si se omite)")
    parser.add_argument("so", nargs="?", help="Ruta al archivo .so dinámico de Android (auto-detectado si se omite)")
    parser.add_argument("--so-base", type=lambda x: int(x, 16), default=None, help="Dirección base asignada al .so en RAM (auto-detectada si se omite)")
    parser.add_argument("--stack-depth", type=int, default=36, help="Profundidad de palabras de pila a inspeccionar")

    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.abspath(os.path.join(script_dir, ".."))

    # Auto-find ELF and SO if not provided
    auto_elf, auto_so = auto_find_files(project_root)
    elf_path = args.elf or auto_elf
    so_path = args.so or auto_so

    dump_path = args.dump
    if not dump_path:
        logs_dir = os.path.join(project_root, "logs")
        dumps = glob.glob(os.path.join(logs_dir, "*.psp2dmp")) + glob.glob(os.path.join(logs_dir, "psp2core*"))
        if dumps:
            dumps.sort(key=os.path.getmtime, reverse=True)
            dump_path = dumps[0]
            print(f"[*] Archivo dump no especificado. Usando el más reciente: {dump_path}")
        else:
            print("[-] Error: No se especificó un dump y no se encontraron archivos .psp2dmp en logs/.")
            sys.exit(1)

    dump_path = dump_path if os.path.isabs(dump_path) else os.path.join(project_root, dump_path) if not os.path.exists(dump_path) else dump_path
    if elf_path and not os.path.isabs(elf_path):
        elf_path = os.path.join(project_root, elf_path)
    if so_path and not os.path.isabs(so_path):
        so_path = os.path.join(project_root, so_path)

    analyze_dump(dump_path, elf_path, so_path, args.so_base, args.stack_depth)

if __name__ == "__main__":
    main()
