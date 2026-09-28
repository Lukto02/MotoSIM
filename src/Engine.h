#pragma once

// Motor + caja de 5 marchas + embrague automático. Nada de simular dientes ni embrague físico:
// el motor entrega torque según una curva RPM -> Nm, multiplicado por throttle y relación total.
struct EngineParams {
    float idleRPM = 1800.0f;
    float clutchRPM = 4200.0f;       // RPM a la que "patina" el embrague automático al arrancar
    float revLimit = 10000.0f;
    float torqueScale = 0.85f;       // multiplica toda la curva de torque
    float finalDrive = 7.5f;         // primaria * piñón/corona
    float gear[5] = {3.00f, 2.20f, 1.75f, 1.45f, 1.25f};
    float shiftTime = 0.12f;         // corte de torque durante el cambio
    float engineInertia = 0.005f;    // kg m^2 en el cigüeñal (se refleja en la rueda con ratio^2)
    float engineBrake = 8.0f;        // Nm de freno motor en el cigüeñal a revLimit
    float efficiency = 0.9f;
    // Marcha atrás (una moto de motocross no tiene: es una ayuda para salir de un pozo).
    float reverseTorque = 140.0f;    // Nm en la rueda con el gas a fondo
    float reverseWheelSpeed = 6.0f;  // rad/s de la rueda como tope (~7 km/h)
    // Otras motos: la curva de torque se estira en vueltas (2 = el mismo torque al doble de rpm, como un
    // motor de carreras que gira mucho más alto) y el sonido sabe cuántos cilindros explotan.
    float rpmScale = 1.0f;
    float cylinders = 1.0f;
    float twoStroke = 0.0f;          // 1 = motor de dos tiempos (sólo cambia el sonido)
};

class Engine {
public:
    EngineParams* p = nullptr;

    float rpm = 1800.0f;
    int gear = 1;                    // 1..5
    bool reverse = false;            // marcha atrás (desde primera, casi parado)
    float shiftTimer = 0.0f;
    bool limiter = false;
    bool clutchSlipping = true;
    bool autoShift = true;           // caja automática por defecto (F3 / X la alterna)
    float crankTorque = 0.0f;        // último torque en el cigüeñal (Nm)

    static float TorqueCurve(float rpm);   // Nm en el cigüeñal a throttle 1 (la curva base, de una 450)
    float Torque(float rpm) const { return TorqueCurve(rpm / p->rpmScale); }   // la de esta moto
    float Ratio(int g) const;
    float TotalRatio() const { return Ratio(gear); }

    // Devuelve el torque en la rueda trasera. outInertia = inercia del motor reflejada en la rueda
    // (0 si el embrague patina o está abierto).
    float Update(float wheelOmega, float throttle, float dt, float& outInertia);

    void ShiftUp();
    void ShiftDown(float wheelOmega);
    void Reset();
};
