#include "EngineSound.h"

#include "raylib.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr int kSampleRate = 44100;
constexpr float kTau = 6.28318530718f;

// Estado compartido con el hilo de audio. Voz 0 = la moto propia; 1..kVoices-1 = las de otros
// jugadores, con el volumen que les toca por la distancia.
struct VoiceParams {
    std::atomic<float> rpm{1800.0f}, throttle{0.0f}, gain{0.0f};
    std::atomic<bool> limiter{false}, shifting{false};
    std::atomic<float> cylinders{1.0f}, revLimit{10000.0f};
    std::atomic<bool> twoStroke{false};
};
VoiceParams gVoice[EngineSound::kVoices];
constexpr float kVolume = 0.8f;            // el volumen de siempre (Ajustes → Volumen al 100%)
std::atomic<float> gVolume{kVolume};
std::atomic<bool> gMuted{false};
std::atomic<int> gPopRequests{0};          // petardeos pedidos y todavía no empezados
std::atomic<float> gSkid{0.0f}, gSkidPaved{0.0f}, gScrape{0.0f};
std::atomic<float> gPopStrength{1.0f};

AudioStream gStream{};
bool gReady = false;

// Sintetizador de un motor (sólo lo toca el hilo de audio).
struct Synth {
    float phase = 0.0f;        // fase del ciclo de 4 tiempos (0..1)
    float env = 0.0f;          // envolvente del último pulso
    float sinceFire = 0.0f;    // s desde la última explosión
    float pulseAmp = 1.0f;
    float rpm = 1800.0f, throttle = 0.0f, gain = 0.0f;
    float lowpass = 0.0f, lowpass2 = 0.0f;
    float crank = 0.0f;
    unsigned noise = 0x9E3779B9u;
    float Noise()
    {
        noise ^= noise << 13;
        noise ^= noise >> 17;
        noise ^= noise << 5;
        return (float)(noise & 0xffff) / 32767.5f - 1.0f;
    }
    // Un sample (antes de la saturación).
    float Step(float targetRpm, float targetThrottle, bool limiter, float dt, float cylinders = 1.0f)
    {
        // Suavizado para que los cambios de rpm no "clickeen".
        rpm += (targetRpm - rpm) * 0.0015f;
        throttle += (targetThrottle - throttle) * 0.0008f;

        const float fireHz = rpm / 120.0f * cylinders; // una explosión cada 2 vueltas por cilindro
        phase += fireHz * dt;
        if (phase >= 1.0f) {
            phase -= 1.0f;
            // En el limitador (o sin gas a muchas rpm) algunas explosiones fallan.
            const bool misfire = (limiter && Noise() > -0.2f) || (throttle < 0.05f && rpm > 4000.0f && Noise() > 0.55f);
            pulseAmp = misfire ? 0.15f : 0.85f + 0.3f * (0.5f + 0.5f * Noise());
            env = 1.0f;
            sinceFire = 0.0f;
        }
        sinceFire += dt;

        // Pulso: decae en ~1/3 del ciclo a altas rpm, más largo en ralentí (golpe grave).
        const float decay = 1.0f / (0.004f + 0.25f / fireHz);
        env *= std::exp(-decay * dt);

        const float res = 95.0f + rpm * 0.017f;      // resonancia del escape
        const float tone = std::sin(kTau * res * sinceFire) * 0.65f + std::sin(kTau * res * 2.03f * sinceFire) * 0.25f +
                           std::sin(kTau * res * 3.1f * sinceFire) * 0.10f * throttle;
        const float bark = 0.35f + 0.65f * throttle;
        float sample = env * pulseAmp * (tone + Noise() * (0.12f + 0.3f * throttle)) * bark;

        // Zumbido mecánico del cigüeñal.
        crank += (rpm / 60.0f) * dt;
        if (crank > 1.0f) crank -= 1.0f;
        sample += std::sin(kTau * crank) * 0.04f;

        // Filtro pasa-bajos: se abre con el acelerador (más brillo al acelerar).
        const float cutoff = 0.10f + 0.25f * throttle + rpm * 0.00002f;
        lowpass += (sample - lowpass) * cutoff;
        lowpass2 += (lowpass - lowpass2) * cutoff;
        return lowpass2;
    }

    // Dos tiempos: explota en cada vuelta, con pulsos más cortos y ásperos (más armónicos y ruido: el
    // "riiing" en vez del golpe grave del cuatro tiempos). El caño de expansión se "enciende" en la banda
    // de potencia (desde ~60% del corte): más fuerte, más brillante, chillón; abajo queda apagado. Con poco
    // gas "cuatritiempea" (saltea explosiones al azar: el ring-ding-ding del ralentí y el borboteo al
    // cortar), y cada explosión hace sonar un poco el metal del cilindro (el "ding").
    float ring = 0.0f, ringPhase = 0.0f, pipe = 0.0f;
    float StepTwoStroke(float targetRpm, float targetThrottle, bool limiter, float dt, float cylinders, float revLimit)
    {
        rpm += (targetRpm - rpm) * 0.0015f;
        throttle += (targetThrottle - throttle) * 0.0012f;

        const float fireHz = rpm / 60.0f * cylinders;  // una explosión por vuelta por cilindro
        const float onPipe = std::clamp((rpm - 0.55f * revLimit) / (0.2f * revLimit), 0.0f, 1.0f);
        pipe += (onPipe * (0.3f + 0.7f * throttle) - pipe) * 0.0008f;
        phase += fireHz * dt;
        if (phase >= 1.0f) {
            phase -= 1.0f;
            const bool fourStroking = throttle < 0.25f && rpm < 0.6f * revLimit && Noise() > 0.25f + 1.6f * throttle;
            const bool misfire = (limiter && Noise() > -0.3f) || fourStroking;
            pulseAmp = misfire ? 0.08f : 0.8f + 0.35f * (0.5f + 0.5f * Noise());
            env = 1.0f;
            sinceFire = 0.0f;
            if (!misfire) ring = std::max(ring, 0.5f - 0.4f * pipe);
        }
        sinceFire += dt;

        // Pulso corto: decae en ~1/4 del ciclo.
        const float decay = 1.0f / (0.0015f + 0.2f / fireHz);
        env *= std::exp(-decay * dt);

        const float res = 160.0f + rpm * 0.026f;      // la boca del caño: más aguda que la de un 4T
        const float s1 = std::sin(kTau * res * sinceFire);
        const float buzz = s1 / (0.35f + std::fabs(s1));   // casi cuadrada: el "bzzz"
        const float tone = buzz * (0.45f + 0.25f * pipe) + std::sin(kTau * res * 2.02f * sinceFire) * 0.25f +
                           std::sin(kTau * res * 3.05f * sinceFire) * (0.12f + 0.25f * pipe);
        const float bark = 0.3f + 0.5f * throttle + 0.45f * pipe;
        float sample = env * pulseAmp * (tone + Noise() * (0.2f + 0.3f * throttle + 0.2f * pipe)) * bark;

        // El "ding" metálico de cada explosión (se oye sobre todo abajo, sin carga).
        ringPhase += 2350.0f * dt;
        if (ringPhase > 1.0f) ringPhase -= 1.0f;
        ring *= std::exp(-60.0f * dt);
        sample += std::sin(kTau * ringPhase) * ring * 0.08f;

        crank += (rpm / 60.0f) * dt;
        if (crank > 1.0f) crank -= 1.0f;
        sample += std::sin(kTau * crank) * 0.02f;

        const float cutoff = 0.14f + 0.2f * throttle + 0.18f * pipe + rpm * 0.000015f;
        lowpass += (sample - lowpass) * std::min(cutoff, 0.9f);
        lowpass2 += (lowpass - lowpass2) * std::min(cutoff, 0.9f);
        return lowpass2 * (0.85f + 0.3f * pipe);
    }
};
Synth gSynth[EngineSound::kVoices];

// Petardeo (explosión de nafta sin quemar en el escape): un chasquido seco, un golpe grave que baja
// de tono y una cola de ruido filtrado; todo pasa por un eco corto (el rebote en el terreno).
struct Backfires {
    struct Pop {
        bool active = false;
        float t = 0.0f, amp = 0.0f, thumpHz = 0.0f, phase = 0.0f;
        float lp1 = 0.0f, lp2 = 0.0f, lastNoise = 0.0f;
    };
    Pop pops[8];
    static constexpr int kEcho = 8192;
    float echo[kEcho] = {};
    int echoPos = 0;
    float echoLp = 0.0f;
    float duck = 0.0f;                         // cuánto se corre el motor (0..1) mientras suena un petardeo
    unsigned noise = 0x2545F491u;
    float Noise()
    {
        noise ^= noise << 13;
        noise ^= noise >> 17;
        noise ^= noise << 5;
        return (float)(noise & 0xffff) / 32767.5f - 1.0f;
    }
    void Start(float strength)
    {
        for (Pop& p : pops) {
            if (p.active) continue;
            p = Pop{};
            p.active = true;
            p.amp = strength * (0.8f + 0.25f * Noise());
            p.thumpHz = 95.0f + 45.0f * Noise();       // cada una suena un poco distinta
            return;
        }
    }
    float Sample(float dt)
    {
        float s = 0.0f;
        duck = 0.0f;
        for (Pop& p : pops) {
            if (!p.active) continue;
            p.t += dt;
            const float n = Noise();
            const float crack = (n - p.lastNoise) * std::exp(-p.t / 0.004f) * 0.8f;      // agudo, instantáneo
            p.lastNoise = n;
            p.phase += kTau * (48.0f + p.thumpHz * std::exp(-p.t / 0.02f)) * dt;
            const float thump = std::sin(p.phase) * std::exp(-p.t / 0.04f) * 1.2f;     // el "bum"
            p.lp1 += (n - p.lp1) * 0.18f;
            p.lp2 += (p.lp1 - p.lp2) * 0.18f;
            const float body = (p.lp1 - p.lp2) * std::exp(-p.t / 0.07f) * 3.0f;        // la cola ronca
            s += p.amp * (crack + thump + body);
            duck = std::max(duck, std::min(1.0f, p.amp) * std::exp(-p.t / 0.06f));
            if (p.t > 0.4f) p.active = false;
        }
        // Eco de ~85 ms que se apaga y pierde agudos en cada vuelta.
        const int delay = (int)(0.085f * kSampleRate);
        const float back = echo[(echoPos + kEcho - delay) % kEcho];
        echoLp += (back - echoLp) * 0.35f;
        echo[echoPos] = s + echoLp * 0.32f;
        echoPos = (echoPos + 1) % kEcho;
        return s + echoLp * 0.45f;
    }
} gBackfires;

// Cubierta que patina: en la tierra, un arrastre grave y granulado (ruido en banda baja con golpeteo
// de piedritas); en el pavimento, un chillido (ruido por un filtro resonante cerca de 1 kHz, con el
// tono que tiembla). Pasa por su propia envolvente para que no clickee.
struct Skid {
    float amount = 0.0f, paved = 0.0f;
    float lp1 = 0.0f, lp2 = 0.0f, grit = 0.0f;
    float low = 0.0f, band = 0.0f, wobble = 0.0f;
    unsigned noise = 0x68E31DA4u;
    float Noise()
    {
        noise ^= noise << 13;
        noise ^= noise >> 17;
        noise ^= noise << 5;
        return (float)(noise & 0xffff) / 32767.5f - 1.0f;
    }
    float Sample(float target, float targetPaved, float dt)
    {
        amount += (target - amount) * 0.0015f;
        paved += (targetPaved - paved) * 0.0005f;
        if (amount < 1e-4f) return 0.0f;
        const float n = Noise();
        lp1 += (n - lp1) * 0.05f;
        lp2 += (lp1 - lp2) * 0.05f;
        grit += ((Noise() > 0.985f ? Noise() : 0.0f) - grit) * 0.3f;          // piedritas
        const float dirt = (lp1 - lp2) * 5.0f + grit * 0.8f;
        wobble += dt * 7.0f;
        const float fc = 980.0f + 90.0f * std::sin(kTau * wobble) + 40.0f * Noise();
        const float f = 2.0f * std::sin(3.14159265f * fc / kSampleRate), q = 0.035f;   // resonante (chilla)
        const float high = n - low - q * band;
        band += f * high;
        low += f * band;
        const float squeal = band * 0.12f;
        return amount * (dirt * (1.0f - paved) + squeal * paved);
    }
} gSkidSynth;

// Raspado de metal y plástico contra algo duro: un rechinar de banda ancha (ruido por dos filtros de
// banda en serie, de ~0.9 a 2.2 kHz según qué tan rápido raspa), un rumor grave del golpeteo contra el
// piso, una aspereza irregular que lo modula (el grano del asfalto) y algún chasquido chico. Antes eran
// dos resonancias angostas y agudas (3 y 5 kHz, casi un silbido) y golpecitos fuertes: sonaba feo y
// tapaba el motor. Medido (tools/sonido.py y una copia en numpy): a fondo ~4.5 dB menos que antes, con
// casi toda la energía entre 0.7 y 6 kHz; despacio es mucho más suave.
struct Scrape {
    float amount = 0.0f;
    float low = 0.0f, band = 0.0f, low2 = 0.0f, band2 = 0.0f, rumbleLp1 = 0.0f, rumbleLp2 = 0.0f, gritLp = 0.0f, hissLp = 0.0f,
          crackle = 0.0f;
    unsigned noise = 0x2545F491u;
    float Noise()
    {
        noise ^= noise << 13;
        noise ^= noise >> 17;
        noise ^= noise << 5;
        return (float)(noise & 0xffff) / 32767.5f - 1.0f;
    }
    float Sample(float target)
    {
        amount += (target - amount) * 0.003f;
        if (amount < 1e-4f) return 0.0f;
        const float n = Noise();
        // Banda ancha: dos filtros de estado variable poco resonantes en serie (flancos más empinados).
        const float fc = 900.0f + 1300.0f * amount;
        const float f = 2.0f * std::sin(3.14159265f * fc / kSampleRate), q = 1.0f;
        const float high = n - low - q * band;
        band += f * high;
        low += f * band;
        const float high2 = band - low2 - q * band2;
        band2 += f * high2;
        low2 += f * band2;
        // Rumor grave y un poco de siseo arriba.
        rumbleLp1 += (n - rumbleLp1) * 0.03f;
        rumbleLp2 += (rumbleLp1 - rumbleLp2) * 0.03f;
        hissLp += (n - hissLp) * 0.5f;
        const float hiss = n - hissLp;
        // Aspereza: el volumen tiembla al azar unas decenas de veces por segundo.
        gritLp += (Noise() - gritLp) * 0.002f;
        const float grit = 0.65f + 0.35f * std::clamp(gritLp * 6.0f, -1.0f, 1.0f);
        if (Noise() > 1.0f - 0.004f * amount) crackle = Noise();    // chasquido de una piedrita
        crackle *= 0.9f;
        return amount * grit * (band2 * 1.2f + rumbleLp2 * 1.8f + hiss * 0.01f + crackle * 0.05f);
    }
} gScrapeSynth;

void SynthCallback(void* buffer, unsigned int frames)
{
    float* out = (float*)buffer;
    const float dt = 1.0f / kSampleRate;
    float rpm[EngineSound::kVoices], throttle[EngineSound::kVoices], gain[EngineSound::kVoices], cyl[EngineSound::kVoices];
    float revLimit[EngineSound::kVoices];
    bool limiter[EngineSound::kVoices], twoStroke[EngineSound::kVoices];
    for (int v = 0; v < EngineSound::kVoices; ++v) {
        rpm[v] = gVoice[v].rpm.load();
        throttle[v] = gVoice[v].shifting.load() ? 0.0f : gVoice[v].throttle.load();
        gain[v] = gVoice[v].gain.load();
        limiter[v] = gVoice[v].limiter.load();
        cyl[v] = gVoice[v].cylinders.load();
        revLimit[v] = gVoice[v].revLimit.load();
        twoStroke[v] = gVoice[v].twoStroke.load();
    }
    const float volume = gMuted.load() ? 0.0f : gVolume.load();
    const float skid = gSkid.load(), skidPaved = gSkidPaved.load(), scrape = gScrape.load();
    for (int n = gPopRequests.exchange(0); n > 0; --n) gBackfires.Start(gPopStrength.load());

    for (unsigned int i = 0; i < frames; ++i) {
        float engine = 0.0f;
        for (int v = 0; v < EngineSound::kVoices; ++v) {
            Synth& s = gSynth[v];
            s.gain += (gain[v] - s.gain) * 0.002f;                 // sin clicks al entrar o salir
            if (s.gain < 1e-4f && gain[v] <= 0.0f) continue;
            const float raw = twoStroke[v] ? s.StepTwoStroke(rpm[v], throttle[v], limiter[v], dt, cyl[v], revLimit[v])
                                           : s.Step(rpm[v], throttle[v], limiter[v], dt, cyl[v]);
            engine += std::tanh(raw * 2.2f) * s.gain;
        }
        // Mezcla: los motores quedan por debajo del tope y se corren un momento con cada petardeo;
        // los petardeos no pasan por el filtro del motor (perderían el chasquido) y saturan aparte:
        // suenan gordos, se destacan y no clipean.
        const float pops = gBackfires.Sample(dt);
        engine *= 0.55f * (1.0f - 0.45f * gBackfires.duck);
        const float tire = gSkidSynth.Sample(skid, skidPaved, dt) * 0.6f + gScrapeSynth.Sample(scrape) * 0.45f;
        out[i] = std::tanh(engine + tire + std::tanh(pops * 1.8f) * 0.95f) * volume;
    }
}

} // namespace

bool EngineSound::Init()
{
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return false;
    SetAudioStreamBufferSizeDefault(1024);
    gStream = LoadAudioStream(kSampleRate, 32, 1);
    SetAudioStreamCallback(gStream, SynthCallback);
    PlayAudioStream(gStream);
    gReady = true;
    return true;
}

void EngineSound::Shutdown()
{
    if (!gReady) return;
    StopAudioStream(gStream);
    UnloadAudioStream(gStream);
    CloseAudioDevice();
    gReady = false;
}

void EngineSound::Update(float rpm, float throttle, bool limiter, bool shifting, const Character& character)
{
    UpdateVoice(0, rpm, throttle, limiter, shifting, 1.0f, character);
}

void EngineSound::UpdateVoice(int voice, float rpm, float throttle, bool limiter, bool shifting, float gain, const Character& character)
{
    if (voice < 0 || voice >= kVoices) return;
    gVoice[voice].cylinders.store(std::max(1.0f, character.cylinders));
    gVoice[voice].twoStroke.store(character.twoStroke);
    gVoice[voice].revLimit.store(std::max(3000.0f, character.revLimit));
    gVoice[voice].rpm.store(rpm);
    gVoice[voice].throttle.store(throttle);
    gVoice[voice].limiter.store(limiter);
    gVoice[voice].shifting.store(shifting);
    gVoice[voice].gain.store(gain);
}

bool EngineSound::RenderTest(const char* path, const char* mode)
{
    // Guion: a fondo a 9000 rpm, cambio a 1 s (corte y petardeos), a fondo otra vez, y a 2 s se suelta
    // el gas arriba (dos petardeos más). "2t": lo mismo con el dos tiempos (hasta 11000 rpm) y 1.5 s más
    // de ralentí. "raspado": el motor bajo y la cola raspando despacio (0.5-1.5 s) y rápido (1.5-2.5 s).
    // Se escribe un WAV mono de 16 bits.
    const std::string m = mode ? mode : "";
    const bool two = m == "2t", scrape = m == "raspado";
    const float kSeconds = two ? 4.5f : 3.0f;
    constexpr int kChunk = 256;
    const float popAt[5] = {1.02f, 1.07f, 1.14f, 2.03f, 2.10f}, popStrength[5] = {0.9f, 0.65f, 0.8f, 0.5f, 0.4f};
    int nextPop = scrape ? 5 : 0;
    Character ch;
    ch.twoStroke = two;
    ch.revLimit = two ? 11500.0f : 10000.0f;
    const float top = two ? 11000.0f : 9000.0f;
    std::vector<short> pcm;
    std::vector<float> buffer(kChunk);
    for (int done = 0; done < (int)(kSeconds * kSampleRate); done += kChunk) {
        const float t = (float)done / kSampleRate;
        float rpm = t < 1.0f ? 5000.0f + (top - 5000.0f) * t : (t < 2.0f ? 6500.0f + (top - 6500.0f) * (t - 1.0f) : top - 3000.0f * (t - 2.0f));
        float thr = t < 2.0f ? 1.0f : 0.0f;
        if (two && t > 3.0f) {                       // ralentí: el ring-ding-ding
            rpm = 1900.0f;
            thr = 0.04f;
        }
        if (scrape) {
            rpm = 3000.0f;
            thr = 0.15f;
            SetScrape(t > 0.5f && t < 1.5f ? 0.35f : (t >= 1.5f && t < 2.5f ? 1.0f : 0.0f));
        }
        UpdateVoice(0, rpm, thr, false, !scrape && t >= 1.0f && t < 1.12f, 1.0f, ch);
        while (nextPop < 5 && popAt[nextPop] <= t) gBackfires.Start(popStrength[nextPop++]);
        SynthCallback(buffer.data(), kChunk);
        for (float v : buffer) pcm.push_back((short)(std::max(-1.0f, std::min(1.0f, v)) * 32767.0f));
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const unsigned dataBytes = (unsigned)(pcm.size() * 2), rate = kSampleRate, byteRate = kSampleRate * 2, riff = 36 + dataBytes, fmtLen = 16;
    const unsigned short pcmFormat = 1, channels = 1, align = 2, bits = 16;
    auto put = [&](const void* data, size_t bytes) { f.write((const char*)data, (std::streamsize)bytes); };
    put("RIFF", 4); put(&riff, 4); put("WAVEfmt ", 8); put(&fmtLen, 4); put(&pcmFormat, 2); put(&channels, 2);
    put(&rate, 4); put(&byteRate, 4); put(&align, 2); put(&bits, 2); put("data", 4); put(&dataBytes, 4);
    put(pcm.data(), pcm.size() * 2);
    return (bool)f;
}

void EngineSound::Backfire(float strength)
{
    gPopStrength.store(strength);
    gPopRequests.fetch_add(1);
}

void EngineSound::SetSkid(float amount, float paved)
{
    gSkid.store(amount);
    gSkidPaved.store(paved);
}

void EngineSound::SetScrape(float amount) { gScrape.store(amount); }

void EngineSound::ToggleMute() { gMuted.store(!gMuted.load()); }
bool EngineSound::Muted() const { return gMuted.load(); }
void EngineSound::SetMuted(bool muted) { gMuted.store(muted); }
void EngineSound::SetVolume(float level) { gVolume.store(kVolume * std::clamp(level, 0.0f, 1.0f)); }
