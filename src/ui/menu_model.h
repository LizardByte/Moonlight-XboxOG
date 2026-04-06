#pragma once

// standard includes
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

// local includes
#include "src/input/navigation_input.h"

namespace ui {

  /**
   * @brief Item shown in a focus-driven menu.
   */
  struct MenuItem {
    std::string id;
    std::string label;
    bool enabled;
  };

  /**
   * @brief Result of applying a UI command to a menu.
   */
  struct MenuUpdate {
    bool selectionChanged;
    bool activationRequested;
    bool backRequested;
    bool previousPageRequested;
    bool nextPageRequested;
    bool overlayToggleRequested;
    std::string activatedItemId;
  };

  /**
   * @brief Menu state that supports controller and keyboard navigation.
   */
  class MenuModel {
  public:
    static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

    /**
     * @brief Construct a menu from a list of items.
     *
     * @param items Menu items in display order.
     */
    explicit MenuModel(std::vector<MenuItem> items = {});

    /**
     * @brief Replace the menu items and select the first enabled entry.
     *
     * @param items Menu items in display order.
     */
    void set_items(std::vector<MenuItem> items);

    /**
     * @brief Return the configured items.
     */
    const std::vector<MenuItem> &items() const;

    /**
     * @brief Return the selected item index or npos when none is selectable.
     */
    std::size_t selected_index() const;

    /**
     * @brief Return the selected item or nullptr when none is selectable.
     */
    const MenuItem *selected_item() const;

    /**
     * @brief Select a specific enabled item by its stable identifier.
     *
     * @param itemId Identifier to select.
     * @return true when the selection changed.
     */
    bool select_item_by_id(const std::string &itemId);

    /**
     * @brief Apply a UI command to the menu.
     *
     * @param command Command from controller or keyboard input.
     * @return Details about selection changes and passthrough actions.
     */
    MenuUpdate handle_command(input::UiCommand command);

  private:
    bool move_selection(int direction);
    std::size_t find_first_enabled_index() const;

    std::vector<MenuItem> items_;
    std::size_t selectedIndex_;
  };

}  // namespace ui
