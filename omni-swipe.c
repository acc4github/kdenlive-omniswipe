#include <stdlib.h>
#include <stdint.h>
#include <frei0r.h>
#include <math.h>

/* LUT resolution - higher = smoother curves, but uses more memory (still tiny) */
#define CURVE_LUT_SIZE 2048

/* ====================== PLUGIN INSTANCE STRUCTURE ====================== */
/* Holds all state for one transition instance */
typedef struct {
    int width, height;                    /* Frame dimensions from Kdenlive */

    /* Main parameters (mirrors XML order) */
    double position;                      /* Transition progress 0.0 → 1.0 */
    int clip1_axis, clip2_axis;           /* 0=0°, 1=90°, 2=180° base direction */
    int clip2_behavior;                   /* 0=Static, 1=Move, 2=Fade */
    double clip1_angle, clip2_angle;      /* Fine angle adjustment (0-180) */

    /* Curve & effect controls */
    double speed_curve;                   /* 0 = linear, >0 = power curve acceleration */
    double gentle_arrival;                /* Ease-out strength at end (0-100) */
    double motion_blur;                   /* Blur intensity (0-100) */

    /* Precomputed lookup table for fast curve evaluation */
    double curve_lut[CURVE_LUT_SIZE];
    double last_speed_curve;              /* Tracks changes to rebuild LUT only when needed */
} omni_swipe_t;

/* ====================== FREI0R PLUGIN INTERFACE ====================== */
/* Standard Frei0r entry points called by Kdenlive */

int f0r_init() { return 1; }
void f0r_deinit() {}

/* Plugin metadata shown in Kdenlive effects list */
void f0r_get_plugin_info(f0r_plugin_info_t *info) {
    info->name = "OmniSwipe";
    info->author = "acc4commissions and Grok 4.3";
    info->plugin_type = F0R_PLUGIN_TYPE_MIXER2;
    info->color_model = F0R_COLOR_MODEL_PACKED32;
    info->frei0r_version = FREI0R_MAJOR_VERSION;
    info->major_version = 0; info->minor_version = 8;
    info->num_params = 9;
    info->explanation = "A Swiss Army knife for swipe and slide transitions.";
}

/* Parameter metadata - MUST match XML slider order exactly */
void f0r_get_param_info(f0r_param_info_t *info, int idx) {
    const char* names[9] = {"position", "axis1", "direction_angle1", "axis2", "direction_angle2",
                            "clip2behavior", "speed_curve", "gentle_arrival", "motion_blur"};
    const char* expl[9] = {
        "Swipe position (progress)",
        "Clip 1 Axis (+0 / +90 / +180)",
        "Clip 1 Axis Wheel",
        "Clip 2 Axis (+0 / +90 / +180)",
        "Clip 2 Axis Wheel",
        "Clip 2 Behavior (0=Static,1=Move,2=Fade)",
        "Speed Curve (%)",
        "Gentle Arrival (%)",
        "Motion Blur (%)"
    };
    info->name = names[idx];
    info->type = F0R_PARAM_DOUBLE;
    info->explanation = expl[idx];
}

/* ====================== INSTANCE LIFECYCLE ====================== */

/* Called when transition is created */
f0r_instance_t f0r_construct(unsigned int w, unsigned int h) {
    omni_swipe_t *inst = calloc(1, sizeof(omni_swipe_t));
    if (inst) {
        inst->width = w; inst->height = h;
        inst->clip2_behavior = 1;      /* Default: Move */
        inst->clip2_angle = 180.0;     /* Default opposite direction */
        inst->last_speed_curve = -1.0; /* Force LUT rebuild on first frame */
    }
    return (f0r_instance_t)inst;
}
void f0r_destruct(f0r_instance_t i) { free(i); }

/* ====================== PARAMETER HANDLING ====================== */
/* Kdenlive calls these when user moves sliders */

void f0r_set_param_value(f0r_instance_t i, f0r_param_t p, int idx) {
    omni_swipe_t *inst = (omni_swipe_t*)i;
    double v = *(double*)p;
    switch(idx) {
        case 0: inst->position = v; break;
        case 1: inst->clip1_axis = (int)(v + 0.5); if(inst->clip1_axis<0) inst->clip1_axis=0; else if(inst->clip1_axis>2) inst->clip1_axis=2; break;
        case 2: inst->clip1_angle = v; break;
        case 3: inst->clip2_axis = (int)(v + 0.5); if(inst->clip2_axis<0) inst->clip2_axis=0; else if(inst->clip2_axis>2) inst->clip2_axis=2; break;
        case 4: inst->clip2_angle = v; break;
        case 5: inst->clip2_behavior = (int)(v+0.5); if(inst->clip2_behavior<0) inst->clip2_behavior=0; else if(inst->clip2_behavior>2) inst->clip2_behavior=2; break;
        case 6: inst->speed_curve = v; break;
        case 7: inst->gentle_arrival = v; break;
        case 8: inst->motion_blur = v; break;
    }
}

void f0r_get_param_value(f0r_instance_t i, f0r_param_t p, int idx) {
    omni_swipe_t *inst = (omni_swipe_t*)i;
    double *out = (double*)p;
    switch(idx) {
        case 0: *out = inst->position; break;
        case 1: *out = inst->clip1_axis; break;
        case 2: *out = inst->clip1_angle; break;
        case 3: *out = inst->clip2_axis; break;
        case 4: *out = inst->clip2_angle; break;
        case 5: *out = inst->clip2_behavior; break;
        case 6: *out = inst->speed_curve; break;
        case 7: *out = inst->gentle_arrival; break;
        case 8: *out = inst->motion_blur; break;
    }
}

/* ====================== CURVE SYSTEM (PROGRESS MAPPING) ====================== */

/* Build lookup table for power curve (only when speed_curve changes) */
static void build_curve_lut(omni_swipe_t *inst) {
    double c = inst->speed_curve;
    if (c <= 0.0) {
        /* Linear case */
        for (int i = 0; i < CURVE_LUT_SIZE; ++i)
            inst->curve_lut[i] = i / (double)(CURVE_LUT_SIZE - 1);
    } else {
        /* Exponential/power curve */
        double exp_val = 1.0 + (c / 100.0) * 8.0;
        for (int i = 0; i < CURVE_LUT_SIZE; ++i) {
            double t = i / (double)(CURVE_LUT_SIZE - 1);
            inst->curve_lut[i] = pow(t, exp_val);
        }
    }
    inst->last_speed_curve = c;
}

/* Fast interpolated lookup from LUT */
static double curve_lookup(const double *lut, double t) {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    double idx = t * (CURVE_LUT_SIZE - 1);
    int i = (int)idx;
    if (i >= CURVE_LUT_SIZE - 1) return lut[CURVE_LUT_SIZE - 1];
    double frac = idx - i;
    return lut[i] * (1.0 - frac) + lut[i + 1] * frac;
}

/* Gentle Arrival when Speed Curve = 0%: fast start + logarithmic slowdown */
static double reversed_linear(omni_swipe_t *inst, double t) {
    double strength = 1.0 + (inst->gentle_arrival / 100.0) * 7.0;
    return 1.0 - pow(1.0 - t, strength);
}

/* Unified progress calculator - core of all easing logic */
static double get_progress(omni_swipe_t *inst, double p) {
    if (p <= 0.0) return 0.0;
    if (p >= 1.0) return 1.0;

    /* Rebuild LUT only when speed_curve changed */
    if (fabs(inst->speed_curve - inst->last_speed_curve) > 0.0001)
        build_curve_lut(inst);

    if (inst->gentle_arrival <= 0.001)
        return curve_lookup(inst->curve_lut, p);   /* No easing */

    if (inst->speed_curve <= 0.0)
        return reversed_linear(inst, p);           /* Full-range ease-out */

    /* Curved speed + gentle tail zone */
    double g = inst->gentle_arrival / 100.0;
    double main_end = 1.0 - g;

    if (p <= main_end)
        return main_end * curve_lookup(inst->curve_lut, p / main_end);

    double zone_t = (p - main_end) / g;
    double eased = 1.0 - curve_lookup(inst->curve_lut, 1.0 - zone_t);
    return main_end + eased * g;
}

/* Approximate instantaneous speed for motion blur */
static double get_instant_speed(omni_swipe_t *inst, double p) {
    if (p <= 0.0 || p >= 1.0) return 0.0;
    double eps = 0.0005;
    double p1 = get_progress(inst, p);
    double p2 = get_progress(inst, p + eps);
    return (p2 - p1) / eps * 0.55;
}

/* ====================== GEOMETRY & PIXEL HELPERS ====================== */

/* Convert axis + angle to movement vector (dx, dy) */
static void get_clip_vector(int axis, double ang, double *dx, double *dy) {
    double base = axis * 90.0;
    double rad = fmod(base + ang, 360.0) * M_PI / 180.0;
    if (rad < 0) rad += 2 * M_PI;
    *dx = cos(rad); *dy = sin(rad);
}

static inline uint32_t get_pixel_safe(const uint32_t *f, int w, int h, int x, int y) {
    return (x>=0 && x<w && y>=0 && y<h) ? f[y*w + x] : 0;
}

static inline uint32_t fade_pixel(uint32_t px, double f) {
    if (f >= 1.0) return px;
    if (f <= 0.0) return 0;
    uint8_t a = (uint8_t)(((px>>24)&0xFF)*f);
    uint8_t r = (uint8_t)(((px>>16)&0xFF)*f);
    uint8_t g = (uint8_t)(((px>>8)&0xFF)*f);
    uint8_t b = (uint8_t)((px&0xFF)*f);
    return (a<<24) | (r<<16) | (g<<8) | b;
}

/* Simplified directional motion blur */
static uint32_t sample_blurred(const uint32_t *f, int w, int h, int x, int y, double dx, double dy, double amt) {
    if (amt <= 0.6) return get_pixel_safe(f, w, h, x, y);
    int steps = 3;
    double r=0,g=0,b=0,a=0,tot=0;
    double step_size = amt / steps;
    for (int i = -steps; i <= steps; ++i) {
        int sx = x + (int)(i * step_size * dx);
        int sy = y + (int)(i * step_size * dy);
        if (sx>=0 && sx<w && sy>=0 && sy<h) {
            uint32_t px = f[sy*w + sx];
            double wt = 1.0 - fabs(i) * 0.25;
            r += ((px>>16)&0xFF)*wt; g += ((px>>8)&0xFF)*wt;
            b += (px&0xFF)*wt; a += ((px>>24)&0xFF)*wt; tot += wt;
        }
    }
    if (tot > 0) {
        r/=tot; g/=tot; b/=tot; a/=tot;
        return ((uint32_t)a<<24) | ((uint32_t)r<<16) | ((uint32_t)g<<8) | (uint32_t)b;
    }
    return 0;
}

/* ====================== MAIN RENDER FUNCTION ====================== */
/* Called every frame by Kdenlive */
static void apply_swipe(omni_swipe_t *inst, uint32_t *out, const uint32_t *in1, const uint32_t *in2, double linear_p) {
    int w = inst->width, h = inst->height;
    double p = get_progress(inst, linear_p);                    /* Apply all easing */
    double blur_amt = inst->motion_blur * 0.085 * get_instant_speed(inst, linear_p);

    double dx1, dy1, dx2, dy2;
    get_clip_vector(inst->clip1_axis, inst->clip1_angle, &dx1, &dy1);
    get_clip_vector(inst->clip2_axis, inst->clip2_angle, &dx2, &dy2);

    double ext1 = w * fabs(dx1) + h * fabs(dy1);   /* Max travel distance for Clip1 */
    double ext2 = w * fabs(dx2) + h * fabs(dy2);

    double off1 = (1.0 - p) * ext1;   /* Clip1 exiting offset */
    double off2 = p * ext2;           /* Clip2 entering offset */

    for (int y = 0; y < h; ++y) {
        int row = y * w;
        int bsx1 = (int)(off1 * dx1 + 0.5);
        int bsy1 = (int)(off1 * dy1 + 0.5) + y;
        int bsx2 = (int)(off2 * dx2 + 0.5);
        int bsy2 = (int)(off2 * dy2 + 0.5) + y;

        for (int x = 0; x < w; ++x) {
            /* Hybrid coverage test (original logic) */
            int sx = bsx1 + x;
            int sy = bsy1;
            uint32_t coverage = get_pixel_safe(in2, w, h, sx, sy);

            uint32_t px;
            if (coverage != 0) {
                /* Clip 2 is visible here */
                px = sample_blurred(in2, w, h, sx, sy, dx1, dy1, blur_amt);
            } else {
                /* Fallback to Clip 1 based on behavior */
                if (inst->clip2_behavior == 0) {
                    /* Static: Clip1 doesn't move */
                    px = sample_blurred(in1, w, h, x, y, 0, 0, blur_amt);
                } else if (inst->clip2_behavior == 1) {
                    /* Move: Clip1 slides with its own vector */
                    int sx1 = bsx2 + x;
                    int sy1 = bsy2;
                    px = sample_blurred(in1, w, h, sx1, sy1, dx2, dy2, blur_amt);
                } else {
                    /* Fade: Cross-dissolve */
                    uint32_t base = sample_blurred(in1, w, h, x, y, 0, 0, blur_amt);
                    px = fade_pixel(base, 1.0 - p);
                }
            }
            out[row + x] = px;
        }
    }
}

/* ====================== FREI0R UPDATE ENTRY POINT ====================== */
void f0r_update2(f0r_instance_t i, double time, const uint32_t *in1, const uint32_t *in2,
                 const uint32_t *in3, uint32_t *out) {
    omni_swipe_t *inst = (omni_swipe_t*)i;
    double p = inst->position;
    if (p < 0.0) p = 0.0;
    if (p > 1.0) p = 1.0;
    apply_swipe(inst, out, in1, in2, p);
}
