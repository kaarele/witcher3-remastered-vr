// modW3VRKeepHudFrameScale - keep the Witcher 3 VR HUD frame scale.
//
// The VR DLL writes witcher3vr.ini hud_horizontal_scale / hud_vertical_scale
// into dx12user.settings [Hidden] uiHorizontalFrameScale / uiVerticalFrameScale
// at every start. The vanilla "Rescale" HUD screen (Options > Video) clamps
// both values to 0.9..1.0 (UIRescaleModule MIN_H_SCALE / MIN_V_SCALE = 0.9 in
// panel_ingamemenu.redswf and panel_uirescale.redswf) and sends
// OnUpdateRescale(0.9, 0.9) as soon as it opens. That moves the HUD back
// toward the corners and is saved when the menu closes.
//
// These wrappers always call the original code, but pass in the stored VR
// value instead whenever that value is below 0.9 (a value the vanilla screen
// cannot represent). If the stored value is 0.9 or more, vanilla behaviour is
// unchanged. Delete this folder to uninstall.

function W3VRKeepHud_StoredScale(varName : name) : float
{
  var cfg : CInGameConfigWrapper;

  cfg = (CInGameConfigWrapper)theGame.GetInGameConfigWrapper();
  if (!cfg)
  {
    return 0.0f;
  }
  return StringToFloat(cfg.GetVarValue('Hidden', varName));
}

function W3VRKeepHud_IsVRScale(value : float) : bool
{
  return value > 0.2f && value < 0.895f;
}

@wrapMethod(CR4IngameMenu)
function OnUpdateRescale(hScale : float, vScale : float)
{
  var result : bool;
  var keepH : float;
  var keepV : float;

  keepH = W3VRKeepHud_StoredScale('uiHorizontalFrameScale');
  keepV = W3VRKeepHud_StoredScale('uiVerticalFrameScale');
  if (W3VRKeepHud_IsVRScale(keepH) || W3VRKeepHud_IsVRScale(keepV))
  {
    hScale = keepH;
    vScale = keepV;
  }

  result = wrappedMethod(hScale, vScale);
  return result;
}

@wrapMethod(CR4UIRescaleMenu)
function UpdateRescale(frameScaleX : float, frameScaleY : float, scale : float, opacity : float)
{
  var keepH : float;
  var keepV : float;

  keepH = W3VRKeepHud_StoredScale('uiHorizontalFrameScale');
  keepV = W3VRKeepHud_StoredScale('uiVerticalFrameScale');
  if (W3VRKeepHud_IsVRScale(keepH) || W3VRKeepHud_IsVRScale(keepV))
  {
    frameScaleX = keepH;
    frameScaleY = keepV;
  }

  wrappedMethod(frameScaleX, frameScaleY, scale, opacity);
}
