#include <pebble.h>

static Window *s_window;
static StatusBarLayer *s_statusBar;
static Layer *s_window_layer, *s_dots_layer, *s_progress_layer;
static TextLayer *s_dist_layer, *s_speed_layer;

static char s_current_dist_buffer[8], s_current_speed_buffer[16];
static int s_dist_start = 0, s_dist_count = 0, s_dist_goal = 0;

typedef enum {
  // Meters per Second
  SpeedTypeMpS = 0,
  // Kilometers per Hour
  SpeedTypeKpH,
  // Minutes per Kilometer
  SpeedTypeMpK,

  SpeedTypeCount
} SpeedType;

static SpeedType s_speed_type = SpeedTypeMpS;
static time_t s_last_update = 0;
static int s_last_dist = 0;
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
    case SpeedTypeMpS:
    {
      int m = cm_p_s / 100;
      int cm = cm_p_s % 100;
      snprintf(s_current_speed_buffer, sizeof(s_current_speed_buffer),
        "%d,%02dm/s", m, cm);
      break;
    }
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

static void tick_handler(struct tm *tick_time, TimeUnits changed) {
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
  for(int i = 0; i < num_dots; i++) {
    GPoint pos = gpoint_from_polar(inset, GOvalScaleModeFitCircle,
      DEG_TO_TRIGANGLE(i * 360 / num_dots));
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_circle(ctx, pos, 2);
  }
}

static void progress_layer_update_proc(Layer *layer, GContext *ctx) {
  const GColor fillColor[6] = {GColorRed, GColorOrange, GColorChromeYellow, GColorYellow, GColorSpringBud, GColorGreen};
  const GRect inset = grect_inset(layer_get_bounds(layer), GEdgeInsets(2));
  int dist = (s_dist_count - s_dist_start);

  graphics_context_set_fill_color(ctx,
    dist >= s_dist_goal ? GColorBlue : 
    fillColor[6 * dist / s_dist_goal] );

  graphics_fill_radial(ctx, inset, GOvalScaleModeFitCircle, 12,
    DEG_TO_TRIGANGLE(0),
    DEG_TO_TRIGANGLE(360 * dist / s_dist_goal));
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) { 
  s_dist_goal += 50;
  display_distance(s_dist_goal);
  layer_mark_dirty(s_progress_layer);
  vibes_cancel();
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) { 
  get_dist_start();
  get_dist_info();
  display_distance(s_dist_count - s_dist_start);
  cm_per_sec = 0;
  s_last_update = time(NULL);
  display_speed(cm_per_sec, SpeedTypeMpS);
  layer_mark_dirty(s_progress_layer);
  vibes_cancel();
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) { 
  s_dist_goal -= 50;
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
  s_statusBar = status_bar_layer_create();
  layer_add_child(s_window_layer, status_bar_layer_get_layer(s_statusBar));

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
      GRect(0, PBL_IF_ROUND_ELSE(82, 78), window_bounds.size.w, 38));
  text_layer_set_text_color(s_dist_layer, GColorWhite);
  text_layer_set_background_color(s_dist_layer, GColorClear);
  text_layer_set_font(s_dist_layer,
                      fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK));
  text_layer_set_text_alignment(s_dist_layer, GTextAlignmentCenter);
  layer_add_child(s_window_layer, text_layer_get_layer(s_dist_layer));

  // Create a layer to hold the current speed
  s_speed_layer = text_layer_create(
      GRect(0, PBL_IF_ROUND_ELSE(58, 54), window_bounds.size.w, 38));
  text_layer_set_text_color(s_speed_layer, GColorYellow);
  text_layer_set_background_color(s_speed_layer, GColorClear);
  text_layer_set_font(s_speed_layer,
                      fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_speed_layer, GTextAlignmentCenter);
  layer_add_child(s_window_layer, text_layer_get_layer(s_speed_layer));

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

  status_bar_layer_destroy(s_statusBar);
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

  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
}

void deinit() {}

int main() {
  init();
  app_event_loop();
  deinit();
}
