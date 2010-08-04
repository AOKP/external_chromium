// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/gtk/gtk_theme_provider.h"

#include <gtk/gtk.h>

#include <set>

#include "app/gtk_signal_registrar.h"
#include "app/resource_bundle.h"
#include "base/env_var.h"
#include "base/stl_util-inl.h"
#include "base/xdg_util.h"
#include "chrome/browser/metrics/user_metrics.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/gtk/cairo_cached_surface.h"
#include "chrome/browser/gtk/hover_controller_gtk.h"
#include "chrome/browser/gtk/gtk_chrome_button.h"
#include "chrome/browser/gtk/meta_frames.h"
#include "chrome/browser/pref_service.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/notification_details.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/notification_source.h"
#include "chrome/common/notification_type.h"
#include "gfx/color_utils.h"
#include "gfx/skbitmap_operations.h"
#include "gfx/skia_utils_gtk.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "gfx/gtk_util.h"
#include "grit/app_resources.h"
#include "grit/theme_resources.h"

namespace {

// The size of the rendered toolbar image.
const int kToolbarImageWidth = 64;
const int kToolbarImageHeight = 128;

const color_utils::HSL kExactColor = { -1, -1, -1 };

const color_utils::HSL kDefaultFrameShift = { -1, -1, 0.4 };

// Values used as the new luminance and saturation values in the inactive tab
// text color.
const double kDarkInactiveLuminance = 0.85;
const double kLightInactiveLuminance = 0.15;
const double kHeavyInactiveSaturation = 0.7;
const double kLightInactiveSaturation = 0.3;

// Minimum difference between the toolbar and the button color before we try a
// different color.
const double kMinimumLuminanceDifference = 0.1;

// Number of times that the background color should be counted when trying to
// calculate the border color in GTK theme mode.
const int kBgWeight = 3;

// Padding to left, top and bottom of vertical separators.
const int kSeparatorPadding = 2;

// Default color for links on the NTP when the GTK+ theme doesn't define a
// link color. Constant taken from gtklinkbutton.c.
const GdkColor kDefaultLinkColor = { 0, 0, 0, 0xeeee };

// Middle color of the separator gradient.
const double kMidSeparatorColor[] =
    { 194.0 / 255.0, 205.0 / 255.0, 212.0 / 212.0 };
// Top color of the separator gradient.
const double kTopSeparatorColor[] =
    { 222.0 / 255.0, 234.0 / 255.0, 248.0 / 255.0 };

// Converts a GdkColor to a SkColor.
SkColor GdkToSkColor(const GdkColor* color) {
  return SkColorSetRGB(color->red >> 8,
                       color->green >> 8,
                       color->blue >> 8);
}

// A list of images that we provide while in gtk mode.
const int kThemeImages[] = {
  IDR_THEME_TOOLBAR,
  IDR_THEME_TAB_BACKGROUND,
  IDR_THEME_TAB_BACKGROUND_INCOGNITO,
  IDR_THEME_FRAME,
  IDR_THEME_FRAME_INACTIVE,
  IDR_THEME_FRAME_INCOGNITO,
  IDR_THEME_FRAME_INCOGNITO_INACTIVE,
};

// A list of icons used in the autocomplete view that should be tinted to the
// current gtk theme selection color so they stand out against the GtkEntry's
// base color.
const int kAutocompleteImages[] = {
  IDR_OMNIBOX_HTTP,
  IDR_OMNIBOX_HTTP_DARK,
  IDR_OMNIBOX_HISTORY,
  IDR_OMNIBOX_HISTORY_DARK,
  IDR_OMNIBOX_SEARCH,
  IDR_OMNIBOX_SEARCH_DARK,
  IDR_OMNIBOX_MORE,
  IDR_OMNIBOX_MORE_DARK,
  IDR_OMNIBOX_STAR,
  IDR_OMNIBOX_STAR_DARK,
  IDR_GEOLOCATION_ALLOWED_LOCATIONBAR_ICON,
  IDR_GEOLOCATION_DENIED_LOCATIONBAR_ICON
};

bool IsOverridableImage(int id) {
  static std::set<int> images;
  if (images.empty()) {
    images.insert(kThemeImages, kThemeImages + arraysize(kThemeImages));
    images.insert(kAutocompleteImages,
                  kAutocompleteImages + arraysize(kAutocompleteImages));

    const std::set<int>& buttons =
        BrowserThemeProvider::GetTintableToolbarButtons();
    images.insert(buttons.begin(), buttons.end());
  }

  return images.count(id) > 0;
}

// Picks a button tint from a set of background colors. While
// |accent_gdk_color| will usually be the same color through a theme, this
// function will get called with the normal GtkLabel |text_color|/GtkWindow
// |background_color| pair and the GtkEntry |text_color|/|background_color|
// pair. While 3/4 of the time the resulting tint will be the same, themes that
// have a dark window background (with light text) and a light text entry (with
// dark text) will get better icons with this separated out.
void PickButtonTintFromColors(const GdkColor& accent_gdk_color,
                              const GdkColor& text_color,
                              const GdkColor& background_color,
                              color_utils::HSL* tint) {
  SkColor accent_color = GdkToSkColor(&accent_gdk_color);
  color_utils::HSL accent_tint;
  color_utils::SkColorToHSL(accent_color, &accent_tint);

  color_utils::HSL text_tint;
  color_utils::SkColorToHSL(GdkToSkColor(&text_color), &text_tint);

  color_utils::HSL background_tint;
  color_utils::SkColorToHSL(GdkToSkColor(&background_color), &background_tint);

  // If the accent color is gray, then our normal HSL tomfoolery will bring out
  // whatever color is oddly dominant (for example, in rgb space [125, 128,
  // 125] will tint green instead of gray). Slight differences (+/-10 (4%) to
  // all color components) should be interpreted as this color being gray and
  // we should switch into a special grayscale mode.
  int rb_diff = abs(SkColorGetR(accent_color) - SkColorGetB(accent_color));
  int rg_diff = abs(SkColorGetR(accent_color) - SkColorGetG(accent_color));
  int bg_diff = abs(SkColorGetB(accent_color) - SkColorGetG(accent_color));
  if (rb_diff < 10 && rg_diff < 10 && bg_diff < 10) {
    // Our accent is white/gray/black. Only the luminance of the accent color
    // matters.
    tint->h = -1;

    // Use the saturation of the text.
    tint->s = text_tint.s;

    // Use the luminance of the accent color UNLESS there isn't enough
    // luminance contrast between the accent color and the base color.
    if (fabs(accent_tint.l - background_tint.l) > 0.3)
      tint->l = accent_tint.l;
    else
      tint->l = text_tint.l;
  } else {
    // Our accent is a color.
    tint->h = accent_tint.h;

    // Don't modify the saturation; the amount of color doesn't matter.
    tint->s = -1;

    // If the text wants us to darken the icon, don't change the luminance (the
    // icons are already dark enough). Otherwise, lighten the icon by no more
    // than 0.9 since we don't want a pure-white icon even if the text is pure
    // white.
    if (text_tint.l < 0.5)
      tint->l = -1;
    else if (text_tint.l <= 0.9)
      tint->l = text_tint.l;
    else
      tint->l = 0.9;
  }
}


// Builds and tints the image with |id| to the GtkStateType |state| and
// places the result in |icon_set|.
void BuildIconFromIDRWithColor(int id,
                               GtkStyle* style,
                               GtkStateType state,
                               GtkIconSet* icon_set) {
  SkColor color = GdkToSkColor(&style->fg[state]);
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  SkBitmap original = *rb.GetBitmapNamed(id);

  SkBitmap fill_color;
  fill_color.setConfig(SkBitmap::kARGB_8888_Config,
                       original.width(), original.height(), 0);
  fill_color.allocPixels();
  fill_color.eraseColor(color);
  SkBitmap masked = SkBitmapOperations::CreateMaskedBitmap(
      fill_color, original);

  GtkIconSource* icon = gtk_icon_source_new();
  GdkPixbuf* pixbuf = gfx::GdkPixbufFromSkBitmap(&masked);
  gtk_icon_source_set_pixbuf(icon, pixbuf);
  g_object_unref(pixbuf);

  gtk_icon_source_set_direction_wildcarded(icon, TRUE);
  gtk_icon_source_set_size_wildcarded(icon, TRUE);

  gtk_icon_source_set_state(icon, state);
  // All fields default to wildcarding being on and setting a property doesn't
  // turn off wildcarding. You need to do this yourself. This is stated once in
  // the documentation in the gtk_icon_source_new() function, and no where else.
  gtk_icon_source_set_state_wildcarded(
      icon, state == GTK_STATE_NORMAL);

  gtk_icon_set_add_source(icon_set, icon);
  gtk_icon_source_free(icon);
}


}  // namespace

GtkWidget* GtkThemeProvider::icon_widget_ = NULL;
GdkPixbuf* GtkThemeProvider::default_folder_icon_ = NULL;
GdkPixbuf* GtkThemeProvider::default_bookmark_icon_ = NULL;

// static
GtkThemeProvider* GtkThemeProvider::GetFrom(Profile* profile) {
  return static_cast<GtkThemeProvider*>(profile->GetThemeProvider());
}

GtkThemeProvider::GtkThemeProvider()
    : BrowserThemeProvider(),
      fake_window_(gtk_window_new(GTK_WINDOW_TOPLEVEL)),
      fake_frame_(meta_frames_new()),
      signals_(new GtkSignalRegistrar),
      fullscreen_icon_set_(NULL) {
  fake_label_.Own(gtk_label_new(""));
  fake_entry_.Own(gtk_entry_new());
  fake_menu_item_.Own(gtk_menu_item_new());

  // Only realized widgets receive style-set notifications, which we need to
  // broadcast new theme images and colors. Only realized widgets have style
  // properties, too, which we query for some colors.
  gtk_widget_realize(fake_frame_);
  gtk_widget_realize(fake_window_);
  signals_->Connect(fake_frame_, "style-set",
                    G_CALLBACK(&OnStyleSetThunk), this);
}

GtkThemeProvider::~GtkThemeProvider() {
  profile()->GetPrefs()->RemovePrefObserver(prefs::kUsesSystemTheme, this);
  gtk_widget_destroy(fake_window_);
  gtk_widget_destroy(fake_frame_);
  fake_label_.Destroy();
  fake_entry_.Destroy();
  fake_menu_item_.Destroy();

  FreeIconSets();

  // We have to call this because FreePlatformCached() in ~BrowserThemeProvider
  // doesn't call the right virutal FreePlatformCaches.
  FreePlatformCaches();
}

void GtkThemeProvider::Init(Profile* profile) {
  profile->GetPrefs()->AddPrefObserver(prefs::kUsesSystemTheme, this);
  use_gtk_ = profile->GetPrefs()->GetBoolean(prefs::kUsesSystemTheme);

  BrowserThemeProvider::Init(profile);
}

SkBitmap* GtkThemeProvider::GetBitmapNamed(int id) const {
  // Try to get our cached version:
  ImageCache::const_iterator it = gtk_images_.find(id);
  if (it != gtk_images_.end())
    return it->second;

  if (use_gtk_ && IsOverridableImage(id)) {
    // We haven't built this image yet:
    SkBitmap* bitmap = GenerateGtkThemeBitmap(id);
    gtk_images_[id] = bitmap;
    return bitmap;
  }

  return BrowserThemeProvider::GetBitmapNamed(id);
}

SkColor GtkThemeProvider::GetColor(int id) const {
  if (use_gtk_) {
    ColorMap::const_iterator it = colors_.find(id);
    if (it != colors_.end())
      return it->second;
  }

  return BrowserThemeProvider::GetColor(id);
}

bool GtkThemeProvider::HasCustomImage(int id) const {
  if (use_gtk_)
    return IsOverridableImage(id);

  return BrowserThemeProvider::HasCustomImage(id);
}

void GtkThemeProvider::InitThemesFor(NotificationObserver* observer) {
  observer->Observe(NotificationType::BROWSER_THEME_CHANGED,
                    Source<ThemeProvider>(this),
                    NotificationService::NoDetails());
}

void GtkThemeProvider::SetTheme(Extension* extension) {
  profile()->GetPrefs()->SetBoolean(prefs::kUsesSystemTheme, false);
  LoadDefaultValues();
  BrowserThemeProvider::SetTheme(extension);
}

void GtkThemeProvider::UseDefaultTheme() {
  profile()->GetPrefs()->SetBoolean(prefs::kUsesSystemTheme, false);
  LoadDefaultValues();
  BrowserThemeProvider::UseDefaultTheme();
}

void GtkThemeProvider::SetNativeTheme() {
  profile()->GetPrefs()->SetBoolean(prefs::kUsesSystemTheme, true);
  ClearAllThemeData();
  LoadGtkValues();
  NotifyThemeChanged(NULL);
}

bool GtkThemeProvider::UsingDefaultTheme() {
  return !use_gtk_ && BrowserThemeProvider::UsingDefaultTheme();
}

void GtkThemeProvider::Observe(NotificationType type,
                               const NotificationSource& source,
                               const NotificationDetails& details) {
  if ((type == NotificationType::PREF_CHANGED) &&
      (*Details<std::wstring>(details).ptr() == prefs::kUsesSystemTheme))
    use_gtk_ = profile()->GetPrefs()->GetBoolean(prefs::kUsesSystemTheme);
}

GtkWidget* GtkThemeProvider::BuildChromeButton() {
  GtkWidget* button = HoverControllerGtk::CreateChromeButton();
  gtk_chrome_button_set_use_gtk_rendering(GTK_CHROME_BUTTON(button), use_gtk_);
  chrome_buttons_.push_back(button);

  signals_->Connect(button, "destroy", G_CALLBACK(OnDestroyChromeButtonThunk),
                    this);
  return button;
}

GtkWidget* GtkThemeProvider::CreateToolbarSeparator() {
  GtkWidget* separator = gtk_vseparator_new();
  GtkWidget* alignment = gtk_alignment_new(0, 0, 1, 1);
  gtk_alignment_set_padding(GTK_ALIGNMENT(alignment),
      kSeparatorPadding, kSeparatorPadding, kSeparatorPadding, 0);
  gtk_container_add(GTK_CONTAINER(alignment), separator);

  signals_->Connect(separator, "expose-event",
                    G_CALLBACK(OnSeparatorExposeThunk), this);
  return alignment;
}

bool GtkThemeProvider::UseGtkTheme() const {
  return use_gtk_;
}

GdkColor GtkThemeProvider::GetGdkColor(int id) const {
  return gfx::SkColorToGdkColor(GetColor(id));
}

GdkColor GtkThemeProvider::GetBorderColor() const {
  GtkStyle* style = gtk_rc_get_style(fake_window_);

  GdkColor text;
  GdkColor bg;
  if (use_gtk_) {
    text = style->text[GTK_STATE_NORMAL];
    bg = style->bg[GTK_STATE_NORMAL];
  } else {
    text = GetGdkColor(COLOR_BOOKMARK_TEXT);
    bg = GetGdkColor(COLOR_TOOLBAR);
  }

  // Creates a weighted average between the text and base color where
  // the base color counts more than once.
  GdkColor color;
  color.pixel = 0;
  color.red = (text.red + (bg.red * kBgWeight)) / (1 + kBgWeight);
  color.green = (text.green + (bg.green * kBgWeight)) / (1 + kBgWeight);
  color.blue = (text.blue + (bg.blue * kBgWeight)) / (1 + kBgWeight);

  return color;
}

GtkIconSet* GtkThemeProvider::GetIconSetForId(int id) const {
  if (id == IDR_FULLSCREEN_MENU_BUTTON)
    return fullscreen_icon_set_;

  return NULL;
}

void GtkThemeProvider::GetScrollbarColors(GdkColor* thumb_active_color,
                                          GdkColor* thumb_inactive_color,
                                          GdkColor* track_color) {
  // Create window containing scrollbar elements
  GtkWidget* window    = gtk_window_new(GTK_WINDOW_POPUP);
  GtkWidget* fixed     = gtk_fixed_new();
  GtkWidget* scrollbar = gtk_hscrollbar_new(NULL);
  gtk_container_add(GTK_CONTAINER(window), fixed);
  gtk_container_add(GTK_CONTAINER(fixed),  scrollbar);
  gtk_widget_realize(window);
  gtk_widget_realize(scrollbar);

  // Draw scrollbar thumb part and track into offscreen image
  const int kWidth  = 100;
  const int kHeight = 20;
  GtkStyle*  style  = gtk_rc_get_style(scrollbar);
  GdkPixmap* pm     = gdk_pixmap_new(window->window, kWidth, kHeight, -1);
  GdkRectangle rect = { 0, 0, kWidth, kHeight };
  unsigned char data[3*kWidth*kHeight];
  for (int i = 0; i < 3; ++i) {
    if (i < 2) {
      // Thumb part
      gtk_paint_slider(style, pm,
                       i == 0 ? GTK_STATE_PRELIGHT : GTK_STATE_NORMAL,
                       GTK_SHADOW_OUT, &rect, scrollbar, "slider", 0, 0,
                       kWidth, kHeight, GTK_ORIENTATION_HORIZONTAL);
    } else {
      // Track
      gtk_paint_box(style, pm, GTK_STATE_ACTIVE, GTK_SHADOW_IN, &rect,
                    scrollbar, "trough-upper", 0, 0, kWidth, kHeight);
    }
    GdkPixbuf* pb = gdk_pixbuf_new_from_data(data, GDK_COLORSPACE_RGB,
                                             FALSE, 8, kWidth, kHeight,
                                             3*kWidth, 0, 0);
    gdk_pixbuf_get_from_drawable(pb, pm, NULL, 0, 0, 0, 0, kWidth, kHeight);

    // Sample pixels
    int components[3] = { 0 };
    for (int y = 2; y < kHeight-2; ++y) {
      for (int c = 0; c < 3; ++c) {
        // Sample a vertical slice of pixels at about one-thirds from the
        // left edge. This allows us to avoid any fixed graphics that might be
        // located at the edges or in the center of the scrollbar.
        // Each pixel is made up of a red, green, and blue component; taking up
        // a total of three bytes.
        components[c] += data[3*(kWidth/3 + y*kWidth) + c];
      }
    }
    GdkColor* color = i == 0 ? thumb_active_color :
                      i == 1 ? thumb_inactive_color :
                               track_color;
    color->pixel = 0;
    // We sampled pixels across the full height of the image, ignoring a two
    // pixel border. In some themes, the border has a completely different
    // color which we do not want to factor into our average color computation.
    //
    // We now need to scale the colors from the 0..255 range, to the wider
    // 0..65535 range, and we need to actually compute the average color; so,
    // we divide by the total number of pixels in the sample.
    color->red   = components[0] * 65535 / (255*(kHeight-4));
    color->green = components[1] * 65535 / (255*(kHeight-4));
    color->blue  = components[2] * 65535 / (255*(kHeight-4));

    g_object_unref(pb);
  }
  g_object_unref(pm);

  gtk_widget_destroy(window);
}

CairoCachedSurface* GtkThemeProvider::GetSurfaceNamed(
    int id, GtkWidget* widget_on_display) {
  GdkDisplay* display = gtk_widget_get_display(widget_on_display);
  CairoCachedSurfaceMap& surface_map = per_display_surfaces_[display];

  // Check to see if we already have the pixbuf in the cache.
  CairoCachedSurfaceMap::const_iterator found = surface_map.find(id);
  if (found != surface_map.end())
    return found->second;

  GdkPixbuf* pixbuf = GetPixbufNamed(id);
  CairoCachedSurface* surface = new CairoCachedSurface;
  surface->UsePixbuf(pixbuf);

  surface_map[id] = surface;

  return surface;
}

CairoCachedSurface* GtkThemeProvider::GetUnthemedSurfaceNamed(
    int id, GtkWidget* widget_on_display) {
  GdkDisplay* display = gtk_widget_get_display(widget_on_display);
  CairoCachedSurfaceMap& surface_map = per_display_unthemed_surfaces_[display];

  // Check to see if we already have the pixbuf in the cache.
  CairoCachedSurfaceMap::const_iterator found = surface_map.find(id);
  if (found != surface_map.end())
    return found->second;

  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  GdkPixbuf* pixbuf = rb.GetPixbufNamed(id);
  CairoCachedSurface* surface = new CairoCachedSurface;
  surface->UsePixbuf(pixbuf);

  surface_map[id] = surface;

  return surface;
}

// static
GdkPixbuf* GtkThemeProvider::GetFolderIcon(bool native) {
  if (native) {
    if (!icon_widget_)
      icon_widget_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    // We never release our ref, so we will leak this on program shutdown.
    if (!default_folder_icon_) {
      default_folder_icon_ =
          gtk_widget_render_icon(icon_widget_, GTK_STOCK_DIRECTORY,
                                 GTK_ICON_SIZE_MENU, NULL);
    }
    if (default_folder_icon_)
      return default_folder_icon_;
  }

  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  static GdkPixbuf* default_folder_icon_ = rb.GetPixbufNamed(
      IDR_BOOKMARK_BAR_FOLDER);
  return default_folder_icon_;
}

// static
GdkPixbuf* GtkThemeProvider::GetDefaultFavicon(bool native) {
  if (native) {
    if (!icon_widget_)
      icon_widget_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    // We never release our ref, so we will leak this on program shutdown.
    if (!default_bookmark_icon_) {
      default_bookmark_icon_ =
          gtk_widget_render_icon(icon_widget_, GTK_STOCK_FILE,
                                 GTK_ICON_SIZE_MENU, NULL);
    }
    if (default_bookmark_icon_)
      return default_bookmark_icon_;
  }

  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  static GdkPixbuf* default_bookmark_icon_ = rb.GetPixbufNamed(
      IDR_DEFAULT_FAVICON);
  return default_bookmark_icon_;
}

// static
bool GtkThemeProvider::DefaultUsesSystemTheme() {
  scoped_ptr<base::EnvVarGetter> env_getter(base::EnvVarGetter::Create());

  switch (base::GetDesktopEnvironment(env_getter.get())) {
    case base::DESKTOP_ENVIRONMENT_GNOME:
    case base::DESKTOP_ENVIRONMENT_XFCE:
      return true;
    default:
      return false;
  }
}

void GtkThemeProvider::ClearAllThemeData() {
  colors_.clear();
  tints_.clear();

  BrowserThemeProvider::ClearAllThemeData();
}

void GtkThemeProvider::LoadThemePrefs() {
  if (use_gtk_) {
    LoadGtkValues();
  } else {
    LoadDefaultValues();
    BrowserThemeProvider::LoadThemePrefs();
  }

  RebuildMenuIconSets();
}

void GtkThemeProvider::NotifyThemeChanged(Extension* extension) {
  BrowserThemeProvider::NotifyThemeChanged(extension);

  // Notify all GtkChromeButtons of their new rendering mode:
  for (std::vector<GtkWidget*>::iterator it = chrome_buttons_.begin();
       it != chrome_buttons_.end(); ++it) {
    gtk_chrome_button_set_use_gtk_rendering(
        GTK_CHROME_BUTTON(*it), use_gtk_);
  }
}

void GtkThemeProvider::FreePlatformCaches() {
  BrowserThemeProvider::FreePlatformCaches();
  FreePerDisplaySurfaces(&per_display_surfaces_);
  FreePerDisplaySurfaces(&per_display_unthemed_surfaces_);
  STLDeleteValues(&gtk_images_);
}

void GtkThemeProvider::OnStyleSet(GtkWidget* widget,
                                  GtkStyle* previous_style) {
  GdkPixbuf* default_folder_icon = default_folder_icon_;
  GdkPixbuf* default_bookmark_icon = default_bookmark_icon_;
  default_folder_icon_ = NULL;
  default_bookmark_icon_ = NULL;

  if (profile()->GetPrefs()->GetBoolean(prefs::kUsesSystemTheme)) {
    ClearAllThemeData();
    LoadGtkValues();
    NotifyThemeChanged(NULL);
  }

  RebuildMenuIconSets();

  // Free the old icons only after the theme change notification has gone
  // through.
  if (default_folder_icon)
    g_object_unref(default_folder_icon);
  if (default_bookmark_icon)
    g_object_unref(default_bookmark_icon);
}

void GtkThemeProvider::LoadGtkValues() {
  // Before we start setting images and values, we have to clear out old, stale
  // values. (If we don't do this, we'll regress startup time in the case where
  // someone installs a heavyweight theme, then goes back to GTK.)
  DictionaryValue* pref_images =
      profile()->GetPrefs()->GetMutableDictionary(prefs::kCurrentThemeImages);
  pref_images->Clear();

  GtkStyle* frame_style = gtk_rc_get_style(fake_frame_);
  GdkColor frame_color = frame_style->bg[GTK_STATE_SELECTED];
  GdkColor inactive_frame_color = frame_style->bg[GTK_STATE_INSENSITIVE];

  GtkStyle* window_style = gtk_rc_get_style(fake_window_);
  GdkColor toolbar_color = window_style->bg[GTK_STATE_NORMAL];
  GdkColor button_color = window_style->bg[GTK_STATE_SELECTED];

  GtkStyle* label_style = gtk_rc_get_style(fake_label_.get());
  GdkColor label_color = label_style->fg[GTK_STATE_NORMAL];

  GtkSettings* settings = gtk_settings_get_default();
  bool theme_has_frame_color = false;
  if (settings) {
    GHashTable* color_scheme = NULL;
    g_object_get(settings, "color-hash", &color_scheme, NULL);

    if (color_scheme) {
      // If we have a "gtk-color-scheme" set in this theme, mine it for hints
      // about what we should actually set the frame color to.
      GdkColor* color = NULL;
      if ((color = static_cast<GdkColor*>(
          g_hash_table_lookup(color_scheme, "frame_color")))) {
        frame_color = *color;
        theme_has_frame_color = true;
      }

      if ((color = static_cast<GdkColor*>(
          g_hash_table_lookup(color_scheme, "inactive_frame_color")))) {
        inactive_frame_color = *color;
      }
    }
  }

  if (!theme_has_frame_color) {
    // If the theme's gtkrc doesn't explicitly tell us to use a specific frame
    // color, change the luminosity of the frame color downwards to 80% of what
    // it currently is. This is in a futile attempt to match the default
    // metacity and xfwm themes.
    SkColor shifted = color_utils::HSLShift(GdkToSkColor(&frame_color),
                                            kDefaultFrameShift);
    frame_color.pixel = 0;
    frame_color.red = SkColorGetR(shifted) * kSkiaToGDKMultiplier;
    frame_color.green = SkColorGetG(shifted) * kSkiaToGDKMultiplier;
    frame_color.blue = SkColorGetB(shifted) * kSkiaToGDKMultiplier;
  }

  // Build the various icon tints.
  GetNormalButtonTintHSL(&button_tint_);
  GetNormalEntryForegroundHSL(&entry_tint_);
  GetSelectedEntryForegroundHSL(&selected_entry_tint_);

  SetThemeTintFromGtk(BrowserThemeProvider::TINT_BUTTONS, &button_color);
  SetThemeTintFromGtk(BrowserThemeProvider::TINT_FRAME, &frame_color);
  SetThemeTintFromGtk(BrowserThemeProvider::TINT_FRAME_INCOGNITO, &frame_color);
  SetThemeTintFromGtk(BrowserThemeProvider::TINT_BACKGROUND_TAB, &frame_color);

  SetThemeColorFromGtk(BrowserThemeProvider::COLOR_FRAME, &frame_color);
  BuildTintedFrameColor(BrowserThemeProvider::COLOR_FRAME_INACTIVE,
                        BrowserThemeProvider::TINT_FRAME_INACTIVE);
  BuildTintedFrameColor(BrowserThemeProvider::COLOR_FRAME_INCOGNITO,
                        BrowserThemeProvider::TINT_FRAME_INCOGNITO);
  BuildTintedFrameColor(BrowserThemeProvider::COLOR_FRAME_INCOGNITO_INACTIVE,
                        BrowserThemeProvider::TINT_FRAME_INCOGNITO_INACTIVE);

  SetThemeColorFromGtk(BrowserThemeProvider::COLOR_TOOLBAR, &toolbar_color);
  SetThemeColorFromGtk(BrowserThemeProvider::COLOR_TAB_TEXT, &label_color);
  SetThemeColorFromGtk(BrowserThemeProvider::COLOR_BOOKMARK_TEXT, &label_color);
  SetThemeColorFromGtk(BrowserThemeProvider::COLOR_CONTROL_BACKGROUND,
                       &window_style->bg[GTK_STATE_NORMAL]);
  SetThemeColorFromGtk(BrowserThemeProvider::COLOR_BUTTON_BACKGROUND,
                       &window_style->bg[GTK_STATE_NORMAL]);

  // The inactive frame color never occurs naturally in the theme, as it is a
  // tinted version of |frame_color|. We generate another color based on the
  // background tab color, with the lightness and saturation moved in the
  // opposite direction. (We don't touch the hue, since there should be subtle
  // hints of the color in the text.)
  color_utils::HSL inactive_tab_text_hsl = tints_[TINT_BACKGROUND_TAB];
  if (inactive_tab_text_hsl.l < 0.5)
    inactive_tab_text_hsl.l = kDarkInactiveLuminance;
  else
    inactive_tab_text_hsl.l = kLightInactiveLuminance;

  if (inactive_tab_text_hsl.s < 0.5)
    inactive_tab_text_hsl.s = kHeavyInactiveSaturation;
  else
    inactive_tab_text_hsl.s = kLightInactiveSaturation;

  colors_[BrowserThemeProvider::COLOR_BACKGROUND_TAB_TEXT] =
      color_utils::HSLToSkColor(inactive_tab_text_hsl, 255);

  // The inactive color/tint is special: We *must* use the exact insensitive
  // color for all inactive windows, otherwise we end up neon pink half the
  // time.
  SetThemeColorFromGtk(BrowserThemeProvider::COLOR_FRAME_INACTIVE,
                       &inactive_frame_color);
  SetTintToExactColor(BrowserThemeProvider::TINT_FRAME_INACTIVE,
                      &inactive_frame_color);
  SetTintToExactColor(BrowserThemeProvider::TINT_FRAME_INCOGNITO_INACTIVE,
                      &inactive_frame_color);

  // We pick the text and background colors for the NTP out of the colors for a
  // GtkEntry. We do this because GtkEntries background color is never the same
  // as |toolbar_color|, is usually a white, and when it isn't a white,
  // provides sufficient contrast to |toolbar_color|. Try this out with
  // Darklooks, HighContrastInverse or ThinIce.
  GtkStyle* entry_style = gtk_rc_get_style(fake_entry_.get());
  GdkColor ntp_background = entry_style->base[GTK_STATE_NORMAL];
  GdkColor ntp_foreground = entry_style->text[GTK_STATE_NORMAL];
  SetThemeColorFromGtk(BrowserThemeProvider::COLOR_NTP_BACKGROUND,
                       &ntp_background);
  SetThemeColorFromGtk(BrowserThemeProvider::COLOR_NTP_TEXT,
                       &ntp_foreground);

  // The NTP header is the color that surrounds the current active thumbnail on
  // the NTP, and acts as the border of the "Recent Links" box. It would be
  // awesome if they were separated so we could use GetBorderColor() for the
  // border around the "Recent Links" section, but matching the frame color is
  // more important.
  SetThemeColorFromGtk(BrowserThemeProvider::COLOR_NTP_HEADER,
                       &frame_color);
  SetThemeColorFromGtk(BrowserThemeProvider::COLOR_NTP_SECTION,
                       &toolbar_color);
  SetThemeColorFromGtk(BrowserThemeProvider::COLOR_NTP_SECTION_TEXT,
                       &label_color);

  // Override the link color if the theme provides it.
  const GdkColor* link_color = NULL;
  gtk_widget_style_get(GTK_WIDGET(fake_window_),
                       "link-color", &link_color, NULL);
  if (!link_color)
    link_color = &kDefaultLinkColor;

  SetThemeColorFromGtk(BrowserThemeProvider::COLOR_NTP_LINK,
                       link_color);
  SetThemeColorFromGtk(BrowserThemeProvider::COLOR_NTP_LINK_UNDERLINE,
                       link_color);
  SetThemeColorFromGtk(BrowserThemeProvider::COLOR_NTP_SECTION_LINK,
                       link_color);
  SetThemeColorFromGtk(BrowserThemeProvider::COLOR_NTP_SECTION_LINK_UNDERLINE,
                       link_color);

  // Generate the colors that we pass to WebKit.
  focus_ring_color_ = GdkToSkColor(&frame_color);
  GdkColor thumb_active_color, thumb_inactive_color, track_color;
  GtkThemeProvider::GetScrollbarColors(&thumb_active_color,
                                       &thumb_inactive_color,
                                       &track_color);
  thumb_active_color_ = GdkToSkColor(&thumb_active_color);
  thumb_inactive_color_ = GdkToSkColor(&thumb_inactive_color);
  track_color_ = GdkToSkColor(&track_color);

  // Some GTK themes only define the text selection colors on the GtkEntry
  // class, so we need to use that for getting selection colors.
  active_selection_bg_color_ =
      GdkToSkColor(&entry_style->base[GTK_STATE_SELECTED]);
  active_selection_fg_color_ =
      GdkToSkColor(&entry_style->text[GTK_STATE_SELECTED]);
  inactive_selection_bg_color_ =
      GdkToSkColor(&entry_style->base[GTK_STATE_ACTIVE]);
  inactive_selection_fg_color_ =
      GdkToSkColor(&entry_style->text[GTK_STATE_ACTIVE]);
}

void GtkThemeProvider::LoadDefaultValues() {
  focus_ring_color_ = SkColorSetARGB(255, 229, 151, 0);
  thumb_active_color_ = SkColorSetRGB(244, 244, 244);
  thumb_inactive_color_ = SkColorSetRGB(234, 234, 234);
  track_color_ = SkColorSetRGB(211, 211, 211);

  active_selection_bg_color_ = SkColorSetRGB(30, 144, 255);
  active_selection_fg_color_ = SK_ColorWHITE;
  inactive_selection_bg_color_ = SkColorSetRGB(200, 200, 200);
  inactive_selection_fg_color_ = SkColorSetRGB(50, 50, 50);
}

void GtkThemeProvider::RebuildMenuIconSets() {
  FreeIconSets();

  GtkStyle* style = gtk_rc_get_style(fake_menu_item_.get());

  fullscreen_icon_set_ = gtk_icon_set_new();
  BuildIconFromIDRWithColor(IDR_FULLSCREEN_MENU_BUTTON,
                            style,
                            GTK_STATE_PRELIGHT,
                            fullscreen_icon_set_);
  BuildIconFromIDRWithColor(IDR_FULLSCREEN_MENU_BUTTON,
                            style,
                            GTK_STATE_NORMAL,
                            fullscreen_icon_set_);
}

void GtkThemeProvider::SetThemeColorFromGtk(int id, const GdkColor* color) {
  colors_[id] = GdkToSkColor(color);
}

void GtkThemeProvider::SetThemeTintFromGtk(int id, const GdkColor* color) {
  color_utils::HSL default_tint = GetDefaultTint(id);
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(GdkToSkColor(color), &hsl);

  if (default_tint.s != -1)
    hsl.s = default_tint.s;

  if (default_tint.l != -1)
    hsl.l = default_tint.l;

  tints_[id] = hsl;
}

void GtkThemeProvider::BuildTintedFrameColor(int color_id, int tint_id) {
  colors_[color_id] = HSLShift(colors_[BrowserThemeProvider::COLOR_FRAME],
                               tints_[tint_id]);
}

void GtkThemeProvider::SetTintToExactColor(int id, const GdkColor* color) {
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(GdkToSkColor(color), &hsl);
  tints_[id] = hsl;
}

void GtkThemeProvider::FreePerDisplaySurfaces(
    PerDisplaySurfaceMap* per_display_map) {
  for (PerDisplaySurfaceMap::iterator it = per_display_map->begin();
       it != per_display_map->end(); ++it) {
    for (CairoCachedSurfaceMap::iterator jt = it->second.begin();
         jt != it->second.end(); ++jt) {
      delete jt->second;
    }
  }
  per_display_map->clear();
}

void GtkThemeProvider::FreeIconSets() {
  if (fullscreen_icon_set_) {
    gtk_icon_set_unref(fullscreen_icon_set_);
    fullscreen_icon_set_ = NULL;
  }
}

SkBitmap* GtkThemeProvider::GenerateGtkThemeBitmap(int id) const {
  switch (id) {
    case IDR_THEME_TOOLBAR: {
      GtkStyle* style = gtk_rc_get_style(fake_window_);
      GdkColor* color = &style->bg[GTK_STATE_NORMAL];
      SkBitmap* bitmap = new SkBitmap;
      bitmap->setConfig(SkBitmap::kARGB_8888_Config,
                        kToolbarImageWidth, kToolbarImageHeight);
      bitmap->allocPixels();
      bitmap->eraseRGB(color->red >> 8, color->green >> 8, color->blue >> 8);
      return bitmap;
    }
    case IDR_THEME_TAB_BACKGROUND:
      return GenerateTabImage(IDR_THEME_FRAME);
    case IDR_THEME_TAB_BACKGROUND_INCOGNITO:
      return GenerateTabImage(IDR_THEME_FRAME_INCOGNITO);
    case IDR_THEME_FRAME:
      return GenerateFrameImage(BrowserThemeProvider::TINT_FRAME);
    case IDR_THEME_FRAME_INACTIVE:
      return GenerateFrameImage(BrowserThemeProvider::TINT_FRAME_INACTIVE);
    case IDR_THEME_FRAME_INCOGNITO:
      return GenerateFrameImage(BrowserThemeProvider::TINT_FRAME_INCOGNITO);
    case IDR_THEME_FRAME_INCOGNITO_INACTIVE: {
      return GenerateFrameImage(
          BrowserThemeProvider::TINT_FRAME_INCOGNITO_INACTIVE);
    }
    // Icons that sit inside the omnibox shouldn't receive TINT_BUTTONS and
    // instead should tint based on the foreground text entry color in GTK+
    // mode because some themes that try to be dark *and* light have very
    // different colors between the omnibox and the normal background area.
    case IDR_OMNIBOX_SEARCH:
    case IDR_OMNIBOX_MORE:
    case IDR_OMNIBOX_STAR:
    case IDR_GEOLOCATION_ALLOWED_LOCATIONBAR_ICON:
    case IDR_GEOLOCATION_DENIED_LOCATIONBAR_ICON: {
      return GenerateTintedIcon(id, entry_tint_);
    }
    // Two sets of omnibox icons, the one for normal http and the one for
    // history, include white backgrounds (and are supposed to, for the windows
    // chrome-theme). On linux, where we have all sorts of wacky themes and
    // color combinations we need to deal with, switch them out with
    // transparent background versions.
    case IDR_OMNIBOX_HTTP: {
      return GenerateTintedIcon(IDR_OMNIBOX_HTTP_TRANSPARENT, entry_tint_);
    }
    case IDR_OMNIBOX_HISTORY: {
      return GenerateTintedIcon(IDR_OMNIBOX_HISTORY_TRANSPARENT, entry_tint_);
    }
    // In GTK mode, the dark versions of the omnibox icons only ever appear in
    // the autocomplete popup and only against the current theme's GtkEntry
    // base[GTK_STATE_SELECTED] color, so tint the icons so they won't collide
    // with the selected color.
    case IDR_OMNIBOX_HTTP_DARK: {
      return GenerateTintedIcon(IDR_OMNIBOX_HTTP_DARK_TRANSPARENT,
                                selected_entry_tint_);
    }
    case IDR_OMNIBOX_HISTORY_DARK: {
      return GenerateTintedIcon(IDR_OMNIBOX_HISTORY_DARK_TRANSPARENT,
                                selected_entry_tint_);
    }
    case IDR_OMNIBOX_SEARCH_DARK:
    case IDR_OMNIBOX_MORE_DARK:
    case IDR_OMNIBOX_STAR_DARK: {
      return GenerateTintedIcon(id, selected_entry_tint_);
    }
    default: {
      return GenerateTintedIcon(id, button_tint_);
    }
  }
}

SkBitmap* GtkThemeProvider::GenerateFrameImage(int tint_id) const {
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  scoped_ptr<SkBitmap> frame(new SkBitmap(*rb.GetBitmapNamed(IDR_THEME_FRAME)));
  TintMap::const_iterator it = tints_.find(tint_id);
  DCHECK(it != tints_.end());
  return new SkBitmap(SkBitmapOperations::CreateHSLShiftedBitmap(*frame,
                                                                 it->second));
}

SkBitmap* GtkThemeProvider::GenerateTabImage(int base_id) const {
  SkBitmap* base_image = GetBitmapNamed(base_id);
  SkBitmap bg_tint = SkBitmapOperations::CreateHSLShiftedBitmap(
      *base_image, GetTint(BrowserThemeProvider::TINT_BACKGROUND_TAB));
  return new SkBitmap(SkBitmapOperations::CreateTiledBitmap(
      bg_tint, 0, 0, bg_tint.width(), bg_tint.height()));
}

SkBitmap* GtkThemeProvider::GenerateTintedIcon(int base_id,
                                               color_utils::HSL tint) const {
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  scoped_ptr<SkBitmap> button(new SkBitmap(*rb.GetBitmapNamed(base_id)));
  return new SkBitmap(SkBitmapOperations::CreateHSLShiftedBitmap(
      *button, tint));
}

void GtkThemeProvider::GetNormalButtonTintHSL(
    color_utils::HSL* tint) const {
  GtkStyle* window_style = gtk_rc_get_style(fake_window_);
  const GdkColor accent_gdk_color = window_style->bg[GTK_STATE_SELECTED];
  const GdkColor base_color = window_style->base[GTK_STATE_NORMAL];

  GtkStyle* label_style = gtk_rc_get_style(fake_label_.get());
  const GdkColor text_color = label_style->fg[GTK_STATE_NORMAL];

  PickButtonTintFromColors(accent_gdk_color, text_color, base_color, tint);
}

void GtkThemeProvider::GetNormalEntryForegroundHSL(
    color_utils::HSL* tint) const {
  GtkStyle* window_style = gtk_rc_get_style(fake_window_);
  const GdkColor accent_gdk_color = window_style->bg[GTK_STATE_SELECTED];

  GtkStyle* style = gtk_rc_get_style(fake_entry_.get());
  const GdkColor text_color = style->text[GTK_STATE_NORMAL];
  const GdkColor base_color = style->base[GTK_STATE_NORMAL];

  PickButtonTintFromColors(accent_gdk_color, text_color, base_color, tint);
}

void GtkThemeProvider::GetSelectedEntryForegroundHSL(
    color_utils::HSL* tint) const {
  // The simplest of all the tints. We just use the selected text in the entry
  // since the icons tinted this way will only be displayed against
  // base[GTK_STATE_SELECTED].
  GtkStyle* style = gtk_rc_get_style(fake_entry_.get());
  const GdkColor color = style->text[GTK_STATE_SELECTED];
  color_utils::SkColorToHSL(GdkToSkColor(&color), tint);
}

void GtkThemeProvider::OnDestroyChromeButton(GtkWidget* button) {
  std::vector<GtkWidget*>::iterator it =
      find(chrome_buttons_.begin(), chrome_buttons_.end(), button);
  if (it != chrome_buttons_.end())
    chrome_buttons_.erase(it);
}

gboolean GtkThemeProvider::OnSeparatorExpose(GtkWidget* widget,
                                             GdkEventExpose* event) {
  if (UseGtkTheme())
    return FALSE;

  cairo_t* cr = gdk_cairo_create(GDK_DRAWABLE(widget->window));
  gdk_cairo_rectangle(cr, &event->area);
  cairo_clip(cr);

  GdkColor bottom_color = GetGdkColor(BrowserThemeProvider::COLOR_TOOLBAR);
  double bottom_color_rgb[] = {
      static_cast<double>(bottom_color.red / 257) / 255.0,
      static_cast<double>(bottom_color.green / 257) / 255.0,
      static_cast<double>(bottom_color.blue / 257) / 255.0, };

  cairo_pattern_t* pattern =
      cairo_pattern_create_linear(widget->allocation.x, widget->allocation.y,
                                  widget->allocation.x,
                                  widget->allocation.y +
                                  widget->allocation.height);
  cairo_pattern_add_color_stop_rgb(
      pattern, 0.0,
      kTopSeparatorColor[0], kTopSeparatorColor[1], kTopSeparatorColor[2]);
  cairo_pattern_add_color_stop_rgb(
      pattern, 0.5,
      kMidSeparatorColor[0], kMidSeparatorColor[1], kMidSeparatorColor[2]);
  cairo_pattern_add_color_stop_rgb(
      pattern, 1.0,
      bottom_color_rgb[0], bottom_color_rgb[1], bottom_color_rgb[2]);
  cairo_set_source(cr, pattern);

  double start_x = 0.5 + widget->allocation.x;
  cairo_new_path(cr);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, start_x, widget->allocation.y);
  cairo_line_to(cr, start_x,
                    widget->allocation.y + widget->allocation.height);
  cairo_stroke(cr);
  cairo_destroy(cr);
  cairo_pattern_destroy(pattern);

  return TRUE;
}
