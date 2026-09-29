# UI modernization

## Starting point

The work starts from the existing Chroma light/dark clay redesign, including
the user's uncommitted revision saved in its own baseline commit. No framework
change is needed: the application uses C++ and Qt Widgets, native QActions,
QMenu, stacked inline pages, Qt item models, and the existing Prism task system.

## Implemented features

### Shared UI and native chrome

ClayStyle supplies shared spacing, radii, motion timings, form controls, menus,
tabs, and scrollbars. ClayWidgets retains Qt action handling, keyboard navigation,
accessibility, and split-menu hit testing. FloatingUi adds bounded menu placement
and a short reveal; Qt still handles popup focus and dismissal.

On supported Windows versions, WindowChrome applies the current palette to the
native caption, text, and border. It preserves native window controls, resizing,
maximizing, and snapping. High contrast restores system caption colors. Unsupported
attributes and other platforms retain their existing native frame behavior.

SmoothScroll animates discrete vertical wheel input and stops on manual scrollbar
changes, keyboard input, clicks, hiding, or range changes. Trackpad pixel scrolling
uses Qt's normal handling. Home and Library retain separate scroll positions;
recent cards retain focus during unrelated model updates, and nested inline pages
restore focus when returning to the previous page.

Motion follows Qt's UI-effects preference and Windows client-area animation
settings. `CHROMA_REDUCED_MOTION=1` disables decorative and interaction animation.
The library background uses static gradients so an idle window does not repeatedly
repaint all visible cards. Buttons and menus retain their short transitions.

### Project details

The shared ProjectDescriptionPage presents **About**, **Gallery**, and **Releases**
for Modrinth and CurseForge. It shows descriptions, available provider metadata,
links, images, release changes, and dependencies. Missing fields are omitted.
Release inspection uses the existing version selector when that release is an
allowed installation choice; provider install and update tasks remain in place.

Open project information from:

- **Add instance or modpack**: choose Modrinth or CurseForge, then select a pack.
- An instance's resource download browser: select a mod or other resource.
- An instance's **Mods** page: select an installed mod and choose **Project details**.
  Mods without provider metadata show the available local information.
- An installed managed pack's provider page in the instance editor.

Public project metadata, release lists, descriptions, and requested changelogs use
the existing provider cache directories. JSON is fresh for 15 minutes; valid saved
data can be used when the provider is unavailable, with a notice for project
information. Images use the existing HTTP(S) image cache and reserve space while
loading. Failed images have a placeholder. Provider content cannot use the text
browser's local-file loader. Request cancellation and page lifetime guards prevent
superseded or closed views from receiving stale completions.

### Instance library and navigation

Drag cards before or after another card to save a manual order. The sort selector
switches to **Manual**; **Name** and **Last played** remain available. Stable instance
IDs are saved in `instgroups.json`, and filtered-out instances retain their order.
Dragging between groups keeps the existing group-move behavior. Drop a card onto
the sidebar to pin it; repeated drops keep one pin and preserve the instance.

Instance layout builds group and card geometry in one pass. Hit testing uses saved
geometry, painting visits visible rows, and progress changes avoid full layouts.
Metadata summaries refresh only for affected rows. Enabled cards, buttons, and
selectors use a hand cursor. Alt no longer reveals the hidden legacy menu bar;
the sidebar launcher menu keeps those commands available. Decorative slogans have
been removed.

### Accounts and Skin Studio

The Accounts page shows the selected profile's avatar, name, UUID, account state,
and actions to use the account, copy its UUID, or customize its skin.

Open **Skins** from the sidebar, choose an account from the dropdown, then select
or import a skin and choose **Edit Skin…** (or double-click it). This opens
**Skin Studio** inline. Switching the skin target leaves the launcher's default
account unchanged. Local editing is also available before adding an account.

Skin Studio opens in **3D Paint** when OpenGL is available. Left-drag paints the
model, right-drag rotates it, and scrolling zooms. **2D Texture** shows the PNG
canvas beside the live preview. Both modes share the texture document, brush,
color, model, and undo history. The native renderer maps picked faces to Classic
or Slim texture regions. The body-part table exposes independent base and outer
visibility for the head, torso, arms, and legs; hiding parts changes the view and
which surfaces can be reached, without altering the PNG.

An inline hue wheel and saturation/value square stay visible beside the canvas,
with hexadecimal color entry, opacity, and palette swatches. Palette, body-part,
and model controls scroll beneath the fixed color controls at compact sizes.
The wheel also supports keyboard hue, brightness, and saturation changes. It does
not open a color dialog.

The editor retains PNG import/export, brush and eraser, color picking, brush size,
body-region and layer selection, 2D grid, zoom/pan, undo/redo, reset, and local
library saving. Imports preserve edit history. Legacy 64 × 32 skins normalize to
64 × 64. Base pixels remain opaque; outer layers support transparency. In 2D,
arrow keys move the pixel cursor, Space paints, and plus/minus zoom. Canvas
shortcuts are B for brush, E for eraser, I for color picker, and H for pan/rotate.
Alt-click picks a color. Tooltips and accessible names describe each tool.

Texture changes are uploaded with the GL context current during painting. A
QOpenGLWidget framebuffer keeps the preview inside Qt's regular composition and
scrolling hierarchy. It resets GL state on each paint and restores edited textures
when inline reparenting replaces a context. If OpenGL or shader initialization is
unavailable, the editor selects 2D and shows a front/back image preview.

The main window prepares Qt's OpenGL composition before its native window is
created. A hidden, zero-size anchor keeps the first inline preview from replacing
the launcher window after it is shown. The anchor does not paint or initialize
its own GL context; headless and unsupported platforms keep the normal fallback.

**Apply Skin** saves locally, validates a fixed PNG snapshot, refreshes sign-in when
needed, and uses the existing Minecraft Services upload. The same bytes update the
account preview after success. Applying requires a Microsoft account with a Java
profile and is unavailable during sign-in or while Minecraft uses the account.
Offline accounts can edit and export locally. Failures leave the saved skin
available for retry.

## Architecture decisions

- Extend ClayStyle and ClayWidgets instead of adding another theme or UI framework.
- Reuse ModPlatform's provider abstraction and normalize only fields providers expose.
- Preserve provider install tasks, local management, account storage, and launch paths.
- Reuse the GPL native skin renderer; no additional 3D dependency is required.
- Qt 6.5 lacks the newer expanded client-area APIs. Native Windows caption colors
  provide integration without introducing frameless resizing and snap regressions.
- Use isolated synthetic profiles for UI checks. Live sign-in, game launch, and skin
  application require an authorized real account and are reported separately.

## References

- [Chroma and Modrinth screenshot comparison](https://www.figma.com/design/g1nzgpz0INt4jHrzsUoBo1/modrinth-vs-chroma?node-id=0-1)
- [Modrinth app changelog](https://modrinth.com/news/changelog/?filter=app)
- [Modrinth API](https://docs.modrinth.com/api/)
- [CurseForge API](https://docs.curseforge.com/rest-api/)

## Baseline verification

- Native Windows build: passed.
- CTest: 24/24 passed.
- Existing screenshot references were inspected; their visual language remains the base.

## Windows verification commands

Run from the repository root with the dependencies described in
[CHROMA.md](CHROMA.md#build-from-source). The helper defaults to the local Qt 6.5.3/MSVC
toolchain under `.tools`; pass dependency path overrides if needed.

```powershell
. .\scripts\build-chroma.ps1 -Action Environment
.\scripts\build-chroma.ps1 -Action All
cmake --build .tools/build --target ChromaUiSmoke --parallel 6
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$QtRoot/plugins/platforms"
$env:QT_QPA_PLATFORM = 'windows'
$env:CHROMA_UI_TEST_ROOT = "$PWD/.chroma-test/ui-modernization"
& .\.tools\build\ChromaUiSmoke.exe
Remove-Item Env:QT_QPA_PLATFORM, Env:QT_QPA_PLATFORM_PLUGIN_PATH, Env:CHROMA_UI_TEST_ROOT
```

`-Action All` configures, builds, and runs CTest. The optional ChromaUiSmoke target
opens the real Qt main window with synthetic accounts, instances, and provider
fixtures, and writes screenshots under its isolated test root. It is excluded
from the default build and CTest. Use `QT_QPA_PLATFORM=offscreen` for headless
layout checks; native caption, window-state, and OpenGL behavior need a native run.

Focused tests are registered as `WindowChrome`, `ProjectMetadata`,
`SkinTextureDocument`, `SkinPreview`, and `InstanceView`:

```powershell
ctest --test-dir .tools/build --output-on-failure -R '^(WindowChrome|ProjectMetadata|SkinTextureDocument|SkinPreview|InstanceView)$'
```

The new smoke coverage includes popup keyboard behavior and placement, native
window flags, focus and scroll restoration, repeated wheel input and interruption,
reduced motion, provider metadata/cache cancellation, account selection, and skin
editor history/preview synchronization. Skin mode and color checks use a patterned
synthetic texture to expose face mapping and transparent layer mistakes. They also
check independent part visibility, color-wheel/hex/opacity synchronization, and
compact panel bounds. Final run results are recorded separately from the baseline
above.

## Initial modernization verification — 2026-09-28

- Windows Qt 6.5.3/MSVC build: passed, including the launcher and test targets.
- CTest: **27/27 suites passed**. Project metadata has 11 passing cases; skin
  documents/application preflight have 12 passing cases.
- Native Windows UI: **35/35 cases passed**, including popup placement and keyboard
  handling, compact catalogs, separate Home/Library scroll restoration, settings,
  account selection, offline cache fallback/cancellation, and progress cancellation.
- Offscreen UI: **35/35 cases passed**, including compact-layout and fallback-preview
  checks. Changed C++ code passes clang-format; the complete diff passes whitespace checks.
- Classic/Slim OpenGL frames and body-part visibility were compared before buffer
  swap. Native child windows do not appear in QWidget screenshots, so the renderer
  is verified with separate framebuffer captures.
- Light/dark and 1280 × 820 / 680 × 640 screenshots were inspected. Skin Studio
  preserves fixed save/apply controls while its editing area scrolls on small windows.
- The synthetic profile intentionally refuses network requests. Expected offline
  service messages and Qt native capture/geometry warnings remain in the logs;
  the assertions and renderer checks pass.

Local verification logs are under `.tools/modernization-*-build.log`,
`.tools/modernization-final-tests.log`, and `.tools/modernization-*-ui.txt`.
Screenshots and synthetic profiles are under `.chroma-test/`; neither directory
is part of the committed source.

Run the development build with `./scripts/run-chroma.ps1` from the repository
root. The script supplies the local Qt runtime paths. The existing redesign was
preserved in a separate baseline checkpoint before the feature commits.

## Skin and library refinement verification — 2026-09-28

- Windows Qt 6.5.3/MSVC build: passed.
- CTest: **29/29 suites passed**, including the new SkinPreview and InstanceView suites.
- Offscreen UI and native Windows UI: **41/41 cases passed in each run**.
- Native preview coverage keeps the status bar enabled while resizing and scrolling,
  checks sibling pixels, resets deliberately altered GL state, and restores edited
  texture data after context recreation. Native screenshots include the preview.
- The 4,000-instance regression checks direct geometry lookup without model reads,
  fewer than 100 card paints at 1920 × 1080, and no relayout for a progress update.
  An idle canvas no longer schedules continuous repaints.
- Account switching, local editing without an account, compact tool icons and key
  selection, Alt behavior, sidebar pin drops, persisted manual order, filtering,
  group transfers, and creation after reordering are covered by UI workflows.
- Native skin screenshots at 1280 × 820 and 680 × 640 were reviewed. Changed C++
  code is formatted and the diff passes whitespace checks.

Logs are under `.tools/ui-refinements-*.log` and `.tools/ui-refinements-*.txt`.
Screenshots and synthetic profiles are under `.chroma-test/ui-refinements-*`.

## Skin Studio 3D painting verification — 2026-09-28

- Windows Qt 6.5.3/MSVC build: passed.
- CTest: **31/31 suites passed**, including SkinPicking and OpenGLComposition.
- Native Windows UI: **42/42 cases passed** after the compact control adjustments.
- Offscreen UI: **42/42 cases passed**, including fallback editing and compact
  controls without horizontal overflow.
- Geometry tests cover 144 Classic/Slim part, layer, and face combinations, plus
  explicit face orientation, arm width, hidden layers, transparent outer pixels,
  and occlusion by another visible body part.
- Native renderer checks cover painting and undo per stroke, erasing and color
  picking, rotation, interrupted strokes, and visible base/outer colors. The
  first and repeated inline preview insertions preserve the launcher window and
  render through Qt's composition.
- UI workflows cover the default 3D mode, 2D switching, shared texture/history,
  the inline wheel, hex and opacity synchronization, independent part visibility,
  and a fixed color panel while the controls below it scroll.
- The native 1280 × 820 and 680 × 640 captures were inspected, including the 2D
  texture with its live preview. Screenshots use original synthetic skin artwork.

Logs are under `.tools/skin-studio-*.log` and `.tools/skin-studio-*.txt`.
Screenshots and isolated profiles are under `.chroma-test/skin-studio-3d-*`.

### Checks not executed here

This work did not perform real Microsoft authentication, a real skin upload, a
live Minecraft launch/session, or live Modrinth/CurseForge installations. These require
separate checks with the appropriate account, service credentials, and game data.
macOS and Linux builds and interactive behavior were not executed here. Synthetic
Windows UI tests do not establish success for those live or platform-specific
flows.
