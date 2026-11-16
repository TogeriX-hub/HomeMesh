#include "HomeMeshUI.h"

#include <stdio.h>

LV_FONT_DECLARE(hm_font_16)
LV_FONT_DECLARE(hm_font_20)
LV_FONT_DECLARE(hm_font_24)
LV_FONT_DECLARE(hm_font_30)
LV_FONT_DECLARE(hm_font_40)
LV_FONT_DECLARE(hm_bold_30)
LV_FONT_DECLARE(hm_bold_40)
LV_FONT_DECLARE(hm_clock_80)
LV_FONT_DECLARE(hm_clock_100)
LV_FONT_DECLARE(hm_clock_130)

namespace homemesh {

// Font Awesome glyphs that LVGL has no LV_SYMBOL_* name for
#define HM_SYM_STAR      "\xEF\x80\x85"
#define HM_SYM_PIN       "\xEF\x82\x8D"
#define HM_SYM_CALENDAR  "\xEF\x84\xB3"
#define HM_SYM_REPLY     "\xEF\x8F\xA5"
#define HM_SYM_SIGNAL    "\xEF\x80\x92"
#define HM_SYM_HASH      "\xEF\x8A\x92"
#define HM_SYM_CHECK2    "\xEF\x95\xA0"

static const uint32_t NEWMSG_TIMEOUT_MS = 60UL * 60 * 1000;  // full screen closes after 1 h
static const uint32_t NIGHT_IDLE_MS = 30UL * 1000;           // night: clock only after 30 s

// ---------------------------------------------------------------- theme

struct Theme {
  lv_color_t bg, surface, line, text, muted, pri, on_pri, accent, accent_bg,
             ok, ok_bg, warn, warn_bg, bad;
};

static Theme day_theme() {
  return Theme{ lv_color_hex(0xFFFFFF), lv_color_hex(0xF3F4F6), lv_color_hex(0xD1D5DB),
                lv_color_hex(0x111827), lv_color_hex(0x6B7280), lv_color_hex(0x111827),
                lv_color_hex(0xFFFFFF), lv_color_hex(0x1D4ED8), lv_color_hex(0xDBEAFE),
                lv_color_hex(0x15803D), lv_color_hex(0xDCFCE7), lv_color_hex(0x92400E),
                lv_color_hex(0xFEF3C7), lv_color_hex(0xB91C1C) };
}

static Theme night_theme() {
  return Theme{ lv_color_hex(0x000000), lv_color_hex(0x141414), lv_color_hex(0x2A2A2A),
                lv_color_hex(0xC8C8C8), lv_color_hex(0x7A7A7A), lv_color_hex(0x2A2A2A),
                lv_color_hex(0xE5E5E5), lv_color_hex(0x7AA2F7), lv_color_hex(0x14213D),
                lv_color_hex(0x4ADE80), lv_color_hex(0x0F2A18), lv_color_hex(0xFBBF24),
                lv_color_hex(0x2A1F05), lv_color_hex(0xF87171) };
}

static Theme T;

// Size classes; "large text" moves every class one step up.
struct Fonts { const lv_font_t *small, *body, *button, *big; };
static Fonts F;

static void selectStyle(const Model& m) {
  T = m.night ? night_theme() : day_theme();
  if (m.large_text) F = Fonts{ &hm_font_20, &hm_font_24, &hm_font_30, &hm_bold_40 };
  else              F = Fonts{ &hm_font_16, &hm_font_20, &hm_font_24, &hm_bold_30 };
}

// ---------------------------------------------------------------- actions

enum Action : uint16_t {
  A_NONE, A_BACK, A_HOME, A_OPEN_CONV, A_MESSAGES, A_CHANNELS, A_READ, A_REPLY,
  A_RETRY, A_QUICK, A_SEND, A_CHANNEL_TAB, A_COMPOSE_CHANNEL, A_DISMISS_WARNING, A_WAKE,
};

static HomeMeshUI* s_ui = nullptr;

static void* packAction(Action a, int arg) {
  return (void*)(uintptr_t)(((uint32_t)a << 16) | (uint16_t)arg);
}

static const char* QUICK_REPLIES[] = { "Alles gut", "Ja, passt", "Nein", "Ruf mich an", "Bis später" };
static const int QUICK_REPLY_COUNT = 5;

// ---------------------------------------------------------------- text helpers

static const char* WEEKDAYS_SHORT[7] = { "Mo", "Di", "Mi", "Do", "Fr", "Sa", "So" };
static const char* MONTHS[12] = { "Januar", "Februar", "März", "April", "Mai", "Juni", "Juli",
                                  "August", "September", "Oktober", "November", "Dezember" };

static void formatMsgTime(const Model& m, uint32_t t, char* out, size_t cap) {
  int64_t day = t / 86400;
  int hh = (t / 3600) % 24, mm = (t / 60) % 60;
  int64_t today = daysFromCivil(m.date.year, m.date.month, m.date.day);
  if (day == today) {
    snprintf(out, cap, "%02d:%02d", hh, mm);
  } else if (day == today - 1) {
    snprintf(out, cap, "Gestern %02d:%02d", hh, mm);
  } else {
    CivilDate d = civilFromDays(day);
    snprintf(out, cap, "%02d.%02d. %02d:%02d", d.day, d.month, hh, mm);
  }
}

// ---------------------------------------------------------------- widget helpers

static lv_obj_t* box(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, LV_PCT(100), LV_SIZE_CONTENT);
  return o;
}

static lv_obj_t* column(lv_obj_t* parent, int gap) {
  lv_obj_t* o = box(parent);
  lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(o, gap, 0);
  return o;
}

static lv_obj_t* row(lv_obj_t* parent, int gap) {
  lv_obj_t* o = box(parent);
  lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(o, gap, 0);
  lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return o;
}

static lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  return l;
}

static void grow(lv_obj_t* o) { lv_obj_set_flex_grow(o, 1); }

static lv_obj_t* spacer(lv_obj_t* parent) {
  lv_obj_t* s = box(parent);
  lv_obj_set_height(s, 1);
  grow(s);
  return s;
}

enum BtnStyle { BTN_OUTLINE, BTN_PRIMARY, BTN_OK };

static lv_obj_t* button(lv_obj_t* parent, const char* text, BtnStyle style, Action a, int arg = 0,
                        const lv_font_t* font = nullptr) {
  lv_obj_t* b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(b, 14, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(b, 16, 0);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(b, 8, 0);
  lv_color_t fg;
  if (style == BTN_PRIMARY) {
    lv_obj_set_style_bg_color(b, T.pri, 0);
    fg = T.on_pri;
  } else if (style == BTN_OK) {
    lv_obj_set_style_bg_color(b, T.ok_bg, 0);
    fg = T.ok;
  } else {
    lv_obj_set_style_bg_color(b, T.bg, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, T.line, 0);
    fg = T.text;
  }
  lv_obj_set_style_bg_color(b, T.surface, LV_STATE_PRESSED);
  if (text) label(b, text, font ? font : F.button, fg);
  lv_obj_add_event_cb(b, HomeMeshUI::onClick, LV_EVENT_CLICKED, packAction(a, arg));
  return b;
}

static void badge(lv_obj_t* parent, int n, bool strong) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%d", n);
  lv_obj_t* b = box(parent);
  lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, strong ? T.bad : T.surface, 0);
  lv_obj_set_style_pad_hor(b, 9, 0);
  lv_obj_set_style_pad_ver(b, 2, 0);
  label(b, buf, &hm_font_16, strong ? lv_color_white() : T.muted);
}

static lv_obj_t* page(lv_obj_t* scr) {
  lv_obj_set_style_bg_color(scr, T.bg, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_t* p = lv_obj_create(scr);
  lv_obj_remove_style_all(p);
  lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(p, 20, 0);
  lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(p, 10, 0);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  return p;
}

// "<  Title" bar
static void titleBar(lv_obj_t* parent, const char* title, Action back_action = A_BACK) {
  lv_obj_t* r = row(parent, 4);
  lv_obj_t* back = button(r, LV_SYMBOL_LEFT, BTN_OUTLINE, back_action);
  lv_obj_set_style_border_width(back, 0, 0);
  lv_obj_set_style_pad_hor(back, 6, 0);
  lv_obj_set_height(back, 44);
  label(r, title, F.button, T.text);
}

// A scrolling column that fills the remaining height
static lv_obj_t* scrollArea(lv_obj_t* parent, int gap) {
  lv_obj_t* s = column(parent, gap);
  lv_obj_set_height(s, 1);
  grow(s);
  lv_obj_add_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(s, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s, LV_SCROLLBAR_MODE_OFF);
  return s;
}

// Lists open at their newest entry. The final sizes of wrapped labels are only
// known once the whole screen is laid out, so the scroll happens after build.
static lv_obj_t* s_scroll_to_end = nullptr;
static void scrollToEnd(lv_obj_t* list) { s_scroll_to_end = list; }

// Full-width row that puts a chat bubble on the left (incoming) or right (outgoing)
static lv_obj_t* bubbleHolder(lv_obj_t* list, bool outgoing) {
  lv_obj_t* h = row(list, 0);
  lv_obj_set_flex_align(h, outgoing ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  return h;
}

// One-line strip on the home screen (warning, pinned note, appointment)
static void strip(lv_obj_t* parent, const char* icon, const char* text, lv_color_t fg,
                  lv_color_t bg, bool outlined, Action a = A_NONE) {
  lv_obj_t* s = row(parent, 10);
  lv_obj_set_style_radius(s, 10, 0);
  lv_obj_set_style_pad_hor(s, 12, 0);
  lv_obj_set_style_pad_ver(s, 9, 0);
  if (outlined) {
    lv_obj_set_style_border_width(s, 2, 0);
    lv_obj_set_style_border_color(s, fg, 0);
  } else {
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s, bg, 0);
  }
  label(s, icon, F.body, fg);
  lv_obj_t* l = label(s, text, F.body, fg);
  grow(l);
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_height(l, lv_font_get_line_height(F.body));
  if (a != A_NONE) {
    lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s, HomeMeshUI::onClick, LV_EVENT_CLICKED, packAction(a, 0));
  }
}

// ---------------------------------------------------------------- lifecycle

void HomeMeshUI::begin(Model* model, UiActions* actions) {
  _m = model;
  _act = actions;
  s_ui = this;
  _view = View::Home;
  build();
}

void HomeMeshUI::refresh() { build(); }

void HomeMeshUI::show(View v) {
  _back_n = 0;
  _view = v;
  build();
}

void HomeMeshUI::go(View v) {
  if (_view != v && _back_n < (int)(sizeof(_back) / sizeof(_back[0]))) _back[_back_n++] = _view;
  _view = v;
  build();
}

void HomeMeshUI::back() {
  _view = _back_n > 0 ? _back[--_back_n] : View::Home;
  build();
}

void HomeMeshUI::onFavouriteMessage(uint32_t now_ms) {
  _newmsg_since_ms = now_ms;
  if (_m->night) { build(); return; }   // quiet hours: only the hint on the clock
  _idle = false;
  _back_n = 0;
  _view = View::NewMessages;
  _screen_off = false;
  build();
}

void HomeMeshUI::onButtonShort() { _screen_off = !_screen_off; }

void HomeMeshUI::onButtonLong() {
  _screen_off = false;
  // settings screen follows in the next step
}

void HomeMeshUI::tick(uint32_t now_ms) {
  if (_view == View::NewMessages && now_ms - _newmsg_since_ms > NEWMSG_TIMEOUT_MS) {
    show(View::Home);   // unread counter stays on the favourite button
  }
  if (_m->night && !_idle && _view == View::Home && now_ms - _last_touch_ms > NIGHT_IDLE_MS) {
    _idle = true;
    build();
  }
}

// Rebuilding deletes the current screen, which may own the widget whose
// event is running right now. So the rebuild always runs from LVGL's timer
// loop, never inside an event callback.
void HomeMeshUI::build() {
  if (_build_pending) return;
  _build_pending = true;
  lv_async_call(buildAsync, this);
}

void HomeMeshUI::buildAsync(void* self) {
  HomeMeshUI* ui = (HomeMeshUI*)self;
  ui->_build_pending = false;
  ui->buildNow();
}

void HomeMeshUI::buildNow() {
  // keep typed text when the compose screen is redrawn (e.g. a message arrives)
  if (_view == View::Compose && _textarea) copyText(_draft, sizeof(_draft), lv_textarea_get_text(_textarea));
  selectStyle(*_m);
  _textarea = nullptr;
  lv_obj_t* old = lv_screen_active();
  lv_obj_t* scr = lv_obj_create(nullptr);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  switch (_view) {
    case View::Home:        if (_m->night && _idle) buildNightClock(scr); else buildHome(scr); break;
    case View::NewMessages: buildNewMessages(scr); break;
    case View::Chat:        buildChat(scr); break;
    case View::Compose:     buildCompose(scr); break;
    case View::Messages:    buildMessages(scr); break;
    case View::Channels:    buildChannels(scr); break;
  }
  lv_screen_load(scr);
  if (old) lv_obj_delete(old);
  if (s_scroll_to_end) {
    lv_obj_update_layout(scr);
    int32_t below = lv_obj_get_scroll_bottom(s_scroll_to_end);
    if (below > 0) lv_obj_scroll_by(s_scroll_to_end, 0, -below, LV_ANIM_OFF);
    s_scroll_to_end = nullptr;
  }
}

// ---------------------------------------------------------------- screens

void HomeMeshUI::buildHome(lv_obj_t* scr) {
  lv_obj_t* p = page(scr);

  // date and radio status
  lv_obj_t* top = row(p, 6);
  char date[40];
  if (_m->time_valid) {
    snprintf(date, sizeof(date), "%s, %d. %s", WEEKDAYS_SHORT[weekdayOf(_m->date)],
             _m->date.day, MONTHS[_m->date.month - 1]);
  } else {
    snprintf(date, sizeof(date), "Uhrzeit unbekannt");
  }
  grow(label(top, date, F.small, T.muted));
  label(top, HM_SYM_SIGNAL, F.small, _m->signal_ok ? T.ok : T.bad);
  label(top, _m->signal_ok ? "Funknetz ok" : "Kein Funkempfang", F.small, T.muted);

  // The clock takes whatever space the strips leave and shrinks only when it
  // has to: big when the screen is calm, smaller with warnings and notes.
  int strips = (_m->warning[0] ? 1 : 0) + (_m->pin[0] ? 1 : 0) + (_m->has_appointment ? 1 : 0);
  int nfav_tmp[MAX_FAVOURITES];
  int rows = strips + (_m->favourites(nfav_tmp) > 1 ? 1 : 0) + (_m->large_text ? 1 : 0);
  const lv_font_t* clock_font = rows == 0 ? &hm_clock_130 : rows == 1 ? &hm_clock_100 : &hm_clock_80;

  lv_obj_t* clock_area = column(p, 0);
  lv_obj_set_height(clock_area, 1);
  grow(clock_area);
  lv_obj_set_flex_align(clock_area, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  char clock[8];
  if (_m->time_valid) snprintf(clock, sizeof(clock), "%02d:%02d", _m->hour, _m->minute);
  else snprintf(clock, sizeof(clock), "--:--");
  label(clock_area, clock, clock_font, T.text);

  if (_m->warning[0]) strip(p, LV_SYMBOL_WARNING, _m->warning, T.warn, T.warn_bg, false, A_DISMISS_WARNING);
  if (_m->pin[0]) strip(p, HM_SYM_PIN, _m->pin, T.text, T.surface, false);
  if (_m->has_appointment) {
    char when[40], text[80];
    formatWhen(_m->appointment, _m->date.year, when, sizeof(when));
    snprintf(text, sizeof(text), "%s · %s", _m->appointment.name, when);
    strip(p, HM_SYM_CALENDAR, text, T.accent, T.bg, true);
  }

  // favourites: one big button each
  int favs[MAX_FAVOURITES];
  int nf = _m->favourites(favs);
  for (int i = 0; i < nf; i++) {
    const Conversation& cv = _m->convs[favs[i]];
    lv_obj_t* b = button(p, nullptr, BTN_PRIMARY, A_OPEN_CONV, favs[i]);
    lv_obj_set_size(b, LV_PCT(100), nf == 1 ? 66 : 56);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    char name[48];
    snprintf(name, sizeof(name), HM_SYM_STAR "  %s", cv.displayName());
    label(b, name, F.button, T.on_pri);
    if (cv.log.unread) {
      char n[16];
      snprintf(n, sizeof(n), "%d neu", cv.log.unread);
      lv_obj_t* bd = box(b);
      lv_obj_set_size(bd, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
      lv_obj_set_style_radius(bd, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_opa(bd, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(bd, T.bad, 0);
      lv_obj_set_style_pad_hor(bd, 10, 0);
      lv_obj_set_style_pad_ver(bd, 3, 0);
      label(bd, n, &hm_font_16, lv_color_white());
    } else {
      label(b, "schreiben " LV_SYMBOL_RIGHT, F.small, T.on_pri);
    }
  }

  lv_obj_t* r = row(p, 10);
  lv_obj_t* msgs = button(r, "Nachrichten", BTN_OUTLINE, A_MESSAGES, 0, F.body);
  grow(msgs);
  lv_obj_set_height(msgs, 54);
  int others = _m->unreadFrom(false);
  if (others) badge(msgs, others, true);
  lv_obj_t* ch = button(r, HM_SYM_HASH " Kanäle", BTN_OUTLINE, A_CHANNELS, 0, F.body);
  grow(ch);
  lv_obj_set_height(ch, 54);
}

void HomeMeshUI::buildNightClock(lv_obj_t* scr) {
  lv_obj_set_style_bg_color(scr, T.bg, 0);
  lv_obj_t* p = column(scr, 8);
  lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_align(p, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(p, HomeMeshUI::onClick, LV_EVENT_CLICKED, packAction(A_WAKE, 0));

  char clock[8];
  if (_m->time_valid) snprintf(clock, sizeof(clock), "%02d:%02d", _m->hour, _m->minute);
  else snprintf(clock, sizeof(clock), "--:--");
  label(p, clock, &hm_clock_130, T.muted);

  int favs[MAX_FAVOURITES];
  int nf = _m->favourites(favs);
  int unread = _m->unreadFrom(true);
  if (unread && nf) {
    char hint[64];
    snprintf(hint, sizeof(hint), LV_SYMBOL_ENVELOPE "  Neue Nachricht von %s", _m->convs[favs[0]].displayName());
    label(p, hint, F.body, T.accent);
  }
}

void HomeMeshUI::buildNewMessages(lv_obj_t* scr) {
  lv_obj_t* p = page(scr);

  // collect unread messages from favourites (normally just one person)
  int favs[MAX_FAVOURITES];
  int nf = _m->favourites(favs);
  int conv = -1, unread = 0;
  for (int i = 0; i < nf; i++) {
    if (_m->convs[favs[i]].log.unread) { conv = favs[i]; unread = _m->convs[conv].log.unread; break; }
  }
  if (conv < 0) { _view = View::Home; buildHome(scr); return; }
  _chat_conv = conv;
  const Conversation& cv = _m->convs[conv];

  char head[64];
  if (unread == 1) snprintf(head, sizeof(head), LV_SYMBOL_BELL "  Neue Nachricht von %s", cv.displayName());
  else snprintf(head, sizeof(head), LV_SYMBOL_BELL "  %d neue Nachrichten von %s", unread, cv.displayName());
  label(p, head, F.body, T.accent);

  lv_obj_t* list = scrollArea(p, 14);
  int first = cv.log.count - unread;
  if (first < 0) first = 0;
  for (int i = first; i < cv.log.count; i++) {
    const Message& msg = cv.log.msgs[i];
    if (msg.outgoing) continue;
    lv_obj_t* item = column(list, 2);
    if (unread > 1) {
      lv_obj_set_style_border_side(item, LV_BORDER_SIDE_LEFT, 0);
      lv_obj_set_style_border_width(item, 3, 0);
      lv_obj_set_style_border_color(item, T.accent, 0);
      lv_obj_set_style_pad_left(item, 12, 0);
    }
    char t[24];
    formatMsgTime(*_m, msg.time, t, sizeof(t));
    label(item, t, F.small, T.muted);
    lv_obj_t* l = label(item, msg.text, unread > 1 ? F.button : F.big, T.text);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
  }
  scrollToEnd(list);

  lv_obj_t* r = row(p, 10);
  lv_obj_t* read = button(r, "Gelesen", BTN_OUTLINE, A_READ, conv);
  lv_obj_set_size(read, 1, 64);
  grow(read);
  lv_obj_t* reply = button(r, HM_SYM_REPLY "  Antworten", BTN_PRIMARY, A_REPLY, conv);
  lv_obj_set_size(reply, 1, 64);
  lv_obj_set_flex_grow(reply, 2);
}

void HomeMeshUI::buildChat(lv_obj_t* scr) {
  lv_obj_t* p = page(scr);
  const Conversation& cv = _m->convs[_chat_conv];
  char title[48];
  snprintf(title, sizeof(title), cv.favourite ? HM_SYM_STAR "  %s" : "%s", cv.displayName());
  titleBar(p, title);

  lv_obj_t* list = scrollArea(p, 10);
  for (int i = 0; i < cv.log.count; i++) {
    const Message& msg = cv.log.msgs[i];
    lv_obj_t* b = column(bubbleHolder(list, msg.outgoing), 4);
    lv_obj_set_width(b, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(b, LV_PCT(82), 0);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, msg.outgoing ? T.accent_bg : T.surface, 0);
    lv_obj_set_style_pad_hor(b, 12, 0);
    lv_obj_set_style_pad_ver(b, 9, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, onBubbleLongPress, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)i);

    lv_obj_t* l = label(b, msg.text, F.body, T.text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_max_width(l, 360, 0);
    lv_obj_set_width(l, LV_SIZE_CONTENT);

    lv_obj_t* meta = row(b, 6);
    lv_obj_set_width(meta, LV_SIZE_CONTENT);
    char t[24];
    formatMsgTime(*_m, msg.time, t, sizeof(t));
    label(meta, t, &hm_font_16, T.muted);
    if (msg.outgoing) {
      switch (msg.status) {
        case Delivery::Pending:   label(meta, LV_SYMBOL_OK " unterwegs", &hm_font_16, T.muted); break;
        case Delivery::Delivered: label(meta, HM_SYM_CHECK2 " zugestellt", &hm_font_16, T.ok); break;
        case Delivery::Failed:    label(meta, LV_SYMBOL_CLOSE " nicht zugestellt", &hm_font_16, T.bad); break;
        default: break;
      }
      if (msg.status == Delivery::Failed) {
        lv_obj_t* retry = button(b, "Nochmal senden", BTN_OUTLINE, A_RETRY, i);
        lv_obj_set_height(retry, 40);
        lv_obj_set_style_border_color(retry, T.bad, 0);
      }
    }
  }
  scrollToEnd(list);

  lv_obj_t* reply = button(p, HM_SYM_REPLY "  Antworten", BTN_PRIMARY, A_REPLY, _chat_conv);
  lv_obj_set_size(reply, LV_PCT(100), 56);
}

void HomeMeshUI::buildMessages(lv_obj_t* scr) {
  lv_obj_t* p = page(scr);
  titleBar(p, "Nachrichten");
  lv_obj_t* list = scrollArea(p, 0);

  // favourites first, then the others in contact order
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < _m->conv_count; i++) {
      const Conversation& cv = _m->convs[i];
      if (cv.favourite != (pass == 0)) continue;
      lv_obj_t* r = row(list, 10);
      lv_obj_set_style_pad_ver(r, 12, 0);
      lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
      lv_obj_set_style_border_width(r, 1, 0);
      lv_obj_set_style_border_color(r, T.line, 0);
      lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(r, onClick, LV_EVENT_CLICKED, packAction(A_OPEN_CONV, i));

      lv_obj_t* col = column(r, 2);
      grow(col);
      char name[48];
      snprintf(name, sizeof(name), cv.favourite ? HM_SYM_STAR "  %s" : "%s", cv.displayName());
      label(col, name, F.body, T.text);
      const Message* last = cv.log.last();
      lv_obj_t* prev = label(col, last ? last->text : "Noch keine Nachrichten", F.small, T.muted);
      lv_label_set_long_mode(prev, LV_LABEL_LONG_MODE_DOTS);
      lv_obj_set_width(prev, LV_PCT(100));
      lv_obj_set_height(prev, lv_font_get_line_height(F.small));
      if (cv.log.unread) badge(r, cv.log.unread, true);
    }
  }
  lv_obj_t* note = label(p, "Nachrichten von Kontakten ohne Stern kommen ohne Ton.", &hm_font_16, T.muted);
  lv_obj_set_width(note, LV_PCT(100));
  lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
}

void HomeMeshUI::buildChannels(lv_obj_t* scr) {
  lv_obj_t* p = page(scr);
  titleBar(p, "Kanäle");
  if (_m->channel_count == 0) {
    label(p, "Noch keine Kanäle. Kanäle werden in der MeshCore-App eingerichtet.", F.body, T.muted);
    return;
  }
  if (_channel >= _m->channel_count) _channel = 0;

  lv_obj_t* tabs = row(p, 8);
  lv_obj_add_flag(tabs, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(tabs, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(tabs, LV_SCROLLBAR_MODE_OFF);
  for (int i = 0; i < _m->channel_count; i++) {
    char name[40];
    snprintf(name, sizeof(name), "#%s", _m->channels[i].name);
    lv_obj_t* t = button(tabs, nullptr, i == _channel ? BTN_PRIMARY : BTN_OUTLINE, A_CHANNEL_TAB, i);
    lv_obj_set_style_radius(t, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_height(t, 40);
    label(t, name, F.small, i == _channel ? T.on_pri : T.text);
  }

  const Channel& ch = _m->channels[_channel];
  lv_obj_t* list = scrollArea(p, 10);
  for (int i = 0; i < ch.log.count; i++) {
    const Message& msg = ch.log.msgs[i];
    if (i == _channel_divider && i > 0) {
      lv_obj_t* d = row(list, 8);
      lv_obj_t* l1 = box(d); lv_obj_set_height(l1, 1); grow(l1);
      lv_obj_set_style_bg_opa(l1, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(l1, T.accent, 0);
      label(d, "Neu seit dem letzten Reinschauen", &hm_font_16, T.accent);
      lv_obj_t* l2 = box(d); lv_obj_set_height(l2, 1); grow(l2);
      lv_obj_set_style_bg_opa(l2, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(l2, T.accent, 0);
    }
    lv_obj_t* b = column(bubbleHolder(list, msg.outgoing), 2);
    lv_obj_set_width(b, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(b, LV_PCT(92), 0);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, msg.outgoing ? T.accent_bg : T.surface, 0);
    lv_obj_set_style_pad_hor(b, 12, 0);
    lv_obj_set_style_pad_ver(b, 8, 0);
    char t[24], head[64];
    formatMsgTime(*_m, msg.time, t, sizeof(t));
    if (msg.outgoing) snprintf(head, sizeof(head), "%s · gesendet", t);
    else snprintf(head, sizeof(head), "%s · %s", msg.sender, t);
    label(b, head, &hm_font_16, T.muted);
    lv_obj_t* l = label(b, msg.text, F.body, T.text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_max_width(l, 400, 0);
    lv_obj_set_width(l, LV_SIZE_CONTENT);
  }
  scrollToEnd(list);

  char w[48];
  snprintf(w, sizeof(w), LV_SYMBOL_EDIT "  In #%s schreiben", ch.name);
  lv_obj_t* b = button(p, w, BTN_OUTLINE, A_COMPOSE_CHANNEL, _channel);
  lv_obj_set_size(b, LV_PCT(100), 52);
}

// German keyboard (QWERTZ). The keyboard widget switches maps on "ABC", "abc"
// and "1#" itself; LV_SYMBOL_OK raises LV_EVENT_READY, which sends.
static const char* KB_LOWER[] = {
  "q", "w", "e", "r", "t", "z", "u", "i", "o", "p", "ü", "\n",
  "a", "s", "d", "f", "g", "h", "j", "k", "l", "ö", "ä", "\n",
  "ABC", "y", "x", "c", "v", "b", "n", "m", "ß", LV_SYMBOL_BACKSPACE, "\n",
  "1#", ",", " ", ".", "?", LV_SYMBOL_OK, "" };
static const char* KB_UPPER[] = {
  "Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P", "Ü", "\n",
  "A", "S", "D", "F", "G", "H", "J", "K", "L", "Ö", "Ä", "\n",
  "abc", "Y", "X", "C", "V", "B", "N", "M", "-", LV_SYMBOL_BACKSPACE, "\n",
  "1#", ",", " ", ".", "!", LV_SYMBOL_OK, "" };
static const char* KB_SPECIAL[] = {
  "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
  "-", "/", ":", ";", "(", ")", "€", "&", "@", "\"", "\n",
  "abc", "+", "=", "%", "*", "#", "'", "°", LV_SYMBOL_BACKSPACE, "\n",
  "abc", ",", " ", ".", "?", LV_SYMBOL_OK, "" };

#define KB_W(n) ((lv_buttonmatrix_ctrl_t)(n))
#define KB_CTRL(n) ((lv_buttonmatrix_ctrl_t)(LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG | (n)))
// Relative key widths; the button matrix stores them in 4 bits (1..15).
static const lv_buttonmatrix_ctrl_t KB_CTRL_LETTERS[] = {
  KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3),
  KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3),
  KB_CTRL(5), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(5),
  KB_CTRL(5), KB_W(3), KB_W(14), KB_W(3), KB_W(3), KB_CTRL(5) };
static const lv_buttonmatrix_ctrl_t KB_CTRL_SPECIAL[] = {
  KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3),
  KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3),
  KB_CTRL(5), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(3), KB_W(5),
  KB_CTRL(5), KB_W(3), KB_W(14), KB_W(3), KB_W(3), KB_CTRL(5) };

void HomeMeshUI::buildCompose(lv_obj_t* scr) {
  lv_obj_t* p = page(scr);
  lv_obj_set_style_pad_all(p, 12, 0);
  lv_obj_set_style_pad_row(p, 8, 0);

  char title[56];
  if (_compose_channel >= 0) snprintf(title, sizeof(title), "In #%s", _m->channels[_compose_channel].name);
  else snprintf(title, sizeof(title), "An %s", _m->convs[_chat_conv].displayName());
  titleBar(p, title);

  lv_obj_t* r = row(p, 8);
  _textarea = lv_textarea_create(r);
  grow(_textarea);
  lv_obj_set_height(_textarea, LV_SIZE_CONTENT);
  lv_textarea_set_max_length(_textarea, TEXT_LEN - 1);
  lv_textarea_set_placeholder_text(_textarea, "Nachricht schreiben");
  lv_textarea_set_text(_textarea, _draft);
  lv_obj_set_style_text_font(_textarea, F.body, 0);
  lv_obj_set_style_text_color(_textarea, T.text, 0);
  lv_obj_set_style_bg_color(_textarea, T.bg, 0);
  lv_obj_set_style_border_color(_textarea, T.line, 0);
  lv_obj_set_style_radius(_textarea, 10, 0);
  lv_obj_set_style_max_height(_textarea, 3 * lv_font_get_line_height(F.body) + 20, 0);
  lv_obj_add_state(_textarea, LV_STATE_FOCUSED);   // shows the cursor
  lv_obj_t* send = button(r, "Senden", BTN_PRIMARY, A_SEND);
  lv_obj_set_height(send, 52);

  if (_compose_channel < 0) {
    lv_obj_t* chips = row(p, 8);
    lv_obj_add_flag(chips, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(chips, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(chips, LV_SCROLLBAR_MODE_OFF);
    for (int i = 0; i < QUICK_REPLY_COUNT; i++) {
      lv_obj_t* c = button(chips, nullptr, BTN_OUTLINE, A_QUICK, i);
      lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_height(c, 40);
      label(c, QUICK_REPLIES[i], F.small, T.text);
    }
  }

  spacer(p);

  lv_obj_t* kb = lv_keyboard_create(p);
  lv_obj_set_size(kb, LV_PCT(100), 220);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, KB_LOWER, KB_CTRL_LETTERS);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, KB_UPPER, KB_CTRL_LETTERS);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_SPECIAL, KB_SPECIAL, KB_CTRL_SPECIAL);
  lv_keyboard_set_mode(kb, _draft[0] ? LV_KEYBOARD_MODE_TEXT_LOWER : LV_KEYBOARD_MODE_TEXT_UPPER);
  lv_keyboard_set_textarea(kb, _textarea);
  lv_obj_set_style_text_font(kb, F.body, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb, T.bg, 0);
  lv_obj_set_style_pad_all(kb, 0, 0);
  lv_obj_set_style_pad_gap(kb, 5, 0);
  lv_obj_set_style_bg_color(kb, T.surface, LV_PART_ITEMS);
  lv_obj_set_style_text_color(kb, T.text, LV_PART_ITEMS);
  lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
  lv_obj_set_style_border_color(kb, T.line, LV_PART_ITEMS);
  lv_obj_set_style_radius(kb, 7, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
  lv_obj_add_event_cb(kb, onKeyboard, LV_EVENT_READY, nullptr);
}

// ---------------------------------------------------------------- actions

void HomeMeshUI::openChat(int conv) {
  _chat_conv = conv;
  _m->convs[conv].log.markRead();
  go(View::Chat);
}

void HomeMeshUI::openChannel(int ch) {
  if (ch >= _m->channel_count) ch = 0;
  _channel = ch;
  if (_m->channel_count) {
    MessageLog& log = _m->channels[ch].log;
    _channel_divider = log.seen < log.count ? log.seen : -1;
    log.markRead();
  }
}

void HomeMeshUI::sendDraft() {
  if (_textarea) copyText(_draft, sizeof(_draft), lv_textarea_get_text(_textarea));
  if (!_draft[0]) return;
  if (_compose_channel >= 0) {
    _act->sendChannel(_compose_channel, _draft);
    _draft[0] = 0;
    _view = View::Channels;
  } else {
    _act->sendDirect(_chat_conv, _draft);
    _draft[0] = 0;
    _view = View::Chat;
  }
  if (_back_n > 0) _back_n--;   // compose is not a place to return to
  build();
}

void HomeMeshUI::onClick(lv_event_t* e) {
  HomeMeshUI* ui = s_ui;
  uint32_t packed = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
  Action a = (Action)(packed >> 16);
  int arg = (int)(packed & 0xFFFF);

  switch (a) {
    case A_BACK: ui->back(); break;
    case A_HOME: ui->show(View::Home); break;
    case A_OPEN_CONV: ui->openChat(arg); break;
    case A_MESSAGES: ui->go(View::Messages); break;
    case A_CHANNELS: ui->openChannel(ui->_channel); ui->go(View::Channels); break;
    case A_CHANNEL_TAB: ui->openChannel(arg); ui->build(); break;
    case A_READ: ui->_m->convs[arg].log.markRead(); ui->show(View::Home); break;
    case A_REPLY:
      ui->_m->convs[arg].log.markRead();
      ui->_chat_conv = arg;
      ui->_compose_channel = -1;
      ui->_draft[0] = 0;
      if (ui->_view == View::NewMessages) { ui->_back_n = 0; ui->_view = View::Chat; }
      ui->go(View::Compose);
      break;
    case A_COMPOSE_CHANNEL:
      ui->_compose_channel = arg;
      ui->_draft[0] = 0;
      ui->go(View::Compose);
      break;
    case A_QUICK:
      if (ui->_textarea) lv_textarea_set_text(ui->_textarea, QUICK_REPLIES[arg]);
      break;
    case A_SEND: ui->sendDraft(); break;
    case A_RETRY: ui->_act->retryDirect(ui->_chat_conv, arg); ui->build(); break;
    case A_DISMISS_WARNING: ui->_m->warning[0] = 0; ui->build(); break;
    case A_WAKE: ui->_idle = false; ui->build(); break;
    default: break;
  }
}

void HomeMeshUI::onBubbleLongPress(lv_event_t* e) {
  HomeMeshUI* ui = s_ui;
  int i = (int)(uintptr_t)lv_event_get_user_data(e);
  const MessageLog& log = ui->_m->convs[ui->_chat_conv].log;
  if (i < log.count) copyText(ui->_m->pin, sizeof(ui->_m->pin), log.msgs[i].text);
}

void HomeMeshUI::onKeyboard(lv_event_t* e) {
  (void)e;
  s_ui->sendDraft();
}

}  // namespace homemesh
