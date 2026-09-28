# Chroma interface fonts

Static TrueType files are bundled for offline use and compatibility with Qt 6.5.
The unmodified fonts come from the official Google Fonts and Google Fonts
upstream repositories. Local filenames are shortened for Qt resource paths.

Nunito supplies headings and numbers; DM Sans supplies interface text.
Each family is distributed under the SIL Open Font License 1.1, included
alongside the fonts and in the application resources.

These static versions expose their true family names to Qt 6.5's Windows font
backend. They avoid the family-name resolution problems found when testing
generated CDN font files with this runtime.

## Exact sources

- [Nunito-700.ttf](https://raw.githubusercontent.com/google/fonts/c7e2740188205a85323c7385547f6f59b4f2245a/ofl/nunito/Nunito-Bold.ttf)
- [Nunito-800.ttf](https://raw.githubusercontent.com/google/fonts/c7e2740188205a85323c7385547f6f59b4f2245a/ofl/nunito/Nunito-ExtraBold.ttf)
- [Nunito-900.ttf](https://raw.githubusercontent.com/google/fonts/c7e2740188205a85323c7385547f6f59b4f2245a/ofl/nunito/Nunito-Black.ttf)
- [DMSans-400.ttf](https://raw.githubusercontent.com/googlefonts/dm-fonts/main/Sans/Exports/DMSans-Regular.ttf)
- [DMSans-500.ttf](https://raw.githubusercontent.com/googlefonts/dm-fonts/main/Sans/Exports/DMSans-Medium.ttf)
- [DMSans-700.ttf](https://raw.githubusercontent.com/googlefonts/dm-fonts/main/Sans/Exports/DMSans-Bold.ttf)
- [Nunito-OFL.txt](https://raw.githubusercontent.com/google/fonts/c7e2740188205a85323c7385547f6f59b4f2245a/ofl/nunito/OFL.txt)
- [DMSans-OFL.txt](https://raw.githubusercontent.com/googlefonts/dm-fonts/main/Sans/OFL.txt)
