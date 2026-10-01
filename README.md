# mod-llm-playerbots

A fork of [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots) that
adds an **LLM brain** to a small, explicitly named subset of the bots.

Everything the upstream module does is unchanged — see
[README_PLAYERBOTS.md](README_PLAYERBOTS.md) for the base documentation. This
file describes only what this fork adds.

The LLM never drives per-GCD combat. It decides **what a bot says** and **what a
bot is currently trying to do**. If the LLM is down, misconfigured, or over
budget, a roster bot degrades to a perfectly ordinary playerbot and nobody
notices.

The feature is **off by default** (`LlmBots.Enable = 0`), so a build of this
fork behaves exactly like upstream until you turn it on.

---

## What changed relative to mod-playerbots

| Path | Change |
|---|---|
| `src/LlmBots/` | **New.** The entire feature: provider layer, per-bot brain, event bus, prompt builder, chronicler, `.llmbot` commands. |
| `src/Script/playerbots_loader.cpp` | Calls `AddLlmBotsScripts()` alongside `AddPlayerbotsScripts()`. **This is the only edit to an existing source file.** |
| `mod-playerbots.cmake` | **New.** Links OpenSSL + Boost.Beast and adds `deps/` to the include path. |
| `deps/json.hpp` | **New.** Vendored [nlohmann/json](https://github.com/nlohmann/json), header-only. |
| `conf/mod_llm_bots.conf.dist` | **New.** All LLM options. Copied into the server's config dir by the module config glob. |
| `conf/env.llm.dist` | **New.** Template for the environment-variable form of the same options (this is where the API key belongs). |
| `data/sql/playerbots/updates/2026_08_08_00_llm_bots.sql` | **New.** Four tables in `acore_playerbots` plus seed personas. Applied automatically by the playerbots DB updater. |
| `README.md` / `README_PLAYERBOTS.md` | This file; the upstream README moved aside. |

No changes to AzerothCore itself are required. The feature deliberately uses
plain SQL over its own tables rather than prepared statements, because
registering prepared statements would mean patching the core's
`PlayerbotsDatabaseConnection`.

### Checkout directory name

Clone this fork into `modules/mod-playerbots`, **not** `modules/mod-llm-playerbots`:

```bash
git clone https://github.com/gsehun811-cloud/mod-playerbots-llm-commander.git modules/mod-playerbots
```

### Project origins and Commander additions

This project builds on
[bigr00/mod-llm-playerbots](https://github.com/bigr00/mod-llm-playerbots),
which adds LLM integration to
[mod-playerbots](https://github.com/mod-playerbots/mod-playerbots).

Credit for the original Playerbots engine and LLM integration belongs to
their respective authors and contributors. Existing copyright notices and
the GPL v2 license are retained.

This fork adds owner-authorized Commander tools, natural-language Playerbot
control, temporary raid-role registration, and Altbot raid preparation.
Use this fork's clone command above to obtain these additions.

### Required Playerbot core

Use the Playerbot branch of the Playerbots core, not standard AzerothCore.
For a fresh checkout:

```bash
git clone --branch Playerbot https://github.com/mod-playerbots/azerothcore-wotlk.git
cd azerothcore-wotlk
git clone https://github.com/gsehun811-cloud/mod-playerbots-llm-commander.git modules/mod-playerbots
```

This repository already contains the Playerbots module. Do not install
another copy alongside it.

Follow the upstream
[Installation Guide](https://github.com/mod-playerbots/mod-playerbots/wiki/Installation-Guide)
for dependencies, databases, client data, CMake, and building.
When following its module-clone step, use this fork's URL.

### Windows native installation checklist

1. Set `CMAKE_INSTALL_PREFIX` explicitly before building the install target,
   for example `C:/az/azerothcore-wotlk/out/install/x64-Release`.
   A default path under `Program Files (x86)` can cause permission errors.
2. Supply compatible `maps`, `vmaps`, `mmaps`, and `dbc` data.
3. Copy installed `.conf.dist` templates to active `.conf` files if the
   destination files do not already exist:

   | Installed template | Active config |
   |---|---|
   | `configs/authserver.conf.dist` | `configs/authserver.conf` |
   | `configs/worldserver.conf.dist` | `configs/worldserver.conf` |
   | `configs/modules/playerbots.conf.dist` | `configs/modules/playerbots.conf` |
   | `configs/modules/mod_llm_bots.conf.dist` | `configs/modules/mod_llm_bots.conf` |

4. Configure database connections before starting the servers.
5. Run the executables from the installed server directory so normal
   relative config paths resolve.

If runtime DLLs are missing, use the matching x64 dependencies for your
build. Our Windows test required `libmysql.dll`, `libcrypto-3-x64.dll`,
and OpenSSL's `legacy.dll` beside the executables.
In that installation, `legacy.dll` was available under
`C:\Program Files\OpenSSL-Win64\bin`.
Dependency versions and installation paths can differ.

### Keep test databases separate

A different installation folder does not isolate MySQL databases.

When testing on a PC that already runs a server, create separate databases,
for example `aztest_auth`, `aztest_world`, `aztest_characters`, and
`aztest_playerbots`, and grant the database user access.

Update these connection settings:

- `LoginDatabaseInfo` in both server configs.
- `WorldDatabaseInfo` and `CharacterDatabaseInfo` in `worldserver.conf`.
- `PlayerbotsDatabaseInfo` in `playerbots.conf`.

If both servers run simultaneously, configure distinct ports and appropriate
realm details too. Do not point a fresh test at the production databases.

### Windows API key setup

In the active `configs/modules/mod_llm_bots.conf`, configure:

```ini
LlmBots.Enable = 1
LlmBots.Provider = openai
LlmBots.Openai.ApiKey =
LlmBots.Openai.ModelFast = YOUR_AVAILABLE_MODEL_ID
LlmBots.Openai.ModelThink = YOUR_AVAILABLE_MODEL_ID
```

Replace model placeholders with API model IDs available to your account
and compatible with the provider's structured-output and tool-calling
requests. Do not leave placeholders in an active configuration.

In Windows Command Prompt:

```cmd
setx AC_LLM_BOTS_OPENAI_API_KEY "YOUR_NEW_API_KEY"
```

Close the current Command Prompt, open a new one, and restart `worldserver`
from the new prompt. `setx` does not update an already running prompt or
server process.

Check that the variable exists without displaying the secret:

```cmd
if defined AC_LLM_BOTS_OPENAI_API_KEY (echo API key variable is set) else (echo API key variable is missing)
```

`AC_LLM_BOTS_OPENAI_API_KEY` overrides `LlmBots.Openai.ApiKey` in the config.
`OPENAI_API_KEY` is a different variable and is not the setting this module
reads. Other `AC_LLM_BOTS_*` environment settings also override their
corresponding config values.

Important clarification for the `env.llm` instructions elsewhere in this
README: creating `conf/env.llm` alone does not load it into a native Windows
process. Use the environment-variable method above, or an explicit launcher
that imports the file. Docker can load it through Compose `env_file:`.
If a launcher imports the file, check how it handles existing variables.

Never commit a populated `env.llm` file or an API key.

### Commander first-run steps

1. Create a Commander character on a non-random account and make it
   available as an owned Altbot to the controlling player. Configure
   account-bot access or account linking as needed.
2. Add these settings to the active `mod_llm_bots.conf`:

   ```ini
   LlmBots.Commander.Name = CM
   LlmBots.Commander.AccountId = 0
   ```

   Replace `CM` with your chosen character name. Replace `0` with the
   controlling human player's numeric `account.id`, not a login name.
   This is not necessarily the account holding the Commander character.
   Leaving the value at `0` leaves owner authorization unconfigured.

   To find the ID, run this query against your authentication database:

   ```sql
   SELECT id, username FROM account WHERE username = 'YOUR_LOGIN_NAME';
   ```

3. Restart `worldserver` after changing the settings.
4. Log in as the controlling GM character and run:

   ```text
   .playerbots bot add CM
   .llmbot add CM
   .llmbot list
   /w CM Hello.
   .llmbot stats
   ```

Replace `CM` in every command with your configured character name.
Logging in a Playerbot and registering it in the LLM roster are separate
steps. Ordinary LLM companions also need to be online and registered.

All `.llmbot` commands above are in-game GM commands. They are not available
in the `AC>` server console. `.llmbot reload` reloads roster and personas;
it does not replace a restart after changing environment variables.

### Troubleshooting

| Symptom | Check |
|---|---|
| Cannot open `configs/authserver.conf` | Copy the template to its active name and check the working directory. |
| Cannot find `legacy.dll` | Check the matching OpenSSL runtime/provider files. |
| `llmbot stats` does not exist in `AC>` | Use `.llmbot stats` in game as a GM. |
| Calls/errors/fallbacks increase but tokens stay at zero | Inspect the provider error in `Server.log`. |
| HTTP 401 / incorrect API key | Replace the variable inherited by worldserver and restart from a new CMD. |
| CM does not respond | Check enablement, online bot status, roster, name, owner ID, models, cooldowns, and budgets. |
| Progression scripts have no code | Check for database content from a module missing from this build. |

Statistics accumulate during the process lifetime. Compare values before
and after a new request rather than requiring earlier errors to disappear.

### Autogear clarification

`AiPlayerbot.AutoGearScoreLimit` limits item level. It does not by itself
guarantee vanilla-only item sources or a specific dungeon/raid loot set.

`AiPlayerbot.AutoGearQualityLimit` also applies. The shipped default is `3`
(rare); `4` allows epic quality. Choose both settings for your progression
and inspect the resulting equipment.

AzerothCore derives both the script-loader function name
(`Addmod_playerbotsScripts`) and the CMake hook file name
(`mod-playerbots.cmake`) from the directory name.

---

## Decision map

| Tier | What | Model | Frequency |
|---|---|---|---|
| 0 | Reflex: combat rotation, movement, looting — the existing playerbots engine | none | every tick |
| 1 | Social: reply to whispers/say, death quips, level-up boasts, zone reactions, bot-to-bot banter | fast | event-triggered, per-bot cooldown (default 20 s) |
| 2 | Agenda: pick a goal + social stance + strategy toggles, self-narrative | think | per-bot timer (default 5 min) |
| 3 | Chronicler: observer bot that digests what the LLM bots did and answers whispers | fast | cache-gated, at most 1 per 60 s |

## Tool calling

Tier 1 calls ship a tool list, and the model chooses what to look up. The tools
read real game state, so the bot answers from facts rather than inventing them:

`get_character_sheet`, `get_equipped_items`, `get_bag_items`, `get_quest_log`,
`get_nearby_players`, `get_group_members`, `get_chat_history`,
`get_server_status`.

Tools execute on the **world thread** (where game state is safe to touch), never
on the HTTP worker threads. A tool round re-enqueues the request rather than
blocking a worker. Up to 4 tool rounds per call, then the bot answers with what
it has.

---

## Setup

1. Clone into `modules/mod-playerbots` (see above) and re-run `cmake`. Sources
   are auto-globbed; `mod-playerbots.cmake` is picked up automatically.
2. Build as usual. OpenSSL and Boost are already AzerothCore dependencies.
3. Start the server once so the playerbots DB updater applies
   `2026_08_08_00_llm_bots.sql` (tables `llm_bot_roster`, `llm_bot_persona`,
   `llm_bot_memory`, `llm_bot_usage` and the seed personas).
4. Create the roster characters on a **dedicated, non-random account**
   (`llmbots` by default). They must not live on a random-bot account, or the
   random-bot rotation will log them out.
5. Configure. Copy `conf/env.llm.dist` somewhere your stack reads environment
   variables from (for the AzerothCore docker stack, a file referenced by
   `env_file:` in your compose override) and fill it in. Environment variables
   win over `mod_llm_bots.conf`, which keeps the API key out of any config
   file. Minimum: `AC_LLM_BOTS_ENABLE=1`,
   `AC_LLM_BOTS_OPENAI_API_KEY`, `AC_LLM_BOTS_OPENAI_MODEL_FAST`,
   `AC_LLM_BOTS_OPENAI_MODEL_THINK`.
6. In game: `.llmbot add <name> [persona]`, then whisper the bot.

### Docker notes

Two optional tweaks to the AzerothCore docker stack make module config and SQL
work without a host bind mount. Neither is part of this repository — apply them
to your own core checkout if you use the stock stack:

- `apps/docker/Dockerfile` — copy `/azerothcore/modules` into the runtime image
  so the DB updater can read module SQL at startup.
- `apps/docker/entrypoint.sh` — activate `modules/*.conf.dist` templates the
  same way the core component configs are activated.

---

## Commands (GM)

```
.llmbot list                      roster + brain state
.llmbot add <name> [persona]      promote a character to an LLM bot
.llmbot remove <name>             demote back to a plain playerbot
.llmbot persona <name> <persona>  swap persona live
.llmbot reload                    re-read roster + personas from the DB
.llmbot stats                     budget / rate / error dashboard
.llmbot say <name> <prompt>       force a one-off Tier 1 call
.llmbot goal <name>               force a Tier 2 planning call
.llmbot pause | .llmbot resume    kill switch — instantly back to Tier 0
```

Roster edits can also be made straight in `llm_bot_roster`, followed by
`.llmbot reload`.

## Personas

Personas live in `llm_bot_persona`. Each has a brief, a voice, quirks, and a
**self-awareness dial** (0 = fully in-fiction, 3 = openly talks about "the
loop"). Five ship with the SQL update.

```sql
INSERT INTO llm_bot_persona (persona, display_name, brief, voice, quirks, self_awareness) VALUES
('grump', 'The Grump', 'A retired soldier who is furious about everything and secretly fond of everyone.',
 'low, grumbling, ends most sentences with a sigh', 'Hates gnomes. Hates murlocs. Hates that he does not hate mornings.', 0);
```

## Providers

- **openai** (default) — the OpenAI Responses API, strict JSON-schema
  structured outputs plus function calling.
- **foundry** — Microsoft Foundry / Azure OpenAI over REST.
- **mock** — canned schema-valid responses, no network. Useful for testing the
  wiring without spending anything.

Adding one is a single file plus a line in the provider registry.

## Cost control

A 20-bot roster plus a chronicler sits around **13 requests/min, ~8k tokens/min**.
Azure ties request rate to token quota at 6 RPM per 1,000 TPM, so provision
around 10k TPM on the fast deployment. The think deployment fires ~2/min and can
be small.

Layered gates: per-bot cooldown, global calls/min token bucket, rolling hourly
token budget, queue-depth cap, request dedup, bot-to-bot conversation caps, and
a circuit breaker on repeated provider failures. `.llmbot stats` shows all of it.

## Threading

```
[map thread]                          [worker pool, 2-4 threads]   [world thread]
PlayerScript::OnPlayerAfterUpdate                |                       |
  brain tick (cooldowns, events)                 |                       |
  submit LlmRequest (ObjectGuid only) ---> LlmClient                     |
                                           worker: provider->Complete    |
                                           (sync HTTPS, retry,           |
                                            schema validation)           |
                                           completion queue -----------> WorldScript::OnUpdate
                                                                           drain -> brain / chronicler
                                                                           tool calls execute here
                                                                           (FindPlayer, null => drop)
```

Requests carry `ObjectGuid` only, never `Player*`, so a bot that logs out
mid-request drops its completion cleanly.

## Layout

- `src/LlmBots/Llm/` — provider-agnostic core, no WoW types: request/response
  types, provider interface, OpenAI + Foundry + mock providers, worker pool,
  retry, circuit breaker, budget governors, JSON schemas and validation, HTTPS
  client.
- `src/LlmBots/Bots/` — roster registry, event bus, conversation guard, per-bot
  brain, prompt builder, tool implementations, chronicler.
- `src/LlmBots/Script/` — AzerothCore hooks, `.llmbot` commands, module facade.

## Commander and Raid Preparation

The LLM integration includes an optional **Commander** mode designed for
controlling Playerbots through natural-language whispers.

The Commander is intentionally separated from normal conversational LLM bots.
Regular roster bots may participate in conversation, while privileged gameplay
tools are restricted to the configured Commander and its authorized owner.

### Configuration

Configure the Commander in `mod_llm_bots.conf`:

```ini
LlmBots.Commander.Name = CM
LlmBots.Commander.AccountId = 0
```

Set `LlmBots.Commander.AccountId` to the AzerothCore account ID of the player
who is allowed to control the Commander.

Replace `0` with the appropriate account ID for your server.

The OpenAI API key must not be committed to the repository. Create
`conf/env.llm` from `conf/env.llm.dist` and set the key there:

```ini
AC_LLM_BOTS_OPENAI_API_KEY=YOUR_API_KEY_HERE
```

`conf/env.llm` is excluded by `.gitignore` and should never be committed.

### Commander usage

Whisper the Commander in natural language. The wording does not need to match
a fixed command syntax.

Examples:

```text
/w CM Find the nearest warrior.
/w CM Invite Playername to my group.
/w CM Teleport me to Playername.
/w CM Teleport me to Stormwind.
```

The Commander can use restricted tools to:

- find suitable Playerbots;
- send supported Playerbot commands;
- invite an owned Playerbot to the owner's group;
- teleport the owner to an eligible Playerbot;
- teleport the owner to supported major cities;
- prepare owned Altbots for a requested raid role and level;
- store and read a temporary raid-role roster.

These tools use explicit validation instead of exposing unrestricted GM
commands directly to the language model.

### Raid role registration

Raid roles can be registered through the Commander with a semicolon-separated
`ROLE` message.

Example:

```text
/w CM ROLE Tankone=main_tank; Warriorone=fury_dps; Priestone=holy_healer; Rogueone=combat_dps
```

Natural-language role names can also be interpreted by the LLM before the tool
call. Internally, supported roles are normalized to canonical role names.

The registered roster is stored **in memory per owner account**. It is not
currently persisted to the database, so the roster must be registered again
after a worldserver restart.

The Commander can read the currently registered raid roster when asked:

```text
/w CM Show me the registered raid roles.
```

### Character preparation

The `prepare_raid_character` tool prepares an eligible owned Altbot for a
specific role and level.

Preparation currently performs:

```text
set requested level
-> apply the requested PvE talent specialization
-> run Playerbot maintenance
-> run Playerbot autogear
```

The tool validates the character and role before making changes.

The target must:

- be online;
- be an Altbot;
- belong to the requesting player;
- use a raid role supported by its class.

Example natural-language requests:

```text
/w CM Prepare Warriorone as fury DPS at level 60.
/w CM Prepare Priestone as a holy healer at level 60.
/w CM Prepare the registered raid for level 60.
```

When several registered characters are prepared, an invalid character or
class/role combination can be reported separately without invalidating
characters that were prepared successfully.

### Offline Altbots

Raid preparation currently operates on **online Playerbots**.

If the characters are offline, log them in first with the normal
mod-playerbots commands. For example, an Altbot account can be loaded with:

```text
.playerbots bot addaccount ACCOUNT_NAME
```

Automatic login of offline characters is not currently part of the Commander
raid-preparation tools.

### Autogear limits

Standard mod-playerbots autogear settings control the equipment selected during
raid preparation.

`AiPlayerbot.AutoGearScoreLimit` can be used to limit maximum item level.

Useful vanilla-era progression caps include:

```text
MC / Onyxia / ZG : 78
BWL               : 83
AQ40              : 88
Naxxramas 40      : 92
```

These are suggested progression values, not hard requirements of the Commander.

### Safety model

Commander tools are deliberately narrow in scope. The design avoids exposing
general unrestricted GM functionality to the language model.

Character preparation requires an owned Altbot and validates class/role
compatibility.

Teleport-to-playerbot operations move the requesting player only. City
teleports use a predefined set of destinations.

This separation allows normal LLM roster bots to remain primarily
conversational characters while a single Commander performs privileged
gameplay actions.

## Known gaps

- `llm_bot_usage` and `llm_bot_memory` are read but never written. Per-call
  usage accounting and memory reflection are not implemented yet.
- Tools are attached to Tier 1 social calls only, not to Tier 2 planning or the
  chronicler.
- Tier 2 produces a narrative and strategy toggles; it does not yet move bots
  between zones.

## License

GNU GPL v2, same as mod-playerbots.
