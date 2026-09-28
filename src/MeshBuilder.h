#pragma once
// Armado de mallas por código: vértices con normal, UV y color. Una malla armada se puede sumar a
// otra transformada y teñida, así una pieza hecha de muchas primitivas se dibuja de una sola vez.
#include "raylib.h"
#include "raymath.h"

#include <cstring>
#include <utility>
#include <vector>

struct MeshBuilder {
    std::vector<float> pos, nrm, uv;
    std::vector<unsigned char> col;
    std::vector<unsigned short> idx;

    int VertexCount() const { return (int)(pos.size() / 3); }
    Vector3 P(int i) const { return {pos[3 * i], pos[3 * i + 1], pos[3 * i + 2]}; }
    Vector3 N(int i) const { return {nrm[3 * i], nrm[3 * i + 1], nrm[3 * i + 2]}; }

    unsigned short Vertex(Vector3 p, Vector3 n, Vector2 t = {0.0f, 0.0f}, Color c = WHITE)
    {
        pos.insert(pos.end(), {p.x, p.y, p.z});
        nrm.insert(nrm.end(), {n.x, n.y, n.z});
        uv.insert(uv.end(), {t.x, t.y});
        col.insert(col.end(), {c.r, c.g, c.b, c.a});
        return (unsigned short)(pos.size() / 3 - 1);
    }
    void Tri(unsigned short a, unsigned short b, unsigned short c) { idx.insert(idx.end(), {a, b, c}); }

    // Da vuelta los triángulos (desde el índice `first`) cuya cara no mira para el lado de sus normales:
    // los generadores no tienen que cuidar el orden de los vértices.
    void FixWinding(size_t first = 0)
    {
        for (size_t i = first; i + 2 < idx.size(); i += 3) {
            const Vector3 a = P(idx[i]), b = P(idx[i + 1]), c = P(idx[i + 2]);
            const Vector3 face = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
            const Vector3 n = Vector3Add(Vector3Add(N(idx[i]), N(idx[i + 1])), N(idx[i + 2]));
            if (Vector3DotProduct(face, n) < 0.0f) std::swap(idx[i + 1], idx[i + 2]);
        }
    }

    // Suma otra malla transformada por m (normales con la inversa transpuesta; si m espeja, se invierte
    // el orden de los triángulos) y con los colores multiplicados por tint. gloss >= 0: brillo propio
    // de estos vértices, que viaja en la UV (u = brillo, v = 2; ver kLitFS).
    void Append(const MeshBuilder& o, const Matrix& m, Color tint = WHITE, float gloss = -1.0f)
    {
        const Matrix inv = MatrixInvert(m);
        const bool mirrored = MatrixDeterminant(m) < 0.0f;
        const unsigned base = (unsigned)VertexCount();
        for (int i = 0; i < o.VertexCount(); ++i) {
            const Vector3 n = o.N(i);
            // Inversa transpuesta: la fila k de inv es la columna k de su transpuesta.
            Vector3 tn = {inv.m0 * n.x + inv.m1 * n.y + inv.m2 * n.z, inv.m4 * n.x + inv.m5 * n.y + inv.m6 * n.z,
                          inv.m8 * n.x + inv.m9 * n.y + inv.m10 * n.z};
            tn = Vector3Normalize(tn);
            const unsigned char* c = &o.col[4 * i];
            const Color cc = {(unsigned char)(c[0] * tint.r / 255), (unsigned char)(c[1] * tint.g / 255),
                              (unsigned char)(c[2] * tint.b / 255), 255};
            const Vector2 t = gloss >= 0.0f ? Vector2{gloss, 2.0f} : Vector2{o.uv[2 * i], o.uv[2 * i + 1]};
            Vertex(Vector3Transform(o.P(i), m), tn, t, cc);
        }
        for (size_t i = 0; i + 2 < o.idx.size(); i += 3) {
            const unsigned short a = (unsigned short)(base + o.idx[i]);
            const unsigned short b = (unsigned short)(base + o.idx[i + 1]);
            const unsigned short c = (unsigned short)(base + o.idx[i + 2]);
            if (mirrored) Tri(a, c, b);
            else Tri(a, b, c);
        }
    }

    Mesh Build()
    {
        if (VertexCount() > 65535) TraceLog(LOG_WARNING, "MeshBuilder: %d vértices, más de los que entran en índices de 16 bits", VertexCount());
        Mesh m{};
        m.vertexCount = VertexCount();
        m.triangleCount = (int)(idx.size() / 3);
        m.vertices = (float*)MemAlloc((unsigned)(pos.size() * sizeof(float)));
        m.normals = (float*)MemAlloc((unsigned)(nrm.size() * sizeof(float)));
        m.texcoords = (float*)MemAlloc((unsigned)(uv.size() * sizeof(float)));
        m.colors = (unsigned char*)MemAlloc((unsigned)col.size());
        m.indices = (unsigned short*)MemAlloc((unsigned)(idx.size() * sizeof(unsigned short)));
        std::memcpy(m.vertices, pos.data(), pos.size() * sizeof(float));
        std::memcpy(m.normals, nrm.data(), nrm.size() * sizeof(float));
        std::memcpy(m.texcoords, uv.data(), uv.size() * sizeof(float));
        std::memcpy(m.colors, col.data(), col.size());
        std::memcpy(m.indices, idx.data(), idx.size() * sizeof(unsigned short));
        UploadMesh(&m, false);
        return m;
    }
};
