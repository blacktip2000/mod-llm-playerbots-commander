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
git clone https://github.com/bigr00/mod-llm-playerbots.git modules/mod-playerbots
```

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

## Known gaps

- `llm_bot_usage` and `llm_bot_memory` are read but never written. Per-call
  usage accounting and memory reflection are not implemented yet.
- Tools are attached to Tier 1 social calls only, not to Tier 2 planning or the
  chronicler.
- Tier 2 produces a narrative and strategy toggles; it does not yet move bots
  between zones.

## License

GNU GPL v2, same as mod-playerbots.
