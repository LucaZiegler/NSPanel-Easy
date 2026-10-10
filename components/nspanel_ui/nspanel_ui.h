#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "esphome/components/nextion/nextion.h"         // For nextion::Nextion (drawing commands)
#include "esphome/components/time/real_time_clock.h"  // For time::RealTimeClock (clock, date, schedule)
#include "esphome/core/component.h"

namespace esphome::nspanel_ui {

/// Screens of the custom UI. The Nextion TFT has a single page; these are drawn by the ESP32.
enum Page : uint8_t {
  PAGE_HOME = 0,
  PAGE_LICHT,
  PAGE_LICHT2,
  PAGE_SZENEN,
  PAGE_BAD,
  PAGE_MEDIEN,
  PAGE_WOHNUNG,
  PAGE_SCREENSAVER,
  PAGE_COUNT,
};

constexpr uint8_t LIGHT_COUNT = 12;  ///< 6 per light page
constexpr uint8_t SCENE_COUNT = 6;   ///< tiles on the scenes page
constexpr uint8_t QUICK_COUNT = 3;   ///< quick scenes on the home page (4th button = all lights off)
constexpr uint8_t TV_SOURCE_COUNT = 3;  ///< source buttons on the TV card of the flat page

/// Home Assistant values pushed in from `homeassistant` text sensors (see custom/nspanel_custom.yaml).
enum Key : uint8_t {
  K_LIGHT = 0,                   ///< + index, light state
  K_LIGHT_BRI = LIGHT_COUNT,     ///< + index, brightness attribute (0-255)
  K_CLIMATE_MODE = 2 * LIGHT_COUNT,
  K_CLIMATE_TARGET,
  K_CLIMATE_CURRENT,
  K_CLIMATE_HUMIDITY,
  K_CLIMATE_ACTION,
  K_CLIMATE_PRESET,
  K_CLIMATE_WINDOW,
  K_WIN_TOP,
  K_WIN_BOTTOM,
  K_WX_STATE,
  K_WX_TEMP,
  K_WX_FEELS,
  K_MEDIA_STATE,
  K_MEDIA_TITLE,
  K_MEDIA_ARTIST,
  K_MEDIA_APP,
  K_MEDIA_SOURCE,
  K_MEDIA_VOLUME,
  K_MEDIA_OUTPUT,
  K_MEDIA_MUTED,
  K_LOCK,
  K_ALARM,
  K_COUNT,
};

/// Entities the UI acts on (lights and scenes have their own lists).
enum Role : uint8_t {
  R_CLIMATE = 0,
  R_MEDIA,
  R_LOCK,
  R_ALARM,
  R_TV_ON_SCRIPT,
  R_SCREEN_OFF_SCRIPT,
  R_OUT_DENON_SCRIPT,
  R_OUT_TV_SCRIPT,
  R_COUNT,
};

/// Calls a Home Assistant action. `key`/`value` carry one extra data field (empty when not needed).
using ActionHandler = std::function<void(const std::string &action, const std::string &entity, const std::string &key,
                                         const std::string &value)>;
/// Fires a Home Assistant event (`esphome.nspanel_ui`).
using EventHandler = std::function<void(const std::string &type, const std::string &value)>;

class NSPanelUI : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // ---- configuration (from __init__.py)
  void set_display(nextion::Nextion *display) { this->display_ = display; }
  void set_time(time::RealTimeClock *time) { this->time_ = time; }
  void set_light(uint8_t index, const std::string &entity) { this->lights_[index] = entity; }
  void set_scene(uint8_t index, const std::string &entity) { this->scenes_[index] = entity; }
  void set_quick(uint8_t index, const std::string &entity) { this->quick_[index] = entity; }
  void set_role(uint8_t role, const std::string &entity) { this->roles_[role] = entity; }
  void set_tv_source(uint8_t index, const std::string &source) { this->tv_sources_[index] = source; }
  void set_screensaver_timeout(uint32_t ms) { this->screensaver_timeout_ms_ = ms; }
  void set_action_handler(ActionHandler handler) { this->action_ = std::move(handler); }
  void set_event_handler(EventHandler handler) { this->event_ = std::move(handler); }

  // ---- inputs
  /// @brief Nextion `on_setup`: the display is ready for drawing commands.
  void on_display_ready();
  /// @brief Nextion `on_touch`. The TFT reports coordinates as x/2 (page_id) and y/2 (component_id).
  void on_touch(uint8_t half_x, uint8_t half_y, bool pressed);
  /// @brief New Home Assistant value for a Key.
  void set_state(uint8_t key, const std::string &value);

  // ---- services (also used by the API actions kept compatible with the old firmware)
  void wake();
  void show_screensaver();
  void set_screensaver_enabled(bool enabled);
  void set_screensaver_brightness(int percent);
  void set_active_brightness(int percent);
  void navigate(const std::string &page);
  void notify_fullscreen(const std::string &title, const std::string &text, const std::string &button1,
                         const std::string &button2, uint32_t seconds);
  void notify_screensaver(const std::string &line1, const std::string &line2);
  /// @brief Stop drawing (e.g. while a TFT upload is running).
  void pause(bool paused) { this->paused_ = paused; }

 protected:
  enum Hold : uint8_t { HOLD_NONE = 0, HOLD_UNLOCK, HOLD_ARM };

  // drawing primitives
  void pic_(uint8_t pic);
  void crop_(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t pic);
  void crop_from_(int16_t x, int16_t y, int16_t w, int16_t h, int16_t sx, int16_t sy, uint8_t pic);
  void fill_(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void outline_(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void text_(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t font, uint16_t color, uint16_t bg, uint8_t align,
             const std::string &text);
  void dim_(int percent);

  // pages
  void show_page_(Page page);
  void render_full_();
  void render_dirty_();
  void draw_meta_();
  void draw_home_clock_();
  void draw_home_weather_();
  void draw_home_bath_();
  void draw_home_chips_();
  void draw_tile_(uint8_t slot);
  void draw_thermo_();
  void draw_bad_windows_();
  void draw_presets_();
  void draw_media_info_();
  void draw_media_play_();
  void draw_media_volume_();
  void draw_media_side_();
  void draw_flat_();
  void draw_tv_();
  void draw_screensaver_();
  void draw_modal_();

  // touch
  void tap_(int16_t x, int16_t y);
  void swipe_(int8_t direction);
  bool start_hold_(int16_t x, int16_t y);
  void cancel_hold_();
  void flash_(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t pic, uint32_t ms);

  // state helpers
  const std::string &st_(uint8_t key) const { return this->state_[key]; }
  bool light_on_(uint8_t index) const { return this->state_[K_LIGHT + index] == "on"; }
  int light_percent_(uint8_t index) const;
  float num_(uint8_t key, float fallback) const;
  bool window_open_(uint8_t key) const { return this->state_[key] == "on"; }
  bool any_window_open_() const { return this->window_open_(K_WIN_TOP) || this->window_open_(K_WIN_BOTTOM); }
  bool climate_off_() const { return this->state_[K_CLIMATE_MODE] == "off"; }
  float target_() const;
  std::string time_str_() const;
  std::string date_str_() const;
  void call_(const std::string &action, const std::string &entity, const std::string &key = "",
             const std::string &value = "");
  void mark_(uint32_t groups) { this->dirty_ |= groups; }
  void activity_();

  nextion::Nextion *display_{nullptr};
  time::RealTimeClock *time_{nullptr};
  ActionHandler action_;
  EventHandler event_;
  std::array<std::string, LIGHT_COUNT> lights_;
  std::array<std::string, SCENE_COUNT> scenes_;
  std::array<std::string, QUICK_COUNT> quick_;
  std::array<std::string, R_COUNT> roles_;
  std::array<std::string, TV_SOURCE_COUNT> tv_sources_;
  std::array<std::string, K_COUNT> state_;

  bool ready_{false};
  bool paused_{false};
  Page page_{PAGE_HOME};
  uint32_t dirty_{0};
  uint16_t tile_dirty_{0};
  uint32_t last_render_ms_{0};
  int last_minute_{-1};
  int last_hour_{-1};

  // screensaver / brightness
  bool screensaver_enabled_{true};
  uint32_t screensaver_timeout_ms_{30000};
  uint32_t last_activity_ms_{0};
  int active_brightness_{100};
  int screensaver_brightness_{10};
  std::string ss_note_;
  uint32_t ss_note_until_ms_{0};

  // touch
  bool pressing_{false};
  bool swallow_{false};
  bool last_touch_pressed_{false};
  uint32_t last_touch_ms_{0};
  int16_t press_x_{0}, press_y_{0};
  uint32_t press_ms_{0};
  Hold hold_{HOLD_NONE};
  bool hold_done_{false};
  uint32_t hold_draw_ms_{0};

  // flashes (short highlight after a tap)
  bool flash_active_{false};
  int16_t flash_x_{0}, flash_y_{0}, flash_w_{0}, flash_h_{0};
  uint8_t flash_pic_{0};
  uint32_t flash_until_ms_{0};

  // thermostat: taps are collected and sent once
  bool target_pending_{false};
  float target_local_{21.0f};
  uint32_t target_send_ms_{0};
  uint32_t target_sent_ms_{0};

  // modal
  bool modal_{false};
  std::string modal_title_, modal_line1_, modal_line2_, modal_btn1_, modal_btn2_;
  uint32_t modal_until_ms_{0};
};

}  // namespace esphome::nspanel_ui
