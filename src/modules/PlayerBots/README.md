# Playerbots
Bot AI Core from ike3 for cmangos classic, tbc and wotlk

This system brings the following features:
- Populate the open world with playerbots
- Populate BGs and Arenas with playerbots
- Use alt characters as playerbots
- Do any kind of PvE content (with some guidance on complex mechanics)
- Very detailed configurations of playerbot behaviors (for the min-maxers out there :D)
- Multiple commands to request playerbots do what you require

# How to install
## Compiling Code
If you're new to building CMaNGOS, check the official guide
https://github.com/cmangos/issues/wiki/Installation-Instructions

Important: to enable the playerbots you need to check it in cmake ( `BUILD_PLAYERBOTS` ✅ )

After successful build get aiplayerbot.conf file from "src/modules/Bots/playerbot/aiplayerbot%expansion.conf.dist" (based on expansion you use) and put it to the same folder where mangosd.conf and realmd.conf are, and remove ".dist" from its name

## Apply DB modifications
With `BUILD_PLAYERBOTS=ON` and `Database.AutoUpdate.Enabled=1`, server startup
automatically imports `sql/world`, `sql/world/classic`, and `sql/characters`.
Core databases and base world content must already be installed. Existing
PlayerBots tables and indexes are adopted without dropping them; populated seed
tables are preserved, empty seed tables receive their initial rows, and partial
installations create missing tables. File hashes are recorded in each database's
`migrations` table.

`ai_playerbot_ahbot_data.sql` is a market-data refresh: its contents are imported
once per file hash, including on an existing installation. Changed snapshots
replace the previous price/listing statistics on the next startup.

CMake installs the scripts under `sql/playerbots`. Startup discovers packaged
SQL next to the configured migration directory, installed SQL, or the source
checkout. Set `Database.AutoUpdate.PlayerbotsPath` to the SQL root to override
discovery. Maintenance scripts in `sql/other` and other expansion folders are
excluded.

After you complete all steps above you can check bots config and start your server. It'll take some time for the first time, as gear/characters for bots will be generated at first launch. Have fun! 🥳

## How to Use

### Auction house in companion mode

With `AiPlayerbot.WindrunnerCompanionMode = 1` and `AhBot.Enabled = 1`, AhBot
creates or reuses two dedicated accounts (`<RandomBotAccountPrefix>AH0` and
`<RandomBotAccountPrefix>AH1`) with nine auction characters per faction. These
characters are saved offline and registered for auction ownership only; they
are never added to the online player registry or automatic world population.
Repeated startups reuse the accounts and only create missing characters.
This pool is independent of the random world population settings.

Auction posting requires market statistics. The checked-in market-data SQL is
imported automatically; regenerate it using [the market-data workflow](ahbot/tools/README.md)
when fresh snapshots are available.

- [List of Commands](https://docs.google.com/document/d/1xIdu5l5lAKLSKhqZ2Hb6vaU8qJgbbLwCw4MxmhCW_gI/edit#heading=h.vsmxe9r82yc7)
- [Playerbots Behavior AddOn](https://github.com/celguar/mangosbot-addon)
- [Playerbot Inventory AddOn](https://github.com/davidonete/mangosbot-EngBags)
- [Playerbots Discord Channel](https://discord.gg/vmjZUnPUdr)
