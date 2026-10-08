# Privacy in Chroma

Updated October 8, 2026.

Chroma is a desktop Minecraft launcher derived from Prism Launcher. It does not add a Chroma account service, analytics SDK, or telemetry endpoint. Its launcher integrations contact the services needed for the features you use.

## Files on your computer

Chroma stores launcher preferences, instance metadata, downloaded files, and logs in the selected profile. Microsoft account names, identifiers, and authentication tokens are saved locally using Prism's account format so the launcher can refresh your login. Protect that profile as account data; do not publish it or include it in a bug report.

The Windows installer normally uses `%APPDATA%\Chroma` for data. The portable edition uses its portable folder. If you choose an existing Prism profile, Chroma reads and changes that same profile directly, including its accounts and instances. Chroma's appearance preferences use `chroma-ui.cfg`, and `profile.json` remembers the selected folder. [Profile details](docs/CHROMA-MIGRATION.md).

## Network connections

- **Account login and game access:** Microsoft, Xbox, and Minecraft services handle authentication, entitlements, profile information, and game downloads. Enter your password on Microsoft's sign-in page. Chroma receives the tokens needed by its authentication flow; it does not need your Microsoft password.
- **Launcher and game metadata:** inherited Prism services provide launcher news, game metadata, and some downloads. Java and loader downloads contact their configured providers.
- **Modpack discovery and installation:** the service you select receives catalog searches and download requests. This may include Modrinth, CurseForge, ATLauncher, Technic, FTB, or links supplied by a pack. Those services receive ordinary connection information such as your IP address and request metadata.
- **Links and support:** opening a project link uses your browser. Issues and files you choose to submit to GitHub are handled by GitHub and may be public. Chroma does not automatically upload your logs to its repository.
- **Discord activity (enabled by default):** Chroma connects to your local Discord desktop app to show **Playing Chroma**, with **Browsing for modpacks** while idle or your running instance's name, Minecraft and modpack versions, public modpack artwork, and session start time. Discord displays this according to your activity privacy settings. Existing packs may need a catalog metadata request to find their artwork; Discord fetches the public image URL. Local/custom images are not uploaded. Chroma does not request your Discord login, tokens, friends, or messages. Turn sharing off in **Settings → General → Discord Activity**, then select **Save** to clear Chroma's activity and disconnect. [Discord activity details](docs/DISCORD-PRESENCE.md).

Each external service has its own privacy policy. Modpacks, mods, Java, and Minecraft run separately and may make their own network connections.

## Microsoft application identity

Chroma retains Prism Launcher's public Microsoft OAuth client ID for compatibility with the existing login flow. Microsoft may display **Prism Launcher** on its consent page. Chroma is an independent project, and this does not imply Prism or Microsoft endorses Chroma. The ID identifies an application; it is not a password or private API secret. You can supply a different registered application ID in **Settings → Services**.

## Your controls

You can remove accounts through the launcher, choose a different profile, or delete profile files yourself after closing the launcher. Uninstalling Chroma preserves profile data, including any shared Prism folder. To revoke application access, use the controls provided by your Microsoft account. Review screenshots and logs before sharing them.

For questions about Chroma's behavior, [open an issue](https://github.com/nikitasokolov01/chroma/issues) without including passwords, authentication tokens, or private profile files.
