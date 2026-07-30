#include "hwr_projection.h"

#include <limits.h>
#include <math.h>

static int64_t hwr_scale_about_midpoint(int32_t value,
                                        uint32_t source_extent,
                                        uint32_t target_extent,
                                        uint32_t numerator,
                                        uint32_t denominator)
{
    int64_t source_twice = (int64_t)value * 2 - source_extent;
    int64_t scaled_twice;
    if (source_twice >= 0)
        scaled_twice =
            (source_twice * numerator + denominator / 2u) / denominator;
    else
        scaled_twice =
            -((-source_twice * numerator + denominator / 2u) / denominator);
    if (((int64_t)target_extent + scaled_twice) >= 0)
        return ((int64_t)target_extent + scaled_twice + 1) / 2;
    return ((int64_t)target_extent + scaled_twice - 1) / 2;
}

int hwr_projection_scale_camera(struct hwr_gl_camera *camera,
                                uint32_t source_width,
                                uint32_t source_height,
                                uint32_t target_width,
                                uint32_t target_height,
                                float fov_deg)
{
    int64_t focal_x;
    int64_t focal_y;
    int64_t center_x;
    int64_t horizon;
    int64_t shade_x;
    int64_t shade_y;
    double fov_scale = 1.0;

    if (camera == NULL || source_width == 0 || source_height == 0 ||
        target_width == 0 || target_height == 0 ||
        source_width > UINT16_MAX || source_height > UINT16_MAX)
        return 0;
    camera->source_width = (uint16_t)source_width;
    camera->source_height = (uint16_t)source_height;
    camera->source_focal_x = camera->focal_x;
    camera->source_focal_y = camera->focal_y;
    camera->source_center_x = camera->center_x;
    camera->source_horizon = camera->horizon;
    camera->source_shade_origin_x = camera->shade_origin_x;
    camera->source_shade_origin_y = camera->shade_origin_y;

    /* fov_deg is the horizontal FOV at the engine's native aspect. Keeping
     * that reference stable makes 0 mean exact stock projection, while a wide
     * target follows Hor+ policy by preserving the resulting vertical FOV. */
    if (fov_deg != 0.0f) {
        double half_radians = (double)fov_deg * 0.00872664625997164788;
        double requested_focal =
            ((double)source_width * 0.5) / tan(half_radians);
        if (!isfinite(requested_focal) || requested_focal <= 0.0)
            return 0;
        fov_scale = requested_focal / camera->source_focal_x;
    }

    /* Both focal axes scale with framebuffer height. Scaling X by target width
     * would preserve the old horizontal FOV and stretch the world at a new
     * aspect; height-based scaling instead keeps vertical framing and exposes
     * additional horizontal view area. */
    focal_x = (int64_t)llround(
        (double)camera->source_focal_x * fov_scale *
        (double)target_height / (double)source_height);
    focal_y = (int64_t)llround(
        (double)camera->source_focal_y * fov_scale *
        (double)target_height / (double)source_height);
    center_x = hwr_scale_about_midpoint(
        camera->source_center_x, source_width, target_width,
        target_height, source_height);
    horizon = hwr_scale_about_midpoint(
        camera->source_horizon, source_height, target_height,
        target_height, source_height);
    shade_x = hwr_scale_about_midpoint(
        camera->shade_origin_x, source_width, target_width,
        target_height, source_height);
    shade_y = hwr_scale_about_midpoint(
        camera->shade_origin_y, source_height, target_height,
        target_height, source_height);
    if (focal_x <= 0 || focal_x > UINT16_MAX ||
        focal_y <= 0 || focal_y > UINT16_MAX ||
        center_x < INT32_MIN || center_x > INT32_MAX ||
        horizon < INT32_MIN || horizon > INT32_MAX ||
        shade_x < INT16_MIN || shade_x > INT16_MAX ||
        shade_y < INT16_MIN || shade_y > INT16_MAX)
        return 0;
    camera->focal_x = (uint16_t)focal_x;
    camera->focal_y = (uint16_t)focal_y;
    camera->center_x = (int32_t)center_x;
    camera->horizon = (int32_t)horizon;
    camera->shade_origin_x = (int16_t)shade_x;
    camera->shade_origin_y = (int16_t)shade_y;
    return 1;
}

double hwr_projection_map_x(const struct hwr_gl_camera *camera,
                            double source_x)
{
    if (camera == NULL || camera->source_focal_x == 0)
        return source_x;
    return (double)camera->center_x +
           (source_x - (double)camera->source_center_x) *
           (double)camera->focal_x / (double)camera->source_focal_x;
}

int64_t hwr_projection_map_x_fixed(const struct hwr_gl_camera *camera,
                                   int32_t source_x)
{
    int64_t delta;
    if (camera == NULL || camera->source_focal_x == 0)
        return (int64_t)source_x * 65536;
    delta = (int64_t)source_x - camera->source_center_x;
    return (int64_t)camera->center_x * 65536 +
           (delta * camera->focal_x * 65536) /
               camera->source_focal_x;
}

double hwr_projection_source_x(const struct hwr_gl_camera *camera,
                               double target_x)
{
    if (camera == NULL || camera->focal_x == 0)
        return target_x;
    return (double)camera->source_center_x +
           (target_x - (double)camera->center_x) *
           (double)camera->source_focal_x / (double)camera->focal_x;
}

double hwr_projection_source_y(const struct hwr_gl_camera *camera,
                               double target_y)
{
    if (camera == NULL || camera->focal_y == 0)
        return target_y;
    return (double)camera->source_horizon +
           (target_y - (double)camera->horizon) *
           (double)camera->source_focal_y / (double)camera->focal_y;
}

int hwr_projection_source_x_bounds(const struct hwr_gl_camera *camera,
                                   uint32_t target_width,
                                   int *left, int *right)
{
    int64_t lo;
    int64_t hi;
    if (camera == NULL || left == NULL || right == NULL ||
        camera->focal_x == 0 || camera->source_focal_x == 0 ||
        target_width == 0)
        return 0;
    lo = camera->source_center_x +
         ((-(int64_t)camera->center_x) * camera->source_focal_x) /
             camera->focal_x;
    hi = camera->source_center_x +
         (((int64_t)target_width - camera->center_x) *
              camera->source_focal_x) /
             camera->focal_x;
    if (lo < INT16_MIN)
        lo = INT16_MIN;
    if (hi > INT16_MAX)
        hi = INT16_MAX;
    if (lo >= hi)
        return 0;
    *left = (int)lo;
    *right = (int)hi;
    return 1;
}
