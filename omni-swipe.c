#include <stdlib.h>
#include <stdint.h>
#include <frei0r.h>
#include <math.h>

/* LUT resolution - higher value = smoother curve at the cost of tiny memory */
#define CURVE_LUT_SIZE 2048

/* ====================== PLUGIN INSTANCE STRUCTURE ====================== */
typedef struct {
    int width, height;                    /* Video frame size */

    /* Main parameters */
    double position;                      /* Transition progress (0.0 - 1.0) */
    int clip1_axis, clip2_axis;           /* Base axis: 0=0°, 1=90°, 2=180° */
    int clip2_behavior;                   /* 0=Static, 1=Move, 2=Fade */
    double clip1_angle, clip2_angle;      /* Fine angle adjustment */

    /* Curve controls */
    double speed_curve;                   /* Main acceleration curve (0-100) */
    double gentle_arrival;                /* Ease-out strength at the end (0-100) */
    double motion_blur;                   /* Motion blur intensity (0-100) */

    /* Precomputed lookup table for speed curve */
    double curve_lut[CURVE_LUT_SIZE];
    double last_speed_curve;              /* Used to detect changes and rebuild LUT */
} omni_swipe_t;

/* Forward declaration */
static void apply_swipe(omni_swipe_t *inst, uint32_t *out,
                        const uint32_t *in1, const uint32_t *in2,
                        double linear_progress);

/* ====================== FREI0R PLUGIN INTERFACE ====================== */
int f0r_init() { return 1; }
void f0r_deinit() {}

/* Plugin information shown in Kdenlive */
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

/* Parameter list - must match XML order exactly */
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

/* Create / Destroy instance */
f0r_instance_t f0r_construct(unsigned int w, unsigned int h) {
    omni_swipe_t *inst = calloc(1, sizeof(omni_swipe_t));
    if (inst) {
        inst->width = w; inst->height = h;
        inst->clip2_behavior = 1;      /* Default: Move */
        inst->clip2_angle = 180.0;
        inst->gentle_arrival = 0.0;
        inst->last_speed_curve = -1.0; /* Force LUT rebuild on first frame */
    }
    return (f0r_instance_t)inst;
}
void f0r_destruct(f0r_instance_t i) { free(i); }

/* ====================== PARAMETER SET / GET ====================== */
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

/* ====================== SPEED CURVE SYSTEM ====================== */

/* Build LUT for the power curve (called only when speed_curve changes) */
static void build_curve_lut(omni_swipe_t *inst) {
    double c = inst->speed_curve;
    if (c <= 0.0) {
        for (int i = 0; i < CURVE_LUT_SIZE; ++i) {
            inst->curve_lut[i] = i / (double)(CURVE_LUT_SIZE - 1);
        }
    } else {
        /* Continuous power curve even at 100% (exponent = 9.0 at max) */
        double exp_val = 1.0 + (c / 100.0) * 8.0;
        for (int i = 0; i < CURVE_LUT_SIZE; ++i) {
            double t = i / (double)(CURVE_LUT_SIZE - 1);
            inst->curve_lut[i] = pow(t, exp_val);
        }
    }
    inst->last_speed_curve = c;
}

/* Fast lookup with linear interpolation */
static double curve_lookup(const double *lut, double t) {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    double idx = t * (CURVE_LUT_SIZE - 1);
    int i = (int)idx;
    if (i >= CURVE_LUT_SIZE - 1) return lut[CURVE_LUT_SIZE - 1];
    double frac = idx - i;
    return lut[i] * (1.0 - frac) + lut[i + 1] * frac;
}

static double main_curve(omni_swipe_t *inst, double t) {
    return curve_lookup(inst->curve_lut, t);
}

/* Reversed curve for Gentle Arrival */
static double reversed_curve(omni_swipe_t *inst, double t) {
    if (inst->speed_curve <= 0.0) {
        /* Independent ease-out when Speed Curve = 0% */
        double strength = 1.0 + (inst->gentle_arrival / 100.0) * 7.0;
        return 1.0 - pow(1.0 - t, strength);
    }
    /* Symmetric mirror of the main Speed Curve */
    return 1.0 - curve_lookup(inst->curve_lut, 1.0 - t);
}

/* Main progress calculation */
static double apply_progress(omni_swipe_t *inst, double p) {
    if (p <= 0.0) return 0.0;
    if (p >= 1.0) return 1.0;
    if (inst->gentle_arrival <= 0.001)
        return main_curve(inst, p);

    double g = inst->gentle_arrival / 100.0;

    if (inst->speed_curve <= 0.0) {
        /* Full reverse curve mode */
        return reversed_curve(inst, p);
    }

    /* Hybrid mode: main curve + gentle arrival zone */
    double zone = g;
    double main_end = 1.0 - zone;

    if (p <= main_end) {
        return main_end * main_curve(inst, p / main_end);
    }

    double zone_t = (p - main_end) / zone;
    double eased_zone = reversed_curve(inst, zone_t);
    return main_end + eased_zone * zone;
}

/* Numerical derivative for motion blur */
static double get_instant_speed(omni_swipe_t *inst, double p) {
    if (p <= 0.0 || p >= 1.0) return 0.0;
    double eps = 0.0005;
    double p1 = apply_progress(inst, p);
    double p2 = apply_progress(inst, p + eps);
    return (p2 - p1) / eps * 0.55;
}

/* ====================== GEOMETRY & PIXEL HELPERS ====================== */
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

static uint32_t sample_blurred(const uint32_t *f, int w, int h, int x, int y, double dx, double dy, double amt) {
    if (amt <= 0.6) return get_pixel_safe(f, w, h, x, y);
    int steps = (int)(amt * 0.08) + 1; if (steps > 6) steps = 6;
    double r=0,g=0,b=0,a=0,tot=0;
    for (int i = -steps; i <= steps; ++i) {
        double t = (double)i / (steps + 0.5);
        int sx = x + (int)(t * amt * dx + (t>=0?0.5:-0.5));
        int sy = y + (int)(t * amt * dy + (t>=0?0.5:-0.5));
        if (sx>=0 && sx<w && sy>=0 && sy<h) {
            uint32_t px = f[sy*w + sx];
            double wt = 1 - fabs(t)*0.65;
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
static void apply_swipe(omni_swipe_t *inst, uint32_t *out, const uint32_t *in1, const uint32_t *in2, double linear_p) {
    /* Rebuild LUT only when needed */
    if (fabs(inst->speed_curve - inst->last_speed_curve) > 0.0001) {
        build_curve_lut(inst);
    }

    int w = inst->width, h = inst->height;
    double p = apply_progress(inst, linear_p);           /* Final eased progress */
    double blur = inst->motion_blur * 0.085 * get_instant_speed(inst, linear_p);

    double dx1,dy1,dx2,dy2;
    get_clip_vector(inst->clip1_axis, inst->clip1_angle, &dx1, &dy1);
    get_clip_vector(inst->clip2_axis, inst->clip2_angle, &dx2, &dy2);

    double ext1 = w*fabs(dx1) + h*fabs(dy1);
    double ext2 = w*fabs(dx2) + h*fabs(dy2);

    /* Fast paths for pure horizontal / vertical movement */
    if (blur <= 0.6 && fabs(dy1) < 0.02 && fabs(dy2) < 0.02) {  /* Horizontal */
        int shift1 = (int)((1-p)*ext1*dx1 + 0.5);
        int shift2 = (int)(p*ext2*dx2 + 0.5);
        for (int y = 0; y < h; ++y) {
            int row = y * w;
            for (int x = 0; x < w; ++x) {
                uint32_t px = get_pixel_safe(in2, w, h, x + shift1, y);
                if (px == 0) {
                    if (inst->clip2_behavior == 0) px = in1[row + x];
                    else if (inst->clip2_behavior == 1) px = get_pixel_safe(in1, w, h, x + shift2, y);
                    else px = fade_pixel(in1[row + x], 1.0 - p);
                }
                out[row + x] = px;
            }
        }
        return;
    }

    if (blur <= 0.6 && fabs(dx1) < 0.02 && fabs(dx2) < 0.02) {  /* Vertical */
        int shift1 = (int)((1-p)*ext1*dy1 + 0.5);
        int shift2 = (int)(p*ext2*dy2 + 0.5);
        for (int y = 0; y < h; ++y) {
            int row = y * w;
            for (int x = 0; x < w; ++x) {
                uint32_t px = get_pixel_safe(in2, w, h, x, y + shift1);
                if (px == 0) {
                    if (inst->clip2_behavior == 0) px = in1[row + x];
                    else if (inst->clip2_behavior == 1) px = get_pixel_safe(in1, w, h, x, y + shift2);
                    else px = fade_pixel(in1[row + x], 1.0 - p);
                }
                out[row + x] = px;
            }
        }
        return;
    }

    /* General case: any angle + motion blur */
    double off1 = (1-p) * ext1;
    double off2 = p * ext2;
    for (int y = 0; y < h; ++y) {
        int bsx1 = (int)(off1*dx1 + 0.5);
        int bsy1 = (int)(off1*dy1 + 0.5) + y;
        int bsx2 = (int)(off2*dx2 + 0.5);
        int bsy2 = (int)(off2*dy2 + 0.5) + y;
        int row = y * w;
        for (int x = 0; x < w; ++x) {
            uint32_t px = (blur > 0.6) ? sample_blurred(in2,w,h, bsx1+x, bsy1, dx1,dy1,blur)
                                       : get_pixel_safe(in2,w,h, bsx1+x, bsy1);
            if (px == 0) {
                if (inst->clip2_behavior == 0)
                    px = (blur > 0.6) ? sample_blurred(in1,w,h,x,y,0,0,blur) : in1[row+x];
                else if (inst->clip2_behavior == 1)
                    px = (blur > 0.6) ? sample_blurred(in1,w,h, bsx2+x, bsy2, dx2,dy2,blur)
                                      : get_pixel_safe(in1,w,h, bsx2+x, bsy2);
                else
                    px = fade_pixel( (blur > 0.6 ? sample_blurred(in1,w,h,x,y,0,0,blur) : in1[row+x]) , 1.0 - p);
            }
            out[row + x] = px;
        }
    }
}

/* ====================== FREI0R UPDATE ====================== */
void f0r_update2(f0r_instance_t i, double time, const uint32_t *in1, const uint32_t *in2,
                 const uint32_t *in3, uint32_t *out) {
    omni_swipe_t *inst = (omni_swipe_t*)i;
    double p = inst->position;
    if (p < 0.0) p = 0.0;
    if (p > 1.0) p = 1.0;
    apply_swipe(inst, out, in1, in2, p);
}
