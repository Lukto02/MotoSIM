#include "Terrain.h"
#include "PhysicsWorld.h"
#include "MathUtil.h"
#include "raylib.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace { constexpr int coarseChunk=64; }

float Terrain::SoilNode(int x,int z,bool pending) const
{
    const auto& nodes=pending?soilPending:soilHeights;
    const auto it=nodes.find({x,z});
    return it==nodes.end()?0.0f:it->second;
}

float Terrain::SoilDelta(float x,float z) const
{
    if (soilHeights.empty()) return 0;
    const float fx=(x-origin)/SoilCell(), fz=(z-origin)/SoilCell();
    const int ix=(int)std::floor(fx), iz=(int)std::floor(fz);
    const float tx=fx-ix,tz=fz-iz;
    const float a=SoilNode(ix,iz,false),b=SoilNode(ix+1,iz,false);
    const float c=SoilNode(ix,iz+1,false),d=SoilNode(ix+1,iz+1,false);
    return tx>tz ? a+(b-a)*tx+(d-b)*tz : a+(d-c)*tx+(c-a)*tz;
}

// El suelo que sienten las ruedas (ver docs/FISICA.md, "El surco no tiene que manejar la moto"). La malla
// guarda el surco entero (hasta 24 cm de hondo y 20 cm de ancho, con paredes casi verticales y bancos) y la
// cubierta, un resorte de 400 kN/m casi sin amortiguar, lo sentía todo: paredes (normales inclinadas, empujes
// de costado de miles de N), saltos de altura de varios cm en cada commit y rugosidad de mm que la hace
// rebotar a ~27 Hz. Con esto la rueda ve una versión del surco: sólo el hundido (los bancos no), promediado
// con un núcleo gaussiano de rideBlur (sigma, 25 cm), con tope blando rideDepth (2 cm) y que llega con demora
// (rideSink, 2 cm/s). Lo que se ve (malla, shader, huellas) no cambia.
float Terrain::RideDelta(float x,float z) const
{
    if (soilRide.empty()) return 0.0f;
    const float cell=SoilCell();
    const float step=std::max(rideBlur,0.02f)*0.70710678f;       // binomial de 9 puntos: sigma = paso·√2
    static const float w[9]={1,8,28,56,70,56,28,8,1};            // suma 256 en cada eje
    auto node=[&](int i,int j) { const auto it=soilRide.find({i,j}); return it==soilRide.end()?0.0f:it->second; };
    float depth=0;
    for (int j=0;j<9;++j) for (int i=0;i<9;++i) {
        const float fx=(x+(i-4)*step-origin)/cell, fz=(z+(j-4)*step-origin)/cell;
        const int ix=(int)std::floor(fx), iz=(int)std::floor(fz);
        const float tx=fx-ix,tz=fz-iz;
        const float a=node(ix,iz),b=node(ix+1,iz),c=node(ix,iz+1),d=node(ix+1,iz+1);
        const float v=tx>tz ? a+(b-a)*tx+(d-b)*tz : a+(d-c)*tx+(c-a)*tz;   // misma triangulación que SoilDelta
        if (v<0.0f) depth-=w[i]*w[j]*v;                          // sólo cuenta lo hundido
    }
    depth/=65536.0f;
    if (rideDepth>0.0f) depth=rideDepth*std::tanh(depth/rideDepth);
    return -depth;
}

// El hundido que sienten las ruedas llega con demora: una huella nueva baja el suelo bajo la cubierta 5-7 cm
// en 4 pasos de física (2 m/s), 40 veces más rápido de lo que la cubierta puede acompañar sin perder el
// apoyo (con 20000 N·s/m de amortiguación al descomprimirse, el suelo que se aleja a más de ~5 cm/s la deja
// sin fuerza): la rueda rebotaba y una moto al límite en una curva se caía. Cada nodo sigue al de la malla a
// rideSink m/s como mucho: pasando encima no se siente cómo se cava; parada, la rueda se hunde de a poco.
void Terrain::StepRide(float dt)
{
    if (rideActive.empty() || rideSoil<=0.0f) return;
    const float maxStep=rideSink>0.0f?rideSink*dt:1e9f;            // m/s de hundimiento (0: al instante)
    for (auto it=rideActive.begin();it!=rideActive.end();) {
        const auto real=soilHeights.find(*it);
        float& cur=soilRide[*it];
        const float target=real==soilHeights.end()?0.0f:real->second;
        const float d=target-cur;
        if (std::fabs(d)<=maxStep) { cur=target; it=rideActive.erase(it); }
        else { cur+=d>0?maxStep:-maxStep; ++it; }
    }
}

float Terrain::RideHeight(float x,float z) const
{
    if (rideSoil<=0.0f) return Height(x,z);
    return BaseHeight(x,z)+RideDelta(x,z);
}

JPH::Vec3 Terrain::BankNormal(float x,float z) const
{
    if (rideSoil<=0.0f) return Normal(x,z);
    return JPH::Vec3(BaseHeight(x-Cell,z)-BaseHeight(x+Cell,z),2.0f*Cell,BaseHeight(x,z-Cell)-BaseHeight(x,z+Cell)).Normalized();
}

JPH::Vec3 Terrain::RideNormal(float x,float z) const
{
    if (soilHeights.empty() || rideSoil<=0.0f) return Normal(x,z);
    float dx=BaseHeight(x-Cell,z)-BaseHeight(x+Cell,z);
    float dz=BaseHeight(x,z-Cell)-BaseHeight(x,z+Cell);
    // Pendiente del hundido a +-10 cm (diferencias finitas, como Normal): la derivada exacta de esta suma de
    // muestras lineales salta en cada borde de triángulo y le mete ruido a la fuerza lateral de la cubierta.
    const float e=0.10f;
    dx+=(RideDelta(x-e,z)-RideDelta(x+e,z))*Cell/e;
    dz+=(RideDelta(x,z-e)-RideDelta(x,z+e))*Cell/e;
    return JPH::Vec3(dx,2.0f*Cell,dz).Normalized();
}

void Terrain::MarkSoilNode(int x,int z)
{
    soilChanged.insert({x,z});
    const int span=soilTileCells*soilSubdivision;
    // Incluye ambos lados de las costuras y las normales vecinas.
    for (int iz=std::max(0,(z-1)/span);iz<=(z+1)/span;++iz)
        for (int ix=std::max(0,(x-1)/span);ix<=(x+1)/span;++ix)
            if (ix*soilTileCells<N-1 && iz*soilTileCells<N-1) soilDirty.insert({ix,iz});
}

void Terrain::PressSoil(float x,float z,float hx,float hz,float load,float slipSpeed,
                        float softness,float maxDepth,float dt,float rollingSpeed)
{
    if (dt<=0 || load<=50 || softness<=0 || maxDepth<=0 || SoilAmount(x,z)<=0) return;
    const float len=std::hypot(hx,hz);
    // El límite protege taludes naturales, no las paredes creadas por nuestras propias huellas.
    const JPH::Vec3 baseNormal(BaseHeight(x-Cell,z)-BaseHeight(x+Cell,z),2*Cell,
                               BaseHeight(x,z-Cell)-BaseHeight(x,z+Cell));
    if (len<1e-5f || baseNormal.Normalized().GetY()<0.55f) return;
    hx/=len; hz/=len;
    softness=std::clamp(softness,0.0f,1.0f); maxDepth=std::clamp(maxDepth,0.0f,0.35f);
    const float pressure=std::clamp(load/1100.0f,0.0f,2.0f),shear=std::clamp(slipSpeed/7,0.0f,3.0f);
    const float rolling=std::clamp(rollingSpeed,0.0f,40.0f);
    const float rollContact=mu::Smoothstep(0.15f,1.0f,rolling);
    const float cell=SoilCell(),across=0.105f,along=0.18f,radius=0.34f;
    const int end=(N-1)*soilSubdivision;
    const int x0=std::max(1,(int)std::floor((x-radius-origin)/cell));
    const int x1=std::min(end-1,(int)std::ceil((x+radius-origin)/cell));
    const int z0=std::max(1,(int)std::floor((z-radius-origin)/cell));
    const int z1=std::min(end-1,(int)std::ceil((z+radius-origin)/cell));
    struct Bank { SoilKey key; float weight; };
    std::vector<Bank> banks;
    float removed=0,weights=0;
    for (int iz=z0;iz<=z1;++iz) for (int ix=x0;ix<=x1;++ix) {
        const float wx=origin+ix*cell,wz=origin+iz*cell;
        const float dx=wx-x,dz=wz-z,side=(dx*hz-dz*hx)/across,forward=(dx*hx+dz*hz)/along;
        const float ground=SoilAmount(wx,wz),r=side*side+forward*forward;
        if (ground<=0) continue;
        if (r<1) {
            const float profile=(1-r)*(1-r);
            // Los tacos dejan variación longitudinal suave; nunca un escalón de medio metro.
            const float tread=0.92f+0.08f*std::cos((wx*hx+wz*hz)*62.83185f);
            const float limit=std::min(maxDepth,0.025f+0.075f*rollContact+shear*maxDepth*0.8f)*softness*ground;
            const float before=SoilNode(ix,iz,true);
            const float amount=std::min(std::max(0.0f,before+limit*profile*tread),
                (0.04f+0.55f*rolling+0.22f*shear)*pressure*softness*ground*profile*std::min(dt,0.05f));
            if (amount>0) { soilPending[{ix,iz}]=before-amount; removed+=amount; MarkSoilNode(ix,iz); }
        } else {
            const float ridge=std::max(0.0f,1-std::fabs(std::fabs(side)-1.7f)/1.0f);
            const float w=ridge*ridge*std::max(0.0f,1-forward*forward/1.96f)*ground;
            if (w>0) { banks.push_back({{ix,iz},w}); weights+=w; }
        }
    }
    for (const Bank& b:banks) {
        const float before=SoilNode(b.key.first,b.key.second,true);
        const float add=std::min(std::max(0.0f,maxDepth*0.35f-before),removed*0.6f*b.weight/std::max(weights,1e-6f));
        if (add>0) { soilPending[b.key]=before+add; MarkSoilNode(b.key.first,b.key.second); }
    }
    if (removed>0) deformed=true;
}

JPH::Ref<JPH::HeightFieldShape> Terrain::SoilShape(int x,int z,int width,int height,int subdiv,bool pending) const
{
    const int w=width*subdiv+1,h=height*subdiv+1,n=(std::max(w,h)+3)/4*4;
    const float spacing=Cell/subdiv;
    std::vector<float> data(n*n,JPH::HeightFieldShapeConstants::cNoCollisionValue);
    for (int iz=0;iz<h;++iz) for (int ix=0;ix<w;++ix) {
        const float wx=origin+x*Cell+ix*spacing,wz=origin+z*Cell+iz*spacing;
        data[iz*n+ix]=BaseHeight(wx,wz)+(subdiv==1?0:SoilNode(x*subdiv+ix,z*subdiv+iz,pending));
    }
    JPH::HeightFieldShapeSettings settings(data.data(),JPH::Vec3(origin+x*Cell,0,origin+z*Cell),JPH::Vec3(spacing,1,spacing),n);
    settings.mBitsPerSample=16;
    auto result=settings.Create();
    if (result.HasError()) throw std::runtime_error("Suelo: "+std::string(result.GetError().c_str()));
    return static_cast<JPH::HeightFieldShape*>(const_cast<JPH::Shape*>(result.Get().GetPtr()));
}

void Terrain::CommitFineSoil(PhysicsWorld& world)
{
    if (soilDirty.empty()) return;
    const int chunkCount=(N-2)/coarseChunk+1;
    // Dos niveles: grilla original lejos; sólo los sectores pisados tienen muestras de 5 cm.
    if (!soilRoot) {
        soilRoot=new JPH::MutableCompoundShape();
        for (int z=0;z<N-1;z+=coarseChunk) for (int x=0;x<N-1;x+=coarseChunk)
            soilRoot->AddShape(JPH::Vec3::sZero(),JPH::Quat::sIdentity(),
                SoilShape(x,z,std::min(coarseChunk,N-1-x),std::min(coarseChunk,N-1-z),1,false));
    }
    std::set<SoilKey> parents;
    for (const SoilKey& key:soilDirty) {
        const int x=key.first*soilTileCells,z=key.second*soilTileCells;
        const SoilKey parent={x/coarseChunk,z/coarseChunk};
        auto& branch=soilBranches[parent];
        const int px=parent.first*coarseChunk,pz=parent.second*coarseChunk;
        const int pw=std::min(coarseChunk,N-1-px),ph=std::min(coarseChunk,N-1-pz);
        if (!branch) {
            branch=new JPH::MutableCompoundShape();
            for (int iz=0;iz<ph;iz+=soilTileCells) for (int ix=0;ix<pw;ix+=soilTileCells)
                branch->AddShape(JPH::Vec3::sZero(),JPH::Quat::sIdentity(),
                    SoilShape(px+ix,pz+iz,std::min(soilTileCells,pw-ix),std::min(soilTileCells,ph-iz),1,false));
        }
        const int index=((z-pz)/soilTileCells)*((pw+soilTileCells-1)/soilTileCells)+(x-px)/soilTileCells;
        branch->ModifyShape(index,JPH::Vec3::sZero(),JPH::Quat::sIdentity(),
            SoilShape(x,z,std::min(soilTileCells,N-1-x),std::min(soilTileCells,N-1-z),soilSubdivision,true));
        parents.insert(parent);
        soilTiles.try_emplace(key);
    }
    for (const SoilKey& parent:parents)
        soilRoot->ModifyShape(parent.second*chunkCount+parent.first,JPH::Vec3::sZero(),JPH::Quat::sIdentity(),soilBranches[parent]);
    if (world.Bodies().GetShape(body)!=soilRoot.GetPtr())
        world.Bodies().SetShape(body,soilRoot,false,JPH::EActivation::DontActivate);
    else world.Bodies().NotifyShapeChanged(body,soilRoot->GetCenterOfMass(),false,JPH::EActivation::DontActivate);
    // Publicar exactamente las mismas muestras que acaba de recibir Jolt.
    for (const SoilKey& key:soilChanged) { soilHeights[key]=soilPending.at(key); if (rideSoil>0.0f) rideActive.insert(key); }
    soilChanged.clear();
    ++deformationRevision;
    if (!chunks.empty()) {
        for (const SoilKey& key:soilDirty) if (soilTiles[key].mesh) UpdateSoilMesh(key);
    }
    soilDirty.clear();
}

void Terrain::UpdateSoilMesh(SoilKey key)
{
    const int x=key.first*soilTileCells,z=key.second*soilTileCells;
    const int w=std::min(soilTileCells,N-1-x)*soilSubdivision+1;
    const int h=std::min(soilTileCells,N-1-z)*soilSubdivision+1;
    Mesh*& ptr=soilTiles[key].mesh;
    const bool fresh=ptr==nullptr;
    if (fresh) {
        ptr=new Mesh{};
        ptr->vertexCount=w*h; ptr->triangleCount=(w-1)*(h-1)*2;
        ptr->vertices=(float*)MemAlloc(w*h*3*sizeof(float));
        ptr->normals=(float*)MemAlloc(w*h*3*sizeof(float));
        ptr->texcoords=(float*)MemAlloc(w*h*2*sizeof(float));
        ptr->texcoords2=(float*)MemAlloc(w*h*2*sizeof(float));
        ptr->colors=(unsigned char*)MemAlloc(w*h*4);
        ptr->indices=(unsigned short*)MemAlloc(ptr->triangleCount*3*sizeof(unsigned short));
        int t=0;
        for (int iz=0;iz<h-1;++iz) for (int ix=0;ix<w-1;++ix) {
            const int a=iz*w+ix,b=a+1,c=a+w,d=c+1;
            for (int v:{a,c,d,a,d,b}) ptr->indices[t++]=(unsigned short)v;
        }
    }
    Mesh& m=*ptr;
    for (int iz=0;iz<h;++iz) for (int ix=0;ix<w;++ix) {
        const int v=iz*w+ix;
        const float wx=origin+x*Cell+ix*SoilCell(),wz=origin+z*Cell+iz*SoilCell();
        const auto normal=Normal(wx,wz);
        m.vertices[v*3]=wx; m.vertices[v*3+1]=Height(wx,wz); m.vertices[v*3+2]=wz;
        m.normals[v*3]=normal.GetX(); m.normals[v*3+1]=normal.GetY(); m.normals[v*3+2]=normal.GetZ();
        m.texcoords2[v*2]=SoilDelta(wx,wz); m.texcoords2[v*2+1]=1.0f;
        if (fresh) {
            m.texcoords[v*2]=(wx-origin)/Size(); m.texcoords[v*2+1]=(wz-origin)/Size();
            // Interpolar el color de la malla original conserva todos los biomas y mapas.
            const float gx=x+(float)ix/soilSubdivision,gz=z+(float)iz/soilSubdivision;
            const int cx=std::min((int)gx,N-2),cz=std::min((int)gz,N-2);
            const int cols=(N-2)/coarseChunk+1;
            const Chunk& c=chunks[(cz/coarseChunk)*cols+cx/coarseChunk];
            const int a=(cz-c.z0)*c.w+cx-c.x0;
            const float tx=gx-cx,tz=gz-cz;
            for (int k=0;k<4;++k) {
                const auto* colors=c.mesh->colors;
                const float top=mu::Lerp((float)colors[a*4+k],(float)colors[(a+1)*4+k],tx);
                const float bottom=mu::Lerp((float)colors[(a+c.w)*4+k],(float)colors[(a+c.w+1)*4+k],tx);
                m.colors[v*4+k]=(unsigned char)mu::Lerp(top,bottom,tz);
            }
        }
    }
    if (fresh) UploadMesh(&m,true);
    else {
        UpdateMeshBuffer(m,0,m.vertices,w*h*3*sizeof(float),0);
        UpdateMeshBuffer(m,2,m.normals,w*h*3*sizeof(float),0);
        UpdateMeshBuffer(m,5,m.texcoords2,w*h*2*sizeof(float),0);
    }
}

void Terrain::FilterCoarseMeshes()
{
    for (Chunk& c:chunks) {
        const int stride=coarseChunk/soilTileCells;
        bool hidden[coarseChunk*coarseChunk]{};
        for (int z=0;z<stride;++z) for (int x=0;x<stride;++x) {
            const auto tile=soilTiles.find({c.x0/soilTileCells+x,c.z0/soilTileCells+z});
            hidden[z*stride+x]=tile!=soilTiles.end() && tile->second.mesh;
        }
        int t=0;
        for (int z=0;z<c.h-1;++z) for (int x=0;x<c.w-1;++x) {
            if (hidden[(z/soilTileCells)*stride+x/soilTileCells]) continue;
            const int a=z*c.w+x,b=a+1,d=a+c.w+1,e=a+c.w;
            for (int v:{a,e,d,a,d,b}) c.mesh->indices[t++]=(unsigned short)v;
        }
        // Puede cambiar qué casillas se excluyen sin cambiar la cantidad: subir siempre.
        c.mesh->triangleCount=t/3;
        if (t) UpdateMeshBuffer(*c.mesh,6,c.mesh->indices,t*sizeof(unsigned short),0);
    }
}

void Terrain::ClearFineSoil()
{
    for (auto& entry:soilTiles) if (entry.second.mesh) { UnloadMesh(*entry.second.mesh); delete entry.second.mesh; }
    soilTiles.clear(); soilDirty.clear(); soilChanged.clear(); soilPending.clear(); soilHeights.clear(); soilRide.clear(); rideActive.clear();
    soilBranches.clear(); soilRoot=nullptr;
}

void Terrain::UpdateSoilVisibility(float x,float z)
{
    // Caché de GPU acotada; las alturas y colisiones persisten aunque el sector quede lejos.
    std::vector<std::pair<float,SoilKey>> candidates;
    for (const auto& entry:soilTiles) {
        const float wx=origin+(entry.first.first+0.5f)*soilTileCells*Cell;
        const float wz=origin+(entry.first.second+0.5f)*soilTileCells*Cell;
        const float distance=std::hypot(wx-x,wz-z);
        if (distance<80) candidates.push_back({distance,entry.first});
    }
    std::sort(candidates.begin(),candidates.end());
    if (candidates.size()>128) candidates.resize(128);
    std::set<SoilKey> visible;
    for (const auto& entry:candidates) visible.insert(entry.second);
    bool changed=false;
    for (auto& entry:soilTiles) if (entry.second.mesh && !visible.count(entry.first)) {
        UnloadMesh(*entry.second.mesh); delete entry.second.mesh; entry.second.mesh=nullptr; changed=true;
    }
    int uploads=0;
    for (const auto& entry:candidates) if (!soilTiles[entry.second].mesh && uploads<8) {
        UpdateSoilMesh(entry.second); ++uploads; changed=true;
    }
    if (changed) FilterCoarseMeshes();
}
