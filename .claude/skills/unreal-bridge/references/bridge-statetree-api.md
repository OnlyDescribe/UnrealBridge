# UnrealBridge StateTree Library API

Module: `unreal.UnrealBridgeStateTreeLibrary`
Preferred wrapper: `from unreal_bridge import StateTree`

This library covers StateTree asset discovery, authoring, binding, compilation,
editor breakpoints, and live `UStateTreeComponent` control. It uses the official
StateTree editor data model, editing subsystem, compiler, schema filters, and
property-binding paths; do not mutate `UStateTreeEditorData` arrays directly.

> **Version gate:** the functional implementation requires UE 5.7+. On UE
> 5.3-5.6 the reflected library remains present, but every operation returns a
> safe default. Check `StateTree.is_state_tree_api_available()` and read
> `StateTree.get_last_state_tree_error()` before planning StateTree work.

> **GUID-first workflow:** states, nodes, transitions, bindable structs, and
> parameters have stable GUID strings. Read them from a list/create call and pass
> those exact values back. Names and array indexes are presentation data and can
> change after edits.

> **Saving is explicit:** writes are transactional and mark the package dirty,
> but do not save it. Compile first, then save through `Editor.save_assets(...)`
> or the standard editor save flow.

## Recommended authoring loop

```python
from unreal_bridge import StateTree

assert StateTree.is_state_tree_api_available()

created = StateTree.create_state_tree(
    asset_path="/Game/AI/ST_AgentAuthored",
    schema_class_path="/Script/GameplayStateTreeModule.StateTreeComponentSchema",
)
assert created.success, created.error

# UStateTreeFactory creates the primary root state for you. Reuse it; adding
# another parentless state creates a separately linkable subtree.
root_id = next(
    state.id
    for state in StateTree.list_state_tree_states(asset_path=created.asset_path)
    if not state.parent_id
)
child_id = StateTree.add_state_tree_state(
    asset_path=created.asset_path,
    parent_state_id=root_id,
    name="Work",
    state_type="State",
    insert_index=-1,
)

# Discover a schema-allowed type; never guess a struct/class path.
task_types = StateTree.list_state_tree_node_types(
    asset_path=created.asset_path,
    kind="Task",
    include_disallowed=False,
)
task_id = StateTree.add_state_tree_node(
    asset_path=created.asset_path,
    owner_id=child_id,
    scope="Task",
    type_path=task_types[0].type_path,
    insert_index=-1,
)

# Discover editable/bindable fields before writing one.
props = StateTree.list_state_tree_node_properties(
    asset_path=created.asset_path,
    node_id=task_id,
    data_source="Instance",
    include_inherited=True,
)

result = StateTree.compile_state_tree(
    asset_path=created.asset_path,
    run_validation=True,
)
assert result.success, [(m.severity, m.message) for m in result.messages]
```

Use one bridge heredoc for the whole loop. After every structural edit, re-list
the affected collection instead of retaining indexes.

## Capability and asset lifecycle

| Function | Result / contract |
|---|---|
| `is_state_tree_api_available()` | `True` only when the real 5.7+ implementation is compiled. |
| `get_last_state_tree_error()` | Most recent library diagnostic. Empty after a successful call that clears it. |
| `create_state_tree(asset_path, schema_class_path)` | Creates via `UStateTreeFactory` + AssetTools. Returns `success`, normalized `asset_path`, and `error`. The path must be `/Mount/Folder/AssetName`, without `.uasset`. |
| `get_state_tree_info(asset_path)` | Schema paths, root-parameter GUID, ready/dirty flags, structural counts, and editor/compiled hashes. |
| `validate_state_tree(asset_path)` | Runs StateTree's editor validation/fixup pass. |
| `compile_state_tree(asset_path, run_validation=True)` | Compiles runnable data and returns every compiler message with severity, state/item GUID, and names/paths where available. Do not treat an empty message list as success; check `success` and `ready_to_run`. |

Typical component schema:
`/Script/GameplayStateTreeModule.StateTreeComponentSchema`. Always use the
schema required by the target system; schema selection controls which state and
node types the add calls accept. The factory creates one primary root state;
discover and reuse it. Additional states with an empty parent are separate
subtree roots, not children of the primary root.

## State hierarchy

`list_state_tree_states(asset_path)` returns a preorder hierarchy. Important
fields are `id`, `parent_id`, `path`, `type`, `selection_behavior`, `tasks_completion`,
and the per-state node/transition counts.

| Function | Notes |
|---|---|
| `add_state_tree_state(asset_path, parent_state_id, name, state_type, insert_index=-1)` | Empty parent creates a root. Valid type names are `State`, `Group`, `Linked`, `LinkedAsset`, and `Subtree`, subject to the schema. Returns the new GUID. |
| `remove_state_tree_state(asset_path, state_id)` | Removes the subtree and prunes dangling property bindings. |
| `move_state_tree_state(asset_path, state_id, new_parent_state_id, insert_index=-1)` | Empty parent moves to root. Cycles are rejected. |
| `set_state_tree_state_type(asset_path, state_id, state_type)` | Safely selects `State`, `Group`, or `Subtree` after schema validation. Use the link calls for `Linked` / `LinkedAsset`. |
| `get_state_tree_state_property(...)` / `set_state_tree_state_property(...)` | Generic reflected read/write; dotted paths and `[index]` work. Values use Unreal export-text syntax. |
| `set_state_tree_linked_state(asset_path, state_id, linked_state_id)` | Changes the state to `Linked` and resolves the target by GUID. |
| `set_state_tree_linked_asset(asset_path, state_id, linked_asset_path)` | Changes the state to `LinkedAsset`; an empty asset path clears it and restores a regular `State`. |

Structural fields (identity, hierarchy, node/transition arrays, parameters,
type, and links) are rejected by the generic setter. Use the dedicated calls;
they maintain StateTree's invariants and clean stale bindings.

## Nodes and property discovery

### Discover types, then add

`list_state_tree_node_types(asset_path, kind='', include_disallowed=False)`
returns native struct and loaded Blueprint node types known to StateTree's class
cache. `kind` is `Evaluator`, `Task`, `Condition`, or `Consideration`.
Use the returned `type_path` verbatim. `allowed_by_schema` is authoritative.
Blueprint generated-class paths end in `_C`.

`add_state_tree_node(asset_path, owner_id, scope, type_path, insert_index=-1)`
accepts these scopes:

| Scope | Owner GUID |
|---|---|
| `Evaluator`, `GlobalTask` | empty |
| `EnterCondition`, `Task`, `SingleTask`, `Consideration` | state GUID |
| `TransitionCondition` | transition GUID |

It returns the new node GUID. `list_state_tree_nodes(asset_path, scope='')`
returns all scopes or one normalized scope. Use `remove_state_tree_node(...)` and
`move_state_tree_node(...)` for edits; `SingleTask` cannot be reordered.

### Discover and edit node data

Every editor node can expose three data sources:

- `Node`: the authored `FStateTreeNodeBase`-derived descriptor.
- `Instance`: editable task/evaluator/condition data; this is the usual target.
- `ExecutionRuntimeData`: authored defaults for execution runtime data, if the
  node type defines it.

Call `list_state_tree_node_properties(asset_path, node_id, data_source,
include_inherited=True)` first. It returns top-level `path`, display name, C++
`type`, current export-text `value`, StateTree `usage` (`Invalid`, `Context`,
`Input`, `Parameter`, or `Output`), category, and editable/bindable flags.

Then use `get_state_tree_node_property(...)` or
`set_state_tree_node_property(...)`. Nested paths such as `Config.Speed` and
array elements such as `Items[0].Tag` are supported even though discovery lists
top-level fields. Write values in the same syntax returned by the getter.

## Transitions

`list_state_tree_transitions(asset_path, state_id='')` lists all transitions or
those owned by one state.

```python
transition_id = StateTree.add_state_tree_transition(
    asset_path=tree,
    state_id=child_id,
    trigger="OnStateSucceeded",
    target_type="GotoState",
    target_state_id=root_id,
    required_event_tag="",
    insert_index=-1,
)
```

- Triggers: `OnStateCompleted`, `OnStateSucceeded`, `OnStateFailed`, `OnTick`,
  `OnEvent`, `OnDelegate`; flags may be joined with `|`.
- Target types: `None`, `Succeeded`, `Failed`, `GotoState`, `NextState`,
  `NextSelectableState`. `GotoState` requires a valid target GUID.
- `OnEvent` should include a registered `required_event_tag`.
- Use `set_state_tree_transition_target(...)` for target changes. Generic
  transition property get/set supports fields such as priority and delay using
  export-text syntax.

Removing a transition also removes its transition-condition nodes and stale
bindings. `move_state_tree_transition(asset_path, transition_id, new_index)`
changes evaluation priority within the owning state's transition array.
The generic setter rejects transition identity, condition arrays, and target
state; use the dedicated node/target calls for those fields.

## Property bindings

Bindings use two independent pieces for each endpoint: an item GUID and a
property path relative to that item's bindable data view.

1. Choose the target item GUID.
2. Call `list_state_tree_bindable_structs(asset_path, target_id)` to get only
   sources accessible from that target's execution position. Empty `target_id`
   returns the generally bindable/global set.
3. Use node property discovery or parameter listing to get exact property names.
4. Call `add_state_tree_binding(asset_path, source_id, source_path, target_id,
   target_path, output_binding=False)`.
5. Compile; type/accessibility errors are reported by the StateTree compiler.

`list_state_tree_bindings(...)` round-trips exact endpoint GUIDs/paths and marks
output bindings. `remove_state_tree_binding(...)` identifies a binding by its
target GUID + target path. `clear_state_tree_bindings_for_item(...)` removes all
incoming and outgoing bindings for one item.

Normal bindings copy source to target. Output bindings reverse the write-back
semantics and are valid only for StateTree usages that support output; do not use
`output_binding=True` as a way to bypass a type/accessibility error.

## Parameters

Root parameters own their schema. State parameter bags are fixed/overridden
views, so schema mutation is deliberately root-only.

| Function | Notes |
|---|---|
| `list_state_tree_parameters(asset_path, scope_id='')` | Empty scope lists root parameters; a state GUID lists that state's parameter bag and override flags. |
| `add_state_tree_root_parameter(asset_path, name, type, default_value='')` | Returns the stable property GUID. |
| `remove_state_tree_root_parameter(...)` / `rename_state_tree_root_parameter(...)` | Updates the property bag through its authoring API. |
| `set_state_tree_parameter_value(asset_path, scope_id, name, value, mark_overridden=True)` | Empty scope writes the root default. A state GUID writes its bag and can mark the field overridden. |

Scalar types: `Bool`, `Byte`, `Int32`, `Int64`, `UInt32`, `UInt64`, `Float`,
`Double`, `Name`, `String`, `Text`. Reflected types use a colon and full path,
for example `Struct:/Script/CoreUObject.Vector` or
`Object:/Script/Engine.Actor`. `Enum`, `SoftObject`, `Class`, and `SoftClass`
follow the same form. One container layer is accepted as `Array<T>` or `Set<T>`.
Defaults and later values use the serialized string returned by the list call.

## Editor breakpoints

- `list_state_tree_breakpoints(asset_path)`
- `set_state_tree_breakpoint(asset_path, item_id, breakpoint_type, enabled)`
- `clear_state_tree_breakpoints(asset_path)`

State breakpoints accept `OnEnter` and `OnExit`; transition GUIDs accept
`OnTransition`. StateTree breakpoints are intentionally transient editor-session
data and disappear when the asset is reloaded.

## Runtime component control

`list_state_tree_components(pie_only=False)` returns exact component object
paths, owner/world details, assigned asset, run status, pause/running flags, and
active states. Pass the returned `component_path` verbatim to:

- `get_state_tree_component_info(component_path)`
- `set_state_tree_component_asset(component_path, asset_path)` — stop logic
  first; empty asset clears the reference.
- `control_state_tree_component(component_path, action, reason='UnrealBridge')`
  where action is `Start`, `Restart`, `Stop`, `Pause`, or `Resume`.
- `send_state_tree_component_event(component_path, event_tag,
  origin='UnrealBridge')`; the GameplayTag must already be registered.

Use `pie_only=True` before runtime control so an editor-preview component is not
mistaken for the live PIE instance.

## Failure and cleanup rules

- Empty arrays and empty GUID strings are valid failure sentinels. Immediately
  read `get_last_state_tree_error()` when a result is unexpectedly empty.
- Schema rejection is intentional. Re-run type discovery for the target asset;
  do not add a disallowed node by mutating editor data directly.
- Structural writes prune invalid bindings, but semantic/type binding problems
  surface at compile time. A successful mutation is not a successful tree.
- Compile and save only after all edits succeed.
- For validation assets, use a unique temporary `/Game/...` folder, close any
  open editor for the asset, delete the asset explicitly, and verify both the
  Asset Registry path and on-disk `.uasset` are gone.
