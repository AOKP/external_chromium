// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/views/wrench_menu.h"

#include <cmath>

#include "app/l10n_util.h"
#include "app/resource_bundle.h"
#include "base/utf_string_conversions.h"
#include "chrome/app/chrome_dll_resource.h"
#include "chrome/browser/browser.h"
#include "chrome/browser/host_zoom_map.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/common/notification_observer.h"
#include "chrome/common/notification_registrar.h"
#include "chrome/common/notification_source.h"
#include "chrome/common/notification_type.h"
#include "gfx/canvas.h"
#include "gfx/canvas_skia.h"
#include "gfx/skia_util.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "views/background.h"
#include "views/controls/button/image_button.h"
#include "views/controls/button/menu_button.h"
#include "views/controls/button/text_button.h"
#include "views/controls/label.h"
#include "views/controls/menu/menu_config.h"
#include "views/controls/menu/menu_item_view.h"
#include "views/controls/menu/menu_scroll_view_container.h"
#include "views/controls/menu/submenu_view.h"
#include "views/window/window.h"

using menus::MenuModel;
using views::CustomButton;
using views::ImageButton;
using views::Label;
using views::MenuConfig;
using views::MenuItemView;
using views::TextButton;
using views::View;

namespace {

// Colors used for buttons.
const SkColor kHotBorderColor = SkColorSetARGB(72, 0, 0, 0);
const SkColor kBorderColor = SkColorSetARGB(36, 0, 0, 0);
const SkColor kPushedBorderColor = SkColorSetARGB(72, 0, 0, 0);
const SkColor kHotBackgroundColor = SkColorSetARGB(204, 255, 255, 255);
const SkColor kBackgroundColor = SkColorSetARGB(102, 255, 255, 255);
const SkColor kPushedBackgroundColor = SkColorSetARGB(13, 0, 0, 0);

// Horizontal padding on the edges of the buttons.
const int kHorizontalPadding = 6;

// Subclass of ImageButton whose preferred size includes the size of the border.
class FullscreenButton : public ImageButton {
 public:
  FullscreenButton(views::ButtonListener* listener) : ImageButton(listener) {}

  virtual gfx::Size GetPreferredSize() {
    gfx::Size pref = ImageButton::GetPreferredSize();
    gfx::Insets insets;
    if (border())
      border()->GetInsets(&insets);
    pref.Enlarge(insets.width(), insets.height());
    return pref;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(FullscreenButton);
};

// Border for buttons contained in the menu. This is only used for getting the
// insets, the actual painting is done in MenuButtonBackground.
class MenuButtonBorder : public views::Border {
 public:
  MenuButtonBorder() {}

  virtual void Paint(const View& view, gfx::Canvas* canvas) const {
    // Painting of border is done in MenuButtonBackground.
  }

  virtual void GetInsets(gfx::Insets* insets) const {
    insets->Set(MenuConfig::instance().item_no_icon_top_margin,
                kHorizontalPadding,
                MenuConfig::instance().item_no_icon_bottom_margin,
                kHorizontalPadding);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MenuButtonBorder);
};

// Combination border/background for the buttons contained in the menu. The
// painting of the border/background is done here as TextButton does not always
// paint the border.
class MenuButtonBackground : public views::Background {
 public:
  enum ButtonType {
    LEFT_BUTTON,
    CENTER_BUTTON,
    RIGHT_BUTTON,
    SINGLE_BUTTON,
  };

  explicit MenuButtonBackground(ButtonType type)
      : type_(type),
        left_button_(NULL),
        right_button_(NULL) {}

  // Used when the type is CENTER_BUTTON to determine if the left/right edge
  // needs to be rendered selected.
  void SetOtherButtons(CustomButton* left_button, CustomButton* right_button) {
    left_button_ = left_button;
    right_button_ = right_button;
  }

  virtual void Paint(gfx::Canvas* canvas, View* view) const {
    CustomButton::ButtonState state =
        (view->GetClassName() == views::Label::kViewClassName) ?
        CustomButton::BS_NORMAL : static_cast<CustomButton*>(view)->state();
    int w = view->width();
    int h = view->height();
    switch (TypeAdjustedForRTL()) {
      case LEFT_BUTTON:
        canvas->FillRectInt(background_color(state), 1, 1, w, h - 2);
        canvas->FillRectInt(border_color(state), 2, 0, w, 1);
        canvas->FillRectInt(border_color(state), 1, 1, 1, 1);
        canvas->FillRectInt(border_color(state), 0, 2, 1, h - 4);
        canvas->FillRectInt(border_color(state), 1, h - 2, 1, 1);
        canvas->FillRectInt(border_color(state), 2, h - 1, w, 1);
        break;

      case CENTER_BUTTON: {
        canvas->FillRectInt(background_color(state), 1, 1, w - 2, h - 2);
        SkColor left_color = state != CustomButton::BS_NORMAL ?
            border_color(state) : border_color(left_button_->state());
        canvas->FillRectInt(left_color, 0, 0, 1, h);
        canvas->FillRectInt(border_color(state), 1, 0, w - 2, 1);
        canvas->FillRectInt(border_color(state), 1, h - 1, w - 2, 1);
        SkColor right_color = state != CustomButton::BS_NORMAL ?
            border_color(state) : border_color(right_button_->state());
        canvas->FillRectInt(right_color, w - 1, 0, 1, h);
        break;
      }

      case RIGHT_BUTTON:
        canvas->FillRectInt(background_color(state), 0, 1, w - 1, h - 2);
        canvas->FillRectInt(border_color(state), 0, 0, w - 2, 1);
        canvas->FillRectInt(border_color(state), w - 2, 1, 1, 1);
        canvas->FillRectInt(border_color(state), w - 1, 2, 1, h - 4);
        canvas->FillRectInt(border_color(state), w - 2, h - 2, 1, 1);
        canvas->FillRectInt(border_color(state), 0, h - 1, w - 2, 1);
        break;

      case SINGLE_BUTTON:
        canvas->FillRectInt(background_color(state), 1, 1, w - 2, h - 2);
        canvas->FillRectInt(border_color(state), 2, 0, w - 4, 1);
        canvas->FillRectInt(border_color(state), 1, 1, 1, 1);
        canvas->FillRectInt(border_color(state), 0, 2, 1, h - 4);
        canvas->FillRectInt(border_color(state), 1, h - 2, 1, 1);
        canvas->FillRectInt(border_color(state), 2, h - 1, w - 4, 1);
        canvas->FillRectInt(border_color(state), w - 2, 1, 1, 1);
        canvas->FillRectInt(border_color(state), w - 1, 2, 1, h - 4);
        canvas->FillRectInt(border_color(state), w - 2, h - 2, 1, 1);
        break;

      default:
        NOTREACHED();
        break;
    }
  }

 private:
  static SkColor border_color(CustomButton::ButtonState state) {
    switch (state) {
      case CustomButton::BS_HOT:    return kHotBorderColor;
      case CustomButton::BS_PUSHED: return kPushedBorderColor;
      default:                      return kBorderColor;
    }
  }

  static SkColor background_color(CustomButton::ButtonState state) {
    switch (state) {
      case CustomButton::BS_HOT:    return kHotBackgroundColor;
      case CustomButton::BS_PUSHED: return kPushedBackgroundColor;
      default:                      return kBackgroundColor;
    }
  }

  ButtonType TypeAdjustedForRTL() const {
    if (!base::i18n::IsRTL())
      return type_;

    switch (type_) {
      case LEFT_BUTTON:   return RIGHT_BUTTON;
      case RIGHT_BUTTON:  return LEFT_BUTTON;
      default:            break;
    }
    return type_;
  }

  const ButtonType type_;

  // See description above setter for details.
  CustomButton* left_button_;
  CustomButton* right_button_;

  DISALLOW_COPY_AND_ASSIGN(MenuButtonBackground);
};

// A View subclass that forces SchedulePaint to paint all. Normally when the
// mouse enters/exits a button the buttons invokes SchedulePaint. As part of the
// button border (MenuButtonBackground) is rendered by the button to the
// left/right of it SchedulePaint on the the button may not be enough, so this
// forces a paint all.
class ScheduleAllView : public views::View {
 public:
  ScheduleAllView() {}

  virtual void SchedulePaint(const gfx::Rect& r, bool urgent) {
    if (!IsVisible())
      return;

    if (GetParent()) {
      GetParent()->SchedulePaint(GetBounds(APPLY_MIRRORING_TRANSFORMATION),
                                 urgent);
    }
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ScheduleAllView);
};

TextButton* CreateAndConfigureButton(View* parent,
                                     views::ButtonListener* listener,
                                     int string_id,
                                     MenuButtonBackground::ButtonType type,
                                     MenuModel* model,
                                     int index,
                                     MenuButtonBackground** background) {
  TextButton* button =
      new TextButton(listener, l10n_util::GetString(string_id));
  button->SetFocusable(true);
  button->set_request_focus_on_press(false);
  button->set_tag(index);
  button->SetEnabled(model->IsEnabledAt(index));
  button->set_prefix_type(TextButton::PREFIX_HIDE);
  MenuButtonBackground* bg = new MenuButtonBackground(type);
  button->set_background(bg);
  button->SetEnabledColor(MenuConfig::instance().text_color);
  if (background)
    *background = bg;
  button->set_border(new MenuButtonBorder());
  button->set_alignment(TextButton::ALIGN_CENTER);
  button->SetShowHighlighted(true);
  button->SetNormalHasBorder(true);
  button->SetFont(views::MenuConfig::instance().font);
  button->ClearMaxTextSize();
  parent->AddChildView(button);
  return button;
}

}  // namespace

// CutCopyPasteView ------------------------------------------------------------

// CutCopyPasteView is the view containing the cut/copy/paste buttons.
class WrenchMenu::CutCopyPasteView : public ScheduleAllView,
                                     public views::ButtonListener {
 public:
  CutCopyPasteView(WrenchMenu* menu,
                   MenuModel* menu_model,
                   int cut_index,
                   int copy_index,
                   int paste_index)
      : menu_(menu),
        menu_model_(menu_model) {
    TextButton* cut = CreateAndConfigureButton(
        this, this, IDS_CUT, MenuButtonBackground::LEFT_BUTTON, menu_model,
        cut_index, NULL);

    MenuButtonBackground* copy_background = NULL;
    CreateAndConfigureButton(
        this, this, IDS_COPY, MenuButtonBackground::CENTER_BUTTON, menu_model,
        copy_index, &copy_background);

    TextButton* paste = CreateAndConfigureButton(
        this, this, IDS_PASTE, MenuButtonBackground::RIGHT_BUTTON, menu_model,
        paste_index, NULL);

    copy_background->SetOtherButtons(cut, paste);
  }

  gfx::Size GetPreferredSize() {
    // Returned height doesn't matter as MenuItemView forces everything to the
    // height of the menuitemview.
    return gfx::Size(GetMaxChildViewPreferredWidth() * GetChildViewCount(), 0);
  }

  void Layout() {
    // All buttons are given the same width.
    int width = GetMaxChildViewPreferredWidth();
    for (int i = 0; i < GetChildViewCount(); ++i)
      GetChildViewAt(i)->SetBounds(i * width, 0, width, height());
  }

  // ButtonListener
  virtual void ButtonPressed(views::Button* sender, const views::Event& event) {
    menu_->CancelAndEvaluate(menu_model_, sender->tag());
  }

 private:
  // Returns the max preferred width of all the children.
  int GetMaxChildViewPreferredWidth() {
    int width = 0;
    for (int i = 0; i < GetChildViewCount(); ++i)
      width = std::max(width, GetChildViewAt(i)->GetPreferredSize().width());
    return width;
  }

  WrenchMenu* menu_;
  MenuModel* menu_model_;

  DISALLOW_COPY_AND_ASSIGN(CutCopyPasteView);
};

// ZoomView --------------------------------------------------------------------

// Padding between the increment buttons and the reset button.
static const int kZoomPadding = 6;

// ZoomView contains the various zoom controls: two buttons to increase/decrease
// the zoom, a label showing the current zoom percent, and a button to go
// full-screen.
class WrenchMenu::ZoomView : public ScheduleAllView,
                             public views::ButtonListener,
                             public NotificationObserver {
 public:
  ZoomView(WrenchMenu* menu,
           MenuModel* menu_model,
           int decrement_index,
           int increment_index,
           int fullscreen_index)
      : menu_(menu),
        menu_model_(menu_model),
        fullscreen_index_(fullscreen_index),
        increment_button_(NULL),
        zoom_label_(NULL),
        decrement_button_(NULL),
        fullscreen_button_(NULL),
        zoom_label_width_(0) {
    decrement_button_ = CreateAndConfigureButton(
        this, this, IDS_ZOOM_MINUS2, MenuButtonBackground::LEFT_BUTTON,
        menu_model, decrement_index, NULL);

    zoom_label_ = new Label(l10n_util::GetStringF(IDS_ZOOM_PERCENT, L"100"));
    zoom_label_->SetColor(MenuConfig::instance().text_color);
    zoom_label_->SetHorizontalAlignment(Label::ALIGN_RIGHT);
    MenuButtonBackground* center_bg =
        new MenuButtonBackground(MenuButtonBackground::CENTER_BUTTON);
    zoom_label_->set_background(center_bg);
    zoom_label_->set_border(new MenuButtonBorder());
    zoom_label_->SetFont(MenuConfig::instance().font);
    AddChildView(zoom_label_);
    zoom_label_width_ = MaxWidthForZoomLabel();

    increment_button_ = CreateAndConfigureButton(
        this, this, IDS_ZOOM_PLUS2, MenuButtonBackground::RIGHT_BUTTON,
        menu_model, increment_index, NULL);

    center_bg->SetOtherButtons(increment_button_, decrement_button_);

    fullscreen_button_ = new FullscreenButton(this);
    fullscreen_button_->SetImage(
        ImageButton::BS_NORMAL,
        ResourceBundle::GetSharedInstance().GetBitmapNamed(
            IDR_FULLSCREEN_MENU_BUTTON));
    fullscreen_button_->SetFocusable(true);
    fullscreen_button_->set_request_focus_on_press(false);
    fullscreen_button_->set_tag(fullscreen_index);
    fullscreen_button_->SetImageAlignment(
        ImageButton::ALIGN_CENTER, ImageButton::ALIGN_MIDDLE);
    fullscreen_button_->set_border(views::Border::CreateEmptyBorder(
        0, kHorizontalPadding, 0, kHorizontalPadding));
    fullscreen_button_->set_background(
        new MenuButtonBackground(MenuButtonBackground::SINGLE_BUTTON));
    AddChildView(fullscreen_button_);

    UpdateZoomControls();

    registrar_.Add(this, NotificationType::ZOOM_LEVEL_CHANGED,
                   Source<Profile>(menu->browser_->profile()));
  }

  gfx::Size GetPreferredSize() {
    // The increment/decrement button are forced to the same width.
    int button_width = std::max(increment_button_->GetPreferredSize().width(),
                                decrement_button_->GetPreferredSize().width());
    int fullscreen_width = fullscreen_button_->GetPreferredSize().width();
    // Returned height doesn't matter as MenuItemView forces everything to the
    // height of the menuitemview.
    return gfx::Size(button_width + zoom_label_width_ + button_width +
                     kZoomPadding + fullscreen_width, 0);
  }

  void Layout() {
    int x = 0;
    int button_width = std::max(increment_button_->GetPreferredSize().width(),
                                decrement_button_->GetPreferredSize().width());
    gfx::Rect bounds(0, 0, button_width, height());

    decrement_button_->SetBounds(bounds);

    x += bounds.width();
    bounds.set_x(x);
    bounds.set_width(zoom_label_width_);
    zoom_label_->SetBounds(bounds);

    x += bounds.width();
    bounds.set_x(x);
    bounds.set_width(button_width);
    increment_button_->SetBounds(bounds);

    x += bounds.width() + kZoomPadding;
    bounds.set_x(x);
    bounds.set_width(fullscreen_button_->GetPreferredSize().width());
    fullscreen_button_->SetBounds(bounds);
  }

  // ButtonListener:
  virtual void ButtonPressed(views::Button* sender, const views::Event& event) {
    if (sender->tag() == fullscreen_index_) {
      menu_->CancelAndEvaluate(menu_model_, sender->tag());
    } else {
      // Zoom buttons don't close the menu.
      menu_model_->ActivatedAt(sender->tag());
    }
  }

  // NotificationObserver:
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details) {
    DCHECK_EQ(NotificationType::ZOOM_LEVEL_CHANGED, type.value);
    UpdateZoomControls();
  }

 private:
  void UpdateZoomControls() {
    bool enable_increment, enable_decrement;
    int zoom_percent =
        static_cast<int>(GetZoom(&enable_increment, &enable_decrement));
    enable_increment = enable_increment &&
        menu_model_->IsEnabledAt(increment_button_->tag());
    enable_decrement = enable_decrement &&
        menu_model_->IsEnabledAt(decrement_button_->tag());
    increment_button_->SetEnabled(enable_increment);
    decrement_button_->SetEnabled(enable_decrement);
    zoom_label_->SetText(l10n_util::GetStringF(
                             IDS_ZOOM_PERCENT, IntToWString(zoom_percent)));
    // If both increment and decrement are disabled, then we disable the zoom
    // label too.
    zoom_label_->SetEnabled(enable_increment || enable_decrement);
  }

  double GetZoom(bool* enable_increment, bool* enable_decrement) {
    // TODO: move this somewhere it can be shared.
    TabContents* selected_tab = menu_->browser_->GetSelectedTabContents();
    *enable_decrement = *enable_increment = false;
    if (!selected_tab)
      return 1;

    HostZoomMap* zoom_map = selected_tab->profile()->GetHostZoomMap();
    if (!zoom_map)
      return 1;

    int zoom_level = zoom_map->GetZoomLevel(selected_tab->GetURL());
    double value = ZoomPercentFromZoomLevel(zoom_level);
    *enable_decrement = (value != 50);
    *enable_increment = (value != 300);
    return value;
  }

  double ZoomPercentFromZoomLevel(int level) {
    return static_cast<double>(
        std::max(std::min(std::pow(1.2, level), 3.0), .5)) * 100;
  }

  // Calculates the max width the zoom string can be.
  int MaxWidthForZoomLabel() {
    gfx::Font font = zoom_label_->font();
    gfx::Insets insets;
    if (zoom_label_->border())
      zoom_label_->border()->GetInsets(&insets);
    int max_w = 0;
    for (int i = -4; i <= 7; ++i) {
      int zoom_percent = static_cast<int>(ZoomPercentFromZoomLevel(i));
      int w = font.GetStringWidth(
          l10n_util::GetStringF(IDS_ZOOM_PERCENT, zoom_percent));
      max_w = std::max(w, max_w);
    }
    return max_w + insets.width();
  }

  // Hosting WrenchMenu.
  WrenchMenu* menu_;

  // The menu model containing the increment/decrement/reset items.
  MenuModel* menu_model_;

  // Index of the fullscreen menu item in the model.
  const int fullscreen_index_;

  NotificationRegistrar registrar_;

  // Button for incrementing the zoom.
  TextButton* increment_button_;

  // Label showing zoom as a percent.
  Label* zoom_label_;

  // Button for decrementing the zoom.
  TextButton* decrement_button_;

  ImageButton* fullscreen_button_;

  // Width given to |zoom_label_|. This is the width at 100%.
  int zoom_label_width_;

  DISALLOW_COPY_AND_ASSIGN(ZoomView);
};

// WrenchMenu ------------------------------------------------------------------

WrenchMenu::WrenchMenu(Browser* browser)
    : browser_(browser),
      selected_menu_model_(NULL),
      selected_index_(0) {
}

WrenchMenu::~WrenchMenu() {
}

void WrenchMenu::Init(menus::MenuModel* model) {
  DCHECK(!root_.get());
  root_.reset(new MenuItemView(this));
  root_->set_has_icons(true);  // We have checks, radios and icons, set this
                               // so we get the taller menu style.
  int next_id = 1;
  PopulateMenu(root_.get(), model, &next_id);
}

void WrenchMenu::RunMenu(views::MenuButton* host) {
  gfx::Point screen_loc;
  views::View::ConvertPointToScreen(host, &screen_loc);
  // Subtract 1 from the height to make the popup flush with the button border.
  gfx::Rect bounds(screen_loc.x(), screen_loc.y(), host->width(),
                   host->height() - 1);
  root_->RunMenuAt(host->GetWindow()->GetNativeWindow(), host, bounds,
                   MenuItemView::TOPRIGHT, true);
  if (selected_menu_model_)
    selected_menu_model_->ActivatedAt(selected_index_);
}

bool WrenchMenu::IsItemChecked(int id) const {
  const Entry& entry = id_to_entry_.find(id)->second;
  return entry.first->IsItemCheckedAt(entry.second);
}

bool WrenchMenu::IsCommandEnabled(int id) const {
  if (id == 0)
    return false;  // The root item.

  const Entry& entry = id_to_entry_.find(id)->second;
  int command_id = entry.first->GetCommandIdAt(entry.second);
  // The items representing the cut (cut/copy/paste) and zoom menu
  // (increment/decrement/reset) are always enabled. The child views of these
  // items enabled state updates appropriately.
  return command_id == IDC_CUT || command_id == IDC_ZOOM_MINUS ||
      entry.first->IsEnabledAt(entry.second);
}

void WrenchMenu::ExecuteCommand(int id) {
  const Entry& entry = id_to_entry_.find(id)->second;
  int command_id = entry.first->GetCommandIdAt(entry.second);

  if (command_id == IDC_CUT || command_id == IDC_ZOOM_MINUS) {
    // These items are represented by child views. If ExecuteCommand is invoked
    // it means the user clicked on the area around the buttons and we should
    // not do anyting.
    return;
  }

  return entry.first->ActivatedAt(entry.second);
}

bool WrenchMenu::GetAccelerator(int id, views::Accelerator* accelerator) {
  const Entry& entry = id_to_entry_.find(id)->second;
  int command_id = entry.first->GetCommandIdAt(entry.second);
  if (command_id == IDC_CUT || command_id == IDC_ZOOM_MINUS) {
    // These have special child views; don't show the accelerator for them.
    return false;
  }

  menus::Accelerator menu_accelerator;
  if (!entry.first->GetAcceleratorAt(entry.second, &menu_accelerator))
    return false;

  *accelerator = views::Accelerator(menu_accelerator.GetKeyCode(),
                                    menu_accelerator.modifiers());
  return true;
}

void WrenchMenu::PopulateMenu(MenuItemView* parent,
                              MenuModel* model,
                              int* next_id) {
  int index_offset = model->GetFirstItemIndex(NULL);
  for (int i = 0, max = model->GetItemCount(); i < max; ++i) {
    int index = i + index_offset;

    MenuItemView* item =
        AppendMenuItem(parent, model, index, model->GetTypeAt(index), next_id);

    if (model->GetTypeAt(index) == MenuModel::TYPE_SUBMENU)
      PopulateMenu(item, model->GetSubmenuModelAt(index), next_id);

    if (model->GetCommandIdAt(index) == IDC_CUT) {
      DCHECK_EQ(MenuModel::TYPE_COMMAND, model->GetTypeAt(index));
      DCHECK_LT(i + 2, max);
      DCHECK_EQ(IDC_COPY, model->GetCommandIdAt(index + 1));
      DCHECK_EQ(IDC_PASTE, model->GetCommandIdAt(index + 2));
      item->SetTitle(l10n_util::GetString(IDS_EDIT2));
      item->AddChildView(
          new CutCopyPasteView(this, model, index, index + 1, index + 2));
      i += 2;
    } else if (model->GetCommandIdAt(index) == IDC_ZOOM_MINUS) {
      DCHECK_EQ(MenuModel::TYPE_COMMAND, model->GetTypeAt(index));
      DCHECK_EQ(IDC_ZOOM_PLUS, model->GetCommandIdAt(index + 1));
      DCHECK_EQ(IDC_FULLSCREEN, model->GetCommandIdAt(index + 2));
      item->SetTitle(l10n_util::GetString(IDS_ZOOM_MENU2));
      item->AddChildView(
          new ZoomView(this, model, index, index + 1, index + 2));
      i += 2;
    }
  }
}

MenuItemView* WrenchMenu::AppendMenuItem(MenuItemView* parent,
                                         MenuModel* model,
                                         int index,
                                         MenuModel::ItemType menu_type,
                                         int* next_id) {
  int id = (*next_id)++;
  SkBitmap icon;
  std::wstring label;
  MenuItemView::Type type;
  switch (menu_type) {
    case MenuModel::TYPE_COMMAND:
      model->GetIconAt(index, &icon);
      type = MenuItemView::NORMAL;
      label = UTF16ToWide(model->GetLabelAt(index));
      break;
    case MenuModel::TYPE_CHECK:
      type = MenuItemView::CHECKBOX;
      label = UTF16ToWide(model->GetLabelAt(index));
      break;
    case MenuModel::TYPE_RADIO:
      type = MenuItemView::RADIO;
      label = UTF16ToWide(model->GetLabelAt(index));
      break;
    case MenuModel::TYPE_SEPARATOR:
      type = MenuItemView::SEPARATOR;
      break;
    case MenuModel::TYPE_SUBMENU:
      type = MenuItemView::SUBMENU;
      label = UTF16ToWide(model->GetLabelAt(index));
      break;
    default:
      NOTREACHED();
      type = MenuItemView::NORMAL;
      break;
  }

  id_to_entry_[id].first = model;
  id_to_entry_[id].second = index;

  MenuItemView* menu_item = parent->AppendMenuItemImpl(id, label, icon, type);

  if (menu_type == MenuModel::TYPE_COMMAND && model->HasIcons()) {
    SkBitmap icon;
    if (model->GetIconAt(index, &icon))
      menu_item->SetIcon(icon);
  }

  return menu_item;
}

void WrenchMenu::CancelAndEvaluate(MenuModel* model, int index) {
  selected_menu_model_ = model;
  selected_index_ = index;
  root_->Cancel();
}
