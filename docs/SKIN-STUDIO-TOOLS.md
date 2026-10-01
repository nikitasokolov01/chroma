# Skin Studio tools in development

These changes are on `codex/skin-studio-tools`. They are not included in the
published 0.3.0 installer.

## Paint, fill, and select

- **Brush (B)** paints individual pixels; **Eraser (E)** clears outer pixels.
- **Bucket (G)** fills connected pixels of the same color. In 3D it stays on the
  clicked face; in 2D it follows the active texture regions. A selection limits
  the fill.
- **Marquee (M)** selects a rectangle. On the 3D model, the selection includes
  visible surface pixels inside that rectangle. On the texture, it selects
  pixels directly. Selected pixels stay highlighted when you change tools.
- **Color picker (I)** samples a pixel. Alt-click also picks a color.
- **Pan / Rotate (H)** moves the texture or rotates the model. Right-drag rotates
  the 3D model regardless of the selected tool.

While **Outer layer** is visible, tools target it. Hold **Shift** to temporarily
fade the outer layer and work on the body beneath it. Release Shift to return to
the outer layer. Hidden body parts and a hidden body layer remain protected.
The preview fade does not change the PNG's opacity.

An active marquee limits brush strokes, fills, pastes, and effects. Clear the
selection before editing the whole visible region again. Copy with **Ctrl+C**;
**Ctrl+V** prepares the copied pixels for placement. Click the model or texture
to place them, or press **Escape** to cancel placement.

## Effects

Open the **Effects** tab under the color controls. **Hue** and **Brightness**
update the skin as you move the slider or change the number. Every adjustment
uses the same starting colors: changing hue from 40° to 50° applies 50° from
the starting skin. It does not add another 50° to the previous preview.

Hue and brightness remember their separate values when you switch effects.
Set an adjustment to **0** to remove it. Both sliders share one undo step,
and **Redo** restores their values. Painting, pasting, changing the selection,
or changing visible parts or layers finishes the adjustment; further slider
changes start from the resulting skin.

**Grayscale** and **Invert** use **Apply effect**. Each application is one undo
step.

![Live hue adjustment with its remembered value](screenshots/skin-effects-development.png)

Effects apply to the enabled layers of the body parts shown in the preview.
Hide a part or layer to protect it. If you also have a marquee, only selected
pixels within that visible region change. Alpha is preserved, and fully
transparent pixels are left alone.

## Reference skins

Import a second PNG as a reference without replacing the editing skin. The
reference has its own Classic/Slim setting and can be viewed in 3D or as a 2D
texture. The toolbar selects the tool for both views. Use **Color picker** and
**Marquee** on either skin to sample colors or copy pixels. Painting and effects
belong to the editing skin; the reference remains read-only.

The reference has its own body-part diagram and **Body / Outer layer** buttons.
These hide reference parts or layers without changing the editing skin's
visibility controls.

Select an area on the reference, copy it, then paste into the editing view.
Pasting is undoable. Hide the reference panel whenever you want more canvas
space; the imported reference stays available during the editing session. In
small windows, use **Editing / Reference** to switch between canvases. Choosing
Paste returns you to the editing skin.

![Editing a skin beside a separate reference](screenshots/skin-reference-development.png)

## Owned capes

Open **Skins**, choose a Microsoft account, and select one of its owned capes
from the cape dropdown. The preview shows the selected cape. Choose
**Apply cape** to equip it without uploading or changing your skin. Select
**No Cape** and apply it to remove the equipped cape.

![Owned cape selection and Apply cape in Skin Library](screenshots/skin-capes-development.png)

Cape changes require an eligible Minecraft Java account and are unavailable
while it is signing in or running Minecraft. Local skin editing remains
available without an account.

## Skin Extras

The **Extras** tab holds reusable pieces across editing sessions.

![A named helmet saved in Skin Extras](screenshots/skin-extras-development.png)

1. Open **Save new extra** and choose **Editing skin** or **Reference skin** as the source.
2. Choose the part and layer to save. For a helmet, choose **Head** and
   **Outer layer**. For a full jelly coating, choose **Whole skin** and
   **Outer layer**. A marquee further limits what is saved.
3. Enter a name and select **Save extra**.
4. In a later design, select that extra in the inventory and choose **Apply**.

Extras return to their original body parts. Classic and Slim arm faces are
mapped when the saved and current models differ. Your current marquee limits
where an extra can be applied; clear it to apply the entire saved piece.

Transparent pixels preserve existing details by default, so you can combine
pieces. Enable **Replace transparency** to clear the corresponding outer pixels
as well. Applying an extra is one undo step. Deleting an inventory entry asks
for confirmation and does not change the skin being edited.

Extras are stored in `skin-extras` inside the active launcher profile. They stay
local and are preserved by launcher updates. Back up this folder with the rest
of your profile.

## Verification

The development build passed 34 test suites, 48 native launcher UI checks,
48 UI checks using the fallback renderer, and eight updater preservation checks.
Coverage includes selected-pixel copy/paste, visible-part effects, Shift cleanup,
live adjustment history, independent reference visibility, Classic/Slim
conversion, undo, and saving then applying extras in another session. Cape
requests use a simulated account and network to check equipping, removing, and
refused changes without altering a real account.
The screenshots above use synthetic skins and an isolated test profile.
