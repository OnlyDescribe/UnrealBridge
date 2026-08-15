# UnrealBridge UMG Library API

Module: `unreal.UnrealBridgeUMGLibrary`

Audited kwargs-only wrapper: `from unreal_bridge import UMG`

This library is the complete Widget Blueprint surface: asset and widget-tree
authoring, reflected layout/style writes, UI-material assignment, widget
animations, UE MVVM, compile-time deliverability checks, and live PIE
validation.

## Version contract

- Widget Blueprint, widget tree, layout/style, animation, and live-widget APIs
  are available on every supported UnrealBridge engine version (UE 5.3+).
- MVVM authoring and runtime calls are functional on UE 5.7+.
- On UE 5.3-5.6 every MVVM function remains reflected and build-compatible,
  but logs a clear `requires UE 5.7+` warning and returns its empty/false
  result. An unavailable MVVM plugin never blocks a lower-version build.
- UI material graph creation belongs to `UnrealBridgeMaterialLibrary`. UMG
  assigns the resulting Material or Material Instance to an `FSlateBrush` and
  can tune its dynamic parameters in PIE.

## Deliverable UI workflow

Use this order for a new screen. Do not stop at `compile=True`; the live phase
is what proves bindings, interactions, geometry, animations, and material
instances work in the target project.

1. Discover widget classes with `list_widget_classes`.
2. Create the Widget Blueprint with `create_widget_blueprint`.
3. Build a valid panel hierarchy with `add_widget`; create exactly one root.
4. Configure widget and slot properties; use `set_canvas_slot_layout` for
   Canvas children.
5. Build the UI-domain material through the Material library, compile it, and
   assign it with `set_widget_brush`.
6. On UE 5.7+, create a ViewModel Blueprint, add typed variables through the
   Blueprint library, mark reactive fields with `set_view_model_field_notify`,
   then configure sources and bindings.
7. Author animation keys in batches.
8. Call `compile_and_validate_widget_blueprint(save=True, ...)`; resolve every
   error and review every warning.
9. Start PIE in a separate bridge call, spawn the screen, and validate live
   state and semantics.
10. Remove spawned instances before stopping PIE. If the assets were only for
    validation, delete their exact dedicated folder and prove both the Asset
    Registry and backing files are empty.

Asset creation and save calls can open UE asset-reference progress work. Keep
each create/save in its own bridge exec. Tree/property/animation mutations may
be batched between those boundaries.

## Value and path conventions

- Asset parameters accept package paths (`/Game/UI/WBP_Menu`) or full object
  paths (`/Game/UI/WBP_Menu.WBP_Menu`).
- Widget classes should use a native class path such as
  `/Script/UMG.TextBlock`, or a loaded generated Widget class path.
- `set_widget_property`, `set_widget_slot_property`, and live generic property
  calls use UE `ImportText`/`ExportText` syntax.
- Dotted paths traverse reflected structs/objects: `Font.Size`,
  `WidgetStyle.Normal`, and similar paths are valid when every segment exists.
- Widget names are object names, not display labels. They must be unique inside
  one WidgetTree.
- Python strips the C++ `b` prefix from booleans in returned structs:
  `bSuccess` becomes `.success`, `bIsVariable` becomes `.is_variable`.

## Widget classes and asset/tree authoring

### list_widget_classes(query, include_abstract, max_results) -> list[FBridgeWidgetClassInfo]

Discover loaded `UWidget` subclasses accepted by `add_widget`. Filter before
using a large result.

```python
from unreal_bridge import UMG

for item in UMG.list_widget_classes(
    query="CanvasPanel", include_abstract=False, max_results=20
):
    print(item.name, item.class_path, item.is_panel)
```

`FBridgeWidgetClassInfo` fields:

| Field | Meaning |
|---|---|
| `name` | Native/generated class name |
| `class_path` | Path accepted by `add_widget` |
| `display_name` | Editor display name |
| `category` | Palette category |
| `is_panel` | Class derives from `UPanelWidget` |
| `can_have_multiple_children` | Multi-child vs. single-child panel |
| `is_abstract` | Abstract class flag |

### create_widget_blueprint(asset_path, parent_class_path) -> FBridgeWidgetOperationResult

Create an empty Widget Blueprint. Pass `""` for a standard `UserWidget`
parent, or a valid `UUserWidget` subclass path.

```python
r = UMG.create_widget_blueprint(
    asset_path="/Game/UI/WBP_Status",
    parent_class_path="",
)
assert r.success, r.error
```

`FBridgeWidgetOperationResult` fields: `success`, `path`, `name`, `error`.

### add_widget(widget_blueprint_path, widget_class_path, widget_name, parent_name, insert_index) -> FBridgeWidgetOperationResult

Add a WidgetTree template.

- Empty `parent_name` creates the root and is valid only while the tree has no
  root.
- A non-empty parent must be a panel that can accept another child.
- `insert_index=-1` appends; a non-negative value requests that child index.

```python
UMG.add_widget(
    widget_blueprint_path=wbp,
    widget_class_path="/Script/UMG.CanvasPanel",
    widget_name="RootCanvas",
    parent_name="",
    insert_index=-1,
)
UMG.add_widget(
    widget_blueprint_path=wbp,
    widget_class_path="/Script/UMG.TextBlock",
    widget_name="StatusLabel",
    parent_name="RootCanvas",
    insert_index=-1,
)
```

### remove_widget(widget_blueprint_path, widget_name) -> bool

Remove the widget and descendants. The operation also cleans related legacy
event/property bindings, animation bindings/tracks, and MVVM destination
bindings so the Blueprint is not left with stale references.

### rename_widget(widget_blueprint_path, widget_name, new_name) -> bool

Rename a widget and update Blueprint variable references, legacy bindings,
animation bindings/possessables, and MVVM widget paths/extensions.

### reparent_widget(widget_blueprint_path, widget_name, new_parent_name, insert_index) -> bool

Move a non-root widget under another panel. The new slot class follows the new
parent; review/reapply slot properties after moving between panel types.

### set_widget_is_variable(widget_blueprint_path, widget_name, is_variable) -> bool

Control whether the compiled `UUserWidget` exposes the template as a member.
Make widgets referenced by Blueprint logic, animations, or external code
variables; presentational leaves may remain non-variable.

## Tree and property inspection

### get_widget_tree(widget_blueprint_path) -> list[FBridgeWidgetInfo]

Return the hierarchy as a flat, parent-linked list.

`FBridgeWidgetInfo` fields:

| Field | Meaning |
|---|---|
| `name` | Widget object name |
| `widget_class` | Short class name, e.g. `TextBlock` |
| `parent_name` | Empty for root |
| `slot_type` | Parent slot class, empty for root |
| `is_variable` | Compiled member-variable flag |
| `visibility` | Current visibility enum name |

Large production HUDs may contain hundreds of widgets. When looking for one
control, call `search_widgets` first.

### search_widgets(widget_blueprint_path, query) -> list[FBridgeWidgetInfo]

Case-insensitive name/class substring search.

### get_widget_properties(widget_blueprint_path, widget_name) -> list[FBridgeWidgetPropertyValue]

Return non-default reflected values on a widget template.

### get_widget_slot_properties(widget_blueprint_path, widget_name) -> list[FBridgeWidgetPropertyValue]

Return non-default values from the widget's parent slot.

`FBridgeWidgetPropertyValue` fields: `name`, `type`, `value`. Values are UE
export text and can normally be passed back into the corresponding setter.

## Property, layout, and style authoring

### set_widget_property(widget_blueprint_path, widget_name, property_name, value) -> bool

Write a widget-template property from ImportText.

```python
UMG.set_widget_property(
    widget_blueprint_path=wbp,
    widget_name="Title",
    property_name="Text",
    value="Mission Complete",
)
UMG.set_widget_property(
    widget_blueprint_path=wbp,
    widget_name="Title",
    property_name="Font.Size",
    value="32",
)
```

Common values:

| Type | ImportText example |
|---|---|
| bool/number | `true`, `0.75`, `24` |
| enum | `Collapsed`, `Center`, `Checked` |
| FVector2D | `(X=20,Y=12)` |
| FLinearColor | `(R=0.1,G=0.5,B=1,A=1)` |
| object | `/Game/UI/M_UI.M_UI` |

If a write returns false, inspect the widget's property list/class before
guessing another spelling.

### set_widget_slot_property(widget_blueprint_path, widget_name, property_name, value) -> bool

Write a property on the current `UPanelSlot` using the same syntax. Examples
include `Padding`, `HorizontalAlignment`, `VerticalAlignment`, and `ZOrder`
when supported by that slot class.

### set_canvas_slot_layout(widget_blueprint_path, widget_name, position, size, anchor_minimum, anchor_maximum, alignment, auto_size, z_order) -> bool

Typed convenience for `UCanvasPanelSlot`. It rejects widgets not currently in
a Canvas slot.

```python
import unreal

UMG.set_canvas_slot_layout(
    widget_blueprint_path=wbp,
    widget_name="StatusLabel",
    position=unreal.Vector2D(0, -120),
    size=unreal.Vector2D(500, 54),
    anchor_minimum=unreal.Vector2D(0.5, 0.5),
    anchor_maximum=unreal.Vector2D(0.5, 0.5),
    alignment=unreal.Vector2D(0.5, 0.5),
    auto_size=False,
    z_order=2,
)
```

Use anchors for the intended responsive behavior; do not fake every screen
with absolute top-left pixel offsets.

### set_widget_brush(widget_blueprint_path, widget_name, brush_property_path, resource_path, tint, draw_as, image_size, margin) -> bool

Assign a Texture, Material, or Material Instance to an `FSlateBrush` property.
An empty `resource_path` clears the resource. Dotted brush paths support style
members such as `WidgetStyle.Normal`.

`draw_as` accepts the `ESlateBrushDrawType` name (commonly `Image`, `Box`,
`Border`, or `NoDrawType`).

```python
import unreal

UMG.set_widget_brush(
    widget_blueprint_path=wbp,
    widget_name="Backdrop",
    brush_property_path="Brush",
    resource_path="/Game/UI/M_UI_Backdrop",
    tint=unreal.LinearColor(1.0, 1.0, 1.0, 1.0),
    draw_as="Image",
    image_size=unreal.Vector2D(640, 420),
    margin=unreal.Margin(),
)
```

### UI material workflow

Build the shader graph through `Material`, not by trying to encode it inside a
brush:

```python
from unreal_bridge import Material

r = Material.create_material(
    path="/Game/UI/M_UI_Backdrop",
    domain="UI",
    shading_model="Unlit",
    blend_mode="Translucent",
    two_sided=False,
    use_material_attributes=False,
)
assert r.success, r.error

# Add Vector/Scalar parameters and connect UI final colour/opacity via
# apply_material_graph_ops; see bridge-material-api.md. Compile and save in a
# separate bridge exec, then assign it with set_widget_brush.
```

For a deliverable screen, compile the material and check
`get_material_compile_errors` before compiling the Widget Blueprint.

## Compile and deliverability validation

### compile_and_validate_widget_blueprint(widget_blueprint_path, save, check_accessibility) -> FBridgeWidgetValidationReport

Validate structural invariants, compile the Widget Blueprint and MVVM view,
and optionally save only when compilation succeeds.

```python
report = UMG.compile_and_validate_widget_blueprint(
    widget_blueprint_path=wbp,
    save=True,
    check_accessibility=True,
)
for issue in report.issues:
    print(issue.severity, issue.code, issue.widget_name, issue.message)
assert report.compile_succeeded and report.saved
```

`FBridgeWidgetValidationReport` fields:

| Field | Meaning |
|---|---|
| `found` | Widget Blueprint loaded |
| `compiled` | Compile was attempted |
| `compile_succeeded` | `UpToDate` or `UpToDateWithWarnings`, with no MVVM binding errors |
| `saved` | Package save succeeded when requested |
| `compile_status` | Blueprint status string |
| `issues` | Validation/compiler findings |

`FBridgeWidgetValidationIssue` fields: `severity`, `code`, `widget_name`,
`message`.

Stable issue codes:

| Code | Severity | Meaning |
|---|---|---|
| `UMG_NO_ROOT` | error | WidgetTree has no root |
| `UMG_DUPLICATE_NAME` | error | Duplicate widget object name |
| `UMG_MISSING_SLOT` | error | Non-root widget has no parent slot |
| `UMG_INTERACTIVE_NOT_ACCESSIBLE` | warning | Interactive widget explicitly uses `NotAccessible` |
| `UMG_INVALID_ANIMATION` | error | Animation lacks a MovieScene |
| `UMG_ANIMATION_MISSING_WIDGET` | error | Animation targets a removed widget |
| `UMG_ANIMATION_MISSING_POSSESSABLE` | error | Stale animation binding GUID |
| `UMG_COMPILE_FAILED` | error | Widget Blueprint compiler failed |
| `UMG_MVVM_BINDING_ERROR` | error | MVVM compiler rejected a binding |
| `UMG_MVVM_BINDING_WARNING` | warning | MVVM compiler warning |
| `UMG_SAVE_FAILED` | error | Package could not be saved |

## Widget animation authoring

### create_widget_animation(widget_blueprint_path, animation_name, duration_seconds, display_rate) -> bool

Create an animation with a finite playback range. Names must be valid UObject
names and unique in the Widget Blueprint.

### add_widget_animation_float_keys(widget_blueprint_path, animation_name, widget_name, property_name, keys, interpolation) -> bool

Batch-add float keys. `RenderOpacity` is the common entrance/exit use case.

### add_widget_animation_color_keys(widget_blueprint_path, animation_name, widget_name, property_name, keys, interpolation) -> bool

Batch-add four-channel colour keys. Common properties include Image
`ColorAndOpacity` and Border `BrushColor`.

### add_widget_animation_transform_keys(widget_blueprint_path, animation_name, widget_name, keys, interpolation) -> bool

Batch-add complete `RenderTransform` keys: translation, scale, shear, angle.

Interpolation strings are case-insensitive: `Constant`, `Linear`, or `Auto`
(unknown values also use auto cubic interpolation).

```python
import unreal

UMG.create_widget_animation(
    widget_blueprint_path=wbp,
    animation_name="Intro",
    duration_seconds=0.5,
    display_rate=60,
)

keys = []
for time, value in ((0.0, 0.0), (0.5, 1.0)):
    key = unreal.BridgeWidgetFloatKey()
    key.time = time
    key.value = value
    keys.append(key)

assert UMG.add_widget_animation_float_keys(
    widget_blueprint_path=wbp,
    animation_name="Intro",
    widget_name="Panel",
    property_name="RenderOpacity",
    keys=keys,
    interpolation="Auto",
)
```

Key structs:

- `FBridgeWidgetFloatKey`: `time`, `value`.
- `FBridgeWidgetColorKey`: `time`, `value` (`FLinearColor`).
- `FBridgeWidgetTransformKey`: `time`, `translation`, `scale`, `shear`,
  `angle`.

### get_widget_animations(widget_blueprint_path) -> list[FBridgeWidgetAnimationInfo]

`FBridgeWidgetAnimationInfo`: `name`, `duration`, `display_rate`, `tracks`.

`FBridgeWidgetAnimTrack`: `widget_name`, `track_type`, `display_name`,
`property_name`, `key_count`.

`key_count` is the aggregate number of channel keys: two colour keyframes
report eight keys and two complete 2D transform keyframes report fourteen.

### remove_widget_animation_track(widget_blueprint_path, animation_name, widget_name, property_name) -> int

Remove every matching property track and return the count removed.

### remove_widget_animation(widget_blueprint_path, animation_name) -> bool

Remove the complete animation.

## MVVM authoring (UE 5.7+)

Prefer MVVM for stateful production UI. It keeps gameplay/domain state out of
Widget event graphs and makes runtime validation deterministic.

### create_mvvm_view_model_blueprint(asset_path, parent_class_path) -> FBridgeWidgetOperationResult

Create a Blueprint derived from `UMVVMViewModelBase`. Pass `""` for that
default base, or another compatible ViewModel class.

Use `Blueprint.add_blueprint_variable` for fields, then:

### set_view_model_field_notify(view_model_blueprint_path, variable_name, enabled) -> bool

Toggle the Blueprint variable's `FieldNotify` metadata. Reactive source fields
for `OneWayToDestination` or `TwoWay` should be FieldNotify-enabled. Compile
and save the ViewModel before adding it to a Widget Blueprint.

```python
from unreal_bridge import Blueprint, Editor, UMG

vm = "/Game/UI/VM_Status"
UMG.create_mvvm_view_model_blueprint(asset_path=vm, parent_class_path="")
Blueprint.add_blueprint_variable(
    blueprint_path=vm,
    name="HealthPercent",
    type_string="Float",
    default_value="1.0",
)
UMG.set_view_model_field_notify(
    view_model_blueprint_path=vm,
    variable_name="HealthPercent",
    enabled=True,
)
Editor.compile_blueprints(blueprint_paths=[vm])
```

### add_mvvm_view_model(widget_blueprint_path, view_model_name, view_model_class_path, creation_type, creation_data, optional, create_getter, create_setter) -> str

Return the ViewModel source GUID string, or empty on failure. The class must
implement `NotifyFieldValueChanged`.

Creation types:

| Type | `creation_data` | Notes |
|---|---|---|
| `CreateInstance` | empty | Widget owns a fresh instance; easiest testable default |
| `Manual` | empty | Caller supplies instance; forced optional + setter |
| `GlobalViewModelCollection` | global identifier | Resolve from global collection |
| `PropertyPath` | property path string | Resolve from an owning property path |
| `Resolver` | empty | Uses configured compatible default resolver |

### add_mvvm_binding(widget_blueprint_path, view_model_name, source_field_path, destination_widget_name, destination_field_path, mode) -> str

Create a binding and return its GUID. Source/destination field paths are
dot-separated reflected property paths. Use destination widget `self` to bind
to the `UUserWidget` itself.

Modes:

- `OneTimeToDestination`
- `OneWayToDestination`
- `TwoWay`
- `OneWayToSource`

```python
vm_id = UMG.add_mvvm_view_model(
    widget_blueprint_path=wbp,
    view_model_name="UIState",
    view_model_class_path="/Game/UI/VM_Status.VM_Status_C",
    creation_type="CreateInstance",
    creation_data="",
    optional=False,
    create_getter=True,
    create_setter=False,
)
assert vm_id

binding_id = UMG.add_mvvm_binding(
    widget_blueprint_path=wbp,
    view_model_name="UIState",
    source_field_path="HealthPercent",
    destination_widget_name="HealthBar",
    destination_field_path="Percent",
    mode="OneWayToDestination",
)
assert binding_id
```

### set_mvvm_view_settings(widget_blueprint_path, initialize_sources_on_construct, initialize_bindings_on_construct, initialize_events_on_construct, create_view_without_bindings) -> bool

Configure generated-view startup. A normal runtime screen typically sets the
first three values true and `create_view_without_bindings` false.

### get_mvvm_view_models(widget_blueprint_path) -> list[FBridgeMVVMViewModelInfo]

Fields: `id`, `name`, `class_path`, `creation_type`, `optional`,
`create_getter`, `create_setter`.

### get_mvvm_bindings(widget_blueprint_path) -> list[FBridgeMVVMBindingInfo]

Fields: `id`, `source_path`, `destination_path`, `mode`, `enabled`, `compile`,
`errors`, `warnings`.

Do not accept an empty error list alone as success. Require `enabled=True`,
`compile=True`, and a clean `compile_and_validate_widget_blueprint` report.

### remove_mvvm_binding(widget_blueprint_path, binding_id) -> bool

### remove_mvvm_view_model(widget_blueprint_path, view_model_name) -> bool

Removing a ViewModel also removes bindings that reference that source.

## Legacy binding/event inspection

### get_widget_bindings(widget_blueprint_path) -> list[FBridgeWidgetBindingInfo]

Inspect legacy UMG property bindings. Fields: `widget_name`, `property_name`,
`function_name`, `kind` (`Function` or `Property`). Prefer MVVM for new work.

### get_widget_events(widget_blueprint_path) -> list[FBridgeWidgetEventInfo]

Inspect Widget event nodes/bindings. Fields: `widget_name`, `event_name`,
`handler_name`.

These are introspection APIs, not event-graph authoring APIs. Use the Blueprint
library only when the user explicitly authorizes Blueprint graph generation
and then apply the Blueprint review loop.

## PIE live validation

Start PIE in its own bridge call and wait until `Editor.is_in_pie()` is true.
Handles returned below are process/session-local weak handles; never persist
them across PIE stop or editor restart.

### spawn_widget_instance(widget_blueprint_path, z_order) -> str

Instantiate the compiled Widget Blueprint in the PIE world and add it to the
viewport. Returns an empty string outside PIE or on failure.

### get_live_widget_tree(instance_handle) -> list[FBridgeLiveWidgetInfo]

Fields: `name`, `widget_class`, `parent_name`, `visibility`, `enabled`,
`has_keyboard_focus`, `render_opacity`, `desired_size`, `absolute_position`,
`absolute_size`.

Geometry becomes meaningful only after Slate has ticked. Spawn in one exec,
then query in a later exec.

### get_live_widget_property(instance_handle, widget_name, property_path) -> str

### set_live_widget_property(instance_handle, widget_name, property_path, value) -> bool

Generic reflected ExportText/ImportText access. Prefer the semantic setters
below for Text, Slider/ProgressBar, CheckBox, focus, and materials because they
call the widgets' public APIs.

### Semantic interaction helpers

| Function | Contract |
|---|---|
| `click_live_button(handle, widget_name)` | Broadcast the Button's semantic `OnClicked` delegate |
| `set_live_widget_text(handle, widget_name, text)` | TextBlock, EditableText, or EditableTextBox public setter |
| `set_live_widget_value(handle, widget_name, value)` | Slider or ProgressBar public setter |
| `set_live_widget_checked(handle, widget_name, checked)` | CheckBox public setter |
| `focus_live_widget(handle, widget_name)` | Request keyboard focus and verify it |

`click_live_button` is deterministic functional validation, not simulated
pointer routing through Slate. Use automation/input tooling when the acceptance
criterion specifically concerns hit-testing, hover, capture, or navigation.

### play_live_widget_animation(instance_handle, animation_name, start_time, num_loops, play_mode, playback_speed) -> bool

`play_mode`: `Forward`, `Reverse`, or `PingPong`. Query animated properties in
a later exec while a slow playback is active to prove intermediate state.

### stop_live_widget_animation(instance_handle, animation_name) -> bool

### set_live_widget_material_scalar(instance_handle, widget_name, parameter_name, value) -> bool

### set_live_widget_material_vector(instance_handle, widget_name, parameter_name, value) -> bool

Create/reuse the dynamic material for an Image or Border material brush, then
set the parameter. The material parameter must exist in the compiled graph.

### get_live_view_model_property(instance_handle, view_model_name, property_path) -> str

Read a reflected field from a live MVVM source.

### set_live_view_model_property(instance_handle, view_model_name, property_path, value) -> bool

Write the field, broadcast FieldNotify, and execute affected bindings. This is
the preferred proof that ViewModel-to-widget state propagation works.

For a `TwoWay` binding, also mutate the destination widget through its semantic
setter in one exec and read the ViewModel plus other dependent widgets in a
later exec.

```python
handle = UMG.spawn_widget_instance(
    widget_blueprint_path=wbp,
    z_order=9000,
)
assert handle

assert UMG.set_live_view_model_property(
    instance_handle=handle,
    view_model_name="UIState",
    property_path="HealthPercent",
    value="0.25",
)
print(UMG.get_live_widget_property(
    instance_handle=handle,
    widget_name="HealthBar",
    property_path="Percent",
))
```

### remove_widget_instance(instance_handle) -> bool

Remove one viewport instance and invalidate the handle.

### remove_all_widget_instances() -> int

Remove every live instance spawned through this library. Call it in a cleanup
path even if individual removals already succeeded. Then stop PIE.

## Offscreen Widget Render

### render_widget_blueprint_to_png(widget_blueprint_path, logical_width, logical_height, scale, output_file) -> FBridgeWidgetRenderResult

Instantiate a compiled Widget Blueprint in the editor world and render its
Slate composition into a transparent PNG without opening PIE or the UMG
designer. The renderer performs a warm-up draw, completes pending shader
compilation, and then records the evidence draw so material-backed brushes are
not silently omitted.

Use `scale=1.0` for the baseline image of a viewport-anchored CanvasPanel. A
non-unit scale also changes the render target size and is intended for explicit
supersampling tests, not as a substitute for project DPI layout.

```python
from unreal_bridge import UMG

result = UMG.render_widget_blueprint_to_png(
    widget_blueprint_path='/Game/UI/WBP_Main',
    logical_width=1280,
    logical_height=720,
    scale=1.0,
    output_file='K:/Project/Key/Saved/UIValidation/iteration-001/actual-design.png')
print(result.success, result.output_file, result.width, result.height, result.error)
```

### FBridgeWidgetRenderResult fields

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether the PNG export completed. |
| `output_file` | str | Absolute output PNG path. |
| `width` | int | Physical output width. |
| `height` | int | Physical output height. |
| `error` | str | Validation or export failure; empty on success. |

---

## Transactional Widget Tree Batch

### apply_widget_tree_batch(widget_blueprint_path, patch_json, compile_after, save_after) -> FBridgeWidgetBatchResult

Preflight and apply a WidgetTree patch as one editor transaction. Supported operations are `create`, `set`, `move`, `reorder`, and `delete`. The preflight rejects duplicate names, multiple roots, root moves/deletion, missing or non-panel parents, single-child-panel overflow, cycles, invalid widget/slot properties, malformed property objects and typed options, and delete operations without `confirm_delete: true` before changing the asset.

Moves preserve compatible slot properties and allow explicit slot overrides. A failure encountered during application invokes editor undo for the transaction.

```python
import json

patch = {
    "operations": [
        {
            "op": "create",
            "name": "QuestIcon",
            "class": "/Script/UMG.Image",
            "parent": "RootCanvas",
            "index": 0,
            "properties": {"Visibility": "SelfHitTestInvisible"},
            "slot_properties": {
                "LayoutData": "(Offsets=(Left=40,Top=210,Right=48,Bottom=48),Anchors=(Minimum=(X=0,Y=0),Maximum=(X=0,Y=0)),Alignment=(X=0,Y=0))"
            }
        },
        {"op": "set", "widget": "QuestIcon", "properties": {"RenderOpacity": 0.9}},
        {"op": "reorder", "widget": "QuestIcon", "index": 2},
        {"op": "move", "widget": "QuestIcon", "parent": "HUDOverlay", "index": 0}
    ]
}

r = unreal.UnrealBridgeUMGLibrary.apply_widget_tree_batch(
    '/Game/UI/WBP_Main', json.dumps(patch), True, True)
print(r.success, r.rolled_back, r.applied_operations, r.error)
print('\n'.join(r.messages))
```

Deletion must be explicit:

```json
{"op":"delete","widget":"ObsoletePanel","confirm_delete":true}
```

### FBridgeWidgetBatchResult fields

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | True only after every requested operation and optional compile/save completed. |
| `rolled_back` | bool | True when an application-time failure triggered transaction undo. |
| `applied_operations` | int | Number of operations applied before completion or rollback. |
| `error` | str | Preflight or application error. Empty on success. |
| `messages` | list[str] | Per-operation validation/application trace. |

The operation/property schema is intentionally reflection-driven. Use exported UE property text for complex structs. Restart KeyEditor and regenerate the bridge manifest/client after adding this reflected API.

## Acceptance checklist

A screen is not deliverable until all applicable checks pass:

- hierarchy has one root and no missing slots;
- names and `is_variable` flags match intended code/binding usage;
- anchors/layout are appropriate at target resolutions;
- text, interactive widgets, focus behavior, and accessibility intent are set;
- UI material domain/blend mode/parameters compile cleanly;
- animation tracks target existing widgets and show intermediate live values;
- MVVM sources and every binding report `enabled=True`, `compile=True`, with no
  errors;
- Widget Blueprint validation compiles and saves with no errors;
- PIE live tree reports non-zero geometry;
- one-way and two-way state propagation are read back from live instances;
- semantic controls and dynamic material writes succeed;
- spawned instances are removed and PIE is stopped;
- temporary validation asset directory is empty in Asset Registry and on disk.
