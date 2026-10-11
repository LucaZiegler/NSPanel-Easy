#include "nspanel_ui.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "esphome/core/hal.h"      // For millis()
#include "esphome/core/helpers.h"  // For parse_number<T>()
#include "esphome/core/log.h"
#include "generated_fonts.h"   // For glyph widths (text fitting)
#include "generated_layout.h"  // For picture ids and rectangles exported from custom/design/ui.html

namespace esphome::nspanel_ui {

using namespace gen;

static const char *const TAG = "nspanel_ui";

// UTF-8 snippets used in texts
#define DEG "\xC2\xB0"        // °
#define DOT " \xC2\xB7 "      // " · "
#define BULLET "\xE2\x80\xA2"  // • (the Sarasa fonts have no U+25CF)
#define ELLIPSIS "\xE2\x80\xA6"

// Dirty groups (what to redraw on the visible page)
static constexpr uint32_t G_META = 1u << 0;
static constexpr uint32_t G_CLOCK = 1u << 1;
static constexpr uint32_t G_WX = 1u << 2;
static constexpr uint32_t G_BATH = 1u << 3;
static constexpr uint32_t G_CHIPS = 1u << 4;
static constexpr uint32_t G_THERMO = 1u << 5;
static constexpr uint32_t G_BAD_WIN = 1u << 6;
static constexpr uint32_t G_PRESETS = 1u << 7;
static constexpr uint32_t G_MEDIA_INFO = 1u << 8;
static constexpr uint32_t G_MEDIA_PLAY = 1u << 9;
static constexpr uint32_t G_MEDIA_VOL = 1u << 10;
static constexpr uint32_t G_MEDIA_SIDE = 1u << 11;
static constexpr uint32_t G_FLAT = 1u << 12;
static constexpr uint32_t G_SS = 1u << 13;

static constexpr uint32_t HOLD_MS = 2000;            ///< hold duration for unlock and arm
static constexpr uint32_t TOUCH_REPEAT_MS = 80;      ///< same touch event again within this time = duplicate
static constexpr int16_t SWIPE_MIN_DX = 70;          ///< horizontal distance for a swipe
static constexpr int16_t SWIPE_MAX_DY = 60;          ///< vertical tolerance for a swipe
static constexpr uint32_t TARGET_SEND_DELAY = 700;   ///< collect +/- taps before sending
static constexpr uint32_t TARGET_HOLD_MS = 10000;    ///< keep the local target until HA confirms
static constexpr Rect PAGER_HIT = {170, 0, 110, 42};  ///< tap on the page dots = next light page
static constexpr Rect LOCK_PROGRESS = {26, 134, 181, 4};
static constexpr Rect MODAL_SINGLE_HIT = {60, 220, 330, 52};
static constexpr TextSpec T_MODAL_SINGLE = {{64, 228, 322, 36}, 1, 10273, 64333, 1};

static const char *const PAGE_NAMES[PAGE_COUNT] = {"home", "licht", "licht2", "szenen",
                                                    "bad",  "medien", "wohnung", "screensaver"};
static const char *const WEEKDAYS[7] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
static const char *const MONTHS[12] = {"Januar", "Februar", "M\xC3\xA4rz",   "April",   "Mai",      "Juni",
                                       "Juli",   "August",  "September",     "Oktober", "November", "Dezember"};

// Presets on the bad page: region, Better Thermostat preset (empty = hvac off)
struct PresetDef {
  Rect region;
  const char *preset;
};
static constexpr PresetDef PRESETS[6] = {
    {R_BAD_PRESET_COMFORT, "comfort"}, {R_BAD_PRESET_ECO, "eco"},  {R_BAD_PRESET_BOOST, "boost"},
    {R_BAD_PRESET_OFF, ""},            {R_BAD_PRESET_AWAY, "away"}, {R_BAD_PRESET_SLEEP, "sleep"},
};

static constexpr Rect TILE_RECTS[6] = {R_LICHT_TILE_1, R_LICHT_TILE_2, R_LICHT_TILE_3,
                                       R_LICHT_TILE_4, R_LICHT_TILE_5, R_LICHT_TILE_6};
static constexpr TextSpec TILE_TEXTS[6] = {T_LICHT_TILE_1_STATE, T_LICHT_TILE_2_STATE, T_LICHT_TILE_3_STATE,
                                           T_LICHT_TILE_4_STATE, T_LICHT_TILE_5_STATE, T_LICHT_TILE_6_STATE};
static constexpr FillSpec TILE_BARS[6] = {F_LICHT_TILE_1_BAR, F_LICHT_TILE_2_BAR, F_LICHT_TILE_3_BAR,
                                          F_LICHT_TILE_4_BAR, F_LICHT_TILE_5_BAR, F_LICHT_TILE_6_BAR};
static constexpr Rect SCENE_RECTS[6] = {R_SZENEN_SCENE_1, R_SZENEN_SCENE_2, R_SZENEN_SCENE_3,
                                        R_SZENEN_SCENE_4, R_SZENEN_SCENE_5, R_SZENEN_SCENE_6};
static constexpr Rect QUICK_RECTS[4] = {R_HOME_HOME_Q1, R_HOME_HOME_Q2, R_HOME_HOME_Q3, R_HOME_HOME_Q4};

// ------------------------------------------------------------------ text helpers
static uint32_t utf8_next(const std::string &s, size_t &i) {
  const auto c = static_cast<uint8_t>(s[i]);
  uint32_t cp;
  size_t n;
  if (c < 0x80) {
    cp = c;
    n = 1;
  } else if ((c & 0xE0) == 0xC0) {
    cp = c & 0x1F;
    n = 2;
  } else if ((c & 0xF0) == 0xE0) {
    cp = c & 0x0F;
    n = 3;
  } else {
    cp = c & 0x07;
    n = 4;
  }
  for (size_t k = 1; k < n && i + k < s.size(); k++)
    cp = (cp << 6) | (static_cast<uint8_t>(s[i + k]) & 0x3F);
  i += n;
  return cp;
}

static uint16_t glyph_width(uint8_t font, uint32_t cp) {
  if (font >= FONT_COUNT)
    return 0;
  if (cp >= FONT_WIDTH_FIRST && cp <= FONT_WIDTH_LAST) {
    const uint8_t w = FONT_WIDTHS[font][cp - FONT_WIDTH_FIRST];
    if (w != 0)
      return w;
  }
  for (const auto &e : FONT_EXTRA) {
    if (e.cp == cp && e.w[font] != 0)
      return e.w[font];
  }
  return FONT_FALLBACK[font];
}

static uint16_t text_width(uint8_t font, const std::string &s) {
  uint16_t w = 0;
  for (size_t i = 0; i < s.size();)
    w += glyph_width(font, utf8_next(s, i));
  return w;
}

/// Cut a text to the width with an ellipsis.
static std::string fit(uint8_t font, const std::string &s, int16_t max_w) {
  if (text_width(font, s) <= max_w)
    return s;
  const uint16_t ell = text_width(font, ELLIPSIS);
  std::string out;
  uint16_t w = 0;
  for (size_t i = 0; i < s.size();) {
    const size_t start = i;
    const uint16_t gw = glyph_width(font, utf8_next(s, i));
    if (w + gw + ell > max_w)
      break;
    out.append(s, start, i - start);
    w += gw;
  }
  while (!out.empty() && out.back() == ' ')
    out.pop_back();
  return out + ELLIPSIS;
}

/// Word-wrap into two lines, the second one cut with an ellipsis.
static void wrap2(uint8_t font, const std::string &s, int16_t max_w, std::string &l1, std::string &l2) {
  l1.clear();
  l2.clear();
  if (text_width(font, s) <= max_w) {
    l1 = s;
    return;
  }
  size_t best = std::string::npos;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == ' ' && text_width(font, s.substr(0, i)) <= max_w)
      best = i;
  }
  if (best == std::string::npos) {
    l1 = fit(font, s, max_w);
    return;
  }
  l1 = s.substr(0, best);
  l2 = fit(font, s.substr(best + 1), max_w);
}

static std::string clean(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '"')
      out += '\'';
    else if (c == '\n' || c == '\r' || c == '\t')
      out += ' ';
    else
      out += c;
  }
  return out;
}

static bool usable(const std::string &s) { return !s.empty() && s != "unknown" && s != "unavailable" && s != "None"; }

static std::string fmt1(float v) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f", v);
  for (char *p = buf; *p; p++)
    if (*p == '.')
      *p = ',';
  return buf;
}

static std::string fmt0(float v) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", static_cast<int>(lroundf(v)));
  return buf;
}

static std::string upper_ascii(std::string s) {
  for (auto &c : s)
    if (c >= 'a' && c <= 'z')
      c = static_cast<char>(c - 'a' + 'A');
  return s;
}

struct WxDef {
  const char *state;
  uint8_t icon;
  const char *text;
};
static constexpr WxDef WEATHER[] = {
    {"sunny", WX_SUNNY, "Sonnig"},
    {"clear-night", WX_NIGHT, "Klar"},
    {"partlycloudy", WX_PARTLY, "Teils bew\xC3\xB6lkt"},
    {"cloudy", WX_CLOUDY, "Bew\xC3\xB6lkt"},
    {"rainy", WX_RAINY, "Regen"},
    {"pouring", WX_POURING, "Starkregen"},
    {"snowy", WX_SNOWY, "Schnee"},
    {"snowy-rainy", WX_SNOWY, "Schneeregen"},
    {"fog", WX_FOG, "Nebel"},
    {"lightning", WX_LIGHTNING, "Gewitter"},
    {"lightning-rainy", WX_LIGHTNING, "Gewitter"},
    {"windy", WX_WINDY, "Windig"},
    {"windy-variant", WX_WINDY, "Windig"},
    {"hail", WX_HAIL, "Hagel"},
};

static const WxDef &weather_def(const std::string &state) {
  static constexpr WxDef UNKNOWN = {"", WX_UNKNOWN, "-"};
  for (const auto &w : WEATHER)
    if (state == w.state)
      return w;
  return UNKNOWN;
}

// ------------------------------------------------------------------ setup / config
void NSPanelUI::setup() { this->last_activity_ms_ = millis(); }

void NSPanelUI::dump_config() {
  ESP_LOGCONFIG(TAG, "NSPanel custom UI:");
  ESP_LOGCONFIG(TAG, "  Screensaver timeout: %" PRIu32 " ms", this->screensaver_timeout_ms_);
  for (uint8_t i = 0; i < LIGHT_COUNT; i++)
    ESP_LOGCONFIG(TAG, "  Light %u: %s", i + 1, this->lights_[i].c_str());
  ESP_LOGCONFIG(TAG, "  Climate: %s", this->roles_[R_CLIMATE].c_str());
  ESP_LOGCONFIG(TAG, "  Media: %s", this->roles_[R_MEDIA].c_str());
}

void NSPanelUI::on_display_ready() {
  ESP_LOGI(TAG, "Display ready");
  this->ready_ = true;
  this->dim_(this->active_brightness_);
  this->last_activity_ms_ = millis();
  this->show_page_(PAGE_HOME);
}

// ------------------------------------------------------------------ state input
void NSPanelUI::set_state(uint8_t key, const std::string &value) {
  if (key >= K_COUNT || this->state_[key] == value)
    return;
  this->state_[key] = value;
  if (key < K_LIGHT_BRI + LIGHT_COUNT) {
    this->tile_dirty_ |= 1u << (key % LIGHT_COUNT);
    this->mark_(G_META);
    return;
  }
  switch (key) {
    case K_CLIMATE_TARGET:
      if (this->target_pending_ && std::fabs(this->num_(K_CLIMATE_TARGET, -100) - this->target_local_) < 0.05f)
        this->target_pending_ = false;
      this->mark_(G_THERMO | G_BATH);
      break;
    case K_CLIMATE_MODE:
    case K_CLIMATE_PRESET:
      this->mark_(G_THERMO | G_BATH | G_PRESETS);
      break;
    case K_CLIMATE_CURRENT:
    case K_CLIMATE_HUMIDITY:
    case K_CLIMATE_ACTION:
    case K_CLIMATE_WINDOW:
      this->mark_(G_THERMO | G_BATH | G_SS);
      break;
    case K_WIN_TOP:
    case K_WIN_BOTTOM:
      this->mark_(G_CHIPS | G_BAD_WIN | G_BATH | G_THERMO | G_FLAT | G_META | G_SS);
      break;
    case K_WX_STATE:
    case K_WX_TEMP:
    case K_WX_FEELS:
      this->mark_(G_WX | G_SS);
      break;
    case K_MEDIA_STATE:
      this->mark_(G_MEDIA_INFO | G_MEDIA_PLAY | G_MEDIA_SIDE | G_META | G_FLAT);
      break;
    case K_MEDIA_SOURCE:
      this->mark_(G_MEDIA_INFO | G_FLAT);
      break;
    case K_MEDIA_MUTED:
      this->mark_(G_FLAT);
      break;
    case K_MEDIA_TITLE:
    case K_MEDIA_ARTIST:
    case K_MEDIA_APP:
      this->mark_(G_MEDIA_INFO);
      break;
    case K_MEDIA_VOLUME:
      this->mark_(G_MEDIA_VOL);
      break;
    case K_MEDIA_OUTPUT:
      this->mark_(G_MEDIA_SIDE);
      break;
    case K_LOCK:
    case K_ALARM:
      this->mark_(G_FLAT | G_META);
      break;
    default:
      break;
  }
}

int NSPanelUI::light_percent_(uint8_t index) const {
  auto v = parse_number<float>(this->state_[K_LIGHT_BRI + index]);
  if (!v.has_value())
    return -1;
  return static_cast<int>(lroundf(*v * 100.0f / 255.0f));
}

float NSPanelUI::num_(uint8_t key, float fallback) const {
  auto v = parse_number<float>(this->state_[key]);
  return v.has_value() ? *v : fallback;
}

float NSPanelUI::target_() const {
  return this->target_pending_ ? this->target_local_ : this->num_(K_CLIMATE_TARGET, 20.0f);
}

std::string NSPanelUI::time_str_() const {
  if (this->time_ == nullptr)
    return "--:--";
  auto now = this->time_->now();
  if (!now.is_valid())
    return "--:--";
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", now.hour, now.minute);
  return buf;
}

std::string NSPanelUI::date_str_() const {
  if (this->time_ == nullptr)
    return "";
  auto now = this->time_->now();
  if (!now.is_valid())
    return "";
  char buf[48];
  snprintf(buf, sizeof(buf), "%s, %d. %s", WEEKDAYS[(now.day_of_week + 6) % 7], now.day_of_month,
           MONTHS[(now.month + 11) % 12]);
  return buf;
}

void NSPanelUI::call_(const std::string &action, const std::string &entity, const std::string &key,
                      const std::string &value) {
  ESP_LOGD(TAG, "Action %s %s %s=%s", action.c_str(), entity.c_str(), key.c_str(), value.c_str());
  if (this->action_ && !entity.empty())
    this->action_(action, entity, key, value);
}

// ------------------------------------------------------------------ drawing primitives
void NSPanelUI::pic_(uint8_t pic) { this->display_->send_command_printf("pic 0,0,%u", pic); }

void NSPanelUI::crop_(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t pic) {
  this->display_->send_command_printf("xpic %d,%d,%d,%d,%d,%d,%u", x, y, w, h, x, y, pic);
}

void NSPanelUI::crop_from_(int16_t x, int16_t y, int16_t w, int16_t h, int16_t sx, int16_t sy, uint8_t pic) {
  this->display_->send_command_printf("xpic %d,%d,%d,%d,%d,%d,%u", x, y, w, h, sx, sy, pic);
}

void NSPanelUI::fill_(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (w <= 0 || h <= 0)
    return;
  this->display_->send_command_printf("fill %d,%d,%d,%d,%u", x, y, w, h, color);
}

void NSPanelUI::outline_(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  for (int16_t i = 0; i < 3; i++)
    this->display_->send_command_printf("draw %d,%d,%d,%d,%u", x + i, y + i, x + w - 1 - i, y + h - 1 - i, color);
}

void NSPanelUI::text_(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t font, uint16_t color, uint16_t bg,
                      uint8_t align, const std::string &text) {
  const std::string t = fit(font, clean(text), w);
  this->display_->send_command_printf("xstr %d,%d,%d,%d,%u,%u,%u,%u,1,1,\"%s\"", x, y, w, h, font, color, bg, align,
                                      t.c_str());
}

void NSPanelUI::dim_(int percent) {
  percent = std::max(0, std::min(100, percent));
  this->display_->send_command_printf("dim=%d", percent);
}

// ------------------------------------------------------------------ pages
void NSPanelUI::show_page_(Page page) {
  if (this->hold_ != HOLD_NONE)
    this->hold_ = HOLD_NONE;
  this->flash_active_ = false;
  this->page_ = page;
  ESP_LOGD(TAG, "Page %s", PAGE_NAMES[page]);
  this->render_full_();
}

void NSPanelUI::render_full_() {
  if (!this->ready_ || this->paused_)
    return;
  this->dirty_ = 0;
  this->tile_dirty_ = 0;
  this->last_render_ms_ = millis();
  if (this->modal_) {
    this->draw_modal_();
    return;
  }
  switch (this->page_) {
    case PAGE_HOME:
      this->pic_(PIC_HOME_BASE);
      this->draw_home_clock_();
      this->draw_home_weather_();
      this->draw_home_bath_();
      this->draw_home_chips_();
      break;
    case PAGE_LICHT:
    case PAGE_LICHT2:
      this->pic_(this->page_ == PAGE_LICHT ? PIC_LICHT_OFF : PIC_LICHT2_OFF);
      this->draw_meta_();
      for (uint8_t i = 0; i < 6; i++)
        this->draw_tile_(i);
      break;
    case PAGE_SZENEN:
      this->pic_(PIC_SZENEN);
      this->draw_meta_();
      break;
    case PAGE_BAD:
      this->pic_(PIC_BAD_BASE);
      this->draw_meta_();
      this->draw_thermo_();
      this->draw_bad_windows_();
      this->draw_presets_();
      break;
    case PAGE_MEDIEN:
      this->pic_(PIC_MEDIEN_BASE);
      this->draw_meta_();
      this->draw_media_info_();
      this->draw_media_play_();
      this->draw_media_volume_();
      this->draw_media_side_();
      break;
    case PAGE_WOHNUNG:
      this->pic_(PIC_WOHNUNG_A);
      this->draw_flat_();
      break;
    case PAGE_SCREENSAVER:
      this->fill_(0, 0, 480, 320, C_BLACK);
      this->draw_screensaver_();
      break;
    default:
      break;
  }
}

void NSPanelUI::render_dirty_() {
  if (!this->ready_ || this->paused_)
    return;
  const uint32_t d = this->dirty_;
  const uint16_t tiles = this->tile_dirty_;
  this->dirty_ = 0;
  this->tile_dirty_ = 0;
  if (this->modal_)
    return;
  switch (this->page_) {
    case PAGE_HOME:
      if (d & G_CLOCK)
        this->draw_home_clock_();
      if (d & G_WX)
        this->draw_home_weather_();
      if (d & G_BATH)
        this->draw_home_bath_();
      if (d & G_CHIPS)
        this->draw_home_chips_();
      break;
    case PAGE_LICHT:
    case PAGE_LICHT2: {
      if (d & G_META)
        this->draw_meta_();
      const uint8_t offset = this->page_ == PAGE_LICHT ? 0 : 6;
      for (uint8_t i = 0; i < 6; i++)
        if (tiles & (1u << (offset + i)))
          this->draw_tile_(i);
      break;
    }
    case PAGE_SZENEN:
      if (d & G_META)
        this->draw_meta_();
      break;
    case PAGE_BAD:
      if (d & G_META)
        this->draw_meta_();
      if (d & G_THERMO)
        this->draw_thermo_();
      if (d & G_BAD_WIN)
        this->draw_bad_windows_();
      if (d & G_PRESETS)
        this->draw_presets_();
      break;
    case PAGE_MEDIEN:
      if (d & G_META)
        this->draw_meta_();
      if (d & G_MEDIA_INFO)
        this->draw_media_info_();
      if (d & G_MEDIA_PLAY)
        this->draw_media_play_();
      if (d & G_MEDIA_VOL)
        this->draw_media_volume_();
      if (d & G_MEDIA_SIDE)
        this->draw_media_side_();
      break;
    case PAGE_WOHNUNG:
      if (d & (G_FLAT | G_META))
        this->draw_flat_();
      break;
    case PAGE_SCREENSAVER:
      if (d & (G_SS | G_CLOCK | G_META))
        this->draw_screensaver_();
      break;
    default:
      break;
  }
}

void NSPanelUI::draw_meta_() {
  const std::string time = this->time_str_();
  std::string s;
  const TextSpec *spec = nullptr;
  switch (this->page_) {
    case PAGE_LICHT:
    case PAGE_LICHT2: {
      int on = 0;
      for (uint8_t i = 0; i < LIGHT_COUNT; i++)
        on += this->light_on_(i) ? 1 : 0;
      s = (on == 0 ? std::string("Alle aus") : std::to_string(on) + " an") + DOT + time;
      spec = this->page_ == PAGE_LICHT ? &T_LICHT_TOP_META : &T_LICHT2_TOP_META;
      break;
    }
    case PAGE_SZENEN:
      s = time;
      spec = &T_SZENEN_TOP_META;
      break;
    case PAGE_BAD:
      s = std::string(this->any_window_open_() ? "Fenster offen" : "Fenster zu") + DOT + time;
      spec = &T_BAD_TOP_META;
      break;
    case PAGE_MEDIEN:
      s = std::string(usable(this->st_(K_MEDIA_STATE)) && this->st_(K_MEDIA_STATE) != "off" ? "LG OLED" : "TV aus") +
          DOT + time;
      spec = &T_MEDIEN_TOP_META;
      break;
    case PAGE_WOHNUNG: {
      const bool alert = this->any_window_open_() || this->st_(K_ALARM) == "triggered" || this->st_(K_LOCK) == "jammed";
      s = std::string(alert ? "Achtung" : "Alles ok") + DOT + time;
      spec = &T_WOHNUNG_TOP_META;
      break;
    }
    default:
      return;
  }
  this->text_(spec->r.x, spec->r.y, spec->r.w, spec->r.h, spec->font, spec->color, spec->bg, spec->align, s);
}

#define TEXT(spec, str) this->text_((spec).r.x, (spec).r.y, (spec).r.w, (spec).r.h, (spec).font, (spec).color, (spec).bg, (spec).align, (str))
#define TEXTC(spec, col, str) \
  this->text_((spec).r.x, (spec).r.y, (spec).r.w, (spec).r.h, (spec).font, (col), (spec).bg, (spec).align, (str))

void NSPanelUI::draw_home_clock_() {
  TEXT(T_HOME_HOME_CLOCK, this->time_str_());
  TEXT(T_HOME_HOME_DATE, this->date_str_());
}

void NSPanelUI::draw_home_weather_() {
  const auto &w = weather_def(this->st_(K_WX_STATE));
  const Rect &r = R_HOME_HOME_WX_ICON;
  this->crop_from_(r.x, r.y, WX_SIZE, WX_SIZE, (w.icon % WX_COLS) * WX_SIZE, (w.icon / WX_COLS) * WX_SIZE,
                   PIC_WX_SPRITE);
  const std::string temp = usable(this->st_(K_WX_TEMP)) ? fmt0(this->num_(K_WX_TEMP, 0)) + DEG : "-";
  TEXT(T_HOME_HOME_WX_TEMP, temp);
  std::string cond = w.text;
  if (usable(this->st_(K_WX_FEELS)))
    cond += std::string(DOT) + "gef\xC3\xBChlt " + fmt0(this->num_(K_WX_FEELS, 0)) + DEG;
  TEXT(T_HOME_HOME_WX_COND, cond);
}

void NSPanelUI::draw_home_bath_() {
  std::string status;
  uint16_t color;
  if (this->any_window_open_() || this->st_(K_CLIMATE_WINDOW) == "True") {
    status = "Fenster offen";
    color = C_RED;
  } else if (this->climate_off_()) {
    status = "aus";
    color = C_TEXT3;
  } else if (this->st_(K_CLIMATE_ACTION) == "heating") {
    status = BULLET " heizt";
    color = C_HEAT;
  } else {
    status = "bereit";
    color = C_TEXT2;
  }
  TEXTC(T_HOME_HOME_BATH_STATUS, color, status);
  TEXT(T_HOME_HOME_BATH_TEMP,
       usable(this->st_(K_CLIMATE_CURRENT)) ? fmt1(this->num_(K_CLIMATE_CURRENT, 0)) + DEG : "-");
  TEXT(T_HOME_HOME_BATH_HUM, usable(this->st_(K_CLIMATE_HUMIDITY))
                                 ? "Feuchte " + fmt0(this->num_(K_CLIMATE_HUMIDITY, 0)) + " %"
                                 : std::string("Feuchte -"));
  TEXT(T_HOME_HOME_BATH_SET, "Soll " + fmt1(this->target_()) + DEG);
}

void NSPanelUI::draw_home_chips_() {
  const Rect &a = R_HOME_HOME_CHIP_TOP;
  const Rect &b = R_HOME_HOME_CHIP_BOTTOM;
  this->crop_(a.x, a.y, a.w, a.h, this->window_open_(K_WIN_TOP) ? PIC_HOME_ALT : PIC_HOME_BASE);
  this->crop_(b.x, b.y, b.w, b.h, this->window_open_(K_WIN_BOTTOM) ? PIC_HOME_ALT : PIC_HOME_BASE);
}

void NSPanelUI::draw_tile_(uint8_t slot) {
  const bool second = this->page_ == PAGE_LICHT2;
  const uint8_t index = slot + (second ? 6 : 0);
  const bool on = this->light_on_(index);
  const std::string &st = this->st_(K_LIGHT + index);
  const Rect &r = TILE_RECTS[slot];
  this->crop_(r.x, r.y, r.w, r.h,
              on ? (second ? PIC_LICHT2_ON : PIC_LICHT_ON) : (second ? PIC_LICHT2_OFF : PIC_LICHT_OFF));
  const TextSpec &t = TILE_TEXTS[slot];
  std::string label;
  if (on) {
    const int pct = this->light_percent_(index);
    label = pct >= 0 ? std::to_string(pct) + " %" : std::string("An");
  } else if (st == "off") {
    label = "Aus";
  } else if (st == "unavailable") {
    label = "Nicht erreichbar";
  } else {
    label = "-";
  }
  this->text_(t.r.x, t.r.y, t.r.w, t.r.h, t.font, on ? C_AMBER : C_TEXT3, on ? C_AMBER_TILE : C_CARD, t.align, label);
  if (on) {
    const FillSpec &f = TILE_BARS[slot];
    const int pct = this->light_percent_(index);
    const int w = pct >= 0 ? f.r.w * pct / 100 : f.r.w;
    this->fill_(f.r.x, f.r.y, std::max(w, 2), f.r.h, f.color);
  }
}

void NSPanelUI::draw_thermo_() {
  const bool off = this->climate_off_();
  const float target = this->target_();
  uint8_t pic = PIC_RING_OFF;
  if (!off) {
    int idx = static_cast<int>(lroundf((target - RING_MIN) / RING_STEP));
    idx = std::max(0, std::min(static_cast<int>(PIC_RING_COUNT) - 1, idx));
    pic = PIC_RING_FIRST + idx;
  }
  this->display_->send_command_printf("pic %d,%d,%u", RING_RECT.x, RING_RECT.y, pic);

  std::string mode;
  uint16_t color;
  if (this->any_window_open_() || this->st_(K_CLIMATE_WINDOW) == "True") {
    mode = "FENSTER OFFEN";
    color = C_RED;
  } else if (off) {
    mode = "AUS";
    color = C_TEXT3;
  } else if (this->st_(K_CLIMATE_ACTION) == "heating") {
    mode = BULLET " HEIZT";
    color = C_HEAT;
  } else {
    mode = "BEREIT";
    color = C_TEXT2;
  }
  TEXTC(T_BAD_BAD_MODE, color, mode);
  TEXTC(T_BAD_BAD_TARGET, off ? C_TEXT3 : C_TEXT, fmt1(target) + DEG);
  std::string cur = usable(this->st_(K_CLIMATE_CURRENT)) ? "Ist " + fmt1(this->num_(K_CLIMATE_CURRENT, 0)) + DEG
                                                         : std::string("Ist -");
  if (usable(this->st_(K_CLIMATE_HUMIDITY)))
    cur += DOT + fmt0(this->num_(K_CLIMATE_HUMIDITY, 0)) + " %";
  TEXT(T_BAD_BAD_CURRENT, cur);
}

void NSPanelUI::draw_bad_windows_() {
  const bool top = this->window_open_(K_WIN_TOP);
  const bool bottom = this->window_open_(K_WIN_BOTTOM);
  TEXTC(T_BAD_BAD_WIN_TOP, top ? C_RED : C_GREEN, top ? "offen" : "zu");
  TEXTC(T_BAD_BAD_WIN_BOTTOM, bottom ? C_RED : C_GREEN, bottom ? "offen" : "zu");
}

void NSPanelUI::draw_presets_() {
  const std::string &preset = this->st_(K_CLIMATE_PRESET);
  const bool off = this->climate_off_();
  for (const auto &p : PRESETS) {
    const bool active = p.preset[0] == '\0' ? off : (!off && preset == p.preset);
    this->crop_(p.region.x, p.region.y, p.region.w, p.region.h, active ? PIC_BAD_ON : PIC_BAD_BASE);
  }
}

void NSPanelUI::draw_media_info_() {
  const std::string &state = this->st_(K_MEDIA_STATE);
  const bool on = usable(state) && state != "off";
  std::string source, title, artist;
  if (!on) {
    source = "AUS";
    title = state == "unavailable" ? "Fernseher nicht erreichbar" : "Fernseher ist aus";
  } else {
    source = upper_ascii(usable(this->st_(K_MEDIA_APP)) ? this->st_(K_MEDIA_APP) : this->st_(K_MEDIA_SOURCE));
    title = usable(this->st_(K_MEDIA_TITLE)) ? this->st_(K_MEDIA_TITLE) : std::string("Kein Titel");
    artist = usable(this->st_(K_MEDIA_ARTIST)) ? this->st_(K_MEDIA_ARTIST) : std::string("");
  }
  std::string l1, l2;
  wrap2(T_MEDIEN_MEDIA_TITLE1.font, clean(title), T_MEDIEN_MEDIA_TITLE1.r.w, l1, l2);
  TEXT(T_MEDIEN_MEDIA_SOURCE, source);
  TEXT(T_MEDIEN_MEDIA_TITLE1, l1);
  TEXT(T_MEDIEN_MEDIA_TITLE2, l2);
  TEXT(T_MEDIEN_MEDIA_ARTIST, artist);
}

void NSPanelUI::draw_media_play_() {
  const Rect &r = R_MEDIEN_MEDIA_PLAY;
  this->crop_(r.x, r.y, r.w, r.h, this->st_(K_MEDIA_STATE) == "playing" ? PIC_MEDIEN_ON : PIC_MEDIEN_BASE);
}

void NSPanelUI::draw_media_volume_() {
  const FillSpec &f = F_MEDIEN_MEDIA_VOLUME;
  const float level = std::max(0.0f, std::min(1.0f, this->num_(K_MEDIA_VOLUME, 0)));
  const int w = static_cast<int>(lroundf(f.r.w * level));
  this->fill_(f.r.x, f.r.y, f.r.w, f.r.h, f.bg);
  this->fill_(f.r.x, f.r.y, w, f.r.h, f.color);
  TEXT(T_MEDIEN_MEDIA_VOLUME, usable(this->st_(K_MEDIA_VOLUME)) ? fmt0(level * 100) : std::string(""));
}

void NSPanelUI::draw_media_side_() {
  const std::string &state = this->st_(K_MEDIA_STATE);
  const std::string &out = this->st_(K_MEDIA_OUTPUT);
  const bool on = usable(state) && state != "off";
  const bool denon = on && out == "external_arc";
  const bool tv = on && out.rfind("tv_speaker", 0) == 0;
  const Rect *rects[4] = {&R_MEDIEN_MEDIA_TV_ON, &R_MEDIEN_MEDIA_SCREEN_OFF, &R_MEDIEN_MEDIA_OUT_DENON,
                          &R_MEDIEN_MEDIA_OUT_TV};
  const bool active[4] = {on, false, denon, tv};
  for (uint8_t i = 0; i < 4; i++)
    this->crop_(rects[i]->x, rects[i]->y, rects[i]->w, rects[i]->h, active[i] ? PIC_MEDIEN_ON : PIC_MEDIEN_BASE);
}

void NSPanelUI::draw_flat_() {
  this->draw_meta_();
  const std::string &lock = this->st_(K_LOCK);
  const std::string &alarm = this->st_(K_ALARM);
  const Rect &rl = R_WOHNUNG_FLAT_LOCK;
  const Rect &ra = R_WOHNUNG_FLAT_ALARM;
  const Rect &rw = R_WOHNUNG_FLAT_WINDOW;
  const uint8_t lock_pic = lock == "unlocked" || lock == "open" ? PIC_WOHNUNG_A
                           : lock == "locked"                    ? PIC_WOHNUNG_B
                                                                 : PIC_WOHNUNG_C;
  uint8_t alarm_pic = PIC_WOHNUNG_A;
  if (alarm == "triggered")
    alarm_pic = PIC_WOHNUNG_C;
  else if (alarm.rfind("armed", 0) == 0 || alarm == "arming" || alarm == "pending")
    alarm_pic = PIC_WOHNUNG_B;
  this->crop_(rl.x, rl.y, rl.w, rl.h, lock_pic);
  this->crop_(ra.x, ra.y, ra.w, ra.h, alarm_pic);
  this->crop_(rw.x, rw.y, rw.w, rw.h, this->any_window_open_() ? PIC_WOHNUNG_B : PIC_WOHNUNG_A);
  this->draw_tv_();
}

void NSPanelUI::draw_tv_() {
  // highlighted (variant b) = muted / currently selected source
  const bool on = usable(this->st_(K_MEDIA_STATE)) && this->st_(K_MEDIA_STATE) != "off";
  const bool muted = on && this->st_(K_MEDIA_MUTED) == "True";
  const Rect buttons[6] = {R_WOHNUNG_TV_VOL_DOWN, R_WOHNUNG_TV_MUTE,  R_WOHNUNG_TV_VOL_UP,
                           R_WOHNUNG_TV_SRC_1,    R_WOHNUNG_TV_SRC_2, R_WOHNUNG_TV_SRC_3};
  for (uint8_t i = 0; i < 6; i++) {
    bool active = false;
    if (i == 1)
      active = muted;
    else if (i >= 3)
      active = on && !this->tv_sources_[i - 3].empty() && this->st_(K_MEDIA_SOURCE) == this->tv_sources_[i - 3];
    const Rect &r = buttons[i];
    this->crop_(r.x, r.y, r.w, r.h, active ? PIC_WOHNUNG_B : PIC_WOHNUNG_A);
  }
}

void NSPanelUI::draw_screensaver_() {
  const std::string time = this->time_str_();
  TEXT(T_SCREENSAVER_SS_CLOCK, time);
  std::string date = this->date_str_();
  if (usable(this->st_(K_WX_TEMP)))
    date += DOT + fmt0(this->num_(K_WX_TEMP, 0)) + DEG + " " + weather_def(this->st_(K_WX_STATE)).text;
  TEXT(T_SCREENSAVER_SS_DATE, date);
  TEXT(T_SCREENSAVER_SS_NOTE, this->ss_note_);
  std::string bottom = "Bad ";
  bottom += usable(this->st_(K_CLIMATE_CURRENT)) ? fmt1(this->num_(K_CLIMATE_CURRENT, 0)) + DEG : "-";
  if (usable(this->st_(K_CLIMATE_HUMIDITY)))
    bottom += DOT + fmt0(this->num_(K_CLIMATE_HUMIDITY, 0)) + " %";
  bottom += DOT;
  bottom += this->any_window_open_() ? "Fenster offen" : "Fenster zu";
  TEXTC(T_SCREENSAVER_SS_BOTTOM, this->any_window_open_() ? C_RED : T_SCREENSAVER_SS_BOTTOM.color, bottom);
}

void NSPanelUI::draw_modal_() {
  const bool two = !this->modal_btn1_.empty() && !this->modal_btn2_.empty();
  this->pic_(two ? PIC_MODAL_TWO : PIC_MODAL_ONE);
  TEXT(T_MODAL_MODAL_TITLE, this->modal_title_);
  TEXT(T_MODAL_MODAL_LINE1, this->modal_line1_);
  TEXT(T_MODAL_MODAL_LINE2, this->modal_line2_);
  if (two) {
    TEXT(T_MODAL_MODAL_BTN1, this->modal_btn1_);
    TEXT(T_MODAL_MODAL_BTN2, this->modal_btn2_);
  } else {
    TEXT(T_MODAL_SINGLE, this->modal_btn1_.empty() ? this->modal_btn2_ : this->modal_btn1_);
  }
}

// ------------------------------------------------------------------ touch
void NSPanelUI::activity_() {
  this->last_activity_ms_ = millis();
}

void NSPanelUI::on_touch(uint8_t half_x, uint8_t half_y, bool pressed) {
  if (!this->ready_ || this->paused_)
    return;
  // With "Send Component ID" enabled on the Nextion page, the display sends its own touch event for
  // the page (page 0, component 0) in addition to the coordinate event from the page code. Drop it.
  if (half_x == 0 && half_y == 0)
    return;
  const int16_t x = half_x * 2;
  const int16_t y = half_y * 2;
  const uint32_t now = millis();
  // Safety net against a repeated report of the same event.
  if (pressed == this->last_touch_pressed_ && now - this->last_touch_ms_ < TOUCH_REPEAT_MS) {
    ESP_LOGV(TAG, "Touch repeat ignored (%d,%d %s)", x, y, pressed ? "press" : "release");
    return;
  }
  this->last_touch_pressed_ = pressed;
  this->last_touch_ms_ = now;
  ESP_LOGD(TAG, "Touch %s at %d,%d", pressed ? "press" : "release", x, y);
  this->activity_();

  if (pressed) {
    if (this->page_ == PAGE_SCREENSAVER && !this->modal_) {
      this->swallow_ = true;  // the first touch only wakes the panel
      this->wake();
      return;
    }
    this->swallow_ = false;
    this->pressing_ = true;
    this->press_x_ = x;
    this->press_y_ = y;
    this->press_ms_ = now;
    if (!this->modal_)
      this->start_hold_(x, y);
    return;
  }

  // release
  this->pressing_ = false;
  if (this->swallow_) {
    this->swallow_ = false;
    return;
  }
  if (this->hold_ != HOLD_NONE) {
    if (!this->hold_done_)
      this->cancel_hold_();
    this->hold_ = HOLD_NONE;
    this->mark_(G_FLAT);
    return;
  }
  const int16_t dx = x - this->press_x_;
  const int16_t dy = y - this->press_y_;
  if (!this->modal_ && x > 0 && std::abs(dx) >= SWIPE_MIN_DX && std::abs(dy) <= SWIPE_MAX_DY) {
    this->swipe_(dx < 0 ? 1 : -1);
    return;
  }
  this->tap_(this->press_x_, this->press_y_);
}

bool NSPanelUI::start_hold_(int16_t x, int16_t y) {
  if (this->page_ != PAGE_WOHNUNG)
    return false;
  Hold h = HOLD_NONE;
  if (H_WOHNUNG_FLAT_LOCK.contains(x, y) && this->st_(K_LOCK) == "locked") {
    h = HOLD_UNLOCK;
  } else if (H_WOHNUNG_FLAT_ALARM.contains(x, y) && this->st_(K_ALARM) == "disarmed") {
    h = HOLD_ARM;
  }
  this->hold_ = h;
  this->hold_done_ = false;
  this->hold_draw_ms_ = 0;
  return h != HOLD_NONE;
}

void NSPanelUI::cancel_hold_() { this->draw_flat_(); }

void NSPanelUI::flash_(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t pic, uint32_t ms) {
  if (pic == 255)
    this->outline_(x, y, w, h, C_AMBER);
  else
    this->crop_(x, y, w, h, pic);
  this->flash_active_ = true;
  this->flash_x_ = x;
  this->flash_y_ = y;
  this->flash_w_ = w;
  this->flash_h_ = h;
  this->flash_pic_ = pic;
  this->flash_until_ms_ = millis() + ms;
}

void NSPanelUI::swipe_(int8_t direction) {
  static constexpr Page LIGHT_PAGES[3] = {PAGE_LICHT, PAGE_LICHT2, PAGE_SZENEN};
  static constexpr Page MAIN_PAGES[5] = {PAGE_HOME, PAGE_LICHT, PAGE_BAD, PAGE_MEDIEN, PAGE_WOHNUNG};
  for (uint8_t i = 0; i < 3; i++) {
    if (this->page_ == LIGHT_PAGES[i]) {
      const int next = static_cast<int>(i) + direction;
      if (next >= 0 && next < 3) {
        this->show_page_(LIGHT_PAGES[next]);
        return;
      }
      // leaving the light group continues with the main tabs
      this->show_page_(direction > 0 ? PAGE_BAD : PAGE_HOME);
      return;
    }
  }
  for (uint8_t i = 0; i < 5; i++) {
    if (this->page_ == MAIN_PAGES[i]) {
      const int next = static_cast<int>(i) + direction;
      if (next >= 0 && next < 5)
        this->show_page_(MAIN_PAGES[next]);
      return;
    }
  }
}

void NSPanelUI::tap_(int16_t x, int16_t y) {
  // ---- modal
  if (this->modal_) {
    const bool two = !this->modal_btn1_.empty() && !this->modal_btn2_.empty();
    std::string pressed;
    if (two && H_MODAL_MODAL_BTN1.contains(x, y))
      pressed = this->modal_btn1_;
    else if (two && H_MODAL_MODAL_BTN2.contains(x, y))
      pressed = this->modal_btn2_;
    else if (!two && MODAL_SINGLE_HIT.contains(x, y))
      pressed = this->modal_btn1_.empty() ? this->modal_btn2_ : this->modal_btn1_;
    if (!pressed.empty()) {
      if (this->event_)
        this->event_("notification_button", pressed);
      this->modal_ = false;
      this->render_full_();
    }
    return;
  }

  // ---- navigation bar
  if (H_NAV_HOME.contains(x, y))
    return this->show_page_(PAGE_HOME);
  if (H_NAV_LICHT.contains(x, y))
    return this->show_page_(PAGE_LICHT);
  if (H_NAV_BAD.contains(x, y))
    return this->show_page_(PAGE_BAD);
  if (H_NAV_MEDIEN.contains(x, y))
    return this->show_page_(PAGE_MEDIEN);
  if (H_NAV_WOHNUNG.contains(x, y))
    return this->show_page_(PAGE_WOHNUNG);

  switch (this->page_) {
    case PAGE_HOME: {
      const Rect hits[4] = {H_HOME_HOME_Q1, H_HOME_HOME_Q2, H_HOME_HOME_Q3, H_HOME_HOME_Q4};
      for (uint8_t i = 0; i < 4; i++) {
        if (hits[i].contains(x, y)) {
          const Rect &r = QUICK_RECTS[i];
          this->flash_(r.x, r.y, r.w, r.h, PIC_HOME_ALT, 450);
          if (i < QUICK_COUNT) {
            this->call_("scene.turn_on", this->quick_[i]);
          } else {
            std::string all;
            for (const auto &l : this->lights_) {
              if (l.empty())
                continue;
              if (!all.empty())
                all += ",";
              all += l;
            }
            this->call_("light.turn_off", all);
          }
          return;
        }
      }
      if (H_HOME_HOME_BATH.contains(x, y))
        this->show_page_(PAGE_BAD);
      return;
    }

    case PAGE_LICHT:
    case PAGE_LICHT2: {
      if (PAGER_HIT.contains(x, y))
        return this->swipe_(1);
      const bool second = this->page_ == PAGE_LICHT2;
      for (uint8_t i = 0; i < 6; i++) {
        if (TILE_RECTS[i].contains(x, y)) {
          const uint8_t index = i + (second ? 6 : 0);
          // optimistic: show the new state right away, Home Assistant confirms shortly after
          this->state_[K_LIGHT + index] = this->light_on_(index) ? "off" : "on";
          this->draw_tile_(i);
          this->mark_(G_META);
          this->call_("light.toggle", this->lights_[index]);
          return;
        }
      }
      return;
    }

    case PAGE_SZENEN:
      if (PAGER_HIT.contains(x, y))
        return this->swipe_(1);
      for (uint8_t i = 0; i < SCENE_COUNT; i++) {
        if (SCENE_RECTS[i].contains(x, y)) {
          const Rect &r = SCENE_RECTS[i];
          this->flash_(r.x, r.y, r.w, r.h, 255, 450);
          this->call_("scene.turn_on", this->scenes_[i]);
          return;
        }
      }
      return;

    case PAGE_BAD: {
      if (H_BAD_BAD_MINUS.contains(x, y) || H_BAD_BAD_PLUS.contains(x, y)) {
        const float step = H_BAD_BAD_PLUS.contains(x, y) ? 0.5f : -0.5f;
        this->target_local_ = std::max(5.0f, std::min(30.0f, this->target_() + step));
        this->target_pending_ = true;
        this->target_send_ms_ = millis() + TARGET_SEND_DELAY;
        this->draw_thermo_();
        this->mark_(G_BATH);
        return;
      }
      for (const auto &p : PRESETS) {
        if (!p.region.contains(x, y))
          continue;
        if (p.preset[0] == '\0') {
          const std::string mode = this->climate_off_() ? "heat" : "off";
          this->state_[K_CLIMATE_MODE] = mode;
          this->call_("climate.set_hvac_mode", this->roles_[R_CLIMATE], "hvac_mode", mode);
        } else {
          if (this->climate_off_()) {
            this->state_[K_CLIMATE_MODE] = "heat";
            this->call_("climate.set_hvac_mode", this->roles_[R_CLIMATE], "hvac_mode", "heat");
          }
          const std::string preset = this->st_(K_CLIMATE_PRESET) == p.preset ? "none" : p.preset;
          this->state_[K_CLIMATE_PRESET] = preset;
          this->call_("climate.set_preset_mode", this->roles_[R_CLIMATE], "preset_mode", preset);
        }
        this->draw_presets_();
        this->draw_thermo_();
        return;
      }
      return;
    }

    case PAGE_MEDIEN: {
      const std::string &media = this->roles_[R_MEDIA];
      if (H_MEDIEN_MEDIA_PREV.contains(x, y))
        return this->call_("media_player.media_previous_track", media);
      if (H_MEDIEN_MEDIA_NEXT.contains(x, y))
        return this->call_("media_player.media_next_track", media);
      if (H_MEDIEN_MEDIA_PLAY.contains(x, y)) {
        const bool playing = this->st_(K_MEDIA_STATE) == "playing";
        if (usable(this->st_(K_MEDIA_STATE)) && this->st_(K_MEDIA_STATE) != "off")
          this->state_[K_MEDIA_STATE] = playing ? "paused" : "playing";
        this->draw_media_play_();
        return this->call_("media_player.media_play_pause", media);
      }
      if (H_MEDIEN_MEDIA_VOLUME.contains(x, y)) {
        const FillSpec &f = F_MEDIEN_MEDIA_VOLUME;
        const float level = std::max(0.0f, std::min(1.0f, static_cast<float>(x - f.r.x) / f.r.w));
        char buf[8];
        snprintf(buf, sizeof(buf), "%.2f", level);
        this->state_[K_MEDIA_VOLUME] = buf;
        this->draw_media_volume_();
        return this->call_("media_player.volume_set", media, "volume_level", buf);
      }
      struct Side {
        const Rect &hit;
        uint8_t role;
      };
      const Side sides[4] = {{H_MEDIEN_MEDIA_TV_ON, R_TV_ON_SCRIPT},
                             {H_MEDIEN_MEDIA_SCREEN_OFF, R_SCREEN_OFF_SCRIPT},
                             {H_MEDIEN_MEDIA_OUT_DENON, R_OUT_DENON_SCRIPT},
                             {H_MEDIEN_MEDIA_OUT_TV, R_OUT_TV_SCRIPT}};
      for (const auto &s : sides) {
        if (!s.hit.contains(x, y))
          continue;
        this->flash_(s.hit.x, s.hit.y, s.hit.w, s.hit.h, PIC_MEDIEN_ON, 500);
        const std::string &state = this->st_(K_MEDIA_STATE);
        if (s.role == R_TV_ON_SCRIPT && usable(state) && state != "off")
          return this->call_("media_player.turn_off", media);  // "TV an" toggles
        return this->call_("script.turn_on", this->roles_[s.role]);
      }
      return;
    }

    case PAGE_WOHNUNG: {
      if (H_WOHNUNG_FLAT_LOCK.contains(x, y) && this->st_(K_LOCK) != "locked")
        return this->call_("lock.lock", this->roles_[R_LOCK]);
      if (H_WOHNUNG_FLAT_WINDOW.contains(x, y))
        return this->show_page_(PAGE_BAD);
      const std::string &media = this->roles_[R_MEDIA];
      const Rect sources[TV_SOURCE_COUNT] = {H_WOHNUNG_TV_SRC_1, H_WOHNUNG_TV_SRC_2, H_WOHNUNG_TV_SRC_3};
      const Rect *hit = nullptr;
      if (H_WOHNUNG_TV_VOL_DOWN.contains(x, y)) {
        hit = &R_WOHNUNG_TV_VOL_DOWN;
        this->call_("media_player.volume_down", media);
      } else if (H_WOHNUNG_TV_VOL_UP.contains(x, y)) {
        hit = &R_WOHNUNG_TV_VOL_UP;
        this->call_("media_player.volume_up", media);
      } else if (H_WOHNUNG_TV_MUTE.contains(x, y)) {
        const bool muted = this->st_(K_MEDIA_MUTED) == "True";
        this->state_[K_MEDIA_MUTED] = muted ? "False" : "True";
        this->call_("media_player.volume_mute", media, "is_volume_muted", muted ? "false" : "true");
        this->draw_tv_();
        return;
      } else {
        for (uint8_t i = 0; i < TV_SOURCE_COUNT; i++) {
          if (sources[i].contains(x, y) && !this->tv_sources_[i].empty()) {
            this->state_[K_MEDIA_SOURCE] = this->tv_sources_[i];
            this->call_("media_player.select_source", media, "source", this->tv_sources_[i]);
            this->draw_tv_();
            return;
          }
        }
      }
      if (hit != nullptr)
        this->flash_(hit->x, hit->y, hit->w, hit->h, PIC_WOHNUNG_B, 250);
      return;
    }

    default:
      return;
  }
}

// ------------------------------------------------------------------ loop
void NSPanelUI::loop() {
  if (!this->ready_ || this->paused_)
    return;
  const uint32_t now = millis();

  // clock and brightness schedule
  if (this->time_ != nullptr) {
    auto t = this->time_->now();
    if (t.is_valid()) {
      if (t.minute != this->last_minute_) {
        this->last_minute_ = t.minute;
        this->mark_(G_META | G_CLOCK | G_SS);
      }
      if (t.hour != this->last_hour_) {
        if (this->last_hour_ != -1) {
          if (t.hour == 0)
            this->set_screensaver_brightness(0);
          else if (t.hour == 7)
            this->set_screensaver_brightness(10);
        }
        this->last_hour_ = t.hour;
      }
    }
  }

  // hold actions (unlock, arm)
  if (this->pressing_ && this->hold_ != HOLD_NONE && !this->hold_done_) {
    const float f = static_cast<float>(now - this->press_ms_) / HOLD_MS;
    if (f >= 1.0f) {
      this->hold_done_ = true;
      switch (this->hold_) {
        case HOLD_UNLOCK:
          this->call_("lock.unlock", this->roles_[R_LOCK]);
          break;
        case HOLD_ARM:
          this->call_("alarm_control_panel.alarm_arm_away", this->roles_[R_ALARM]);
          break;
        default:
          break;
      }
      this->draw_flat_();
    } else if (now - this->hold_draw_ms_ >= 60) {
      this->hold_draw_ms_ = now;
      Rect r = LOCK_PROGRESS;
      uint16_t color = C_GREEN;
      if (this->hold_ == HOLD_ARM) {
        r = F_WOHNUNG_FLAT_ALARM_PROGRESS.r;
        color = F_WOHNUNG_FLAT_ALARM_PROGRESS.color;
      }
      this->fill_(r.x, r.y, static_cast<int16_t>(r.w * f), r.h, color);
    }
  }
  // end of a tap highlight
  if (this->flash_active_ && now > this->flash_until_ms_) {
    this->flash_active_ = false;
    if (!this->modal_) {
      if (this->page_ == PAGE_HOME) {
        this->crop_(this->flash_x_, this->flash_y_, this->flash_w_, this->flash_h_, PIC_HOME_BASE);
      } else if (this->page_ == PAGE_SZENEN) {
        this->crop_(this->flash_x_, this->flash_y_, this->flash_w_, this->flash_h_, PIC_SZENEN);
      } else if (this->page_ == PAGE_MEDIEN) {
        this->mark_(G_MEDIA_SIDE);
      } else if (this->page_ == PAGE_WOHNUNG) {
        this->draw_tv_();
      }
    }
  }

  // thermostat: send collected taps
  if (this->target_pending_ && this->target_send_ms_ != 0 && now >= this->target_send_ms_) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%.1f", this->target_local_);
    this->call_("climate.set_temperature", this->roles_[R_CLIMATE], "temperature", buf);
    this->target_send_ms_ = 0;
    this->target_sent_ms_ = now;
  }
  if (this->target_pending_ && this->target_send_ms_ == 0 && now - this->target_sent_ms_ > TARGET_HOLD_MS) {
    this->target_pending_ = false;
    this->mark_(G_THERMO | G_BATH);
  }

  // modal timeout
  if (this->modal_ && this->modal_until_ms_ != 0 && now > this->modal_until_ms_) {
    this->modal_ = false;
    this->render_full_();
  }

  // screensaver note expiry
  if (!this->ss_note_.empty() && this->ss_note_until_ms_ != 0 && now > this->ss_note_until_ms_) {
    this->ss_note_.clear();
    this->mark_(G_SS);
  }

  // idle → screensaver
  if (this->screensaver_enabled_ && !this->modal_ && !this->pressing_ && this->page_ != PAGE_SCREENSAVER &&
      now - this->last_activity_ms_ > this->screensaver_timeout_ms_) {
    this->show_screensaver();
  }

  if ((this->dirty_ != 0 || this->tile_dirty_ != 0) && now - this->last_render_ms_ >= 40) {
    this->last_render_ms_ = now;
    this->render_dirty_();
  }
}

// ------------------------------------------------------------------ services
void NSPanelUI::wake() {
  this->activity_();
  if (!this->ready_)
    return;
  this->dim_(this->active_brightness_);
  this->ss_note_.clear();
  if (this->page_ == PAGE_SCREENSAVER)
    this->show_page_(PAGE_HOME);
}

void NSPanelUI::show_screensaver() {
  if (!this->ready_)
    return;
  this->modal_ = false;
  this->dim_(this->screensaver_brightness_);
  this->show_page_(PAGE_SCREENSAVER);
}

void NSPanelUI::set_screensaver_enabled(bool enabled) {
  this->screensaver_enabled_ = enabled;
  this->activity_();
}

void NSPanelUI::set_screensaver_brightness(int percent) {
  this->screensaver_brightness_ = std::max(0, std::min(100, percent));
  if (this->ready_ && this->page_ == PAGE_SCREENSAVER)
    this->dim_(this->screensaver_brightness_);
}

void NSPanelUI::set_active_brightness(int percent) {
  this->active_brightness_ = std::max(5, std::min(100, percent));
  if (this->ready_ && this->page_ != PAGE_SCREENSAVER)
    this->dim_(this->active_brightness_);
}

void NSPanelUI::navigate(const std::string &page) {
  for (uint8_t i = 0; i < PAGE_COUNT; i++) {
    if (page == PAGE_NAMES[i]) {
      if (i == PAGE_SCREENSAVER) {
        this->show_screensaver();
      } else {
        this->activity_();
        this->dim_(this->active_brightness_);
        this->show_page_(static_cast<Page>(i));
      }
      return;
    }
  }
  ESP_LOGW(TAG, "Unknown page '%s'", page.c_str());
}

void NSPanelUI::notify_fullscreen(const std::string &title, const std::string &text, const std::string &button1,
                                  const std::string &button2, uint32_t seconds) {
  this->modal_title_ = clean(title);
  wrap2(T_MODAL_MODAL_LINE1.font, clean(text), T_MODAL_MODAL_LINE1.r.w, this->modal_line1_, this->modal_line2_);
  this->modal_btn1_ = clean(button1);
  this->modal_btn2_ = clean(button2);
  if (this->modal_btn1_.empty() && this->modal_btn2_.empty())
    this->modal_btn2_ = "OK";
  this->modal_until_ms_ = seconds == 0 ? 0 : millis() + seconds * 1000;
  this->modal_ = true;
  this->activity_();
  if (this->page_ == PAGE_SCREENSAVER)
    this->page_ = PAGE_HOME;
  if (this->ready_) {
    this->dim_(this->active_brightness_);
    this->render_full_();
  }
}

void NSPanelUI::notify_screensaver(const std::string &line1, const std::string &line2) {
  this->ss_note_ = clean(line2.empty() || line2 == "." ? line1 : line1 + DOT + line2);
  this->ss_note_until_ms_ = millis() + 10 * 60 * 1000;
  this->mark_(G_SS);
}

}  // namespace esphome::nspanel_ui
