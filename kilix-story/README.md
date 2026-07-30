# kilix-story

This component is maintained in [`kilix-game-sdk`](..). Games pin the SDK,
not this directory as a separate repository.

`kilix-story` is a small, game-rule-free C11 runtime for story state,
conditions, transactional actions, and bounded dialogue traversal.

It provides:

- caller-owned bit flags and signed counters;
- all/any condition evaluation over flags and counter comparisons;
- transactional flag/counter action lists that reject invalid indices and
  signed overflow without partially mutating state;
- validated static dialogue graphs with conditional choices, game-owned event
  IDs, explicit next nodes, and terminal choices; and
- allocation-free sessions that borrow all strings and tables.

The library does not define quests, combat, inventory, rewards, shops, party
members, scripting syntax, rendering, saves, or audio. A game interprets event
IDs and stores the underlying flag words and counters through its own save
schema.

## Build and verify

```sh
make test
make sanitize
make test-clang
```

## Use

Add the SDK, include its path fragment, then link the story archive before
platform libraries:

```make
KILIX_GAME_SDK_DIR ?= third_party/kilix-game-sdk
include $(KILIX_GAME_SDK_DIR)/mk/kilix-game-sdk.mk
include $(KILIX_STORY_ROOT)/mk/kilix-story.mk
CPPFLAGS += $(KILIX_STORY_CPPFLAGS)

game: $(GAME_OBJECTS) $(KILIX_STORY_LIB)
	$(CC) -o $@ $(GAME_OBJECTS) $(KILIX_STORY_LIB)
```

A graph is ordinary immutable C data. Conditions and actions refer only to
numeric flag/counter slots; choice event IDs are returned to the game:

```c
kilix_story_state state;
kilix_story_session dialogue;

kilix_story_state_bind(&state, flag_words, 2, counters, 8);
kilix_story_session_start(&dialogue, &graph, &state, opening_node);
kilix_story_session_choose(&dialogue, selected_choice, &event);
handle_story_event(event.event);
```

Content compilers can emit these tables from game-specific schemas. The
compiler and authored schema remain outside this runtime so different games
can keep distinct dialogue and quest semantics.

## License

MIT.
