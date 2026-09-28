#pragma once

// Sonido de motor procedural: cada explosión excita una resonancia de escape; el acelerador agrega
// "ladrido" (más brillo y ruido) y el limitador produce fallos de encendido. Un cuatro tiempos explota
// una vez cada dos vueltas por cilindro; un dos tiempos, en cada vuelta y con otro carácter (ver
// Synth::StepTwoStroke). Se genera en el hilo de audio de raylib.
// Cómo suena el motor de una moto (sale de su tuning). Va fuera de EngineSound porque clang no deja
// usar como argumento por defecto (`= {}`) un struct anidado con inicializadores de miembros dentro
// de la misma clase; se usa como EngineSound::Character.
struct EngineSoundCharacter {
    float cylinders = 1.0f;          // más cilindros = más explosiones por vuelta: grita en vez de ladrar
    bool twoStroke = false;
    float revLimit = 10000.0f;       // rpm de corte (la 2T se "pone en la pipa" arriba de ~60% de esto)
};

class EngineSound {
public:
    static constexpr int kVoices = 4;    // la moto propia + hasta 3 de otros jugadores
    using Character = EngineSoundCharacter;

    bool Init();
    void Shutdown();
    // Llamar una vez por frame con el estado del motor propio (voz 0).
    void Update(float rpm, float throttle, bool limiter, bool shifting, const Character& character = {});
    // Motor de otro jugador (voces 1..kVoices-1); gain según la distancia (0 = callado).
    static void UpdateVoice(int voice, float rpm, float throttle, bool limiter, bool shifting, float gain, const Character& character = {});
    void Backfire(float strength);   // un petardeo en el escape (0..1)
    // Cubierta que patina (la moto propia): amount 0..1 (cuánto y qué tan rápido desliza), paved 0..1
    // (en el pavimento chilla, en la tierra arrastra).
    static void SetSkid(float amount, float paved);
    // Metal / plástico raspando algo duro (la cola en un wheelie pasado, sobre asfalto, cemento o un
    // objeto): amount 0..1; en la tierra no suena.
    static void SetScrape(float amount);
    // Prueba (--sound-test archivo.wav [2t|raspado]): unos segundos de motor con un cambio y petardeos
    // (el de siempre o el dos tiempos) o del raspado solo, en un WAV.
    static bool RenderTest(const char* path, const char* mode = "");
    void ToggleMute();
    bool Muted() const;
};
