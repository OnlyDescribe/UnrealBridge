# UnrealBridge UMG Library API

Module: `unreal.UnrealBridgeUMGLibrary`

## Widget Tree

### get_widget_tree(widget_blueprint_path) -> list[FBridgeWidgetInfo]

Get the widget hierarchy of a Widget Blueprint as a flat list with parent references.

> ⚠️ **Token cost: MEDIUM–HIGH on complex HUDs.** No result cap. A production HUD or inventory screen can hold 100–400+ widgets, each emitting 6 fields. When you only need one widget, use `search_widgets(path, query)` to locate it first, then `get_widget_properties(path, widget_name)` for detail.

```python
widgets = unreal.UnrealBridgeUMGLibrary.get_widget_tree('/Game/UI/WBP_MainMenu')
for w in widgets:
    indent = '  ' if w.parent_name else ''
    var_tag = ' [var]' if w.is_variable else ''
    print(f'{indent}{w.name} ({w.widget_class}) slot={w.slot_type} vis={w.visibility}{var_tag}')
```

### FBridgeWidgetInfo fields

| Field | Type | Description |
|-------|------|-------------|
| `name` | str | Widget name |
| `widget_class` | str | e.g. "CanvasPanel", "TextBlock", "Button" |
| `parent_name` | str | Parent widget name (empty for root) |
| `slot_type` | str | Slot class if parented, e.g. "CanvasPanelSlot" |
| `is_variable` | bool | Exposed as a variable in the Blueprint |
| `visibility` | str | "Visible", "Collapsed", "Hidden", "HitTestInvisible", "SelfHitTestInvisible" |

---

## Widget Properties

### get_widget_properties(widget_blueprint_path, widget_name) -> list[FBridgeWidgetPropertyValue]

Get non-default property values for a specific widget. Only returns properties that differ from class defaults.

```python
props = unreal.UnrealBridgeUMGLibrary.get_widget_properties('/Game/UI/WBP_Main', 'TitleText')
for p in props:
    print(f'{p.name} ({p.type}) = {p.value}')
```

### FBridgeWidgetPropertyValue fields

| Field | Type | Description |
|-------|------|-------------|
| `name` | str | Property name |
| `type` | str | C++ type string |
| `value` | str | Exported text value |

---

## Widget Animations

### get_widget_animations(widget_blueprint_path) -> list[FBridgeWidgetAnimationInfo]

Get all widget animations with tracks, durations, and which widgets they target.

```python
anims = unreal.UnrealBridgeUMGLibrary.get_widget_animations('/Game/UI/WBP_Main')
for a in anims:
    print(f'{a.name} ({a.duration}s)')
    for t in a.tracks:
        print(f'  {t.widget_name}: {t.display_name} ({t.track_type})')
```

### FBridgeWidgetAnimationInfo fields

| Field | Type | Description |
|-------|------|-------------|
| `name` | str | Animation display name |
| `duration` | float | Duration in seconds |
| `tracks` | list[FBridgeWidgetAnimTrack] | Animated tracks |

### FBridgeWidgetAnimTrack fields

| Field | Type | Description |
|-------|------|-------------|
| `widget_name` | str | Target widget name |
| `track_type` | str | Track class name |
| `display_name` | str | Human-readable track name |

---

## Widget Bindings

### get_widget_bindings(widget_blueprint_path) -> list[FBridgeWidgetBindingInfo]

Get all property bindings (e.g. Text bound to a function, Visibility bound to a property).

```python
bindings = unreal.UnrealBridgeUMGLibrary.get_widget_bindings('/Game/UI/WBP_Main')
for b in bindings:
    print(f'{b.widget_name}.{b.property_name} -> {b.function_name} ({b.kind})')
```

### FBridgeWidgetBindingInfo fields

| Field | Type | Description |
|-------|------|-------------|
| `widget_name` | str | The widget this binding is on |
| `property_name` | str | Bound property, e.g. "Text", "Visibility" |
| `function_name` | str | Function or property providing the value |
| `kind` | str | "Function" or "Property" |

---

## Widget Events

### get_widget_events(widget_blueprint_path) -> list[FBridgeWidgetEventInfo]

Get widget event bindings from the event graph (OnClicked, OnHovered, etc.).

```python
events = unreal.UnrealBridgeUMGLibrary.get_widget_events('/Game/UI/WBP_Main')
for e in events:
    print(f'{e.widget_name}.{e.event_name} -> {e.handler_name}')
```

### FBridgeWidgetEventInfo fields

| Field | Type | Description |
|-------|------|-------------|
| `widget_name` | str | The widget this event is on |
| `event_name` | str | Event name, e.g. "OnClicked", "OnHovered" |
| `handler_name` | str | Bound function or node description |

---

## Search Widgets

### search_widgets(widget_blueprint_path, query) -> list[FBridgeWidgetInfo]

Search widgets by name or class substring. Returns FBridgeWidgetInfo (same as GetWidgetTree).

```python
buttons = unreal.UnrealBridgeUMGLibrary.search_widgets('/Game/UI/WBP_Main', 'Button')
texts = unreal.UnrealBridgeUMGLibrary.search_widgets('/Game/UI/WBP_Main', 'Health')
```

---

## Set Widget Property

### set_widget_property(widget_blueprint_path, widget_name, property_name, value) -> bool

Set a design-time property on a widget. Value is parsed as text.

```python
ok = unreal.UnrealBridgeUMGLibrary.set_widget_property(
    '/Game/UI/WBP_Main', 'TitleText', 'Text', 'Hello World'
)
```

---

## PIE Widget Preview

### add_widget_blueprint_to_pie_viewport(widget_blueprint_path, z_order) -> bool

Instantiate a `UUserWidget` Blueprint in the active PIE viewport without
changing the Widget Blueprint asset. Use this for runtime visual validation of
a screen that the current game flow does not open automatically.

```python
unreal.UnrealBridgeUMGLibrary.add_widget_blueprint_to_pie_viewport(
    '/Game/UI/WBP_Main', 100)
```

### remove_pie_preview_widgets() -> int

Remove every still-live widget added through the preview helper and return the
number removed. PIE teardown also destroys them.

---

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

Preflight and apply a WidgetTree patch as one editor transaction. Supported operations are `create`, `set`, `move`, `reorder`, and `delete`. The preflight rejects duplicate names, missing/non-panel parents, single-child-panel overflow, cycles, invalid classes/properties, root deletion, and delete operations without `confirm_delete: true` before changing the asset.

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
