// Simple3DEngine - bundled Box2D Lite (subset)
// ---------------------------------------------------------------------------
// A compact, self-contained 2D rigid-body physics solver in the spirit of
// Erin Catto's "Box2D Lite" (https://github.com/erincatto/box2d), adapted for
// Simple3DEngine so that NO external dependency is required — ideal for old
// PCs and cross-platform builds (Windows / Linux / Android / FreeBSD).
//
// Features:
//   * Impulse-based sequential contact solver with accumulated impulses
//   * Separating-Axis-Theorem (SAT) manifolds: polygon vs polygon (rotated
//     boxes), circle vs polygon, circle vs circle
//   * Static / dynamic bodies, density-based mass & inertia
//   * Gravity, linear/angular damping, restitution & friction mixing
//   * Fixed-timestep integration (semi-implicit Euler) + position correction
//   * Collision callbacks (begin-contact style) for game logic / Lua
//
// Units are engine units (meters). Coordinates: X right, Y up.
// ---------------------------------------------------------------------------
#ifndef BOX2D_H
#define BOX2D_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define B2_MAX_POLYGON_VERTS 8
#define B2_MAX_CONTACTS      16
#define B2_PI                3.14159265358979323846f

// ---------------------------------------------------------------------------
// Math helpers
// ---------------------------------------------------------------------------
typedef struct b2Vec2 { float x, y; } b2Vec2;

static inline b2Vec2 b2V(float x, float y) { b2Vec2 v = {x, y}; return v; }
static inline b2Vec2 b2Add(b2Vec2 a, b2Vec2 b) { return b2V(a.x + b.x, a.y + b.y); }
static inline b2Vec2 b2Sub(b2Vec2 a, b2Vec2 b) { return b2V(a.x - b.x, a.y - b.y); }
static inline b2Vec2 b2MulS(b2Vec2 a, float s) { return b2V(a.x * s, a.y * s); }
static inline float  b2Dot(b2Vec2 a, b2Vec2 b) { return a.x * b.x + a.y * b.y; }
static inline float  b2Cross(b2Vec2 a, b2Vec2 b) { return a.x * b.y - a.y * b.x; }
static inline b2Vec2 b2CrossSV(float s, b2Vec2 a) { return b2V(s * a.y, -s * a.x); }
static inline b2Vec2 b2CrossVS(b2Vec2 a, float s) { return b2V(-s * a.y, s * a.x); }
static inline float  b2LengthSq(b2Vec2 a) { return b2Dot(a, a); }
static inline float  b2Length(b2Vec2 a) { return sqrtf(b2LengthSq(a)); }
static inline b2Vec2 b2Normalize(b2Vec2 a) {
    float l = b2Length(a);
    return (l > 1e-8f) ? b2MulS(a, 1.0f / l) : b2V(0.0f, 0.0f);
}
static inline b2Vec2 b2Neg(b2Vec2 a) { return b2V(-a.x, -a.y); }

// Column-major 2x2 rotation matrix stored as two basis vectors
typedef struct b2Mat22 { b2Vec2 ex, ey; } b2Mat22;

static inline b2Mat22 b2Mat(float c, float s) { b2Mat22 m = {{c, s}, {-s, c}}; return m; }
static inline b2Vec2  b2MulM(b2Mat22 M, b2Vec2 v) {
    return b2V(M.ex.x * v.x + M.ey.x * v.y, M.ex.y * v.x + M.ey.y * v.y);
}
static inline b2Vec2  b2MulMt(b2Mat22 M, b2Vec2 v) { // transpose (world->local)
    return b2V(b2Dot(M.ex, v), b2Dot(M.ey, v));
}

// ---------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------
typedef enum b2ShapeType { B2_SHAPE_NONE = 0, B2_SHAPE_POLYGON, B2_SHAPE_CIRCLE } b2ShapeType;

typedef struct b2Polygon {
    b2Vec2 verts[B2_MAX_POLYGON_VERTS];      // local space
    b2Vec2 normals[B2_MAX_POLYGON_VERTS];    // outward face normals (local)
    int    count;
} b2Polygon;

typedef struct b2Shape {
    b2ShapeType type;
    b2Polygon   poly;        // when type == B2_SHAPE_POLYGON
    float       radius;      // when type == B2_SHAPE_CIRCLE
} b2Shape;

// ---------------------------------------------------------------------------
// Bodies
// ---------------------------------------------------------------------------
typedef enum b2BodyType { B2_STATIC = 0, B2_DYNAMIC } b2BodyType;

typedef struct b2BodyDef {
    b2BodyType type;
    b2Vec2     position;
    float      rotation;      // radians
    float      density;       // used for dynamic bodies (mass = area*density)
    float      friction;      // 0..1
    float      restitution;   // 0..1
    float      linearDamping;
    float      angularDamping;
    void*      userData;      // e.g. back-pointer to an s3d_game_object
} b2BodyDef;

typedef struct b2Body {
    b2BodyType type;
    b2Shape    shape;

    // State
    b2Vec2     position;
    float      rotation;
    b2Mat22    rot;             // cached rotation matrix
    b2Vec2     velocity;
    float      angularVelocity;
    b2Vec2     force;
    float      torque;

    // Properties
    float      mass, invMass;
    float      I, invI;         // inertia about center
    float      friction, restitution;
    float      linearDamping, angularDamping;
    int        id;              // stable handle for world queries
    void*      userData;

    struct b2Body* next;        // world linked list
} b2Body;

// ---------------------------------------------------------------------------
// Contacts
// ---------------------------------------------------------------------------
typedef struct b2ContactPoint {
    b2Vec2 position;            // world-space contact point
    b2Vec2 normal;              // from body A to body B
    float  separation;          // <= 0 while touching
    // Solver cache
    float  massNormal, massTangent;
    float  raxN, rayN, rbxN, rbyN;
    float  bias;
    float  restitution;                        // cached mixed restitution
    uint8_t resting;                             // low-speed contact (no bounce)
    float  normalImpulse, tangentImpulse;      // per-step
    float  normalImpulseAccum, tangentImpulseAccum; // persistent
    b2Vec2 r, rB;                              // lever arms (prestep cache)
} b2ContactPoint;

typedef struct b2Contact {
    b2Body*           bodyA;
    b2Body*           bodyB;
    b2ContactPoint    points[B2_MAX_CONTACTS];
    int               pointCount;
    int               key;              // pair id for persistent state
    uint8_t           wasTouching;
    uint8_t           isTrigger;        // report only, no response
    uint8_t           alive;            // marked during narrowphase
} b2Contact;

// Collision callback: fired on begin-contact.
typedef void (*b2BeginContactFn)(b2Contact* c, void* userDataA, void* userDataB);
typedef void (*b2EndContactFn)(int key, void* userDataA, void* userDataB);

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------
typedef struct b2World {
    b2Vec2   gravity;
    b2Body*  bodies;            // singly linked list
    int      bodyCount;
    int      nextBodyId;

    b2Contact* contacts;        // growable array
    int        contactCount;
    int        contactCapacity;
    int        nextContactKey;

    int      velocityIterations;
    float    positionCorrection; // fraction of penetration corrected per step
    float    allowedPenetration; // slop

    b2BeginContactFn onBeginContact;
    b2EndContactFn   onEndContact;
} b2World;

// --- lifecycle -------------------------------------------------------------
b2World* b2CreateWorld(float gravityX, float gravityY);
void     b2DestroyWorld(b2World* w);

b2Body*  b2CreateBody(b2World* w, const b2BodyDef* def, const b2Shape* shape);
void     b2DestroyBody(b2World* w, b2Body* body);
b2Body*  b2GetBodyById(b2World* w, int id);

// --- body configuration ----------------------------------------------------
void     b2SetMassFromDensity(b2Body* b);      // recompute after shape change
void     b2ApplyForce(b2Body* b, b2Vec2 force, b2Vec2 worldPoint);
void     b2ApplyImpulse(b2Body* b, b2Vec2 impulse, b2Vec2 worldPoint);
void     b2SetTransform(b2Body* b, b2Vec2 pos, float rotRad);

// --- simulation ------------------------------------------------------------
void     b2Step(b2World* w, float dt, int subSteps);

// --- queries ---------------------------------------------------------------
b2Body*  b2RaycastClosest(b2World* w, b2Vec2 p0, b2Vec2 dir, float maxDist);

// --- shape factories ---------------------------------------------------------
b2Shape  b2MakeCircle(float radius);
b2Shape  b2MakeBox(float hx, float hy);                 // centered box
b2Shape  b2MakePolygon(int count, const b2Vec2* verts); // convex, CCW

#ifdef __cplusplus
}
#endif

#endif // BOX2D_H
