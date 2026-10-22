// Witcher3VR pause menu see-through and main menu signals (Remastered 5.01 only).
//
// While the in-game pause menu (its main list and its settings pages) is open,
// dxgi.dll keeps rendering the world in stereo behind the menu, so the world
// stays visible and graphics changes show as they are made
// ([openxr] options_menu_see_through=1). The menu itself is then drawn on the
// HUD plane over the live world. Inventory, map and the other panels are not
// part of this menu and keep their normal presentation.
//
// The signal is a sentinel value passed to theGame.SetHDRMenuFadePercentage.
// On PC that native evaluates its argument and ignores it; dxgi.dll intercepts
// the two sentinel values. Without the DLL, or with the key off, these calls
// do nothing. The game's own calls (0 and 1 around the HDR page) still run.

// The start screen's main menu (isMainMenu) is shown or closed. dxgi.dll uses
// it to treat the main menu reached by quitting a game like the startup
// frontend (world screen, loading background).
function W3VR_MainMenuSignal(shown: bool) {
  if (shown) {
    theGame.SetHDRMenuFadePercentage(7703.0f);
  } else {
    theGame.SetHDRMenuFadePercentage(7702.0f);
  }
}

function W3VR_OptionsSeeThroughSignal(enable: bool) {
  if (enable) {
    theGame.SetHDRMenuFadePercentage(7701.0f);
  } else {
    theGame.SetHDRMenuFadePercentage(7700.0f);
  }
}

// Every pause menu settings page except the HDR calibration page, which fades
// the screen itself. Submenu ids are the last part of the option group's
// displayName, for example "general" (Graphics) or "display" (Display).
function W3VR_IsSeeThroughOptionsPage(id: string): bool {
  return id != "settings_hdr";
}

// Opening the pause menu. Only in-game: behind the start screen's menu there
// is no world.
@wrapMethod(CR4IngameMenu)
function OnConfigUI() {
  var result: bool;

  result = wrappedMethod();
  W3VR_OptionsSeeThroughSignal(!isMainMenu);
  W3VR_MainMenuSignal(isMainMenu);
  return result;
}

@wrapMethod(CR4IngameMenu)
function OnShowOptionSubmenu(actionType: int, menuTag: int, id: string): void {
  wrappedMethod(actionType, menuTag, id);
  W3VR_OptionsSeeThroughSignal(!isMainMenu && W3VR_IsSeeThroughOptionsPage(id));
}

@wrapMethod(CR4IngameMenu)
function OnOptionPanelNavigateBack() {
  var result: bool;
  var reopening: bool;

  // The original closes and reopens the menu when a setting needs a viewport
  // refresh; the reopened menu signals again from OnConfigUI.
  reopening = inGameConfigBufferedWrapper.AnyBufferedVarHasTag('refreshViewport');
  W3VR_OptionsSeeThroughSignal(false);
  result = wrappedMethod();
  if (!reopening) {
    W3VR_OptionsSeeThroughSignal(!isMainMenu);
  }
  return result;
}

// Back from a settings page to the pause menu's main list.
@wrapMethod(CR4IngameMenu)
function OnNavigatedBack() {
  var result: bool;

  result = wrappedMethod();
  W3VR_OptionsSeeThroughSignal(!isMainMenu);
  return result;
}

@wrapMethod(CR4IngameMenu)
function OnClosingMenu() {
  var result: bool;

  W3VR_OptionsSeeThroughSignal(false);
  if (isMainMenu) {
    W3VR_MainMenuSignal(false);
  }
  result = wrappedMethod();
  return result;
}
