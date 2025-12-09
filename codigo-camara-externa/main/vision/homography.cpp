#include "homography.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"

static const char *TAG = "Homography";

static bool solve_linear_system(float A[8][8], float b[8], float x[8])
{
    for (int i = 0; i < 8; ++i)
    {
        int pivot_row = i;
        float max_val = fabsf(A[i][i]);
        for (int r = i + 1; r < 8; ++r)
        {
            float val = fabsf(A[r][i]);
            if (val > max_val)
            {
                max_val = val;
                pivot_row = r;
            }
        }

        if (max_val < 1e-6f)
        {
            return false;
        }

        if (pivot_row != i)
        {
            for (int c = i; c < 8; ++c)
            {
                float tmp = A[i][c];
                A[i][c] = A[pivot_row][c];
                A[pivot_row][c] = tmp;
            }
            float tmp_b = b[i];
            b[i] = b[pivot_row];
            b[pivot_row] = tmp_b;
        }

        float pivot = A[i][i];
        float inv_pivot = 1.0f / pivot;
        for (int c = i; c < 8; ++c)
        {
            A[i][c] *= inv_pivot;
        }
        b[i] *= inv_pivot;

        for (int r = 0; r < 8; ++r)
        {
            if (r == i)
            {
                continue;
            }

            float factor = A[r][i];
            for (int c = i; c < 8; ++c)
            {
                A[r][c] -= factor * A[i][c];
            }
            b[r] -= factor * b[i];
        }
    }

    for (int i = 0; i < 8; ++i)
    {
        x[i] = b[i];
    }
    return true;
}

static void apply_homography(const homography_matrix_t *H,
                            float u,
                            float v,
                            world_point_t *world)
{
    const float *h = H->h;
    const float x_h = h[0] * u + h[1] * v + h[2];
    const float y_h = h[3] * u + h[4] * v + h[5];
    const float w_h = h[6] * u + h[7] * v + h[8];

    if (fabsf(w_h) > 1e-6f && isfinite(x_h) && isfinite(y_h))
    {
        world->x = x_h / w_h;
        world->y = y_h / w_h;
    }
    else
    {
        world->x = 0.0f;
        world->y = 0.0f;
        ESP_LOGW(TAG, "Invalid homography output (w=%.6f)", w_h);
    }
}

void homography_init(homography_matrix_t *H, const float h_coeffs[9])
{
    if (H == NULL || h_coeffs == NULL)
    {
        return;
    }
    memcpy(H->h, h_coeffs, 9 * sizeof(float));
}

void homography_transform(const homography_matrix_t *H,
                          pixel_point_t pixel,
                          world_point_t *world)
{
    if (H == NULL || world == NULL)
    {
        return;
    }

    apply_homography(H,
                     static_cast<float>(pixel.u),
                     static_cast<float>(pixel.v),
                     world);
}

bool homography_calculate(homography_matrix_t *H,
                          const pixel_point_t src_points[4],
                          const world_point_t dst_points[4])
{
    if (H == NULL || src_points == NULL || dst_points == NULL)
    {
        return false;
    }

    float A[8][8] = {0};
    float b_vec[8] = {0};

    for (int i = 0; i < 4; ++i)
    {
        const float u = static_cast<float>(src_points[i].u);
        const float v = static_cast<float>(src_points[i].v);
        const float x = dst_points[i].x;
        const float y = dst_points[i].y;

        int row = i * 2;
        A[row][0] = u;
        A[row][1] = v;
        A[row][2] = 1.0f;
        A[row][6] = -u * x;
        A[row][7] = -v * x;
        b_vec[row] = x;

        ++row;
        A[row][3] = u;
        A[row][4] = v;
        A[row][5] = 1.0f;
        A[row][6] = -u * y;
        A[row][7] = -v * y;
        b_vec[row] = y;
    }

    float h_solution[8] = {0};
    if (!solve_linear_system(A, b_vec, h_solution))
    {
        ESP_LOGE(TAG, "Failed to solve homography system");
        memset(H->h, 0, sizeof(H->h));
        return false;
    }

    memcpy(H->h, h_solution, 8 * sizeof(float));
    H->h[8] = 1.0f;

    ESP_LOGI(TAG, "Homography calculated without OpenCV");
    return true;
}

void homography_load_default(homography_matrix_t *H,
                             int image_width,
                             int image_height,
                             float real_width,
                             float real_height)
{
    if (H == NULL || image_width <= 0 || image_height <= 0)
    {
        return;
    }

    const float sx = real_width / (float)image_width;
    const float sy = real_height / (float)image_height;

    const float default_coeffs[9] = {
        sx, 0.0f, 0.0f,
        0.0f, sy, 0.0f,
        0.0f, 0.0f, 1.0f};

    homography_init(H, default_coeffs);
    ESP_LOGI(TAG, "Loaded default homography (%.2f cm x %.2f cm)",
             real_width, real_height);
}
