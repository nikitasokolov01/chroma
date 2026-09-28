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
Background drift runs only while its window is visible, active, and not minimized.

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

### Accounts and Skin Studio

The Accounts page shows the selected profile's avatar, name, UUID, account state,
and actions to use the account, copy its UUID, or customize its skin.

To open the editor, go to **Settings → Accounts**, select an account, choose
**Customize Skin…**, select or import a skin in **Skin Library**, then choose
**Edit Skin…** or double-click the skin. This opens **Skin Studio** inline.

Skin Studio provides PNG import/export, brush and eraser, color picking, brush
size, body-region and layer selection, grid, zoom/pan, undo/redo, reset, and local
library saving. Imports preserve edit history. Classic and Slim use their own UV
regions; legacy 64 × 32 skins normalize to 64 × 64. Base-layer pixels remain
opaque, while outer layers support transparency. Keyboard users can move the pixel
cursor with arrow keys, paint with Space, and zoom with plus/minus.

The existing native OpenGL renderer supports orbit, zoom, view reset, body-part
visibility, and base/outer-layer visibility. Texture changes are uploaded with the
GL context current during painting. If OpenGL or shader initialization is
unavailable, a front/back image preview keeps editing and export usable.

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

Focused tests are registered as `WindowChrome`, `ProjectMetadata`, and
`SkinTextureDocument`:

```powershell
ctest --test-dir .tools/build --output-on-failure -R '^(WindowChrome|ProjectMetadata|SkinTextureDocument)$'
```

The new smoke coverage includes popup keyboard behavior and placement, native
window flags, focus and scroll restoration, repeated wheel input and interruption,
reduced motion, provider metadata/cache cancellation, account selection, and skin
editor history/preview synchronization. Final run results are recorded separately
from the baseline above.

## Final verification — 2026-09-28

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
root. The script supplies the local Qt runtime paths. The feature branch is
`codex/prism-ui-modernization`; the existing redesign was preserved in a separate
baseline checkpoint before the feature commits.

### Checks not executed here

This work did not perform real Microsoft authentication, a real skin upload, a
live Minecraft launch/session, or live Modrinth/CurseForge installations. These require
separate checks with the appropriate account, service credentials, and game data.
macOS and Linux builds and interactive behavior were not executed here. Synthetic
Windows UI tests do not establish success for those live or platform-specific
flows.
