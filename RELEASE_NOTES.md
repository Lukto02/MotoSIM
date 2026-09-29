## Cómo jugar
1. Bajá **MotoSim-v0.3.2.zip** (abajo, en *Assets*).
2. Descomprimí la carpeta entera. No lo abras desde adentro del zip.
3. Ejecutá **MotoSim.exe**. Si Windows dice "Windows protegió su PC": *Más información* → *Ejecutar de todas formas* (el juego no está firmado).

Requisitos: Windows 10 u 11 de 64 bits y una placa de video con OpenGL 3.3. No hace falta instalar nada. Adentro del zip, el `LEEME.txt` tiene los controles y cómo jugar en red. En Mac se compila desde el código (`./build.sh`).

**En red**: juega con la v0.2.5, la v0.2.6, la v0.2.7 y la v0.2.8 (protocolo 7, no cambió). Conviene que los dos tengan la última, porque las reglas de los choques las usa cada uno en su PC. El suelo que se hunde no viaja por red: en una partida en red los surcos son sólo huellas dibujadas.

## Novedades de la v0.3.2
Junta todo lo hecho desde la v0.2.8: si venís de la v0.2.8, esto es lo que hay de nuevo.
- **Suelo de tierra que se deforma de verdad**:
  - la moto hunde la tierra y deja la huella; andando normal, sin patinar, ya queda marcada (unos 5 cm en la tierra de la pista, unos 2 cm en el pasto), y patinando cavás un surco más hondo (hasta 24 cm) con bordes de tierra;
  - los surcos tienen volumen y colisión: de las ruedas saltan terrones que rebotan y quedan en el suelo, el pasto se dobla y se aplasta, y la tierra removida se ve más húmeda y oscura; el asfalto no se deforma;
  - **el surco no maneja la moto**: acelerás, frenás y doblás casi igual que sobre suelo parejo (en las pruebas, el radio de una curva al límite cambia menos del 1,5 % y no hay caídas); las claves `rut_ride*` de `tuning.ini` sirven para tocarlo;
  - se maneja en Ajustes → Juego y sonido → Terreno (deformación apagada, visual o física; blandura; profundidad máxima; restaurar suelo); F8 alterna física y visual.
- **Calidad gráfica en cuatro niveles** (Bajo, Medio, Alto y Ultra): el juego elige uno según tu placa de video al abrir y, si anda a los tirones, baja solo un escalón y te avisa. Cada nivel cambia las sombras, la distancia del pasto y de los objetos, el filtro de texturas y la resolución de la imagen.
- **Ajustes en tres páginas**: Gráficos, Imagen (estilo de color, brillo, niebla, viñeta, grano, aberración, motion blur) y Juego y sonido. Si tenías un `preferencias.ini` de una versión anterior, los gráficos vuelven a los recomendados para tu placa, lo demás (nombre, moto, volumen) queda, y el archivo viejo se guarda como `preferencias.ini.v1.bak`.
- **Sombras en dos capas**: nítidas cerca de la moto (se leen hasta los rayos de las ruedas con Alto y Ultra) y sombras de lo que no se mueve hasta unos 96 m (Alto) o 140 m (Ultra), sin las franjas negras cortadas.
- **Imagen más limpia**: los bordes salen sin escalones ni halos de color (antes, con el motion blur, quedaban halos magenta); estilo de color Natural, Vívido (viene así) o Suave; el suelo lejano se ve más nítido.
- **Pasto**: hasta 120 m de distancia (según el nivel), sin saltos al aparecer, sin matas lima en las lomas y sin pasto sobre el asfalto de los boxes. El polvo es redondo, sin cortes rectos contra el suelo.
- **Mapas más bonitos**: la favela más cálida y sin tono violeta, Los Médanos con arena dorada y más relieve, el Parque con la pared del fondo menos lavada, la niebla más pareja entre mapas. El autódromo anda más liviano porque no dibuja lo que no ves.
- **El piloto**: la espalda de la campera sin el aro en relieve y el traje mejor terminado.
- La física de la Motocross 450 es la misma de siempre (el bot da la vuelta en 1:08.73 y 1:08.42).
