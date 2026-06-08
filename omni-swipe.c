#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <frei0r.h>
#include <math.h>

#define PLUGIN_NAME "OmniSwipe"
#define PLUGIN_DESC "A Swiss Army knife for swipe and slide transitions."

/* ====================== PLUGIN INSTANCE DATA ====================== */
typedef struct {
    int width;
    int height;

    double position;           // 0.0 ~ 1.0 (animated)

    int    clip1_axis;         // 0 = Horizontal, 1 = Vertical
    double clip1_angle;        // 0~180° Direction Angle

    int    clip2_axis;         // 0 = Horizontal, 1 = Vertical
    double clip2_angle;        // 0~180° Direction Angle

    int    clip2_behavior;     // 0=Static, 1=Move, 2=Fade
    double speed_curve;        // 0~100%
    double motion_blur;        // 0~100%
} custom_swipe_t;


/* ====================== FORWARD DECLARATIONS ====================== */
static void apply_swipe(custom_swipe_t *inst,
                        uint32_t *out,
                        const uint32_t *in1,   // old / outgoing clip
                        const uint32_t *in2,   // new / incoming clip
                        double linear_progress);


/* ================================================================ */
/* ==================== FREI0R MANDATORY FUNCTIONS ==================== */
/* ================================================================ */

int f0r_init() {
    return 1;
}

void f0r_deinit() {
}

void f0r_get_plugin_info(f0r_plugin_info_t *info) {
    info->name = PLUGIN_NAME;
    info->author = "Your Name";
    info->plugin_type = F0R_PLUGIN_TYPE_MIXER2;
    info->color_model = F0R_COLOR_MODEL_PACKED32;
    info->frei0r_version = FREI0R_MAJOR_VERSION;
    info->major_version = 0;
    info->minor_version = 1;
    info->num_params = 8;
    info->explanation = PLUGIN_DESC;
}

void f0r_get_param_info(f0r_param_info_t *info, int param_index) {
    switch (param_index) {
        case 0: info->name = "position";           info->type = F0R_PARAM_DOUBLE; info->explanation = "Swipe position (progress)"; break;
        case 1: info->name = "axis1";              info->type = F0R_PARAM_DOUBLE; info->explanation = "Clip 1 Axis (0=Horizontal, 1=Vertical)"; break;
        case 2: info->name = "direction_angle1";   info->type = F0R_PARAM_DOUBLE; info->explanation = "Clip 1 Direction Angle (0-180°)"; break;
        case 3: info->name = "axis2";              info->type = F0R_PARAM_DOUBLE; info->explanation = "Clip 2 Axis (0=Horizontal, 1=Vertical)"; break;
        case 4: info->name = "direction_angle2";   info->type = F0R_PARAM_DOUBLE; info->explanation = "Clip 2 Direction Angle (0-180°)"; break;
        case 5: info->name = "clip2behavior";      info->type = F0R_PARAM_DOUBLE; info->explanation = "Clip 2 Behavior (0=Static, 1=Move, 2=Fade)"; break;
        case 6: info->name = "speed_curve";        info->type = F0R_PARAM_DOUBLE; info->explanation = "Speed Curve (%)"; break;
        case 7: info->name = "motion_blur";        info->type = F0R_PARAM_DOUBLE; info->explanation = "Motion Blur (%) - directional, speed synced"; break;
    }
}

f0r_instance_t f0r_construct(unsigned int width, unsigned int height) {
    custom_swipe_t *inst = (custom_swipe_t *)calloc(1, sizeof(custom_swipe_t));
    if (inst) {
        inst->width          = width;
        inst->height         = height;
        inst->position       = 0.0;
        inst->clip1_axis     = 0;
        inst->clip1_angle    = 0.0;
        inst->clip2_axis     = 0;
        inst->clip2_angle    = 180.0;
        inst->clip2_behavior = 1;      // Move (default)
        inst->speed_curve    = 0.0;
        inst->motion_blur    = 0.0;
    }
    return (f0r_instance_t)inst;
}

void f0r_destruct(f0r_instance_t instance) {
    if (instance) free(instance);
}

void f0r_set_param_value(f0r_instance_t instance, f0r_param_t param, int param_index) {
    custom_swipe_t *inst = (custom_swipe_t *)instance;
    switch (param_index) {
        case 0: inst->position       = *(double *)param; break;
        case 1:
            inst->clip1_axis     = (int)(*(double *)param + 0.5);
            if (inst->clip1_axis != 0) inst->clip1_axis = 1;
            break;
        case 2: inst->clip1_angle    = *(double *)param; break;
        case 3:
            inst->clip2_axis     = (int)(*(double *)param + 0.5);
            if (inst->clip2_axis != 0) inst->clip2_axis = 1;
            break;
        case 4: inst->clip2_angle    = *(double *)param; break;
        case 5:
            // Clamp behavior value safely
            inst->clip2_behavior = (int)(*(double *)param + 0.5);
            if (inst->clip2_behavior < 0) {
                inst->clip2_behavior = 0;
            }
            if (inst->clip2_behavior > 2) {
                inst->clip2_behavior = 2;
            }
            break;
        case 6: inst->speed_curve    = *(double *)param; break;
        case 7: inst->motion_blur    = *(double *)param; break;
    }
}

void f0r_get_param_value(f0r_instance_t instance, f0r_param_t param, int param_index) {
    custom_swipe_t *inst = (custom_swipe_t *)instance;
    switch (param_index) {
        case 0: *(double *)param = inst->position;       break;
        case 1: *(double *)param = (double)inst->clip1_axis; break;
        case 2: *(double *)param = inst->clip1_angle;    break;
        case 3: *(double *)param = (double)inst->clip2_axis; break;
        case 4: *(double *)param = inst->clip2_angle;    break;
        case 5: *(double *)param = (double)inst->clip2_behavior; break;
        case 6: *(double *)param = inst->speed_curve;    break;
        case 7: *(double *)param = inst->motion_blur;    break;
    }
}

void f0r_update2(f0r_instance_t instance, double time,
                 const uint32_t *inframe1, const uint32_t *inframe2,
                 const uint32_t *inframe3, uint32_t *outframe) {
    custom_swipe_t *inst = (custom_swipe_t *)instance;
    double progress = inst->position;
    if (progress < 0.0) progress = 0.0;
    if (progress > 1.0) progress = 1.0;

    apply_swipe(inst, outframe, inframe1, inframe2, progress);
}


/* ================================================================ */
/* ====================== HELPER FUNCTIONS ======================== */
/* ================================================================ */

/* Direction calculation */
static double get_clip_angle(int axis, double direction_angle) {
    double base_deg = (axis == 0) ? 0.0 : 90.0;
    double total_deg = base_deg + direction_angle;
    total_deg = fmod(total_deg, 360.0);
    if (total_deg < 0.0) total_deg += 360.0;
    return total_deg * M_PI / 180.0;
}

static void get_clip_vector(int axis, double direction_angle, double *dx, double *dy) {
    double angle = get_clip_angle(axis, direction_angle);
    *dx = cos(angle);
    *dy = sin(angle);
}


/* Speed curve (slow start → fast end) */
static double apply_speed_curve(double progress, double curve_percent) {
    if (curve_percent <= 0.0) return progress;
    if (curve_percent >= 100.0) return (progress >= 1.0 ? 1.0 : 0.0);

    double t = curve_percent / 100.0;
    double power = 1.0 + t * 8.0;           // stronger curve = more acceleration
    return pow(progress, power);
}

static double get_instant_speed(double progress, double curve_percent) {
    if (curve_percent <= 0.0) return 1.0;
    if (curve_percent >= 100.0) return 0.0;
    if (progress <= 0.0) return 0.0;

    double t = curve_percent / 100.0;
    double power = 1.0 + t * 8.0;
    if (power <= 1.0) return 1.0;
    return power * pow(progress, power - 1.0);
}


/* Directional blurred sampling */
static uint32_t sample_blurred(const uint32_t *in, int w, int h,
                               int x, int y, double dx, double dy,
                               double blur_amount) {
    if (blur_amount <= 0.5) {
        if (x >= 0 && x < w && y >= 0 && y < h)
            return in[y * w + x];
        return 0;
    }

    int steps = (int)(blur_amount * 0.12) + 1;
    if (steps > 12) steps = 12;

    double r = 0, g = 0, b = 0, a = 0;
    double total_weight = 0.0;

    for (int i = -steps; i <= steps; i++) {
        double t = (double)i / (steps + 0.5);
        int sx = x + (int)round(t * blur_amount * dx);
        int sy = y + (int)round(t * blur_amount * dy);

        if (sx >= 0 && sx < w && sy >= 0 && sy < h) {
            uint32_t pixel = in[sy * w + sx];
            double weight = 1.0 - fabs(t) * 0.7;   // center weighted

            r += ((pixel >> 16) & 0xFF) * weight;
            g += ((pixel >> 8)  & 0xFF) * weight;
            b += (pixel & 0xFF) * weight;
            a += ((pixel >> 24) & 0xFF) * weight;
            total_weight += weight;
        }
    }

    if (total_weight > 0.0) {
        r /= total_weight; g /= total_weight; b /= total_weight; a /= total_weight;
        return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
    return 0;
}


/* ====================== MAIN RENDERING ====================== */

static void apply_swipe(custom_swipe_t *inst,
                        uint32_t *out,
                        const uint32_t *in1,   // outgoing (old clip)
                        const uint32_t *in2,   // incoming (new clip)
                        double linear_progress) {

    int w = inst->width;
    int h = inst->height;

    /* Apply easing */
    double progress      = apply_speed_curve(linear_progress, inst->speed_curve);
    double instant_speed = get_instant_speed(linear_progress, inst->speed_curve);
    double blur_strength = inst->motion_blur * 0.08 * instant_speed;

    /* Direction vectors */
    double dx1, dy1, dx2, dy2;
    get_clip_vector(inst->clip1_axis, inst->clip1_angle, &dx1, &dy1);
    get_clip_vector(inst->clip2_axis, inst->clip2_angle, &dx2, &dy2);

    double extent1 = w * fabs(dx1) + h * fabs(dy1);
    double extent2 = w * fabs(dx2) + h * fabs(dy2);

    /* Clip 1 always moves as incoming */
    double offset1 = (1.0 - progress) * extent1;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {

            /* === Clip 1 (Incoming - Top layer) === */
            int sx1 = x + (int)round(offset1 * dx1);
            int sy1 = y + (int)round(offset1 * dy1);
            uint32_t pixel = sample_blurred(in2, w, h, sx1, sy1, dx1, dy1, blur_strength);

            /* === Clip 2 (Outgoing - Bottom layer) === */
            if (pixel == 0) {
                uint32_t clip2_pixel = 0;

                if (inst->clip2_behavior == 0) {           /* Static */
                    clip2_pixel = sample_blurred(in1, w, h, x, y, 0.0, 0.0, blur_strength);
                }
                else if (inst->clip2_behavior == 1) {      /* Move */
                    double offset2 = progress * extent2;
                    int sx2 = x + (int)round(offset2 * dx2);
                    int sy2 = y + (int)round(offset2 * dy2);
                    clip2_pixel = sample_blurred(in1, w, h, sx2, sy2, dx2, dy2, blur_strength);
                }
                else if (inst->clip2_behavior == 2) {      /* Fade */
                    clip2_pixel = sample_blurred(in1, w, h, x, y, 0.0, 0.0, blur_strength);
                    double fade = 1.0 - progress;
                    uint8_t a = (uint8_t)(((clip2_pixel >> 24) & 0xFF) * fade);
                    uint8_t r = (uint8_t)(((clip2_pixel >> 16) & 0xFF) * fade);
                    uint8_t g = (uint8_t)(((clip2_pixel >> 8)  & 0xFF) * fade);
                    uint8_t b = (uint8_t)((clip2_pixel & 0xFF) * fade);
                    clip2_pixel = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                }

                pixel = clip2_pixel;
            }

            out[y * w + x] = pixel;
        }
    }
}
