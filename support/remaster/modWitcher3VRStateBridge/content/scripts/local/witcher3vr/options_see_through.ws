// Witcher3VR options see-through signal (Remastered 5.01 only).
//
// While the in-game graphics or display options page is open, dxgi.dll keeps
// rendering the world in stereo behind the menu, so changes are visible as
// they are made ([openxr] options_menu_see_through=1). The menu itself is then
// drawn on the HUD plane over the live world.
//
// The signal is a sentinel value passed to theGame.SetHDRMenuFadePercentage.
// On PC that native evaluates its argument and ignores it; dxgi.dll intercepts
// the two sentinel values. Without the DLL, or with the key off, these calls
// do nothing. The game's own calls (0 and 1 around the HDR page) still run.

function W3VR_OptionsSeeThroughSignal(enable: bool) {
  if (enable) {
    theGame.SetHDRMenuFadePercentage(7701.0f);
  } else {
    theGame.SetHDRMenuFadePercentage(7700.0f);
  }
}

// Submenu ids are the last part of the option group's displayName:
// "video.general" (Graphics) and "video.display" (Display).
function W3VR_IsSeeThroughOptionsPage(id: string): bool {
  return id == "general" || id == "display" || id == "postprocess";
}

@wrapMethod(CR4IngameMenu)
function OnShowOptionSubmenu(actionType: int, menuTag: int, id: string): void {
  wrappedMethod(actionType, menuTag, id);
  // Only in-game: behind the start screen's options there is no world.
  W3VR_OptionsSeeThroughSignal(!isMainMenu && W3VR_IsSeeThroughOptionsPage(id));
}

@wrapMethod(CR4IngameMenu)
function OnOptionPanelNavigateBack() {
  var result: bool;

  // Before the original, which may close and reopen the menu to apply
  // graphics changes.
  W3VR_OptionsSeeThroughSignal(false);
  result = wrappedMethod();
  return result;
}

@wrapMethod(CR4IngameMenu)
function OnNavigatedBack() {
  var result: bool;

  W3VR_OptionsSeeThroughSignal(false);
  result = wrappedMethod();
  return result;
}

@wrapMethod(CR4IngameMenu)
function OnClosingMenu() {
  var result: bool;

  W3VR_OptionsSeeThroughSignal(false);
  result = wrappedMethod();
  return result;
}
