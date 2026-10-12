// Creación, reset, tuning y dibujo de la moto. La física está en BikePhysics.cpp.
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include "Bike.h"

#include "BikeStyleDef.h"

#include "MathUtil.h"
#include "PhysicsWorld.h"
#include "Rider.h"          // incluye Jolt: antes que raylib (Render.h)
#include "Render.h"
#include "Tuning.h"

#include <algorithm>
#include <cmath>
#include <string>

using JPH::Mat44;
using JPH::Quat;
using JPH::Vec3;

void BikeParams::Register(Tuning& t)
{
    t.Add("original_031", &original031);
    t.Add("mass", &mass);
    t.Add("inertia_pitch", &inertiaPitch);
    t.Add("inertia_yaw", &inertiaYaw);
    t.Add("inertia_roll", &inertiaRoll);
    t.Add("com_height", &comHeight);
    t.Add("com_forward", &comForward);
    t.Add("rider_shift", &riderShift);
    t.Add("rider_speed", &riderSpeed);
    t.Add("rider_pitch_damping", &riderPitchDamping);
    t.Add("rider_side_shift", &riderSideShift);
    t.Add("rider_side_steer", &riderSideSteer);

    t.Add("front_mount_y", &frontMountY);
    t.Add("front_mount_z", &frontMountZ);
    t.Add("rake_deg", &rakeDeg);
    t.Add("rear_mount_y", &rearMountY);
    t.Add("rear_mount_z", &rearMountZ);
    t.Add("rear_axis_deg", &rearAxisDeg);

    auto susp = [&](const std::string& p, SuspensionParams& s) {
        t.Add(p + "travel", &s.travel);
        t.Add(p + "spring", &s.spring);
        t.Add(p + "damping", &s.damping);
        t.Add(p + "rebound_ratio", &s.reboundRatio);
        t.Add(p + "bottoming_damping", &s.bottomingDamping);
        t.Add(p + "rebound_bottoming", &s.reboundBottoming);
        t.Add(p + "rebound_bottoming_from", &s.reboundBottomingFrom);
        t.Add(p + "progressivity", &s.progressivity);
        t.Add(p + "bump_stop", &s.bumpStop);
        t.Add(p + "bump_damping", &s.bumpDamping);
        t.Add(p + "max_damper_velocity", &s.maxDamperVelocity);
    };
    susp("front_", front);
    susp("rear_", rear);
    t.Add("hard_landing_spin", &hardLandingSpin);

    auto tire = [&](const std::string& p, TireParams& tp) {
        t.Add(p + "radius", &tp.radius);
        t.Add(p + "inertia", &tp.inertia);
        t.Add(p + "long_grip", &tp.longGrip);
        t.Add(p + "lat_grip", &tp.latGrip);
        t.Add(p + "roundness", &tp.roundness);
        t.Add(p + "stiffness", &tp.stiffness);
        t.Add(p + "damping", &tp.damping);
        t.Add(p + "unsprung_mass", &tp.unsprungMass);
        t.Add(p + "rebound_damping", &tp.reboundDamping);
        t.Add(p + "loose_grip", &tp.looseGrip);
        t.Add(p + "paved_grip", &tp.pavedGrip);
    };
    tire("front_tire_", frontTire);
    tire("rear_tire_", rearTire);

    t.Add("long_stiffness", &longStiffness);
    t.Add("lat_stiffness", &latStiffness);
    t.Add("cornering_stiffness", &corneringStiffness);
    t.Add("min_slip_speed", &minSlipSpeed);
    t.Add("tire_roll_coupling", &tireRollCoupling);

    t.Add("front_brake_torque", &frontBrakeTorque);
    t.Add("rear_brake_torque", &rearBrakeTorque);
    t.Add("abs_slip", &absSlip);
    t.Add("brake_stand_up", &brakeStandUp);
    t.Add("stoppie_yaw_damping", &stoppieYawDamping);
    t.Add("rolling_resistance", &rollingResistance);
    t.Add("drag_area", &dragArea);

    t.Add("max_steer_deg", &maxSteerDeg);
    t.Add("max_steer_high_speed_deg", &maxSteerHighSpeedDeg);
    t.Add("steer_rate", &steerRate);
    t.Add("caster_align", &casterAlign);
    t.Add("caster_deadzone_deg", &casterDeadzoneDeg);
    t.Add("slide_friction", &slideFriction);
    t.Add("slide_hold_bars", &slideHoldBars);
    t.Add("slide_speed_max", &slideSpeedMax);
    t.Add("slide_upright", &slideUpright);
    t.Add("slide_angle_min", &slideAngleMin);
    t.Add("slide_angle_max", &slideAngleMax);
    t.Add("slide_steer_in", &slideSteerIn);
    t.Add("slide_wander", &slideWander);
    t.Add("min_turn_radius", &minTurnRadius);
    t.Add("max_lateral_accel", &maxLateralAccel);
    t.Add("bank_turn_gain", &bankTurnGain);
    t.Add("lean_surface_grip", &leanSurfaceGrip);
    t.Add("max_lean_deg", &maxLeanDeg);
    t.Add("lean_steer_speed_low", &leanSteerSpeedLow);
    t.Add("lean_steer_speed_high", &leanSteerSpeedHigh);
    t.Add("leg_out_speed_min", &legOutSpeedMin);
    t.Add("leg_out_speed_max", &legOutSpeedMax);
    t.Add("leg_out_grip", &legOutGrip);
    t.Add("leg_out_com_forward", &legOutComForward);
    t.Add("balance_k_low", &balanceKLow);
    t.Add("balance_k_high", &balanceKHigh);
    t.Add("balance_d_low", &balanceDLow);
    t.Add("balance_d_high", &balanceDHigh);
    t.Add("balance_max_torque", &balanceMaxTorque);
    t.Add("balance_speed", &balanceSpeed);

    t.Add("air_pitch_torque", &airPitchTorque);
    t.Add("air_pitch_rate", &airPitchRate);
    t.Add("air_pitch_gain", &airPitchGain);
    t.Add("air_neutral_damping", &airNeutralDamping);
    t.Add("air_max_pitch_rate", &airMaxPitchRate);
    t.Add("air_pitch_rate_damping", &airPitchRateDamping);
    t.Add("air_roll_assist", &airRollAssist);
    t.Add("air_reaction_gain", &airReactionGain);
    t.Add("air_yaw_align", &airYawAlign);
    t.Add("air_yaw_damping", &airYawDamping);
    t.Add("air_body_tilt_deg", &airBodyTiltDeg);
    t.Add("air_body_inertia", &airBodyInertia);
    t.Add("air_body_rate", &airBodyRate);
    t.Add("air_whip_assist_fade", &airWhipAssistFade);
    t.Add("air_gyro_gain", &airGyroGain);
    t.Add("air_max_roll_rate", &airMaxRollRate);
    t.Add("air_roll_rate_damping", &airRollRateDamping);
    t.Add("wheelie_start_deg", &wheelieStartDeg);
    t.Add("wheelie_prediction", &wheeliePrediction);
    t.Add("wheelie_throttle_floor", &wheelieThrottleFloor);
    t.Add("wheelie_keep_throttle", &wheelieKeepThrottle);
    t.Add("wheelie_keep_from_deg", &wheelieKeepFromDeg);
    t.Add("wheelie_keep_full_deg", &wheelieKeepFullDeg);
    t.Add("wheelie_end_deg", &wheelieEndDeg);
    t.Add("wheelie_assist_torque", &wheelieAssistTorque);
    t.Add("wheelie_lean_back_deg", &wheelieLeanBackDeg);
    t.Add("wheelie_gear_fade", &wheelieGearFade);
    t.Add("wheelie_lean_torque", &wheelieLeanTorque);
    t.Add("grau_assist", &grauAssist);
    t.Add("grau_pitch_neutral_deg", &grauPitchNeutralDeg);
    t.Add("grau_speed_min", &grauSpeedMin);
    t.Add("grau_speed_max", &grauSpeedMax);
    t.Add("grau_hold_hz", &grauHoldHz);
    t.Add("grau_brake", &grauBrake);
    t.Add("grau_clutch_pop", &grauClutchPop);
    t.Add("grau_pop_time", &grauPopTime);
    t.Add("grau_turn", &grauTurn);
    t.Add("grau_align", &grauAlign);
    t.Add("wheelie_turn", &wheelieTurn);
    t.Add("wheelie_align", &wheelieAlign);
    t.Add("brake_align", &brakeAlign);
    t.Add("slide_pivot", &slidePivot);
    t.Add("brake_align_torque", &brakeAlignTorque);
    t.Add("brake_align_from", &brakeAlignFrom);
    t.Add("brake_align_full", &brakeAlignFull);
    t.Add("brake_align_free", &brakeAlignFree);
    t.Add("flat_side_exact", &flatSideExact);
    t.Add("brake_align_rear", &brakeAlignRear);
    t.Add("tire_track_exact", &tireTrackExact);
    t.Add("rear_lift_load", &rearLiftLoad);
    t.Add("rear_lift_mitigation", &rearLiftMitigation);
    t.Add("brake_yaw_comp", &brakeYawComp);
    t.Add("brake_transition_release", &brakeTransitionRelease);
    t.Add("lean_sweep_comp", &leanSweepComp);
    t.Add("handbrake_yaw", &handbrakeYaw);
    t.Add("handbrake_angle", &handbrakeAngle);
    t.Add("handbrake_angle_fast", &handbrakeAngleFast);
    t.Add("handbrake_catch", &handbrakeCatch);
    t.Add("handbrake_lean", &handbrakeLean);
    t.Add("handbrake_ramp", &handbrakeRamp);
    t.Add("tc_slip", &tcSlip);
    t.Add("crash_angle_deg", &crashAngleDeg);
    t.Add("crash_impact_speed", &crashImpactSpeed);
    t.Add("crash_impact_vertical", &crashImpactVertical);
    t.Add("lean_load", &leanLoad);
    t.Add("lean_load_world_deg", &leanLoadWorldDeg);
    t.Add("lean_load_flat", &leanLoadFlat);
    t.Add("balance_fast_k", &balanceFastK);
    t.Add("balance_fast_d", &balanceFastD);
    t.Add("balance_fast_speed", &balanceFastSpeed);
    t.Add("balance_fast_lead", &balanceFastLead);
    t.Add("whip_takeoff", &whipTakeoff);
    t.Add("whip_air", &whipAir);
    t.Add("whip_air_rate", &whipAirRate);
    t.Add("whip_air_torque", &whipAirTorque);
    t.Add("whip_air_gain", &whipAirGain);
    t.Add("whip_air_damping", &whipAirDamping);
    t.Add("whip_air_return", &whipAirReturn);
    t.Add("whip_air_return_torque", &whipAirReturnTorque);
    t.Add("whip_land_crash_deg", &whipLandCrashDeg);
    t.Add("object_wall_deg", &objectWallDeg);
    t.Add("trick_speed", &trickSpeed);
    t.Add("trick_reaction", &trickReaction);
    t.Add("trick_crash", &trickCrash);
    t.Add("trick_hit", &trickHit);

    t.Add("engine_idle_rpm", &engine.idleRPM);
    t.Add("engine_clutch_rpm", &engine.clutchRPM);
    t.Add("engine_rev_limit", &engine.revLimit);
    t.Add("engine_torque_scale", &engine.torqueScale);
    t.Add("engine_final_drive", &engine.finalDrive);
    for (int g = 0; g < 5; ++g) t.Add("engine_gear" + std::to_string(g + 1), &engine.gear[g]);
    t.Add("engine_shift_time", &engine.shiftTime);
    t.Add("engine_inertia", &engine.engineInertia);
    t.Add("engine_brake", &engine.engineBrake);
    t.Add("engine_efficiency", &engine.efficiency);
    t.Add("engine_reverse_torque", &engine.reverseTorque);
    t.Add("engine_reverse_speed", &engine.reverseWheelSpeed);
    t.Add("engine_rpm_scale", &engine.rpmScale);
    t.Add("engine_cylinders", &engine.cylinders);
    t.Add("engine_two_stroke", &engine.twoStroke);
}

float Wheel::Compression01() const
{
    return mu::Clamp((travel - extension) / travel, 0.0f, 1.0f);
}

Vec3 Wheel::AxleLocal() const
{
    return mountLocal + axisLocal * mu::Clamp(extension, 0.0f, travel);
}

void Bike::BuildShapes()
{
    // Colisión del chasis: sólo para choques/caídas. Las ruedas NO son cuerpos físicos. Hay una
    // versión sin el piloto para cuando sale despedido como ragdoll.
    auto chassis = [&](bool withRider) {
        JPH::StaticCompoundShapeSettings compound;
        compound.AddShape(Vec3(0.0f, -0.18f, 0.02f), Quat::sIdentity(), new JPH::BoxShape(Vec3(0.12f, 0.25f, 0.52f)));  // chasis/motor
        if (withRider)
            compound.AddShape(Vec3(0.0f, 0.45f, -0.10f), Quat::sIdentity(), new JPH::BoxShape(Vec3(0.17f, 0.30f, 0.17f))); // piloto
        compound.AddShape(wheels[FRONT].mountLocal, Quat::sIdentity(), new JPH::SphereShape(0.17f));                  // topes de rueda
        compound.AddShape(wheels[REAR].mountLocal, Quat::sIdentity(), new JPH::SphereShape(0.17f));
        // Cola con su protector de acero (el caño debajo de la patente, BikeMeshes): en un wheelie pasado
        // es lo que toca el piso, a ~83°, cuando el centro de masa todavía queda entre la cola y la rueda
        // (se queda apoyada raspando en vez de irse de espaldas). Raspa: poco roce y sin rebote
        // (PhysicsWorld), y saca chispas (Game). Es un bloque que ocupa guardabarros y parrilla, no más
        // fino que el chasis: Jolt usa la parte más fina de la forma para decidir cuándo barre los choques
        // a alta velocidad, y así eso no cambia.
        if (P->original031 < 0.5f) {
        JPH::BoxShape* tail = new JPH::BoxShape(Vec3(0.12f, 0.12f, 0.12f), 0.02f);
        tail->SetUserData(kTailSkidTag);
        compound.AddShape(Vec3(0.0f, 0.04f, -0.90f), Quat::sIdentity(), tail);
        }
        JPH::ShapeRefC shape = compound.Create().Get();
        // El centro de masa queda en el origen del espacio local de la moto (lo controlamos nosotros).
        return JPH::OffsetCenterOfMassShapeSettings(-shape->GetCenterOfMass(), shape).Create().Get();
    };
    shapeWithRider = chassis(true);
    shapeNoRider = chassis(false);
}

void Bike::InitVisual(BikeParams& params)
{
    P = &params;
    engine.p = &P->engine;
    UpdateWheelConfig();
    BuildShapes();
}

void Bike::SetVisualState(Vec3 position, Quat rotation, bool riderOn)
{
    prevPos = currPos = position;
    prevRot = currRot = rotation;
    riderOnBike = riderOn;
}

void Bike::Create(PhysicsWorld& world, BikeParams& params, Vec3 position, float yaw)
{
    P = &params;
    engine.p = &P->engine;
    UpdateWheelConfig();
    BuildShapes();

    JPH::BodyCreationSettings bcs(shapeWithRider, position, Quat::sRotation(Vec3::sAxisY(), yaw), JPH::EMotionType::Dynamic, Layers::MOVING);
    bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
    bcs.mMassPropertiesOverride.mMass = P->mass;
    bcs.mMassPropertiesOverride.mInertia = Mat44::sScale(Vec3(P->inertiaPitch, P->inertiaYaw, P->inertiaRoll));
    bcs.mAllowSleeping = false;
    bcs.mCollisionGroup = JPH::CollisionGroup(nullptr, kBikeCollisionGroup, 0);   // el ragdoll lo reconoce por esto
    bcs.mMotionQuality = JPH::EMotionQuality::LinearCast;
    bcs.mFriction = 0.6f;
    bcs.mRestitution = 0.05f;
    bcs.mLinearDamping = 0.0f;
    bcs.mAngularDamping = 0.05f;
    body = world.Bodies().CreateBody(bcs);
    world.Bodies().AddBody(body->GetID(), JPH::EActivation::Activate);

    Reset(world, position, yaw);
}

void Bike::ApplyMassProperties()
{
    // Sin el piloto (salió despedido) queda la moto sola: menos masa y bastante menos inercia.
    JPH::MassProperties mp;
    mp.mMass = riderOnBike ? P->mass : std::max(P->mass - kRiderMass, 0.3f * P->mass);
    mp.mInertia = Mat44::sScale(Vec3(P->inertiaPitch, P->inertiaYaw, P->inertiaRoll) * (riderOnBike ? 1.0f : 0.5f));
    body->GetMotionProperties()->SetMassProperties(JPH::EAllowedDOFs::All, mp);
}

void Bike::DetachRider(PhysicsWorld& world)
{
    if (!riderOnBike) return;
    riderOnBike = false;
    world.Bodies().SetShape(body->GetID(), shapeNoRider, false, JPH::EActivation::Activate);
    ApplyMassProperties();
}

void Bike::Reset(PhysicsWorld& world, Vec3 position, float yaw)
{
    if (!riderOnBike) {
        riderOnBike = true;
        world.Bodies().SetShape(body->GetID(), shapeWithRider, false, JPH::EActivation::Activate);
    }
    ApplyMassProperties();
    const Quat q = Quat::sRotation(Vec3::sAxisY(), yaw);
    world.Bodies().SetPositionRotationAndVelocity(body->GetID(), position, q, Vec3::sZero(), Vec3::sZero());
    for (Wheel& w : wheels) {
        w.susp = SuspensionState{};
        w.extension = w.travel;
        w.axialVel = 0.0f;
        w.tireDelta = 0.0f;
        w.groundExtension = 1e6f;
        w.grounded = w.locked = false;
        w.omega = 0.0f;
        w.slipRatio = w.slipAngle = w.gripUsage = 0.0f;
        w.normalForce = w.longForce = w.latForce = 0.0f;
        w.gripFactor = 1.0f;
        w.suspForce = w.tireForce = Vec3::sZero();
    }
    engine.Reset();
    crashed = false;
    crashedTime = crashTimer = tailStill = 0.0f;
    airTime = 0.0f;
    steerAngle = leanTarget = riderLean = legOut = bodyTilt = bodyTiltRate = riderSide = 0.0f;
    steerBase = steerCaster = 0.0f;
    handbrake = handbrakeTorque = hbSlide = hbRate = hbWant = hbCatch = hbLean = hbCatching = hbUp = hbIn = hbW = hbAim = hbCatchWant = 0.0f;
    hbValid = false;
    leanGrip = 1.0f;
    loadNormal = Vec3::sAxisY();
    loadAccel = 9.81f;
    leanSurface = leanTargetPrev = leanTargetRate = 0.0f;
    leanTargetValid = false;
    balanceSettle = 0.0f;
    whipIntent = whipPushTime = whipYaw = whipPeakYaw = whipPeakRoll = whipLaunch = 0.0f;
    whipCrossedLanding = false;
    whipArmed = true;
    whipRiderFlight = false;
    whipReturn = 0.0f;
    trick = Trick::None;
    trickAmt = trickRate = trickHitAmount = 0.0f;
    trickLanded = -1.0f;
    trickTorque = trickCom = Vec3::sZero();
    trickScale = Mat44::sIdentity();
    reverseHold = 0.0f;
    ++resetCount;
    trackExactW = 0.0f;
    steerShown = steerShownRate = 0.0f;
    steerShownTime = -1.0;
    prevPos = currPos = position;
    prevRot = currRot = q;
}

void Bike::UpdateSteerShown(double time)
{
    constexpr float kSteerShownW = 40.0f;   // rad/s: atrás de la física como mucho ~5° (el bot de la Trial derrapando)
    const double dt = time - steerShownTime;
    if (!steerShownOn || steerShownTime < 0.0 || dt < 0.0 || dt > 0.5) {   // arranca, o el tiempo saltó: el de la física
        steerShown = steerAngle;
        steerShownRate = 0.0f;
    } else {
        const int n = std::max(1, (int)std::ceil(dt / (1.0 / 240.0)));          // en pasos chicos: estable con cualquier cuadro
        const float h = (float)(dt / n);
        for (int k = 0; k < n; ++k) {
            steerShownRate += (kSteerShownW * kSteerShownW * (steerAngle - steerShown) - 2.0f * kSteerShownW * steerShownRate) * h;
            steerShown += steerShownRate * h;
        }
    }
    steerShownTime = time;
    steerShownOn = true;
}

void Bike::SetVelocity(PhysicsWorld& world, Vec3 velocity)
{
    world.Bodies().SetLinearVelocity(body->GetID(), velocity);
}

void Bike::SetAngularVelocity(PhysicsWorld& world, Vec3 angularVelocity)
{
    world.Bodies().SetAngularVelocity(body->GetID(), angularVelocity);
}

Vec3 Bike::RenderPosition(float alpha) const { return prevPos + (currPos - prevPos) * alpha; }
Quat Bike::RenderRotation(float alpha) const { return prevRot.SLERP(currRot, alpha).Normalized(); }
Vec3 Bike::Velocity() const { return body->GetLinearVelocity(); }
Vec3 Bike::AngularVelocity() const { return body->GetAngularVelocity(); }
JPH::BodyID Bike::BodyID() const { return body->GetID(); }

// ---------------------------------------------------------------------------------- dibujo
// Cada pieza es una malla armada en Render (BikeMeshes.cpp) con sus colores; acá sólo se ubica según
// el estado de la física: ruedas girando, basculante siguiendo al eje trasero, amortiguador que se
// comprime, horquilla que se hunde y todo lo de adelante girando con la dirección.
void Bike::Draw(Renderer& r, float alpha, bool drawRider, int livery) const
{
    const Mat44 W = Mat44::sRotationTranslation(RenderRotation(alpha), RenderPosition(alpha));
    r.bikeStyle = P ? P->visualStyle : 0;              // cada moto con sus piezas (pueden ser distintas en red)
    auto draw = [&](BikeMesh::Id id, const Mat44& m) { r.DrawMeshColored(r.BikePart(id, livery), ToRl(m), WHITE); };

    const Wheel& fw = wheels[FRONT];
    const Wheel& rw = wheels[REAR];
    const Vec3 frontAxle = fw.AxleLocal();
    const Vec3 rearAxle = rw.AxleLocal();

    // Eje de dirección: pasa por el eje delantero a lo largo de la horquilla.
    const Vec3 forkUp = -fw.axisLocal;
    const Vec3 steerHead = fw.mountLocal + forkUp * 0.53f;
    const Mat44 steerM = W * Mat44::sTranslation(steerHead) * Mat44::sRotation(Quat::sRotation(forkUp, -DrawSteer())) *
                         Mat44::sTranslation(-steerHead);
    const Mat44 forkTilt = Mat44::sRotation(Quat::sFromTo(Vec3::sAxisY(), forkUp));
    r.UpdateBikeBody(ToRl(steerHead), ToRl(forkUp), livery);   // (con un esquema a gusto, también el suyo)

    // ------------------------------------------------------------------ ruedas
    auto wheelM = [](const Mat44& frame, Vec3 axle, float angle, float scale) {
        return frame * Mat44::sTranslation(axle) * Mat44::sRotation(Quat::sRotation(Vec3::sAxisX(), angle)) * Mat44::sScale(scale);
    };
    const Mat44 rearWheel = wheelM(W, rearAxle, rw.angle, rw.radius / BikeMesh::kRearRadius);
    const Mat44 frontWheel = wheelM(steerM, frontAxle, fw.angle, fw.radius / BikeMesh::kFrontRadius);
    draw(BikeMesh::WheelRear, rearWheel);
    draw(BikeMesh::TireRear, rearWheel);
    draw(BikeMesh::WheelFront, frontWheel);
    draw(BikeMesh::TireFront, frontWheel);

    // ------------------------------------------------------------------ chasis, basculante y amortiguador
    draw(BikeMesh::Body, W);
    const Vec3 pivot = ToJph(BikeMesh::kSwingarmPivot);
    const Vec3 arm = rearAxle - pivot;                 // el eje trasero sube por una recta: se estira apenas
    const Mat44 armLocal = Mat44::sTranslation(pivot) * Mat44::sRotation(Quat::sRotation(Vec3::sAxisX(), std::atan2(arm.GetY(), -arm.GetZ()))) *
                           Mat44::sScale(Vec3(1.0f, 1.0f, arm.Length() / BikeMesh::kSwingarmLength));
    draw(BikeMesh::Swingarm, W * armLocal);

    const Vec3 shockTop = ToJph(BikeMesh::kShockTop);
    const Vec3 shockLow = armLocal * ToJph(BikeMesh::kShockLow);
    const float shockLen = (shockLow - shockTop).Length();
    const Vec3 shockDir = (shockLow - shockTop) / shockLen;
    const Quat shockRot = Quat::sFromTo(-Vec3::sAxisY(), shockDir);
    draw(BikeMesh::ShockBody, W * Mat44::sRotationTranslation(shockRot, shockTop));
    draw(BikeMesh::ShockLower, W * Mat44::sRotationTranslation(Quat::sFromTo(Vec3::sAxisY(), -shockDir), shockLow));
    const float springLen = shockLen - BikeMesh::kSpringTop - BikeMesh::kSpringBottom;
    draw(BikeMesh::ShockSpring, W * Mat44::sRotationTranslation(shockRot, shockTop + shockDir * BikeMesh::kSpringTop) *
                                    Mat44::sScale(Vec3(1.0f, springLen / BikeMesh::kSpringLength, 1.0f)));

    // ------------------------------------------------------------------ horquilla y manubrio (giran)
    draw(BikeMesh::ForkUpper, steerM * Mat44::sTranslation(steerHead) * forkTilt);
    draw(BikeMesh::ForkLower, steerM * Mat44::sTranslation(frontAxle) * forkTilt);
    draw(BikeMesh::Cockpit, steerM * Mat44::sTranslation(steerHead));
    draw(BikeMesh::FrontFender, steerM * Mat44::sTranslation(steerHead - forkUp * BikeMesh::kFenderDrop));
    r.SetGloss(0.5f);                                  // lo que sigue (piloto generado) usa el brillo común

    // ------------------------------------------------------------------ piloto (de pie, posición de ataque)
    // Tras una caída lo dibuja el ragdoll.
    if (riderOnBike && drawRider)
        for (const RiderPiece& pc : RiderPieces(RiderPoseLocal())) DrawRiderPiece(r, W * pc.local, pc.mesh, pc.color);
}

RiderPose Bike::RiderPoseLocal() const
{
    if (P->original031 >= 0.5f) return RiderPoseLocal031();
    const Wheel& fw = wheels[FRONT];
    const Vec3 forkUp = -fw.axisLocal;
    const Vec3 steerHead = fw.mountLocal + forkUp * 0.53f;
    const Mat44 steerOnly = Mat44::sTranslation(steerHead) * Mat44::sRotation(Quat::sRotation(forkUp, -DrawSteer())) * Mat44::sTranslation(-steerHead);
    // Manubrio, estriberas y asiento según el estilo de la moto (BikeStyles.cpp y BikeMeshes.cpp).
    const BikeStyleDef& style = GetBikeStyle(P ? P->visualStyle : 0);
    const Vec3 barCenter = steerHead + style.barOffset;
    const float gripX = style.gripX;

    const float lean = riderLean;
    RiderPose p;
    p.lean = lean;
    p.throttle = gripThrottle;
    p.hips = style.hips + Vec3(0.0f, -0.07f * std::max(0.0f, -lean), style.leanHipsZ * lean);
    p.shoulders = style.shoulders + Vec3(0.0f, -0.06f * std::max(0.0f, lean), style.leanShouldersZ * lean);
    // Agachado sobre el tanque a alta velocidad (moto de carreras): el pecho baja hacia el manubrio.
    const float tucked = style.tuck * mu::Smoothstep(8.0f, 25.0f, forwardSpeed);
    p.shoulders += Vec3(0.0f, -0.20f, 0.12f) * tucked;
    // Colgado en la curva (carreras): inclinada fuerte, la cadera sale del asiento hacia adentro, el
    // torso y el casco se meten en la curva y la rodilla de adentro se abre hasta casi tocar el piso.
    const float hang = style.hangOff * mu::Smoothstep(mu::Rad(12.0f), mu::Rad(45.0f), std::fabs(roll)) * mu::Smoothstep(6.0f, 12.0f, forwardSpeed);
    const float hangSide = roll > 0.0f ? -1.0f : 1.0f;             // lado de adentro (+X = izquierda)
    p.hips += Vec3(0.14f * hangSide, -0.03f, 0.0f) * hang;
    p.shoulders += Vec3(0.20f * hangSide, -0.05f, 0.03f) * hang;
    // Pata afuera: sentado en la punta del asiento, el torso un poco hacia afuera de la curva (la moto
    // se inclina más que el piloto) y la pierna de adentro estirada hacia el eje delantero, con la
    // punta de la bota para arriba para que no se enganche. La de afuera carga la estribera.
    const float out = mu::Smoothstep(0.0f, 1.0f, std::fabs(legOut));
    const float inSide = legOut > 0.0f ? -1.0f : 1.0f;             // lado (+X = izquierda) de la pierna que sale
    p.hips += Vec3(-0.03f * inSide, -0.01f, 0.19f) * out;
    p.shoulders += Vec3(-0.07f * inSide, -0.02f, 0.08f) * out;
    // Whip: el cuerpo corrido de costado respecto de la moto, que se acuesta debajo del piloto: la
    // cadera se va para un lado y el torso queda más derecho que la moto.
    const float tiltX = -std::sin(bodyTilt);                        // +X = izquierda
    p.hips += Vec3(0.40f * tiltX, -0.03f * std::fabs(tiltX), 0.0f);
    p.shoulders += Vec3(0.62f * tiltX, -0.04f * std::fabs(tiltX), 0.0f);
    // Cuerpo de costado (flechas): la cadera se corre sobre el asiento y el torso un poco más.
    const float sideX = -riderSide;                                 // + derecha = -X
    p.hips += Vec3(0.13f * sideX, -0.03f * std::fabs(sideX), 0.0f);
    p.shoulders += Vec3(0.20f * sideX, -0.03f * std::fabs(sideX), 0.0f);
    // La cola no pasa de la punta del asiento: tirado adelante y con la pata afuera a la vez se sumaban los
    // dos corrimientos y la cadera quedaba sobre el tanque.
    p.hips.SetZ(std::min(p.hips.GetZ(), style.hips.GetZ() + std::max(style.leanHipsZ, 0.19f)));
    // De costado, el whip y el cuerpo corrido (y colgado) también se suman: el torso se iba ~55 cm al
    // costado, arriba de un puño, y con la otra mano no llegaba al suyo. Se frena suave (tanh): lo chico
    // queda igual. Ver docs/PILOTO.md, "El piloto se metía en la moto".
    p.hips.SetX(0.25f * std::tanh(p.hips.GetX() / 0.25f));
    p.shoulders.SetX(0.30f * std::tanh(p.shoulders.GetX() / 0.30f));
    // Moto casi vertical sobre la trasera (wheelie pasado, la cola raspando): de pie en los pedales, con
    // las piernas casi estiradas y el torso hacia el manubrio (con la moto a 80°, en el mundo queda ~30°
    // tirado atrás, colgado del manubrio; sentado y tirado atrás quedaría acostado). Antes el torso iba
    // derecho respecto del mundo, a lo largo del tanque, y la cadera y el pecho quedaban adentro del tanque
    // (ver docs/PILOTO.md, "El piloto se metía en la moto").
    // Sólo apoyada: en el aire, tirándose atrás, la moto pasa de 55° y el piloto saltaba a esta pose.
    const float stand = std::max(style.stand, mu::Smoothstep(mu::Rad(55.0f), mu::Rad(80.0f), pitch) * supported);
    p.hips = p.hips + (Vec3(0.0f, 0.34f, -0.08f) - p.hips) * stand;
    p.shoulders = p.shoulders + (Vec3(0.0f, 0.66f, 0.30f) - p.shoulders) * stand;
    p.head = p.shoulders + Vec3(0.05f * hangSide * hang, 0.18f - 0.02f * hang, 0.09f);   // el casco casi apoyado en el collarín
    for (int i = 0; i < 2; ++i) {
        const float side = i == 0 ? 1.0f : -1.0f;       // +X = izquierda
        const Vec3 peg(style.peg.GetX() * side, style.peg.GetY(), style.peg.GetZ());
        const float a = side == inSide ? out : 0.0f;
        p.hip[i] = p.hips + Vec3(0.10f * side, 0, 0);
        p.knee[i] = Vec3(style.knee.GetX() * side + 0.2f * tiltX + 0.07f * sideX, style.knee.GetY(), style.knee.GetZ() + 0.08f * lean + 0.10f * (out - a));
        p.knee[i] = p.knee[i] + (Vec3(0.19f * side, -0.03f, 0.05f) - p.knee[i]) * stand;
        if (side == hangSide) p.knee[i] = p.knee[i] + (Vec3(0.38f * side, -0.05f, 0.08f) - p.knee[i]) * hang;   // rodilla al piso
        p.ankle[i] = peg + Vec3(0, 0.06f, 0);
        p.foot[i] = peg + Vec3(0, 0.02f, 0.04f);
        p.legOut[i] = a;
        if (a > 0.0f) {
            // Sale por afuera de los plásticos (arco) hasta quedar al costado de la rueda delantera.
            const Vec3 ankleOut(0.34f * side, -0.28f, 0.64f);
            p.ankle[i] = p.ankle[i] + (ankleOut - p.ankle[i]) * a + Vec3(0.07f * side, 0.04f, 0.0f) * std::sin(mu::kPi * a);
            p.foot[i] = p.ankle[i] + Vec3(0.0f, -0.04f + 0.07f * a, 0.04f + 0.03f * a);
            // Rodilla apenas doblada y hacia arriba (nunca recta del todo: el ragdoll arma la bisagra con ella).
            const Vec3 bent = (p.hip[i] + p.ankle[i]) * 0.5f + Vec3(0.05f * side, 0.07f, 0.02f);
            p.knee[i] = p.knee[i] + (bent - p.knee[i]) * a;
        }
        p.grip[i] = steerOnly * (barCenter + Vec3(gripX * side, 0.0056f, -0.006f));   // centro del puño (el manubrio sube hacia las puntas)
        p.shoulder[i] = p.shoulders + Vec3(0.19f * side, -0.03f, 0);
        p.elbow[i] = (p.shoulder[i] + p.grip[i]) * 0.5f + Vec3(0.12f * side, -0.05f, -0.06f);
    }
    if (trickAmt > 0.0f) ApplyTrickPose(p, trick, trickAmt);   // trucos (ver ApplyTrickPose)
    return p;
}

void Bike::DrawDebug(float alpha) const
{
    (void)alpha;
    constexpr float kForceScale = 1.0f / 2000.0f;   // 2000 N = 1 m
    for (const Wheel& w : wheels) {
        const Vec3 mount = currPos + currRot * w.mountLocal;
        const Vec3 axle = currPos + currRot * w.AxleLocal();
        DrawLine3D(ToRl(mount), ToRl(axle), GRAY);
        if (!w.grounded) continue;
        const Vector3 cp = ToRl(w.contactPoint);
        DrawLine3D(cp, ToRl(w.contactPoint + w.suspForce * kForceScale), GREEN);
        DrawLine3D(cp, ToRl(w.contactPoint + w.tireForce * kForceScale), RED);
        DrawLine3D(cp, ToRl(w.contactPoint + w.contactNormal * 0.5f), BLUE);
        DrawSphere(cp, 0.03f, YELLOW);
    }
    DrawSphere(ToRl(currPos), 0.035f, WHITE);                  // COM físico
    DrawSphere(ToRl(currPos + comShiftWorld), 0.05f, ORANGE);  // COM efectivo (piloto + offsets)
    DrawLine3D(ToRl(currPos), ToRl(currPos + body->GetLinearVelocity() * 0.2f), WHITE);
}

// Pose original: pata afuera, whip y cuerpo adelante/atrás.
RiderPose Bike::RiderPoseLocal031() const
{
    const Wheel& fw = wheels[FRONT];
    const Vec3 forkUp = -fw.axisLocal;
    const Vec3 steerHead = fw.mountLocal + forkUp * 0.53f;
    const Mat44 steerOnly = Mat44::sTranslation(steerHead) * Mat44::sRotation(Quat::sRotation(forkUp, -DrawSteer())) * Mat44::sTranslation(-steerHead);
    const Vec3 barCenter = steerHead + Vec3(0.0f, 0.14f, -0.06f);

    const float lean = riderLean;
    RiderPose p;
    p.lean = lean;
    p.throttle = gripThrottle;
    p.hips = Vec3(0.0f, 0.30f - 0.07f * std::max(0.0f, -lean), -0.30f + 0.22f * lean);
    p.shoulders = Vec3(0.0f, 0.68f - 0.06f * std::max(0.0f, lean), -0.04f + 0.30f * lean);
    // Pata afuera: sentado en la punta del asiento, el torso un poco hacia afuera de la curva (la moto
    // se inclina más que el piloto) y la pierna de adentro estirada hacia el eje delantero, con la
    // punta de la bota para arriba para que no se enganche. La de afuera carga la estribera.
    const float out = mu::Smoothstep(0.0f, 1.0f, std::fabs(legOut));
    const float inSide = legOut > 0.0f ? -1.0f : 1.0f;             // lado (+X = izquierda) de la pierna que sale
    p.hips += Vec3(-0.03f * inSide, -0.01f, 0.19f) * out;
    p.shoulders += Vec3(-0.07f * inSide, -0.02f, 0.08f) * out;
    // Whip: el cuerpo corrido de costado respecto de la moto, que se acuesta debajo del piloto: la
    // cadera se va para un lado y el torso queda más derecho que la moto.
    const float tiltX = -std::sin(bodyTilt);                        // +X = izquierda
    p.hips += Vec3(0.40f * tiltX, -0.03f * std::fabs(tiltX), 0.0f);
    p.shoulders += Vec3(0.62f * tiltX, -0.04f * std::fabs(tiltX), 0.0f);
    p.head = p.shoulders + Vec3(0.0f, 0.18f, 0.09f);   // el casco casi apoyado en el collarín
    for (int i = 0; i < 2; ++i) {
        const float side = i == 0 ? 1.0f : -1.0f;       // +X = izquierda
        const Vec3 peg(0.17f * side, -0.40f, -0.10f);
        const float a = side == inSide ? out : 0.0f;
        p.hip[i] = p.hips + Vec3(0.10f * side, 0, 0);
        p.knee[i] = Vec3(0.19f * side + 0.2f * tiltX, -0.02f, 0.06f + 0.08f * lean + 0.10f * (out - a));
        p.ankle[i] = peg + Vec3(0, 0.06f, 0);
        p.foot[i] = peg + Vec3(0, 0.02f, 0.04f);
        p.legOut[i] = a;
        if (a > 0.0f) {
            // Sale por afuera de los plásticos (arco) hasta quedar al costado de la rueda delantera.
            const Vec3 ankleOut(0.34f * side, -0.28f, 0.64f);
            p.ankle[i] = p.ankle[i] + (ankleOut - p.ankle[i]) * a + Vec3(0.07f * side, 0.04f, 0.0f) * std::sin(mu::kPi * a);
            p.foot[i] = p.ankle[i] + Vec3(0.0f, -0.04f + 0.07f * a, 0.04f + 0.03f * a);
            // Rodilla apenas doblada y hacia arriba (nunca recta del todo: el ragdoll arma la bisagra con ella).
            const Vec3 bent = (p.hip[i] + p.ankle[i]) * 0.5f + Vec3(0.05f * side, 0.07f, 0.02f);
            p.knee[i] = p.knee[i] + (bent - p.knee[i]) * a;
        }
        p.grip[i] = steerOnly * (barCenter + Vec3(0.345f * side, 0.0056f, -0.006f));   // centro del puño (el manubrio sube hacia las puntas)
        p.shoulder[i] = p.shoulders + Vec3(0.19f * side, -0.03f, 0);
        p.elbow[i] = (p.shoulder[i] + p.grip[i]) * 0.5f + Vec3(0.12f * side, -0.05f, -0.06f);
    }
    if (trickAmt > 0.0f) ApplyTrickPose(p, trick, trickAmt);
    return p;
}

// ------------------------------------------------------------------------------------------------ trucos
// Ver docs/PILOTO.md, "Trucos". La pose del truco sale de la del estilo (asiento, estriberas, manubrio), así sirve
// para todas las motos; la usan el dibujo (el modelo por IK, el generado), el ragdoll al caerse y la física (TrickBody:
// con la pose neutra, la reacción del cuerpo sobre la moto).
namespace {

Vec3 Bezier(Vec3 a, Vec3 c, Vec3 b, float t)
{
    const float u = 1.0f - t;
    return a * (u * u) + c * (2.0f * u * t) + b * (t * t);
}

// Articulación del medio de dos huesos de largos l1 y l2 de `a` a `tip`, doblando hacia `pole` (dirección). Si no
// llega, la punta se acerca (queda casi estirado). Desde el 90% del alcance la punta se acerca al tope de a poco (tanh, sin
// quiebre): con el tope de golpe, el tobillo o la mano que pasaba de largo frenaba en un cuadro y la rodilla, que cerca de
// estirada se mueve muy rápido, pegaba un tirón (5-8 cm por cuadro en el heel clicker; docs/PILOTO.md, "Saltos de la pose").
Vec3 MidJoint(Vec3 a, Vec3& tip, float l1, float l2, Vec3 pole)
{
    Vec3 d = tip - a;
    const float reach = (l1 + l2) * 0.995f, soft = 0.9f * reach;
    float dist = d.Length();
    if (dist > soft) dist = soft + (reach - soft) * std::tanh((dist - soft) / (reach - soft));
    dist = std::max(dist, std::fabs(l1 - l2) + 1e-3f);
    const Vec3 dir = d.NormalizedOr(-Vec3::sAxisY());
    tip = a + dir * dist;
    const float along = (l1 * l1 - l2 * l2 + dist * dist) / (2.0f * dist);
    const float h = std::sqrt(std::max(0.0f, l1 * l1 - along * along));
    Vec3 side = pole - dir * pole.Dot(dir);
    side = side.Length() > 1e-4f ? side.Normalized() : dir.GetNormalizedPerpendicular();
    return a + dir * along + side * h;
}

} // namespace

void Bike::ApplyTrickPose(RiderPose& p, int t, float amount) const
{
    if (t <= Trick::None || t >= Trick::Count || !(amount > 0.0f)) return;
    const BikeStyleDef& style = GetBikeStyle(P ? P->visualStyle : 0);
    const float a = mu::Clamp(amount, 0.0f, 1.0f);
    const RiderPose base = p;
    const Vec3 gc = (p.grip[0] + p.grip[1]) * 0.5f;            // centro del manubrio (gira con él)
    const Vec3 sh = style.hips;                                  // cadera neutra del estilo (el asiento)
    const float pegX = std::fabs(style.peg.GetX());

    // Lo que pide cada truco: cadera y hombros (y la cabeza respecto de ellos) y, para cada pie o mano que se suelta, a
    // dónde va (end), por dónde pasa (ctrl: una curva que esquiva el asiento, la cola o el tanque), hacia dónde dobla la
    // rodilla (pole) y hacia dónde apunta el pie (toe). Cada uno con su peso: el cuerpo arranca antes que los pies.
    Vec3 hipsT = p.hips, shouldersT = p.shoulders, headOff = p.head - p.shoulders;
    float wBody = mu::Smoothstep(0.0f, 0.85f, a);
    float lookYaw = 0.0f, lookDown = 0.0f, twist = 0.0f;         // hacia dónde mira (sólo el modelo: RiderPose::lookYaw)
    constexpr float kNacNacLook = 1.35f;                        // rad: el nac-nac mira atrás (~77°), por arriba del hombro
    struct Limb {
        bool free = false;
        Vec3 ctrl = Vec3::sZero(), end = Vec3::sZero(), pole = -Vec3::sAxisY(), toe = Vec3::sAxisZ();
        float w = 0.0f;
    } leg[2], hand[2];
    switch (t) {
    case Trick::Superman: {
        // Los pies salen de las estriberas y el cuerpo se estira atrás, horizontal, con las manos en el manubrio y los
        // brazos estirados: los hombros bajan detrás del manubrio, la cadera queda sobre la cola y las piernas, estiradas
        // atrás y un poco arriba.
        shouldersT = gc + Vec3(0.0f, 0.12f, -0.51f);
        hipsT = shouldersT + Vec3(0.0f, 0.04f, -0.46f);
        headOff = Vec3(0.0f, 0.07f, 0.17f);
        for (int i = 0; i < 2; ++i) {
            const float s = i == 0 ? 1.0f : -1.0f;              // +X = izquierda
            Limb& l = leg[i];
            l.free = true;
            l.end = hipsT + Vec3(0.13f * s, 0.06f, -0.74f);
            l.ctrl = Vec3((pegX + 0.16f) * s, sh.GetY() - 0.08f, sh.GetZ() - 0.22f);   // por afuera de la cola
            l.pole = Vec3(0.0f, -1.0f, 0.15f);                  // boca abajo: las rodillas hacia la moto
            l.toe = Vec3(0.0f, 0.15f, -1.0f);                   // en punta, hacia atrás
            l.w = mu::Smoothstep(0.05f, 1.0f, a);
        }
        break;
    }
    case Trick::HeelClicker: {
        // Los dos pies juntos por arriba y por delante del manubrio, las piernas estiradas entre los brazos, la cola
        // levantada en la punta del asiento y el torso derecho, colgado de los brazos estirados (el modelo lo inclina
        // adelante lo justo para llegar al manubrio: ver docs/PILOTO.md, "Brazos más largos").
        hipsT = sh + Vec3(0.0f, 0.06f, 0.28f);
        shouldersT = hipsT + Vec3(0.0f, 0.45f, -0.03f);
        for (int i = 0; i < 2; ++i) {
            const float s = i == 0 ? 1.0f : -1.0f;
            Limb& l = leg[i];
            l.free = true;
            l.end = Vec3(0.04f * s, gc.GetY() + 0.72f, gc.GetZ() + 0.20f);   // arriba y adelante del manubrio (~45° desde la cadera)
            l.ctrl = Vec3(0.12f * s, sh.GetY() + 0.22f, sh.GetZ() + 0.45f);   // las rodillas suben al pecho y se estiran
            l.pole = Vec3(0.0f, 1.0f, -0.2f);
            l.toe = Vec3(0.0f, 0.5f, 0.85f);
            l.w = mu::Smoothstep(0.0f, 1.0f, a);
        }
        break;
    }
    case Trick::NoHander: {
        // Suelta las manos y abre los brazos a los costados (un poco arriba); se sienta derecho, un poco atrás, y aprieta
        // la moto con las rodillas.
        hipsT = p.hips + Vec3(0.0f, 0.03f, -0.05f);
        shouldersT = p.shoulders + Vec3(0.0f, 0.03f, -0.10f);
        for (int i = 0; i < 2; ++i) {
            const float s = i == 0 ? 1.0f : -1.0f;
            Limb& h = hand[i];
            h.free = true;
            h.end = shouldersT + Vec3(0.60f * s, 0.12f, 0.03f);
            h.ctrl = (p.grip[i] + h.end) * 0.5f + Vec3(0.0f, 0.16f, 0.0f);   // sube y se abre (no barre el manubrio)
            h.pole = Vec3(0.2f * s, -1.0f, -0.6f);              // codos abajo y atrás
            h.w = mu::Smoothstep(0.0f, 1.0f, a);
        }
        break;
    }
    case Trick::NacNac:
    case Trick::CanCan:
    case Trick::NacNacRight:
    case Trick::CanCanLeft: {
        // Nac-nac (a la izquierda): la pierna derecha pasa por arriba de la cola al otro lado y queda estirada atrás y
        // afuera; el pie izquierdo sigue en su estribera y la cadera se corre y sube; la cabeza mira atrás, por arriba
        // del hombro de ese lado (la pierna), y los hombros giran un poco con ella. Can-can (a la derecha): la pierna
        // izquierda cruza por arriba del asiento, por delante de la cadera, y queda estirada al costado de la rueda
        // delantera; el cuerpo, adelante sobre el manubrio. NacNacRight y CanCanLeft, los mismos espejados.
        const bool nac = t == Trick::NacNac || t == Trick::NacNacRight;
        const float s = (t == Trick::NacNac || t == Trick::CanCanLeft) ? 1.0f : -1.0f;   // hacia dónde va la pierna (+X = izquierda)
        const int i = s > 0.0f ? 1 : 0;                          // la pierna que cruza (la del otro lado)
        Limb& l = leg[i];
        l.free = true;
        if (nac) {
            hipsT = p.hips + Vec3(0.10f * s, 0.08f, -0.03f);
            shouldersT = p.shoulders + Vec3(0.07f * s, -0.01f, 0.03f);
            l.end = Vec3(0.50f * s, sh.GetY() - 0.20f, sh.GetZ() - 0.40f);
            // El pie sale para atrás por su lado y cruza por arriba del guardabarros (antes pasaba a 17 cm de la cadera, por
            // arriba del asiento: el muslo giraba 130° en cuatro cuadros y la rodilla pegaba un tirón de 22 cm por cuadro).
            l.ctrl = Vec3(-0.30f * s, sh.GetY() + 0.40f, sh.GetZ() - 0.80f);
            l.pole = Vec3(0.0f, -1.0f, -0.3f);
            l.toe = Vec3(0.5f * s, -0.3f, -0.8f);
            lookYaw = kNacNacLook * s;
            lookDown = 0.30f;
            twist = 0.25f * s;
        } else {
            hipsT = p.hips + Vec3(0.07f * s, 0.12f, 0.05f);
            shouldersT = p.shoulders + Vec3(0.03f * s, 0.02f, 0.10f);
            l.end = Vec3(0.56f * s, sh.GetY() + 0.02f, sh.GetZ() + 0.42f);   // estirada al costado y adelante, casi horizontal
            l.ctrl = Vec3(-0.05f * s, sh.GetY() + 0.45f, sh.GetZ() + 0.30f);
            l.pole = Vec3(0.0f, 0.8f, 0.6f);
            l.toe = Vec3(0.6f * s, 0.1f, 0.8f);
        }
        l.w = mu::Smoothstep(0.0f, 1.0f, a);
        break;
    }
    default: return;
    }

    // Cuerpo.
    p.hips = base.hips + (hipsT - base.hips) * wBody;
    p.shoulders = base.shoulders + (shouldersT - base.shoulders) * wBody;
    p.trickHips = p.hips - base.hips;
    p.trickShoulders = p.shoulders - base.shoulders;
    p.head = p.shoulders + (base.head - base.shoulders) + (headOff - (base.head - base.shoulders)) * wBody;
    p.lookYaw = lookYaw * wBody;
    p.lookDown = lookDown * wBody;
    p.twist = twist * wBody;
    float most = wBody;
    for (int i = 0; i < 2; ++i) {
        p.hip[i] = p.hips + (base.hip[i] - base.hips);
        p.shoulder[i] = p.shoulders + (base.shoulder[i] - base.shoulders);
        // Piernas: el tobillo por la curva (o en la estribera) y la rodilla con los largos de la pose (el piloto generado
        // y el ragdoll se arman con ellos). La diferencia con la pose de siempre se va en el primer 30%: sin salto al empezar.
        const Limb& l = leg[i];
        const float l1 = (base.knee[i] - base.hip[i]).Length(), l2 = (base.ankle[i] - base.knee[i]).Length();
        const Vec3 poleBase = (base.knee[i] - (base.hip[i] + base.ankle[i]) * 0.5f).NormalizedOr(Vec3::sAxisZ());
        Vec3 ankle0 = base.ankle[i];
        const Vec3 knee0 = MidJoint(base.hip[i], ankle0, l1, l2, poleBase);
        Vec3 ankle = l.free ? Bezier(base.ankle[i], l.ctrl, l.end, l.w) : base.ankle[i];
        const Vec3 pole = l.free ? (poleBase * (1.0f - l.w) + l.pole.Normalized() * l.w).NormalizedOr(poleBase) : poleBase;
        const Vec3 knee = MidJoint(p.hip[i], ankle, l1, l2, pole);
        const float keep = 1.0f - mu::Smoothstep(0.0f, 0.3f, l.free ? l.w : wBody);
        p.knee[i] = knee + (base.knee[i] - knee0) * keep;
        p.ankle[i] = ankle;
        if (l.free) {
            p.foot[i] = ankle + (base.foot[i] - base.ankle[i]) * (1.0f - l.w) + l.toe.Normalized() * (0.07f * l.w);
            p.toe[i] = l.toe.Normalized();
            p.legFree[i] = l.w;
            p.legOut[i] *= 1.0f - l.w;
            most = std::max(most, l.w);
        } else {
            p.foot[i] = ankle + (base.foot[i] - base.ankle[i]);
        }
        // Brazos: la mano por su curva (o en el puño) y el codo con los largos de la pose.
        const Limb& h = hand[i];
        const float a1 = (base.elbow[i] - base.shoulder[i]).Length(), a2 = (base.grip[i] - base.elbow[i]).Length();
        const Vec3 elbowPole = (base.elbow[i] - (base.shoulder[i] + base.grip[i]) * 0.5f).NormalizedOr(-Vec3::sAxisY());
        Vec3 grip0 = base.grip[i];
        const Vec3 elbow0 = MidJoint(base.shoulder[i], grip0, a1, a2, elbowPole);
        Vec3 grip = h.free ? Bezier(base.grip[i], h.ctrl, h.end, h.w) : base.grip[i];
        const Vec3 ePole = h.free ? (elbowPole * (1.0f - h.w) + h.pole.Normalized() * h.w).NormalizedOr(elbowPole) : elbowPole;
        Vec3 gripReach = grip;
        const Vec3 elbow = MidJoint(p.shoulder[i], gripReach, a1, a2, ePole);
        p.elbow[i] = elbow + (base.elbow[i] - elbow0) * (1.0f - mu::Smoothstep(0.0f, 0.3f, h.free ? h.w : wBody));
        if (h.free) {
            p.grip[i] = gripReach;
            p.handFree[i] = h.w;
            most = std::max(most, h.w);
        }
    }
    p.trick = most;
}

// Centros de masa de las partes del cuerpo (RiderPart: pelvis, torso, cabeza, muslos, piernas, brazos, antebrazos) con la
// pose neutra del estilo (sin cuerpo adelante ni atrás, el manubrio derecho) y el truco: lo que se movió respecto de esa
// misma pose sin truco es lo que mueve la física. No depende del dibujo (ni del cuerpo con masa).
void Bike::TrickBody(int t, float amount, Vec3* c) const
{
    const BikeStyleDef& style = GetBikeStyle(P ? P->visualStyle : 0);
    const Wheel& fw = wheels[FRONT];
    const Vec3 steerHead = fw.mountLocal - fw.axisLocal * 0.53f;
    const Vec3 barCenter = steerHead + style.barOffset;
    RiderPose p;
    p.hips = style.hips;
    p.shoulders = style.shoulders;
    p.head = p.shoulders + Vec3(0.0f, 0.18f, 0.09f);
    for (int i = 0; i < 2; ++i) {
        const float side = i == 0 ? 1.0f : -1.0f;
        const Vec3 peg(style.peg.GetX() * side, style.peg.GetY(), style.peg.GetZ());
        p.hip[i] = p.hips + Vec3(0.10f * side, 0, 0);
        p.knee[i] = Vec3(style.knee.GetX() * side, style.knee.GetY(), style.knee.GetZ());
        p.ankle[i] = peg + Vec3(0, 0.06f, 0);
        p.foot[i] = peg + Vec3(0, 0.02f, 0.04f);
        p.grip[i] = barCenter + Vec3(style.gripX * side, 0.0056f, -0.006f);
        p.shoulder[i] = p.shoulders + Vec3(0.19f * side, -0.03f, 0);
        p.elbow[i] = (p.shoulder[i] + p.grip[i]) * 0.5f + Vec3(0.12f * side, -0.05f, -0.06f);
    }
    ApplyTrickPose(p, t, amount);
    c[RiderPart::Pelvis] = p.hips;
    c[RiderPart::Torso] = p.hips + (p.shoulders - p.hips) * 0.55f;
    c[RiderPart::Head] = p.head;
    for (int i = 0; i < 2; ++i) {
        c[RiderPart::Thigh + i] = (p.hip[i] + p.knee[i]) * 0.5f;
        c[RiderPart::Shin + i] = (p.knee[i] + p.foot[i]) * 0.5f;
        c[RiderPart::UpperArm + i] = (p.shoulder[i] + p.elbow[i]) * 0.5f;
        c[RiderPart::Forearm + i] = (p.elbow[i] + p.grip[i]) * 0.5f;
    }
}
