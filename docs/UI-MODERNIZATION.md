# UI modernization

## Starting point

The work starts from the existing Chroma light/dark clay redesign, including
the user's uncommitted revision saved in its own baseline commit. No framework
change is needed: the application uses C++ and Qt Widgets, native QActions,
QMenu, stacked inline pages, Qt item models, and the existing Prism task system.

## Implementation order

1. Extend Clay design tokens and shared form, menu, tab, and scrollbar styling.
2. Add shared floating-menu behavior while preserving Qt keyboard and popup handling.
3. Match native Windows chrome to the theme; retain reliable platform controls.
4. Improve scrolling, navigation restoration, and reduced-motion behavior.
5. Extend IndexedPack/IndexedVersion metadata and the shared project details UI
   for Modrinth and CurseForge, using their existing authenticated network layer.
6. Add account profile information to the existing account management page.
7. Extend the existing native OpenGL skin viewer with reliable live texture updates.
8. Add a texture document and editor with PNG import/export, history, UV regions,
   and synchronized preview.
9. Apply skins through the existing Minecraft Services upload and account refresh.
10. Run focused and application-wide regression checks and save phase commits.

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
