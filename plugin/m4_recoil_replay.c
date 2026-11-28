#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "m4_recoil.h"

#define ROWS 8192

static double at[ROWS], pitch[ROWS], sent[ROWS], hand[ROWS], history_at[ROWS + 512], history_view[ROWS + 512];
static int shots[ROWS], rows;

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    FILE *file = fopen(argv[1], "r");
    if (!file) return 2;
    int lag_rows = argc > 2 ? atoi(argv[2]) : 2;
    double frame_ms = argc > 3 ? atof(argv[3]) : 10;
    int from_shot = argc > 4 ? atoi(argv[4]) : 1;
    char line[512];
    while (rows < ROWS && fgets(line, sizeof(line), file)) {
        int pitched, lag, released, sent_dy, player_dy, injected_dy;
        double budget, floor_up, anchor;
        if (sscanf(line, "%lf %d %lf %lf %lf %lf %lf %d %d %d %d %d %d", &at[rows], &pitched, &pitch[rows], &sent[rows], &budget, &floor_up,
                   &anchor, &lag, &released, &shots[rows], &sent_dy, &player_dy, &injected_dy) == 13)
            rows++;
    }
    fclose(file);
    if (rows < 2) return 2;
    for (int i = 0; i < rows; i++) hand[i] = pitch[i] - pitch[0] - sent[i < lag_rows ? 0 : i - lag_rows];
    recoil_state s;
    recoil_tuning t = RECOIL_CS_TUNING;
    t.camera_scale = 2.5;
    t.shake_scale = 0.15;
    t.cycle_ms = 73;
    t.ceiling_deg = 9;
    t.settle_deg_s = 1100 / t.counts_per_degree;
    recoil_init(&s, &RECOIL_M4A4);
    long up = 0;
    int fired = 0, seen_count = 0, total = shots[rows - 1];
    double sent_before = 0, end = at[rows - 1], now = 0;
    for (int row = 0; now <= end + 1500; now = row + 1 < rows ? at[++row] : now + RECOIL_STEP_MS) {
        double player = hand[row < rows ? row : rows - 1];
        history_at[seen_count] = now;
        history_view[seen_count] = pitch[0] - up / t.counts_per_degree + player;
        if (seen_count + 1 < ROWS + 512) seen_count++;
        double wanted = floor((now - lag_rows * RECOIL_STEP_MS) / frame_ms) * frame_ms, seen = history_view[0];
        for (int k = seen_count - 1; k >= 0; k--)
            if (history_at[k] <= wanted) {
                seen = history_view[k];
                break;
            }
        int recorded = shots[row < rows ? row : rows - 1];
        while (fired < recorded) {
            recoil_fire(&s, &t, row > 0 && row < rows ? (at[row - 1] + now) / 2 : now, 1, seen);
            if (++fired == from_shot) sent_before = -up / t.counts_per_degree;
        }
        int move_side, move_up;
        recoil_step(&s, &t, now, 1, seen, &move_side, &move_up);
        up += move_up;
    }
    printf("game climb %.2f replay climb %.2f climb from shot %d of %d %.2f\n", sent[rows - 1], -up / t.counts_per_degree, from_shot, total,
           -up / t.counts_per_degree - sent_before);
    return 0;
}
