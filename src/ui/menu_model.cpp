// class header include
#include "src/ui/menu_model.h"

// standard includes
#include <utility>

namespace ui {

  MenuModel::MenuModel(std::vector<MenuItem> items)
    : selectedIndex_(npos) {
    set_items(std::move(items));
  }

  void MenuModel::set_items(std::vector<MenuItem> items) {
    items_ = std::move(items);
    selectedIndex_ = find_first_enabled_index();
  }

  const std::vector<MenuItem> &MenuModel::items() const {
    return items_;
  }

  std::size_t MenuModel::selected_index() const {
    return selectedIndex_;
  }

  const MenuItem *MenuModel::selected_item() const {
    if (selectedIndex_ == npos || selectedIndex_ >= items_.size()) {
      return nullptr;
    }

    return &items_[selectedIndex_];
  }

  bool MenuModel::select_item_by_id(const std::string &itemId) {
    for (std::size_t index = 0; index < items_.size(); ++index) {
      if (items_[index].enabled && items_[index].id == itemId) {
        const bool changed = index != selectedIndex_;
        selectedIndex_ = index;
        return changed;
      }
    }

    return false;
  }

  MenuUpdate MenuModel::handle_command(input::UiCommand command) {
    MenuUpdate update {};

    switch (command) {
      case input::UiCommand::move_up:
        update.selectionChanged = move_selection(-1);
        break;
      case input::UiCommand::move_down:
        update.selectionChanged = move_selection(1);
        break;
      case input::UiCommand::activate:
      case input::UiCommand::confirm:
        if (const MenuItem *item = selected_item(); item != nullptr && item->enabled) {
          update.activationRequested = true;
          update.activatedItemId = item->id;
        }
        break;
      case input::UiCommand::back:
        update.backRequested = true;
        break;
      case input::UiCommand::previous_page:
        update.previousPageRequested = true;
        break;
      case input::UiCommand::next_page:
        update.nextPageRequested = true;
        break;
      case input::UiCommand::toggle_overlay:
        update.overlayToggleRequested = true;
        break;
      case input::UiCommand::none:
      case input::UiCommand::delete_character:
      case input::UiCommand::move_left:
      case input::UiCommand::move_right:
        break;
    }

    return update;
  }

  bool MenuModel::move_selection(int direction) {
    if (items_.empty() || selectedIndex_ == npos) {
      return false;
    }

    const std::size_t itemCount = items_.size();
    std::size_t candidateIndex = selectedIndex_;

    for (std::size_t visited = 0; visited < itemCount; ++visited) {
      candidateIndex = direction < 0
        ? (candidateIndex + itemCount - 1) % itemCount
        : (candidateIndex + 1) % itemCount;

      if (items_[candidateIndex].enabled) {
        const bool changed = candidateIndex != selectedIndex_;
        selectedIndex_ = candidateIndex;
        return changed;
      }
    }

    return false;
  }

  std::size_t MenuModel::find_first_enabled_index() const {
    for (std::size_t index = 0; index < items_.size(); ++index) {
      if (items_[index].enabled) {
        return index;
      }
    }

    return npos;
  }

}  // namespace ui
