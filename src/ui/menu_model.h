/**
 * @file src/ui/menu_model.h
 * @brief Declares menu model structures and helpers.
 */
#pragma once

// standard includes
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

// local includes
#include "src/input/navigation_input.h"

namespace ui {

  /**
   * @brief Item shown in a focus-driven menu.
   */
  struct MenuItem {
    std::string id;  ///< Stable identifier used by reducers and view builders.
    std::string label;  ///< User-facing label shown in the menu.
    std::string description;  ///< Helper copy explaining what the item changes or activates.
    bool enabled;  ///< True when the item can be selected and activated.
  };

  /**
   * @brief Result of applying a UI command to a menu.
   */
  struct MenuUpdate {
    bool selectionChanged;  ///< True when the focused item changed.
    bool activationRequested;  ///< True when the selected item should be activated.
    bool backRequested;  ///< True when the caller should navigate back.
    bool previousPageRequested;  ///< True when the caller should move to the previous page.
    bool nextPageRequested;  ///< True when the caller should move to the next page.
    bool overlayToggleRequested;  ///< True when the caller should toggle the diagnostics overlay.
    std::string activatedItemId;  ///< Stable identifier for the activated item, when any.
  };

  /**
   * @brief Menu state that supports controller and keyboard navigation.
   */
  class MenuModel {
  public:
    /**
     * @brief Sentinel index used when no menu item is selectable.
     */
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
     *
     * @return Immutable view of the menu items in display order.
     */
    [[nodiscard]] const std::vector<MenuItem> &items() const;

    /**
     * @brief Return the selected item index or npos when none is selectable.
     *
     * @return Selected item index or npos.
     */
    [[nodiscard]] std::size_t selected_index() const;

    /**
     * @brief Return the selected item or nullptr when none is selectable.
     *
     * @return Pointer to the selected item, or nullptr when unavailable.
     */
    [[nodiscard]] const MenuItem *selected_item() const;

    /**
     * @brief Select a specific enabled item by its stable identifier.
     *
     * @param itemId Identifier to select.
     * @return true when the selection changed.
     */
    bool select_item_by_id(std::string_view itemId);

    /**
     * @brief Apply a UI command to the menu.
     *
     * @param command Command from controller or keyboard input.
     * @return Details about selection changes and passthrough actions.
     */
    MenuUpdate handle_command(input::UiCommand command);

  private:
    bool move_selection(int direction);
    [[nodiscard]] std::size_t find_first_enabled_index() const;

    std::vector<MenuItem> items_;
    std::size_t selectedIndex_ = npos;
  };

}  // namespace ui
