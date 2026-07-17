#!/usr/bin/env python3
import os
import re
import sys
import subprocess
import time
from ftplib import FTP, all_errors

VITA_IP = "192.168.3.15"
VITA_PORT = 1337
LOCAL_VPK_PATH = "build/zenonia_3.vpk"
VITA_DOWNLOADS_DIR = "/ux0:/downloads"
VITA_DATA_DIR = "/ux0:/data"
VITA_LOGS_DIR = "/ux0:/data/zenonia3/logs"
VITA_CG_DIR = "ux0:/data/zenonia3/cg"
BASE_DIR = os.path.dirname(os.path.abspath(__file__))

def print_banner():
    print("====================================================")
    print("         PS VITA DEPLOYMENT & DEBUG TOOL            ")
    print("                    (Zenonia 3)                     ")
    print("====================================================")

def disconnect_proton_vpn():
    print("[*] Intentando desconectar Proton VPN...")
    try:
        result = subprocess.run(["protonvpn-cli", "disconnect"], capture_output=True, text=True)
        if result.returncode == 0:
            print("[+] Proton VPN desconectado exitosamente.")
            return True
        else:
            if "not connected" in result.stderr.lower() or "not connected" in result.stdout.lower():
                print("[+] Proton VPN ya estaba desconectado.")
                return True
            print(f"[-] Error de Proton VPN: {result.stderr.strip() or result.stdout.strip()}")
    except FileNotFoundError:
        print("[!] 'protonvpn-cli' no está instalado en el PATH.")
    except Exception as e:
        print(f"[-] Ocurrió un error inesperado al intentar desconectar VPN: {e}")
    return False

def get_local_ip_for_route():
    import socket
    try:
        hostname = socket.gethostname()
        for ip in socket.gethostbyname_ex(hostname)[2]:
            if ip.startswith("192.168.3."):
                return ip
    except Exception:
        pass
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect((VITA_IP, 9))
        ip = s.getsockname()[0]
        s.close()
        if ip.startswith("192.168.3."):
            return ip
    except Exception:
        pass
    return None

def connect_ftp():
    print(f"[*] Conectando a la PS Vita en {VITA_IP}:{VITA_PORT}...")
    local_ip = get_local_ip_for_route()
    source_addr = (local_ip, 0) if local_ip else None
    if local_ip:
        print(f"[*] Forzando ruta local mediante IP origen física: {local_ip} (Bypasseando VPN)")
    try:
        ftp = FTP()
        ftp.connect(VITA_IP, VITA_PORT, timeout=10, source_address=source_addr)
        ftp.login() 
        print("[+] Conexión FTP establecida.")
        return ftp
    except all_errors as e:
        print(f"[-] Error al conectar por FTP a la PS Vita: {e}")
        return None

def create_directory_if_not_exists(ftp, path):
    try:
        ftp.cwd(path)
    except all_errors:
        print(f"[*] El directorio '{path}' no existe. Intentando crearlo...")
        try:
            parts = [p for p in path.split('/') if p]
            current = ""
            for part in parts:
                if ":" in part:
                    current = part
                else:
                    current = f"{current}/{part}"
                try:
                    ftp.mkd(current)
                except all_errors:
                    pass 
            ftp.cwd(path)
            print(f"[+] Directorio '{path}' listo.")
        except all_errors as e:
            print(f"[-] No se pudo crear el directorio '{path}': {e}")

def upload_vpk():
    if not os.path.exists(LOCAL_VPK_PATH):
        print(f"[-] Error: No se encontró el archivo VPK local en '{LOCAL_VPK_PATH}'.")
        return

    disconnect_proton_vpn()
    ftp = connect_ftp()
    if not ftp:
        return

    try:
        create_directory_if_not_exists(ftp, VITA_DOWNLOADS_DIR)
        filename = os.path.basename(LOCAL_VPK_PATH)
        dest_file_path = f"{VITA_DOWNLOADS_DIR}/{filename}"

        print(f"[*] Subiendo {LOCAL_VPK_PATH} a {dest_file_path}...")
        
        with open(LOCAL_VPK_PATH, "rb") as f:
            ftp.storbinary(f"STOR {dest_file_path}", f)
            
        print(f"[+] Transferencia exitosa! Instala el VPK en tu Vita desde '{VITA_DOWNLOADS_DIR.replace('/ux0:', 'ux0:')}/{filename}'")
    except all_errors as e:
        print(f"[-] Falló la transferencia del VPK: {e}")
    finally:
        try:
            ftp.quit()
        except:
            pass

def download_latest_debug_files():
    disconnect_proton_vpn()
    ftp = connect_ftp()
    if not ftp:
        return

    descargar_dmp = input("¿Deseas descargar el último crash dump (DMP)? (s/n): ").strip().lower()
    
    if descargar_dmp in ['s', 'y', 'si', 'yes']:
        print("[*] Buscando el último crash dump (.dmp / psp2core) en ux0:/data...")
        latest_dmp = None
        latest_dmp_time = 0
        try:
            ftp.cwd(VITA_DATA_DIR)
            try:
                files = list(ftp.mlsd())
                for name, facts in files:
                    if (name.startswith("psp2core") or name.endswith(".dmp")) and not name.endswith(".tmp"):
                        mtime = facts.get("modify", "0")
                        if mtime > str(latest_dmp_time):
                            latest_dmp_time = int(mtime) if mtime.isdigit() else mtime
                            latest_dmp = name
            except all_errors:
                file_list = []
                ftp.retrlines("LIST", file_list.append)
                valid_files = []
                for line in file_list:
                    parts = line.split()
                    if len(parts) >= 9:
                        name = " ".join(parts[8:])
                        if (name.startswith("psp2core") or name.endswith(".dmp")) and not name.endswith(".tmp"):
                            valid_files.append(name)
                if valid_files:
                    valid_files.sort()
                    latest_dmp = valid_files[-1]
            
            if latest_dmp:
                project_root = os.path.dirname(BASE_DIR)
                logs_folder = os.path.join(project_root, "logs")
                if not os.path.exists(logs_folder):
                    os.makedirs(logs_folder)
                local_dmp_name = os.path.join(logs_folder, f"Zenonia3-{latest_dmp}")
                print(f"[+] Último dump detectado: '{latest_dmp}'. Descargando como '{local_dmp_name}'...")
                with open(local_dmp_name, "wb") as f:
                    ftp.retrbinary(f"RETR {latest_dmp}", f.write)
                print(f"[+] Descargado '{latest_dmp}' en la carpeta logs/.")
            else:
                print("[-] No se encontraron archivos de crash dump en ux0:/data.")
        except all_errors as e:
            print(f"[-] Error al buscar o descargar dump: {e}")
    else:
        print("[*] Omitiendo la descarga del crash dump.")

    print("[*] Buscando el último log de Zenonia 3 en ux0:/data/zenonia3/logs...")
    latest_log = None
    latest_log_time = 0
    try:
        ftp.cwd(VITA_LOGS_DIR)
        try:
            files = list(ftp.mlsd())
            for name, facts in files:
                if name.endswith(".txt") or "log" in name.lower():
                    mtime = facts.get("modify", "0")
                    if mtime > str(latest_log_time):
                        latest_log_time = int(mtime) if mtime.isdigit() else mtime
                        latest_log = name
        except all_errors:
            file_list = []
            ftp.retrlines("LIST", file_list.append)
            valid_logs = []
            for line in file_list:
                parts = line.split()
                if len(parts) >= 9:
                    name = " ".join(parts[8:])
                    if name.endswith(".txt") or "log" in name.lower():
                        valid_logs.append(name)
            if valid_logs:
                valid_logs.sort()
                latest_log = valid_logs[-1]
                        
        if latest_log:
            project_root = os.path.dirname(BASE_DIR)
            logs_folder = os.path.join(project_root, "logs")
            if not os.path.exists(logs_folder):
                os.makedirs(logs_folder)
            local_log_name = os.path.join(logs_folder, f"{latest_log}")
            print(f"[+] Último log detectado: '{latest_log}'. Descargando como '{local_log_name}'...")
            with open(local_log_name, "wb") as f:
                ftp.retrbinary(f"RETR {latest_log}", f.write)
            print(f"[+] Descargado '{latest_log}' en la carpeta logs/.")
        else:
            print("[-] No se encontraron archivos de registro (.txt) en ux0:/data/zenonia3/logs.")
    except all_errors as e:
        print(f"[-] Error al buscar o descargar logs: {e}")
    finally:
        try:
            ftp.quit()
        except:
            pass

def download_glsl_shaders():
    disconnect_proton_vpn()
    ftp = connect_ftp()
    if not ftp:
        return

    vita_glsl_dir = "ux0:/data/zenonia3/glsl"
    project_root = os.path.dirname(BASE_DIR)
    local_glsl_dir = os.path.join(project_root, "glsl_dump")
    if not os.path.exists(local_glsl_dir):
        os.makedirs(local_glsl_dir)

    print(f"[*] Buscando shaders en {vita_glsl_dir}...")
    try:
        ftp.cwd(vita_glsl_dir)
        files = []
        try:
            files_info = list(ftp.mlsd())
            files = [name for name, facts in files_info if name.endswith(".glsl")]
        except all_errors:
            file_list = []
            ftp.retrlines("LIST", file_list.append)
            for line in file_list:
                parts = line.split()
                if len(parts) >= 9:
                    name = " ".join(parts[8:])
                    if name.endswith(".glsl"):
                        files.append(name)
        
        if not files:
            print("[-] No se encontraron shaders en la carpeta.")
        else:
            print(f"[+] Encontrados {len(files)} shaders. Descargando...")
            for f in files:
                local_path = os.path.join(local_glsl_dir, f)
                with open(local_path, "wb") as local_f:
                    ftp.retrbinary(f"RETR {f}", local_f.write)
                print(f"  -> Descargado: {f}")
            print(f"[+] Todos los shaders descargados en {local_glsl_dir}")
    except all_errors as e:
        print(f"[-] Error al listar o descargar shaders: {e}")
    finally:
        try:
            ftp.quit()
        except:
            pass


def upload_cg_shaders():
    project_root = os.path.dirname(BASE_DIR)
    local_cg_dir = os.path.join(project_root, "assets", "cg")

    if not os.path.exists(local_cg_dir):
        print(f"[-] No se encontró la carpeta local '{local_cg_dir}'.")
        return

    cg_files = sorted(
        f for f in os.listdir(local_cg_dir)
        if f.endswith(".cg") and not f.startswith("._")
    )
    if not cg_files:
        print(f"[-] No hay archivos .cg en '{local_cg_dir}'.")
        return

    disconnect_proton_vpn()
    ftp = connect_ftp()
    if not ftp:
        return

    try:
        create_directory_if_not_exists(ftp, VITA_CG_DIR)
        print(f"[*] Subiendo {len(cg_files)} shader(s) .cg a {VITA_CG_DIR}...")
        for fname in cg_files:
            local_path = os.path.join(local_cg_dir, fname)
            dest_path = f"{VITA_CG_DIR}/{fname}"
            with open(local_path, "rb") as f:
                ftp.storbinary(f"STOR {dest_path}", f)
            print(f"  -> Subido: {fname}")
        print(f"[+] Todos los shaders .cg fueron subidos a {VITA_CG_DIR}.")
    except all_errors as e:
        print(f"[-] Falló la subida de shaders .cg: {e}")
    finally:
        try:
            ftp.quit()
        except:
            pass


def sync_shaders():
    """Descarga los .glsl volcados (no traducidos) y sube los .cg ya
    traducidos en un solo paso, reportando qué shaders siguen sin traducir."""
    print("[*] Paso 1/2: descargando shaders GLSL sin traducir desde la Vita...")
    download_glsl_shaders()

    project_root = os.path.dirname(BASE_DIR)
    local_glsl_dir = os.path.join(project_root, "glsl_dump")
    local_cg_dir = os.path.join(project_root, "assets", "cg")

    dumped = set()
    if os.path.exists(local_glsl_dir):
        dumped = {
            os.path.splitext(f)[0] for f in os.listdir(local_glsl_dir)
            if f.endswith(".glsl") and not f.startswith("._")
        }
    translated = set()
    if os.path.exists(local_cg_dir):
        translated = {
            os.path.splitext(f)[0] for f in os.listdir(local_cg_dir)
            if f.endswith(".cg") and not f.startswith("._")
        }

    missing = sorted(dumped - translated)
    if missing:
        print(f"[!] {len(missing)} shader(s) todavía SIN traducir a .cg "
              f"(pídele a Claude que los traduzca en assets/cg/ antes de continuar):")
        for h in missing:
            print(f"  - {h}.glsl")
    else:
        print("[+] Todos los shaders volcados ya tienen su .cg correspondiente.")

    print("[*] Paso 2/2: subiendo todos los .cg traducidos a la Vita...")
    upload_cg_shaders()


def check_libshacccg():
    """Chequea por FTP si libshacccg.suprx existe y su tamaño en las 2 rutas
    donde el juego/vitaGL lo buscan. Un tamaño de 0 o muy chico (esperado:
    ~1-2 MB) o directamente "no existe" en ambas indica una instalación
    corrupta/incompleta, que es exactamente el tipo de fallo genérico
    ("fatal internal error" en cualquier shader, incluso uno trivial) que
    estamos viendo en el log."""
    disconnect_proton_vpn()
    ftp = connect_ftp()
    if not ftp:
        return

    candidates = [
        "/ur0:/data/libshacccg.suprx",
        "/ur0:/data/external/libshacccg.suprx",
    ]

    try:
        ftp.voidcmd("TYPE I")  # binary mode -- SIZE is unreliable/rejected in ASCII mode
        for path in candidates:
            try:
                size = ftp.size(path)
                print(f"[+] {path}: existe, {size} bytes"
                      + ("  <-- sospechosamente chico/vacio!" if not size or size < 100000 else ""))
            except all_errors as e:
                print(f"[-] {path}: no encontrado ({e})")
    finally:
        try:
            ftp.quit()
        except:
            pass


VITA_GAME_DATA_DIR = "/ux0:/data/zenonia3/data"
LOCAL_DATA_REFERENCE_DIR = "zenonia3/assets"


def _local_file_count(path):
    total = 0
    for root, dirs, files in os.walk(path):
        total += sum(1 for f in files if not f.startswith("._") and f != ".DS_Store")
    return total


def _local_shallow_count(path):
    return sum(
        1 for name in os.listdir(path)
        if not name.startswith("._") and name != ".DS_Store"
    )


_DEVICE_MOUNT_RE = re.compile(r"^[A-Za-z0-9]+0:$")


def _ftp_list_entries(ftp, path):
    """Devuelve lista de (nombre, es_directorio) para 'path'. cwd() primero
    (falla con un error claro tipo 550 si no existe) y despues un LIST sin
    argumentos -- pasarle el path completo a LIST directamente confunde al
    ftpd de VitaShell (contesta "200 Okay" en vez de una lista real)."""
    ftp.cwd(path)  # raises all_errors if it doesn't exist -- let it propagate

    entries = []
    try:
        for name, facts in ftp.mlsd():
            if name in (".", ".."):
                continue
            entries.append((name, facts.get("type") == "dir"))
        return entries
    except all_errors:
        pass

    lines = []
    ftp.retrlines("LIST", lines.append)
    for line in lines:
        parts = line.split(None, 8)
        if len(parts) < 9:
            continue
        name = parts[8]
        if name in (".", ".."):
            continue
        entries.append((name, line.startswith("d")))
    return entries


def _ftp_shallow_count(ftp, path):
    """Cuenta cuántas entradas (archivos + carpetas) hay directamente
    adentro de 'path', SIN recursar a subcarpetas. Un conteo recursivo
    completo abre una conexión de datos por cada subcarpeta -- en algo como
    3d/ (miles de subcarpetas) eso agota las conexiones del ftpd de
    VitaShell y termina en "Connection refused" a mitad de camino. Un
    conteo superficial alcanza para detectar "esta carpeta esta vacia/no
    existe", que es lo que realmente estamos chequeando."""
    try:
        entries = _ftp_list_entries(ftp, path)
    except all_errors as e:
        print(f"  [-] No se pudo listar '{path}': {e}")
        return None  # None = no se pudo determinar (distinto de 0 = vacia)

    # VitaShell's ftpd falls back to listing the device root (ux0:, ur0:,
    # os0:, etc.) instead of erroring when asked for something that isn't
    # really a browsable directory -- treat that as "doesn't exist".
    if any(_DEVICE_MOUNT_RE.match(name) for name, _ in entries):
        print(f"  [-] '{path}' devolvió una lista de dispositivos (os0:/ur0:/...) "
              f"en vez de contenido real -- probablemente no existe.")
        return None

    return sum(
        1 for name, _ in entries
        if not name.startswith("._") and name != ".DS_Store"
    )


def verify_data_assets():
    """Compara, carpeta por carpeta, cuántas entradas de primer nivel hay en
    el volcado local de referencia (zenonia3/assets)
    contra lo que realmente está subido en ux0:data/zenonia3/data/ en
    la Vita. Es un chequeo SUPERFICIAL (no recursivo) a propósito: contar
    recursivamente todo adentro de carpetas con miles de subcarpetas (3d/)
    abre demasiadas conexiones de datos seguidas y el ftpd de VitaShell
    termina rechazando la conexión a mitad de camino. Alcanza para detectar
    "esta carpeta esta vacia o no existe", que es lo que importa acá.
    Reconecta por cada subcarpeta para no acumular ninguna conexión de datos
    a medio cerrar entre una y la siguiente."""
    project_root = os.path.dirname(BASE_DIR)
    local_data_dir = os.path.join(project_root, LOCAL_DATA_REFERENCE_DIR)

    if not os.path.exists(local_data_dir):
        print(f"[-] No se encontró la referencia local en '{local_data_dir}'.")
        return

    subfolders = sorted(
        d for d in os.listdir(local_data_dir)
        if os.path.isdir(os.path.join(local_data_dir, d))
    )
    if not subfolders:
        print(f"[-] '{local_data_dir}' no tiene subcarpetas.")
        return

    disconnect_proton_vpn()
    ftp = connect_ftp()
    if not ftp:
        return

    print(f"[*] Comparando {len(subfolders)} subcarpeta(s) de data/ "
          f"(local vs. {VITA_GAME_DATA_DIR} en la Vita, chequeo superficial)...\n")
    any_mismatch = False
    
    try:
        for sub in subfolders:
            local_count = _local_shallow_count(os.path.join(local_data_dir, sub))
            remote_count = _ftp_shallow_count(ftp, f"{VITA_GAME_DATA_DIR}/{sub}")

            if remote_count is None:
                status = "?"
                any_mismatch = True
            elif local_count == remote_count:
                status = "OK"
            else:
                status = "MISMATCH"
                any_mismatch = True
            print(f"  [{status}] {sub}/: local={local_count}  vita={remote_count}")
    finally:
        try:
            ftp.quit()
        except:
            pass

    print()
    if any_mismatch:
        print("[!] Alguna(s) carpeta(s) no coinciden -- probablemente "
              "quedaron a mitad de copiar. Volvé a subirlas por FTP.")
    else:
        print("[+] Todas las carpetas coinciden en cantidad de archivos.")


def run_script(folder, script_name, is_python=False):
    script_path = os.path.join(BASE_DIR, folder, script_name)
    if os.path.exists(script_path):
        print(f"[*] Ejecutando {script_name} desde {folder}...")
        try:
            cmd = [sys.executable if is_python else "bash", script_path]
            subprocess.run(cmd, check=True)
            print(f"[+] {script_name} ejecutado exitosamente.")
        except subprocess.CalledProcessError as e:
            print(f"[-] Error al ejecutar {script_name}: {e}")
        except Exception as e:
            print(f"[-] Ocurrió un error inesperado: {e}")
    else:
        print(f"[-] Error: No se encontró el script en '{script_path}'.")

def main():
    while True:
        print_banner()
        print("1. Subir VPK compilado a la PS Vita (ux0:downloads/)")
        print("2. Descargar el último dump (.dmp) y log (.txt) de Zenonia 3")
        print("3. Desconectar Proton VPN ahora mismo")
        print("4. Ejecutar clean_macos.sh (build/)")
        print("5. Ejecutar build_and_install.sh (build/)")
        print("6. Ejecutar deploy_and_launch_vita3k.sh (build/)")
        print("7. Ejecutar decompile_all.sh (build/)")
        print("8. Ejecutar run_tests.sh (tests/)")
        print("9. Ejecutar get_dump.sh (misc/)")
        print("10. Descargar Shaders GLSL dumpeados")
        print("11. Subir Shaders CG traducidos (assets/cg/ -> Vita)")
        print("12. Sincronizar Shaders (descargar GLSL + subir CG, todo en uno)")
        print("13. Chequear libshacccg.suprx (tamano/existencia por FTP)")
        print("14. Verificar data/ completa (conteo de archivos local vs Vita)")
        print("15. Salir")
        print("====================================================")
        try:
            opcion = input("Elige una opción (1-15): ").strip()
            print()
            if opcion == "1":
                upload_vpk()
            elif opcion == "2":
                download_latest_debug_files()
            elif opcion == "3":
                disconnect_proton_vpn()
            elif opcion == "4":
                run_script("build", "clean_macos.sh")
            elif opcion == "5":
                run_script("build", "build_and_install.sh")
            elif opcion == "6":
                run_script("build", "deploy_and_launch_vita3k.sh")
            elif opcion == "7":
                run_script("build", "decompile_all.sh")
            elif opcion == "8":
                run_script("tests", "run_tests.sh")
            elif opcion == "9":
                run_script("misc", "get_dump.sh")
            elif opcion == "10":
                download_glsl_shaders()
            elif opcion == "11":
                upload_cg_shaders()
            elif opcion == "12":
                sync_shaders()
            elif opcion == "13":
                check_libshacccg()
            elif opcion == "14":
                verify_data_assets()
            elif opcion == "15":
                print("¡Hasta luego!")
                break
            else:
                print("[-] Opción no válida. Inténtalo de nuevo.")
        except KeyboardInterrupt:
            print("\n¡Hasta luego!")
            break
        print("\nPresiona ENTER para volver al menú principal...")
        input()

if __name__ == "__main__":
    main()
