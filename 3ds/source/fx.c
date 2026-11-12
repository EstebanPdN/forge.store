// Banner particles (same presets as the prototype's fx.js, simplified) and the
// spark burst when an install finishes. A fixed pool, no allocation per frame.
#include "fx.h"

#define MAXP 128
typedef struct {
    float x, y, vx, vy, t, life, size, rot, spin, ph;
    bool burst, alive;
} P;

static P ps[MAXP];
static int preset = -1;
static float acc;

enum { SPARKLES, LEAVES, EMBERS, STARS, SPEED, SEA, TWILIGHT, SCAN, CLOUDS, SNOW, DUST, PIXELS, PETALS, HEX, NPRESET };
static const char *names[NPRESET] = {"sparkles", "leaves", "embers", "stars", "speed", "sea", "twilight", "scan", "clouds", "snow", "dust", "pixels", "petals", "hex"};
static const float rate[NPRESET] = {7, 2.2f, 14, 30, 18, 6, 6, 0.8f, 0.35f, 12, 5, 5, 3, 1.4f};
static const int maxn[NPRESET] = {26, 14, 60, 90, 36, 30, 34, 3, 5, 60, 30, 28, 18, 6};

static float rnd(float a, float b) { return a + (b - a) * (rand() / (float)RAND_MAX); }

void fx_set(const char *name) {
    int p = -1;
    for (int i = 0; name && i < NPRESET; i++)
        if (!strcmp(names[i], name)) p = i;
    if (p == preset) return;
    preset = p;
    for (int i = 0; i < MAXP; i++)
        if (!ps[i].burst) ps[i].alive = false;
}

static P *spawn(void) {
    for (int i = 0; i < MAXP; i++)
        if (!ps[i].alive) {
            memset(&ps[i], 0, sizeof ps[i]);
            ps[i].alive = true;
            return &ps[i];
        }
    return NULL;
}

static void spawn_preset(void) {
    P *p = spawn();
    if (!p) return;
    const float W = 400, H = 240;
    switch (preset) {
        case SPARKLES: *p = (P){rnd(0, W), rnd(18, H * .75f), 0, 0, 0, rnd(1.2f, 2.4f), rnd(1.2f, 3.2f), .alive = true}; break;
        case LEAVES: *p = (P){rnd(-20, W * .8f), -10, rnd(10, 24), rnd(12, 22), 0, 12, rnd(3, 5.5f), rnd(0, 6.28f), rnd(-2, 2), .alive = true}; break;
        case EMBERS: *p = (P){rnd(0, W), H + 4, rnd(-6, 6), rnd(-38, -18), 0, rnd(2, 4), rnd(.8f, 2), .alive = true}; break;
        case STARS: {
            float a = rnd(0, 6.28f);
            *p = (P){W / 2, H * .45f, cosf(a), sinf(a), 0, 5, rnd(2, 30), rnd(14, 40), .alive = true};
            break;
        }
        case SPEED: *p = (P){W + 30, rnd(20, H - 10), rnd(-520, -300), 0, 0, 2, rnd(20, 60), .alive = true}; break;
        case SEA: *p = (P){rnd(0, W), rnd(H * .55f, H), 0, 0, 0, rnd(1, 2.2f), rnd(1, 2.2f), .alive = true}; break;
        case TWILIGHT: *p = (P){rnd(0, W), H + 6, rnd(-4, 4), rnd(-22, -10), 0, rnd(4, 7), rnd(2, 5), rnd(0, 6.28f), .alive = true}; break;
        case SCAN: *p = (P){0, -10, 0, rnd(50, 90), 0, 6, 0, .alive = true}; break;
        case CLOUDS: *p = (P){-70, rnd(20, H * .5f), rnd(6, 12), 0, 0, 40, rnd(16, 30), .alive = true}; break;
        case SNOW: *p = (P){rnd(0, W), -4, 0, rnd(14, 30), 0, 14, rnd(.7f, 1.8f), .ph = rnd(0, 6.28f), .alive = true}; break;
        case DUST: *p = (P){rnd(0, W), rnd(0, H), rnd(-4, 4), rnd(-6, -1), 0, rnd(3, 6), rnd(.6f, 1.5f), .alive = true}; break;
        case PIXELS: *p = (P){rnd(0, W), H + 4, 0, rnd(-26, -12), 0, rnd(3, 6), (float)(2 + rand() % 3), .alive = true}; break;
        case PETALS: *p = (P){rnd(0, W), -6, rnd(-8, 8), rnd(14, 24), 0, 14, rnd(2.4f, 4), rnd(0, 6.28f), rnd(-3, 3), .alive = true}; break;
        case HEX: *p = (P){W / 2, H * .48f, 0, 0, 0, 3.4f, 4, rnd(0, 6.28f), .alive = true}; break;
        default: p->alive = false;
    }
}

void fx_burst(float x, float y, int n) {
    for (int i = 0; i < n; i++) {
        P *p = spawn();
        if (!p) return;
        float a = rnd(-3.14f, 0) + rnd(-.4f, .4f), v = rnd(60, 220);
        *p = (P){x, y, cosf(a) * v, sinf(a) * v, 0, rnd(.6f, 1.4f), rnd(.8f, 1.8f), .ph = rnd(20, 50), .burst = true, .alive = true};
    }
}

bool fx_active(void) {
    if (preset >= 0) return true;
    for (int i = 0; i < MAXP; i++)
        if (ps[i].alive) return true;
    return false;
}

void fx_update(float dt) {
    if (preset >= 0) {
        acc += dt * rate[preset];
        int alive = 0;
        for (int i = 0; i < MAXP; i++) alive += ps[i].alive && !ps[i].burst;
        while (acc >= 1) {
            acc -= 1;
            if (alive++ < maxn[preset]) spawn_preset();
        }
    }
    for (int i = 0; i < MAXP; i++) {
        P *p = &ps[i];
        if (!p->alive) continue;
        p->t += dt;
        if (p->t > p->life) {
            p->alive = false;
            continue;
        }
        if (p->burst) {
            p->vy += 260 * dt;
            p->vx *= 1 - dt * .8f;
            p->x += p->vx * dt;
            p->y += p->vy * dt;
            continue;
        }
        switch (preset) {
            case LEAVES: p->x += (p->vx + sinf(p->t * 2 + p->rot) * 10) * dt; p->y += p->vy * dt; p->rot += p->spin * dt; break;
            case PETALS: p->x += (p->vx + sinf(p->t * 1.6f + p->rot) * 10) * dt; p->y += p->vy * dt; p->rot += p->spin * dt; break;
            case EMBERS: p->x += (p->vx + sinf(p->t * 3 + p->size * 5) * 8) * dt; p->y += p->vy * dt; break;
            case STARS: p->rot *= 1 + dt * 1.1f; p->size += p->rot * dt; break;
            case SPEED: p->x += p->vx * dt; break;
            case TWILIGHT: p->y += p->vy * dt; p->x += p->vx * dt; p->rot += dt * .6f; break;
            case SCAN: p->y += p->vy * dt; break;
            case CLOUDS: p->x += p->vx * dt; break;
            case SNOW: p->y += p->vy * dt; p->x += sinf(p->t + p->ph) * 8 * dt; break;
            case DUST: case PIXELS: p->x += p->vx * dt; p->y += p->vy * dt; break;
            case HEX: p->size += dt * 70; p->rot += dt * .5f; break;
            default: break;
        }
    }
}

static void star4(float x, float y, float r, u32 c) {
    C2D_DrawTriangle(x, y - r * 2, c, x + r * .45f, y, c, x - r * .45f, y, c, .5f);
    C2D_DrawTriangle(x, y + r * 2, c, x + r * .45f, y, c, x - r * .45f, y, c, .5f);
    C2D_DrawTriangle(x - r * 2, y, c, x, y - r * .45f, c, x, y + r * .45f, c, .5f);
    C2D_DrawTriangle(x + r * 2, y, c, x, y - r * .45f, c, x, y + r * .45f, c, .5f);
}

static void rotated_ellipse(float x,float y,float rx,float ry,float angle,u32 c) {
    float cs=cosf(angle),sn=sinf(angle);
    for(int i=0;i<16;i++) {
        float a=i*2*M_PI/16,b=(i+1)*2*M_PI/16;
        float ax=rx*cosf(a),ay=ry*sinf(a),bx=rx*cosf(b),by=ry*sinf(b);
        C2D_DrawTriangle(x,y,c,x+ax*cs-ay*sn,y+ax*sn+ay*cs,c,x+bx*cs-by*sn,y+bx*sn+by*cs,c,.5f);
    }
}
static void polygon(float x,float y,float r,float angle,int sides,u32 fill,u32 stroke,float width) {
    for(int i=0;i<sides;i++) {
        float a=angle+i*2*M_PI/sides,b=angle+(i+1)*2*M_PI/sides;
        float ax=x+r*cosf(a),ay=y+r*sinf(a),bx=x+r*cosf(b),by=y+r*sinf(b);
        if(fill>>24) C2D_DrawTriangle(x,y,fill,ax,ay,fill,bx,by,fill,.5f);
        if(stroke>>24) C2D_DrawLine(ax,ay,stroke,bx,by,stroke,width,.5f);
    }
}

void fx_draw(float ox, float alpha) {
    for (int i = 0; i < MAXP; i++) {
        P *p = &ps[i];
        if (!p->alive) continue;
        float life = p->t / p->life, x = p->x + ox, y = p->y;
        float a = alpha;
        if (p->burst) {
            float k = 1 - life;
            C2D_DrawLine(x, y, C2D_Color32f(1, .75f + .2f * k, .35f, k * a), x - p->vx * .025f, y - p->vy * .025f, C2D_Color32f(1, .6f, .2f, 0), p->size, .5f);
            continue;
        }
        float tri = sinf(life * 3.14159f);
        switch (preset) {
            case SPARKLES: star4(x, y, p->size * (.6f + tri * .6f), C2D_Color32f(1, .95f, .75f, tri * a)); break;
            case SEA: star4(x, y, p->size * 1.6f, C2D_Color32f(1, .96f, .82f, tri * .9f * a)); break;
            case LEAVES: rotated_ellipse(x,y,p->size,p->size*.45f,p->rot, C2D_Color32f(.35f, .6f, .3f, .85f * a)); break;
            case PETALS: rotated_ellipse(x,y,p->size,p->size*.55f,p->rot, C2D_Color32f(1, .67f, .8f, .85f * a)); break;
            case EMBERS: circle(x, y, p->size, C2D_Color32f(1, .55f + .3f * (1 - life), .24f, (1 - life) * .9f * a)); break;
            case STARS: {
                float dx = p->vx * p->size * 1.5f, dy = p->vy * p->size;
                float k = fminf(1, p->size / 80);
                C2D_DrawLine(p->x + dx + ox, p->y + dy, C2D_Color32f(.86f, .92f, 1, k * a), p->x + dx * .9f + ox, p->y + dy * .9f, C2D_Color32f(.86f, .92f, 1, 0), .6f + k, .5f);
                break;
            }
            case SPEED: C2D_DrawLine(x, y, C2D_Color32f(1, 1, 1, .65f * a), x + p->size, y, C2D_Color32f(1, 1, 1, 0), 1, .5f); break;
            case TWILIGHT: polygon(x,y,p->size*.7071068f,p->rot+M_PI/4,4,C2D_Color32f(.04f,.05f,.08f,tri*.85f*a),C2D_Color32f(.37f,1,.84f,tri*.45f*a),.6f); break;
            case SCAN: vgrad(ox, y - 14, 400, 14, C2D_Color32f(1, 0, 0, 0), C2D_Color32f(1, .16f, .16f, .35f * a)); break;
            case CLOUDS:
                circle(x, y, p->size * .7f, C2D_Color32f(1, 1, 1, .55f * a));
                circle(x + p->size * .9f, y - p->size * .3f, p->size * .52f, C2D_Color32f(1, 1, 1, .55f * a));
                circle(x + p->size * 1.7f, y+p->size*.05f, p->size * .595f, C2D_Color32f(1, 1, 1, .55f * a));
                circle(x-p->size*.8f,y+p->size*.15f,p->size*.49f,C2D_Color32f(1,1,1,.55f*a));
                break;
            case SNOW: circle(x, y, p->size, C2D_Color32f(1, 1, 1, .85f * a)); break;
            case DUST: circle(x, y, p->size, C2D_Color32f(1, .86f, .55f, tri * .8f * a)); break;
            case PIXELS: rect(floorf(x), floorf(y), p->size, p->size, C2D_Color32f(1, 1, 1, tri * .55f * a)); break;
            case HEX: polygon(x,y,p->size,p->rot,6,0,C2D_Color32f(1,.35f,.82f,(1-life)*.7f*a),2); break;
            default: break;
        }
    }
}
