# Teclas de verdad para probar el menú y el HUD con ventana, sin tocar nada más.
#
# Manda WM_KEYDOWN / WM_KEYUP con PostMessage SOLO a la ventana del juego que corre desde la carpeta -Dir (otros
# agentes pueden tener el suyo abierto). No usar SendKeys ni keybd_event: van a la ventana que esté al frente
# (la del usuario) y Windows no deja pasar la del juego al frente desde un proceso de fondo.
#
# Uso (el juego ya abierto, p. ej. con --menu ajustes --screenshot 6 x.png --time 7, en otra terminal o con &):
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\teclas.ps1 -Dir build-msvc-interfaz -Keys "DOWN,LEFT,ENTER" [-Wait 3]
# -Wait: segundos antes de la primera tecla (que el juego termine de cargar). Cada tecla queda apretada 90 ms:
# si se aprieta y se suelta en el mismo cuadro, raylib no ve el IsKeyPressed.
param([string]$Keys, [string]$Dir = "build-msvc", [double]$Wait = 2.5)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class MotoSimKeys {
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
}
"@
# nombre = código virtual, scancode, extendida (GLFW saca la tecla del scancode del lParam)
$codes = @{
    UP = @(0x26, 0x48, 1); DOWN = @(0x28, 0x50, 1); LEFT = @(0x25, 0x4B, 1); RIGHT = @(0x27, 0x4D, 1)
    ENTER = @(0x0D, 0x1C, 0); ESC = @(0x1B, 0x01, 0); SPACE = @(0x20, 0x39, 0); BACKSPACE = @(0x08, 0x0E, 0)
    W = @(0x57, 0x11, 0); A = @(0x41, 0x1E, 0); S = @(0x53, 0x1F, 0); D = @(0x44, 0x20, 0); Q = @(0x51, 0x10, 0); E = @(0x45, 0x12, 0)
    R = @(0x52, 0x13, 0); T = @(0x54, 0x14, 0); H = @(0x48, 0x23, 0); M = @(0x4D, 0x32, 0); P = @(0x50, 0x19, 0); C = @(0x43, 0x2E, 0)
    F1 = @(0x70, 0x3B, 0); F2 = @(0x71, 0x3C, 0); F3 = @(0x72, 0x3D, 0); F4 = @(0x73, 0x3E, 0); F6 = @(0x75, 0x40, 0); F7 = @(0x76, 0x41, 0)
    D1 = @(0x31, 0x02, 0); D2 = @(0x32, 0x03, 0); D3 = @(0x33, 0x04, 0); D4 = @(0x34, 0x05, 0); D5 = @(0x35, 0x06, 0)
}
Start-Sleep -Milliseconds ([int]($Wait * 1000))
$p = Get-Process motocross, MotoSim -ErrorAction SilentlyContinue |
     Where-Object { $_.MainWindowHandle -ne 0 -and $_.Path -like "*\$Dir\*" } | Select-Object -First 1
if (-not $p) { "no hay un juego con ventana corriendo desde $Dir"; exit 1 }
$h = $p.MainWindowHandle
foreach ($k in $Keys.Split(',')) {
    $c = $codes[$k.Trim().ToUpper()]
    if (-not $c) { "tecla desconocida: $k"; continue }
    $down = 1 -bor ($c[1] -shl 16) -bor ($c[2] -shl 24)
    $up = $down -bor 0xC0000000
    [MotoSimKeys]::PostMessage($h, 0x100, [IntPtr]$c[0], [IntPtr][int64]$down) | Out-Null
    Start-Sleep -Milliseconds 90
    [MotoSimKeys]::PostMessage($h, 0x101, [IntPtr]$c[0], [IntPtr][int64]$up) | Out-Null
    Start-Sleep -Milliseconds 120
}
"enviadas a $($p.Path): $Keys"
