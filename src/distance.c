#include <pebble.h>

//#define DEBUG_LOG
#ifndef DEBUG_LOG
#undef APP_LOG
#define APP_LOG(level, fmt, args...) 
#endif

#define TIMECHART_HEIGHT 38

static Window *s_window;
static StatusBarLayer *s_status_bar;
static Layer *s_window_layer, *s_dots_layer, *s_progress_layer, *s_timechart_layer;
static TextLayer *s_dist_layer, *s_speed_layer, *s_steps_layer;
static HealthMinuteData s_minute_data[60];

static char s_current_dist_buffer[8], s_current_speed_buffer[16], s_steps_buffer[4];
static int s_dist_start = 0, s_dist_count = 0, s_dist_goal = 0;

typedef enum {
  // Kilometers per Hour
  SpeedTypeKpH = 0,
  // Minutes per Kilometer
  SpeedTypeMpK,

  SpeedTypeCount
} SpeedType;

static SpeedType s_speed_type = SpeedTypeKpH;
static time_t s_first_update = 0, s_last_update = 0;
static int s_last_dist = 0;
static int s_max_step = 50;
static int cm_per_sec = 0;

// Is health data available?
bool health_data_is_available() {
  return HealthServiceAccessibilityMaskAvailable &
    health_service_metric_accessible(HealthMetricWalkedDistanceMeters,
      time_start_of_today(), time(NULL));
}

static void get_dist_start() {
  s_dist_start = (int)health_service_sum_today(HealthMetricWalkedDistanceMeters);
}

// Todays current distance
static void get_dist_info() {
  s_dist_count = (int)health_service_sum_today(HealthMetricWalkedDistanceMeters);

  int prev_dist = s_last_dist - s_dist_start;
  int new_dist = s_dist_count - s_dist_start;

  time_t now = time(NULL);
  time_t delta = now - s_first_update;
  cm_per_sec = (new_dist * 100) / delta;

  if (prev_dist < s_dist_goal && new_dist >= s_dist_goal) {
    static const uint32_t const segments[] = {
        200, 100, 400, 800, 
        200, 100, 400, 800, 
        200, 100, 400, 800, 
        200, 100, 400
    };
    VibePattern pat = {
        .durations = segments,
        .num_segments = ARRAY_LENGTH(segments),
    };
    vibes_enqueue_custom_pattern(pat);
    vibes_enqueue_custom_pattern(pat);
    vibes_enqueue_custom_pattern(pat);
    vibes_enqueue_custom_pattern(pat);
  } else if (prev_dist / 1000 < new_dist / 1000) {
    vibes_double_pulse();
  }
  
  s_last_update = now;
  s_last_dist = s_dist_count;
}

static void display_speed(int cm_p_s, SpeedType type) { 
  switch(type) {
    case SpeedTypeKpH:
    {
      int m_p_h = cm_p_s * 36;
      int k = m_p_h / 1000;
      int f = m_p_h % 1000 / 10;
      snprintf(s_current_speed_buffer, sizeof(s_current_speed_buffer),
        "%d,%02dkm/h", k, f);
      break;
    }
    case SpeedTypeMpK:
    {
      int s_p_k = 100000 / cm_p_s;
      int m = s_p_k / 60;
      int s = s_p_k % 60;
      snprintf(s_current_speed_buffer, sizeof(s_current_speed_buffer),
        "%d:%02d/km", m, s);
      break;
    }
    default:
      return;
  }

  text_layer_set_text(s_speed_layer, s_current_speed_buffer);
}

static void display_distance(int dist) {
  int thousands = dist / 1000;
  int hundreds = dist % 1000;

  if(thousands > 0) {
    snprintf(s_current_dist_buffer, sizeof(s_current_dist_buffer),
      "%d,%03dm", thousands, hundreds);
  } else {
    snprintf(s_current_dist_buffer, sizeof(s_current_dist_buffer),
      "%dm", hundreds);
  }

  text_layer_set_text(s_dist_layer, s_current_dist_buffer);
}

static void health_handler(HealthEventType event, void *context) {
  if(event != HealthEventSleepUpdate) {
    get_dist_info();
    display_distance(s_dist_count - s_dist_start);
    layer_mark_dirty(s_progress_layer);
  }
}

static void update_timechart() {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "update_timechart");
  time_t now = time(NULL);
  time_t start = now - (60 * 60);
  time_t end = now - 60;
  uint num = health_service_get_minute_history(s_minute_data, 60, &start, &end);

  s_max_step = 50;
  for (uint i=0; i<num; ++i) {
    s_max_step = s_minute_data[i].steps > s_max_step ? s_minute_data[i].steps : s_max_step;
  }
  // Round up
  s_max_step = ((s_max_step + 9) / 10) * 10;
  
  layer_mark_dirty(s_timechart_layer);
}

static void tick_handler(struct tm *tick_time, TimeUnits changed) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "tick_handler");
  if (changed & MINUTE_UNIT) {
    update_timechart();
  }

  if (tick_time->tm_sec % 3) {
    time_t now = time(NULL);
    if (now - s_last_update > 10) {
      cm_per_sec = 0;
      s_last_update = now;
    }

    SpeedType type = (SpeedType)tick_time->tm_sec / 3 % (int)SpeedTypeCount;
    display_speed(cm_per_sec, type);
  }
}

static void dots_layer_update_proc(Layer *layer, GContext *ctx) {
  const GRect inset = grect_inset(layer_get_bounds(layer), GEdgeInsets(6));

  const int num_dots = 12;
  for(int i = 0; i <= num_dots; i++) {
    GPoint pos = gpoint_from_polar(inset, GOvalScaleModeFitCircle,
      DEG_TO_TRIGANGLE(i * 240 / num_dots - 120));
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_circle(ctx, pos, 2);
  }
}

static void progress_layer_update_proc(Layer *layer, GContext *ctx) {
  const GColor fillColor[6] = {GColorRed, GColorOrange, GColorChromeYellow, GColorYellow, GColorSpringBud, GColorGreen};
  const GRect inset = grect_inset(layer_get_bounds(layer), GEdgeInsets(2));
  int dist = (s_dist_count - s_dist_start);
  bool goal_achieved = dist >= s_dist_goal;

  graphics_context_set_fill_color(ctx,
    dist >= s_dist_goal ? GColorBlue : 
    fillColor[6 * dist / s_dist_goal] );

  graphics_fill_radial(ctx, inset, GOvalScaleModeFitCircle, 12,
    DEG_TO_TRIGANGLE(-120),
    goal_achieved ? DEG_TO_TRIGANGLE(120) :
    DEG_TO_TRIGANGLE(240 * dist / s_dist_goal - 120));
}

static void timechart_layer_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorGreen);

  GRect bb = layer_get_bounds(layer);

  // Grid
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_draw_rect(ctx, bb);
  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  graphics_draw_line(ctx, GPoint(0, bb.size.h / 2), GPoint(bb.size.w, bb.size.h / 2));
  graphics_draw_line(ctx, GPoint(30, 0), GPoint(30, bb.size.h));
  graphics_draw_line(ctx, GPoint(60, 0), GPoint(60, bb.size.h));
  graphics_draw_line(ctx, GPoint(90, 0), GPoint(90, bb.size.h));

  for (int i=0; i < 60; ++i) {
    HealthMinuteData m = s_minute_data[i];
    if (m.is_invalid) {
      continue;
    }
    uint steps = TIMECHART_HEIGHT * m.steps / s_max_step;
    graphics_context_set_fill_color(ctx, steps > 19 ? GColorGreen : GColorWhite);
    GRect s;
    s.origin = GPoint(i * 2, TIMECHART_HEIGHT - steps);
    s.size = GSize(1, steps);
    graphics_fill_rect(ctx, s, 0, GCornerNone);
  }

  snprintf(s_steps_buffer, sizeof(s_steps_buffer), "%d", s_max_step);
  text_layer_set_text(s_steps_layer, s_steps_buffer);

  }

static void up_click_handler(ClickRecognizerRef recognizer, void *context) { 
  s_dist_goal += 100;
  display_distance(s_dist_goal);
  layer_mark_dirty(s_progress_layer);
  vibes_cancel();
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) { 
  get_dist_start();
  get_dist_info();
  display_distance(s_dist_count - s_dist_start);
  cm_per_sec = 0;
  s_first_update = s_last_update = time(NULL);
  display_speed(cm_per_sec, SpeedTypeKpH);
  layer_mark_dirty(s_progress_layer);
  vibes_cancel();
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) { 
  s_dist_goal -= 100;
  display_distance(s_dist_goal);
  layer_mark_dirty(s_progress_layer);
  vibes_cancel();
}

static void back_click_handler(ClickRecognizerRef recognizer, void *context) {
  light_enable_interaction();
}
  
static void click_config_provider(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, down_click_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click_handler);
}

static void window_load(Window *window) {
  GRect window_bounds = layer_get_bounds(s_window_layer);

  // Status bar
  s_status_bar = status_bar_layer_create();
  layer_add_child(s_window_layer, status_bar_layer_get_layer(s_status_bar));

  // Dots for the progress indicator
  s_dots_layer = layer_create(window_bounds);
  layer_set_update_proc(s_dots_layer, dots_layer_update_proc);
  layer_add_child(s_window_layer, s_dots_layer);

  // Progress indicator
  s_progress_layer = layer_create(window_bounds);
  layer_set_update_proc(s_progress_layer, progress_layer_update_proc);
  layer_add_child(s_window_layer, s_progress_layer);

  // Create a layer to hold the current distance
  s_dist_layer = text_layer_create(
      GRect(0, PBL_IF_ROUND_ELSE(82, 78), window_bounds.size.w, TIMECHART_HEIGHT));
  text_layer_set_text_color(s_dist_layer, GColorWhite);
  text_layer_set_background_color(s_dist_layer, GColorClear);
  text_layer_set_font(s_dist_layer,
                      fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK));
  text_layer_set_text_alignment(s_dist_layer, GTextAlignmentCenter);
  layer_add_child(s_window_layer, text_layer_get_layer(s_dist_layer));

  // Create a layer to hold the current speed
  s_speed_layer = text_layer_create(
      GRect(0, PBL_IF_ROUND_ELSE(58, 54), window_bounds.size.w, TIMECHART_HEIGHT));
  text_layer_set_text_color(s_speed_layer, GColorYellow);
  text_layer_set_background_color(s_speed_layer, GColorClear);
  text_layer_set_font(s_speed_layer,
                      fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_speed_layer, GTextAlignmentCenter);
  layer_add_child(s_window_layer, text_layer_get_layer(s_speed_layer));

  // Time chart graph
  s_timechart_layer = layer_create(GRect(18, 168 - TIMECHART_HEIGHT, 120, TIMECHART_HEIGHT));
  layer_set_update_proc(s_timechart_layer, timechart_layer_update_proc);
  layer_add_child(s_window_layer, s_timechart_layer);
  s_steps_layer = text_layer_create(GRect(0, 125, 18, 14));
  text_layer_set_text_color(s_steps_layer, GColorWhite);
  text_layer_set_background_color(s_steps_layer, GColorClear);
  text_layer_set_font(s_steps_layer,
                      fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_steps_layer, GTextAlignmentRight);
  layer_add_child(s_window_layer, text_layer_get_layer(s_steps_layer));

  // Subscribe to health events if we can
  if(health_data_is_available()) {
    health_service_events_subscribe(health_handler, NULL);
  }
}

static void window_unload(Window *window) {
  layer_destroy(text_layer_get_layer(s_dist_layer));
  layer_destroy(text_layer_get_layer(s_speed_layer));
  layer_destroy(s_dots_layer);
  layer_destroy(s_progress_layer);
  layer_destroy(s_timechart_layer);
  layer_destroy(text_layer_get_layer(s_steps_layer));

  status_bar_layer_destroy(s_status_bar);
}

void init() {
  s_window = window_create();
  s_window_layer = window_get_root_layer(s_window);
  window_set_background_color(s_window, GColorBlack);

  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload
  });

  window_stack_push(s_window, true);

  update_timechart();

  tick_timer_service_subscribe(MINUTE_UNIT | SECOND_UNIT, tick_handler);
}

void deinit() {}

int main() {
  init();
  app_event_loop();
  deinit();
}
