// Simple3DEngine - bundled Box2D Lite implementation (subset, C99 port)
// Algorithms follow Erin Catto's "Box2D Lite" (https://github.com/erincatto/box2d,
// zlib license): sequential-impulse solver with accumulated impulses and warm
// starting, SAT clip-plane manifolds for convex polygons, circle manifolds.
// Written from scratch in C so the engine has zero external dependencies.

#include "box2d.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Shape factories
// ---------------------------------------------------------------------------
b2Shape b2MakeCircle(float radius) {
    b2Shape s;
    memset(&s, 0, sizeof(s));
    s.type = B2_SHAPE_CIRCLE;
    s.radius = radius;
    return s;
}

static void compute_polygon_normals(b2Polygon* p) {
    // Ensure CCW winding: signed area > 0 means CCW.
    float area = 0.0f;
    for (int i = 0; i < p->count; i++) {
        int j = (i + 1) % p->count;
        area += 0.5f * b2Cross(p->verts[i], p->verts[j]);
    }
    if (area < 0.0f) { // flip to CCW
        for (int i = 0; i < p->count / 2; i++) {
            b2Vec2 tmp = p->verts[i];
            p->verts[i] = p->verts[p->count - 1 - i];
            p->verts[p->count - 1 - i] = tmp;
        }
    }
    for (int i = 0; i < p->count; i++) {
        b2Vec2 v1 = p->verts[i];
        b2Vec2 v2 = p->verts[(i + 1) % p->count];
        b2Vec2 edge = b2Sub(v2, v1);
        p->normals[i] = b2Normalize(b2V(edge.y, -edge.x)); // outward for CCW
    }
}

b2Shape b2MakeBox(float hx, float hy) {
    b2Shape s;
    memset(&s, 0, sizeof(s));
    s.type = B2_SHAPE_POLYGON;
    s.poly.count = 4;
    s.poly.verts[0] = b2V(-hx, -hy);
    s.poly.verts[1] = b2V( hx, -hy);
    s.poly.verts[2] = b2V( hx,  hy);
    s.poly.verts[3] = b2V(-hx,  hy);
    compute_polygon_normals(&s.poly);
    return s;
}

b2Shape b2MakePolygon(int count, const b2Vec2* verts) {
    b2Shape s;
    memset(&s, 0, sizeof(s));
    s.type = B2_SHAPE_POLYGON;
    if (count > B2_MAX_POLYGON_VERTS) count = B2_MAX_POLYGON_VERTS;
    s.poly.count = count;
    for (int i = 0; i < count; i++) s.poly.verts[i] = verts[i];
    compute_polygon_normals(&s.poly);
    return s;
}

// ---------------------------------------------------------------------------
// Polygon mass properties (centroid & second moment about centroid)
// ---------------------------------------------------------------------------
static float polygon_area(const b2Polygon* p) {
    float area = 0.0f;
    for (int i = 0; i < p->count; i++) {
        int j = (i + 1) % p->count;
        area += 0.5f * b2Cross(p->verts[i], p->verts[j]);
    }
    return fabsf(area);
}

static b2Vec2 polygon_centroid(const b2Polygon* p) {
    b2Vec2 c = b2V(0, 0);
    float area = 0.0f;
    for (int i = 0; i < p->count; i++) {
        int j = (i + 1) % p->count;
        float cross = b2Cross(p->verts[i], p->verts[j]);
        area += cross;
        c = b2Add(c, b2MulS(b2Add(p->verts[i], p->verts[j]), cross));
    }
    if (fabsf(area) < 1e-8f) return b2V(0, 0);
    return b2MulS(c, 1.0f / (3.0f * area));
}

// Second moment about the centroid (unit density), like Box2D's ComputeMassData.
static float polygon_second_moment(const b2Polygon* p, b2Vec2 center) {
    float numerator = 0.0f, denominator = 0.0f;
    for (int i = 0; i < p->count; i++) {
        b2Vec2 e1 = b2Sub(p->verts[i], center);
        b2Vec2 e2 = b2Sub(p->verts[(i + 1) % p->count], center);
        float a = b2Cross(e1, e2);
        numerator   += a * (b2Dot(e1, e1) + b2Dot(e1, e2) + b2Dot(e2, e2));
        denominator += a;
    }
    if (fabsf(denominator) < 1e-8f) return 0.0f;
    return numerator / (6.0f * denominator);
}

void b2SetMassFromDensity(b2Body* b) {
    if (!b) return;
    if (b->type == B2_STATIC) {
        b->mass = 0.0f; b->invMass = 0.0f;
        b->I = 0.0f;    b->invI = 0.0f;
        return;
    }
    // Preserve the original density that produced the current mass.
    float area = (b->shape.type == B2_SHAPE_CIRCLE)
                     ? B2_PI * b->shape.radius * b->shape.radius
                     : polygon_area(&b->shape.poly);
    if (area <= 0.0f) area = 1.0f;
    float density = (b->mass > 0.0f) ? b->mass / area : 1.0f;

    b->mass = area * density;
    if (b->mass <= 0.0f) b->mass = density;
    b->invMass = 1.0f / b->mass;

    if (b->shape.type == B2_SHAPE_CIRCLE) {
        b->I = 0.5f * b->mass * b->shape.radius * b->shape.radius;
    } else {
        b2Vec2 c = polygon_centroid(&b->shape.poly);
        b->I = density * polygon_second_moment(&b->shape.poly, c);
    }
    if (b->I <= 0.0f) b->I = b->mass;
    b->invI = 1.0f / b->I;
}

// ---------------------------------------------------------------------------
// World / body lifecycle
// ---------------------------------------------------------------------------
b2World* b2CreateWorld(float gravityX, float gravityY) {
    b2World* w = (b2World*)calloc(1, sizeof(b2World));
    if (!w) return NULL;
    w->gravity = b2V(gravityX, gravityY);
    w->velocityIterations = 8;
    w->positionCorrection = 0.2f;
    w->allowedPenetration = 0.01f;
    w->nextBodyId = 1;
    w->contactCapacity = 64;
    w->contacts = (b2Contact*)calloc((size_t)w->contactCapacity, sizeof(b2Contact));
    return w;
}

void b2DestroyWorld(b2World* w) {
    if (!w) return;
    b2Body* b = w->bodies;
    while (b) { b2Body* n = b->next; free(b); b = n; }
    free(w->contacts);
    free(w);
}

b2Body* b2CreateBody(b2World* w, const b2BodyDef* def, const b2Shape* shape) {
    if (!w || !def || !shape) return NULL;
    b2Body* b = (b2Body*)calloc(1, sizeof(b2Body));
    if (!b) return NULL;

    b->type = def->type;
    b->shape = *shape;
    b->position = def->position;
    b->rotation = def->rotation;
    b->rot = b2Mat(cosf(def->rotation), sinf(def->rotation));
    b->friction = def->friction;
    b->restitution = def->restitution;
    b->linearDamping = def->linearDamping;
    b->angularDamping = def->angularDamping;
    b->userData = def->userData;
    b->id = w->nextBodyId++;

    if (b->type == B2_DYNAMIC) {
        float d = (def->density > 0.0f) ? def->density : 1.0f;
        float area = (b->shape.type == B2_SHAPE_CIRCLE)
                         ? B2_PI * b->shape.radius * b->shape.radius
                         : polygon_area(&b->shape.poly);
        b->mass = area * d;
        if (b->mass <= 0.0f) b->mass = d;
        b->invMass = 1.0f / b->mass;
        if (b->shape.type == B2_SHAPE_CIRCLE) {
            b->I = 0.5f * b->mass * b->shape.radius * b->shape.radius;
        } else {
            b2Vec2 c = polygon_centroid(&b->shape.poly);
            b->I = d * polygon_second_moment(&b->shape.poly, c);
        }
        if (b->I <= 0.0f) b->I = b->mass;
        b->invI = 1.0f / b->I;
    }

    b->next = w->bodies;
    w->bodies = b;
    w->bodyCount++;
    return b;
}

void b2DestroyBody(b2World* w, b2Body* body) {
    if (!w || !body) return;
    b2Body** prev = &w->bodies;
    while (*prev) {
        if (*prev == body) { *prev = body->next; break; }
        prev = &(*prev)->next;
    }
    w->bodyCount--;
    for (int i = 0; i < w->contactCount; i++) {
        b2Contact* c = &w->contacts[i];
        if (c->bodyA == body || c->bodyB == body) {
            c->alive = 0; c->pointCount = 0; c->bodyA = NULL; c->bodyB = NULL;
        }
    }
    free(body);
}

b2Body* b2GetBodyById(b2World* w, int id) {
    if (!w) return NULL;
    for (b2Body* b = w->bodies; b; b = b->next)
        if (b->id == id) return b;
    return NULL;
}

void b2ApplyForce(b2Body* b, b2Vec2 force, b2Vec2 worldPoint) {
    if (!b || b->type != B2_DYNAMIC) return;
    b->force = b2Add(b->force, force);
    b2Vec2 r = b2Sub(worldPoint, b->position);
    b->torque += b2Cross(r, force);
}

void b2ApplyImpulse(b2Body* b, b2Vec2 impulse, b2Vec2 worldPoint) {
    if (!b || b->type != B2_DYNAMIC) return;
    b->velocity = b2Add(b->velocity, b2MulS(impulse, b->invMass));
    b2Vec2 r = b2Sub(worldPoint, b->position);
    b->angularVelocity += b->invI * b2Cross(r, impulse);
}

void b2SetTransform(b2Body* b, b2Vec2 pos, float rotRad) {
    if (!b) return;
    b->position = pos;
    b->rotation = rotRad;
    b->rot = b2Mat(cosf(rotRad), sinf(rotRad));
}

// ---------------------------------------------------------------------------
// Broadphase: AABB overlap
// ---------------------------------------------------------------------------
typedef struct b2AABB { b2Vec2 lo, hi; } b2AABB;

static b2AABB compute_aabb(const b2Body* b) {
    b2AABB a;
    if (b->shape.type == B2_SHAPE_CIRCLE) {
        float r = b->shape.radius;
        a.lo = b2Sub(b->position, b2V(r, r));
        a.hi = b2Add(b->position, b2V(r, r));
    } else {
        b2Vec2 lo = b2V(1e18f, 1e18f), hi = b2V(-1e18f, -1e18f);
        for (int i = 0; i < b->shape.poly.count; i++) {
            b2Vec2 v = b2Add(b->position, b2MulM(b->rot, b->shape.poly.verts[i]));
            if (v.x < lo.x) lo.x = v.x;
            if (v.y < lo.y) lo.y = v.y;
            if (v.x > hi.x) hi.x = v.x;
            if (v.y > hi.y) hi.y = v.y;
        }
        a.lo = lo; a.hi = hi;
    }
    return a;
}

static int aabb_overlap(b2AABB a, b2AABB b) {
    if (a.hi.x < b.lo.x || b.hi.x < a.lo.x) return 0;
    if (a.hi.y < b.lo.y || b.hi.y < a.lo.y) return 0;
    return 1;
}

// ---------------------------------------------------------------------------
// Narrowphase: SAT manifolds (Box2D Lite style)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Narrowphase: SAT manifolds, exactly in the style of Erin Catto's Box2D Lite
// (FindMaxSeparation -> reference/incident faces -> ClipSegmentToMax).
// ---------------------------------------------------------------------------

// Find A's polygon face whose outward normal maximizes the separation from
// B's vertices. Returns that max separation (>=0 means gap along best axis).
static void find_max_separation(const b2Body* A, const b2Body* B,
                                int* outFaceIndex, float* outSeparation) {
    const b2Polygon* pa = &A->shape.poly;
    const b2Polygon* pb = &B->shape.poly;
    float bestSep = -1e18f;
    int bestFace = 0;

    for (int i = 0; i < pa->count; i++) {
        b2Vec2 n = b2MulM(A->rot, pa->normals[i]);
        b2Vec2 v1 = b2Add(A->position, b2MulM(A->rot, pa->verts[i]));
        // minimum signed distance of B's vertices to this face plane
        float minDist = 1e18f;
        for (int k = 0; k < pb->count; k++) {
            b2Vec2 vb = b2Add(B->position, b2MulM(B->rot, pb->verts[k]));
            float d = b2Dot(n, b2Sub(vb, v1));
            if (d < minDist) minDist = d;
        }
        if (minDist > bestSep) { bestSep = minDist; bestFace = i; }
    }
    *outFaceIndex = bestFace;
    *outSeparation = bestSep;
}

// Keep the portion of the segment where dot(normal,p) <= offset, replacing
// clipped endpoints with edge-plane intersections (Box2D Lite clip trick).
// Returns new point count written back into pts[0..n-1].
static int clip_segment(b2Vec2 pts[2], b2Vec2 normal, float offset) {
    float d0 = b2Dot(normal, pts[0]) - offset;
    float d1 = b2Dot(normal, pts[1]) - offset;
    int n = 0;
    b2Vec2 outp[2];
    if (d0 <= 0.0f) outp[n++] = pts[0];
    if (d1 <= 0.0f) outp[n++] = pts[1];
    if (d0 * d1 < 0.0f) {
        float t = d0 / (d0 - d1);
        outp[n++] = b2Add(pts[0], b2MulS(b2Sub(pts[1], pts[0]), t));
    }
    if (n >= 2) { pts[0] = outp[0]; pts[1] = outp[1]; return 2; }
    if (n == 1) { pts[0] = outp[0]; pts[1] = outp[0]; return 1; }
    return 0;
}

static int collide_poly_poly(const b2Body* A, const b2Body* B,
                             b2ContactPoint* out, int maxOut) {
    int faceA, faceB;
    float sepA, sepB;
    find_max_separation(A, B, &faceA, &sepA);
    if (sepA > 0.0f) return 0;            // separating axis found
    find_max_separation(B, A, &faceB, &sepB);
    if (sepB > 0.0f) return 0;

    // Reference polygon: the one with LARGER separation (less deep).
    const b2Body *ref, *inc;
    int refFace;
    b2Vec2 refNormal;                     // world, points out of ref toward inc
    int flip;                             // 1 when ref == B => negate for A->B
    const float tol = 0.1f;
    if (sepB > sepA + tol) {
        ref = B; inc = A; refFace = faceB;
        refNormal = b2MulM(B->rot, B->shape.poly.normals[faceB]);
        flip = 1;
    } else {
        ref = A; inc = B; refFace = faceA;
        refNormal = b2MulM(A->rot, A->shape.poly.normals[faceA]);
        flip = 0;
    }

    const b2Polygon* rp = &ref->shape.poly;
    const b2Polygon* ip = &inc->shape.poly;

    // Incident face: inc face most anti-parallel to the reference normal.
    int incFace = 0; float minDot = 1e18f;
    for (int i = 0; i < ip->count; i++) {
        b2Vec2 n = b2MulM(inc->rot, ip->normals[i]);
        float d = b2Dot(n, refNormal);
        if (d < minDot) { minDot = d; incFace = i; }
    }

    // Reference face endpoints & tangent (world).
    b2Vec2 rv1 = b2Add(ref->position, b2MulM(ref->rot, rp->verts[refFace]));
    b2Vec2 rv2 = b2Add(ref->position, b2MulM(ref->rot, rp->verts[(refFace + 1) % rp->count]));
    b2Vec2 tangent = b2Normalize(b2Sub(rv2, rv1));

    // Incident face endpoints (world), then clip against side planes.
    b2Vec2 pts[2];
    pts[0] = b2Add(inc->position, b2MulM(inc->rot, ip->verts[incFace]));
    pts[1] = b2Add(inc->position, b2MulM(inc->rot, ip->verts[(incFace + 1) % ip->count]));

    if (clip_segment(pts, b2Neg(tangent), -b2Dot(tangent, rv1)) < 2) return 0;
    if (clip_segment(pts, tangent,      b2Dot(tangent, rv2)) < 2) return 0;

    // Keep only points at or below the reference face plane.
    float refOffset = b2Dot(refNormal, rv1);
    int produced = 0;
    for (int i = 0; i < 2 && produced < maxOut; i++) {
        float sep = b2Dot(refNormal, pts[i]) - refOffset;
        if (sep <= 0.0f) {
            out[produced].position = pts[i];
            out[produced].separation = sep;
            // Normal convention: from body A toward body B.
            out[produced].normal = flip ? b2Neg(refNormal) : refNormal;
            produced++;
        }
    }
    return produced;
}

static int collide_circle_circle(const b2Body* A, const b2Body* B,
                                 b2ContactPoint* out, int maxOut) {
    if (maxOut < 1) return 0;
    b2Vec2 d = b2Sub(B->position, A->position);
    float dist = b2Length(d);
    float rr = A->shape.radius + B->shape.radius;
    if (dist >= rr) return 0;
    b2Vec2 n = (dist > 1e-8f) ? b2MulS(d, 1.0f / dist) : b2V(0, 1);
    out[0].normal = n; // A -> B
    out[0].separation = dist - rr;
    out[0].position = b2Add(A->position, b2MulS(n, A->shape.radius + out[0].separation * 0.5f));
    return 1;
}

// Circle vs convex polygon: pick the polygon face of maximum separation to
// the circle center; handle vertex/face regions like Box2D Lite CollideCircle.
static int collide_circle_poly(const b2Body* circle, const b2Body* poly,
                               b2ContactPoint* out, int maxOut, int circleIsA) {
    if (maxOut < 1) return 0;
    const b2Polygon* p = &poly->shape.poly;
    b2Vec2 c = b2MulMt(poly->rot, b2Sub(circle->position, poly->position));

    int face = 0; float maxSep = -1e18f;
    for (int i = 0; i < p->count; i++) {
        float s = b2Dot(p->normals[i], b2Sub(c, p->verts[i]));
        if (s > maxSep) { maxSep = s; face = i; }
    }

    b2Vec2 v1 = p->verts[face];
    b2Vec2 v2 = p->verts[(face + 1) % p->count];
    b2Vec2 seg = b2Sub(v2, v1);
    float segLenSq = b2LengthSq(seg);
    b2Vec2 segDir = (segLenSq > 1e-16f) ? b2MulS(seg, 1.0f / sqrtf(segLenSq)) : b2V(1, 0);

    float u1 = b2Dot(b2Sub(c, v1), segDir);   // projection along edge
    float u2 = b2Dot(b2Sub(v2, c), segDir);   // remaining length past center
    b2Vec2 closestLocal;
    if (u1 <= 0.0f)      closestLocal = v1;                        // v1 region
    else if (u2 <= 0.0f) closestLocal = v2;                        // v2 region
    else if (maxSep > circle->shape.radius) return 0;              // face region, gap
    else                 closestLocal = b2Add(v1, b2MulS(segDir, u1)); // on face

    b2Vec2 closestWorld = b2Add(poly->position, b2MulM(poly->rot, closestLocal));
    b2Vec2 d = b2Sub(circle->position, closestWorld);
    float dist = b2Length(d);
    if (dist >= circle->shape.radius) return 0;

    b2Vec2 nPolyToCircle = (dist > 1e-8f) ? b2MulS(d, 1.0f / dist)
                                          : b2MulM(poly->rot, p->normals[face]);
    if (circleIsA) {
        out[0].normal = b2Neg(nPolyToCircle); // A(circle) -> B(poly)
        out[0].position = b2Add(circle->position, b2MulS(out[0].normal, circle->shape.radius));
    } else {
        out[0].normal = nPolyToCircle;        // A(poly) -> B(circle)
        out[0].position = closestWorld;
    }
    out[0].separation = dist - circle->shape.radius;
    return 1;
}

static int collide_shapes(const b2Body* A, const b2Body* B,
                          b2ContactPoint* out, int maxOut) {
    if (A->shape.type == B2_SHAPE_POLYGON && B->shape.type == B2_SHAPE_POLYGON)
        return collide_poly_poly(A, B, out, maxOut);
    if (A->shape.type == B2_SHAPE_CIRCLE && B->shape.type == B2_SHAPE_CIRCLE)
        return collide_circle_circle(A, B, out, maxOut);
    if (A->shape.type == B2_SHAPE_CIRCLE)
        return collide_circle_poly(A, B, out, maxOut, 1);
    if (B->shape.type == B2_SHAPE_CIRCLE)
        return collide_circle_poly(B, A, out, maxOut, 0);
    return 0;
}

// ---------------------------------------------------------------------------
// Contact bookkeeping (warm starting via pair key)
// ---------------------------------------------------------------------------
static b2Contact* find_or_add_contact(b2World* w, b2Body* A, b2Body* B, int* outNew) {
    long long key = (A->id < B->id)
                        ? ((long long)A->id * 1000000LL + B->id)
                        : ((long long)B->id * 1000000LL + A->id);
    for (int i = 0; i < w->contactCount; i++) {
        b2Contact* c = &w->contacts[i];
        // Match by pair key regardless of the alive flag (it was cleared at
        // the start of this narrowphase pass); persistent state such as
        // wasTouching and warm-start impulses lives in the found contact.
        if ((long long)c->key == key) { *outNew = 0; return c; }
    }
    if (w->contactCount >= w->contactCapacity) {
        int newCap = w->contactCapacity * 2;
        b2Contact* nc = (b2Contact*)realloc(w->contacts, (size_t)newCap * sizeof(b2Contact));
        if (!nc) return NULL;
        memset(nc + w->contactCapacity, 0,
               (size_t)(newCap - w->contactCapacity) * sizeof(b2Contact));
        w->contacts = nc;
        w->contactCapacity = newCap;
    }
    b2Contact* c = &w->contacts[w->contactCount++];
    memset(c, 0, sizeof(*c));
    c->key = (int)key;
    c->bodyA = A; c->bodyB = B;
    c->alive = 1;
    *outNew = 1;
    return c;
}

// ---------------------------------------------------------------------------
// Solver (sequential impulses with accumulated impulses, as in Box2D Lite)
// ---------------------------------------------------------------------------
static inline float mix_restitution(float a, float b) { return (a > b) ? a : b; }
static inline float mix_friction(float a, float b) { return sqrtf(a * b); }

static void prestep_contact(b2Contact* c) {
    b2Body* A = c->bodyA;
    b2Body* B = c->bodyB;
    float invSum = A->invMass + B->invMass;
    float e = mix_restitution(A->restitution, B->restitution);

    for (int i = 0; i < c->pointCount; i++) {
        b2ContactPoint* cp = &c->points[i];
        b2Vec2 ra = b2Sub(cp->position, A->position);
        b2Vec2 rb = b2Sub(cp->position, B->position);
        cp->r = ra; cp->rB = rb;
        b2Vec2 n = cp->normal;
        b2Vec2 t = b2V(-n.y, n.x);

        float rnA = b2Cross(ra, n), rnB = b2Cross(rb, n);
        float kn = invSum + A->invI * rnA * rnA + B->invI * rnB * rnB;
        cp->massNormal = (kn > 0.0f) ? 1.0f / kn : 0.0f;

        float rtA = b2Cross(ra, t), rtB = b2Cross(rb, t);
        float kt = invSum + A->invI * rtA * rtA + B->invI * rtB * rtB;
        cp->massTangent = (kt > 0.0f) ? 1.0f / kt : 0.0f;

        // Restitution bias from the approach velocity captured at pre-step
        // time (narrowphase runs before force integration, so this sees the
        // true impact velocity). The threshold suppresses micro-bounce jitter
        // for resting contacts, exactly like Box2D Lite's vRel < -threshold.
        b2Vec2 vA = b2Add(A->velocity, b2CrossVS(ra, A->angularVelocity));
        b2Vec2 vB = b2Add(B->velocity, b2CrossVS(rb, B->angularVelocity));
        float vn = b2Dot(b2Sub(vB, vA), n);
        cp->restitution = e;
        cp->bias = (vn < -1.0f) ? (-e * vn) : 0.0f;
    }
}

// One sequential-impulse iteration over a contact (Box2D Lite SolveVelocity).
// Friction is applied first and clamped by mu * accumulated normal impulse;
// the normal impulse uses the frozen restitution bias and is non-negative.
static void solve_contact_velocity(b2Contact* c, float frictionMu) {
    b2Body* A = c->bodyA;
    b2Body* B = c->bodyB;

    for (int i = 0; i < c->pointCount; i++) {
        b2ContactPoint* cp = &c->points[i];
        b2Vec2 ra = cp->r;
        b2Vec2 rb = cp->rB;
        b2Vec2 n = cp->normal;
        b2Vec2 t = b2V(-n.y, n.x);

        // --- Normal impulse (constraint: vn >= -bias)
        b2Vec2 vA = b2Add(A->velocity, b2CrossVS(ra, A->angularVelocity));
        b2Vec2 vB = b2Add(B->velocity, b2CrossVS(rb, B->angularVelocity));
        float vn = b2Dot(b2Sub(vB, vA), n);
        float lambda = -(vn + cp->bias) * cp->massNormal;
        float newN = cp->normalImpulseAccum + lambda;
        if (newN < 0.0f) newN = 0.0f;
        lambda = newN - cp->normalImpulseAccum;
        cp->normalImpulseAccum = newN;

        b2Vec2 Pn = b2MulS(n, lambda);
        A->velocity = b2Sub(A->velocity, b2MulS(Pn, A->invMass));
        A->angularVelocity -= A->invI * b2Cross(ra, Pn);
        B->velocity = b2Add(B->velocity, b2MulS(Pn, B->invMass));
        B->angularVelocity += B->invI * b2Cross(rb, Pn);

        // --- Friction impulse (cone: |P| <= mu * normalImpulseAccum)
        vA = b2Add(A->velocity, b2CrossVS(ra, A->angularVelocity));
        vB = b2Add(B->velocity, b2CrossVS(rb, B->angularVelocity));
        float vt = b2Dot(b2Sub(vB, vA), t);
        float jt = -vt * cp->massTangent;
        float maxF = frictionMu * cp->normalImpulseAccum;
        float newTan = cp->tangentImpulseAccum + jt;
        if (newTan > maxF) newTan = maxF;
        if (newTan < -maxF) newTan = -maxF;
        jt = newTan - cp->tangentImpulseAccum;
        cp->tangentImpulseAccum = newTan;

        b2Vec2 Pt = b2MulS(t, jt);
        A->velocity = b2Sub(A->velocity, b2MulS(Pt, A->invMass));
        A->angularVelocity -= A->invI * b2Cross(ra, Pt);
        B->velocity = b2Add(B->velocity, b2MulS(Pt, B->invMass));
        B->angularVelocity += B->invI * b2Cross(rb, Pt);
    }
}

static void positional_correction(b2World* w, b2Contact* c) {
    b2Body* A = c->bodyA;
    b2Body* B = c->bodyB;
    float invSum = A->invMass + B->invMass;
    if (invSum <= 0.0f || c->pointCount == 0) return;

    float maxPen = 0.0f;
    for (int i = 0; i < c->pointCount; i++) {
        float pen = -c->points[i].separation;
        if (pen > maxPen) maxPen = pen;
    }
    float corr = w->positionCorrection * fmaxf(maxPen - w->allowedPenetration, 0.0f) / invSum;
    if (corr <= 0.0f) return;
    b2Vec2 n = c->points[0].normal;
    b2Vec2 disp = b2MulS(n, -corr);
    A->position = b2Add(A->position, b2MulS(disp, A->invMass));
    B->position = b2Sub(B->position, b2MulS(disp, B->invMass));
}

// ---------------------------------------------------------------------------
// Step
// ---------------------------------------------------------------------------
void b2Step(b2World* w, float dt, int subSteps) {
    if (!w || dt <= 0.0f) return;
    if (subSteps < 1) subSteps = 1;
    float h = dt / subSteps;

    for (int s = 0; s < subSteps; s++) {
        // 1) Broadphase + narrowphase FIRST so that impact velocities are
        //    still visible to the restitution bias (Box2D Lite ordering).
        //    Manifolds persist between substeps because positions are only
        //    integrated after the velocity solve.
        for (int i = 0; i < w->contactCount; i++) w->contacts[i].alive = 0;

        for (b2Body* A = w->bodies; A; A = A->next) {
            for (b2Body* B = A->next; B; B = B->next) {
                if (A->type == B2_STATIC && B->type == B2_STATIC) continue;

                b2AABB aa = compute_aabb(A), ab = compute_aabb(B);
                aa.lo = b2Sub(aa.lo, b2V(0.05f, 0.05f));
                aa.hi = b2Add(aa.hi, b2V(0.05f, 0.05f));
                if (!aabb_overlap(aa, ab)) continue;

                int isNew = 0;
                b2Contact* c = find_or_add_contact(w, A, B, &isNew);
                if (!c) continue;
                c->alive = 1;

                b2ContactPoint pts[B2_MAX_CONTACTS];
                int n = collide_shapes(A, B, pts, B2_MAX_CONTACTS);

                // Warm start (Box2D Lite): keep accumulated impulses only when
                // the manifold is unchanged from the previous frame/substep.
                // NOTE: c->points still holds the OLD manifold here, so the
                // comparison must happen before we overwrite it below.
                int manifoldChanged = (n != c->pointCount);
                for (int i = 0; i < n && !manifoldChanged; i++) {
                    if (b2LengthSq(b2Sub(pts[i].position, c->points[i].position)) > 1e-4f ||
                        b2Dot(pts[i].normal, c->points[i].normal) < 0.999f)
                        manifoldChanged = 1;
                }
                if (manifoldChanged) {
                    for (int i = 0; i < n; i++) {
                        pts[i].normalImpulseAccum = 0.0f;
                        pts[i].tangentImpulseAccum = 0.0f;
                    }
                } else {
                    for (int i = 0; i < n; i++) {
                        pts[i].normalImpulseAccum = c->points[i].normalImpulseAccum;
                        pts[i].tangentImpulseAccum = c->points[i].tangentImpulseAccum;
                    }
                }
                memcpy(c->points, pts, (size_t)n * sizeof(b2ContactPoint));
                c->pointCount = n;

                int touching = (n > 0);
                if (touching && !c->wasTouching && w->onBeginContact)
                    w->onBeginContact(c, A->userData, B->userData);
                if (!touching && c->wasTouching && w->onEndContact)
                    w->onEndContact(c->key, A->userData, B->userData);
                c->wasTouching = (uint8_t)touching;
            }
        }

        // Compact dead contacts away
        int write = 0;
        for (int read = 0; read < w->contactCount; read++) {
            if (w->contacts[read].alive) {
                if (write != read) w->contacts[write] = w->contacts[read];
                write++;
            }
        }
        w->contactCount = write;

        // 2) Integrate forces (semi-implicit Euler)
        for (b2Body* b = w->bodies; b; b = b->next) {
            if (b->type != B2_DYNAMIC) continue;
            b->velocity = b2Add(b->velocity,
                                b2MulS(b2Add(w->gravity, b2MulS(b->force, b->invMass)), h));
            b->angularVelocity += b->invI * b->torque * h;
            b->velocity = b2MulS(b->velocity, 1.0f / (1.0f + h * b->linearDamping));
            b->angularVelocity *= 1.0f / (1.0f + h * b->angularDamping);
            b->force = b2V(0, 0);
            b->torque = 0.0f;
        }

        // 3) Pre-step caches. Accumulated impulses are warm-started across
        // substeps (reset only when manifold geometry changes, see above).
        for (int i = 0; i < w->contactCount; i++) {
            b2Contact* c = &w->contacts[i];
            if (c->isTrigger) continue;
            prestep_contact(c);
        }

        // 4) Velocity solver iterations
        for (int iter = 0; iter < w->velocityIterations; iter++) {
            for (int i = 0; i < w->contactCount; i++) {
                b2Contact* c = &w->contacts[i];
                if (c->isTrigger) continue;
                float mu = mix_friction(c->bodyA->friction, c->bodyB->friction);
                solve_contact_velocity(c, mu);
            }
        }

        // 5) Integrate velocities -> positions
        for (b2Body* b = w->bodies; b; b = b->next) {
            if (b->type != B2_DYNAMIC) continue;
            b->position = b2Add(b->position, b2MulS(b->velocity, h));
            b->rotation += b->angularVelocity * h;
            b->rot = b2Mat(cosf(b->rotation), sinf(b->rotation));
        }

        // 6) Positional correction (anti-sink)
        for (int i = 0; i < w->contactCount; i++) {
            b2Contact* c = &w->contacts[i];
            if (c->isTrigger) continue;
            positional_correction(w, c);
        }
    }
}

// ---------------------------------------------------------------------------
// Raycast
// ---------------------------------------------------------------------------
static int ray_vs_body(b2Vec2 p0, b2Vec2 dir, float maxDist, const b2Body* b, float* outT) {
    if (b->shape.type == B2_SHAPE_CIRCLE) {
        b2Vec2 m = b2Sub(p0, b->position);
        float c = b2Length(m);
        float r = b->shape.radius;
        if (c <= r) { *outT = 0.0f; return 1; }
        float proj = -b2Dot(m, dir);
        if (proj < 0.0f) return 0;
        float d2 = b2LengthSq(m) - proj * proj;
        if (d2 > r * r) return 0;
        float t = proj - sqrtf(r * r - d2);
        if (t > maxDist) return 0;
        *outT = t;
        return 1;
    } else {
        // Ray in local space; test against each face half-space (convex slab).
        b2Vec2 lo = b2MulMt(b->rot, b2Sub(p0, b->position));
        b2Vec2 dl = b2MulMt(b->rot, dir);
        const b2Polygon* p = &b->shape.poly;
        float tmin = 0.0f, tmax = maxDist;
        for (int i = 0; i < p->count; i++) {
            b2Vec2 n = p->normals[i];
            float denom = b2Dot(n, dl);
            float numer = b2Dot(n, b2Sub(lo, p->verts[i])); // signed dist to plane
            if (fabsf(denom) < 1e-8f) {
                if (numer > 0.0f) return 0; // parallel outside
            } else {
                float t = -numer / denom;  // crossing where dist becomes 0
                if (denom < 0.0f) { if (t > tmin) tmin = t; } // entering
                else { if (t < tmax) tmax = t; }              // leaving
                if (tmin > tmax) return 0;
            }
        }
        *outT = tmin;
        return 1;
    }
}

b2Body* b2RaycastClosest(b2World* w, b2Vec2 p0, b2Vec2 dir, float maxDist) {
    if (!w) return NULL;
    dir = b2Normalize(dir);
    b2Body* best = NULL;
    float bestT = maxDist;
    for (b2Body* b = w->bodies; b; b = b->next) {
        float t;
        if (ray_vs_body(p0, dir, bestT, b, &t)) { bestT = t; best = b; }
    }
    return best;
}
