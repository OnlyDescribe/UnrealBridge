# UnrealBridge Control Rig / IK Rig / IK Retargeter API

Module: `unreal.UnrealBridgeRigLibrary`

Preferred kwargs-only wrapper: `from unreal_bridge import Rig`

This library is the authoring and delivery surface for Control Rig hierarchy
and RigVM graphs, IK Rig solvers/goals/retarget chains, IK Retargeter operation
stacks/mappings/poses/profiles, batch animation retargeting, transient Control
Rig evaluation, and sampled animation-quality checks. Use the dedicated
controllers here instead of mutating RigVM, IK Rig, or Retargeter internals
through raw properties.

> **Version gate:** the functional implementation requires UE 5.7+. UE
> 5.3-5.6 retain the reflected library and kwargs wrapper through generated
> safe stubs. Check `Rig.is_rig_api_available()` first and read
> `Rig.get_last_rig_error()` if it is false. Lower-version calls log the
> actionable `requires UE 5.7+` reason and never block the plugin build.

> **Discover before writing:** Control Rig units, RigVM templates, IK solvers,
> Retargeter ops, graph names, node names, pin paths, and reflected setting
> fields are engine/plugin dependent. Obtain them from `list_rig_types`, the
> relevant `list_*` call, or property discovery. Never invent a `/Script/...`
> type path or reconstruct a pin path from its display label.

> **Saving is explicit:** asset edits are transactional and mark packages
> dirty. Control Rig, IK Rig, and Retargeter validation functions accept
> `save=True`; batch retargeting has its own `save` argument. Validate before
> saving. A successful mutation alone is not a deliverable rig.

## Mandatory delivery workflow

Use a single bridge heredoc for each coherent author/validate unit, but keep PIE
or other asynchronous editor transitions in separate bridge calls.

1. Confirm `is_rig_api_available()` and identify exact source/target skeletal
   meshes, skeletons, and representative animation sequences.
2. Call `list_rig_types` for every unit, template, solver, or retarget op you
   intend to add. Record returned type paths/notations verbatim.
3. Create the Control Rig from its target Skeletal Mesh or Skeleton, author and
   re-list the hierarchy, then build a connected RigVM graph from discovered
   node and pin identities.
4. Auto-layout, compile, and call `validate_control_rig(save=True)`. Resolve all
   errors and review every warning.
5. Call `evaluate_control_rig` on a transient instance with at least one
   non-default input. Check the returned controls and bones, not only
   `success`.
6. Create or configure each IK Rig. Validate preview mesh, retarget root,
   chains, goals, solver connections, excluded bones, and reflected settings;
   require `validate_ik_rig(save=True)` to succeed.
7. Create the IK Retargeter, configure both rigs/meshes, add/review its op
   stack, map every required chain, author representative poses/profiles, and
   require `validate_ik_retargeter(initialize_processor=True, save=True)` to
   succeed.
8. Batch-retarget representative animations and run
   `analyze_animation_quality`. Review root motion, foot contact, and angular
   discontinuities, then visually play the result for artistic acceptance.
9. If this was validation-only work, delete the exact dedicated folder,
   including every retargeted output, and prove both Asset Registry and backing
   files are empty.

## Capability and type discovery

| Function | Contract |
|---|---|
| `is_rig_api_available()` | `True` only when the UE 5.7+ implementation is compiled. |
| `get_last_rig_error()` | Most recent library diagnostic. Read immediately after an unexpected empty string/list, `False`, or `-1`. |
| `list_rig_types(kind, query, max_results)` | Discovers loaded `ControlRigUnit`, `RigVMTemplate`, `IKSolver`, or `RetargetOp` types. Empty `kind` lists all categories. `query` matches path, display name, or category. |

For `ControlRigUnit`, `IKSolver`, and `RetargetOp`, pass the returned
`type_path` verbatim. For `RigVMTemplate`, the returned `type_path` is the
template notation accepted by `add_control_rig_template_node`. Do not choose a
deprecated entry unless migrating an existing asset.

## Control Rig asset and hierarchy

### Create and inspect

| Function | Notes |
|---|---|
| `create_control_rig(asset_path, source_skeletal_asset_path, modular_rig, import_curves)` | Creates through the Control Rig factory. Source may be a `USkeletalMesh` or `USkeleton`; a mesh also becomes the preview mesh. `asset_path` is `/Game/Folder/AssetName`, without `.uasset`. |
| `get_control_rig_info(asset_path)` | Preview mesh/generated class, hierarchy counts, graph/node counts, modular flag, and dirty state. |
| `import_control_rig_hierarchy(asset_path, source_skeletal_asset_path, replace_existing, import_curves)` | Imports bones and optional curves through the hierarchy controller and updates source-import metadata. Use `replace_existing` deliberately; it can replace existing imported content. |
| `list_control_rig_elements(asset_path, element_type)` | Lists transforms, parent identity, control type, tags, and imported-bone state. Empty/`All` returns the full hierarchy. |

Concrete element types are `Bone`, `Null` (`Space` is accepted as input),
`Control`, `Curve`, `Connector`, `Socket`, and `Reference`. Parent arguments
always carry both a name and a concrete type; names alone are ambiguous in a
Rig hierarchy.

### Hierarchy writes

| Function | Notes |
|---|---|
| `add_control_rig_bone(...)` | Adds user/imported bone with local or global initial transform. |
| `add_control_rig_null(...)` | Adds a transform null/space. Use nulls for clean control offsets and logical grouping. |
| `add_control_rig_control(...)` | Adds an animatable or proxy control, offset/shape transforms, shape/color, and an export-text initial value. |
| `add_control_rig_curve(asset_path, name, value)` | Adds a float hierarchy curve. |
| `add_control_rig_connector(...)` | Adds modular-rig connector metadata. Connector types are engine enum names such as primary/secondary; discover current values before authoring modular assets. |
| `remove_control_rig_element(...)` | Removes the exact name+type identity. Re-list afterward because dependent hierarchy state may change. |
| `rename_control_rig_element(...)` | Renames through the hierarchy controller and returns the actual unique name. |
| `reparent_control_rig_element(...)` | Reparents with optional global-transform preservation. Empty parent detaches to root. |
| `set_control_rig_element_transform(...)` | Writes initial/current and local/global transform with optional child propagation. |
| `set_control_rig_control_shape(...)` | Updates shape name, color, and visibility. Shape availability depends on project Control Rig shape libraries. |
| `add_control_rig_element_tag(...)` / `remove_control_rig_element_tag(...)` | Edits hierarchy tags without bypassing hierarchy metadata. |

Supported control-value types include `Bool`, `Float`, `ScaleFloat`, `Integer`,
`Vector2D`, `Position`, `Scale`, `Rotator`, `Transform`,
`TransformNoScale`, and `EulerTransform`. `initial_value` uses Unreal export
text, for example `True`, `1.0`, `(X=0,Y=0)`, `(X=0,Y=0,Z=0)`, or the complete
tuple returned by property/export-text inspection. An empty initial value uses
the identity value for that control type.

### Reflected control settings

- `list_control_rig_control_properties(asset_path, control_name)`
- `get_control_rig_control_property(asset_path, control_name, property_path)`
- `set_control_rig_control_property(asset_path, control_name, property_path, value)`

List first and copy the exact `path` and current `value` syntax. Dotted and
indexed paths are supported for nested editable fields. The writer imports into
temporary storage first, so malformed export text does not partially corrupt
the setting. Use dedicated shape/hierarchy calls when they exist.

## RigVM graph authoring

### Read identities before edits

| Function | Result |
|---|---|
| `list_control_rig_graphs(asset_path)` | Graph name, full node path, node/link counts, and function-library flag. |
| `list_control_rig_nodes(asset_path, graph_name)` | Stable current node names/paths, titles, classes, positions, and every pin's exact path/direction/type/default/link state. |
| `list_control_rig_links(asset_path, graph_name)` | Exact source and target pin paths. |

An empty `graph_name` selects the first model, but deliverable scripts should
list graphs and pass an exact name or node path. After every node add/remove,
re-list nodes before connecting pins; the controller may uniquify a requested
node name.

### Write operations

| Function | Notes |
|---|---|
| `add_control_rig_member_variable(...)` / `remove_control_rig_member_variable(...)` | Uses RigVM C++ type text and export-text defaults. The add call returns the actual variable name. |
| `add_control_rig_unit_node(...)` | Adds a discovered unit struct path and method (`Execute` when empty). |
| `add_control_rig_template_node(...)` | Adds a discovered RigVM template notation. |
| `add_control_rig_variable_node(...)` | Adds getter/setter node and can atomically create its member variable. Supply the exact C++ type and optional type-object path. |
| `add_control_rig_branch_node(...)` | Adds explicit execution branching. |
| `add_control_rig_comment_node(...)` | Adds a readable section/comment box. Use meaningful stage names, not generic comments. |
| `remove_control_rig_node(...)` | Removes by current node name. |
| `set_control_rig_node_position(...)` | Sets graph coordinates. Prefer auto-layout after structural editing. |
| `set_control_rig_pin_default_value(...)` | Imports the exact pin-default syntax. Set `resize_arrays=True` only when array resizing is intended. |
| `connect_control_rig_pins(...)` / `disconnect_control_rig_pins(...)` | Uses the exact pin paths returned by node/link inspection. Optional cast-node creation is explicit. |
| `auto_layout_control_rig_graph(...)` | Layers the graph by link dependencies and reports positioned-node/layer counts plus warnings. |

A graph that merely contains nodes is incomplete. Review execution-context
links, data links, unlinked required inputs, semantic ordering, comments, and
layout. Do not infer `Node.Pin` strings from display titles: use returned pin
paths.

### Compile, validate, and evaluate

| Function | Gate |
|---|---|
| `compile_control_rig(asset_path, save)` | Runs the public Blueprint compiler and returns every tokenized compiler message. |
| `validate_control_rig(asset_path, save)` | Compiles, verifies hierarchy presence, and flags empty bone/control/RigVM content. `success` requires zero errors and a successful save when requested. |
| `evaluate_control_rig(asset_path, event_name, input_controls)` | Creates a transient rig instance, applies named global transforms, executes the event, and returns all resulting control/bone global transforms without modifying the asset. Empty event selects `Forwards Solve`. |

`input_controls` is an array of `unreal.BridgeRigNamedTransform` values with
`name`, concrete hierarchy `type` (normally `Control`), and `transform`.
Evaluation proves the VM runs, but does not replace viewport playback,
sequencer integration, or animator review.

## IK Rig authoring

### Asset, solvers, goals, and chains

| Function | Notes |
|---|---|
| `create_ik_rig(asset_path, skeletal_mesh_path)` | Creates an IK Rig and optionally assigns its preview Skeletal Mesh. |
| `get_ik_rig_info(asset_path)` | Mesh/skeleton/root plus bone, solver, goal, and chain counts. |
| `list_ik_rig_solvers(...)`, `list_ik_rig_goals(...)`, `list_ik_rig_retarget_chains(...)` | Returns current indices/identities, configuration, connections, and chain validity. |
| `add_ik_rig_solver(asset_path, solver_type_path)` | Adds a type returned by `list_rig_types(kind='IKSolver', ...)`; returns its current stack index. |
| `remove_ik_rig_solver(...)`, `move_ik_rig_solver(...)`, `set_ik_rig_solver_enabled(...)` | Stack edits invalidate cached indices; re-list immediately. |
| `set_ik_rig_solver_bones(...)` | Sets supported start/end bones for the selected solver. Empty side leaves that side unchanged. |
| `add_ik_rig_goal(...)`, `remove_ik_rig_goal(...)` | Goal name must be unique and bone must exist. Add returns the actual name. |
| `connect_ik_rig_goal_to_solver(...)` / `disconnect_ik_rig_goal_from_solver(...)` | Connect only goals supported by that solver type. |
| `add_ik_rig_retarget_chain(...)`, `remove_ik_rig_retarget_chain(...)`, `rename_ik_rig_retarget_chain(...)` | Uses exact skeleton bone names and optional goal identity. Add/rename return the actual chain name. |
| `set_ik_rig_retarget_root(...)` | Sets the root used for retarget translation. |
| `set_ik_rig_bone_excluded(...)` | Includes/excludes a skeleton bone from solving. |
| `apply_ik_rig_auto_setup(asset_path, retarget_definition, full_body_ik)` | Uses Epic's skeleton characterization to create a humanoid retarget definition and/or FBIK setup. It can fail for custom skeletons; fall back to explicit authoring rather than mutating private data. |

### Generic solver/goal settings

- `list_ik_rig_properties(asset_path, target_kind, solver_index, target_name)`
- `get_ik_rig_property(...)`
- `set_ik_rig_property(...)`

`target_kind` is `Solver`/`SolverSettings`, `Goal`, `GoalSettings`, or
`BoneSettings`. For `Goal`, `target_name` is the goal name and the solver index
is ignored. Goal/bone settings require a valid solver index and exact goal/bone
name; setting missing bone settings creates their official settings entry.
Values use the same discover-first export-text contract as Control Rig.

`validate_ik_rig(asset_path, save)` checks mesh/skeleton presence, per-solver
warnings, every retarget chain, retarget root, and duplicate chain names. Review
warnings even when `success` is true.

## IK Retargeter authoring

### Create, configure, and inspect

| Function | Notes |
|---|---|
| `create_ik_retargeter(...)` | Creates an asset and assigns source/target IK Rigs and preview meshes. `add_default_ops=True` ensures Epic's default op stack exists without duplicating a stack already initialized by the engine factory. |
| `get_ik_retargeter_info(asset_path)` | Assigned assets, current poses/profile, operation/mapping/pose counts, unmapped count, and dirty state. |
| `configure_ik_retargeter_assets(...)` | Reassigns rigs and preview meshes through the controller. Use exact compatible mesh paths. |

### Operation stack and chain mappings

| Function | Notes |
|---|---|
| `list_ik_retarget_ops(asset_path)` | Current index/name/type/parent/enabled state and whether an op owns chain mapping. |
| `add_ik_retarget_op(asset_path, op_type_path, op_name)` | Adds a discovered `RetargetOp`; returns current index. |
| `add_default_ik_retarget_ops(asset_path)` | Idempotently ensures the engine's standard operation stack exists. A complete existing core stack is left unchanged. |
| `remove_ik_retarget_op(...)`, `move_ik_retarget_op(...)`, `set_ik_retarget_op_enabled(...)` | Re-list after stack mutation because indices/order changed. |
| `set_ik_retarget_op_parent(asset_path, child_op_name, parent_op_name)` | Sets an allowed op dependency by stable op name. Some op types require a compatible parent and correctly reject clearing it. |
| `auto_map_ik_retarget_chains(asset_path, mapping_type, force_remap, op_name)` | `mapping_type` is `Exact`, `Fuzzy`, or `Clear`. Empty op name applies to all chain-mapping ops. |
| `set_ik_retarget_chain_mapping(...)` | Explicitly maps one target chain to one source chain for one/all mapping ops. Empty source clears the mapping. |
| `list_ik_retarget_chain_mappings(...)` | Review every target/source pair and whether settings remain at defaults. |

Retarget-op settings use:

- `list_ik_retarget_op_properties(asset_path, op_index)`
- `get_ik_retarget_op_property(...)`
- `set_ik_retarget_op_property(...)`

Again, copy discovered paths and export-text values. The setter emits the
official op-property notification so processors/editor views refresh.

### Retarget poses and runtime profiles

All pose calls take `side='Source'` or `side='Target'`.

| Function | Notes |
|---|---|
| `list_ik_retarget_poses(...)` | Name, side, root offset, bone-rotation count, and current flag. |
| `create_ik_retarget_pose(...)`, `duplicate_ik_retarget_pose(...)`, `rename_ik_retarget_pose(...)`, `remove_ik_retarget_pose(...)` | Structural pose lifecycle; returned strings are actual names. |
| `set_current_ik_retarget_pose(...)` | Chooses the active pose for one side. |
| `set_ik_retarget_pose_bone_rotation(...)` | Writes a quaternion rotation offset for one exact bone. |
| `set_ik_retarget_pose_root_offset(...)` | Writes the active pose root translation. |
| `reset_ik_retarget_pose(...)` | Resets named bones, or the full pose when the list is empty. |
| `auto_align_ik_retarget_pose(...)` | Uses the engine alignment method for selected/all bones; discover/test method names on the target build. |
| `list_ik_retarget_profiles(...)`, `save_current_ik_retarget_profile(...)`, `remove_ik_retarget_profile(...)`, `set_current_ik_retarget_profile(...)` | Snapshots and selects runtime op settings plus optional source/target pose application. `force_all_ik_off` is useful for FK-only fallback profiles. |

Profiles are runtime configuration snapshots, not substitutes for authored pose
assets. Validate the selected profile because it can change processor behavior.

### Processor validation and batch retargeting

`validate_ik_retargeter(asset_path, initialize_processor, save)` checks all
assigned assets, non-empty op stack, op warnings/parents, and unmapped target
chains. With `initialize_processor=True`, it constructs a real
`FIKRetargetProcessor` from the selected profile and returns its complete
errors, warnings, and informational log. This flag is mandatory before batch
delivery.

Asset-side ops can report a transient "not initialized" warning before a
processor owns them. When `initialize_processor=True`, validation relies on the
processor log instead of those pre-initialization messages; configuration
warnings and errors from the real processor are still returned in full.

`batch_retarget_animations(...)` accepts source animation asset paths, source
and target meshes, destination `/Game/...` folder, search/replace and
prefix/suffix naming, referenced-asset inclusion, overwrite, and save policy.
It returns every created path and every invalid source path. The batch
operation may duplicate referenced BlendSpaces or dependent animation assets
when requested; include all returned paths in review and cleanup.

Never batch into a production folder during validation. Use a unique dedicated
folder because overwrite and referenced-asset duplication can affect more than
the explicit source list.

## Animation quality sampling

`analyze_animation_quality(animation_path, foot_bone_names, num_samples,
contact_height_tolerance, foot_slide_speed_tolerance,
joint_angular_delta_tolerance_degrees, max_reported_bones)` samples an
`UAnimSequence` and reports:

- maximum horizontal root speed in Unreal units/second;
- minimum foot height relative to the root;
- maximum planted-foot horizontal speed in Unreal units/second;
- maximum per-sample global joint angular delta in degrees;
- ranked per-bone metrics and `FootPenetration`, `FootSlide`, or
  `JointDiscontinuity` issues.

An empty foot-bone list auto-detects names containing `foot`, `ball`, or `toe`.
`num_samples <= 0` uses the sequence sample rate and duration, capped at 2000
samples. Contact is defined relative to each foot's sampled minimum height plus
`contact_height_tolerance`; tune thresholds for the project's scale and frame
rate. A `success=True` result means analysis executed, not that warnings are
artistically acceptable.

This check catches common mechanical defects but cannot judge silhouettes,
weight, timing, contacts against moving props, cloth, facial performance, or
camera presentation. Always pair it with viewport/sequencer playback.

## Failure, review, and cleanup rules

- Empty arrays/strings, `False`, and `-1` are deliberate failure sentinels.
  Read `get_last_rig_error()` before attempting another write.
- Re-list after structural edits. Solver/op indices and controller-uniquified
  names are not safe to cache across mutations.
- Use the returned validation `issues`; do not rely only on the aggregate
  boolean. Warnings often represent unmapped chains or incomplete solvers.
- Never fall back to raw editor-data arrays when a controller rejects an edit.
  The rejection protects hierarchy, mapping, profile, and compilation state.
- Validation assets must live under one unique `/Game/...` folder. Track every
  path returned by batch retargeting, close any open editors, delete the exact
  folder, rescan/verify Asset Registry emptiness, and confirm its Content
  directory no longer contains `.uasset` files.
