#include <stdlib.h>
#include <stdint.h>
#include <frei0r.h>
#include <math.h>

#define CURVE_LUT_SIZE 2048

typedef struct {
    int width, height;

    double position;
    int clipa_axis, clipb_axis;
    int clipb_behavior;
    double clipa_angle, clipb_angle;

    double speed_curve;
    double gentle_arrival;
    double motion_blur;

    int edge_smoothing;
    int invert;

    int c1_min_x, c1_min_y, c1_max_x, c1_max_y;
    int c2_min_x, c2_min_y, c2_max_x, c2_max_y;
    int bounds_calculated;

    double curve_lut[CURVE_LUT_SIZE];
    double last_speed_curve;
} omni_swipe_t;

int f0r_init() { return 1; }
void f0r_deinit() {}

void f0r_get_plugin_info(f0r_plugin_info_t *info) {
    info->name = "OmniSwipe";
    info->author = "acc4commissions and Grok 4.3";
    info->plugin_type = F0R_PLUGIN_TYPE_MIXER2;
    info->color_model = F0R_COLOR_MODEL_PACKED32;
    info->frei0r_version = FREI0R_MAJOR_VERSION;
    info->major_version = 0; info->minor_version = 10;
    info->num_params = 11;
    info->explanation = "A Swiss Army knife for swipe and slide transitions with edge smoothing and invert option.";
}

void f0r_get_param_info(f0r_param_info_t *info, int idx) {
    const char* names[11] = {"position", "axis_a", "wheel_a", "axis_b", "wheel_b",
                             "clipb_behavior", "speed_curve", "gentle_arrival", "motion_blur",
                             "edge_smoothing", "invert"};
    const char* expl[11] = {
        "Swipe position (progress)",
        "Clip 1 Axis (+0 / +90 / +180)",
        "Clip 1 Axis Wheel",
        "Clip 2 Axis (+0 / +90 / +180)",
        "Clip 2 Axis Wheel",
        "Clip 2 Behavior (0=Static,1=Move,2=Fade to transparent)",
        "Speed Curve (%)",
        "Gentle Arrival (%)",
        "Motion Blur (%)",
        "Edge Smoothing (ON = full frame, OFF = tight content bounds)",
        "Invert (swap Clip A and Clip B)"
    };
    info->name = names[idx];
    info->type = (idx == 9 || idx == 10) ? F0R_PARAM_BOOL : F0R_PARAM_DOUBLE;
    info->explanation = expl[idx];
}

f0r_instance_t f0r_construct(unsigned int w, unsigned int h) {
    omni_swipe_t *inst = calloc(1, sizeof(omni_swipe_t));
    if (inst) {
        inst->width = w; inst->height = h;
        inst->clipb_behavior = 1;
        inst->clipb_angle = 180.0;
        inst->last_speed_curve = -1.0;
        inst->edge_smoothing = 1;
        inst->invert = 1;

        inst->c1_min_x = inst->c2_min_x = 0;
        inst->c1_max_x = inst->c2_max_x = w - 1;
        inst->c1_min_y = inst->c2_min_y = 0;
        inst->c1_max_y = inst->c2_max_y = h - 1;
        inst->bounds_calculated = 0;
    }
    return (f0r_instance_t)inst;
}

void f0r_destruct(f0r_instance_t i) { free(i); }

void f0r_set_param_value(f0r_instance_t i, f0r_param_t p, int idx) {
    omni_swipe_t *inst = (omni_swipe_t*)i;
    double v = *(double*)p;
    switch(idx) {
        case 0: inst->position = v; break;
        case 1: inst->clipa_axis = (int)(v + 0.5); if(inst->clipa_axis<0) inst->clipa_axis=0; else if(inst->clipa_axis>2) inst->clipa_axis=2; break;
        case 2: inst->clipa_angle = v; break;
        case 3: inst->clipb_axis = (int)(v + 0.5); if(inst->clipb_axis<0) inst->clipb_axis=0; else if(inst->clipb_axis>2) inst->clipb_axis=2; break;
        case 4: inst->clipb_angle = v; break;
        case 5: inst->clipb_behavior = (int)(v+0.5); if(inst->clipb_behavior<0) inst->clipb_behavior=0; else if(inst->clipb_behavior>2) inst->clipb_behavior=2; break;
        case 6: inst->speed_curve = v; break;
        case 7: inst->gentle_arrival = v; break;
        case 8: inst->motion_blur = v; break;
        case 9: inst->edge_smoothing = (v > 0.5) ? 1 : 0; inst->bounds_calculated = 0; break;
        case 10: inst->invert = (v > 0.5) ? 1 : 0; inst->bounds_calculated = 0; break;
    }
}

void f0r_get_param_value(f0r_instance_t i, f0r_param_t p, int idx) {
    omni_swipe_t *inst = (omni_swipe_t*)i;
    double *out = (double*)p;
    switch(idx) {
        case 0: *out = inst->position; break;
        case 1: *out = inst->clipa_axis; break;
        case 2: *out = inst->clipa_angle; break;
        case 3: *out = inst->clipb_axis; break;
        case 4: *out = inst->clipb_angle; break;
        case 5: *out = inst->clipb_behavior; break;
        case 6: *out = inst->speed_curve; break;
        case 7: *out = inst->gentle_arrival; break;
        case 8: *out = inst->motion_blur; break;
        case 9: *out = inst->edge_smoothing; break;
        case 10: *out = inst->invert; break;
    }
}

/* [All helper functions remain exactly as in previous version:
   calculate_content_bounds, build_curve_lut, curve_lookup, reversed_linear,
   get_progress, get_instant_speed, get_clip_vector, fade_to_transparent, sample_blurred] */

static void calculate_content_bounds(const uint32_t* buf, int bw, int bh,
                                     int* min_x, int* min_y, int* max_x, int* max_y) {
    int left = bw, right = -1, top = bh, bottom = -1;

    for (int x = 0; x < bw; ++x) {
        const uint8_t* p = (const uint8_t*)&buf[(bh/2) * bw + x];
        if (p[0] > 20 || p[1] > 20 || p[2] > 20 || p[3] > 30) {
            if (x < left) left = x;
            if (x > right) right = x;
        }
    }

    for (int y = 0; y < bh; ++y) {
        const uint8_t* p = (const uint8_t*)&buf[y * bw + (bw/2)];
        if (p[0] > 20 || p[1] > 20 || p[2] > 20 || p[3] > 30) {
            if (y < top) top = y;
            if (y > bottom) bottom = y;
        }
    }

    if (left > right || top > bottom || left >= bw || right < 0) {
        *min_x = 0; *max_x = bw-1;
        *min_y = 0; *max_y = bh-1;
    } else {
        *min_x = left; *max_x = right;
        *min_y = top; *max_y = bottom;
    }
}

static void build_curve_lut(omni_swipe_t *inst) {
    double c = inst->speed_curve;
    if (c <= 0.0) {
        for (int i = 0; i < CURVE_LUT_SIZE; ++i)
            inst->curve_lut[i] = i / (double)(CURVE_LUT_SIZE - 1);
    } else {
        double exp_val = 1.0 + (c / 100.0) * 8.0;
        for (int i = 0; i < CURVE_LUT_SIZE; ++i) {
            double t = i / (double)(CURVE_LUT_SIZE - 1);
            inst->curve_lut[i] = pow(t, exp_val);
        }
    }
    inst->last_speed_curve = c;
}

static double curve_lookup(const double *lut, double t) {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    double idx = t * (CURVE_LUT_SIZE - 1);
    int i = (int)idx;
    if (i >= CURVE_LUT_SIZE - 1) return lut[CURVE_LUT_SIZE - 1];
    double frac = idx - i;
    return lut[i] * (1.0 - frac) + lut[i + 1] * frac;
}

static double reversed_linear(omni_swipe_t *inst, double t) {
    double strength = 1.0 + (inst->gentle_arrival / 100.0) * 7.0;
    return 1.0 - pow(1.0 - t, strength);
}

static double get_progress(omni_swipe_t *inst, double p) {
    if (p <= 0.0) return 0.0;
    if (p >= 1.0) return 1.0;

    if (fabs(inst->speed_curve - inst->last_speed_curve) > 0.0001)
        build_curve_lut(inst);

    if (inst->gentle_arrival <= 0.001)
        return curve_lookup(inst->curve_lut, p);

    if (inst->speed_curve <= 0.0)
        return reversed_linear(inst, p);

    double g = inst->gentle_arrival / 100.0;
    double main_end = 1.0 - g;

    if (p <= main_end)
        return main_end * curve_lookup(inst->curve_lut, p / main_end);

    double zone_t = (p - main_end) / g;
    double eased = 1.0 - curve_lookup(inst->curve_lut, 1.0 - zone_t);
    return main_end + eased * g;
}

static double get_instant_speed(omni_swipe_t *inst, double p) {
    if (p <= 0.0 || p >= 1.0) return 0.0;
    double eps = 0.0005;
    double p1 = get_progress(inst, p);
    double p2 = get_progress(inst, p + eps);
    return (p2 - p1) / eps * 0.55;
}

static void get_clip_vector(int axis, double ang, double *dx, double *dy) {
    double base = axis * 90.0;
    double rad = fmod(base + ang, 360.0) * M_PI / 180.0;
    if (rad < 0) rad += 2 * M_PI;
    *dx = cos(rad); *dy = sin(rad);
}

static inline uint32_t fade_to_transparent(uint32_t px, double f) {
    if (f >= 1.0) return px;
    if (f <= 0.0) return 0;
    uint8_t a = (uint8_t)(((px>>24)&0xFF)*f);
    uint8_t r = (px>>16)&0xFF;
    uint8_t g = (px>>8)&0xFF;
    uint8_t b = px&0xFF;
    return (a<<24) | (r<<16) | (g<<8) | b;
}

static uint32_t sample_blurred(const uint32_t *f, int w, int h, int x, int y,
                               double dx, double dy, double amt,
                               int minx, int miny, int maxx, int maxy) {
    if (amt <= 0.6) {
        if (x < minx || x > maxx || y < miny || y > maxy) return 0;
        return (x>=0 && x<w && y>=0 && y<h) ? f[y*w + x] : 0;
    }

    int steps = 3;
    double r=0,g=0,b=0,a=0,tot=0;
    double step_size = amt / steps;

    for (int i = -steps; i <= steps; ++i) {
        int sx = x + (int)(i * step_size * dx);
        int sy = y + (int)(i * step_size * dy);
        if (sx >= minx && sx <= maxx && sy >= miny && sy <= maxy &&
            sx >= 0 && sx < w && sy >= 0 && sy < h) {
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

static void apply_swipe(omni_swipe_t *inst, uint32_t *out, const uint32_t *in1, const uint32_t *in2, double linear_p) {
    int w = inst->width, h = inst->height;
    double p = get_progress(inst, linear_p);
    double blur_amt = inst->motion_blur * 0.085 * get_instant_speed(inst, linear_p);

    if (inst->edge_smoothing) {
        inst->c1_min_x = inst->c2_min_x = 0;
        inst->c1_max_x = inst->c2_max_x = w - 1;
        inst->c1_min_y = inst->c2_min_y = 0;
        inst->c1_max_y = inst->c2_max_y = h - 1;
        inst->bounds_calculated = 1;
    } else if (!inst->bounds_calculated) {
        calculate_content_bounds(in1, w, h, &inst->c1_min_x, &inst->c1_min_y, &inst->c1_max_x, &inst->c1_max_y);
        calculate_content_bounds(in2, w, h, &inst->c2_min_x, &inst->c2_min_y, &inst->c2_max_x, &inst->c2_max_y);
        inst->bounds_calculated = 1;
    }

    double dx1, dy1, dx2, dy2;
    get_clip_vector(inst->clipa_axis, inst->clipa_angle, &dx1, &dy1);
    get_clip_vector(inst->clipb_axis, inst->clipb_angle, &dx2, &dy2);

    double ext1 = w * fabs(dx1) + h * fabs(dy1);
    double ext2 = w * fabs(dx2) + h * fabs(dy2);

    double off1 = (1.0 - p) * ext1;
    double off2 = p * ext2;

    for (int y = 0; y < h; ++y) {
        int row = y * w;
        int bsx1 = (int)(off1 * dx1 + 0.5);
        int bsy1 = (int)(off1 * dy1 + 0.5) + y;
        int bsx2 = (int)(off2 * dx2 + 0.5);
        int bsy2 = (int)(off2 * dy2 + 0.5) + y;

        for (int x = 0; x < w; ++x) {
            int sx = bsx1 + x;
            int sy = bsy1;

            int c2x_ok = (sx >= inst->c2_min_x && sx <= inst->c2_max_x && sy >= inst->c2_min_y && sy <= inst->c2_max_y);
            uint32_t coverage = (c2x_ok && sx>=0 && sx<w && sy>=0 && sy<h) ? in2[sy*w + sx] : 0;

            uint32_t px;
            if (coverage != 0) {
                px = sample_blurred(in2, w, h, sx, sy, dx1, dy1, blur_amt,
                                    inst->c2_min_x, inst->c2_min_y, inst->c2_max_x, inst->c2_max_y);
            } else {
                if (inst->clipb_behavior == 0) {
                    px = sample_blurred(in1, w, h, x, y, 0, 0, blur_amt,
                                        inst->c1_min_x, inst->c1_min_y, inst->c1_max_x, inst->c1_max_y);
                } else if (inst->clipb_behavior == 1) {
                    int sx1 = bsx2 + x;
                    int sy1 = bsy2;
                    px = sample_blurred(in1, w, h, sx1, sy1, dx2, dy2, blur_amt,
                                        inst->c1_min_x, inst->c1_min_y, inst->c1_max_x, inst->c1_max_y);
                } else {
                    uint32_t base = sample_blurred(in1, w, h, x, y, 0, 0, blur_amt,
                                                   inst->c1_min_x, inst->c1_min_y, inst->c1_max_x, inst->c1_max_y);
                    px = fade_to_transparent(base, 1.0 - p);
                }
            }
            out[row + x] = px;
        }
    }
}

void f0r_update2(f0r_instance_t i, double time, const uint32_t *in1, const uint32_t *in2,
                 const uint32_t *in3, uint32_t *out) {
    omni_swipe_t *inst = (omni_swipe_t*)i;
    double p = inst->position;
    if (p < 0.0) p = 0.0;
    if (p > 1.0) p = 1.0;

    const uint32_t *clip_a = inst->invert ? in2 : in1;
    const uint32_t *clip_b = inst->invert ? in1 : in2;

    apply_swipe(inst, out, clip_a, clip_b, p);
}
