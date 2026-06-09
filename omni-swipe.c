#include <stdlib.h>
#include <stdint.h>
#include <frei0r.h>
#include <math.h>
#include <xmmintrin.h>
#include <emmintrin.h>

/* Main plugin instance structure */
typedef struct {
    int width, height;
    double position;
    int clip1_axis, clip2_axis, clip2_behavior;   /* 0,1,2 for axis; 0-2 for behavior */
    double clip1_angle, clip2_angle, speed_curve, motion_blur;
} omni_swipe_t;

/* Forward declaration of core render function */
static void apply_swipe(omni_swipe_t *inst, uint32_t *out,
                        const uint32_t *in1, const uint32_t *in2,
                        double linear_progress);

/* ====================== FREI0R PLUGIN INTERFACE ====================== */
int f0r_init() { return 1; }
void f0r_deinit() {}

/* Plugin metadata for Kdenlive / Frei0r host */
void f0r_get_plugin_info(f0r_plugin_info_t *info) {
    info->name = "OmniSwipe";
    info->author = "acc4commissions and Grok 4.3";
    info->plugin_type = F0R_PLUGIN_TYPE_MIXER2;
    info->color_model = F0R_COLOR_MODEL_PACKED32;
    info->frei0r_version = FREI0R_MAJOR_VERSION;
    info->major_version = 0; info->minor_version = 3;
    info->num_params = 8; info->explanation = "A Swiss Army knife for swipe and slide transitions.";
}

/* Parameter metadata (matches XML names and order) */
void f0r_get_param_info(f0r_param_info_t *info, int idx) {
    const char* names[8] = {"position", "axis1", "direction_angle1", "axis2", "direction_angle2",
                            "clip2behavior", "speed_curve", "motion_blur"};
    const char* expl[8] = {
        "Swipe position (progress)",
        "Clip 1 Axis (+0 / +90 / +180)",
        "Clip 1 Axis Wheel",
        "Clip 2 Axis (+0 / +90 / +180)",
        "Clip 2 Axis Wheel",
        "Clip 2 Behavior (0=Static,1=Move,2=Fade)",
        "Speed Curve (%)",
        "Motion Blur (%)"
    };
    info->name = names[idx];
    info->type = F0R_PARAM_DOUBLE;   /* Frei0r uses double even for lists */
    info->explanation = expl[idx];
}

/* Create plugin instance */
f0r_instance_t f0r_construct(unsigned int w, unsigned int h) {
    omni_swipe_t *inst = calloc(1, sizeof(omni_swipe_t));  /* Zero-initialized */
    if (inst) {
        inst->width = w; inst->height = h;
        inst->clip2_behavior = 1; inst->clip2_angle = 180.0;  /* Sensible defaults */
    }
    return (f0r_instance_t)inst;
}

/* Destroy plugin instance - clean free, no leaks */
void f0r_destruct(f0r_instance_t i) { free(i); }

/* Set parameter values from host (Kdenlive) */
void f0r_set_param_value(f0r_instance_t i, f0r_param_t p, int idx) {
    omni_swipe_t *inst = (omni_swipe_t*)i;
    double v = *(double*)p;
    switch(idx) {
        case 0: inst->position = v; break;
        case 1: /* Clip 1 Axis: 0=+0°, 1=+90°, 2=+180° */
            inst->clip1_axis = (int)(v + 0.5);
            if(inst->clip1_axis<0) inst->clip1_axis=0;
            else if(inst->clip1_axis>2) inst->clip1_axis=2;
            break;
        case 2: inst->clip1_angle = v; break;
        case 3: /* Clip 2 Axis: same as above */
            inst->clip2_axis = (int)(v + 0.5);
            if(inst->clip2_axis<0) inst->clip2_axis=0;
            else if(inst->clip2_axis>2) inst->clip2_axis=2;
            break;
        case 4: inst->clip2_angle = v; break;
        case 5: /* Clip 2 Behavior */
            inst->clip2_behavior = (int)(v+0.5);
            if(inst->clip2_behavior<0) inst->clip2_behavior=0;
            else if(inst->clip2_behavior>2) inst->clip2_behavior=2;
            break;
        case 6: inst->speed_curve = v; break;
        case 7: inst->motion_blur = v; break;
    }
}

/* Get current parameter values for host */
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
        case 7: *out = inst->motion_blur; break;
    }
}

/* Main update callback from host */
void f0r_update2(f0r_instance_t i, double time, const uint32_t *in1, const uint32_t *in2,
                 const uint32_t *in3, uint32_t *out) {
    omni_swipe_t *inst = (omni_swipe_t*)i;
    double p = inst->position;
    if (p < 0.0) p = 0.0;
    if (p > 1.0) p = 1.0;
    apply_swipe(inst, out, in1, in2, p);   /* in3 unused for mixer2 */
}

/* ====================== HELPERS ====================== */

/* Compute direction vector from axis base + wheel angle */
static void get_clip_vector(int axis, double ang, double *dx, double *dy) {
    double base = axis * 90.0;  /* 0→0°, 1→90°, 2→180° */
    double rad = fmod(base + ang, 360.0) * M_PI / 180.0;
    if (rad < 0) rad += 2 * M_PI;
    *dx = cos(rad); *dy = sin(rad);
}

/* Apply easing curve to progress */
static double apply_speed_curve(double p, double c) {
    if (c <= 0) return p;
    if (c >= 100) return p >= 1 ? 1 : 0;
    return pow(p, 1 + c/100.0 * 8);
}

/* Instant speed for motion blur amount */
static double get_instant_speed(double p, double c) {
    if (c <= 0) return 1.0;
    if (c >= 100 || p <= 0) return 0.0;
    double pw = 1 + c/100.0 * 8;
    return pw * pow(p, pw - 1);
}

/* Safe pixel read with bounds check */
static inline uint32_t get_pixel_safe(const uint32_t *f, int w, int h, int x, int y) {
    return (x>=0 && x<w && y>=0 && y<h) ? f[y*w + x] : 0;
}

/* Alpha + RGB fade/scaling */
static inline uint32_t fade_pixel(uint32_t px, double f) {
    if (f >= 1.0) return px;
    if (f <= 0.0) return 0;
    uint8_t a = (uint8_t)(((px>>24)&0xFF)*f);
    uint8_t r = (uint8_t)(((px>>16)&0xFF)*f);
    uint8_t g = (uint8_t)(((px>>8)&0xFF)*f);
    uint8_t b = (uint8_t)((px&0xFF)*f);
    return (a<<24) | (r<<16) | (g<<8) | b;
}

/* Scalar motion blur sampling (multi-sample along direction) */
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

/* ====================== SIMD HELPERS ====================== */

/* Fast SIMD copy of shifted row (clip2 incoming) */
static inline void simd_copy_shifted(uint32_t *dst, const uint32_t *src, int w, int shift, int y, int h) {
    int row = y * w;
    __m128i zero = _mm_setzero_si128();
    int x = 0;
    for (; x + 4 <= w; x += 4) {
        int sx = x + shift;
        __m128i v;
        if (sx >= 0 && sx + 3 < w) {
            v = _mm_loadu_si128((__m128i*)(src + row + sx));
        } else {
            v = zero;
            for (int k = 0; k < 4; ++k) {
                int px = sx + k;
                ((uint32_t*)&v)[k] = get_pixel_safe(src, w, h, px, y);
            }
        }
        _mm_storeu_si128((__m128i*)(dst + row + x), v);
    }
    for (; x < w; ++x) {
        dst[row + x] = get_pixel_safe(src, w, h, x + shift, y);
    }
}

/* Composite clip1 over zero pixels from clip2 (SIMD where possible) */
static inline void simd_composite_clip1(uint32_t *out, const uint32_t *in1, int w, int h, int y,
                                        int shift2, int clip2_behavior, double p) {
    int row = y * w;
    __m128i zero = _mm_setzero_si128();
    int x = 0;
    for (; x + 4 <= w; x += 4) {
        __m128i vout = _mm_loadu_si128((__m128i*)(out + row + x));
        __m128i mask = _mm_cmpeq_epi32(vout, zero);

        __m128i v1 = zero;
        if (clip2_behavior == 0) {
            v1 = _mm_loadu_si128((__m128i*)(in1 + row + x));
        } else if (clip2_behavior == 1) {
            int sx = x + shift2;
            if (sx >= 0 && sx + 3 < w) {
                v1 = _mm_loadu_si128((__m128i*)(in1 + row + sx));
            } else {
                v1 = zero;
                for (int k = 0; k < 4; ++k) {
                    ((uint32_t*)&v1)[k] = get_pixel_safe(in1, w, h, sx + k, y);
                }
            }
        } else {
            /* Fade behavior */
            __m128i orig = _mm_loadu_si128((__m128i*)(in1 + row + x));
            v1 = orig;  /* TODO: vectorized fade if performance critical */
        }

        __m128i result = _mm_or_si128(_mm_andnot_si128(mask, vout), _mm_and_si128(mask, v1));
        _mm_storeu_si128((__m128i*)(out + row + x), result);
    }
    /* Scalar cleanup for remainder pixels */
    for (; x < w; ++x) {
        if (out[row + x] == 0) {
            uint32_t px = 0;
            if (clip2_behavior == 0) {
                px = in1[row + x];
            } else if (clip2_behavior == 1) {
                px = get_pixel_safe(in1, w, h, x + shift2, y);
            } else {
                px = fade_pixel(in1[row + x], 1.0 - p);
            }
            out[row + x] = px;
        }
    }
}

/* ====================== MAIN RENDER ====================== */
static void apply_swipe(omni_swipe_t *inst, uint32_t *out, const uint32_t *in1, const uint32_t *in2, double linear_p) {
    int w = inst->width, h = inst->height;
    double p = apply_speed_curve(linear_p, inst->speed_curve);
    double blur = inst->motion_blur * 0.085 * get_instant_speed(linear_p, inst->speed_curve);

    double dx1,dy1,dx2,dy2;
    get_clip_vector(inst->clip1_axis, inst->clip1_angle, &dx1, &dy1);
    get_clip_vector(inst->clip2_axis, inst->clip2_angle, &dx2, &dy2);

    double ext1 = w*fabs(dx1) + h*fabs(dy1);
    double ext2 = w*fabs(dx2) + h*fabs(dy2);

    /* ==================== FAST HORIZONTAL SIMD ==================== */
    if (blur <= 0.6 && fabs(dy1) < 0.02 && fabs(dy2) < 0.02) {
        int shift1 = (int)((1-p)*ext1*dx1 + 0.5);
        int shift2 = (int)(p*ext2*dx2 + 0.5);

        for (int y = 0; y < h; ++y) {
            simd_copy_shifted(out, in2, w, shift1, y, h);
            simd_composite_clip1(out, in1, w, h, y, shift2, inst->clip2_behavior, p);
        }
        return;
    }

    /* ==================== FAST VERTICAL SIMD ==================== */
    if (blur <= 0.6 && fabs(dx1) < 0.02 && fabs(dx2) < 0.02) {
        int shift1 = (int)((1-p)*ext1*dy1 + 0.5);
        int shift2 = (int)(p*ext2*dy2 + 0.5);

        for (int y = 0; y < h; ++y) {
            int row = y * w;
            int x = 0;
            __m128i zero = _mm_setzero_si128();
            for (; x + 4 <= w; x += 4) {
                __m128i v = zero;
                int sy = y + shift1;
                for (int k = 0; k < 4; ++k) {
                    ((uint32_t*)&v)[k] = get_pixel_safe(in2, w, h, x + k, sy);
                }
                _mm_storeu_si128((__m128i*)(out + row + x), v);
            }
            for (; x < w; ++x) {
                out[row + x] = get_pixel_safe(in2, w, h, x, y + shift1);
            }

            simd_composite_clip1(out, in1, w, h, y, shift2, inst->clip2_behavior, p);
        }
        return;
    }

    /* ==================== GENERAL CASE (Diagonal + Blur) ==================== */
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
