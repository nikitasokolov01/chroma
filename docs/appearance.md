# Appearance

Chroma is the default appearance when a profile first opens in Chroma. It uses
soft lavender backgrounds, charcoal text, layered clay surfaces, and a violet
accent (`#7c3aed`). **Chroma Dark** uses the same clay surfaces and typography
with deep violet backgrounds and light text. Later launches keep your saved
appearance, including when you use an existing Prism folder.

Preview [Chroma Dark on Home](screenshots/dark-home.png) and its
[compact layout](screenshots/dark-compact.png).

## Choose a theme and accent

1. Open **Settings → Launcher → Appearance**.
2. Select **Chroma** or **Chroma Dark** in the **Theme** menu.
3. Choose **Violet (Default)**, **Lavender**, **Sky Blue**, **Mint**, **Rose**, or **Amber** in
   the **Accent** menu. Select **Custom Color...** to choose another color.

Choosing either Chroma theme pairs the standard Breeze icons with its light or
dark palette. Custom and other icon themes keep your selected icons.

Changes are saved and displayed immediately, including before closing Settings.
Restarting the launcher preserves the selected accent. Choose **Violet
(Default)** to restore the original color. These controls are also available
during initial setup.

The accent applies to selection highlights and launcher controls that use the
application palette. Text on accent backgrounds switches between black and
white for readability. Link and focus colors are adjusted for contrast on the
current background. Painted button gradients also retain readable labels when
you choose a very light or dark accent.

## Scope

Chroma and Chroma Dark supply the colors for the redesigned native launcher
interface. The existing Dark, Bright, System, and custom appearances remain
available. Accent controls work with either Chroma theme; other appearances
use their own palettes. Modpack artwork and Minecraft's in-game appearance are unaffected.

The preference is stored as `AccentColor=#RRGGBB` in `chroma-ui.cfg`, beside the
active profile's `prismlauncher.cfg`. `ApplicationTheme=chroma` selects Chroma;
`ApplicationTheme=chroma-dark` selects Chroma Dark.
Malformed values, named colors, and colors containing an alpha channel fall
back to violet. Previously saved lavender and custom accents are preserved.

Chroma's interface preferences are separate from Prism's appearance settings.
When you [use an existing Prism folder](CHROMA-MIGRATION.md), the launch settings,
accounts, instances, and worlds are shared; the Chroma palette and window layout
remain in `chroma-ui.cfg`.

## Home and the inline workspace

Home scrolls as one page, from **Jump back in** through the complete library.
The header and sidebar stay in place. Settings, instance editing, account
management, and instance creation open in the main workspace using the same
palette. **Back** returns to the previous screen; **Home** and **Library** return
to the library after the current pages finish their normal close checks.

Select an instance and choose **Pin to sidebar** in its details panel or card
context menu. Click its sidebar icon to open its editor. Use **Unpin from sidebar**
in the same controls, or right-click the pinned icon, to remove it. The pin list
scrolls independently when needed, and is saved as `ChromaPinnedInstances` in
`chroma-ui.cfg`.

The details panel hides in narrower windows to leave room for the library;
instance context menus retain the same actions. Settings and instance pages
switch to a compact page selector when their navigation list would take too
much space.

## Clay surfaces and motion

Home uses a large recent-instance card beside two smaller cards on wide windows.
The same cards stack vertically on compact windows. Buttons lift on hover and
compress when pressed; search uses a recessed surface. Keyboard focus has an
explicit outline. Nunito headings and DM Sans interface text are bundled with
the launcher and work offline.

Decorative background colors drift slowly while Home is visible. On Windows,
motion follows **Accessibility → Visual effects → Animation effects**. The
cross-platform environment variable `CHROMA_REDUCED_MOTION=1` also disables
decorative drift, hover lift, and press scaling. Static depth and color remain.
Other platforms use Qt's UI effects preference as their default.

## Implementation

`ui/themes/ClayStyle` centralizes colors, radii, text styles, motion preferences,
and the four lighting layers used to paint surfaces. `ui/widgets/ClayWidgets`
provides reusable native buttons, inputs, panels, and the ambient canvas.
`InstanceDelegate` uses the same surface painter for library cards. Qt style
sheets supply typography and the standard controls used in existing settings
pages; custom painting supplies depth and interaction feedback on Home. These
are native Qt equivalents of the supplied web design tokens, including bounded
painted lighting in place of CSS blur and box shadows.
