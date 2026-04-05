// class header include
#include "src/ui/shell_view.h"

// standard includes
#include <algorithm>
#include <cstddef>

namespace {

  std::string screen_title(app::ScreenId screen) {
	switch (screen) {
	  case app::ScreenId::home:
			return "Xbox";
	  case app::ScreenId::hosts:
		return "Hosts";
	  case app::ScreenId::add_host:
		return "Add Host";
	  case app::ScreenId::pair_host:
		return "Pair Host";
	  case app::ScreenId::settings:
		return "Settings";
	}

		return "Xbox";
  }

	std::string active_add_host_field_label(const app::ClientState &state) {
	  return state.addHostDraft.activeField == app::AddHostField::address ? "Address" : "Port";
	}

	std::string keypad_value(const app::ClientState &state) {
	  if (state.addHostDraft.keypad.visible) {
		return state.addHostDraft.keypad.stagedInput.empty() && state.addHostDraft.activeField == app::AddHostField::port
		  ? "default (47984)"
		  : state.addHostDraft.keypad.stagedInput;
	  }

	  return state.addHostDraft.activeField == app::AddHostField::address
		? state.addHostDraft.addressInput
		: (state.addHostDraft.portInput.empty() ? "default (47984)" : state.addHostDraft.portInput);
	}

	std::vector<ui::ShellModalButton> keypad_buttons(const app::ClientState &state) {
	  const std::vector<std::string> labels = state.addHostDraft.activeField == app::AddHostField::address
		? std::vector<std::string> {"1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0"}
		: std::vector<std::string> {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};

	  std::vector<ui::ShellModalButton> buttons;
	  buttons.reserve(labels.size());
	  for (std::size_t index = 0; index < labels.size(); ++index) {
		buttons.push_back({
		  labels[index],
		  true,
		  state.addHostDraft.keypad.visible && index == state.addHostDraft.keypad.selectedButtonIndex,
		});
	  }

	  return buttons;
	}

	std::vector<std::string> keypad_modal_lines(const app::ClientState &state) {
	  std::vector<std::string> lines = {
		std::string("Editing field: ") + active_add_host_field_label(state),
		std::string("Staged value: ") + keypad_value(state),
		"Use the D-pad to choose a key, A to enter it, X to delete, Start to accept, and B to cancel.",
	  };

	  if (state.addHostDraft.activeField == app::AddHostField::address) {
		lines.emplace_back("Enter a dotted IPv4 address such as 192.168.0.10.");
	  }
	  else {
		lines.emplace_back("Enter digits for a custom TCP port, or leave it empty to keep the default.");
	  }

	  return lines;
	}

  std::vector<std::string> screen_body_lines(const app::ClientState &state) {
	switch (state.activeScreen) {
	  case app::ScreenId::home:
		{
		  std::vector<std::string> lines = {
			"Hello, Moonlight!",
			"Saved hosts available: " + std::to_string(state.hosts.size()),
		  };

		  if (!state.statusMessage.empty()) {
			lines.push_back("Status: " + state.statusMessage);
		  }

		  return lines;
		}
	  case app::ScreenId::hosts:
		{
		  std::vector<std::string> lines;
		  if (state.hosts.empty()) {
			lines = {
			  "No saved hosts yet.",
			  "Select Add Host to create a manual IPv4 entry.",
			  "Discovery and real host-driven pairing will build on this saved-host list next.",
			};
		  }
		  else {
			lines.push_back("Saved hosts: " + std::to_string(state.hosts.size()));
			if (const app::HostRecord *host = app::selected_host(state); host != nullptr) {
			  lines.push_back("Selected host: " + host->displayName);
					lines.push_back("Address: " + host->address);
					lines.push_back("Port: " + std::to_string(app::effective_host_port(host->port)));
			  lines.push_back(std::string("Pairing: ") + (host->pairingState == app::PairingState::paired ? "Paired" : "Not paired yet"));
											lines.emplace_back(host->pairingState == app::PairingState::paired
				? "This host is already paired."
						: "Select Pair Selected Host to start the pairing handshake in the background.");
			}
			else {
			  lines.emplace_back("Select a saved host to inspect its address and pairing state.");
			}
		  }

		  if (!state.statusMessage.empty()) {
			lines.push_back("Status: " + state.statusMessage);
		  }

		  return lines;
		}
	  case app::ScreenId::add_host:
		{
		  std::vector<std::string> lines = {
			"Manual host entry.",
			std::string("Current address: ") + app::current_add_host_address(state),
			std::string("Current port: ") + std::to_string(app::current_add_host_port(state)),
			std::string("Selected field: ") + active_add_host_field_label(state),
		  };

		  if (!state.addHostDraft.validationMessage.empty()) {
			lines.push_back("Validation: " + state.addHostDraft.validationMessage);
		  }

		  if (!state.addHostDraft.connectionMessage.empty()) {
			lines.push_back("Connection: " + state.addHostDraft.connectionMessage);
		  }

		  return lines;
		}
	  case app::ScreenId::pair_host:
		{
		  std::vector<std::string> lines = {
			std::string("Target host: ") + state.pairingDraft.targetAddress,
			std::string("Target port: ") + std::to_string(state.pairingDraft.targetPort),
			std::string("PIN: ") + app::current_pairing_pin(state),
			"Enter the PIN on the host if prompted and wait for the status below.",
			"Cancel leaves this screen, but the active network request may need a few seconds to unwind.",
		  };

		  if (!state.pairingDraft.statusMessage.empty()) {
			lines.push_back("Status: " + state.pairingDraft.statusMessage);
		  }

		  return lines;
		}
	  case app::ScreenId::settings:
		return {
		  "Display, input, overlay, and logging settings land here next.",
		  "The renderer already supports a log overlay toggle.",
		  "Use Back to return to the home screen.",
		};
	}

	return {};
  }

  std::vector<std::string> footer_lines(const app::ClientState &state) {
	std::vector<std::string> lines = {
	  "D-pad / Arrows: move",
	  "A / Enter: select",
	  "B / Esc: back",
	  "Y / F3: toggle overlay",
	  "Black/White/LT/RT / PgUp/PgDn: scroll logs",
	};

	if (state.activeScreen == app::ScreenId::add_host) {
	  lines.insert(lines.begin() + 1, state.addHostDraft.keypad.visible
		? "Keypad open: D-pad moves, A enters, X deletes, Start accepts, B cancels"
		: "Select Address or Port to open the keypad modal");
	}

	return lines;
  }

}  // namespace

namespace ui {

  ShellViewModel build_shell_view_model(
	const app::ClientState &state,
	const std::vector<logging::LogEntry> &logEntries,
	const std::vector<std::string> &statsLines
  ) {
	ShellViewModel viewModel {};
	viewModel.title = screen_title(state.activeScreen);
	viewModel.bodyLines = screen_body_lines(state);
	viewModel.footerLines = footer_lines(state);
	viewModel.overlayVisible = state.overlayVisible;
	viewModel.overlayTitle = "Diagnostics";
	viewModel.keypadModalVisible = state.activeScreen == app::ScreenId::add_host && state.addHostDraft.keypad.visible;
	viewModel.keypadModalTitle = state.addHostDraft.activeField == app::AddHostField::address ? "Address Keypad" : "Port Keypad";
	viewModel.keypadModalColumnCount = 3;

	const ui::MenuItem *selectedItem = state.menu.selected_item();
	for (const MenuItem &item : state.menu.items()) {
	  viewModel.menuRows.push_back({
		item.id,
		item.label,
		item.enabled,
		selectedItem != nullptr && item.id == selectedItem->id,
	  });
	}

	if (viewModel.keypadModalVisible) {
	  viewModel.keypadModalLines = keypad_modal_lines(state);
	  viewModel.keypadModalButtons = keypad_buttons(state);
	}

	if (viewModel.overlayVisible) {
	  if (!statsLines.empty()) {
		viewModel.overlayLines.insert(viewModel.overlayLines.end(), statsLines.begin(), statsLines.end());
	  }
	  else {
		viewModel.overlayLines.emplace_back("No active stream");
	  }

	  const std::size_t logLineLimit = 10;
	  const std::size_t availableLogCount = logEntries.size();
	  const std::size_t maxOffset = availableLogCount > logLineLimit ? availableLogCount - logLineLimit : 0;
	  const std::size_t clampedOffset = std::min(state.overlayScrollOffset, maxOffset);
	  const std::size_t startIndex = availableLogCount > logLineLimit ? availableLogCount - logLineLimit - clampedOffset : 0;
	  const std::size_t endIndex = std::min(availableLogCount, startIndex + logLineLimit);
	  for (std::size_t index = startIndex; index < endIndex; ++index) {
		viewModel.overlayLines.push_back(logging::format_entry(logEntries[index]));
	  }

	  if (clampedOffset > 0) {
		viewModel.overlayLines.insert(viewModel.overlayLines.begin(), "Showing earlier log entries");
	  }
	}

	return viewModel;
  }

}  // namespace ui
