#!/usr/bin/env python3
import os
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
LOCAL_DRAWABLE_DIR = "zenonia3/res/drawable"
LOCAL_HTML_DIR = "zenonia3/assets/html"
VITA_ZENONIA_DATA_DIR = "/ux0:/data/zenonia3"
# Mismo set de PNG "globales" (en ingles) que empaqueta CMakeLists.txt bajo
# drawable/ -- ver loader/androidui.c. Se listan a mano (no todo LOCAL_DRAWABLE_DIR)
# porque esa carpeta trae tambien variantes localizadas (_kr/_jp/_ch) que el
# port no usa.
DRAWABLE_ASSETS = [
    "ui_logo_gamevil.png",
    "ui_title_bg_nate.png",
    "ui_title_logo5.png",
    "ui_menu_back0.png",
    "ui_menu_back1.png",
    "ui_menu_newgame.png",
    "ui_menu_continue.png",
    "ui_menu_options.png",
    "ui_menu_help.png",
    "ui_menu_about.png",
    "ui_menu_community.png",
    "ui_about_bg.png",
    "ui_help_bg.png",
    "ui_menu_back.png",
    "reply_page_back_e.png",
    "button_write_01_global.png",
    "button_later_01_global.png",
]

def print_banner():
    print("====================================================")
    print("         PS VITA DEPLOYMENT & DEBUG TOOL            ")
    print("====================================================")

def disconnect_proton_vpn():
    print("[*] Intentando desconectar Proton VPN...")
    try:
        # Intentamos ejecutar 'protonvpn-cli disconnect'
        result = subprocess.run(["protonvpn-cli", "disconnect"], capture_output=True, text=True)
        if result.returncode == 0:
            print("[+] Proton VPN desconectado exitosamente.")
            return True
        else:
            # Si da error, quizás ya está desconectado
            if "not connected" in result.stderr.lower() or "not connected" in result.stdout.lower():
                print("[+] Proton VPN ya estaba desconectado.")
                return True
            print(f"[-] Error de Proton VPN: {result.stderr.strip() or result.stdout.strip()}")
    except FileNotFoundError:
        print("[!] 'protonvpn-cli' no está instalado en el PATH. Omitiendo desconexión automática.")
    except Exception as e:
        print(f"[-] Ocurrió un error inesperado al intentar desconectar VPN: {e}")
    
    print("[!] Por favor, asegúrate de apagar Proton VPN manualmente si tienes problemas de conexión local.")
    return False

def get_local_ip_for_route():
    import socket
    # Intentar obtener IPs locales mediante el hostname
    try:
        hostname = socket.gethostname()
        for ip in socket.gethostbyname_ex(hostname)[2]:
            if ip.startswith("192.168.3."):
                return ip
    except Exception:
        pass

    # Fallback: conectar socket UDP para forzar la selección de interfaz del OS
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
        ftp.login() # Inicio de sesión anónimo (por defecto en VitaShell)
        print("[+] Conexión FTP establecida.")
        return ftp
    except all_errors as e:
        print(f"[-] Error al conectar por FTP a la PS Vita: {e}")
        print("[!] Verifica que VitaShell esté abierto y el servidor FTP (SELECT) esté activo en la PS Vita.")
        return None


def create_directory_if_not_exists(ftp, path):
    try:
        ftp.cwd(path)
    except all_errors:
        print(f"[*] El directorio '{path}' no existe. Intentando crearlo...")
        try:
            # Dividir ruta e intentar crear los segmentos
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
                    pass # Puede que ya exista
            ftp.cwd(path)
            print(f"[+] Directorio '{path}' listo.")
        except all_errors as e:
            print(f"[-] No se pudo crear el directorio '{path}': {e}")

def upload_vpk():
    if not os.path.exists(LOCAL_VPK_PATH):
        print(f"[-] Error: No se encontró el archivo VPK local en '{LOCAL_VPK_PATH}'.")
        print("[!] Ejecuta './build.sh' primero para generar la compilación.")
        return

    # Desconectar VPN antes de transferir
    disconnect_proton_vpn()

    ftp = connect_ftp()
    if not ftp:
        return

    try:
        # Asegurar directorio de descargas
        create_directory_if_not_exists(ftp, VITA_DOWNLOADS_DIR)

        filename = os.path.basename(LOCAL_VPK_PATH)
        dest_file_path = f"{VITA_DOWNLOADS_DIR}/{filename}"

        print(f"[*] Subiendo {LOCAL_VPK_PATH} a {dest_file_path}...")
        
        with open(LOCAL_VPK_PATH, "rb") as f:
            # storbinary sobreescribe automáticamente en la mayoría de implementaciones FTP
            ftp.storbinary(f"STOR {dest_file_path}", f)
            
        print(f"[+] Transferencia exitosa! Instala el VPK en tu Vita desde '{VITA_DOWNLOADS_DIR.replace('/ux0:', 'ux0:')}/{filename}'")
    except all_errors as e:
        print(f"[-] Falló la transferencia del VPK: {e}")
    finally:
        try:
            ftp.quit()
        except:
            pass

def upload_external_assets():
    # loader/androidui.c y loader/htmlview.c leen estos PNG/HTML desde
    # ux0:data/zenonia3/ (ya no se empaquetan en el VPK, ver CMakeLists.txt) --
    # esto sube esos archivos ahi. Necesario para CUALQUIER build, no solo uno
    # "seguro": sin esto el logo/menu/ABOUT/HELP quedan en blanco/negro.
    # Los PNG se suben tal cual (sin conversion), decodificados en runtime con
    # stb_image.
    if not os.path.isdir(LOCAL_DRAWABLE_DIR):
        print(f"[-] No se encontró la carpeta local '{LOCAL_DRAWABLE_DIR}'.")
        print("[!] Reconstruila desde una copia legal del APK (ver README, 'Setup Instructions').")
        return

    png_files = [f for f in DRAWABLE_ASSETS if os.path.isfile(os.path.join(LOCAL_DRAWABLE_DIR, f))]
    missing = [f for f in DRAWABLE_ASSETS if f not in png_files]
    if missing:
        print(f"[!] Faltan {len(missing)} PNG en '{LOCAL_DRAWABLE_DIR}': {', '.join(missing)}")
    if not png_files:
        print(f"[-] No se encontró ningún PNG esperado en '{LOCAL_DRAWABLE_DIR}'.")
        return

    html_files = []
    if os.path.isdir(LOCAL_HTML_DIR):
        html_files = sorted(f for f in os.listdir(LOCAL_HTML_DIR) if f.endswith(".html"))

    disconnect_proton_vpn()

    ftp = connect_ftp()
    if not ftp:
        return

    try:
        remote_drawable_dir = f"{VITA_ZENONIA_DATA_DIR}/drawable"
        create_directory_if_not_exists(ftp, remote_drawable_dir)
        for name in png_files:
            local_path = os.path.join(LOCAL_DRAWABLE_DIR, name)
            dest_path = f"{remote_drawable_dir}/{name}"
            print(f"[*] Subiendo {local_path} a {dest_path}...")
            with open(local_path, "rb") as f:
                ftp.storbinary(f"STOR {dest_path}", f)
        print(f"[+] {len(png_files)} archivo(s) .png subidos a {remote_drawable_dir}")

        if html_files:
            remote_html_dir = f"{VITA_ZENONIA_DATA_DIR}/html"
            create_directory_if_not_exists(ftp, remote_html_dir)
            for name in html_files:
                local_path = os.path.join(LOCAL_HTML_DIR, name)
                dest_path = f"{remote_html_dir}/{name}"
                print(f"[*] Subiendo {local_path} a {dest_path}...")
                with open(local_path, "rb") as f:
                    ftp.storbinary(f"STOR {dest_path}", f)
            print(f"[+] {len(html_files)} archivo(s) .html subidos a {remote_html_dir}")

        print(f"[+] Listo. Con build/zenonia_3.vpk instalado, el UI se")
        print(f"    cargará desde '{VITA_ZENONIA_DATA_DIR}' automáticamente.")
    except all_errors as e:
        print(f"[-] Falló la subida de assets: {e}")
    finally:
        try:
            ftp.quit()
        except:
            pass

def download_latest_debug_files():
    # Desconectar VPN antes de transferir
    disconnect_proton_vpn()

    ftp = connect_ftp()
    if not ftp:
        return

    # 1. Obtener último dump de ux0:/data
    descargar_dmp = input("¿Deseas descargar el último crash dump (DMP)? (s/n): ").strip().lower()
    
    if descargar_dmp in ['s', 'y', 'si', 'yes']:
        print("[*] Buscando el último crash dump (.dmp / psp2core) en ux0:/data...")
        latest_dmp = None
        latest_dmp_time = 0
        try:
            ftp.cwd(VITA_DATA_DIR)
            # Usamos MLSD si está soportado para ver la fecha/hora de forma fiable, o LIST de lo contrario
            try:
                files = list(ftp.mlsd())
                for name, facts in files:
                    if name.startswith("psp2core") or name.endswith(".dmp"):
                        mtime = facts.get("modify", "0")
                        if mtime > str(latest_dmp_time):
                            latest_dmp_time = int(mtime) if mtime.isdigit() else mtime
                            latest_dmp = name
            except all_errors:
                # Fallback simple a LIST si MLSD falla
                file_list = []
                ftp.retrlines("LIST", file_list.append)
                # Para LIST, simplemente tomamos el último archivo que coincida en la lista (generalmente ordenados cronológica o alfabéticamente)
                for line in file_list:
                    parts = line.split()
                    if len(parts) >= 9:
                        name = " ".join(parts[8:])
                        if name.startswith("psp2core") or name.endswith(".dmp"):
                            latest_dmp = name
            
            if latest_dmp:
                local_dmp_name = f"{latest_dmp}"
                print(f"[+] Último dump detectado: '{latest_dmp}'. Descargando como '{local_dmp_name}'...")
                with open(local_dmp_name, "wb") as f:
                    ftp.retrbinary(f"RETR {latest_dmp}", f.write)
                print(f"[+] Descargado '{local_dmp_name}' en la carpeta actual.")
            else:
                print("[-] No se encontraron archivos de crash dump (*.dmp / psp2core*) en ux0:/data.")
        except all_errors as e:
            print(f"[-] Error al buscar o descargar dump: {e}")
    else:
        print("[*] Omitiendo la descarga del crash dump.")

    # 2. Obtener último log de ux0:/data/zenonia3/logs
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
            for line in file_list:
                parts = line.split()
                if len(parts) >= 9:
                    name = " ".join(parts[8:])
                    if name.endswith(".txt") or "log" in name.lower():
                        latest_log = name
                        
        if latest_log:
            local_log_name = f"{latest_log}"
            print(f"[+] Último log detectado: '{latest_log}'. Descargando como '{local_log_name}'...")
            with open(local_log_name, "wb") as f:
                ftp.retrbinary(f"RETR {latest_log}", f.write)
            print(f"[+] Descargado '{local_log_name}' en la carpeta actual.")
        else:
            print("[-] No se encontraron archivos de registro (.txt) en ux0:/data/zenonia3/logs.")
    except all_errors as e:
        print(f"[-] Error al buscar o descargar logs (asegúrate de que exista la ruta ux0:/data/zenonia3/logs): {e}")
    finally:
        try:
            ftp.quit()
        except:
            pass

def run_clean_macos():
    print("[*] Ejecutando clean_macos.sh...")
    script_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "clean_macos.sh")
    if os.path.exists(script_path):
        try:
            subprocess.run(["bash", script_path], check=True)
            print("[+] clean_macos.sh ejecutado exitosamente.")
        except subprocess.CalledProcessError as e:
            print(f"[-] Error al ejecutar clean_macos.sh: {e}")
        except Exception as e:
            print(f"[-] Ocurrió un error inesperado: {e}")
    else:
        print(f"[-] Error: No se encontró el script en '{script_path}'.")

def main():
    while True:
        print_banner()
        print("1. Subir VPK compilado a la PS Vita (ux0:downloads/)")
        print("2. Descargar el último dump (.dmp) y el último log (.txt) a la carpeta actual")
        print("3. Desconectar Proton VPN ahora mismo")
        print("4. Ejecutar clean_macos.sh (limpiar archivos ocultos de macOS)")
        print("5. Subir PNG/HTML (drawable) a ux0:data/zenonia3/ (necesario para el UI)")
        print("6. Salir")
        print("====================================================")
        try:
            opcion = input("Elige una opción (1-6): ").strip()
            print()
            if opcion == "1":
                upload_vpk()
            elif opcion == "2":
                download_latest_debug_files()
            elif opcion == "3":
                disconnect_proton_vpn()
            elif opcion == "4":
                run_clean_macos()
            elif opcion == "5":
                upload_external_assets()
            elif opcion == "6":
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
