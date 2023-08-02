/*
 * Open Surge Engine
 * physicsactor.c - physics system: actor
 * Copyright (C) 2008-2023  Alexandre Martins <alemartf@gmail.com>
 * http://opensurge2d.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <math.h>
#include "physicsactor.h"
#include "sensor.h"
#include "obstaclemap.h"
#include "obstacle.h"
#include "../core/image.h"
#include "../core/color.h"
#include "../core/video.h"
#include "../core/input.h"
#include "../core/timer.h"
#include "../util/numeric.h"
#include "../util/util.h"

/* physicsactor class */
struct physicsactor_t
{
    v2d_t position; /* center of the sprite */
    float xsp; /* x speed */
    float ysp; /* y speed */
    float gsp; /* ground speed */
    float acc; /* acceleration */
    float dec; /* deceleration */
    float frc; /* friction */
    float capspeed; /* cap speed */
    float topspeed; /* top speed */
    float topyspeed; /* top y speed */
    float air; /* air acceleration */
    float airdrag; /* air drag (friction) */
    float jmp; /* initial jump velocity */
    float jmprel; /* release jump velocity */
    float diejmp; /* death jump velocity */
    float hitjmp; /* get hit jump velocity */
    float grv; /* gravity */
    float slp; /* slope factor */
    float chrg; /* charge-and-release max speed */
    float rollfrc; /* roll friction */
    float rolldec; /* roll deceleration */
    float rolluphillslp; /* roll uphill slope */
    float rolldownhillslp; /* roll downhill slope */
    float rollthreshold; /* roll threshold */
    float unrollthreshold; /* unroll threshold */
    float walkthreshold; /* walk threshold */
    float falloffthreshold; /* fall off threshold */
    float brakingthreshold; /* braking animation treshold */
    float airdragthreshold; /* air drag threshold */
    float airdragxthreshold; /* air drag x-threshold */
    float chrgthreshold; /* charge intensity threshold */
    float waittime; /* wait time in seconds */
    int angle; /* angle (0-255 clockwise) */
    bool midair; /* is the player midair? */
    bool was_midair; /* was the player midair in the previous frame of the simulation? */
    bool facing_right; /* is the player facing right? */
    bool touching_ceiling; /* is the player touching a ceiling? */
    bool inside_wall; /* inside a solid brick, possibly smashed */
    bool winning_pose; /* winning pose enabled? */
    float hlock_timer; /* horizontal control lock timer, in seconds */
    float jump_lock_timer; /* jump lock timer, in seconds */
    float wait_timer; /* how long has the physics actor been stopped, in seconds */
    float midair_timer; /* how long has the physics actor been midair, in second */
    float breathe_timer; /* if greater than zero, set animation to breathing */
    bool sticky_lock; /* sticky physics lock */
    float charge_intensity; /* charge intensity */
    float airdrag_coefficient[2]; /* airdrag approx. coefficient */
    physicsactorstate_t state; /* state */
    movmode_t movmode; /* current movement mode, based on the angle */
    obstaclelayer_t layer; /* current layer */
    input_t *input; /* input device */

    sensor_t *A_normal; /* sensors */
    sensor_t *B_normal;
    sensor_t *C_normal;
    sensor_t *D_normal;
    sensor_t *M_normal;
    sensor_t *N_normal;
    sensor_t *U_normal;
    sensor_t *A_intheair;
    sensor_t *B_intheair;
    sensor_t *C_intheair;
    sensor_t *D_intheair;
    sensor_t *M_intheair;
    sensor_t *N_intheair;
    sensor_t *U_intheair;
    sensor_t *A_jumproll;
    sensor_t *B_jumproll;
    sensor_t *C_jumproll;
    sensor_t *D_jumproll;
    sensor_t *M_jumproll;
    sensor_t *N_jumproll;
    sensor_t *U_jumproll;
    v2d_t angle_sensor[2];

    float reference_time; /* used in fixed_update */
    float fixed_time;
};

/* private stuff ;-) */
static inline void render_ball(v2d_t sensor_position, int radius, color_t color, v2d_t camera_position);
static inline char pick_the_best_ground(const physicsactor_t *pa, const obstacle_t *a, const obstacle_t *b, const sensor_t *a_sensor, const sensor_t *b_sensor);
static inline char pick_the_best_ceiling(const physicsactor_t *pa, const obstacle_t *c, const obstacle_t *d, const sensor_t *c_sensor, const sensor_t *d_sensor);
static inline int distance_between_angle_sensors(const physicsactor_t* pa);
static inline int delta_angle(int alpha, int beta);
static const int CLOUD_OFFSET = 12;

/* physics simulation */
#define WANT_JUMP_ATTENUATION   0
#define WANT_FIXED_TIMESTEP     1
#define TARGET_FPS              60.0f
#define FIXED_TIMESTEP          (1.0f / TARGET_FPS)
static void run_simulation(physicsactor_t *pa, const obstaclemap_t *obstaclemap, float dt);
static void update_sensors(physicsactor_t* pa, const obstaclemap_t* obstaclemap, obstacle_t const** const at_A, obstacle_t const** const at_B, obstacle_t const** const at_C, obstacle_t const** const at_D, obstacle_t const** const at_M, obstacle_t const** const at_N);
static void update_movmode(physicsactor_t* pa);


/*
 * The character has a few sensors:
 * the dot '.' represents the position of the character;
 * sensors specified relative to this dot
 *
 *                                     U
 * A (vertical; left bottom)          ---
 * B (vertical; right bottom)       C | | D
 * C (vertical; left top)           M -.- N
 * D (vertical; right top)          A | | B
 * M (horizontal; left middle)      ^^^^^^^
 * N (horizontal; right middle)      ground
 * U (horizontal; up)
 *
 * The position of the sensors change according to the state of the player.
 * Instead of modifying the coordinates of the sensor, we have multiple,
 * immutable copies of them, and we retrieve them appropriately.
 */
static sensor_t* sensor_A(const physicsactor_t *pa);
static sensor_t* sensor_B(const physicsactor_t *pa);
static sensor_t* sensor_C(const physicsactor_t *pa);
static sensor_t* sensor_D(const physicsactor_t *pa);
static sensor_t* sensor_M(const physicsactor_t *pa);
static sensor_t* sensor_N(const physicsactor_t *pa);
static sensor_t* sensor_U(const physicsactor_t *pa);


/*
 * sine/cosine table
 * In this subsystem, the angle ranges in 0-255 and increases clockwise
 * conversion formula:
 *     degrees = ((256 - angle) * 1.40625) % 360
 *     angle = (256 - degrees / 1.40625) % 256
 *
 * ps: 180/128 = 1.40625
 */

/* use these macros to get the values */
#define SIN(a) cos_table[((a) + 0x40) & 0xFF]
#define COS(a) cos_table[(a) & 0xFF]
static const float cos_table[256] = {
     1.00000f,  0.99970f,  0.99880f,  0.99729f,  0.99518f,  0.99248f,  0.98918f,  0.98528f, 
     0.98079f,  0.97570f,  0.97003f,  0.96378f,  0.95694f,  0.94953f,  0.94154f,  0.93299f, 
     0.92388f,  0.91421f,  0.90399f,  0.89322f,  0.88192f,  0.87009f,  0.85773f,  0.84485f, 
     0.83147f,  0.81758f,  0.80321f,  0.78835f,  0.77301f,  0.75721f,  0.74095f,  0.72425f, 
     0.70711f,  0.68954f,  0.67156f,  0.65317f,  0.63439f,  0.61523f,  0.59570f,  0.57581f, 
     0.55557f,  0.53500f,  0.51410f,  0.49290f,  0.47140f,  0.44961f,  0.42755f,  0.40524f, 
     0.38268f,  0.35990f,  0.33689f,  0.31368f,  0.29028f,  0.26671f,  0.24298f,  0.21910f, 
     0.19509f,  0.17096f,  0.14673f,  0.12241f,  0.09802f,  0.07356f,  0.04907f,  0.02454f, 
     0.00000f, -0.02454f, -0.04907f, -0.07356f, -0.09802f, -0.12241f, -0.14673f, -0.17096f, 
    -0.19509f, -0.21910f, -0.24298f, -0.26671f, -0.29028f, -0.31368f, -0.33689f, -0.35990f, 
    -0.38268f, -0.40524f, -0.42755f, -0.44961f, -0.47140f, -0.49290f, -0.51410f, -0.53500f, 
    -0.55557f, -0.57581f, -0.59570f, -0.61523f, -0.63439f, -0.65317f, -0.67156f, -0.68954f, 
    -0.70711f, -0.72425f, -0.74095f, -0.75721f, -0.77301f, -0.78835f, -0.80321f, -0.81758f, 
    -0.83147f, -0.84485f, -0.85773f, -0.87009f, -0.88192f, -0.89322f, -0.90399f, -0.91421f, 
    -0.92388f, -0.93299f, -0.94154f, -0.94953f, -0.95694f, -0.96378f, -0.97003f, -0.97570f, 
    -0.98079f, -0.98528f, -0.98918f, -0.99248f, -0.99518f, -0.99729f, -0.99880f, -0.99970f, 
    -1.00000f, -0.99970f, -0.99880f, -0.99729f, -0.99518f, -0.99248f, -0.98918f, -0.98528f, 
    -0.98079f, -0.97570f, -0.97003f, -0.96378f, -0.95694f, -0.94953f, -0.94154f, -0.93299f, 
    -0.92388f, -0.91421f, -0.90399f, -0.89322f, -0.88192f, -0.87009f, -0.85773f, -0.84485f, 
    -0.83147f, -0.81758f, -0.80321f, -0.78835f, -0.77301f, -0.75721f, -0.74095f, -0.72425f, 
    -0.70711f, -0.68954f, -0.67156f, -0.65317f, -0.63439f, -0.61523f, -0.59570f, -0.57581f, 
    -0.55557f, -0.53500f, -0.51410f, -0.49290f, -0.47140f, -0.44961f, -0.42756f, -0.40524f, 
    -0.38268f, -0.35990f, -0.33689f, -0.31368f, -0.29028f, -0.26671f, -0.24298f, -0.21910f, 
    -0.19509f, -0.17096f, -0.14673f, -0.12241f, -0.09802f, -0.07356f, -0.04907f, -0.02454f, 
    -0.00000f,  0.02454f,  0.04907f,  0.07356f,  0.09802f,  0.12241f,  0.14673f,  0.17096f, 
     0.19509f,  0.21910f,  0.24298f,  0.26671f,  0.29028f,  0.31368f,  0.33689f,  0.35990f, 
     0.38268f,  0.40524f,  0.42756f,  0.44961f,  0.47140f,  0.49290f,  0.51410f,  0.53500f, 
     0.55557f,  0.57581f,  0.59570f,  0.61523f,  0.63439f,  0.65317f,  0.67156f,  0.68954f, 
     0.70711f,  0.72425f,  0.74095f,  0.75721f,  0.77301f,  0.78835f,  0.80321f,  0.81758f, 
     0.83147f,  0.84485f,  0.85773f,  0.87009f,  0.88192f,  0.89322f,  0.90399f,  0.91421f, 
     0.92388f,  0.93299f,  0.94154f,  0.94953f,  0.95694f,  0.96378f,  0.97003f,  0.97570f, 
     0.98079f,  0.98528f,  0.98918f,  0.99248f,  0.99518f,  0.99729f,  0.99880f,  0.99970f
};

/* slope table: stored angles */
/* SLOPE(y,x) is the angle of the (y,x) slope, where -SLOPE_LIMIT <= y,x <= SLOPE_LIMIT */
#define SLOPE_LIMIT 11
#define SLOPE(y,x) slp_table[(SLOPE_LIMIT) + ((y) + ((y)<-(SLOPE_LIMIT))*(-(SLOPE_LIMIT)-(y)) + ((y)>(SLOPE_LIMIT))*((SLOPE_LIMIT)-(y)))][(SLOPE_LIMIT) + ((x) + ((x)<-(SLOPE_LIMIT))*(-(SLOPE_LIMIT)-(x)) + ((x)>(SLOPE_LIMIT))*((SLOPE_LIMIT)-(x)))]
static const int slp_table[23][23] = {
    { 0xA0, 0xA2, 0xA4, 0xA6, 0xA9, 0xAC, 0xAF, 0xB2, 0xB5, 0xB9, 0xBC, 0xC0, 0xC4, 0xC7, 0xCB, 0xCE, 0xD1, 0xD4, 0xD7, 0xDA, 0xDC, 0xDE, 0xE0 },
    { 0x9E, 0xA0, 0xA2, 0xA5, 0xA7, 0xAA, 0xAD, 0xB0, 0xB4, 0xB8, 0xBC, 0xC0, 0xC4, 0xC8, 0xCC, 0xD0, 0xD3, 0xD6, 0xD9, 0xDB, 0xDE, 0xE0, 0xE2 },
    { 0x9C, 0x9E, 0xA0, 0xA2, 0xA5, 0xA8, 0xAB, 0xAF, 0xB3, 0xB7, 0xBB, 0xC0, 0xC5, 0xC9, 0xCD, 0xD1, 0xD5, 0xD8, 0xDB, 0xDE, 0xE0, 0xE2, 0xE4 },
    { 0x9A, 0x9B, 0x9E, 0xA0, 0xA3, 0xA6, 0xA9, 0xAD, 0xB1, 0xB6, 0xBB, 0xC0, 0xC5, 0xCA, 0xCF, 0xD3, 0xD7, 0xDA, 0xDD, 0xE0, 0xE2, 0xE5, 0xE6 },
    { 0x97, 0x99, 0x9B, 0x9D, 0xA0, 0xA3, 0xA7, 0xAB, 0xB0, 0xB5, 0xBA, 0xC0, 0xC6, 0xCB, 0xD0, 0xD5, 0xD9, 0xDD, 0xE0, 0xE3, 0xE5, 0xE7, 0xE9 },
    { 0x94, 0x96, 0x98, 0x9A, 0x9D, 0xA0, 0xA4, 0xA8, 0xAD, 0xB3, 0xB9, 0xC0, 0xC7, 0xCD, 0xD3, 0xD8, 0xDC, 0xE0, 0xE3, 0xE6, 0xE8, 0xEA, 0xEC },
    { 0x91, 0x93, 0x95, 0x97, 0x99, 0x9C, 0xA0, 0xA5, 0xAA, 0xB0, 0xB8, 0xC0, 0xC8, 0xD0, 0xD6, 0xDB, 0xE0, 0xE4, 0xE7, 0xE9, 0xEB, 0xED, 0xEF },
    { 0x8E, 0x90, 0x91, 0x93, 0x95, 0x98, 0x9B, 0xA0, 0xA6, 0xAD, 0xB6, 0xC0, 0xCA, 0xD3, 0xDA, 0xE0, 0xE5, 0xE8, 0xEB, 0xED, 0xEF, 0xF0, 0xF2 },
    { 0x8B, 0x8C, 0x8D, 0x8F, 0x90, 0x93, 0x96, 0x9A, 0xA0, 0xA8, 0xB3, 0xC0, 0xCD, 0xD8, 0xE0, 0xE6, 0xEA, 0xED, 0xF0, 0xF1, 0xF3, 0xF4, 0xF5 },
    { 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8D, 0x90, 0x93, 0x98, 0xA0, 0xAD, 0xC0, 0xD3, 0xE0, 0xE8, 0xED, 0xF0, 0xF3, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9 },
    { 0x84, 0x84, 0x85, 0x85, 0x86, 0x87, 0x88, 0x8A, 0x8D, 0x93, 0xA0, 0xC0, 0xE0, 0xED, 0xF3, 0xF6, 0xF8, 0xF9, 0xFA, 0xFB, 0xFB, 0xFC, 0xFC },
    { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x7C, 0x7C, 0x7B, 0x7B, 0x7A, 0x79, 0x78, 0x76, 0x73, 0x6D, 0x60, 0x40, 0x20, 0x13, 0x0D, 0x0A, 0x08, 0x07, 0x06, 0x05, 0x05, 0x04, 0x04 },
    { 0x79, 0x78, 0x77, 0x76, 0x75, 0x73, 0x70, 0x6D, 0x68, 0x60, 0x53, 0x40, 0x2D, 0x20, 0x18, 0x13, 0x10, 0x0D, 0x0B, 0x0A, 0x09, 0x08, 0x07 },
    { 0x75, 0x74, 0x73, 0x71, 0x70, 0x6D, 0x6A, 0x66, 0x60, 0x58, 0x4D, 0x40, 0x33, 0x28, 0x20, 0x1A, 0x16, 0x13, 0x10, 0x0F, 0x0D, 0x0C, 0x0B },
    { 0x72, 0x70, 0x6F, 0x6D, 0x6B, 0x68, 0x65, 0x60, 0x5A, 0x53, 0x4A, 0x40, 0x36, 0x2D, 0x26, 0x20, 0x1B, 0x18, 0x15, 0x13, 0x11, 0x10, 0x0E },
    { 0x6F, 0x6D, 0x6B, 0x69, 0x67, 0x64, 0x60, 0x5B, 0x56, 0x50, 0x48, 0x40, 0x38, 0x30, 0x2A, 0x25, 0x20, 0x1C, 0x19, 0x17, 0x15, 0x13, 0x11 },
    { 0x6C, 0x6A, 0x68, 0x66, 0x63, 0x60, 0x5C, 0x58, 0x53, 0x4D, 0x47, 0x40, 0x39, 0x33, 0x2D, 0x28, 0x24, 0x20, 0x1D, 0x1A, 0x18, 0x16, 0x14 },
    { 0x69, 0x67, 0x65, 0x63, 0x60, 0x5D, 0x59, 0x55, 0x50, 0x4B, 0x46, 0x40, 0x3A, 0x35, 0x30, 0x2B, 0x27, 0x23, 0x20, 0x1D, 0x1B, 0x19, 0x17 },
    { 0x66, 0x65, 0x62, 0x60, 0x5D, 0x5A, 0x57, 0x53, 0x4F, 0x4A, 0x45, 0x40, 0x3B, 0x36, 0x31, 0x2D, 0x29, 0x26, 0x23, 0x20, 0x1E, 0x1B, 0x1A },
    { 0x64, 0x62, 0x60, 0x5E, 0x5B, 0x58, 0x55, 0x51, 0x4D, 0x49, 0x45, 0x40, 0x3B, 0x37, 0x33, 0x2F, 0x2B, 0x28, 0x25, 0x22, 0x20, 0x1E, 0x1C },
    { 0x62, 0x60, 0x5E, 0x5B, 0x59, 0x56, 0x53, 0x50, 0x4C, 0x48, 0x44, 0x40, 0x3C, 0x38, 0x34, 0x30, 0x2D, 0x2A, 0x27, 0x25, 0x22, 0x20, 0x1E },
    { 0x60, 0x5E, 0x5C, 0x5A, 0x57, 0x54, 0x51, 0x4E, 0x4B, 0x47, 0x44, 0x40, 0x3C, 0x39, 0x35, 0x32, 0x2F, 0x2C, 0x29, 0x26, 0x24, 0x22, 0x20 }
};

/* public methods */
physicsactor_t* physicsactor_create(v2d_t position)
{
    physicsactor_t *pa = mallocx(sizeof *pa);

    /* initializing... */
    pa->position = position;
    pa->xsp = 0.0f;
    pa->ysp = 0.0f;
    pa->gsp = 0.0f;
    pa->angle = 0x0;
    pa->movmode = MM_FLOOR;
    pa->state = PAS_STOPPED;
    pa->layer = OL_DEFAULT;
    pa->midair = true;
    pa->midair_timer = 0.0f;
    pa->hlock_timer = 0.0f;
    pa->jump_lock_timer = 0.0f;
    pa->facing_right = true;
    pa->touching_ceiling = false;
    pa->input = input_create_computer();
    pa->wait_timer = 0.0f;
    pa->winning_pose = false;
    pa->breathe_timer = 0.0f;
    pa->sticky_lock = false;
    pa->charge_intensity = 0.0f;
    pa->airdrag_coefficient[0] = 0.0f;
    pa->airdrag_coefficient[1] = 1.0f;
    pa->reference_time = 0.0f;
    pa->fixed_time = 0.0f;

    /* initialize the physics model */
    physicsactor_reset_model_parameters(pa);

    /* sensors */
    pa->A_normal = sensor_create_vertical(-9, 0, 20, color_rgb(0,255,0));
    pa->B_normal = sensor_create_vertical(9, 0, 20, color_rgb(255,255,0));
    pa->C_normal = sensor_create_vertical(-9, -24, 0, color_rgb(0,255,0));
    pa->D_normal = sensor_create_vertical(9, -24, 0, color_rgb(255,255,0));
    pa->M_normal = sensor_create_horizontal(4, -10, 0, color_rgb(255,0,0)); /* use x(sensor A) + 1 */
    pa->N_normal = sensor_create_horizontal(4, 0, 10, color_rgb(255,64,255));
    pa->U_normal = sensor_create_horizontal(-4, 0, 0, color_rgb(255,255,255)); /* smash sensor */

    pa->A_intheair = sensor_create_vertical(-9, 0, 20, color_rgb(0,255,0));
    pa->B_intheair = sensor_create_vertical(9, 0, 20, color_rgb(255,255,0));
    pa->C_intheair = sensor_create_vertical(-9, -24, 0, color_rgb(0,255,0));
    pa->D_intheair = sensor_create_vertical(9, -24, 0, color_rgb(255,255,0));
    pa->M_intheair = sensor_create_horizontal(0, -11, 0, color_rgb(255,0,0)); /* use x(sensor M_normal) + 1 */
    pa->N_intheair = sensor_create_horizontal(0, 0, 11, color_rgb(255,64,255));
    pa->U_intheair = sensor_create_horizontal(-4, 0, 0, color_rgb(255,255,255));

    pa->A_jumproll = sensor_create_vertical(-5, 0, 19, color_rgb(0,255,0));
    pa->B_jumproll = sensor_create_vertical(5, 0, 19, color_rgb(255,255,0));
    pa->C_jumproll = sensor_create_vertical(-5, -10, 0, color_rgb(0,255,0));
    pa->D_jumproll = sensor_create_vertical(5, -10, 0, color_rgb(255,255,0));
    pa->M_jumproll = sensor_create_horizontal(0, -11, 0, color_rgb(255,0,0));
    pa->N_jumproll = sensor_create_horizontal(0, 0, 11, color_rgb(255,64,255));
    pa->U_jumproll = sensor_create_horizontal(-4, 0, 0, color_rgb(255,255,255));

    /* success!!! ;-) */
    return pa;
}

physicsactor_t* physicsactor_destroy(physicsactor_t *pa)
{
    sensor_destroy(pa->A_normal);
    sensor_destroy(pa->B_normal);
    sensor_destroy(pa->C_normal);
    sensor_destroy(pa->D_normal);
    sensor_destroy(pa->M_normal);
    sensor_destroy(pa->N_normal);
    sensor_destroy(pa->U_normal);

    sensor_destroy(pa->A_intheair);
    sensor_destroy(pa->B_intheair);
    sensor_destroy(pa->C_intheair);
    sensor_destroy(pa->D_intheair);
    sensor_destroy(pa->M_intheair);
    sensor_destroy(pa->N_intheair);
    sensor_destroy(pa->U_intheair);

    sensor_destroy(pa->A_jumproll);
    sensor_destroy(pa->B_jumproll);
    sensor_destroy(pa->C_jumproll);
    sensor_destroy(pa->D_jumproll);
    sensor_destroy(pa->M_jumproll);
    sensor_destroy(pa->N_jumproll);
    sensor_destroy(pa->U_jumproll);

    input_destroy(pa->input);
    free(pa);
    return NULL;
}

void physicsactor_reset_model_parameters(physicsactor_t* pa)
{
    const float fpsmul = TARGET_FPS;

    /*
      +--------------------+----------------+-----------------+
      | model parameter    |  magic number  | fps multitplier |
      +--------------------+----------------+-----------------+
     */
    pa->acc =                  (3.0f/64.0f) * fpsmul * fpsmul ;
    pa->dec =                   0.5f        * fpsmul * fpsmul ;
    pa->frc =                  (3.0f/64.0f) * fpsmul * fpsmul ;
    pa->capspeed =              16.0f       * fpsmul * 1.0f   ;
    pa->topspeed =              6.0f        * fpsmul * 1.0f   ;
    pa->topyspeed =             16.0f       * fpsmul * 1.0f   ;
    pa->air =                  (6.0f/64.0f) * fpsmul * fpsmul ;
    pa->airdrag =             (31.0f/32.0f) * 1.0f   * 1.0f   ;
    pa->jmp =                   -6.5f       * fpsmul * 1.0f   ;
    pa->jmprel =                -4.0f       * fpsmul * 1.0f   ;
    pa->diejmp =                -7.0f       * fpsmul * 1.0f   ;
    pa->hitjmp =                -4.0f       * fpsmul * 1.0f   ;
    pa->grv =                 (14.0f/64.0f) * fpsmul * fpsmul ;
    pa->slp =                  (8.0f/64.0f) * fpsmul * fpsmul ;
    pa->chrg =                  12.0f       * fpsmul * 1.0f   ;
    pa->walkthreshold =         0.5f        * fpsmul * 1.0f   ;
    pa->unrollthreshold =       0.5f        * fpsmul * 1.0f   ;
    pa->rollthreshold =         1.0f        * fpsmul * 1.0f   ;
    pa->rollfrc =             (3.0f/128.0f) * fpsmul * fpsmul ;
    pa->rolldec =              (8.0f/64.0f) * fpsmul * fpsmul ;
    pa->rolluphillslp =        (5.0f/64.0f) * fpsmul * fpsmul ;
    pa->rolldownhillslp =     (20.0f/64.0f) * fpsmul * fpsmul ;
    pa->falloffthreshold =      2.5f        * fpsmul * 1.0f   ;
    pa->brakingthreshold =      4.0f        * fpsmul * 1.0f   ;
    pa->airdragthreshold =      -4.0f       * fpsmul * 1.0f   ;
    pa->airdragxthreshold =    (8.0f/64.0f) * fpsmul * 1.0f   ;
    pa->chrgthreshold =        (1.0f/64.0f) * 1.0f   * 1.0f   ;
    pa->waittime =              3.0f        * 1.0f   * 1.0f   ;

    /* recompute airdrag coefficients */
    physicsactor_set_airdrag(pa, pa->airdrag);
}

void physicsactor_update(physicsactor_t *pa, const obstaclemap_t *obstaclemap)
{
    float dt = timer_get_delta();
    const obstacle_t *at_U = NULL;

    /* inside a solid brick? */
    at_U = sensor_check(sensor_U(pa), pa->position, pa->movmode, pa->layer, obstaclemap);
    pa->inside_wall = (at_U != NULL && obstacle_is_solid(at_U));

    /* run the physics simulation */
#if WANT_FIXED_TIMESTEP
    pa->reference_time += dt;
    if(pa->reference_time <= pa->fixed_time + FIXED_TIMESTEP) {
        /* will run with a fixed timestep, but only at TARGET_FPS */
        run_simulation(pa, obstaclemap, FIXED_TIMESTEP); /* improved precision */
        pa->fixed_time += FIXED_TIMESTEP;
    }
    else {
        /* prevent jittering at lower fps rates */
        run_simulation(pa, obstaclemap, dt); /* can't use a fixed timestep */
        pa->fixed_time = pa->reference_time;
    }
#else
    run_simulation(pa, obstaclemap, dt);
#endif
}

void physicsactor_render_sensors(const physicsactor_t *pa, v2d_t camera_position)
{
    sensor_render(sensor_A(pa), pa->position, pa->movmode, camera_position);
    sensor_render(sensor_B(pa), pa->position, pa->movmode, camera_position);
    sensor_render(sensor_C(pa), pa->position, pa->movmode, camera_position);
    sensor_render(sensor_D(pa), pa->position, pa->movmode, camera_position);
    sensor_render(sensor_M(pa), pa->position, pa->movmode, camera_position);
    sensor_render(sensor_N(pa), pa->position, pa->movmode, camera_position);
    sensor_render(sensor_U(pa), pa->position, pa->movmode, camera_position);
    render_ball(pa->position, 1, color_rgb(255, 255, 255), camera_position);

    if(!pa->midair) {
        render_ball(pa->angle_sensor[0], 2, sensor_get_color(sensor_A(pa)), camera_position);
        render_ball(pa->angle_sensor[1], 2, sensor_get_color(sensor_B(pa)), camera_position);
    }
}

physicsactorstate_t physicsactor_get_state(const physicsactor_t *pa)
{
    return pa->state;
}

int physicsactor_get_angle(const physicsactor_t *pa)
{
    return (((256 - pa->angle) * 180) / 128) % 360;
}

v2d_t physicsactor_get_position(const physicsactor_t *pa)
{
    return pa->position;
}

void physicsactor_set_position(physicsactor_t *pa, v2d_t position)
{
    pa->position = position;
}

void physicsactor_lock_horizontally_for(physicsactor_t *pa, float seconds)
{
    seconds = max(seconds, 0.0f);
    if(seconds > pa->hlock_timer)
        pa->hlock_timer = seconds;
}

bool physicsactor_ressurrect(physicsactor_t *pa, v2d_t position)
{
    if(pa->state == PAS_DEAD || pa->state == PAS_DROWNED) {
        pa->gsp = 0.0f;
        pa->xsp = 0.0f;
        pa->ysp = 0.0f;
        pa->facing_right = true;
        pa->state = PAS_STOPPED;
        physicsactor_set_position(pa, position);
        return true;
    }

    return false;
}

bool physicsactor_is_midair(const physicsactor_t *pa)
{
    return pa->midair;
}

bool physicsactor_is_touching_ceiling(const physicsactor_t *pa)
{
    return pa->touching_ceiling;
}

bool physicsactor_is_facing_right(const physicsactor_t *pa)
{
    return pa->facing_right;
}

bool physicsactor_is_inside_wall(const physicsactor_t *pa)
{
    return pa->inside_wall;
}

void physicsactor_enable_winning_pose(physicsactor_t *pa)
{
    pa->winning_pose = true;
}

movmode_t physicsactor_get_movmode(const physicsactor_t *pa)
{
    return pa->movmode;
}

obstaclelayer_t physicsactor_get_layer(const physicsactor_t *pa)
{
    return pa->layer;
}

void physicsactor_set_layer(physicsactor_t *pa, obstaclelayer_t layer)
{
    pa->layer = layer;
}

int physicsactor_roll_delta(const physicsactor_t* pa)
{
    /* the difference of the height of the (foot) sensors */
    return sensor_get_y2(pa->A_normal) - sensor_get_y2(pa->A_jumproll);
}

float physicsactor_charge_intensity(const physicsactor_t* pa)
{
    return pa->charge_intensity;
}

void physicsactor_walk_right(physicsactor_t *pa)
{
    input_simulate_button_down(pa->input, IB_RIGHT);
}

void physicsactor_walk_left(physicsactor_t *pa)
{
    input_simulate_button_down(pa->input, IB_LEFT);
}

void physicsactor_duck(physicsactor_t *pa)
{
    input_simulate_button_down(pa->input, IB_DOWN);
}

void physicsactor_look_up(physicsactor_t *pa)
{
    input_simulate_button_down(pa->input, IB_UP);
}

void physicsactor_jump(physicsactor_t *pa)
{
    input_simulate_button_down(pa->input, IB_FIRE1);
}

void physicsactor_kill(physicsactor_t *pa)
{
    pa->state = PAS_DEAD;
}

void physicsactor_hit(physicsactor_t *pa)
{
    pa->state = PAS_GETTINGHIT;
}

void physicsactor_bounce(physicsactor_t *pa)
{
    pa->state = PAS_JUMPING;
}

void physicsactor_spring(physicsactor_t *pa)
{
    pa->state = PAS_SPRINGING;
}

void physicsactor_roll(physicsactor_t *pa)
{
    pa->state = PAS_ROLLING;
}

void physicsactor_drown(physicsactor_t *pa)
{
    pa->state = PAS_DROWNED;
}

void physicsactor_breathe(physicsactor_t *pa)
{
    pa->state = PAS_BREATHING;
    pa->breathe_timer = 0.5f;
}

/* getters and setters */
#define GENERATE_GETTER_AND_SETTER_OF(x) \
float physicsactor_get_##x(const physicsactor_t *pa) \
{ \
    return pa->x; \
} \
\
void physicsactor_set_##x(physicsactor_t *pa, float value) \
{ \
    pa->x = value; \
}

GENERATE_GETTER_AND_SETTER_OF(xsp)
GENERATE_GETTER_AND_SETTER_OF(ysp)
GENERATE_GETTER_AND_SETTER_OF(gsp)
GENERATE_GETTER_AND_SETTER_OF(acc)
GENERATE_GETTER_AND_SETTER_OF(dec)
GENERATE_GETTER_AND_SETTER_OF(frc)
GENERATE_GETTER_AND_SETTER_OF(topspeed)
GENERATE_GETTER_AND_SETTER_OF(air)
GENERATE_GETTER_AND_SETTER_OF(jmp)
GENERATE_GETTER_AND_SETTER_OF(jmprel)
GENERATE_GETTER_AND_SETTER_OF(diejmp)
GENERATE_GETTER_AND_SETTER_OF(hitjmp)
GENERATE_GETTER_AND_SETTER_OF(grv)
GENERATE_GETTER_AND_SETTER_OF(slp)
GENERATE_GETTER_AND_SETTER_OF(chrg)
GENERATE_GETTER_AND_SETTER_OF(rollfrc)
GENERATE_GETTER_AND_SETTER_OF(rolldec)
GENERATE_GETTER_AND_SETTER_OF(rolluphillslp)
GENERATE_GETTER_AND_SETTER_OF(rolldownhillslp)
GENERATE_GETTER_AND_SETTER_OF(walkthreshold)
GENERATE_GETTER_AND_SETTER_OF(rollthreshold)
GENERATE_GETTER_AND_SETTER_OF(unrollthreshold)
GENERATE_GETTER_AND_SETTER_OF(falloffthreshold)
GENERATE_GETTER_AND_SETTER_OF(brakingthreshold)
GENERATE_GETTER_AND_SETTER_OF(airdragthreshold)
GENERATE_GETTER_AND_SETTER_OF(airdragxthreshold)
GENERATE_GETTER_AND_SETTER_OF(chrgthreshold)
GENERATE_GETTER_AND_SETTER_OF(waittime)

float physicsactor_get_airdrag(const physicsactor_t *pa) { return pa->airdrag; }
void physicsactor_set_airdrag(physicsactor_t *pa, float value)
{
    pa->airdrag = clip01(value);
    if(pa->airdrag > 0.0f && pa->airdrag < 1.0f) {
        /* recompute airdrag coefficients */
        pa->airdrag_coefficient[0] = 60.0f * pa->airdrag * logf(pa->airdrag);
        pa->airdrag_coefficient[1] = pa->airdrag * (1.0f - logf(pa->airdrag));
    }
    else if(pa->airdrag > 0.0f) {
        /* no airdrag */
        pa->airdrag_coefficient[0] = 0.0f;
        pa->airdrag_coefficient[1] = 1.0f;
    }
    else {
        pa->airdrag_coefficient[0] = 0.0f;
        pa->airdrag_coefficient[1] = 0.0f;
    }
}


/* sensors */
#define GENERATE_SENSOR_GETTER(x) \
sensor_t* sensor_##x(const physicsactor_t *pa) \
{ \
    if(pa->state == PAS_JUMPING || pa->state == PAS_ROLLING) \
        return pa->x##_jumproll; \
    else if(pa->midair || pa->state == PAS_SPRINGING) \
        return pa->x##_intheair; \
    else \
        return pa->x##_normal; \
}

GENERATE_SENSOR_GETTER(A)
GENERATE_SENSOR_GETTER(B)
GENERATE_SENSOR_GETTER(C)
GENERATE_SENSOR_GETTER(D)
GENERATE_SENSOR_GETTER(M)
GENERATE_SENSOR_GETTER(N)
GENERATE_SENSOR_GETTER(U)


/* get bounding box */
void physicsactor_bounding_box(const physicsactor_t *pa, int *width, int *height, v2d_t *center)
{
    const sensor_t* sensor_a = pa->A_normal; /*sensor_A(pa);*/
    const sensor_t* sensor_d = sensor_D(pa);
    const sensor_t* sensor_m = sensor_M(pa);
    const sensor_t* sensor_n = sensor_N(pa);

    point2d_t a = sensor_tail(sensor_a, pa->position, pa->movmode);
    point2d_t d = sensor_head(sensor_d, pa->position, pa->movmode);
    point2d_t m = sensor_head(sensor_m, pa->position, pa->movmode);
    point2d_t n = sensor_tail(sensor_n, pa->position, pa->movmode);

    int w, h;
    switch(pa->movmode) {
        case MM_FLOOR:
            w = n.x - m.x + 1;
            h = a.y - d.y + 1;
            break;

        case MM_CEILING:
            w = m.x - n.x + 1;
            h = d.y - a.y + 1;
            break;

        case MM_RIGHTWALL:
            w = a.x - d.x + 1;
            h = m.y - n.y + 1;
            break;

        case MM_LEFTWALL:
            w = d.x - a.x + 1;
            h = n.y - m.y + 1;
            break;
    }

    if(width != NULL)
        *width = w;
    if(height != NULL)
        *height = h;
    if(center != NULL)
        *center = pa->position;
}

/* check if the physicsactor is standing on a specific platform (obstacle) */
bool physicsactor_is_standing_on_platform(const physicsactor_t *pa, const obstacle_t *obstacle)
{
    int x1, y1, x2, y2;
    const sensor_t *a = sensor_A(pa);
    const sensor_t *b = sensor_B(pa);

    /* check sensor A */
    sensor_worldpos(a, pa->position, pa->movmode, &x1, &y1, &x2, &y2);
    if(obstacle_got_collision(obstacle, x1, y1, x2, y2))
        return true;

    /* check sensor B */
    sensor_worldpos(b, pa->position, pa->movmode, &x1, &y1, &x2, &y2);
    if(obstacle_got_collision(obstacle, x1, y1, x2, y2))
        return true;

    /* no collision */
    return false;
}



/*
 * ---------------------------------------
 *           PHYSICS ENGINE
 * ---------------------------------------
 */

/* utility macros */
#define WALKING_OR_RUNNING(pa)      ((fabs((pa)->gsp) >= (pa)->topspeed) ? PAS_RUNNING : PAS_WALKING)

/* call UPDATE_SENSORS whenever you update pa->position or pa->angle */
#define UPDATE_SENSORS()            update_sensors(pa, obstaclemap, &at_A, &at_B, &at_C, &at_D, &at_M, &at_N)

/* call UPDATE_MOVMODE whenever you update pa->angle */
#define UPDATE_MOVMODE()            update_movmode(pa)

/* compute the current pa->angle */
#define UPDATE_ANGLE() \
    do { \
        const sensor_t* sensor = sensor_A(pa); \
        int sensor_height = sensor_get_y2(sensor) - sensor_get_y1(sensor); \
        int search_base = sensor_get_y2(sensor) - 1; \
        int max_iterations = sensor_height * 3; \
        int half_dist = distance_between_angle_sensors(pa) / 2; \
        int hoff = half_dist + (1 - half_dist % 2); /* odd number */ \
        int min_hoff = pa->was_midair ? 3 : 1; \
        int max_delta = min(hoff * 2, SLOPE_LIMIT); \
        int current_angle = pa->angle; \
        int angular_tolerance = 0x14; \
        int dx = 0, dy = 0; \
        \
        do { \
            pa->angle = current_angle; /* assume continuity */ \
            UPDATE_ANGLE_STEP(hoff, search_base, max_iterations, dx, dy); \
            hoff -= 2; /* increase precision */ \
        } while(hoff >= min_hoff && (at_M == NULL && at_N == NULL) && (dx < -max_delta || dx > max_delta || dy < -max_delta || dy > max_delta || delta_angle(pa->angle, current_angle) > angular_tolerance)); \
    } while(0)

#define UPDATE_ANGLE_STEP(hoff, search_base, max_iterations, out_dx, out_dy) \
    do { \
        const obstacle_t* gnd = NULL; \
        bool found_a = false, found_b = false; \
        int x, y, xa, ya, xb, yb, ang; \
        float h; \
        \
        for(int i = 0; i < (max_iterations) && !(found_a && found_b); i++) { \
            h = (search_base) + i; \
            x = pa->position.x + h * SIN(pa->angle) + 0.5f; \
            y = pa->position.y + h * COS(pa->angle) + 0.5f; \
            if(!found_a) { \
                xa = x - (hoff) * COS(pa->angle); \
                ya = y + (hoff) * SIN(pa->angle); \
                gnd = obstaclemap_get_best_obstacle_at(obstaclemap, xa, ya, xa, ya, pa->movmode, pa->layer); \
                found_a = (gnd != NULL && (obstacle_is_solid(gnd) || ( \
                    (pa->movmode == MM_FLOOR && ya < obstacle_ground_position(gnd, xa, ya, GD_DOWN) + CLOUD_OFFSET) || \
                    (pa->movmode == MM_CEILING && ya > obstacle_ground_position(gnd, xa, ya, GD_UP) - CLOUD_OFFSET) || \
                    (pa->movmode == MM_LEFTWALL && xa > obstacle_ground_position(gnd, xa, ya, GD_LEFT) - CLOUD_OFFSET) || \
                    (pa->movmode == MM_RIGHTWALL && xa < obstacle_ground_position(gnd, xa, ya, GD_RIGHT) + CLOUD_OFFSET) \
                ))); \
            } \
            if(!found_b) { \
                xb = x + (hoff) * COS(pa->angle); \
                yb = y - (hoff) * SIN(pa->angle); \
                gnd = obstaclemap_get_best_obstacle_at(obstaclemap, xb, yb, xb, yb, pa->movmode, pa->layer); \
                found_b = (gnd != NULL && (obstacle_is_solid(gnd) || ( \
                    (pa->movmode == MM_FLOOR && yb < obstacle_ground_position(gnd, xb, yb, GD_DOWN) + CLOUD_OFFSET) || \
                    (pa->movmode == MM_CEILING && yb > obstacle_ground_position(gnd, xb, yb, GD_UP) - CLOUD_OFFSET) || \
                    (pa->movmode == MM_LEFTWALL && xb > obstacle_ground_position(gnd, xb, yb, GD_LEFT) - CLOUD_OFFSET) || \
                    (pa->movmode == MM_RIGHTWALL && xb < obstacle_ground_position(gnd, xb, yb, GD_RIGHT) + CLOUD_OFFSET) \
                ))); \
            } \
        } \
        \
        out_dx = out_dy = 0; \
        pa->angle_sensor[0] = pa->angle_sensor[1] = pa->position; \
        if(found_a && found_b) { \
            const obstacle_t* ga = obstaclemap_get_best_obstacle_at(obstaclemap, xa, ya, xa, ya, pa->movmode, pa->layer); \
            const obstacle_t* gb = obstaclemap_get_best_obstacle_at(obstaclemap, xb, yb, xb, yb, pa->movmode, pa->layer); \
            if(ga && gb) { \
                switch(pa->movmode) { \
                    case MM_FLOOR: \
                        ya = obstacle_ground_position(ga, xa, ya, GD_DOWN); \
                        yb = obstacle_ground_position(gb, xb, yb, GD_DOWN); \
                        break; \
                    case MM_LEFTWALL: \
                        xa = obstacle_ground_position(ga, xa, ya, GD_LEFT); \
                        xb = obstacle_ground_position(gb, xb, yb, GD_LEFT); \
                        break; \
                    case MM_CEILING: \
                        ya = obstacle_ground_position(ga, xa, ya, GD_UP); \
                        yb = obstacle_ground_position(gb, xb, yb, GD_UP); \
                        break; \
                    case MM_RIGHTWALL: \
                        xa = obstacle_ground_position(ga, xa, ya, GD_RIGHT); \
                        xb = obstacle_ground_position(gb, xb, yb, GD_RIGHT); \
                        break; \
                } \
                x = xb - xa; y = yb - ya; \
                if(x != 0 || y != 0) { \
                    ang = SLOPE(y, x); \
                    if(ga == gb || delta_angle(ang, pa->angle) <= 0x25) { \
                        pa->angle = ang; \
                        pa->angle_sensor[0] = v2d_new(xa, ya); \
                        pa->angle_sensor[1] = v2d_new(xb, yb); \
                        out_dx = x; out_dy = y; \
                    } \
                } \
            } \
        } \
    } while(0)

/* auxiliary macro to force the angle to a value */
#define FORCE_ANGLE(new_angle) \
    do { \
        pa->angle = (new_angle); \
        UPDATE_MOVMODE(); \
        UPDATE_SENSORS(); \
    } while(0)

/* auxiliary macro to compute the angle automatically */
#define SET_AUTO_ANGLE() \
    do { \
        UPDATE_ANGLE(); \
        UPDATE_MOVMODE(); \
        UPDATE_SENSORS(); \
    } while(0)

/* physics simulation */
void run_simulation(physicsactor_t *pa, const obstaclemap_t *obstaclemap, float dt)
{
    const obstacle_t *at_A, *at_B, *at_C, *at_D, *at_M, *at_N;

    /* initialize and save previous midair state */
    UPDATE_SENSORS();
    pa->was_midair = pa->midair;

    /*
     *
     * death
     *
     */

    if(pa->state == PAS_DEAD || pa->state == PAS_DROWNED) {
        pa->ysp = min(pa->ysp + pa->grv * dt, pa->topyspeed);
        pa->position.y += pa->ysp * dt;
        pa->facing_right = true;
        return;
    }

    /*
     *
     * getting hit
     *
     */

    if(pa->state == PAS_GETTINGHIT) {
        input_reset(pa->input);
        if(!nearly_zero(pa->xsp))
            pa->facing_right = (pa->xsp < 0.0f);
    }

    /*
     *
     * waiting
     *
     */

    if(pa->state == PAS_STOPPED) {
        pa->wait_timer += dt;
        if(pa->wait_timer >= pa->waittime)
            pa->state = PAS_WAITING;
    }
    else
        pa->wait_timer = 0.0f;

    /*
     *
     * winning
     *
     */

    if(pa->winning_pose) {
        /* brake on level clear */
        const float threshold = 60.0f;
        input_reset(pa->input);

        pa->gsp = clip(pa->gsp, -0.67f * pa->capspeed, 0.67f * pa->capspeed);
        if(pa->state == PAS_ROLLING)
            pa->state = PAS_BRAKING;

        if(pa->gsp > threshold)
            input_simulate_button_down(pa->input, IB_LEFT);
        else if(pa->gsp < -threshold)
            input_simulate_button_down(pa->input, IB_RIGHT);
        else
            input_disable(pa->input);

        /* enter the winning state */
        if(!pa->midair) {
            if(fabs(pa->gsp) < pa->walkthreshold)
                pa->state = PAS_WINNING;
        }
    }

    /*
     *
     * horizontal control lock
     *
     */
    if(pa->hlock_timer > 0.0f) {
        pa->hlock_timer -= dt;
        if(pa->hlock_timer < 0.0f)
            pa->hlock_timer = 0.0f;

        input_simulate_button_up(pa->input, IB_LEFT);
        input_simulate_button_up(pa->input, IB_RIGHT);

        if(!pa->midair && !nearly_zero(pa->gsp))
            pa->facing_right = (pa->gsp > 0.0f);
        else if(pa->midair && !nearly_zero(pa->xsp))
            pa->facing_right = (pa->xsp > 0.0f);
    }

    /*
     *
     * facing left or right
     *
     */

    if(pa->state != PAS_ROLLING && (!nearly_zero(pa->gsp) || !nearly_zero(pa->xsp))) {
        if((pa->gsp > 0.0f || pa->midair) && input_button_down(pa->input, IB_RIGHT))
            pa->facing_right = true;
        else if((pa->gsp < 0.0f || pa->midair) && input_button_down(pa->input, IB_LEFT))
            pa->facing_right = false;
    }

    /*
     *
     * walking & running
     *
     */

    if(!pa->midair && pa->state != PAS_ROLLING && pa->state != PAS_CHARGING) {

        /* slope factor */
        if(fabs(pa->gsp) >= pa->walkthreshold || fabs(SIN(pa->angle)) >= 0.707f)
            pa->gsp += pa->slp * -SIN(pa->angle) * dt;

        /* acceleration */
        if(input_button_down(pa->input, IB_RIGHT) && !input_button_down(pa->input, IB_LEFT) && pa->gsp >= 0.0f) {
            if(pa->gsp < pa->topspeed) {
                pa->gsp += pa->acc * dt;
                if(pa->gsp >= pa->topspeed) {
                    pa->gsp = pa->topspeed;
                    pa->state = PAS_RUNNING;
                }
                else if(!(pa->state == PAS_PUSHING && pa->facing_right))
                    pa->state = PAS_WALKING;
            }
        }
        else if(input_button_down(pa->input, IB_LEFT) && !input_button_down(pa->input, IB_RIGHT) && pa->gsp <= 0.0f) {
            if(pa->gsp > -pa->topspeed) {
                pa->gsp -= pa->acc * dt;
                if(pa->gsp <= -pa->topspeed) {
                    pa->gsp = -pa->topspeed;
                    pa->state = PAS_RUNNING;
                }
                else if(!(pa->state == PAS_PUSHING && !pa->facing_right))
                    pa->state = PAS_WALKING;
            }
        }

        /* deceleration */
        if(input_button_down(pa->input, IB_RIGHT) && pa->gsp < 0.0f) {
            pa->gsp += pa->dec * dt;
            if(pa->gsp >= 0.0f) {
                pa->gsp = 0.0f;
                pa->state = PAS_STOPPED;
            }
            else if(fabs(pa->gsp) >= pa->brakingthreshold) {
                if(pa->movmode == MM_FLOOR)
                    pa->state = PAS_BRAKING;
            }
        }
        else if(input_button_down(pa->input, IB_LEFT) && pa->gsp > 0.0f) {
            pa->gsp -= pa->dec * dt;
            if(pa->gsp <= 0.0f) {
                pa->gsp = 0.0f;
                pa->state = PAS_STOPPED;
            }
            else if(fabs(pa->gsp) >= pa->brakingthreshold) {
                if(pa->movmode == MM_FLOOR)
                    pa->state = PAS_BRAKING;
            }
        }

        if(pa->state == PAS_BRAKING) {
            /* braking */
            float brk = pa->frc * (1.5f + 3.0f * fabs(SIN(pa->angle)));
            if(fabs(pa->gsp) <= brk * dt) {
                pa->gsp = 0.0f;
                pa->state = PAS_STOPPED;
            }
            else
                pa->gsp -= brk * sign(pa->gsp) * dt;
        }
        else {
            /* friction */
            if(!input_button_down(pa->input, IB_LEFT) && !input_button_down(pa->input, IB_RIGHT)) {
                if(fabs(pa->gsp) <= pa->frc * dt) {
                    pa->gsp = 0.0f;
                    pa->state = PAS_STOPPED;
                }
                else
                    pa->gsp -= pa->frc * sign(pa->gsp) * dt;
            }
        }

        /* animation issues */
        if(fabs(pa->gsp) < pa->walkthreshold) {
            if(pa->state == PAS_PUSHING && !input_button_down(pa->input, IB_LEFT) && !input_button_down(pa->input, IB_RIGHT))
                pa->state = PAS_STOPPED;
            else if(pa->state == PAS_PUSHING || pa->state == PAS_LOOKINGUP || pa->state == PAS_DUCKING)
                ;
            else if(input_button_down(pa->input, IB_LEFT) || input_button_down(pa->input, IB_RIGHT))
                pa->state = input_button_down(pa->input, IB_LEFT) && input_button_down(pa->input, IB_RIGHT) ? PAS_STOPPED : PAS_WALKING;
            else if(pa->state != PAS_WAITING)
                pa->state = PAS_STOPPED;
            else if((pa->state == PAS_STOPPED || pa->state == PAS_WAITING) && !nearly_zero(pa->gsp))
                pa->state = PAS_WALKING;
        }
        else {
            if(pa->state == PAS_STOPPED || pa->state == PAS_WAITING || pa->state == PAS_LEDGE || pa->state == PAS_WALKING || pa->state == PAS_RUNNING || pa->state == PAS_DUCKING || pa->state == PAS_LOOKINGUP)
                pa->state = WALKING_OR_RUNNING(pa);
            else if(pa->state == PAS_PUSHING)
                pa->state = PAS_WALKING;
        }
    }

    /*
     *
     * looking up & crouching down
     *
     */

    if(!pa->midair) {
        if(pa->state != PAS_PUSHING && pa->state != PAS_ROLLING && pa->state != PAS_CHARGING) {
            if(nearly_zero(pa->gsp)) {
                if(input_button_down(pa->input, IB_DOWN))
                    pa->state = PAS_DUCKING;
                else if(input_button_down(pa->input, IB_UP))
                    pa->state = PAS_LOOKINGUP;
            }
        }
    }

    /*
     *
     * springing
     *
     */

    if(pa->state == PAS_SPRINGING) {
        if(pa->midair && pa->ysp > 0.0f)
            pa->state = PAS_WALKING;
    }

    /*
     *
     * breathing
     *
     */

    if(pa->breathe_timer > 0.0f) {
        pa->breathe_timer -= dt;
        pa->state = PAS_BREATHING;
    }
    else if(pa->state == PAS_BREATHING && pa->midair) {
        pa->breathe_timer = 0.0f;
        pa->state = PAS_WALKING;
    }

    /*
     *
     * balancing on ledges
     *
     */

    if(
        !pa->midair && pa->movmode == MM_FLOOR && nearly_zero(pa->gsp) &&
        !(pa->state == PAS_LEDGE || pa->state == PAS_PUSHING)
    ) {
        const sensor_t* s = at_A ? sensor_A(pa) : sensor_B(pa);
        int x = (int)pa->position.x;
        int y = (int)pa->position.y + sensor_get_y2(s) + 8;
        if(at_A != NULL && at_B == NULL && obstaclemap_get_best_obstacle_at(obstaclemap, x, y, x, y, pa->movmode, pa->layer) == NULL) {
            pa->state = PAS_LEDGE;
            pa->facing_right = true;
        }
        else if(at_A == NULL && at_B != NULL && obstaclemap_get_best_obstacle_at(obstaclemap, x, y, x, y, pa->movmode, pa->layer) == NULL) {
            pa->state = PAS_LEDGE;
            pa->facing_right = false;
        }
    }

    /*
     *
     * rolling
     *
     */

    /* start rolling */
    if(!pa->midair && (pa->state == PAS_WALKING || pa->state == PAS_RUNNING)) {
        if(fabs(pa->gsp) >= pa->rollthreshold && input_button_down(pa->input, IB_DOWN))
            pa->state = PAS_ROLLING;
    }

    /* roll */
    if(!pa->midair && pa->state == PAS_ROLLING) {

        /* slope factor */
        if(pa->gsp * SIN(pa->angle) >= 0.0f)
            pa->gsp += pa->rolluphillslp * -SIN(pa->angle) * dt;
        else
            pa->gsp += pa->rolldownhillslp * -SIN(pa->angle) * dt;

        /* deceleration */
        if(input_button_down(pa->input, IB_RIGHT) && pa->gsp < 0.0f)
            pa->gsp = min(0.0f, pa->gsp + pa->rolldec * dt);
        else if(input_button_down(pa->input, IB_LEFT) && pa->gsp > 0.0f)
            pa->gsp = max(0.0f, pa->gsp - pa->rolldec * dt);

        /* friction */
        if(fabs(pa->gsp) > pa->rollfrc * dt)
            pa->gsp -= pa->rollfrc * sign(pa->gsp) * dt;
        else
            pa->gsp = 0.0f;

        /* unroll */
        if(fabs(pa->gsp) < pa->unrollthreshold)
            pa->state = PAS_STOPPED; /*PAS_WALKING;*/ /* anim transition: rolling -> stopped */

        /* facing right? */
        if(!nearly_zero(pa->gsp))
            pa->facing_right = (pa->gsp > 0.0f);
    }

    /*
     *
     * charge and release
     *
     */

    /* begin to charge */
    if(pa->state == PAS_DUCKING) {
        if(input_button_down(pa->input, IB_DOWN) && input_button_pressed(pa->input, IB_FIRE1)) {
            if(!nearly_zero(pa->chrg)) /* check if the player has the ability to charge */
                pa->state = PAS_CHARGING;
        }
    }

    /* charging... */
    if(pa->state == PAS_CHARGING) {
        /* charging more...! */
        if(input_button_pressed(pa->input, IB_FIRE1))
            pa->charge_intensity = min(1.0f, pa->charge_intensity + 0.25f);
        else if(fabs(pa->charge_intensity) >= pa->chrgthreshold)
            pa->charge_intensity *= 0.999506551f - 1.84539309f * dt; /* attenuate charge intensity */

        /* release */
        if(!input_button_down(pa->input, IB_DOWN)) {
            float s = pa->facing_right ? 1.0f : -1.0f;
            pa->gsp = (s * pa->chrg) * (0.67f + pa->charge_intensity * 0.33f);
            pa->state = PAS_ROLLING;
            pa->charge_intensity = 0.0f;
            pa->jump_lock_timer = 0.09375f;
        }
        else
            pa->gsp = 0.0f;
    }

    /*
     *
     * ground speed
     *
     */

    if(!pa->midair) {

        /* you're way too fast... */
        pa->gsp = clip(pa->gsp, -pa->capspeed, pa->capspeed);

        /* speed */
        pa->xsp = pa->gsp * COS(pa->angle);
        pa->ysp = pa->gsp * -SIN(pa->angle);

    }

    /*
     *
     * falling off
     *
     */

    if(pa->midair) {

        /* air acceleration */
        if(input_button_down(pa->input, IB_RIGHT) && !input_button_down(pa->input, IB_LEFT)) {
            if(pa->xsp < pa->topspeed)
                pa->xsp = min(pa->xsp + pa->air * dt, pa->topspeed);
        }

        if(input_button_down(pa->input, IB_LEFT) && !input_button_down(pa->input, IB_RIGHT)) {
            if(pa->xsp > -pa->topspeed)
                pa->xsp = max(pa->xsp - pa->air * dt, -pa->topspeed);
        }

        /* air drag */
        if(pa->state != PAS_GETTINGHIT) {
            if(pa->ysp < 0.0f && pa->ysp > pa->airdragthreshold) {
                if(fabs(pa->xsp) >= pa->airdragxthreshold) {
                    /*pa->xsp *= powf(pa->airdrag, 60.0f * dt);*/
                    pa->xsp *= pa->airdrag_coefficient[0] * dt + pa->airdrag_coefficient[1];
                }
            }
        }

        /* gravity */
        float grv = (pa->state != PAS_GETTINGHIT) ? pa->grv : (pa->grv / 7.0f) * 6.0f;
        pa->ysp += grv * dt;
        if(pa->ysp > pa->topyspeed)
            pa->ysp = pa->topyspeed;
    }

    /*
     *
     * jumping
     *
     */

    if(!pa->midair) {

        pa->jump_lock_timer -= dt;
        if(pa->jump_lock_timer <= 0.0f) {
            pa->jump_lock_timer = 0.0f;

            /* jump */
            if(
                input_button_pressed(pa->input, IB_FIRE1) && (
                    (!input_button_down(pa->input, IB_UP) && !input_button_down(pa->input, IB_DOWN)) ||
                    pa->state == PAS_ROLLING
                ) &&
                !pa->touching_ceiling /* don't bother jumping if near a ceiling */
            ) {
#if WANT_JUMP_ATTENUATION
                /* reduce the jump height when moving uphill */
                float grv_attenuation = (pa->gsp * SIN(pa->angle) < 0.0f) ? 1.0f : 0.5f;
                pa->xsp = pa->jmp * SIN(pa->angle) + pa->gsp * COS(pa->angle);
                pa->ysp = pa->jmp * COS(pa->angle) - pa->gsp * SIN(pa->angle) * grv_attenuation;
#else
                pa->xsp = pa->jmp * SIN(pa->angle) + pa->gsp * COS(pa->angle);
                pa->ysp = pa->jmp * COS(pa->angle) - pa->gsp * SIN(pa->angle);
#endif
                pa->gsp = 0.0f;
                pa->state = PAS_JUMPING;
                FORCE_ANGLE(0x0);
            }
        }
    }
    else {

        /* jump sensitivity */
        if(pa->state == PAS_JUMPING) {
            if(!input_button_down(pa->input, IB_FIRE1) && pa->ysp < pa->jmprel)
                pa->ysp = pa->jmprel;
        }

    }

    /*
     *
     * updating the position
     *
     */

#if 0
    /* move using a faster method */
    pa->position.x += pa->xsp * dt;
    pa->position.y += pa->ysp * dt;
    UPDATE_SENSORS();
#else
    /* move by small increments for a
       more robust collision detection */
    do {
        v2d_t vel = v2d_new(pa->xsp, pa->ysp);
        v2d_t ds = v2d_multiply(vel, dt);
        int sx = (ds.x > 0.0f) - (ds.x < 0.0f); /* -1, 0 or 1 */
        int sy = (ds.y > 0.0f) - (ds.y < 0.0f);
        float ds_length = v2d_magnitude(ds); /* typically <= 16 (~ capspeed per frame) */

        const float max_increment = 2.0f; /* in pixels; the less the number, the most robust the collision detection, but the more iterations it takes */
        float max_ds_length = max(pa->capspeed, pa->topyspeed) / TARGET_FPS; /* per frame */
        int max_increments = (int)ceilf(max_ds_length / max_increment); /* typically 8 (when max_ds_length = 16 and max_increment = 2) */
        int num_increments = min(max_increments, (int)ceilf(ds_length));
        v2d_t increment = num_increments > 0 ? v2d_multiply(ds, 1.0f / (float)num_increments) : v2d_new(0, 0);

        #if 0
        video_showmessage("inc: %.5f,%.5f (x %d)",increment.x, increment.y, num_increments);
        #endif

        while(num_increments-- > 0) {
            /* translate by one increment */
            pa->position.x += increment.x;
            pa->position.y += increment.y;
            UPDATE_SENSORS();

            /* check collisions */
            switch(pa->movmode) {
                case MM_FLOOR:
                    if(at_M && sx < 0)
                        increment.x = 0.0f;
                    if(at_N && sx > 0)
                        increment.x = 0.0f;
                    /*
                    // sticky physics
                    if(at_A || at_B) {
                        if(sy > 0)
                            increment.y = 0.0f;
                    }*/
                    if(at_C || at_D) {
                        if(sy < 0)
                            increment.y = 0.0f;
                    }
                    break;

                case MM_RIGHTWALL:
                    if(at_M && sy > 0)
                        increment.y = 0.0f;
                    if(at_N && sy < 0)
                        increment.y = 0.0f;
                    /*if(at_A || at_B) {
                        if(sx > 0)
                            increment.x = 0.0f;
                    }*/
                    if(at_C || at_D) {
                        if(sx < 0)
                            increment.x = 0.0f;
                    }
                    break;

                case MM_CEILING:
                    if(at_M && sx > 0)
                        increment.x = 0.0f;
                    if(at_N && sx < 0)
                        increment.x = 0.0f;
                    /*if(at_A || at_B) {
                        if(sy < 0)
                            increment.y = 0.0f;
                    }*/
                    if(at_C || at_D) {
                        if(sy > 0)
                            increment.y = 0.0f;
                    }
                    break;

                case MM_LEFTWALL:
                    if(at_M && sy < 0)
                        increment.y = 0.0f;
                    if(at_N && sy > 0)
                        increment.y = 0.0f;
                    /*if(at_A || at_B) {
                        if(sx < 0)
                            increment.x = 0.0f;
                    }*/
                    if(at_C || at_D) {
                        if(sx > 0)
                            increment.x = 0.0f;
                    }
                    break;
            }

            /* exit early */
            if(increment.x == 0.0f && increment.y == 0.0f)
                break;
        }

    } while(0);
#endif

    /* stop if we land after getting hit */
    if(!pa->midair && pa->was_midair) {
        if(pa->state == PAS_GETTINGHIT) {
            pa->gsp = pa->xsp = 0.0f;
            pa->state = PAS_STOPPED;
        }
    }

    /*
     *
     * wall collisions
     *
     */

    /* right wall */
    if(at_N != NULL) {
        const sensor_t* sensor = sensor_N(pa);
        v2d_t position = v2d_new(floorf(pa->position.x), floorf(pa->position.y));
        point2d_t tail = sensor_tail(sensor, position, pa->movmode);
        point2d_t local_tail = point2d_subtract(tail, point2d_from_v2d(position));
        bool reset_angle = false;
        int wall;

        /* reset gsp */
        if(pa->gsp > 0.0f)
            pa->gsp = 0.0f;

        /* reposition the player */
        switch(pa->movmode) {
            case MM_FLOOR:
                wall = obstacle_ground_position(at_N, tail.x, tail.y, GD_RIGHT);
                pa->position.x = wall - local_tail.x - 1;
                pa->xsp = min(pa->xsp, 0.0f);
                reset_angle = false;
                break;

            case MM_CEILING:
                wall = obstacle_ground_position(at_N, tail.x, tail.y, GD_LEFT);
                pa->position.x = wall - local_tail.x + 1;
                pa->xsp = max(pa->xsp, 0.0f);
                reset_angle = true;
                break;

            case MM_RIGHTWALL:
                wall = obstacle_ground_position(at_N, tail.x, tail.y, GD_UP);
                pa->position.y = wall - local_tail.y - 1;
                pa->ysp = max(pa->ysp, 0.0f);
                reset_angle = true;
                break;

            case MM_LEFTWALL:
                wall = obstacle_ground_position(at_N, tail.x, tail.y, GD_DOWN);
                pa->position.y = wall - local_tail.y + 1;
                pa->ysp = min(pa->ysp, 0.0f);
                reset_angle = true;
                break;
        }

        /* update sensors */
        if(!reset_angle) {
            UPDATE_SENSORS();
        }
        else {
            FORCE_ANGLE(0x0);
        }

        /* pushing a wall */
        if(!pa->midair && pa->movmode == MM_FLOOR && pa->state != PAS_ROLLING) {
            if(input_button_down(pa->input, IB_RIGHT)) {
                pa->state = PAS_PUSHING;
                pa->facing_right = true;
            }
            else
                pa->state = PAS_STOPPED;
        }
    }

    /* left wall */
    if(at_M != NULL) {
        const sensor_t* sensor = sensor_M(pa);
        v2d_t position = v2d_new(floorf(pa->position.x), floorf(pa->position.y));
        point2d_t head = sensor_head(sensor, position, pa->movmode);
        point2d_t local_head = point2d_subtract(head, point2d_from_v2d(position));
        bool reset_angle = false;
        int wall;

        /* reset gsp */
        if(pa->gsp < 0.0f)
            pa->gsp = 0.0f;

        /* reposition the player */
        switch(pa->movmode) {
            case MM_FLOOR:
                wall = obstacle_ground_position(at_M, head.x, head.y, GD_LEFT);
                pa->position.x = wall - local_head.x + 1;
                pa->xsp = max(pa->xsp, 0.0f);
                reset_angle = false;
                break;

            case MM_CEILING:
                wall = obstacle_ground_position(at_M, head.x, head.y, GD_RIGHT);
                pa->position.x = wall - local_head.x - 1;
                pa->xsp = min(pa->xsp, 0.0f);
                reset_angle = true;
                break;

            case MM_RIGHTWALL:
                wall = obstacle_ground_position(at_M, head.x, head.y, GD_DOWN);
                pa->position.y = wall - local_head.y - 1;
                pa->ysp = min(pa->ysp, 0.0f);
                reset_angle = true;
                break;

            case MM_LEFTWALL:
                wall = obstacle_ground_position(at_M, head.x, head.y, GD_UP);
                pa->position.y = wall - local_head.y + 1;
                pa->ysp = max(pa->ysp, 0.0f);
                reset_angle = true;
                break;
        }

        /* update sensors */
        if(!reset_angle) {
            UPDATE_SENSORS();
        }
        else {
            FORCE_ANGLE(0x0);
        }

        /* pushing a wall */
        if(!pa->midair && pa->movmode == MM_FLOOR && pa->state != PAS_ROLLING) {
            if(input_button_down(pa->input, IB_LEFT)) {
                pa->state = PAS_PUSHING;
                pa->facing_right = false;
            }
            else
                pa->state = PAS_STOPPED;
        }
    }

    /*
     *
     * ceiling collision
     *
     */

    if(pa->midair && pa->touching_ceiling) {
        const obstacle_t *ceiling = NULL;
        const sensor_t *ceiling_sensor = NULL;
        bool must_reattach = false;

        /* picking the ceiling */
        if(pick_the_best_ceiling(pa, at_C, at_D, sensor_C(pa), sensor_D(pa)) == 'c') {
            ceiling = at_C;
            ceiling_sensor = sensor_C(pa);
        }
        else {
            ceiling = at_D;
            ceiling_sensor = sensor_D(pa);
        }

        /* are we touching the ceiling for the first time? */
        if(pa->ysp < 0.0f) {
            /* compute the angle */
            FORCE_ANGLE(0x80);
            SET_AUTO_ANGLE();

            /* reattach to the ceiling */
            if((pa->angle >= 0xA0 && pa->angle <= 0xBF) || (pa->angle >= 0x40 && pa->angle <= 0x5F)) {
                must_reattach = !pa->midair;
                if(must_reattach) {
                    pa->gsp = (fabs(pa->xsp) > -pa->ysp) ? -pa->xsp : pa->ysp * -sign(SIN(pa->angle));
                    pa->xsp = pa->ysp = 0.0f;
                    if(pa->state != PAS_ROLLING)
                        pa->state = WALKING_OR_RUNNING(pa);
                }
            }
        }

        /* we won't reattach to the ceiling */
        if(!must_reattach) {
            /* adjust speed & angle */
            pa->ysp = max(pa->ysp, 0.0f);
            FORCE_ANGLE(0x0);

            /* find the position of the sensor
               after setting the angle to 0 */
            v2d_t position = v2d_new(floorf(pa->position.x), floorf(pa->position.y));
            point2d_t head = sensor_head(ceiling_sensor, position, pa->movmode);
            point2d_t local_head = point2d_subtract(head, point2d_from_v2d(position));

            /* reposition the player */
            int ceiling_position = obstacle_ground_position(ceiling, head.x, head.y, GD_UP);
            pa->position.y = ceiling_position - local_head.y + 1;
            UPDATE_SENSORS();
        }
    }

    /*
     *
     * sticky physics
     * FIXME
     *
     */

    /* sticky physics */
    if(pa->midair && (
        (!pa->was_midair && pa->state != PAS_JUMPING && pa->state != PAS_GETTINGHIT && pa->state != PAS_SPRINGING && pa->state != PAS_DROWNED && pa->state != PAS_DEAD) ||
        (pa->state == PAS_ROLLING && !pa->sticky_lock)
    )){
        v2d_t offset = v2d_new(0,0);
        int u = 4; /* FIXME: try to use a fraction of the sensor height as well */

        /* mystery */
        if(fabs(pa->xsp) > pa->topspeed || pa->state == PAS_ROLLING) {
            int x, y, h = 12; /* shouldn't be higher */
            const sensor_t* s = (pa->xsp > 0) ? sensor_B(pa) : sensor_A(pa);
            sensor_worldpos(s, pa->position, pa->movmode, NULL, NULL, &x, &y);
            /*
            // FIXME: rolling in a U
            if(pa->state == PAS_ROLLING) // rolling hack
                u *= 2;
            */
            for(; u < h; u++) {
                if(pa->movmode == MM_FLOOR) {
                    if(obstaclemap_obstacle_exists(obstaclemap, x, y + u, pa->layer))
                        break;
                }
                else if(pa->movmode == MM_RIGHTWALL) {
                    if(obstaclemap_obstacle_exists(obstaclemap, y + u, x, pa->layer))
                        break;
                }
                else if(pa->movmode == MM_CEILING) {
                    if(obstaclemap_obstacle_exists(obstaclemap, x, y - u, pa->layer))
                        break;
                }
                else if(pa->movmode == MM_LEFTWALL) {
                    if(obstaclemap_obstacle_exists(obstaclemap, y - u, x, pa->layer))
                        break;
                }
            }
        }

        /* computing the test offset */
        if(pa->movmode == MM_FLOOR)
            offset = v2d_new(0,u);
        else if(pa->movmode == MM_CEILING)
            offset = v2d_new(0,-u);
        else if(pa->movmode == MM_RIGHTWALL)
            offset = v2d_new(u,0);
        else if(pa->movmode == MM_LEFTWALL)
            offset = v2d_new(-u,0);

        /* offset the character */
        pa->position = v2d_add(pa->position, offset);
        pa->midair = false; /* cloud bugfix for UPDATE_SENSORS */
        SET_AUTO_ANGLE();

        /* if the player is still in the air,
           undo the offset */
        if(pa->midair) {
            pa->position = v2d_subtract(pa->position, offset);
            SET_AUTO_ANGLE();

            /* sticky physics hack */
            if(pa->state == PAS_ROLLING)
                pa->sticky_lock = true;
        }
    }
    else if(!pa->midair && pa->state == PAS_ROLLING) {
        /* undo sticky physics hack */
        pa->sticky_lock = false;
    }

    /* stick to the ground */
    if(!pa->midair && !((pa->state == PAS_JUMPING || pa->state == PAS_GETTINGHIT || pa->state == PAS_SPRINGING || pa->state == PAS_DROWNED || pa->state == PAS_DEAD) && pa->ysp < 0.0f)) {
        const obstacle_t *ground = NULL;
        const sensor_t *ground_sensor = NULL;
        int offset = 0;

        /* picking the ground */
        if(pick_the_best_ground(pa, at_A, at_B, sensor_A(pa), sensor_B(pa)) == 'a') {
            ground = at_A;
            ground_sensor = sensor_A(pa);
        }
        else {
            ground = at_B;
            ground_sensor = sensor_B(pa);
        }

        /* computing the offset (note: if !pa->midair, then ground_sensor != NULL) */
        offset = sensor_get_y2(ground_sensor) - 1; /* need -1 */

        /* adjust position */
        if(pa->movmode == MM_LEFTWALL)
            pa->position.x = obstacle_ground_position(ground, (int)pa->position.x - sensor_get_y2(ground_sensor), (int)pa->position.y + sensor_get_x2(ground_sensor), GD_LEFT) + offset;
        else if(pa->movmode == MM_CEILING)
            pa->position.y = obstacle_ground_position(ground, (int)pa->position.x - sensor_get_x2(ground_sensor), (int)pa->position.y - sensor_get_y2(ground_sensor), GD_UP) + offset;
        else if(pa->movmode == MM_RIGHTWALL)
            pa->position.x = obstacle_ground_position(ground, (int)pa->position.x + sensor_get_y2(ground_sensor), (int)pa->position.y - sensor_get_x2(ground_sensor), GD_RIGHT) - offset;
        else if(pa->movmode == MM_FLOOR)
            pa->position.y = obstacle_ground_position(ground, (int)pa->position.x + sensor_get_x2(ground_sensor), (int)pa->position.y + sensor_get_y2(ground_sensor), GD_DOWN) - offset;

        /* additional adjustments when first touching the ground */
        if(pa->was_midair) {
            if(pa->movmode == MM_FLOOR) {
                /* fix the speed (reacquisition of the ground comes next) */
                pa->gsp = pa->xsp;

                /* unroll after rolling midair */
                if(pa->state == PAS_ROLLING) {
                    if(pa->midair_timer >= 0.2f && !input_button_down(pa->input, IB_DOWN)) {
                        pa->state = WALKING_OR_RUNNING(pa);
                        if(!nearly_zero(pa->gsp))
                            pa->facing_right = (pa->gsp > 0.0f);
                    }
                }
                else {
                    /* animation fix (e.g., when jumping near edges) */
                    pa->state = WALKING_OR_RUNNING(pa);
                }
            }
        }

        /* update the angle */
        SET_AUTO_ANGLE();
    }

    /*
     *
     * reacquisition of the ground
     *
     */

    if(!pa->midair && pa->was_midair) {
        if(pa->angle >= 0xF0 || pa->angle <= 0x0F)
            pa->gsp = pa->xsp;
        else if((pa->angle >= 0xE0 && pa->angle <= 0xEF) || (pa->angle >= 0x10 && pa->angle <= 0x1F))
            pa->gsp = fabs(pa->xsp) > pa->ysp ? pa->xsp : pa->ysp * 0.5f * -sign(SIN(pa->angle));
        else if((pa->angle >= 0xC0 && pa->angle <= 0xDF) || (pa->angle >= 0x20 && pa->angle <= 0x3F))
            pa->gsp = fabs(pa->xsp) > pa->ysp ? pa->xsp : pa->ysp * -sign(SIN(pa->angle));

        pa->xsp = pa->ysp = 0.0f;
        if(pa->state != PAS_ROLLING)
            pa->state = WALKING_OR_RUNNING(pa);
    }

    /*
     *
     * falling off walls and ceilings
     *
     */

    if(!pa->midair) {
        if(pa->movmode != MM_FLOOR && pa->hlock_timer == 0.0f) {
            if(fabs(pa->gsp) < pa->falloffthreshold) {
                pa->hlock_timer = 0.5f;
                if(pa->angle >= 0x40 && pa->angle <= 0xC0) {
                    pa->gsp = 0.0f;
                    FORCE_ANGLE(0x0);
                }
            }
        }
    }

    /*
     *
     * misc
     *
     */

    /* reset the angle & update the midair_timer */
    if(pa->midair) {
        pa->midair_timer += dt;
        FORCE_ANGLE(0x0);

        /* reset gsp, otherwise we may restore it when landing on the ground */
        if(pa->ysp < 0.0f)
            pa->gsp = 0.0f;
    }
    else
        pa->midair_timer = 0.0f;

    /* fix invalid states */
    if(pa->midair) {
        if(pa->state == PAS_PUSHING || pa->state == PAS_STOPPED || pa->state == PAS_WAITING || pa->state == PAS_DUCKING || pa->state == PAS_LOOKINGUP)
            pa->state = WALKING_OR_RUNNING(pa);
    }
    else {
        if(pa->state == PAS_WALKING && nearly_zero(pa->gsp))
            pa->state = PAS_STOPPED;
    }
}

/* call update_sensors() whenever you update pa->position or pa->angle */
void update_sensors(physicsactor_t* pa, const obstaclemap_t* obstaclemap, obstacle_t const** const at_A, obstacle_t const** const at_B, obstacle_t const** const at_C, obstacle_t const** const at_D, obstacle_t const** const at_M, obstacle_t const** const at_N)
{
    /* get sensors */
    sensor_t* a = sensor_A(pa);
    sensor_t* b = sensor_B(pa);
    sensor_t* c = sensor_C(pa);
    sensor_t* d = sensor_D(pa);
    sensor_t* m = sensor_M(pa);
    sensor_t* n = sensor_N(pa);

    /* disable sensors for efficiency */
    if(!pa->midair) {
        sensor_set_enabled(a, true);
        sensor_set_enabled(b, true);
        sensor_set_enabled(c, false);
        sensor_set_enabled(d, false);
        sensor_set_enabled(m, pa->gsp < 0.0f);
        sensor_set_enabled(n, pa->gsp > 0.0f);
    }
    else {
        sensor_set_enabled(a, pa->ysp >= 0.0f);
        sensor_set_enabled(b, pa->ysp >= 0.0f);
        sensor_set_enabled(c, pa->ysp < 0.0f);
        sensor_set_enabled(d, pa->ysp < 0.0f);
        sensor_set_enabled(m, pa->xsp < 0.0f);
        sensor_set_enabled(n, pa->xsp > 0.0f);
    }

    /* read sensors */
    *at_A = sensor_check(a, pa->position, pa->movmode, pa->layer, obstaclemap);
    *at_B = sensor_check(b, pa->position, pa->movmode, pa->layer, obstaclemap);
    *at_C = sensor_check(c, pa->position, pa->movmode, pa->layer, obstaclemap);
    *at_D = sensor_check(d, pa->position, pa->movmode, pa->layer, obstaclemap);
    *at_M = sensor_check(m, pa->position, pa->movmode, pa->layer, obstaclemap);
    *at_N = sensor_check(n, pa->position, pa->movmode, pa->layer, obstaclemap);

    /* C, D, M, N: ignore clouds */
    *at_C = (*at_C != NULL && obstacle_is_solid(*at_C)) ? *at_C : NULL;
    *at_D = (*at_D != NULL && obstacle_is_solid(*at_D)) ? *at_D : NULL;
    *at_M = (*at_M != NULL && obstacle_is_solid(*at_M)) ? *at_M : NULL;
    *at_N = (*at_N != NULL && obstacle_is_solid(*at_N)) ? *at_N : NULL;

    /* A, B: ignore clouds when moving upwards */
    if(pa->ysp < 0.0f) {
        if(-pa->ysp > fabsf(pa->xsp)) {
            *at_A = (*at_A != NULL && obstacle_is_solid(*at_A)) ? *at_A : NULL;
            *at_B = (*at_B != NULL && obstacle_is_solid(*at_B)) ? *at_B : NULL;
        }
    }

    /* A: ignore if it's a cloud and if the tail of the sensor is not touching it */
    if(*at_A != NULL && !obstacle_is_solid(*at_A)) {
        point2d_t tail = sensor_tail(a, pa->position, pa->movmode);
        if(!obstacle_point_collision(*at_A, tail))
            *at_A = NULL;
        else if(pa->midair && pa->movmode == MM_FLOOR && pa->angle == 0x0) {
            int ygnd = obstacle_ground_position(*at_A, tail.x, tail.y, GD_DOWN);
            *at_A = (tail.y < ygnd + CLOUD_OFFSET) ? *at_A : NULL;
        }
    }

    /* B: ignore if it's a cloud and if the tail of the sensor is not touching it */
    if(*at_B != NULL && !obstacle_is_solid(*at_B)) {
        point2d_t tail = sensor_tail(b, pa->position, pa->movmode);
        if(!obstacle_point_collision(*at_B, tail))
            *at_B = NULL;
        else if(pa->midair && pa->movmode == MM_FLOOR && pa->angle == 0x0) {
            int ygnd = obstacle_ground_position(*at_B, tail.x, tail.y, GD_DOWN);
            *at_B = (tail.y < ygnd + CLOUD_OFFSET) ? *at_B : NULL;
        }
    }

    /* A, B: special logic when both are clouds and A != B */
    if(*at_A != NULL && *at_B != NULL && *at_A != *at_B && !obstacle_is_solid(*at_A) && !obstacle_is_solid(*at_B)) {
        if(pa->movmode == MM_FLOOR) {
            point2d_t tail_a = sensor_tail(a, pa->position, pa->movmode);
            point2d_t tail_b = sensor_tail(b, pa->position, pa->movmode);
            int gnd_a = obstacle_ground_position(*at_A, tail_a.x, tail_a.y, GD_DOWN);
            int gnd_b = obstacle_ground_position(*at_B, tail_b.x, tail_b.y, GD_DOWN);
            if(abs(gnd_a - gnd_b) > 8) {
                if(gnd_a < gnd_b)
                    *at_A = NULL;
                else
                    *at_B = NULL;
            }
        }
    }

    /* set flags */
    pa->midair = (*at_A == NULL) && (*at_B == NULL);
    pa->touching_ceiling = (*at_C != NULL) || (*at_D != NULL);
}

/* call update_movmode() whenever you update pa->angle */
void update_movmode(physicsactor_t* pa)
{
    /* angles 0x20, 0x60, 0xA0, 0xE0
       do not change the movmode */
    if(pa->angle < 0x20 || pa->angle > 0xE0) {
        if(pa->movmode == MM_CEILING)
            pa->gsp = -pa->gsp;
        pa->movmode = MM_FLOOR;
    }
    else if(pa->angle > 0x20 && pa->angle < 0x60)
        pa->movmode = MM_LEFTWALL;
    else if(pa->angle > 0x60 && pa->angle < 0xA0)
        pa->movmode = MM_CEILING;
    else if(pa->angle > 0xA0 && pa->angle < 0xE0)
        pa->movmode = MM_RIGHTWALL;
}

/* which one is the tallest obstacle, a or b? */
char pick_the_best_ground(const physicsactor_t *pa, const obstacle_t *a, const obstacle_t *b, const sensor_t *a_sensor, const sensor_t *b_sensor)
{
    int xa, ya, xb, yb, ha, hb;

    if(a == NULL)
        return 'b';
    else if(b == NULL)
        return 'a';

    switch(pa->movmode) {
        case MM_FLOOR:
            xa = (int)pa->position.x + sensor_get_x2(a_sensor);
            ya = (int)pa->position.y + sensor_get_y2(a_sensor);
            xb = (int)pa->position.x + sensor_get_x2(b_sensor);
            yb = (int)pa->position.y + sensor_get_y2(b_sensor);
            ha = obstacle_ground_position(a, xa, ya, GD_DOWN);
            hb = obstacle_ground_position(b, xb, yb, GD_DOWN);
            return ha < hb ? 'a' : 'b';

        case MM_LEFTWALL:
            xa = (int)pa->position.x - sensor_get_y2(a_sensor);
            ya = (int)pa->position.y + sensor_get_x2(a_sensor);
            xb = (int)pa->position.x - sensor_get_y2(b_sensor);
            yb = (int)pa->position.y + sensor_get_x2(b_sensor);
            ha = obstacle_ground_position(a, xa, ya, GD_LEFT);
            hb = obstacle_ground_position(b, xb, yb, GD_LEFT);
            return ha >= hb ? 'a' : 'b';

        case MM_CEILING:
            xa = (int)pa->position.x - sensor_get_x2(a_sensor);
            ya = (int)pa->position.y - sensor_get_y2(a_sensor);
            xb = (int)pa->position.x - sensor_get_x2(b_sensor);
            yb = (int)pa->position.y - sensor_get_y2(b_sensor);
            ha = obstacle_ground_position(a, xa, ya, GD_UP);
            hb = obstacle_ground_position(b, xb, yb, GD_UP);
            return ha >= hb ? 'a' : 'b';

        case MM_RIGHTWALL:
            xa = (int)pa->position.x + sensor_get_y2(a_sensor);
            ya = (int)pa->position.y - sensor_get_x2(a_sensor);
            xb = (int)pa->position.x + sensor_get_y2(b_sensor);
            yb = (int)pa->position.y - sensor_get_x2(b_sensor);
            ha = obstacle_ground_position(a, xa, ya, GD_RIGHT);
            hb = obstacle_ground_position(b, xb, yb, GD_RIGHT);
            return ha < hb ? 'a' : 'b';
    }

    return 'a';
}

/* which one is the best ceiling, c or d? */
char pick_the_best_ceiling(const physicsactor_t *pa, const obstacle_t *c, const obstacle_t *d, const sensor_t *c_sensor, const sensor_t *d_sensor)
{
    int xc, yc, xd, yd, hc, hd;

    if(c == NULL)
        return 'd';
    else if(d == NULL)
        return 'c';

    switch(pa->movmode) {
        case MM_FLOOR:
            xc = (int)pa->position.x + sensor_get_x1(c_sensor);
            yc = (int)pa->position.y + sensor_get_y1(c_sensor);
            xd = (int)pa->position.x + sensor_get_x1(d_sensor);
            yd = (int)pa->position.y + sensor_get_y1(d_sensor);
            hc = obstacle_ground_position(c, xc, yc, GD_UP);
            hd = obstacle_ground_position(d, xd, yd, GD_UP);
            return hc >= hd ? 'c' : 'd';

        case MM_LEFTWALL:
            xc = (int)pa->position.x - sensor_get_y1(c_sensor);
            yc = (int)pa->position.y + sensor_get_x1(c_sensor);
            xd = (int)pa->position.x - sensor_get_y1(d_sensor);
            yd = (int)pa->position.y + sensor_get_x1(d_sensor);
            hc = obstacle_ground_position(c, xc, yc, GD_RIGHT);
            hd = obstacle_ground_position(d, xd, yd, GD_RIGHT);
            return hc < hd ? 'c' : 'd';

        case MM_CEILING:
            xc = (int)pa->position.x - sensor_get_x1(c_sensor);
            yc = (int)pa->position.y - sensor_get_y1(c_sensor);
            xd = (int)pa->position.x - sensor_get_x1(d_sensor);
            yd = (int)pa->position.y - sensor_get_y1(d_sensor);
            hc = obstacle_ground_position(c, xc, yc, GD_DOWN);
            hd = obstacle_ground_position(d, xd, yd, GD_DOWN);
            return hc < hd ? 'c' : 'd';

        case MM_RIGHTWALL:
            xc = (int)pa->position.x + sensor_get_y1(c_sensor);
            yc = (int)pa->position.y - sensor_get_x1(c_sensor);
            xd = (int)pa->position.x + sensor_get_y1(d_sensor);
            yd = (int)pa->position.y - sensor_get_x1(d_sensor);
            hc = obstacle_ground_position(c, xc, yc, GD_LEFT);
            hd = obstacle_ground_position(d, xd, yd, GD_LEFT);
            return hc >= hd ? 'c' : 'd';
    }

    return 'c';
}

/* renders an angle sensor */
void render_ball(v2d_t sensor_position, int radius, color_t color, v2d_t camera_position)
{
    v2d_t topleft = v2d_subtract(camera_position, v2d_multiply(video_get_screen_size(), 0.5f));
    v2d_t position = v2d_subtract(sensor_position, topleft);
    color_t border_color = color_rgb(0, 0, 0);

    image_ellipse(position.x, position.y, radius + 1, radius + 1, border_color);
    image_ellipse(position.x, position.y, radius, radius, color);
}

/* distance between the angle sensors */
int distance_between_angle_sensors(const physicsactor_t* pa)
{
    const sensor_t* sensor = pa->A_normal; /* not sensor_A(pa), because varying the size makes it inconsistent */
    return (1 - sensor_get_x1(sensor));
}

/* the min angle between alpha and beta */
int delta_angle(int alpha, int beta)
{
    alpha &= 0xFF; beta &= 0xFF;
    int diff = alpha > beta ? alpha - beta : beta - alpha;
    return diff > 0x80 ? 0xFF - diff + 1 : diff;
}