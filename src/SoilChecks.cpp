#include "PhysicsWorld.h"
#include "Terrain.h"
#include "TerrainDeformation.h"
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

// --headless --test soilcheck: invariantes del suelo, contra el collider real de Jolt.
bool RunSoilChecks(Terrain& terrain, PhysicsWorld& world)
{
    int failures=0;
    auto check=[&](bool ok,const char* label) {
        std::printf("soilcheck: %s %s\n",ok?"OK":"FAIL",label);
        failures += !ok;
    };
    Vector2 dirt{}, paved{};
    bool haveDirt=false, havePaved=false;
    float bestSoil=0.4f;
    for (int z=16; z<terrain.N-16; z+=4) for (int x=16; x<terrain.N-16; x+=4) {
        const Vector2 p={terrain.OriginX()+x*terrain.Cell,terrain.OriginZ()+z*terrain.Cell};
        if (terrain.SoilAmount(p.x,p.y)>bestSoil && terrain.Normal(p.x,p.y).GetY()>0.98f) {
            dirt=p; haveDirt=true; bestSoil=terrain.SoilAmount(p.x,p.y);
        }
        if (!havePaved && terrain.PavedAmount(p.x,p.y)>0.99f) { paved=p; havePaved=true; }
    }
    check(haveDirt,"parche de tierra encontrado");
    if (!haveDirt) return false;
    const float base=terrain.Height(dirt.x,dirt.y);
    const float outside=terrain.Height(dirt.x+0.4f,dirt.y);
    // Alturas y normales sin tocar en la grilla de más abajo (para lo que sienten las ruedas).
    float untouched[25][25];
    JPH::Vec3 untouchedN[25][25];
    for (int iz=-12;iz<=12;++iz) for (int ix=-12;ix<=12;++ix) {
        untouched[iz+12][ix+12]=terrain.Height(dirt.x+ix*0.027f,dirt.y+iz*0.027f);
        untouchedN[iz+12][ix+12]=terrain.Normal(dirt.x+ix*0.027f,dirt.y+iz*0.027f);
    }
    const auto body=terrain.BodyID();
    auto press=[&](float load,float slip,float dt) { terrain.PressSoil(dirt.x,dirt.y,0,1,load,slip,1,0.24f,dt); };
    press(0,20,1.0f/120);
    check(!terrain.HasPendingDeformation(),"sin carga no excava");
    press(1200,20,1.0f/120);
    check(terrain.HasPendingDeformation() && terrain.Height(dirt.x,dirt.y)==base,"altura publica estable hasta commit");
    terrain.CommitDeformation(world);
    check(terrain.Height(dirt.x,dirt.y)<base,"desplazamiento geometrico real");
    terrain.ResetDeformation(world);
    for (int i=0;i<1200;++i) press(1200,0,1.0f/120);
    terrain.CommitDeformation(world);
    const float standing=base-terrain.Height(dirt.x,dirt.y);
    check(standing>0 && standing<=0.0251f,"rueda quieta se compacta sin cavar indefinidamente");
    for (int i=0;i<2400;++i) press(1200,28,1.0f/120);
    terrain.CommitDeformation(world);
    const float depth=base-terrain.Height(dirt.x,dirt.y);
    check(depth>standing && depth<=0.2401f,"patinaje excava y respeta el limite");
    check(terrain.SoilCell()<=0.0501f,"resolucion local de cinco centimetros");
    check(std::fabs(terrain.Height(dirt.x+0.4f,dirt.y)-outside)<0.001f,
        "huella concentrada en el ancho de la cubierta");
    float dug,banked,bank;
    terrain.SoilStats(dug,banked,bank);
    check(dug>0 && banked>0 && banked<=dug*0.601f && bank<=0.0841f,"bordes acotados y balance de volumen");
    struct OnlyTerrain : JPH::BodyFilter {
        JPH::BodyID id;
        explicit OnlyTerrain(JPH::BodyID body):id(body){}
        bool ShouldCollide(const JPH::BodyID& other) const override { return other==id; }
    } filter(body);
    float worst=0;
    for (int i=-3;i<=3;++i) {
        const float x=dirt.x+i*terrain.Cell*0.5f, z=dirt.y+0.17f*terrain.Cell;
        const float height=terrain.Height(x,z);
        const JPH::RRayCast ray(JPH::RVec3(x,height+2,z),JPH::Vec3(0,-4,0));
        JPH::RayCastResult hit;
        const bool found=world.Query().CastRay(ray,hit,JPH::BroadPhaseLayerFilter(),JPH::ObjectLayerFilter(),filter);
        worst=std::max(worst,found?std::fabs((float)ray.GetPointOnRay(hit.mFraction).GetY()-height):100.0f);
    }
    check(worst<0.003f,"raycasts de Jolt coinciden con el relieve (3 mm)");
    for (int iz=-12;iz<=12;++iz) for (int ix=-12;ix<=12;++ix) {
        const float x=dirt.x+ix*0.027f,z=dirt.y+iz*0.027f,h=terrain.Height(x,z);
        const JPH::RRayCast ray(JPH::RVec3(x,h+1,z),JPH::Vec3(0,-2,0));
        JPH::RayCastResult hit;
        const bool found=world.Query().CastRay(ray,hit,JPH::BroadPhaseLayerFilter(),JPH::ObjectLayerFilter(),filter);
        worst=std::max(worst,found?std::fabs((float)ray.GetPointOnRay(hit.mFraction).GetY()-h):100.0f);
    }
    check(worst<0.003f,"625 rayos: costuras, bordes y triangulacion fina sin huecos");
    if (terrain.rideSoil>0.0f) {
        // Lo que sienten las ruedas (RideHeight/RideNormal): sólo el hundido, suavizado, con tope y sin paredes.
        terrain.StepRide(10.0f);                   // que el suelo de las ruedas termine de hundirse
        float felt=0,above=-1,tilt=1,meshTilt=1,bank=0;
        for (int iz=-12;iz<=12;++iz) for (int ix=-12;ix<=12;++ix) {
            const float x=dirt.x+ix*0.027f,z=dirt.y+iz*0.027f,un=untouched[iz+12][ix+12];
            felt=std::max(felt,un-terrain.RideHeight(x,z));
            above=std::max(above,terrain.RideHeight(x,z)-un);
            bank=std::max(bank,terrain.Height(x,z)-un);
            tilt=std::min(tilt,terrain.RideNormal(x,z).Dot(untouchedN[iz+12][ix+12]));
            meshTilt=std::min(meshTilt,terrain.Normal(x,z).Dot(untouchedN[iz+12][ix+12]));
        }
        std::printf("soilcheck: las ruedas hunden %.1f mm como mucho (la malla tiene %.0f mm de banco); normal vs sin surco: %.4f (la de la malla, %.4f)\n",
                    felt*1000,bank*1000,tilt,meshTilt);
        check(felt>0.001f && felt<=terrain.rideDepth+0.0005f,"las ruedas sienten el surco, con el tope de hundimiento");
        check(above<0.0005f && bank>0.005f,"los bancos estan en la malla pero no en lo que sienten las ruedas");
        check(tilt>0.998f && meshTilt<0.95f,"la normal que sienten las ruedas no se inclina en el surco");
    }
    // Una rueda que llega al borde de su propio surco debe poder seguir removiendo tierra.
    bool steepRut=false;
    for (int i=-20;i<=20 && !steepRut;++i) {
        const float x=dirt.x+i*0.01f;
        if (terrain.Normal(x,dirt.y).GetY()>=0.55f) continue;
        steepRut=true;
        terrain.PressSoil(x,dirt.y,0,1,1400,28,1,0.24f,1.0f/120);
        check(terrain.HasPendingDeformation(),"el borde de un surco no bloquea nuevas pasadas");
    }
    if (!steepRut) std::printf("soilcheck: SKIP borde abrupto (suelo poco blando)\n");
    terrain.ResetDeformation(world);
    check(terrain.BodyID()==body && terrain.Height(dirt.x,dirt.y)==base,"restaurar conserva body y recupera altura original");
    check(terrain.RideHeight(dirt.x,dirt.y)==base && terrain.RideNormal(dirt.x,dirt.y)==terrain.Normal(dirt.x,dirt.y),
          "sin surcos las ruedas ven exactamente Height y Normal");
    auto temporal=[&](int hz) {
        for (int i=0;i<hz/2;++i) press(900,3,1.0f/hz);
        terrain.CommitDeformation(world);
        const float d=base-terrain.Height(dirt.x,dirt.y);
        terrain.ResetDeformation(world);
        return d;
    };
    check(std::fabs(temporal(60)-temporal(120))<0.0005f,"excavacion independiente del paso temporal");
    auto rollingPass=[&](int hz,float speed) {
        TerrainDeformation pass;
        const float dt=1.0f/hz;
        const int steps=(int)std::ceil(1.2f/(speed*dt));
        for (int i=0;i<=steps;++i)
            pass.PressWheel(terrain,0,{dirt.x,dirt.y-0.6f+i*speed*dt},{0,1},true,1100,0,0,0.65f,0.24f,dt);
        terrain.CommitDeformation(world);
        const float depth=base-terrain.Height(dirt.x,dirt.y);
        terrain.ResetDeformation(world);
        return depth;
    };
    const float roll5=rollingPass(120,5),roll10=rollingPass(120,10),roll60=rollingPass(60,5);
    std::printf("soilcheck: rodadura sin patinaje %.1f / %.1f mm (18 / 36 km/h)\n",roll5*1000,roll10*1000);
    check(roll5>0.012f && roll10>0.012f,"una pasada normal sin derrapar deja huella visible con ajustes de fabrica");
    check(std::fabs(roll5-roll10)<0.008f && std::fabs(roll5-roll60)<0.003f,"rodadura estable entre velocidades y pasos temporales");
    TerrainDeformation wheels;
    wheels.PressWheel(terrain,0,dirt,{0,1},false,1200,20,0,1,0.24f,1.0f/120);
    check(!terrain.HasPendingDeformation(),"rueda en el aire no deja surco");
    wheels.PressWheel(terrain,0,dirt,{1,0},true,1200,20,0,1,0.24f,1.0f/120);
    const float middle=terrain.Height(dirt.x+6,dirt.y);
    wheels.PressWheel(terrain,0,{dirt.x+12,dirt.y},{1,0},true,1200,20,0,1,0.24f,1.0f/120);
    terrain.CommitDeformation(world);
    check(terrain.Height(dirt.x+6,dirt.y)==middle,"reaparecer no conecta huellas a distancia");
    terrain.ResetDeformation(world);
    if (havePaved) {
        for (int i=0;i<600;++i) terrain.PressSoil(paved.x,paved.y,0,1,3000,40,1,0.35f,1.0f/120);
        check(!terrain.HasPendingDeformation(),"asfalto protegido de excavacion");
    } else std::printf("soilcheck: SKIP pavimento (este mapa no tiene)\n");
    std::printf("soilcheck: %d fallos, error collider %.6f m\n",failures,worst);
    return failures==0;
}
