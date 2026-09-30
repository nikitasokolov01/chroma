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

Open the **Effects** tab under the color controls. Choose **Hue**, **Brightness**,
**Grayscale**, or **Invert**, set the amount where applicable, and select
**Apply**. Each application is one undo step.

Effects apply to the enabled layers of the body parts shown in the preview.
Hide a part or layer to protect it. If you also have a marquee, only selected
pixels within that visible region change. Alpha is preserved, and fully
transparent pixels are left alone.

## Reference skins

Import a second PNG as a reference without replacing the editing skin. The
reference has its own Classic/Slim setting and can be viewed in 3D or as a 2D
texture. Use its color picker and marquee to sample colors or copy pixels.
Painting and effects always belong to the editing skin.

Select an area on the reference, copy it, then paste into the editing view.
Pasting is undoable. Hide the reference panel whenever you want more canvas
space; the imported reference stays available during the editing session. In
small windows, use **Editing / Reference** to switch between canvases. Choosing
Paste returns you to the editing skin.

![Editing a skin beside a separate reference](screenshots/skin-reference-development.png)

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

The development build passed 34 test suites, 47 native launcher UI checks,
47 UI checks using the fallback renderer, and eight updater preservation checks.
Coverage includes selected-pixel copy/paste, visible-part effects, Shift cleanup,
Classic/Slim conversion, undo, and saving then applying extras in another session.
The screenshots above use synthetic skins and an isolated test profile.
