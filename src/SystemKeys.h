#pragma once
// Atajos de accesibilidad de Windows mientras se juega. Shift es el botón de los trucos, y apretarlo cinco veces seguidas
// abre "Teclas especiales" (mantener el Shift derecho 8 s, "Teclas filtro"; Bloq Num 5 s, "Teclas de alternancia"): el
// diálogo saca al juego de la pantalla en medio de una vuelta. Como recomienda Microsoft para los juegos ("Disabling
// Shortcut Keys in Games"), mientras la ventana del juego tiene el foco se apagan esos atajos (sólo los atajos: si el
// jugador tiene alguna de esas funciones prendida, no se toca), y vuelven como estaban al perder el foco y al salir. El
// cambio es de la sesión (no se escribe en el perfil): si el juego se cierra mal, vuelve al reiniciar la sesión.
// En otros sistemas no hace nada. Ver docs/PILOTO.md, "Trucos en el aire".
namespace SystemKeys {
void SetGameFocus(bool focused);   // llamar cada cuadro con IsWindowFocused()
void Restore();                    // al salir
}
