# Appearance

Chroma is the default appearance when a profile first opens in Chroma. It uses
charcoal backgrounds, light text, and a lavender accent (`#b7a5f5`). Later launches
keep your saved Chroma appearance, including when you use an existing Prism folder.

## Change the accent

1. Open **Settings → Launcher → Appearance**.
2. Select **Chroma** in the **Theme** menu.
3. Choose **Lavender (Default)**, **Sky Blue**, **Mint**, **Rose**, or **Amber** in
   the **Accent** menu. Select **Custom Color...** to choose another color.

Changes are saved and displayed immediately, including before closing Settings.
Restarting the launcher preserves the selected accent. Choose **Lavender
(Default)** to restore the original color. These controls are also available
during initial setup.

The accent applies to selection highlights and launcher controls that use the
application palette. Text on accent backgrounds switches between black and
white for readability. Very dark accent colors are brightened for links so the
links remain readable against the background.

## Scope

Chroma supplies the colors for the redesigned native launcher interface. The
existing Dark, Bright, System, and custom appearances remain available. Accent
controls are enabled when Chroma is selected; other appearances use their own
palettes. Modpack artwork and Minecraft's in-game appearance are unaffected.

The preference is stored as `AccentColor=#RRGGBB` in `chroma-ui.cfg`, beside the
active profile's `prismlauncher.cfg`. `ApplicationTheme=chroma` selects Chroma.
Malformed values, named colors, and colors containing an alpha channel fall
back to lavender.

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
