import os
import sys
if os.name != "nt":
    print("  sorry, this installer is windows only")
    sys.exit(1)
import time
import base64
import lzma
import hashlib
import threading
import traceback
import urllib.request
import ctypes
import win32api
import win32con
import win32file
import win32process
import win32security
import win32gui
import win32event
import wmi

def die(status):
    win32api.TerminateProcess(win32api.GetCurrentProcess(), status)
def die_after(status, sec):
    def die_fn():
        time.sleep(sec)
        die(status)
    threading.Thread(target=die_fn).start()

try:
    ctypes.windll.user32.SetProcessDPIAware()
except:
    pass


llusbdac_ver = "2.0"

if getattr(sys, "frozen", False):
    safeloader_dir = sys._MEIPASS
    llusbdac_dir = sys._MEIPASS
    panic2screen_dir = sys._MEIPASS
    os.chdir(os.path.dirname(sys.executable))
else:
    safeloader_dir = "safeloader"
    llusbdac_dir = "llusbdac"
    panic2screen_dir = "panic2screen"

def load_ko(filepath):
    class InstallerCorruptedError(Exception):
        pass
    with open(filepath, "rb") as f:
        ko = f.read()
    with open(os.path.splitext(filepath)[0] + ".sha256", "rb") as f:
        if hashlib.sha256(ko).hexdigest() != f.read().split()[0].decode():
            raise InstallerCorruptedError()
    return ko
safeloader_ko = load_ko(os.path.join(safeloader_dir, "safeloader.ko"))
llusbdac_ko = load_ko(os.path.join(llusbdac_dir, "llusbdac.ko"))
panic2screen_ko = load_ko(os.path.join(panic2screen_dir, "panic2screen.ko"))


firmware_ver = "v2.02"
package_exe = "NW-ZX300_V2_02.exe"
package_url = "http://walkman.update.sony.net/fw/pc/ZX300/NW-ZX300_V2_02.exe"
package_sha256 = "c8a533db5d3638407e93446f5702f2e1b641db05dd68790a76d2a05aa16a1c66"
firmware_patched_sha256 = "645bd7efce6835487787197c3def64ed5b1f73a667a35c732c63d89e734d4c10"
firmware_patches = [
#   These patches inject following commands to firmware update script:
#       sleep 3
#       mount -o remount,rw /contents
#       (cd /contents && busybox sha256sum -c LLUSBDAC.SUM && busybox sh LLUSBDAC.DAT && rm -f LLUSBDAC.SUM LLUSBDAC.DAT)
#       mount -o remount,ro /contents
#       sync
#       sync
#       sleep 3
#
#   You can use following steps to generate these blobs:
#       # upgtool is come from rockbox project
#       # see: https://www.rockbox.org/wiki/SonyNWUPGTool
#       ./upgtool -d -e -o UNPACK -m nw-zx300 NW_WM_FW.UPG
#       vim -b UNPACK0.bin  # do necessary modifications here
#       truncate -s 8K UNPACK0.bin
#       ./upgtool -d -c -m nw-zx300 MYFW.UPG $(ls UNPACK* | sort -V)
#       diff <(xxd -g1 NW_WM_FW.UPG) <(xxd -g1 MYFW.UPG)
#
    (0, base64.b64decode(b"78y4mUt4s/UA33XiWGc0ow==")),
    (0x1BB0, base64.b64decode(b"""
        VM1XFkDbOoTZSENbF5sYE2wRcY+Epn88/kJh3rJzb2GDKBR0dS17p/ouR3Pq58zWWUFdWYqgJNnP
        rsx/+mZ6yB1zfAjofALljxHdXSPrlPPMud4pnpZJe2BqyDCmmj5McojSuhvmJIa/BVmqIBf+F4sf
        DFNOQQHcusnqFsKwu31vdBHvOR4Iu0HfR4JE8UQFTdjyvCZGZc4KKrVERMzXhwonn4XHpRp2ZA2V
        t1HkzmBjvdkmcVzuJGGHWhi1zEXjkSP7G3S+VtWIWTXGpMG5L/IKRFjk4bhNHgVC0xuzulegzeHb
        tdJH5XgrNn6KvmckhP51KtZpA5BGtPKgi2sgYISJQPEQbZvDHwriCn3IbfUi67WH9Zv7LQGqtQ/s
        n/a9oPDNXLKTDusIw3uphQThPsHGmIw06GKrIhUYytEnOrCoHL9hr/a1ifiuCr+HrvROdQPY8iPT
        hNUY6/bzfxTqinmvTVZRMRjMaxX9HahANqIT5bdjl/NtDgK8TGFvd4VopAk3c4WAuZqPk6DS8IzP
        xRD17agdfU+a37MX3lc0yS/J5PXc38bchxxUHN74+tQRMj7wR2XqLTTkxXkp69/9r9GGOJ7OmAxU
        4UF+uyfF4NkAe3CYPPlJSldAMcK0cS2SR0vNvUF4tNylgKa1wlqQD3DPTvm9tjZCKUee5HLRGsiJ
        HSRtEIeEn5T9WIIKl7XKDRgLxDCj2N3BNICVZo9ngNgHv0Qj4r92dbl50R2iCnfmQUFP4vHXkAFj
        f77qBlSMuvygRCQLxBPhAiFukm2ZfFYkQTzENP32wPfeomFMUIW8ePoVuEUBWkxzjActHstYQGPa
        FWWjwpifCRcEpQgLtVmaTzm1Ulkgiv6db3wv4sPI29k3oBx/1zrpse1x8PDT/TZWUo6kk1ppPOGa
        JpuOpcIKv2MrU6Rb0QVFuZN7s9lFo3bUeSzeaa+/pXwzFLjlhNW1hs4ubbLyJawQF8Qm2SNzZuwg
        SAbAzXMEpXnTo1n/7IA5F6OQWtcuM/q+OEM7vtb0Vjqvr/YshqDVoVBwWDWWidCqExdG2HitzJCc
        SosWR5aqap7OkctRRpkWkKlNCfCsIrVCAt3jH/r+8xKNVPIO78NJ5OX9OFVawn3la62F4SE5TN8C
        YH9nWLT3zpGYJaW34sCVflybxZGNO2WZoGzkKG6NSh7dLewNjCF//n1h6TjQUA4tJZV+QEpDrvkN
        +ix6ftrh8PCQ0B5iPfKhR9t36wRDNt+WGSP9o5twlaJETvO0oS1YOg7EiBLAdn26duWZ5OxjJHhr
        IkDzVqNrdrfNIbvZDPS0ggOyAZlmI/jPMBzdh/XMhIMcb2glCjsHyPKk7mjCGHA8wZfrlybvsnQ/
        QhY/vyJ9UafoNMtglmQDD8w707qwS8TtziqwO1vJtIp4F3LOu/joARe7QwnpJPARg6WzuhDNffZL
        7NiAFRzpcctzV2mW8a6qJi97azls177qD8qel4QlPu+FrE7yFEY7wAtQ1zBy0YWDOz9fnxo38sjY
        IOSZ1L18LrDK9E5cKgi2OFIK4chvau5MQVrUCKSHg9Em3j+CB3ui0umDmtyymWHiVdIIFikGPSqJ
        2YmSjeqlvBXriwvBlT08QPSzXh8HRwUrHYeAnwr+c4jSaZeynBFhFuO/mYY2bUGDWJVtEQbCiPnQ
        3qjzjO3G2yGPdNHgRzl1Tqs+Jqa6a7bRbgTJ5cYXhYlbsZ/NXqEhG+QR""")),
]


lang = win32api.GetUserDefaultLangID()
if lang == 2052:
    S = {
        "DLGFONT": "宋体",

        "TITLE": "LLUSBDAC 安装工具",
        "OPT_TITLE": "将安装 SONY 固件 %s，请选择额外功能" % firmware_ver,
        "OPT_LLUSBDAC": "安装 LLUSBDAC v" + llusbdac_ver,
        "OPT_PANIC2SCREEN": "当发生内核错误时，蓝屏并显示错误信息",
        "OPT_ENABLEADB": "启用 ADB （安卓调试桥）",
        "OPT_INSTALLLOG": "保存安装日志",
        "WARNING": "注意",
        "WARNING_ADB": "启用 ADB 后，若要禁用 ADB，需要进行以下操作：\n（1）重新刷写未修改的固件；\n（2）初始化所有设置。\n\n确定要继续吗？",
        "WARNING_INSTALLLOG": "已选择“保存安装日志”。\n安装脚本的调试信息会保存至 WALKMAN 根目录下的 LLUSBDAC.LOG 文件中。",
        "START": "开始安装",
        "LEAVE": "取消安装",
        "MULTI_INST": "安装工具已经在运行中，请勿多开。",
        "INFORM_RISK": "安装 LLUSBDAC 需要修改播放器的固件。\n修改固件具有一定危险性，可能会导致失去保修，甚至损坏设备。\n本软件作者对任何损坏或损失不承担任何责任。\n\n是否继续？",

        "CANCEL": "取消",
        "ERROR": "错误",
        "EXCEPTION": "发生意外异常，无法继续安装。",

        "VERIFY_PACKAGE": "正在验证固件升级包",
        "NO_PACKAGE": "未找到固件升级包，\n",
        "BAD_PACKAGE": "固件升级包已损坏，\n",
        "ASK_DOWNLOAD": "要立即从 SONY 服务器上下载吗？\n\n注意：下载固件即表示您同意固件随附的条款与条件。",
        "DOWNLOAD_PROGRESS": "已下载 %.0f%%",
        "DOWNLOAD_ERROR": "下载过程中发生错误。",
        "ERR_PACKAGE": "固件升级包验证失败，无法继续安装。",

        "FIND_WALKMAN": "正在检测 WALKMAN 播放器",
        "NO_WALKMAN": "未检测到 WALKMAN 播放器，请现在插入设备",
        "WALKMAN_AT": "已检测到 WALKMAN 播放器位于 %s",

        "UPLOAD_SCRIPT": "正在向 %s 写入安装脚本",

        "RUN_LAUNCHER": "正在解压缩固件升级包\n过程中请勿进行手动操作",
        "NO_FIRMWARE": "找不到所需固件文件，无法继续安装。\n（提示：多开？）",
        "NO_DIALOG": "找不到固件升级窗口，无法继续安装。\n（提示：多开？）",

        "PATCH_FIRMWARE": "正在修改固件文件\n过程中请勿进行手动操作",
        "BAD_FIRMWARE": "固件文件修改失败，无法继续安装。",

        "INFORM_USER": "固件升级包修改完毕\n请在下方窗口手动操作，按正常步骤进行固件升级\n固件正常升级完毕后，安装即告成功"
    }
else:
    S = {
        "DLGFONT": "Tahoma",

        "TITLE": "LLUSBDAC Installer",
        "OPT_TITLE": "Install SONY firmware %s with extra features:" % firmware_ver,
        "OPT_LLUSBDAC": "Install LLUSBDAC v" + llusbdac_ver,
        "OPT_PANIC2SCREEN": "Show BSoD if player kernel panic",
        "OPT_ENABLEADB": "Enable ADB (Android Debug Bridge)",
        "OPT_INSTALLLOG": "Save installer log",
        "WARNING": "Warning",
        "WARNING_ADB": "After enabled ADB, if you want to disable ADB, you must:\n 1. Flash the unmodified firmware.\n 2. Reset all settings.\n\nConfirm?",
        "WARNING_INSTALLLOG": "'Save installer log' is selected.\nInstaller debug information will be saved to 'LLUSBDAC.LOG' at WALKMAN's root directory.",
        "START": "Start",
        "LEAVE": "Cancel",
        "MULTI_INST": "Installer is running. Please don't open multiple instances.",
        "INFORM_RISK": "In order to install LLUSBDAC, installer will modify the firmware of your player.\nThis may void warranty or damage your device.\nPlease use at your own risk.\n\nContinue?",

        "CANCEL": "Cancel",
        "ERROR": "Error",
        "EXCEPTION": "Fatal error:",

        "VERIFY_PACKAGE": "Verifying update package ...",
        "NO_PACKAGE": "Update package not found.\n",
        "BAD_PACKAGE": "Update package corrupted.\n",
        "ASK_DOWNLOAD": "Download it from SONY server now?\n\nNOTE: Downloading it means you agree its EULA.",
        "DOWNLOAD_PROGRESS": "Download progress %.0f%% ...",
        "DOWNLOAD_ERROR": "Download error:",
        "ERR_PACKAGE": "Can't verify update package, installation aborted.",

        "FIND_WALKMAN": "Detecting WALKMAN ...",
        "NO_WALKMAN": "Please connect your WALKMAN now ...",
        "WALKMAN_AT": "WALKMAN found at %s",

        "UPLOAD_SCRIPT": "Writing installation script to %s ...",

        "RUN_LAUNCHER": "Unpacking update package ...\nPlease do NOT do manual operations.",
        "NO_FIRMWARE": "Firmware not found, installation aborted.\n(Hint: Multiple instances opened?)",
        "NO_DIALOG": "Updater window not found, installation aborted.\n(Hint: Multiple instances opened?)",

        "PATCH_FIRMWARE": "Patching firmware ...\nPlease do NOT do manual operations.",
        "BAD_FIRMWARE": "Can't verify patched firmware, installation aborted.",

        "INFORM_USER": "Firmware patched successfully.\nPlease use the window below to proceed normal update process."
    }


def sha256sum(filepath, check_cancel_fn=None):
    try:
        with open(filepath, "rb") as f:
            h = hashlib.sha256()
            while buf := f.read(4096):
                if check_cancel_fn is not None:
                    check_cancel_fn()
                h.update(buf)
            return h.hexdigest()
    except OSError:
        return None

def download(url, filepath, check_cancel_fn, progress_fn):
    class DownloadCanceledException(Exception):
        @staticmethod
        def throw():
            raise DownloadCanceledException()
    tmp = os.path.splitext(filepath)[0] + ".tmp"
    def remove_tmp():
        try:
            os.remove(tmp)
        except:
            pass
    ts = time.time()
    def reporthook_fn(xferd, bs, count):
        nonlocal ts
        now_ts = time.time()
        if now_ts - ts > 0.5:
            progress_fn(xferd * bs / count * 100 if count >= 0 else 0)
            ts = now_ts
        check_cancel_fn(DownloadCanceledException.throw)
    try:
        progress_fn(0)
        urllib.request.urlretrieve(url, tmp, reporthook_fn)
        os.replace(tmp, filepath)
        return None
    except DownloadCanceledException:
        remove_tmp()
        die(1)
    except:
        remove_tmp()
        return str(sys.exc_info()[1])


c = wmi.WMI()


# allow only one instance
def single_instance():
    mutex = win32event.CreateMutex(None, True, "LLUSBDAC_INSTALLER_MUTEX")
    ret = win32event.WaitForSingleObject(mutex, 2000)
    if ret != win32event.WAIT_OBJECT_0 and ret != win32event.WAIT_ABANDONED:
        win32api.MessageBox(None, S["MULTI_INST"], S["TITLE"], win32con.MB_ICONERROR | win32con.MB_TOPMOST)
        die(1)
    mutex.Detach()
single_instance()


# inform user the risk
if win32api.MessageBox(None, S["INFORM_RISK"], S["TITLE"], win32con.MB_ICONWARNING | win32con.MB_YESNO | win32con.MB_TOPMOST) != win32con.IDYES:
    die(1)


# ask user for installer options
def ask_options():
    class InstallerOptionsDialog:
        className = "LLUSBDAC_INSTALLER_OPTIONS_DIALOG"
        def __init__(self):
            self.options = None
            wc = win32gui.WNDCLASS()
            wc.style = win32con.CS_VREDRAW | win32con.CS_HREDRAW
            wc.SetDialogProc()
            wc.cbWndExtra = win32con.DLGWINDOWEXTRA
            wc.hInstance = win32gui.dllhandle
            wc.hCursor = win32gui.LoadCursor(0, win32con.IDC_ARROW)
            wc.hbrBackground = win32con.COLOR_WINDOW + 1
            wc.lpszClassName = self.className
            win32gui.RegisterClass(wc)
        def DoModel(self):
            style = win32con.DS_SETFONT | win32con.DS_MODALFRAME | win32con.WS_POPUP | win32con.WS_SYSMENU | win32con.WS_VISIBLE | win32con.WS_CAPTION | win32con.CS_DBLCLKS
            s = win32con.WS_CHILD | win32con.WS_VISIBLE
            win32gui.DialogBoxIndirect(win32gui.dllhandle, [
                [S["TITLE"], (0, 0, 180, 115), style, win32con.WS_EX_TOPMOST, (12, S["DLGFONT"]), None, self.className],
                [128, S["START"], win32con.IDOK, (30, 91, 50, 15), s | win32con.WS_TABSTOP | win32con.BS_DEFPUSHBUTTON],
                [128, S["LEAVE"], win32con.IDCANCEL, (100, 91, 50, 15), s | win32con.WS_TABSTOP | win32con.BS_PUSHBUTTON],
                [130, S["OPT_TITLE"], -1, (10, 9, 170, 15), s | win32con.SS_LEFT],
                [128, S["OPT_LLUSBDAC"], 1000, (10, 24, 170, 15), s | win32con.WS_TABSTOP | win32con.BS_AUTOCHECKBOX ],
                [128, S["OPT_PANIC2SCREEN"], 1001, (10, 39, 170, 15), s | win32con.WS_TABSTOP | win32con.BS_AUTOCHECKBOX ],
                [128, S["OPT_ENABLEADB"], 1002, (10, 54, 170, 15), s | win32con.WS_TABSTOP | win32con.BS_AUTOCHECKBOX ],
                [128, S["OPT_INSTALLLOG"], 1003, (10, 69, 170, 15), s | win32con.WS_TABSTOP | win32con.BS_AUTOCHECKBOX ],
            ], None, {
                win32con.WM_COMMAND: self.OnCommand,
                win32con.WM_INITDIALOG: self.OnInitDialog,
            })
        def OnCommand(self, hwnd, msg, wparam, lparam):
            if wparam == win32con.IDCANCEL:
                win32gui.EndDialog(hwnd, 0)
            elif wparam == win32con.IDOK:
                cur_options = (
                    win32gui.SendMessage(win32gui.GetDlgItem(hwnd, 1000), win32con.BM_GETCHECK, 0, 0) == win32con.BST_CHECKED,
                    win32gui.SendMessage(win32gui.GetDlgItem(hwnd, 1001), win32con.BM_GETCHECK, 0, 0) == win32con.BST_CHECKED,
                    win32gui.SendMessage(win32gui.GetDlgItem(hwnd, 1002), win32con.BM_GETCHECK, 0, 0) == win32con.BST_CHECKED,
                    win32gui.SendMessage(win32gui.GetDlgItem(hwnd, 1003), win32con.BM_GETCHECK, 0, 0) == win32con.BST_CHECKED)
                if cur_options[2]:
                    if win32gui.MessageBox(hwnd, S["WARNING_ADB"], S["WARNING"], win32con.MB_ICONWARNING | win32con.MB_YESNO) == win32con.IDNO:
                        return
                if cur_options[3]:
                    win32gui.MessageBox(hwnd, S["WARNING_INSTALLLOG"], S["WARNING"], win32con.MB_ICONWARNING)
                self.options = cur_options
                win32gui.EndDialog(hwnd, 0)
        def OnInitDialog(self, hwnd, msg, wparam, lparam):
            l, t, r, b = win32gui.GetWindowRect(hwnd)
            pl, pt, pr, pb = win32gui.GetWindowRect(win32gui.GetDesktopWindow())
            xoff = ((pr - pl) - (r - l)) // 2
            yoff = ((pb - pt) - (b - t)) // 2
            win32gui.SetWindowPos(hwnd, win32con.HWND_TOPMOST, pl + xoff, pt + yoff, 0, 0, win32con.SWP_NOSIZE)
            win32gui.SendMessage(win32gui.GetDlgItem(hwnd, 1000), win32con.BM_SETCHECK, win32con.BST_CHECKED, 0)
            win32gui.SendMessage(win32gui.GetDlgItem(hwnd, 1001), win32con.BM_SETCHECK, win32con.BST_CHECKED, 0)
    dlg = InstallerOptionsDialog()
    dlg.DoModel()
    return dlg.options
options = ask_options()
if options is None:
    die(1)
opt_llusbdac, opt_panic2screen, opt_enableadb, opt_installlog = options


# start progress dialog
class ProgressManager:
    hwnd = None
    thread = None
    canceled = False
    callback_fn = None
    callback_ret = None
    on_timer = None
    @staticmethod
    def show():
        created = threading.Event()
        class InstallerProgressDialog:
            className = "LLUSBDAC_INSTALLER_PROGRESS_DIALOG"
            def __init__(self):
                self.options = None
                wc = win32gui.WNDCLASS()
                wc.style = win32con.CS_VREDRAW | win32con.CS_HREDRAW
                wc.SetDialogProc()
                wc.cbWndExtra = win32con.DLGWINDOWEXTRA
                wc.hInstance = win32gui.dllhandle
                wc.hCursor = win32gui.LoadCursor(0, win32con.IDC_ARROW)
                wc.hbrBackground = win32con.COLOR_WINDOW + 1
                wc.lpszClassName = self.className
                win32gui.RegisterClass(wc)
            def DoModel(self):
                style = win32con.DS_SETFONT | win32con.DS_MODALFRAME | win32con.WS_POPUP | win32con.WS_VISIBLE | win32con.WS_CAPTION
                s = win32con.WS_CHILD | win32con.WS_VISIBLE
                win32gui.DialogBoxIndirect(win32gui.dllhandle, [
                    [S["TITLE"], (0, 0, 200, 50), style, win32con.WS_EX_TOPMOST, (12, S["DLGFONT"]), None, self.className],
                    [128, S["CANCEL"], win32con.IDCANCEL, (75, 30, 50, 14), s | win32con.WS_TABSTOP | win32con.BS_DEFPUSHBUTTON],
                    [130, "", 1000, (0, 12, 200, 30), s | win32con.SS_CENTER | win32con.WS_CLIPSIBLINGS],
                ], None, {
                    win32con.WM_APP: self.OnApp,
                    win32con.WM_TIMER: self.OnTimer,
                    win32con.WM_COMMAND: self.OnCommand,
                    win32con.WM_DESTROY: self.OnDestroy,
                    win32con.WM_INITDIALOG: self.OnInitDialog,
                })
            def OnApp(self, hwnd, msg, wparam, lparam):
                ProgressManager.callback_ret = ProgressManager.callback_fn(hwnd)
                ProgressManager.callback_fn = None
            def OnTimer(self, hwnd, msg, wparam, lparam):
                if ProgressManager.on_timer is not None:
                    return ProgressManager.on_timer(hwnd, msg, wparam, lparam)
            def OnCommand(self, hwnd, msg, wparam, lparam):
                if wparam == win32con.IDCANCEL:
                    ProgressManager.canceled = True
                    win32gui.EnableWindow(win32gui.GetDlgItem(hwnd, win32con.IDCANCEL), False)
                    die_after(1, 5)
            def OnDestroy(self, hwnd, msg, wparam, lparam):
                ProgressManager.hwnd = None
            def OnInitDialog(self, hwnd, msg, wparam, lparam):
                ProgressManager.hwnd = hwnd
                l, t, r, b = win32gui.GetWindowRect(hwnd)
                pl, pt, pr, pb = win32gui.GetWindowRect(win32gui.GetDesktopWindow())
                xoff = ((pr - pl) - (r - l)) // 2
                yoff = ((pb - pt) - (b - t)) // 2
                win32gui.SetWindowPos(hwnd, win32con.HWND_TOPMOST, pl + xoff, pt + yoff, 0, 0, win32con.SWP_NOSIZE)
                created.set()
        def thread_fn():
            dlg = InstallerProgressDialog()
            dlg.DoModel()
        ProgressManager.thread = threading.Thread(target=thread_fn)
        ProgressManager.thread.start()
        created.wait()
    @staticmethod
    def invoke(fn):
        ProgressManager.callback_fn = fn
        ProgressManager.callback_ret = None
        win32gui.SendMessage(ProgressManager.hwnd, win32con.WM_APP, 0, 0)
        return ProgressManager.callback_ret
    @staticmethod
    def close():
        ProgressManager.invoke(lambda hwnd: win32gui.EndDialog(hwnd, 0))
        ProgressManager.thread.join()
    @staticmethod
    def check_cancel(cancel_action=None):
        if ProgressManager.canceled:
            ProgressManager.close()
            if cancel_action is not None:
                cancel_action()
            else:
                die(1)
    @staticmethod
    def no_cancel():
        def fn(hwnd):
            win32gui.EnableWindow(win32gui.GetDlgItem(hwnd, win32con.IDCANCEL), False)
            win32gui.ShowWindow(win32gui.GetDlgItem(hwnd, win32con.IDCANCEL), False)
        ProgressManager.invoke(fn)
        ProgressManager.check_cancel()
    @staticmethod
    def fail(err):
        def fn(hwnd):
            win32api.MessageBox(hwnd, err, S["ERROR"], win32con.MB_ICONERROR | win32con.MB_TOPMOST)
            win32gui.EndDialog(hwnd, 0)
        ProgressManager.invoke(fn)
        ProgressManager.thread.join()
        die(1)
    @staticmethod
    def progress(status):
        ProgressManager.invoke(lambda hwnd: win32gui.SetWindowText(win32gui.GetDlgItem(hwnd, 1000), status))
ProgressManager.show()

try:

    # fetch update package
    while True:
        ProgressManager.progress(S["VERIFY_PACKAGE"])
        if sha256sum(package_exe, ProgressManager.check_cancel) == package_sha256:
            break
        if os.path.exists(package_exe):
            ret = ProgressManager.invoke(lambda hwnd: win32api.MessageBox(hwnd, S["BAD_PACKAGE"] + S["ASK_DOWNLOAD"], S["TITLE"], win32con.MB_ICONWARNING | win32con.MB_OKCANCEL | win32con.MB_TOPMOST))
        else:
            ret = ProgressManager.invoke(lambda hwnd: win32api.MessageBox(hwnd, S["NO_PACKAGE"] + S["ASK_DOWNLOAD"], S["TITLE"], win32con.MB_ICONQUESTION | win32con.MB_OKCANCEL | win32con.MB_TOPMOST))
        if ret == win32con.IDCANCEL:
            ProgressManager.fail(S["ERR_PACKAGE"])
        err = download(package_url, package_exe, ProgressManager.check_cancel, lambda percent: ProgressManager.progress(S["DOWNLOAD_PROGRESS"] % percent))
        if err is not None:
            ProgressManager.invoke(lambda hwnd: win32api.MessageBox(hwnd, S["DOWNLOAD_ERROR"] + "\n" + err, S["ERROR"], win32con.MB_ICONERROR | win32con.MB_TOPMOST))


    # find walkman
    ProgressManager.progress(S["FIND_WALKMAN"])
    def find_walkman():
        try:
            drives = []
            for disk in c.Win32_DiskDrive(Model="SONY WALKMAN USB Device", SCSILogicalUnit=0):
                for partition in disk.associators("Win32_DiskDriveToDiskPartition"):
                    for drive in partition.associators("Win32_LogicalDiskToPartition"):
                        drives.append(drive.DeviceID)
            return drives[0] + "\\" if len(drives) == 1 else None
        except:
            return None
    last_ts = time.time()
    last_drive = None
    while True:
        now_ts = time.time()
        now_drive = find_walkman()
        if now_drive != last_drive:
            if now_drive is not None:
                ProgressManager.progress(S["WALKMAN_AT"] % now_drive)
            elif last_drive is not None:
                ProgressManager.progress(S["NO_WALKMAN"])
            last_ts = now_ts
            last_drive = now_drive
        if now_ts - last_ts > 2:
            if now_drive is not None:
                drive = now_drive
                break
            else:
                ProgressManager.progress(S["NO_WALKMAN"])
                last_ts = now_ts
        ProgressManager.check_cancel()
        time.sleep(0.1)
    ProgressManager.check_cancel()
    ProgressManager.no_cancel()


    # upload script
    ProgressManager.progress(S["UPLOAD_SCRIPT"] % drive)
    def upload_script(script):
        script_bytes = b"\n".join(script) + b"\n"
        script_file = os.path.join(drive, "LLUSBDAC.DAT")
        checksum_file = os.path.join(drive, "LLUSBDAC.SUM")
        if os.path.exists(script_file):
            win32file.SetFileAttributes(script_file, win32con.FILE_ATTRIBUTE_NORMAL)
            os.remove(script_file)
        if os.path.exists(checksum_file):
            win32file.SetFileAttributes(checksum_file, win32con.FILE_ATTRIBUTE_NORMAL)
            os.remove(checksum_file)
        with open(script_file, "wb") as f:
            f.write(script_bytes)
            f.flush()
            os.fsync(f.fileno())
        with open(checksum_file, "wb") as f:
            f.write((hashlib.sha256(script_bytes).hexdigest() + "  " + os.path.basename(script_file) + "\n").encode())
            f.flush()
            os.fsync(f.fileno())
    script = [
        b"# automatically generated shell script for LLUSBDAC installation",
        b"#   this script file (LLUSBDAC.DAT) and checksum file (LLUSBDAC.SUM)",
        b"#   can be deleted safely after installation completed",
        b""
    ]
    if opt_installlog:
        script.append(b"exec > /contents/LLUSBDAC.LOG; exec 2>&1; set -x;")
        script.append(b"busybox")
        script.append(b"busybox uname -a")
        script.append(b"mount")
        script.append(b"")
    def gen_file(file_bytes, target_path):
        script.append(b"busybox base64 -d << 'END_OF_BASE64' | busybox xz -d -c > '%s'" % target_path)
        blob = base64.b64encode(lzma.compress(file_bytes, preset=9|lzma.PRESET_EXTREME))
        for i in range(0, len(blob), 76):
            script.append(blob[i:i + 76])
        script.append(b"END_OF_BASE64")
        script.append(b"")
    script.append(b"mount -t ext4 -o ro /emmc@android /system")
    script.append(b"mount -t ext4 -o rw,remount /emmc@android /system")
    script.append(b"")
    if opt_panic2screen or opt_llusbdac:
        checksum = []
        cmd = []
        gen_file(safeloader_ko, b"/system/lib/modules/safeloader.ko")
        checksum.append(hashlib.sha256(safeloader_ko).hexdigest().encode() + b"  safeloader.ko")
        if opt_panic2screen:
            gen_file(panic2screen_ko, b"/system/lib/modules/panic2screen.ko")
            checksum.append(hashlib.sha256(panic2screen_ko).hexdigest().encode() + b"  panic2screen.ko")
            cmd.append(b"insmod /system/lib/modules/panic2screen.ko")
        if opt_llusbdac:
            gen_file(llusbdac_ko, b"/system/lib/modules/llusbdac.ko")
            checksum.append(hashlib.sha256(llusbdac_ko).hexdigest().encode() + b"  llusbdac.ko")
            cmd.append(b"insmod /system/lib/modules/llusbdac.ko")
        gen_file(b"\n".join(checksum) + b"\n", b"/system/lib/modules/llusbdac.sha256")
        script.append(b"cat << 'EOF' >> '/system/bin/load_sony_driver'")
        script.append(b"")
        script.append(b"(")
        script.append(b"  cd /system/lib/modules")
        script.append(b"  if busybox sha256sum -c llusbdac.sha256; then")
        script.append(b"    insmod safeloader.ko 'script=\"%s\"'" % b";".join(cmd))
        script.append(b"  fi")
        script.append(b")")
        script.append(b"")
        script.append(b"exit 0")
        script.append(b"EOF")
        script.append(b"")
    if opt_enableadb:
        script.append(b"cat << 'EOF' >> '/system/build.prop'")
        script.append(b"")
        script.append(b"persist.sys.sony.icx.adb=1")
        script.append(b"EOF")
        script.append(b"")
    script.append(b"mount -t ext4 -o ro,remount /emmc@android /system")
    script.append(b"")
    script.append(b"exit 0")
    upload_script(script)


    # run launcher
    ProgressManager.progress(S["RUN_LAUNCHER"])
    try:
        win32file.DeleteFile(os.path.abspath(package_exe) + ":Zone.Identifier")
    except:
        pass
    si = win32process.STARTUPINFO()
    si.dwFlags = win32con.STARTF_USESHOWWINDOW
    si.wShowWindow = win32con.SW_MINIMIZE
    hProcess, hThread, dwProcessId, dwThreadId = win32process.CreateProcess(package_exe, None, None, None, 0, 0, None, None, si)
    def launcher_alive():
        return win32process.GetExitCodeProcess(hProcess) == win32con.STILL_ACTIVE
    def find_updater():
        pids = { dwProcessId }
        while launcher_alive():
            try:
                for pid in pids.copy():
                    for process in c.Win32_Process(ParentProcessId=pid):
                        child_pid = process.ProcessId
                        child_name = process.Name
                        child_path = process.ExecutablePath
                        if child_pid is None or child_name is None or child_path is None:
                            continue
                        pids.add(child_pid)
                        if child_name.lower() == "SoftwareUpdateTool.exe".lower():
                            return (process, os.path.join(os.path.dirname(child_path), "Data", "Device", "NW_WM_FW.UPG"))
            except:
                pass
            time.sleep(0.01)
        return None, None
    fwupdater, fwpath = find_updater()  # need administrator
    if fwpath is None:
        ProgressManager.fail(S["NO_FIRMWARE"])
    def find_dialog():
        def enumfn(hwnd, results):
            try:
                if win32process.GetWindowThreadProcessId(hwnd)[1] == fwupdater.ProcessId:
                    if win32gui.GetClassName(hwnd) == "#32770":
                        wndtext = win32gui.GetWindowText(hwnd)
                        if wndtext == "Software update tool" or wndtext == "ソフトウェアアップデートツール":
                            results.append(hwnd)
            except:
                pass
        results = []
        while launcher_alive() and not results:
            try:
                win32gui.EnumWindows(enumfn, results)
            except:
                pass
            time.sleep(0.01)
        return results[0] if len(results) == 1 else None
    fwdialog = find_dialog()
    if fwdialog is None:
        ProgressManager.fail(S["NO_DIALOG"])
    win32gui.EnableWindow(fwdialog, False)
    win32gui.ShowWindow(fwdialog, win32con.SW_FORCEMINIMIZE)
    def kill_updater():
        try:
            fwupdater.Terminate()
        except:
            pass


    # patch firmware
    ProgressManager.progress(S["PATCH_FIRMWARE"])
    def do_patch():
        try:
            f = win32file.CreateFile(
                fwpath,
                win32con.GENERIC_READ | win32con.GENERIC_WRITE,
                0,
                None,
                win32con.OPEN_EXISTING,
                win32con.FILE_ATTRIBUTE_NORMAL,
                None)
            try:
                for offset, patch in firmware_patches:
                    win32file.SetFilePointer(f, offset, win32con.FILE_BEGIN)
                    win32file.WriteFile(f, patch)
            except:
                pass
            f.Close()
        except:
            pass
    def patch_firmware():
        try:
            sd = win32security.GetFileSecurity(fwpath, win32con.DACL_SECURITY_INFORMATION)
            dacl_old = sd.GetSecurityDescriptorDacl()
            dacl_new = sd.GetSecurityDescriptorDacl()
            while True:
                for i in range(dacl_new.GetAceCount()):
                    if dacl_new.GetAce(i)[0][0] == win32con.ACCESS_DENIED_ACE_TYPE:
                        dacl_new.DeleteAce(i)
                        break
                else:
                    break
            sd.SetSecurityDescriptorDacl(True, dacl_new, False)
            win32security.SetFileSecurity(fwpath, win32con.DACL_SECURITY_INFORMATION, sd)
            do_patch()
            sd.SetSecurityDescriptorDacl(True, dacl_old, False)
            win32security.SetFileSecurity(fwpath, win32con.DACL_SECURITY_INFORMATION, sd)
        except:
            do_patch()
    patch_firmware()
    if sha256sum(fwpath) != firmware_patched_sha256:
        kill_updater()
        ProgressManager.fail(S["BAD_FIRMWARE"])


    # show updater
    win32gui.EnableWindow(fwdialog, True)
    win32gui.ShowWindow(fwdialog, win32con.SW_RESTORE)
    win32gui.SetWindowPos(fwdialog, win32con.HWND_TOPMOST, 0, 0, 0, 0, win32con.SWP_NOMOVE | win32con.SWP_NOSIZE)
    win32gui.RedrawWindow(fwdialog, None, None, win32con.RDW_ERASE | win32con.RDW_FRAME | win32con.RDW_INTERNALPAINT | win32con.RDW_INVALIDATE | win32con.RDW_UPDATENOW | win32con.RDW_ALLCHILDREN)
    def update_fn(hwnd, flash_target=True):
        try:
            fwleft, fwtop, fwright, fwbottom = win32gui.GetWindowRect(fwdialog)
            left, top, right, bottom = win32gui.GetWindowRect(hwnd)
            left = fwleft + ((fwright - fwleft) - (right - left)) // 2
            top = fwtop - (bottom - top)
            win32gui.SetWindowPos(hwnd, 0, left, top, 0, 0, win32con.SWP_NOSIZE | win32con.SWP_NOZORDER)
            if win32gui.GetForegroundWindow() == hwnd:
                win32gui.SetForegroundWindow(fwdialog)
                if flash_target:
                    win32gui.FlashWindowEx(fwdialog, win32con.FLASHW_CAPTION | win32con.FLASHW_TIMER, 7, 70)
        except:
            pass
    ProgressManager.on_timer = lambda hwnd, msg, wparam, lparam: update_fn(hwnd)
    def fn(hwnd):
        win32gui.SetWindowText(win32gui.GetDlgItem(hwnd, 1000), S["INFORM_USER"])
        update_fn(hwnd, False)
        ctypes.windll.user32.SetTimer(hwnd, None, 100, None)
    ProgressManager.invoke(fn)
    win32event.WaitForSingleObject(hProcess, win32event.INFINITE)

except:
    ProgressManager.fail(S["EXCEPTION"] + "\n" + traceback.format_exc())
ProgressManager.close()
die(0)
