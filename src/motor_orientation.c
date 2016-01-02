#include <ch.h>
#include <hal.h>
#include <stm32f4xx.h>
#include <stdlib.h>
#include "debug.h"

static int g_motor_angle;
static int g_motor_rpm;
static int g_motor_rpm_adj_P;
static int g_motor_lockcount;
static int g_hall_time_since_change;
static int g_hall_prev_angle;

#define LOCK_THRESHOLD 5

// Table index is H3 | H2 | H1, value is sector 1-6.
// Typical rotation is:
// Hall     Sector
// 001=1    1
// 011=3    2
// 010=2    3
// 110=6    4
// 100=4    5
// 101=5    6
const int g_hall_table[8] = {-1, 1, 3, 2, 5, 6, 4, -1};

int get_hall_sector()
{
  uint32_t hall_state =
    (palReadPad(GPIOB, GPIOB_HALL_1) ? 1 : 0) |
    (palReadPad(GPIOB, GPIOB_HALL_2) ? 2 : 0) |
    (palReadPad(GPIOC, GPIOC_HALL_3) ? 4 : 0);
  
  return g_hall_table[hall_state];
}

static int angle_diff(int a, int b)
{
  int diff = a - b;
  if (diff > 180) diff -= 360;
  if (diff < -180) diff += 360;
  return diff;
}

void update_motor_orientation(int loop_freq)
{
  int sector = get_hall_sector();
  
  static int hall_error_count = 0;
  if (sector < 0)
  {
    hall_error_count++;
    if (hall_error_count > 100)
    {
      abort_with_error("HALL_ERROR");
    }
    return;
  }
  
  int hall_angle = 360 - sector * 60 + 30;
  
  if (abs(angle_diff(hall_angle, g_motor_angle)) > 60)
  {
    // Lost sync with the motor rotation
    g_motor_lockcount = 0;
    g_motor_angle = hall_angle;
    g_motor_rpm = 0;
  }
  
  if (hall_angle != g_hall_prev_angle)
  {
    // Moved from one sector to the next, current angle is
    // quite accurately in between the two.
    int angle_now = hall_angle - 30;
    
    if (g_motor_rpm <= 0)
    {
      // Setup the velocity based on time between sectors.
      // sectors_per_s = loop_freq / g_hall_time_since_change
      // rpm = sectors_per_s * 60 / 6
      g_motor_rpm = 10 * loop_freq / g_hall_time_since_change;
      g_motor_rpm_adj_P = 0;
      g_motor_angle = angle_now;
    }
    else
    {
      if (g_motor_lockcount < 100)
      {
        g_motor_lockcount++;
      }
      
      // Fine-tune the velocity estimate.
      int delta = angle_diff(angle_now, g_motor_angle);
      int rpm_delta = delta * loop_freq / (g_hall_time_since_change * 6);
      g_motor_rpm_adj_P = rpm_delta;
      g_motor_rpm += rpm_delta / 12;
    }
    
    g_hall_time_since_change = 0;
    g_hall_prev_angle = hall_angle;
  }
  else
  {
    g_hall_time_since_change++;
  }
  
  // Increase g_motor_angle based on g_motor_rpm.
  // Accumulator is used to take fractional values into account properly.
  static int accumulator = 0;
  accumulator += g_motor_rpm + g_motor_rpm_adj_P;
  
  const int acc_per_degree = loop_freq * 60 / 360;
  int degs = accumulator / acc_per_degree;
  int new_angle = g_motor_angle + degs;
  if (new_angle >= 360) new_angle -= 360;
  g_motor_angle = new_angle;
  accumulator -= degs * acc_per_degree;
}

int get_motor_orientation()
{
  if (g_motor_lockcount > LOCK_THRESHOLD)
  {
    return g_motor_angle;
  }
  else
  {
    return g_hall_prev_angle;
  }
}

int get_hall_angle()
{
  return g_hall_prev_angle;
}

int get_motor_rpm()
{
  return g_motor_rpm;
}