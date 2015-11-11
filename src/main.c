#include "pebble.h"
// Window
static Window *window;
static Layer *window_layer;
static GRect window_bounds;
// Background
static BitmapLayer *background_layer;
static GBitmap *background_image;
// Time
static struct tm *current_time;
// Hands
static Layer *hands_layer;
static GPath *minute_hand;
static GPath *hour_hand;
const GPathInfo MINUTE_HAND_POINTS = { 4, (GPoint[] ) { { -4, 12 }, { 4, 12 }, { 4, -69 }, { -4, -69 }, } };
const GPathInfo HOUR_HAND_POINTS = { 4, (GPoint[] ) { { -4, 12 }, { 4, 12 }, { 4, -43 }, { -4, -43 }, } };
// Date Window
static BitmapLayer *date_window_layer;
static GBitmap *image_date_window;
// Date Text
static TextLayer *date_layer;
static char date_text[] = "13";

// Heartrate text
static TextLayer *hr_layer;
static char hr_count_text[] = "Count Beats";
static char hr_ready_text[] = "Find Pulse";
static char hr_cancelled_text[] = "Cancelled";
static char hr_oneclick_text[] = "Shake again";

// Status
static bool status_showing = false;
// Bluetooth
static BitmapLayer *bt_layer;
static GBitmap *icon_bt_connected;
static GBitmap *icon_bt_disconnected;
static bool bt_status = false;
static bool already_vibrated = false;
static bool hr_mode = false;
static bool hr_one_shake = false;

// Pebble Battery Icon
static BitmapLayer *battery_layer;
static GBitmap *icon_battery_charging;
static GBitmap *icon_battery_100;
static GBitmap *icon_battery_90;
static GBitmap *icon_battery_80;
static GBitmap *icon_battery_70;
static GBitmap *icon_battery_60;
static GBitmap *icon_battery_50;
static GBitmap *icon_battery_40;
static GBitmap *icon_battery_30;
static GBitmap *icon_battery_20;
static GBitmap *icon_battery_10;
static uint8_t battery_level;
static bool battery_plugged = false;
static bool battery_charging = false;
static bool battery_hide = false;
// Timers
static AppTimer *display_timer;
static AppTimer *hr_timer;
static AppTimer *hr_ready_timer;
static AppTimer *vibrate_timer;
///
/// Easy set items for version management (and future settings screen)
///
static bool config_badge = true;
static bool config_hide_date = false;
static bool config_vibe_on_disconnect = false;
static bool config_hr_support = false;

///
/// Draw Callbacks
///
// Draw/Update/Hide Battery icon. Loads appropriate icon per battery state. Sets appropriate hide/unhide status for state. Activates hide status. -- DONE
void draw_battery_icon() {
        if (battery_plugged && !status_showing) {
    if (battery_charging) bitmap_layer_set_bitmap(battery_layer, icon_battery_charging);
    else bitmap_layer_set_bitmap(battery_layer, icon_battery_100);
  }
  else {
    if (battery_level == 100) bitmap_layer_set_bitmap(battery_layer, icon_battery_100);
    else if (battery_level == 90) bitmap_layer_set_bitmap(battery_layer, icon_battery_90);
    else if (battery_level == 80) bitmap_layer_set_bitmap(battery_layer, icon_battery_80);
    else if (battery_level == 70) bitmap_layer_set_bitmap(battery_layer, icon_battery_70);
    else if (battery_level == 60) bitmap_layer_set_bitmap(battery_layer, icon_battery_60);
    else if (battery_level == 50) bitmap_layer_set_bitmap(battery_layer, icon_battery_50);
    else if (battery_level == 40) bitmap_layer_set_bitmap(battery_layer, icon_battery_40);
    else if (battery_level == 30) bitmap_layer_set_bitmap(battery_layer, icon_battery_30);
    else if (battery_level == 20) bitmap_layer_set_bitmap(battery_layer, icon_battery_20);
    else if (battery_level == 10) bitmap_layer_set_bitmap(battery_layer, icon_battery_10);
  }
  if (battery_plugged) battery_hide = false;
  else if (battery_level <= 30) battery_hide = false;
  else if (status_showing) battery_hide = false;
  else battery_hide = true;
  layer_set_hidden(bitmap_layer_get_layer(battery_layer), battery_hide);
}
// Draw/Update/Hide bluetooth status icon. Loads appropriate icon. Sets appropriate hide/unhide status per status. ("draw_bt_icon" is separate from "bt_connection_handler" as to not vibrate if not connected on init)
void draw_bt_icon() {
  if (bt_status) bitmap_layer_set_bitmap(bt_layer, icon_bt_connected);
  else bitmap_layer_set_bitmap(bt_layer, icon_bt_disconnected);
  if (status_showing) layer_set_hidden(bitmap_layer_get_layer(bt_layer), false);
  else layer_set_hidden(bitmap_layer_get_layer(bt_layer), bt_status);
}
// Draw/Update Date
void draw_date() {
  strftime(date_text, sizeof(date_text), "%d", current_time);
  text_layer_set_text(date_layer, date_text);
}

// Draw/Update Clock Hands
void draw_hands(Layer *layer, GContext *ctx){
  // Hour hand
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_context_set_stroke_color(ctx, GColorBlack);  
  gpath_rotate_to(hour_hand, (((TRIG_MAX_ANGLE * current_time->tm_hour) / 12) + (((TRIG_MAX_ANGLE * current_time->tm_min) / 60) / 12))); //Moves hour hand 1/60th between hour indicators.  Only updates per minute.
  gpath_draw_filled(ctx, hour_hand);
  gpath_draw_outline(ctx, hour_hand);
  // Minite hand
  gpath_rotate_to(minute_hand, TRIG_MAX_ANGLE * current_time->tm_min / 60);
  gpath_draw_filled(ctx, minute_hand);
  gpath_draw_outline(ctx, minute_hand);
  // Dot in the middle
  const GPoint center = grect_center_point(&window_bounds);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, center, 2);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, center, 1);  
}

void long_pulse(){
  vibes_long_pulse();
}

void short_pulse(){
  vibes_short_pulse();
}

void hide_hr() {
  // Show window
  layer_set_hidden(text_layer_get_layer(hr_layer), true);
  app_timer_cancel(hr_timer);
  hr_mode=false;
}

void finish_hr() {
 long_pulse();
 hide_hr();
}

// Heart rate mode. Show text and wait for 30 sec.
void start_hr() {
  // Show text
  text_layer_set_text(hr_layer, hr_count_text);
  layer_set_hidden(text_layer_get_layer(hr_layer), false);
  long_pulse();
  // 30 Sec timer then call "finish_hr". Cancels previous timer if ongoing
  app_timer_cancel(hr_timer);
  hr_mode=true;
  hr_timer = app_timer_register(30000, finish_hr, NULL);
}

void cancel_hr_shake() {
  hr_one_shake=false;
  if(!hr_mode) {
    layer_set_hidden(text_layer_get_layer(hr_layer), true);
  }
}

// Heart rate mode. Show text and wait for 5 secs
void prepare_hr() {
  if (hr_one_shake==false) {
    hr_one_shake=true;
    text_layer_set_text(hr_layer, hr_oneclick_text);
    layer_set_hidden(text_layer_get_layer(hr_layer), false);
    app_timer_register(2000, cancel_hr_shake, NULL);
    return;
  }
    
  // Show text
  text_layer_set_text(hr_layer, hr_ready_text);
  layer_set_hidden(text_layer_get_layer(hr_layer), false);
  short_pulse();
  // 5 Sec timer then call "start_hr".
  app_timer_cancel(hr_timer);
  app_timer_cancel(hr_ready_timer);
  hr_mode=true;
  hr_ready_timer = app_timer_register(5000, start_hr, NULL);  
}


// Hides status icons. Call draw of default battery/bluetooth icons (which will show or hide icon based on set logic). Hides date/window if selected
void hide_status() {
  status_showing = false;
  draw_battery_icon();
  draw_bt_icon();
  layer_set_hidden(text_layer_get_layer(date_layer), config_hide_date);
  layer_set_hidden(bitmap_layer_get_layer(date_window_layer), config_hide_date);
}
// Shows status icons. Call draw of default battery/bluetooth icons (which will show or hide icon based on set logic). Shows date/window.
void show_status() {
  status_showing = true;
  // Redraws battery icon
  draw_battery_icon();
  // Shows current bluetooth status icon
  draw_bt_icon();
  // Show Date Window & Date
  layer_set_hidden(text_layer_get_layer(date_layer), false);
  layer_set_hidden(bitmap_layer_get_layer(date_window_layer), false);
  // 4 Sec timer then call "hide_status". Cancels previous timer if another show_status is called within the 5000ms
  app_timer_cancel(display_timer);  
  display_timer = app_timer_register(5000, hide_status, NULL);
}


///
/// PEBBLE HANDLERS
///
// Battery state handler. Updates battery level, plugged, charging states. Calls "draw_battery_icon".
void battery_state_handler(BatteryChargeState c) {
  battery_level = c.charge_percent;
  battery_plugged = c.is_plugged;
  battery_charging = c.is_charging;
  draw_battery_icon();
}

// If bluetooth is still not connected after 5 sec delay, vibrate. (double vibe was too easily confused with a signle short vibe.  Two short vibes was easier to distinguish from a notification)
void bt_vibrate(){
  if (!bt_status && !already_vibrated) {
    already_vibrated = true;
    app_timer_register(0, short_pulse, NULL);
    app_timer_register(350, short_pulse, NULL);
  }
}
// Bluetooth connection status handler.  Updates bluetooth status. Calls "draw_bt_icon". If bluetooth not connected, wait 5 seconds then call "bt_vibrate". Cancel any vibrate timer if status change in 5 seconds (minimizes repeat vibration alerts)
void bt_connection_handler(bool bt) {
  bt_status = bt;
  draw_bt_icon();
  if(config_vibe_on_disconnect) {
    app_timer_cancel(vibrate_timer);
    if (!bt_status) vibrate_timer = app_timer_register(5000, bt_vibrate, NULL);
    else if (bt_status && already_vibrated) already_vibrated = false;
  }
}

// Shake/Tap Handler. On shake/tap... call "show_status"
// also vibrate after 5s then again after 30 for checking pulse
void tap_handler(AccelAxisType axis, int32_t direction) {
  if (!status_showing) {
    show_status();
  }
  else if (config_hr_support) {
    prepare_hr();
  }
  
}
// Time Update Handler. Set current time, redraw date (to update when changed not at 2359) and update hands layer
void update_time(struct tm *t, TimeUnits units_changed) {
  current_time = t;
  layer_mark_dirty(hands_layer);
  draw_date();
}
///
// Deint item to free up memory when leaving watchface
///
void deinit() {
  // cancel any timers
  app_timer_cancel(display_timer);
  app_timer_cancel(hr_timer);
  app_timer_cancel(hr_ready_timer);
  app_timer_cancel(vibrate_timer);

  // unsubscribe from services
  tick_timer_service_unsubscribe();
  bluetooth_connection_service_unsubscribe();
  battery_state_service_unsubscribe();
  accel_tap_service_unsubscribe();

  // and destroy everything
  layer_destroy(hands_layer);
  gpath_destroy(minute_hand);
  gpath_destroy(hour_hand);
  bitmap_layer_destroy(battery_layer);
  gbitmap_destroy(icon_battery_charging);
  gbitmap_destroy(icon_battery_100);
  gbitmap_destroy(icon_battery_90);
  gbitmap_destroy(icon_battery_80);
  gbitmap_destroy(icon_battery_70);
  gbitmap_destroy(icon_battery_60);
  gbitmap_destroy(icon_battery_50);
  gbitmap_destroy(icon_battery_40);
  gbitmap_destroy(icon_battery_30);
  gbitmap_destroy(icon_battery_20);
  gbitmap_destroy(icon_battery_10);
  bitmap_layer_destroy(bt_layer); 
  gbitmap_destroy(icon_bt_connected);
  gbitmap_destroy(icon_bt_disconnected);
  text_layer_destroy(hr_layer);
  text_layer_destroy(date_layer);
  bitmap_layer_destroy(date_window_layer);
  gbitmap_destroy(image_date_window);
  bitmap_layer_destroy(background_layer);
  gbitmap_destroy(background_image);
  layer_destroy(window_layer);
  // don't destroy window, as this crashes watchface
  // TODO - is this a memory leak???
  //window_destroy(window);
}
///
/// Initial watchface call
///
void init() {

  // Window init
  window = window_create();
  window_stack_push(window, true /* Animated */);
  window_layer = window_get_root_layer(window);
  window_bounds = layer_get_bounds(window_layer);
  const GPoint center = grect_center_point(&window_bounds);
  
  // Background image init and draw
  if (config_badge) background_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND_BADGE);
  else background_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND_NOBADGE);
  background_layer = bitmap_layer_create(window_bounds);
  bitmap_layer_set_alignment(background_layer, GAlignCenter);
    layer_add_child(window_layer, bitmap_layer_get_layer(background_layer));
  bitmap_layer_set_bitmap(background_layer, background_image);
  
  // Time at init
  time_t now = time(NULL);
  current_time = localtime(&now);
  
  // Date Window init, draw and hide
  image_date_window = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DATE_WINDOW);
  date_window_layer = bitmap_layer_create(GRect(117,75,27,21));
  layer_add_child(window_layer, bitmap_layer_get_layer(date_window_layer));
  bitmap_layer_set_bitmap(date_window_layer, image_date_window);
  layer_set_hidden(bitmap_layer_get_layer(date_window_layer), config_hide_date);
      
  // Date text init, then call "draw_date"
  date_layer = text_layer_create(GRect(119, 72, 30, 30));
  text_layer_set_text_color(date_layer, GColorBlack);
  text_layer_set_text_alignment(date_layer, GTextAlignmentLeft);
  text_layer_set_background_color(date_layer, GColorClear);
  text_layer_set_font(date_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_CONDENSED_21)));
  layer_add_child(window_layer, text_layer_get_layer(date_layer));
  layer_set_hidden(text_layer_get_layer(date_layer), config_hide_date);
  draw_date();
  
    // hr text init
  hr_layer = text_layer_create(GRect(35, 107, 80, 20));
  text_layer_set_text_color(hr_layer, GColorBlack);
  text_layer_set_text_alignment(hr_layer, GTextAlignmentCenter);
  text_layer_set_background_color(hr_layer, GColorWhite);
  layer_add_child(window_layer, text_layer_get_layer(hr_layer));
  text_layer_set_text(hr_layer, hr_ready_text);
  layer_set_hidden(text_layer_get_layer(date_layer), true);
  hide_hr();

  
  // Bluetooth icon init, then call "draw_bt_icon" (doesn't call bt_connection_handler to avoid vibrate at init if bluetooth not connected)
  icon_bt_connected = gbitmap_create_with_resource(RESOURCE_ID_BLUETOOTH_CONNECTED);
  icon_bt_disconnected = gbitmap_create_with_resource(RESOURCE_ID_BLUETOOTH_DISCONNECTED);
  bt_layer = bitmap_layer_create(GRect(60,139,24,24));
  layer_add_child(window_layer, bitmap_layer_get_layer(bt_layer));
  bt_status = bluetooth_connection_service_peek();
  draw_bt_icon();
  
  // Pebble battery icon init, then call "battery_state_handler" with current battery state
  icon_battery_charging = gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_CHARGING);
  icon_battery_100 = gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_100);
  icon_battery_90 = gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_90);
  icon_battery_80 = gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_80);
  icon_battery_70 = gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_70);
  icon_battery_60 = gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_60);
  icon_battery_50 = gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_50);
  icon_battery_40 = gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_40);
  icon_battery_30 = gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_30);
  icon_battery_20 = gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_20);
  icon_battery_10 = gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_10);
  battery_layer = bitmap_layer_create(GRect(53,4,41,24));
  bitmap_layer_set_background_color(battery_layer, GColorBlack);
  layer_add_child(window_layer, bitmap_layer_get_layer(battery_layer));
  battery_state_handler(battery_state_service_peek());
  
  // Show status screen on init (comment out to have battery and bluetooth status hidden at init)
  show_status();
     
  // Watch hands init
  hour_hand = gpath_create(&HOUR_HAND_POINTS);
  minute_hand = gpath_create(&MINUTE_HAND_POINTS);
  gpath_move_to(hour_hand, center);
  gpath_move_to(minute_hand, center);
  hands_layer = layer_create(window_bounds);
  layer_add_child(window_layer, hands_layer);
  layer_set_update_proc(hands_layer, &draw_hands);
  
  // check date is shown
  draw_date();
}

///
/// Main. Initiate watchapp. Subcribe to services: Time Update, Bluetooth connection status update, Battery state update, Accelerometer tap/shake event, App message recieve.
///
int main(void) {
  init();
  tick_timer_service_subscribe(MINUTE_UNIT, update_time);
  bluetooth_connection_service_subscribe(bt_connection_handler);
  battery_state_service_subscribe(battery_state_handler);
  accel_tap_service_subscribe(tap_handler);
  app_event_loop();
  deinit();
}


