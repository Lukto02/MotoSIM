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
    // Cubiertas de la moto propia, 0..1 cada uno: dirt, el arrastre en la tierra (cuánto y qué tan rápido
    // desliza); squeal, el chillido deslizando en lo duro (trabada, patinando o de costado); chirp, frenando
    // cerca del límite en lo duro sin trabar (chirridos sueltos que se juntan al acercarse al bloqueo).
    static void SetSkid(float dirt, float squeal = 0.0f, float chirp = 0.0f);
    // Metal / plástico raspando algo duro (la cola en un wheelie pasado, sobre asfalto, cemento o un
    // objeto): amount 0..1; en la tierra no suena.
    static void SetScrape(float amount);
    // Prueba (--sound-test archivo.wav [2t|raspado|chillido]): unos segundos de motor con un cambio y
    // petardeos (el de siempre o el dos tiempos), del raspado o del chillido de la cubierta, en un WAV.
    static bool RenderTest(const char* path, const char* mode = "");
    // Grabación de una prueba sin ventana (--sound-log archivo.wav): después de cada paso de la física se
    // actualiza el estado (Update, SetSkid...) y CaptureStep genera esos segundos del mismo sintetizador.
    // Sin dispositivo de audio (con ventana el hilo de audio es el que llama al sintetizador).
    static void CaptureStep(float seconds);
    static bool SaveCapture(const char* path);
    void ToggleMute();
    bool Muted() const;
    void SetMuted(bool muted);
    // Volumen del menú (Ajustes): 0..1 del de siempre (1 = como sonó siempre; el WAV de prueba no lo usa).
    static void SetVolume(float level);
};
