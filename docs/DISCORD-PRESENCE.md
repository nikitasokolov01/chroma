# Discord activity

Chroma can share a Rich Presence activity with the Discord desktop app. It is enabled by default; open **Settings → General → Discord Activity**, change **Show Chroma activity on Discord**, and choose **Save** to enable or disable it. Disabling clears Chroma's activity and stops reconnecting. No Discord login, bot token, or account token is needed in Chroma.

The activity shows **Playing Chroma**. The expanded profile shows:

- **Browsing for modpacks** while no Minecraft instance is running, including during launch preparation.
- The running instance's name, elapsed play time, and public modpack artwork when available.
- The most recently started instance when several games are running. When it exits, the previous running instance resumes; after the last game exits, browsing returns.

Keep the Discord desktop app open and allow activity sharing in Discord. Chroma connects automatically when Discord becomes available and reconnects after it restarts. The status in General settings describes the connection. Discord's own activity privacy settings determine who can see it.

## Artwork

Catalog imports retain public artwork from Modrinth, CurseForge, ATLauncher, and Technic. Existing managed Modrinth and CurseForge instances can resolve artwork from their catalog metadata; ATLauncher artwork is derived from its pack ID. A Chroma image is used when catalog artwork is unavailable. Custom local icons are not uploaded or shared.

## Application identity and builds

The public Discord application ID is `1557805260255400097`. Its application name in the [Discord Developer Portal](https://discord.com/developers/applications) should remain **Chroma**: Discord uses that name for the activity. Forks can use their own application by setting the CMake cache variable `Launcher_DISCORD_APPLICATION_ID`; an empty value leaves the integration unconfigured. This ID is public, not a secret or bot token.

Chroma uses local Discord IPC and the `SET_ACTIVITY` command. Artwork uses public HTTPS image URLs supported by [Discord Rich Presence](https://discord.com/developers/docs/rich-presence/overview). The `DiscordRpcClient`, `DiscordPresence`, and `DiscordArtwork` tests cover transport, lifecycle, and artwork handling; UI smoke tests use an empty application ID so fixture activity cannot reach a real Discord account.
