# Validacion de v0.3.4

- Build Windows Release, MSVC, runtime estatico, raylib 5.5 y Jolt 5.6.0.
- 13 casos en plano conservan la telemetria original de la primera 0.3.1: idle, accel, wheelie, turn, sharpturn, brake, braketurn, reverse, whip, whiphold, flip, frontflip y crashloop. La pista ya no se compara bit a bit porque cambiaron sus peraltes.
- Bot en la pista inicial: dos vueltas en 150 segundos (1:08.45 y 1:08.00). Peralte exterior maximo configurado: 2.8 m frente a 1.9 m; ganancia 52 frente a 34. Se mantienen los filtros y el perfil transversal suave. Los otros mapas conservan sus valores.
- Capturas de 450 y 600 con el personaje original. La 600 es turquesa y reutiliza el chasis y la suspension de la 450.
- Wheelie de 8 segundos de ambas motos. La 600 conserva el perfil de 58 hp, 110 kg y los ajustes de asistencia documentados en FISICA.md.
- Conexion local real: anfitrion 450 / cliente 600 y viceversa, 28 y 20 segundos. Mas de 1000 estados recibidos por extremo; estilos mx y mx600 reconocidos. Sin errores ni desconexion inesperada.
- Pruebas de choques con 75 ms de demora y 25 ms de jitter por extremo. Ver registro adjunto para resultados.
- Revision del serializador: Reader controla el largo y ReceiveState descarta paquetes incompletos; IDs remotos y origen de estados se validan. Se agrega rechazo de HELLO incompleto antes de registrar o modificar jugadores.
- Correccion del port: crash_impact_speed=0 conserva el comportamiento contra obstaculos de la 0.3.1, pero las motos originales usan 7 m/s como umbral frontal online. Evita que la restauracion desactive las caidas frontales entre jugadores.
- Formato de paquetes sin cambios: protocolo 7. Usar v0.3.4 en ambos equipos para compartir el terreno y la moto nueva. Las pruebas son locales con latencia simulada; no verifican NAT ni conexion por internet entre redes distintas.
- Empaquetador actualizado al modelo original 2 y sus texturas; comprueba integridad ZIP y correspondencia con la carpeta.
