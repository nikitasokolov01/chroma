<h1 align="center">Chroma</h1>

<p align="center"><strong>Minecraft instances, modpacks, and skins in one native launcher.</strong><br>
Built on Prism Launcher with C++, Qt Widgets, and customizable light and dark clay themes.</p>

<p align="center">
  <a href="https://github.com/nikitasokolov01/chroma/releases/tag/v1.0.0">Download for Windows</a> ·
  <a href="docs/CHROMA.md">Guide</a> ·
  <a href="docs/CHROMA-MIGRATION.md">Use your Prism library</a> ·
  <a href="https://github.com/nikitasokolov01/chroma/issues">Report an issue</a>
</p>

![Chroma v1 in its dark clay theme, with a card library and window controls in the top-right corner](docs/screenshots/chroma-v1-header-dark.png)

Chroma rebuilds Prism Launcher's default interface around a card library, a persistent sidebar, and screens that open inside the main window. It keeps Prism's instance management and installation workflows underneath, with a visual direction inspired by Modrinth.

**Independent project:** Chroma is based on Prism Launcher 10.0.5. It is not affiliated with or endorsed by Prism Launcher, Modrinth, Mojang, or Microsoft.

## Download

**v1.0.0 · First stable release · Windows x64**

| Package | Use it when… |
| --- | --- |
| [Windows installer](https://github.com/nikitasokolov01/chroma/releases/download/v1.0.0/Chroma-1.0.0-Windows-x64-Setup.exe) | You want a normal installation for your Windows account. |
| [Portable ZIP](https://github.com/nikitasokolov01/chroma/releases/download/v1.0.0/Chroma-1.0.0-Windows-x64.zip) | You want to extract a folder and run `chroma.exe` from it. Keep its files together. |

Read the [v1.0.0 release notes](docs/releases/1.0.0.md) for requirements and verification results. The [release page](https://github.com/nikitasokolov01/chroma/releases/tag/v1.0.0) also includes checksums and corresponding source archives.

**Already using 0.3.0?** Use **Chroma header menu → Check for updates…**, or install the new package over your existing installation. From 0.2.0 or earlier, download v1.0.0 manually. Your launcher profile holds your instances, worlds, skins, Extras, and outfits; keep it when replacing a portable installation. See [updating Chroma](docs/CHROMA.md#updating-chroma).

## New in v1.0.0

Version 1 expands Skin Studio into a complete local skin workspace and brings
update notices and themed window controls to the launcher.

- **Outfit presets.** Save named looks with their Classic/Slim model and optional owned cape choice. Preview them locally, then apply a complete outfit to the selected account.
- **Mirror painting.** Mirror the head and torso, pair arms, or pair legs independently. Brush, eraser, and texture strokes work in 2D and 3D, respect hidden parts and selections, and undo together.
- **Palette swapping.** Compare large, synchronized **Current** and **Preview** 3D models. Recolor grouped shades for the whole skin or a chosen body part and layer while retaining highlights and shadows. The floating color picker keeps you in the editor.
- **Texture brushes.** Add **Fine grain**, **Fabric**, or **Hair** shading from the colors already on your skin, with adjustable strength and support for mirrored strokes.
- **Reusable skin pieces.** Save helmets, clothing, or whole layers in **Skin Extras**. Import a reference skin, sample colors, and copy selected pieces across. Bucket fill, marquee selections, Shift painting through the outer layer, and live hue/brightness adjustments round out the tools.
- **Updates in the app.** A persistent notice highlights a new release; **What's new** shows its features without leaving your work behind. Download and install when you choose.
- **A matching Windows header.** Flat minimize, maximize/restore, and close controls sit in the top-right corner above the navigation, with support for resizing, snapping, and fullscreen transitions.

### Recolor and save your look

| Palette swapping | Outfit presets |
| --- | --- |
| [![Current and Preview 3D skins with grouped color families and body-part and layer selectors](docs/screenshots/v1-palette-swap.png)](docs/screenshots/v1-palette-swap.png) | [![A named Night outfit in Skin Library with Save, Rename, Delete, and Apply outfit controls](docs/screenshots/v1-outfit-presets.png)](docs/screenshots/v1-outfit-presets.png) |

The palette picker opens beside its control. Changes stay in the preview until
you select **Apply swap**, and each swap is undoable.

<p align="center"><img src="docs/screenshots/v1-palette-picker.png" alt="Floating color wheel with a hex field for previewing a palette replacement" width="230"></p>

### Paint details and reuse them

| Texture brush | Skin Extras |
| --- | --- |
| [![Fabric texture brush with adjustable strength in Skin Studio](docs/screenshots/v1-texture-brush.png)](docs/screenshots/v1-texture-brush.png) | [![A named reusable skin piece in the Skin Extras inventory](docs/screenshots/v1-skin-extras.png)](docs/screenshots/v1-skin-extras.png) |

| Mirror painting | Reference skins |
| --- | --- |
| [![A mirrored head stroke with independent Head and torso, Pair arms, and Pair legs controls](docs/screenshots/v1-mirror.png)](docs/screenshots/v1-mirror.png) | [![Selecting pixels from a separate reference skin beside the editing texture and live 3D preview](docs/screenshots/v1-reference-skin.png)](docs/screenshots/v1-reference-skin.png) |

Read the [Skin Studio tools guide](docs/SKIN-STUDIO-TOOLS.md) for mirror controls,
palette scopes, reference skins, copy/paste, cape switching, and outfit management.

### Review an update before installing

[![In-app update notice and What's new page, demonstrated with a synthetic 99.0.0 release](docs/screenshots/chroma-update-notes-fixture.png)](docs/screenshots/chroma-update-notes-fixture.png)

*Update UI demo: 99.0.0 is a synthetic test release. The stable release is v1.0.0.*
Checks do not download or install updates. Chroma verifies the package checksum
before offering **Install and restart**. Configure checks in
**Settings → Launcher → Updater**.

## Skin Studio

Paint directly on a Classic or Slim 3D model, or switch to the 2D texture beside
a live preview. Both views share the same skin, brush, color, and undo history.

- **Pick colors in place.** The color wheel, hex value, and opacity stay visible beside the canvas.
- **See each pixel.** The 3D grid follows both body and outer surfaces, including transparent outer pixels. The Grid control works in both editing views.
- **Choose what is visible.** Use **Body** and **Outer layer** to show each layer; click the head, torso, arms, or legs in the body diagram to hide parts and reach surfaces behind them. Space toggles a focused part.
- **Paint the visible outer layer.** When **Outer layer** is on, painting targets it. Hold **Shift** to work on the body beneath it, or hide the outer layer. The same rule applies in 2D.
- **Keep editing flexible.** Use brush, eraser, color picker, undo/redo, PNG import/export, and local library saving. Base pixels stay opaque; outer layers support transparency.

Open **Skins** in the sidebar, choose a skin, then select **Edit Skin…**. In 3D,
left-drag paints, right-drag rotates, and scrolling zooms. Switch **3D Paint →
2D Texture** to work on individual pixels. With a canvas focused, use **B** for
brush, **E** for eraser, **I** for color picker, and **H** to rotate or pan.

Editing and exporting work without an account. **Apply Skin** requires a Microsoft
account with a Minecraft Java profile. Devices without OpenGL use the 2D editor
and image preview.

## More features

**In the next update:** optional [Discord activity](docs/DISCORD-PRESENCE.md) shows **Playing Chroma**, with **Browsing for modpacks** or the running pack's name and public artwork. Disable it in **Settings → General → Discord Activity**. This feature is not included in the v1.0.0 download above.

- **Organize instances.** Search grouped cards, open recent instances, or drag cards into a saved manual order. Name and last-played sorting remain available.
- **Pin from the library.** Drop an instance onto the sidebar to pin it, then open its settings with one click.
- **Manage skins from the sidebar.** Open **Skins**, switch accounts with the dropdown, and manage skins and capes without changing your default launch account. Local editing works before adding an account.
- **Browse modpacks and project details.** Browse Modrinth, CurseForge, ATLauncher, Technic, and FTB Legacy. Modrinth and CurseForge share **About**, **Gallery**, and **Releases** views with cached project information for offline use. Available services depend on the build's API configuration.
- **Work inside one window.** Instance editing, settings, accounts, and installation flows open inline. Smooth scrolling and restored scroll positions help with navigation; clickable cards and controls use hand cursors.
- **Handle larger libraries.** Card painting visits visible rows, geometry is cached, and progress updates avoid full layouts. The library background stays still while idle. Regression coverage includes a 4,000-instance library.
- **Choose your appearance.** Use light or dark clay themes and preset or custom accents. Custom Windows controls follow the selected theme.
- **Use your existing Prism profile.** Open instances, worlds, accounts, and custom instance locations directly, without copying modpacks.

Launcher commands now live in the **Chroma header menu**. Click **Chroma** at the
top of the window for update checks, profile selection, accounts, and instance
actions. The sidebar keeps navigation and pins together.

### Light and dark themes

| Light | Dark |
| --- | --- |
| [![Chroma v1 home in the light clay theme with custom corner controls](docs/screenshots/chroma-v1-header-light.png)](docs/screenshots/chroma-v1-header-light.png) | [![Chroma v1 home in the dark clay theme with custom corner controls](docs/screenshots/chroma-v1-header-dark.png)](docs/screenshots/chroma-v1-header-dark.png) |

<details>
<summary>More screenshots: modpack browsing, settings, and compact layouts</summary>

### Browse, choose, install

| Choose a service | Explore a pack |
| --- | --- |
| [![Modpack provider gallery](docs/screenshots/provider-gallery.png)](docs/screenshots/provider-gallery.png) | [![Cover grid and selected modpack's version and details](docs/screenshots/modpack-details.png)](docs/screenshots/modpack-details.png) |

[![Shared project information with About, Gallery, and Releases tabs and an offline cache notice](docs/screenshots/project-details.png)](docs/screenshots/project-details.png)

### Familiar controls, inside the new interface

| Launcher settings | Compact layout |
| --- | --- |
| [![Settings open inside Chroma's main window](docs/screenshots/inline-settings.png)](docs/screenshots/inline-settings.png) | [![Modpack cards in a narrow Chroma window](docs/screenshots/modpack-catalog-compact.png)](docs/screenshots/modpack-catalog-compact.png) |

</details>

Screenshots show the native Windows application with synthetic instances,
accounts, skins, and catalog entries; they do not show a signed-in user's profile.
The v1 feature and header images show the updated interface. The additional
browsing and settings images illustrate workflows carried forward from the previews.

## Already use Prism?

1. Close Prism and any games using its profile.
2. In Chroma, choose **Chroma header menu → Use Prism folder…**.
3. Select the data folder containing `prismlauncher.cfg`, inspect it, and choose **Use this folder**.

Chroma restarts once and remembers that folder. Instances, worlds, accounts, and launch settings are shared directly; changes affect the same files. Keep one launcher open at a time. Chroma's appearance preferences are stored separately in `chroma-ui.cfg`.

See the [existing-profile guide](docs/CHROMA-MIGRATION.md) for portable profiles, custom folders, and `--dir` overrides.

## Documentation and development

- [Getting started, accounts, building, and testing](docs/CHROMA.md)
- [Skin Studio tools, outfits, and reusable pieces](docs/SKIN-STUDIO-TOOLS.md)
- [Appearance and accent colors](docs/appearance.md)
- [Using an existing Prism folder](docs/CHROMA-MIGRATION.md)
- [UI features, implementation notes, and verification results](docs/UI-MODERNIZATION.md)
- [Privacy and local account data](PRIVACY.md)

The interface is implemented in C++ and Qt Widgets and compiled into the launcher. Development builds use the scripts in [`scripts/`](scripts/); prerequisites and native UI tests are covered in the [build guide](docs/CHROMA.md#build-from-source).

Found a problem? [Open an issue](https://github.com/nikitasokolov01/chroma/issues) with the Chroma version, steps to reproduce, and a screenshot when helpful. Review logs before sharing them and remove account information or access tokens.

## Credits and license

Chroma builds on the work of the [Prism Launcher](https://github.com/PrismLauncher/PrismLauncher), PolyMC, and MultiMC contributors. Prism's original project information is preserved in the [upstream README](docs/UPSTREAM_README.md).

Launcher code is licensed under **GPL-3.0-only**; see [LICENSE](LICENSE). Third-party components retain their own licenses and notices in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), [COPYING.md](COPYING.md), and their source directories. Upstream logos and branding retain their [CC BY-SA 4.0 license](program_info/LICENSE). See the [release licensing notes](docs/RELEASE-LICENSING.md) for distribution and corresponding source information.
