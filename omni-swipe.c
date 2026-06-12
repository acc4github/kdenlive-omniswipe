#include <stdlib.h>
#include <stdint.h>
#include <frei0r.h>
#include <math.h>

#define CURVE_LUT_SIZE 2048

/* ==========================================================================
   Plugin Instance Data
   ========================================================================== */
typedef struct {
    int width, height;                    // Frame dimensions

    double position;                      // Current transition progress (0.0 -> 1.0)

    int arrival_axis, departure_axis;     // 0=horizontal, 1=vertical, 2=opposite
    double arrival_angle, departure_angle;// Fine angle offset in degrees

    int outgoing_behavior;                // 0=Static, 1=Move, 2=Fade

    double speed_curve;                   // Easing strength (0 = linear)
    double gentle_arrival;                // Soft landing strength at the end (%)
    double motion_blur;                   // Motion blur intensity (%)

    int edge_smoothing;                   // 1 = full frame, 0 = tight content bounds
    int invert;                           // Swap incoming / outgoing clips

    int out_min_x, out_min_y, out_max_x, out_max_y;  // Content bounds for outgoing clip
    int in_min_x, in_min_y, in_max_x, in_max_y;      // Content bounds for incoming clip
    int bounds_calculated;                // Flag to avoid recalculating bounds every frame

    double curve_lut[CURVE_LUT_SIZE];     // Precomputed easing curve lookup table
    double last_speed_curve;              // Cache to rebuild LUT only when needed
} omni_swipe_t;

/* Forward declarations */
static void calculate_content_bounds(const uint32_t* buf, int bw, int bh,
                                     int* min_x, int* min_y, int* max_x, int* max_y);

static void build_curve_lut(omni_swipe_t *inst);
static double curve_lookup(const double *lut, double t);
static double reversed_linear(omni_swipe_t *inst, double t);
static double get_progress(omni_swipe_t *inst, double p);
static double get_instant_speed(omni_swipe_t *inst, double p);

static void get_clip_vector(int axis, double ang, double *dx, double *dy);

static inline uint32_t fade_to_transparent(uint32_t px, double f);

static uint32_t sample_blurred(const uint32_t *f, int w, int h, int x, int y,
                               double dx, double dy, double amt,
                               int minx, int miny, int maxx, int maxy);

static void apply_swipe(omni_swipe_t *inst, uint32_t *out,
                        const uint32_t *clip_a, const uint32_t *clip_b,
                        double linear_p);

/* ==========================================================================
   Plugin Interface (Frei0r standard)
   ========================================================================== */

int f0r_init() { return 1; }
void f0r_deinit() {}

void f0r_get_plugin_info(f0r_plugin_info_t *info) {
    info->name = "OmniSwipe";
    info->author = "acc4commissions and Grok 4.3";
    info->plugin_type = F0R_PLUGIN_TYPE_MIXER2;
    info->color_model = F0R_COLOR_MODEL_PACKED32;
    info->frei0r_version = FREI0R_MAJOR_VERSION;
    info->major_version = 0;
    info->minor_version = 10;
    info->num_params = 11;
    info->explanation = "Versatile directional swipe/slide transition with motion blur, custom easing, content-aware bounds, and invert support.";
}

void f0r_get_param_info(f0r_param_info_t *info, int idx) {
    const char* names[11] = {
        "position", "arrival_axis", "arrival_wheel", "departure_axis", "departure_wheel",
        "outgoing_behavior", "speed_curve", "gentle_arrival", "motion_blur",
        "edge_smoothing", "invert"
    };
    const char* expl[11] = {
        "Swipe position (progress)",
        "Arrival Direction Axis: direction the incoming clip slides in from (+0 / +90 / +180)",
        "Arrival Direction Wheel: fine angle offset for incoming clip",
        "Departure Direction Axis: direction the outgoing clip slides out to (+0 / +90 / +180)",
        "Departure Direction Wheel: fine angle offset for outgoing clip",
        "Outgoing Behavior (0=Static, 1=Move, 2=Fade)",
        "Speed Curve (%)",
        "Gentle Arrival (%)",
        "Motion Blur (%)",
        "Edge Smoothing (ON = full frame, OFF = tight content bounds)",
        "Invert (swap incoming and outgoing clips)"
    };

    info->name = names[idx];
    info->type = (idx == 9 || idx == 10) ? F0R_PARAM_BOOL : F0R_PARAM_DOUBLE;
    info->explanation = expl[idx];
}

f0r_instance_t f0r_construct(unsigned int w, unsigned int h) {
    omni_swipe_t *inst = calloc(1, sizeof(omni_swipe_t));
    if (!inst) return NULL;

    inst->width = w;
    inst->height = h;

    // Default sensible values
    inst->outgoing_behavior = 1;      // Move by default
    inst->departure_angle = 180.0;    // Slide out to the right
    inst->edge_smoothing = 1;
    inst->invert = 1;
    inst->last_speed_curve = -1.0;

    // Full frame bounds initially
    inst->out_min_x = inst->in_min_x = 0;
    inst->out_max_x = inst->in_max_x = w - 1;
    inst->out_min_y = inst->in_min_y = 0;
    inst->out_max_y = inst->in_max_y = h - 1;

    return (f0r_instance_t)inst;
}

void f0r_destruct(f0r_instance_t i) {
    free(i);
}

void f0r_set_param_value(f0r_instance_t i, f0r_param_t p, int idx) {
    omni_swipe_t *inst = (omni_swipe_t*)i;
    double v = *(double*)p;

    switch (idx) {
        case 0: inst->position = v; break;
        case 1: inst->arrival_axis = (int)(v + 0.5);
                if (inst->arrival_axis < 0) inst->arrival_axis = 0;
                else if (inst->arrival_axis > 2) inst->arrival_axis = 2;
                break;
        case 2: inst->arrival_angle = v; break;
        case 3: inst->departure_axis = (int)(v + 0.5);
                if (inst->departure_axis < 0) inst->departure_axis = 0;
                else if (inst->departure_axis > 2) inst->departure_axis = 2;
                break;
        case 4: inst->departure_angle = v; break;
        case 5: inst->outgoing_behavior = (int)(v + 0.5);
                if (inst->outgoing_behavior < 0) inst->outgoing_behavior = 0;
                else if (inst->outgoing_behavior > 2) inst->outgoing_behavior = 2;
                break;
        case 6: inst->speed_curve = v; break;
        case 7: inst->gentle_arrival = v; break;
        case 8: inst->motion_blur = v; break;
        case 9: inst->edge_smoothing = (v > 0.5) ? 1 : 0;
                inst->bounds_calculated = 0;
                break;
        case 10: inst->invert = (v > 0.5) ? 1 : 0;
                 inst->bounds_calculated = 0;
                 break;
    }
}

void f0r_get_param_value(f0r_instance_t i, f0r_param_t p, int idx) {
    omni_swipe_t *inst = (omni_swipe_t*)i;
    double *out = (double*)p;

    switch (idx) {
        case 0: *out = inst->position; break;
        case 1: *out = inst->arrival_axis; break;
        case 2: *out = inst->arrival_angle; break;
        case 3: *out = inst->departure_axis; break;
        case 4: *out = inst->departure_angle; break;
        case 5: *out = inst->outgoing_behavior; break;
        case 6: *out = inst->speed_curve; break;
        case 7: *out = inst->gentle_arrival; break;
        case 8: *out = inst->motion_blur; break;
        case 9: *out = inst->edge_smoothing; break;
        case 10: *out = inst->invert; break;
    }
}

/* ==========================================================================
   Core Helper Functions
   ========================================================================== */

/* Scans a frame to find the tight bounding box of non-transparent content.
   Used when edge_smoothing is OFF for cleaner transitions. */
static void calculate_content_bounds(const uint32_t* buf, int bw, int bh,
                                     int* min_x, int* min_y, int* max_x, int* max_y) {
    int left = bw, right = -1, top = bh, bottom = -1;

    // Horizontal scan using middle row
    for (int x = 0; x < bw; ++x) {
        const uint8_t* p = (const uint8_t*)&buf[(bh/2) * bw + x];
        if (p[0] > 0 || p[1] > 0 || p[2] > 0 || p[3] > 0) {
            if (x < left) left = x;
            if (x > right) right = x;
        }
    }

    // Vertical scan using middle column
    for (int y = 0; y < bh; ++y) {
        const uint8_t* p = (const uint8_t*)&buf[y * bw + (bw/2)];
        if (p[0] > 0 || p[1] > 0 || p[2] > 0 || p[3] > 0) {
            if (y < top) top = y;
            if (y > bottom) bottom = y;
        }
    }

    if (left > right || top > bottom || left >= bw || right < 0) {
        // No content detected → fallback to full frame
        *min_x = 0; *max_x = bw - 1;
        *min_y = 0; *max_y = bh - 1;
    } else {
        *min_x = left; *max_x = right;
        *min_y = top; *max_y = bottom;
    }
}

/* Builds exponential easing lookup table */
static void build_curve_lut(omni_swipe_t *inst) {
    double c = inst->speed_curve;
    if (c <= 0.0) {
        // Linear
        for (int i = 0; i < CURVE_LUT_SIZE; ++i)
            inst->curve_lut[i] = i / (double)(CURVE_LUT_SIZE - 1);
    } else {
        double exp_val = 1.0 + (c / 100.0) * 8.0;   // Map 0-100% → exponent 1..9
        for (int i = 0; i < CURVE_LUT_SIZE; ++i) {
            double t = i / (double)(CURVE_LUT_SIZE - 1);
            inst->curve_lut[i] = pow(t, exp_val);
        }
    }
    inst->last_speed_curve = c;
}

/* Bilinear lookup in the curve table */
static double curve_lookup(const double *lut, double t) {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;

    double idx = t * (CURVE_LUT_SIZE - 1);
    int i = (int)idx;
    if (i >= CURVE_LUT_SIZE - 1) return lut[CURVE_LUT_SIZE - 1];

    double frac = idx - i;
    return lut[i] * (1.0 - frac) + lut[i + 1] * frac;
}

/* Gentle arrival easing (used when speed_curve = 0) */
static double reversed_linear(omni_swipe_t *inst, double t) {
    double strength = 1.0 + (inst->gentle_arrival / 100.0) * 7.0;
    return 1.0 - pow(1.0 - t, strength);
}

/* Main progress mapper: applies speed curve + optional gentle arrival */
static double get_progress(omni_swipe_t *inst, double p) {
    if (p <= 0.0) return 0.0;
    if (p >= 1.0) return 1.0;

    if (fabs(inst->speed_curve - inst->last_speed_curve) > 0.0001)
        build_curve_lut(inst);

    if (inst->gentle_arrival <= 0.001)
        return curve_lookup(inst->curve_lut, p);

    // Combined curve + gentle arrival
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

/* Approximate instantaneous velocity for motion blur strength */
static double get_instant_speed(omni_swipe_t *inst, double p) {
    if (p <= 0.0 || p >= 1.0) return 0.0;
    double eps = 0.0005;
    double p1 = get_progress(inst, p);
    double p2 = get_progress(inst, p + eps);
    return (p2 - p1) / eps * 0.55;   // Tuned scaling factor
}

/* Convert axis + angle into normalized movement vector (dx, dy) */
static void get_clip_vector(int axis, double ang, double *dx, double *dy) {
    double base = axis * 90.0;
    double rad = fmod(base + ang, 360.0) * M_PI / 180.0;
    if (rad < 0) rad += 2 * M_PI;
    *dx = cos(rad);
    *dy = sin(rad);
}

/* Apply alpha fade for fade-out behavior */
static inline uint32_t fade_to_transparent(uint32_t px, double f) {
    if (f >= 1.0) return px;
    if (f <= 0.0) return 0;

    uint8_t a = (uint8_t)(((px >> 24) & 0xFF) * f);
    uint8_t r = (px >> 16) & 0xFF;
    uint8_t g = (px >> 8) & 0xFF;
    uint8_t b = px & 0xFF;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/* Sample with directional motion blur (fast 9-sample approximation) */
static uint32_t sample_blurred(const uint32_t *f, int w, int h, int x, int y,
                               double dx, double dy, double amt,
                               int minx, int miny, int maxx, int maxy) {
    if (amt <= 0.6) {
        if (x < minx || x > maxx || y < miny || y > maxy) return 0;
        return (x >= 0 && x < w && y >= 0 && y < h) ? f[y * w + x] : 0;
    }

    /* directional blur steps (x2 samples) */
    const int steps = 5;
    double r = 0, g = 0, b = 0, a = 0, tot = 0;
    double step_size = amt / steps;

    for (int i = -steps; i <= steps; ++i) {
        int sx = x + (int)(i * step_size * dx);
        int sy = y + (int)(i * step_size * dy);

        if (sx >= minx && sx <= maxx && sy >= miny && sy <= maxy &&
            sx >= 0 && sx < w && sy >= 0 && sy < h) {
            uint32_t px = f[sy * w + sx];

            double dist = (double)i / steps;
            double wt = 1.0 - dist * dist * 1.65;   // Gaussian-like weight
            if (wt > 0.0) {
                r += ((px >> 16) & 0xFF) * wt;
                g += ((px >> 8) & 0xFF) * wt;
                b += (px & 0xFF) * wt;
                a += ((px >> 24) & 0xFF) * wt;
                tot += wt;
            }
        }
    }

    if (tot > 0.0) {
        r /= tot; g /= tot; b /= tot; a /= tot;
        return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
    return 0;
}

/* Main transition rendering function */
static void apply_swipe(omni_swipe_t *inst, uint32_t *out,
                        const uint32_t *clip_a, const uint32_t *clip_b,
                        double linear_p) {
    int w = inst->width, h = inst->height;
    double p = get_progress(inst, linear_p);
    double blur_amt = inst->motion_blur * get_instant_speed(inst, linear_p);

    // Update content bounds if needed
    if (inst->edge_smoothing) {
        inst->out_min_x = inst->in_min_x = 0;
        inst->out_max_x = inst->in_max_x = w - 1;
        inst->out_min_y = inst->in_min_y = 0;
        inst->out_max_y = inst->in_max_y = h - 1;
        inst->bounds_calculated = 1;
    } else if (!inst->bounds_calculated) {
        calculate_content_bounds(clip_a, w, h, &inst->out_min_x, &inst->out_min_y, &inst->out_max_x, &inst->out_max_y);
        calculate_content_bounds(clip_b, w, h, &inst->in_min_x, &inst->in_min_y, &inst->in_max_x, &inst->in_max_y);
        inst->bounds_calculated = 1;
    }

    double arr_dx, arr_dy, dep_dx, dep_dy;
    get_clip_vector(inst->arrival_axis, inst->arrival_angle, &arr_dx, &arr_dy);
    get_clip_vector(inst->departure_axis, inst->departure_angle, &dep_dx, &dep_dy);

    // Extension distance needed to fully slide the clip off-screen
    double ext_arr = w * fabs(arr_dx) + h * fabs(arr_dy);
    double ext_dep = w * fabs(dep_dx) + h * fabs(dep_dy);

    double off_arr = (1.0 - p) * ext_arr;   // Incoming offset
    double off_dep = p * ext_dep;           // Outgoing offset

    for (int y = 0; y < h; ++y) {
        int row = y * w;
        int arr_ox = (int)(off_arr * arr_dx + 0.5);
        int arr_oy = (int)(off_arr * arr_dy + 0.5) + y;
        int dep_ox = (int)(off_dep * dep_dx + 0.5);
        int dep_oy = (int)(off_dep * dep_dy + 0.5) + y;

        for (int x = 0; x < w; ++x) {
            // Sample incoming clip (clip_b)
            int sx = arr_ox + x;
            int sy = arr_oy;
            int covered = (sx >= inst->in_min_x && sx <= inst->in_max_x &&
                           sy >= inst->in_min_y && sy <= inst->in_max_y);

            uint32_t incoming_px = 0;
            if (covered && sx >= 0 && sx < w && sy >= 0 && sy < h) {
                incoming_px = sample_blurred(clip_b, w, h, sx, sy, arr_dx, arr_dy, blur_amt,
                                             inst->in_min_x, inst->in_min_y, inst->in_max_x, inst->in_max_y);
            }

            // Sample outgoing clip (clip_a)
            uint32_t outgoing_px;
            if (inst->outgoing_behavior == 0) {           // Static
                outgoing_px = sample_blurred(clip_a, w, h, x, y, 0, 0, blur_amt,
                                             inst->out_min_x, inst->out_min_y, inst->out_max_x, inst->out_max_y);
            } else if (inst->outgoing_behavior == 1) {    // Move
                int sx1 = dep_ox + x;
                int sy1 = dep_oy;
                int out_covered = (sx1 >= inst->out_min_x && sx1 <= inst->out_max_x &&
                                   sy1 >= inst->out_min_y && sy1 <= inst->out_max_y);
                outgoing_px = out_covered ? sample_blurred(clip_a, w, h, sx1, sy1, dep_dx, dep_dy, blur_amt,
                                                           inst->out_min_x, inst->out_min_y, inst->out_max_x, inst->out_max_y) : 0;
            } else {                                      // Fade
                uint32_t base = sample_blurred(clip_a, w, h, x, y, 0, 0, blur_amt,
                                               inst->out_min_x, inst->out_min_y, inst->out_max_x, inst->out_max_y);
                outgoing_px = fade_to_transparent(base, 1.0 - p);
            }

            // Priority: incoming pixels overwrite outgoing when they appear
            uint32_t px = (incoming_px != 0) ? incoming_px : outgoing_px;
            out[row + x] = px;
        }
    }
}

/* Main rendering entry point called by Kdenlive / Frei0r host */
void f0r_update2(f0r_instance_t i, double time, const uint32_t *in1, const uint32_t *in2,
                 const uint32_t *in3, uint32_t *out) {
    omni_swipe_t *inst = (omni_swipe_t*)i;
    double p = inst->position;
    if (p < 0.0) p = 0.0;
    if (p > 1.0) p = 1.0;

    // Respect invert flag
    const uint32_t *clip_a = inst->invert ? in2 : in1;   // Outgoing base
    const uint32_t *clip_b = inst->invert ? in1 : in2;   // Incoming

    apply_swipe(inst, out, clip_a, clip_b, p);
}
