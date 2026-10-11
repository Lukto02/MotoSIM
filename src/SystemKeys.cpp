// Atajos de accesibilidad de Windows mientras se juega (ver SystemKeys.h). Archivo aparte: <windows.h> choca con raylib.
#include "SystemKeys.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {

bool saved = false, off = false;
STICKYKEYS stickyStart = {sizeof(STICKYKEYS), 0};
TOGGLEKEYS toggleStart = {sizeof(TOGGLEKEYS), 0};
FILTERKEYS filterStart = {sizeof(FILTERKEYS), 0, 0, 0, 0, 0};

void Save()
{
    if (saved) return;
    SystemParametersInfo(SPI_GETSTICKYKEYS, sizeof(STICKYKEYS), &stickyStart, 0);
    SystemParametersInfo(SPI_GETTOGGLEKEYS, sizeof(TOGGLEKEYS), &toggleStart, 0);
    SystemParametersInfo(SPI_GETFILTERKEYS, sizeof(FILTERKEYS), &filterStart, 0);
    saved = true;
}

} // namespace

namespace SystemKeys {

void SetGameFocus(bool focused)
{
    if (focused == off) return;
    if (!focused) {
        Restore();
        return;
    }
    Save();
    // Sólo si la función está apagada se le saca el atajo (y su confirmación); si está prendida, el jugador la usa.
    STICKYKEYS sk = stickyStart;
    if ((sk.dwFlags & SKF_STICKYKEYSON) == 0) {
        sk.dwFlags &= ~(SKF_HOTKEYACTIVE | SKF_CONFIRMHOTKEY);
        SystemParametersInfo(SPI_SETSTICKYKEYS, sizeof(STICKYKEYS), &sk, 0);
    }
    TOGGLEKEYS tk = toggleStart;
    if ((tk.dwFlags & TKF_TOGGLEKEYSON) == 0) {
        tk.dwFlags &= ~(TKF_HOTKEYACTIVE | TKF_CONFIRMHOTKEY);
        SystemParametersInfo(SPI_SETTOGGLEKEYS, sizeof(TOGGLEKEYS), &tk, 0);
    }
    FILTERKEYS fk = filterStart;
    if ((fk.dwFlags & FKF_FILTERKEYSON) == 0) {
        fk.dwFlags &= ~(FKF_HOTKEYACTIVE | FKF_CONFIRMHOTKEY);
        SystemParametersInfo(SPI_SETFILTERKEYS, sizeof(FILTERKEYS), &fk, 0);
    }
    off = true;
}

void Restore()
{
    if (!off) return;
    SystemParametersInfo(SPI_SETSTICKYKEYS, sizeof(STICKYKEYS), &stickyStart, 0);
    SystemParametersInfo(SPI_SETTOGGLEKEYS, sizeof(TOGGLEKEYS), &toggleStart, 0);
    SystemParametersInfo(SPI_SETFILTERKEYS, sizeof(FILTERKEYS), &filterStart, 0);
    off = false;
}

} // namespace SystemKeys

#else

namespace SystemKeys {
void SetGameFocus(bool) {}
void Restore() {}
} // namespace SystemKeys

#endif
