# kilix-story

This component is maintained in [`kilix-game-sdk`](..). Games pin the SDK,
not this directory as a separate repository.

`kilix-story` is a small, game-rule-free C11 runtime for story state,
conditions, transactional actions, and validated dialogue traversal.

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

## Runtime contracts

The state object, flag words, and counters are caller-owned and must occupy
disjoint byte ranges. Binding is transactional, and zero-length buffers may be
null. Independent state and session objects may be used concurrently, but the
caller must serialize access to the same object.

Condition lists are fully validated even when their truth value is already
known. `all` over an empty list is true and `any` over an empty list is false.
If evaluation fails, the output boolean is unchanged. Action lists are also
fully validated before mutation; invalid operations, invalid indices, and
counter overflow leave all state unchanged.

Graph validation checks non-empty node tables, unique IDs, required text and
choice labels, condition/action definitions, and every non-terminal link.
Cycles and self-links are allowed. A graph does not prescribe state capacity,
so session start separately checks every referenced flag and counter index
against the bound state. Failed session starts leave the prior session
unchanged.

Graphs, nodes, choices, condition/action arrays, and strings are borrowed and
must remain immutable and alive while a session uses them. The runtime does
no allocation or I/O. Validation selects bounded linear or stack-indexed
paths by input size; oversized inputs retain an exact allocation-free
fallback.

## License

MIT.
