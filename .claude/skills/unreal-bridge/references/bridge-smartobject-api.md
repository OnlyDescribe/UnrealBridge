# UnrealBridge Smart Object Library API

Module: `unreal.UnrealBridgeSmartObjectLibrary`
Preferred wrapper: `from unreal_bridge import SmartObject`

This library covers Smart Object Definition authoring, slots, behaviors,
annotations and definition data, World Conditions, parameters and property
bindings, world components, persistent collections, runtime spatial queries,
claim/occupy/release, runtime tags and enablement, slot events, and entrance
validation. Use it instead of mutating private `USmartObjectDefinition` arrays or
fabricating native handles from Python.

> **Version gate:** the functional implementation requires UE 5.7+. UE 5.3-5.6
> retain the reflected library and kwargs wrapper but execute generated safe
> stubs. Check `SmartObject.is_smart_object_api_available()` and, on failure,
> read `SmartObject.get_last_smart_object_error()`.

> **Discover, then write:** behavior classes, definition-data structs,
> annotations, and World Condition structs are project/plugin dependent. Always
> call the corresponding `list_*_types` function and use its returned
> `type_path`. Do not guess `/Script/...` paths.

> **GUID-first authoring:** slot IDs, definition-data IDs, parameter IDs, and
> binding endpoint IDs are stable GUID strings. Re-list after structural edits
> and pass the returned IDs back. Array indexes are ordering data only.

> **Saving is explicit:** asset and editor-world writes are transactional and
> mark packages dirty, but do not save them. Validate first, then save with
> `Editor.save_assets(...)` or the normal editor save flow.

## Recommended definition-authoring loop

```python
import unreal
from unreal_bridge import SmartObject

assert SmartObject.is_smart_object_api_available()

created = SmartObject.create_smart_object_definition(
    asset_path="/Game/AI/SmartObjects/SO_AgentAuthored",
)
assert created.success, created.error

slot_id = SmartObject.add_smart_object_slot(
    asset_path=created.asset_path,
    name="Use",
    offset=unreal.Vector(0, 0, 0),
    rotation=unreal.Rotator(0, 0, 0),
    enabled=True,
    insert_index=-1,
)

# Discover compatible behavior classes. Which class is useful depends on the
# target project's enabled gameplay plugins.
behavior_types = SmartObject.list_smart_object_behavior_types()
behavior_path = behavior_types[0].type_path
behavior_object = SmartObject.add_smart_object_behavior_definition(
    asset_path=created.asset_path,
    behavior_class_path=behavior_path,
    slot_id=slot_id,
    insert_index=-1,
)

# Inspect fields before using export-text writes.
behavior_props = SmartObject.list_smart_object_behavior_properties(
    asset_path=created.asset_path,
    behavior_object_path=behavior_object,
    include_inherited=True,
)

# Slot annotations and other definition data use the same discovery pattern.
data_types = SmartObject.list_smart_object_definition_data_types(
    asset_path=created.asset_path,
    slot_id=slot_id,
)

validation = SmartObject.validate_smart_object_definition(
    asset_path=created.asset_path,
)
assert validation.success, [
    (message.severity, message.message) for message in validation.messages
]
```

Use one bridge heredoc for a complete edit/validate/save unit. If any call
returns `False`, `""`, `-1`, or a result with `success == False`, stop and read
`get_last_smart_object_error()` before continuing.

## Capability, lifecycle, and generic definition properties

| Function | Result / contract |
|---|---|
| `is_smart_object_api_available()` | `True` only for the functional UE 5.7+ implementation. |
| `get_last_smart_object_error()` | Most recent library diagnostic; successful top-level calls clear it before work. |
| `create_smart_object_definition(asset_path)` | Creates a `USmartObjectDefinition` Data Asset through AssetTools. Path form is `/Mount/Folder/AssetName`, without `.uasset`. Returns `success`, normalized `asset_path`, and `error`. |
| `get_smart_object_definition_info(asset_path)` | Returns tag query/tags/policies, World Condition schema, preview settings, root/parameter binding IDs, structural counts, validation state, and dirty state. |
| `validate_smart_object_definition(asset_path)` | Calls Epic's definition validator and returns every message with severity plus aggregate `success`. |
| `list_smart_object_definition_properties(asset_path, include_inherited=True)` | Lists reflected top-level fields with export-text value, type, category, editability, and bindability. |
| `get_smart_object_definition_property(asset_path, property_path)` | Reads a dotted or indexed property path. |
| `set_smart_object_definition_property(asset_path, property_path, value)` | Imports Unreal export-text. Structural arrays and fields with dedicated APIs are rejected. |

Generic property writers deliberately do not replace slot, behavior,
definition-data, condition, parameter, or binding collections. Dedicated calls
maintain IDs, condition schemas, binding paths, editor notifications, and
validation caches.

## Tags, filters, and policies

| Function | Notes |
|---|---|
| `get_smart_object_tag_query_json(asset_path, slot_id='')` | Empty `slot_id` selects the definition user-tag filter; a slot GUID selects that slot's filter. Returns Epic's `FGameplayTagQueryExpression` JSON, or `""` for an empty query. |
| `set_smart_object_tag_query_json(asset_path, query_json, slot_id='')` | Replaces the selected query. Empty JSON clears it. Round-trip an existing result when editing complex expressions. |
| `set_smart_object_tags(asset_path, tags, slot_id='', tag_set='Activity')` | Replaces the full tag set. Definitions accept `Activity`; slots accept `Activity` or `Runtime`. Every tag must already be registered. |
| `set_smart_object_tag_policies(asset_path, user_tags_filtering_policy, activity_tags_merging_policy)` | Sets reflected enum names/display names. Typical values include `NoFilter`/`Combine`/`Override` according to the engine enum; inspect current info before changing policy. |

Runtime query `user_tags` are evaluated against the authored user-tag queries.
Runtime query `activity_tags` are converted into a tag query using
`activity_match`: `All`, `Any`, `None`, `ExactAll`, or `ExactAny`.

## Slots

`list_smart_object_slots(asset_path)` returns stable `id`, current `index`,
name, local offset/rotation, enabled state, user filter JSON, authored activity
and runtime tags, and behavior/data/condition counts.

| Function | Notes |
|---|---|
| `add_smart_object_slot(asset_path, name, offset, rotation, enabled=True, insert_index=-1)` | Creates a slot with a new GUID and the definition's World Condition schema. `-1` appends. Returns the GUID. |
| `duplicate_smart_object_slot(asset_path, source_slot_id, new_name='', insert_index=-1)` | Deep-copies authored content but regenerates the slot and definition-data GUIDs. Returns the new slot GUID. |
| `remove_smart_object_slot(asset_path, slot_id)` | Removes the slot and lets the definition prune/update dependent references and bindings. |
| `move_smart_object_slot(asset_path, slot_id, new_index)` | Reorders only; the GUID remains stable. |
| `list_smart_object_slot_properties(asset_path, slot_id)` | Discovers reflected slot fields and export-text values. |
| `get_smart_object_slot_property(...)` / `set_smart_object_slot_property(...)` | Reads/writes dotted/indexed non-structural slot properties using export-text. Identity and nested collections are rejected by the setter. |

## Behavior definitions

A behavior is an instanced `USmartObjectBehaviorDefinition` object. Empty
`slot_id` targets definition-level fallback behaviors; a slot GUID targets its
local behaviors. Effective runtime behavior classes include both sets.

| Function | Notes |
|---|---|
| `list_smart_object_behavior_types()` | Lists loaded, concrete behavior classes with class path and display name. Enable/load the target gameplay plugin before discovery. |
| `list_smart_object_behavior_definitions(asset_path, slot_id='')` | Returns object path, name, class path, order, property count, and owning slot (empty for defaults). |
| `add_smart_object_behavior_definition(asset_path, behavior_class_path, slot_id='', insert_index=-1)` | Instantiates the discovered class as an owned transactional subobject. Returns its object path. |
| `remove_smart_object_behavior_definition(asset_path, behavior_object_path)` | Removes the exact instanced behavior. |
| `move_smart_object_behavior_definition(asset_path, behavior_object_path, new_index)` | Reorders within its current default/slot array. |
| `list_smart_object_behavior_properties(...)` | Discovers export-text fields. |
| `get_smart_object_behavior_property(...)` / `set_smart_object_behavior_property(...)` | Reads/writes a reflected behavior field. Pass the object path returned by add/list, not only the class path. |

## Definition data and slot annotations

Definition data uses `FSmartObjectDefinitionData`-derived instanced structs.
`FSmartObjectSlotAnnotation` derivatives are slot-only definition data; entrance
annotations are the common way to author navigation/use locations.

| Function | Notes |
|---|---|
| `list_smart_object_definition_data_types(asset_path, slot_id='')` | Returns concrete, loaded types valid for the selected scope. `kind` distinguishes `DefinitionData` and `Annotation`; use `allowed_at_definition` / `allowed_at_slot`. |
| `list_smart_object_definition_data(asset_path, slot_id='')` | Returns stable data GUID, index, type, annotation flag, transform capability, and property count. |
| `add_smart_object_definition_data(asset_path, struct_type_path, slot_id='', insert_index=-1)` | Creates the instanced struct and a new GUID. An annotation with empty `slot_id` is rejected. |
| `remove_smart_object_definition_data(asset_path, data_id)` | Finds by GUID across definition and slots. |
| `move_smart_object_definition_data(asset_path, data_id, new_index)` | Reorders within the current scope. |
| `list_smart_object_definition_data_properties(...)` | Discovers fields on the concrete struct. |
| `get_smart_object_definition_data_property(...)` / `set_smart_object_definition_data_property(...)` | Reads/writes reflected data using export-text. |

## Object and slot World Conditions

Empty `slot_id` means definition preconditions; a slot GUID means selection
preconditions for that slot. The active `UWorldConditionSchema` filters types.

| Function | Notes |
|---|---|
| `list_smart_object_world_condition_types(asset_path, slot_id='')` | Returns schema-allowed loaded `FWorldConditionBase` derivatives. Use only returned paths. |
| `list_smart_object_world_conditions(asset_path, slot_id='')` | Returns index, type, operator, expression depth, inversion, and property count. |
| `add_smart_object_world_condition(asset_path, condition_struct_path, slot_id='', operator='And', expression_depth=0, invert=False, insert_index=-1)` | Adds and initializes a condition query. Returns its index or `-1`. |
| `remove_smart_object_world_condition(asset_path, slot_id, condition_index)` | Removes one expression entry and reinitializes the query. |
| `move_smart_object_world_condition(asset_path, slot_id, condition_index, new_index)` | Reorders expression entries. Re-list before further indexed edits. |
| `set_smart_object_world_condition_expression(asset_path, slot_id, condition_index, operator, expression_depth, invert)` | Updates expression grouping metadata and reinitializes/validates the query. |
| `list_smart_object_world_condition_properties(...)` | Discovers concrete condition fields. |
| `get_smart_object_world_condition_property(...)` / `set_smart_object_world_condition_property(...)` | Export-text property access; failed query reinitialization rolls the edit back. |

World Condition indexes are not identities. After add/remove/move, discard old
indexes and call `list_smart_object_world_conditions` again.

## Parameters and property bindings

Parameters live in the definition's `FInstancedPropertyBag`. Type syntax matches
StateTree property bags: scalar names such as `Bool`, `Int32`, `Float`,
`Double`, `Name`, `String`, `Vector`, and object/class/struct paths such as
`Object:/Script/Engine.Actor`. Container forms supported by the parser should be
copied from existing project conventions or verified with a small temporary
definition.

| Function | Notes |
|---|---|
| `list_smart_object_parameters(asset_path)` | Returns parameter GUID, name, normalized type, and serialized value. |
| `add_smart_object_parameter(asset_path, name, type, default_value='')` | Adds a unique property and optionally imports a serialized default. Returns its GUID. |
| `remove_smart_object_parameter(asset_path, name)` | Removes by name and updates binding paths. |
| `rename_smart_object_parameter(asset_path, old_name, new_name)` | Renames through the property-bag API. |
| `set_smart_object_parameter_value(asset_path, name, value)` | Imports a serialized value. |
| `list_smart_object_bindable_structs(asset_path)` | Returns binding endpoint kind (`Root`, `Parameters`, `Slot`, `DefinitionData`), GUID, name, and type path. |
| `list_smart_object_bindings(asset_path)` | Returns source/target endpoint GUIDs and relative property paths. |
| `add_smart_object_binding(asset_path, source_id, source_path, target_id, target_path)` | Adds/replaces a target binding after endpoint/path validation. IDs and paths are separate arguments. |
| `remove_smart_object_binding(asset_path, target_id, target_path)` | Removes the binding that writes the exact target path. |

Binding endpoint GUIDs must come from `list_smart_object_bindable_structs`.
Property paths are relative to the endpoint. List the corresponding properties
before binding; compatible source/target property types are still required.

## Loaded-world components

World selection follows the bridge session: during PIE the play world is used;
otherwise the current editor world is used. Component paths are full live UObject
paths returned by the list/add calls.

| Function | Notes |
|---|---|
| `list_smart_object_components(pie_only=False)` | Lists loaded non-CDO components with owner/world, base/applied definition, registered handle/type, transform/bounds, bound/enabled state, and collection eligibility. |
| `get_smart_object_component_info(component_path)` | Returns the same data for one component. |
| `add_smart_object_component(actor_path, definition_asset_path, component_name='SmartObject', can_be_part_of_collection=False, register_with_subsystem=True)` | Adds an instance component transactionally, attaches it to the root, and optionally registers it. Returns the component path. |
| `remove_smart_object_component(component_path)` | Removes runtime state, instance-component ownership, and the component. |
| `set_smart_object_component_definition(component_path, definition_asset_path, register_with_subsystem=True)` | Safely unregisters/rebinds around the definition change. |
| `control_smart_object_component(component_path, action)` | Actions: `Register`, `Unregister`, `RemoveFromSimulation`, `Refresh`, `Enable`, `Disable`. `RemoveFromSimulation` destroys its runtime instance while keeping the component registered for later lifecycle handling. |

## Persistent collections

| Function | Notes |
|---|---|
| `list_persistent_smart_object_collections(pie_only=False)` | Lists loaded collection actors, entry count, bounds, world type, and actual subsystem registration state. |
| `create_persistent_smart_object_collection(actor_label='SmartObjectPersistentCollection')` | Spawns in the current persistent level and rebuilds it. Returns the actor path. Refuses while PIE is running. |
| `destroy_persistent_smart_object_collection(collection_actor_path)` | Unregisters then destroys the actor transactionally. |
| `control_persistent_smart_object_collection(collection_actor_path, action)` | Actions: `Rebuild`, `Clear`, `Register`, `Unregister`. |
| `list_persistent_smart_object_collection_entries(collection_actor_path)` | Returns each entry's handle, component, definition, transform, bounds, and tags. |

Rebuild scans currently loaded eligible components; it does not force unloaded
World Partition actors into memory. Save the level only after inspecting entries.

## Runtime query and slot inspection

```python
results = SmartObject.query_smart_objects(
    center=unreal.Vector(0, 0, 0),
    extent=unreal.Vector(5000, 5000, 1000),
    user_tags=[],
    activity_tags=[],
    behavior_class_paths=[],
    activity_match="All",
    claim_priority="Normal",
    evaluate_conditions=True,
    include_claimed_slots=False,
    include_disabled_slots=False,
    user_actor_path="",
    sort_by_distance=True,
    max_results=0,
)
```

`query_smart_objects` returns ranked slot candidates with distance, object/slot
handles, live component/owner when one exists, definition, transform, state,
activity/runtime tags, effective behavior class paths, and enabled/claimable
flags. `max_results=0` means unlimited. Claim priorities are reflected enum
names such as `Low`, `Normal`, or `High`.

`list_smart_object_runtime_slots(smart_object_handle='', component_path='',
claim_priority='Normal')` lists every slot for one live object. Normally supply
exactly one selector. If both are supplied, they must refer to the same object.

Handle strings are opaque bridge/engine values:

- Smart Object handle: `{guid}`.
- Slot handle: `{guid}:slot-index`.
- Claim token: a separate session-local GUID returned by the bridge.

Never manufacture or persist these values. Re-query after world, streaming,
registration, or PIE lifecycle changes.

## Dynamic runtime objects and claim lifecycle

| Function | Notes |
|---|---|
| `create_runtime_smart_object(definition_asset_path, transform, owner_actor_path='')` | Creates a componentless runtime instance. Optional actor owner data is kept alive by the bridge until destruction. Returns an object handle. The subsystem runtime must already be initialized. |
| `destroy_runtime_smart_object(smart_object_handle)` | Releases bridge-held claims on the object, destroys the runtime instance, and releases owner data. |
| `claim_smart_object_slot(slot_handle, user_actor_path='', claim_priority='Normal')` | Claims one slot and returns `success`, an opaque `claim_token`, handles, priority/state, and error. Optional user data is held for the claim lifetime. |
| `occupy_smart_object_claim(claim_token, behavior_class_path)` | Transitions a claim to occupied and returns the matching authored behavior object's path. Use one of the candidate's effective behavior class paths. |
| `release_smart_object_claim(claim_token)` | Marks claimed or occupied slot free and invalidates the token. Always call this in `finally`. |
| `list_smart_object_claims()` | Lists bridge-owned tokens in this editor session and reports whether each underlying native claim is still valid. |

```python
claim = SmartObject.claim_smart_object_slot(
    slot_handle=results[0].slot_handle,
    user_actor_path="",
    claim_priority="Normal",
)
assert claim.success, claim.error
try:
    behavior = results[0].behavior_class_paths[0]
    occupied = SmartObject.occupy_smart_object_claim(
        claim_token=claim.claim_token,
        behavior_class_path=behavior,
    )
    assert occupied.success, occupied.error
finally:
    SmartObject.release_smart_object_claim(claim_token=claim.claim_token)
```

Claim tokens are process-local and deliberately cannot be reconstructed after a
module/editor restart. Cleanup outstanding claims before rebuilding or closing
the editor.

## Runtime tags, enabled state, and events

| Function | Notes |
|---|---|
| `set_smart_object_runtime_tags(handle, scope, tags, replace=True)` | `scope` is `Object` for an object handle or `Slot` for a slot handle. `replace=True` makes the exact set; `False` only adds. Tags must be registered. |
| `set_smart_object_runtime_enabled(smart_object_handle, enabled, reason_tag='')` | Empty reason uses Epic's default gameplay reason. A custom reason must be a registered enabled-reason tag. |
| `set_smart_object_runtime_slot_enabled(slot_handle, enabled)` | Enables/disables one runtime slot; the return value reports operation success, not the previous state. |
| `send_smart_object_slot_event(slot_handle, event_tag)` | Sends a tag-only `FSmartObjectEventData` event with empty payload. Returns `True` only when the runtime object currently has at least one event listener; `False` with a diagnostic means no broadcast occurred. |

Authored slot runtime tags are copied into runtime state when the instance is
created. Runtime tag edits do not write back to the Definition asset.

## Entrance lookup and offline validation

Both entrance functions accept a `BridgeSmartObjectEntranceRequest`:

- `user_actor_path`, or explicit `validation_filter_class_path` plus positive
  capsule radius/height/step height when no actor supplies navigation settings;
- `search_location`;
- `selection_method`: `First` or `NearestToSearchLocation` (use reflected name);
- `location_type`: `Entry` or `Exit`;
- navigation projection, ground trace, transition trajectory, entrance overlap,
  slot overlap, slot-location fallback, and up-axis-locked rotation flags.

| Function | Notes |
|---|---|
| `find_smart_object_entrance(slot_handle, request)` | Validates/selects a live runtime slot entrance. Returns found/valid flags, slot, location, rotation, tags, nav-node presence, and error. |
| `validate_smart_object_definition_entrances(asset_path, owner_transform, request, skip_actor_path='')` | Runs Epic's static all-entrance validation before registering an object. Every returned candidate has its own `valid` flag. `skip_actor_path` avoids colliding with a placement-preview actor. |

For a geometry-independent smoke test, disable projection/traces/overlaps and
set `use_slot_location_as_fallback=True`. For production validation, supply the
same filter/capsule/navigation expectations as the real user.

## Subsystem debug control

`debug_smart_object_subsystem(action)` accepts `InitializeRuntime`,
`CleanupRuntime`, `RegisterAll`, or `UnregisterAll`. These are editor/debug
controls, not normal gameplay lifecycle APIs.

- `CleanupRuntime` releases all bridge-owned claims and dynamic owner data before
  destroying subsystem runtime state.
- `UnregisterAll` and `CleanupRuntime` can invalidate every live handle in the
  selected world.
- Debug actions may be unavailable in engine configurations built with Smart
  Object debugging disabled.

Use them only in isolated validation/setup code, restore the expected state in
`finally`, and never run destructive debug lifecycle actions during a user's PIE
session without explicit intent.

## Validation and cleanup pattern

When creating temporary verification data, use a unique folder and actor-label
prefix and clean both assets and world objects even after a failed assertion:

```python
TEMP_FOLDER = "/Game/__UnrealBridgeValidation"
runtime_handle = ""
claim_token = ""
component_path = ""
collection_path = ""
try:
    # Create/edit/validate definition; add a temporary component or dynamic
    # runtime object; query; claim; occupy; entrance-check; inspect collection.
    pass
finally:
    if claim_token:
        SmartObject.release_smart_object_claim(claim_token=claim_token)
    if runtime_handle:
        SmartObject.destroy_runtime_smart_object(
            smart_object_handle=runtime_handle,
        )
    if component_path:
        SmartObject.remove_smart_object_component(
            component_path=component_path,
        )
    if collection_path:
        SmartObject.destroy_persistent_smart_object_collection(
            collection_actor_path=collection_path,
        )
    unreal.EditorAssetLibrary.delete_directory(TEMP_FOLDER)
```

After cleanup, verify all three independently: asset registry returns nothing
under the folder, no loaded actor/component carries the prefix, and no `.uasset`
remains on disk. Do not save the validation level unless the test explicitly
requires a save/reload round trip.
