#!/usr/bin/env python3
from __future__ import annotations

import argparse
import gettext
import inspect
import json
import os
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import boxes
import boxes.edges
import boxes.generators


SAMPLES_DIR = ROOT / "boxes" / "static" / "samples"
SKIP_ACTIONS = {"help", "output", "format", "render"}


JOINT_FAMILY_SPECS = [
    {
        "id": "finger",
        "label": "Finger",
        "settings_cls": boxes.edges.FingerJointSettings,
        "prefix": "FingerJoint",
        "edges": [
            {"id": "edge", "label": "Edge", "char": "f", "side_mode": "feature"},
            {"id": "counterpart", "label": "Counterpart", "char": "F", "side_mode": "recess"},
            {"id": "holes", "label": "Holes", "char": "h", "side_mode": "recess"},
        ],
        "roles": [
            {"id": "edge", "label": "Edge", "line0_edge": "edge"},
            {"id": "counterpart", "label": "Counterpart", "line0_edge": "counterpart"},
            {"id": "holes", "label": "Holes", "line0_edge": "holes"},
            {"id": "pair_edge_counterpart", "label": "Pair: Edge / Counterpart", "line0_edge": "edge", "line1_edge": "counterpart"},
            {"id": "pair_counterpart_edge", "label": "Pair: Counterpart / Edge", "line0_edge": "counterpart", "line1_edge": "edge"},
            {"id": "pair_edge_holes", "label": "Pair: Edge / Holes", "line0_edge": "edge", "line1_edge": "holes"},
            {"id": "pair_holes_edge", "label": "Pair: Holes / Edge", "line0_edge": "holes", "line1_edge": "edge"},
        ],
    },
    {
        "id": "stackable",
        "label": "Stackable",
        "settings_cls": boxes.edges.StackableSettings,
        "prefix": "Stackable",
        "edges": [
            {"id": "bottom", "label": "Bottom", "char": "s", "side_mode": "feature"},
            {"id": "top", "label": "Top", "char": "S", "side_mode": "recess"},
            {"id": "feet", "label": "Feet", "char": "š", "side_mode": "feature"},
            {"id": "top_holes", "label": "Top holes", "char": "Š", "side_mode": "recess"},
        ],
        "roles": [
            {"id": "bottom", "label": "Bottom", "line0_edge": "bottom"},
            {"id": "top", "label": "Top", "line0_edge": "top"},
            {"id": "feet", "label": "Feet", "line0_edge": "feet"},
            {"id": "top_holes", "label": "Top holes", "line0_edge": "top_holes"},
            {"id": "pair_bottom_top", "label": "Pair: Bottom / Top", "line0_edge": "bottom", "line1_edge": "top"},
            {"id": "pair_feet_top_holes", "label": "Pair: Feet / Top holes", "line0_edge": "feet", "line1_edge": "top_holes"},
        ],
    },
    {
        "id": "hinge",
        "label": "Hinge",
        "settings_cls": boxes.edges.HingeSettings,
        "prefix": "Hinge",
        "edges": [
            {"id": "eye_start", "label": "Eye (start)", "char": "i", "side_mode": "feature"},
            {"id": "pin_start", "label": "Pin (start)", "char": "I", "side_mode": "feature"},
            {"id": "eye_end", "label": "Eye (end)", "char": "j", "side_mode": "feature"},
            {"id": "pin_end", "label": "Pin (end)", "char": "J", "side_mode": "feature"},
            {"id": "eye_both", "label": "Eye (both)", "char": "k", "side_mode": "feature"},
            {"id": "pin_both", "label": "Pin (both)", "char": "K", "side_mode": "feature"},
        ],
        "roles": [
            {"id": "eye_start", "label": "Eye (start)", "line0_edge": "eye_start"},
            {"id": "pin_start", "label": "Pin (start)", "line0_edge": "pin_start"},
            {"id": "eye_end", "label": "Eye (end)", "line0_edge": "eye_end"},
            {"id": "pin_end", "label": "Pin (end)", "line0_edge": "pin_end"},
            {"id": "eye_both", "label": "Eye (both)", "line0_edge": "eye_both"},
            {"id": "pin_both", "label": "Pin (both)", "line0_edge": "pin_both"},
            {"id": "pair_start", "label": "Pair: Eye / Pin (start)", "line0_edge": "eye_start", "line1_edge": "pin_start"},
            {"id": "pair_end", "label": "Pair: Eye / Pin (end)", "line0_edge": "eye_end", "line1_edge": "pin_end"},
            {"id": "pair_both", "label": "Pair: Eye / Pin (both)", "line0_edge": "eye_both", "line1_edge": "pin_both"},
        ],
    },
    {
        "id": "chest_hinge",
        "label": "Chest Hinge",
        "settings_cls": boxes.edges.ChestHingeSettings,
        "prefix": "ChestHinge",
        "edges": [
            {"id": "box_start", "label": "Box (start)", "char": "o", "side_mode": "feature"},
            {"id": "box_end", "label": "Box (end)", "char": "O", "side_mode": "feature"},
            {"id": "lid_start", "label": "Lid (start)", "char": "p", "side_mode": "feature"},
            {"id": "lid_end", "label": "Lid (end)", "char": "P", "side_mode": "feature"},
            {"id": "pin", "label": "Pin edge", "char": "q", "side_mode": "feature"},
            {"id": "front", "label": "Front edge", "char": "Q", "side_mode": "recess"},
        ],
        "roles": [
            {"id": "box_start", "label": "Box (start)", "line0_edge": "box_start"},
            {"id": "box_end", "label": "Box (end)", "line0_edge": "box_end"},
            {"id": "lid_start", "label": "Lid (start)", "line0_edge": "lid_start"},
            {"id": "lid_end", "label": "Lid (end)", "line0_edge": "lid_end"},
            {"id": "pin", "label": "Pin edge", "line0_edge": "pin"},
            {"id": "front", "label": "Front edge", "line0_edge": "front"},
            {"id": "pair_box_lid_start", "label": "Pair: Box / Lid (start)", "line0_edge": "box_start", "line1_edge": "lid_start"},
            {"id": "pair_box_lid_end", "label": "Pair: Box / Lid (end)", "line0_edge": "box_end", "line1_edge": "lid_end"},
            {"id": "pair_pin_front", "label": "Pair: Pin / Front", "line0_edge": "pin", "line1_edge": "front"},
        ],
    },
    {
        "id": "cabinet_hinge",
        "label": "Cabinet Hinge",
        "settings_cls": boxes.edges.CabinetHingeSettings,
        "prefix": "CabinetHinge",
        "edges": [
            {"id": "side", "label": "Side", "char": "u", "side_mode": "feature"},
            {"id": "top", "label": "Top", "char": "U", "side_mode": "feature"},
            {"id": "angled", "label": "Angled", "char": "v", "side_mode": "feature"},
            {"id": "angled_top", "label": "Angled top", "char": "V", "side_mode": "feature"},
        ],
        "roles": [
            {"id": "side", "label": "Side", "line0_edge": "side"},
            {"id": "top", "label": "Top", "line0_edge": "top"},
            {"id": "angled", "label": "Angled", "line0_edge": "angled"},
            {"id": "angled_top", "label": "Angled top", "line0_edge": "angled_top"},
            {"id": "pair_side_top", "label": "Pair: Side / Top", "line0_edge": "side", "line1_edge": "top"},
            {"id": "pair_angled_top", "label": "Pair: Angled / Angled top", "line0_edge": "angled", "line1_edge": "angled_top"},
        ],
    },
    {
        "id": "slide_on_lid",
        "label": "Slide-on Lid",
        "settings_cls": boxes.edges.SlideOnLidSettings,
        "prefix": "SlideOnLid",
        "edges": [
            {"id": "lid_back", "label": "Lid back", "char": "l", "side_mode": "feature"},
            {"id": "box_back", "label": "Box back", "char": "L", "side_mode": "recess"},
            {"id": "lid_left", "label": "Lid left", "char": "m", "side_mode": "feature"},
            {"id": "box_left", "label": "Box left", "char": "M", "side_mode": "recess"},
            {"id": "lid_right", "label": "Lid right", "char": "n", "side_mode": "feature"},
            {"id": "box_right", "label": "Box right", "char": "N", "side_mode": "recess"},
        ],
        "roles": [
            {"id": "lid_back", "label": "Lid back", "line0_edge": "lid_back"},
            {"id": "box_back", "label": "Box back", "line0_edge": "box_back"},
            {"id": "lid_left", "label": "Lid left", "line0_edge": "lid_left"},
            {"id": "box_left", "label": "Box left", "line0_edge": "box_left"},
            {"id": "lid_right", "label": "Lid right", "line0_edge": "lid_right"},
            {"id": "box_right", "label": "Box right", "line0_edge": "box_right"},
            {"id": "pair_back", "label": "Pair: Lid back / Box back", "line0_edge": "lid_back", "line1_edge": "box_back"},
            {"id": "pair_left", "label": "Pair: Lid left / Box left", "line0_edge": "lid_left", "line1_edge": "box_left"},
            {"id": "pair_right", "label": "Pair: Lid right / Box right", "line0_edge": "lid_right", "line1_edge": "box_right"},
        ],
    },
    {
        "id": "click",
        "label": "Click",
        "settings_cls": boxes.edges.ClickSettings,
        "prefix": "Click",
        "edges": [
            {"id": "connector", "label": "Connector", "char": "c", "side_mode": "feature"},
            {"id": "edge", "label": "Edge", "char": "C", "side_mode": "recess"},
        ],
        "roles": [
            {"id": "connector", "label": "Connector", "line0_edge": "connector"},
            {"id": "edge", "label": "Edge", "line0_edge": "edge"},
            {"id": "pair_connector_edge", "label": "Pair: Connector / Edge", "line0_edge": "connector", "line1_edge": "edge"},
            {"id": "pair_edge_connector", "label": "Pair: Edge / Connector", "line0_edge": "edge", "line1_edge": "connector"},
        ],
    },
    {
        "id": "dovetail",
        "label": "Dovetail",
        "settings_cls": boxes.edges.DoveTailSettings,
        "prefix": "DoveTail",
        "edges": [
            {"id": "joint", "label": "Joint", "char": "d", "side_mode": "feature"},
            {"id": "counterpart", "label": "Counterpart", "char": "D", "side_mode": "recess"},
        ],
        "roles": [
            {"id": "joint", "label": "Joint", "line0_edge": "joint"},
            {"id": "counterpart", "label": "Counterpart", "line0_edge": "counterpart"},
            {"id": "pair_joint_counterpart", "label": "Pair: Joint / Counterpart", "line0_edge": "joint", "line1_edge": "counterpart"},
            {"id": "pair_counterpart_joint", "label": "Pair: Counterpart / Joint", "line0_edge": "counterpart", "line1_edge": "joint"},
        ],
    },
    {
        "id": "grooved",
        "label": "Grooved",
        "settings_cls": boxes.edges.GroovedSettings,
        "prefix": "Grooved",
        "edges": [
            {"id": "groove", "label": "Grooved", "char": "z", "side_mode": "feature"},
            {"id": "counterpart", "label": "Counterpart", "char": "Z", "side_mode": "recess"},
        ],
        "roles": [
            {"id": "groove", "label": "Grooved", "line0_edge": "groove"},
            {"id": "counterpart", "label": "Counterpart", "line0_edge": "counterpart"},
            {"id": "pair_groove_counterpart", "label": "Pair: Grooved / Counterpart", "line0_edge": "groove", "line1_edge": "counterpart"},
            {"id": "pair_counterpart_groove", "label": "Pair: Counterpart / Grooved", "line0_edge": "counterpart", "line1_edge": "groove"},
        ],
    },
    {
        "id": "mounting",
        "label": "Mounting",
        "settings_cls": boxes.edges.MountingSettings,
        "prefix": "Mounting",
        "edges": [
            {"id": "mounting", "label": "Mounting edge", "char": "G", "side_mode": "feature"},
        ],
        "roles": [
            {"id": "mounting", "label": "Mounting edge", "line0_edge": "mounting"},
        ],
    },
    {
        "id": "flex",
        "label": "Flex",
        "settings_cls": boxes.edges.FlexSettings,
        "prefix": "Flex",
        "edges": [
            {
                "id": "flex",
                "label": "Flex cut",
                "char": "X",
                "side_mode": "feature",
                "extra_args": [
                    {
                        "dest": "height",
                        "label": "Height",
                        "kind": "float",
                        "default_string": "30.0",
                        "default_float": 30.0,
                        "group": "Flex",
                        "help": "height of the flex pattern in mm",
                    }
                ],
            },
        ],
        "roles": [
            {"id": "flex", "label": "Flex cut", "line0_edge": "flex"},
        ],
    },
    {
        "id": "rounded_triangle",
        "label": "Rounded Triangle",
        "settings_cls": boxes.edges.RoundedTriangleEdgeSettings,
        "prefix": "RoundedTriangleEdge",
        "edges": [
            {"id": "edge", "label": "Rounded edge", "char": "t", "side_mode": "feature"},
            {"id": "holes", "label": "Rounded holes", "char": "T", "side_mode": "recess"},
        ],
        "roles": [
            {"id": "edge", "label": "Rounded edge", "line0_edge": "edge"},
            {"id": "holes", "label": "Rounded holes", "line0_edge": "holes"},
            {"id": "pair_edge_holes", "label": "Pair: Rounded edge / Holes", "line0_edge": "edge", "line1_edge": "holes"},
            {"id": "pair_holes_edge", "label": "Pair: Holes / Rounded edge", "line0_edge": "holes", "line1_edge": "edge"},
        ],
    },
    {
        "id": "handle",
        "label": "Handle",
        "settings_cls": boxes.edges.HandleEdgeSettings,
        "prefix": "HandleEdge",
        "edges": [
            {"id": "edge", "label": "Handle edge", "char": "y", "side_mode": "feature"},
            {"id": "holes", "label": "Handle holes", "char": "Y", "side_mode": "recess"},
        ],
        "roles": [
            {"id": "edge", "label": "Handle edge", "line0_edge": "edge"},
            {"id": "holes", "label": "Handle holes", "line0_edge": "holes"},
            {"id": "pair_edge_holes", "label": "Pair: Handle edge / Holes", "line0_edge": "edge", "line1_edge": "holes"},
            {"id": "pair_holes_edge", "label": "Pair: Holes / Handle edge", "line0_edge": "holes", "line1_edge": "edge"},
        ],
    },
]


def get_translation():
    localedir = ROOT / "locale"
    if localedir.exists():
        try:
            return gettext.translation("boxes.py", localedir=str(localedir), fallback=True)
        except Exception:
            pass
    return gettext.translation("boxes.py", fallback=True)


def discover_generators() -> dict[str, type[boxes.Boxes]]:
    return {
        cls.__name__.lower(): cls
        for cls in boxes.generators.getAllBoxGenerators().values()
        if getattr(cls, "webinterface", True)
    }


def normalize_sample_path(path: str | None) -> str | None:
    if not path:
        return None
    path = path.strip()
    if not path:
        return None
    if path.startswith("static/samples/"):
        path = path[len("static/samples/") :]
    return path or None


def resolve_sample_image(box_cls: type[boxes.Boxes]) -> str | None:
    explicit = [
        f"{box_cls.__name__}.jpg",
        f"{box_cls.__name__}.png",
        f"{box_cls.__name__}.jpeg",
        f"{box_cls.__name__}.webp",
    ]
    for candidate in explicit:
        if (SAMPLES_DIR / candidate).exists():
            return candidate

    text_parts = []
    if box_cls.__doc__:
        text_parts.append(inspect.cleandoc(box_cls.__doc__))
    description = getattr(box_cls, "description", "")
    if description:
        text_parts.append(description)
    text = "\n".join(text_parts)
    for match in re.finditer(r"\(\s*static/samples/([^)]+)\)", text):
        candidate = normalize_sample_path(match.group(1))
        if candidate and (SAMPLES_DIR / candidate).exists():
            return candidate
    return None


def resolve_thumbnail_image(sample_image: str | None) -> str | None:
    if not sample_image:
        return None
    path = Path(sample_image)
    thumb = path.with_name(f"{path.stem}-thumb.jpg")
    if (SAMPLES_DIR / thumb).exists():
        return thumb.as_posix()
    return None


def classify_action(action: argparse.Action) -> str | None:
    if action.dest in SKIP_ACTIONS or not action.option_strings:
        return None
    if isinstance(action, argparse._HelpAction):
        return None
    if action.choices:
        return "choice"
    if isinstance(action.default, bool):
        return "bool"
    if action.type is int or isinstance(action.default, int):
        return "int"
    if action.type is float or isinstance(action.default, float):
        return "float"
    if isinstance(action.type, argparse.FileType):
        return None
    if action.type is None or action.type is str or isinstance(action.default, str) or action.default is None:
        return "string"
    return None


def action_title(action: argparse.Action) -> str:
    label = action.option_strings[-1] if action.option_strings else action.dest
    label = label.lstrip("-").replace("_", " ")
    return label[:1].upper() + label[1:]


def serialize_action(action: argparse.Action, group_title: str) -> dict[str, object] | None:
    kind = classify_action(action)
    if not kind:
        return None

    default = action.default
    if default is None:
        default_string = ""
    elif isinstance(default, bool):
        default_string = "1" if default else "0"
    else:
        default_string = str(default)

    item: dict[str, object] = {
        "dest": action.dest,
        "option": action.option_strings[-1],
        "label": action_title(action),
        "kind": kind,
        "group": group_title,
        "help": action.help or "",
        "default_string": default_string,
    }
    if kind == "choice":
        item["choices"] = [str(choice) for choice in action.choices]
    elif kind == "bool":
        item["default_bool"] = bool(default)
    elif kind == "int":
        item["default_int"] = int(default)
    elif kind == "float":
        item["default_float"] = float(default)
    return item


def settings_metadata(settings_cls: type[boxes.edges.Settings], prefix: str) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    parser = argparse.ArgumentParser(add_help=False)
    settings_cls.parserArguments(parser, prefix)
    groups: list[dict[str, object]] = []
    args: list[dict[str, object]] = []
    seen_dests: set[str] = set()

    for group in parser._action_groups:
        group_items = []
        for action in group._group_actions:
            item = serialize_action(action, getattr(group, "title", "Settings") or "Settings")
            if not item or item["dest"] in seen_dests:
                continue
            seen_dests.add(str(item["dest"]))
            group_items.append(item)
            args.append(item)
        if group_items:
            groups.append({"title": getattr(group, "title", "Settings") or "Settings", "args": [item["dest"] for item in group_items]})

    return args, groups


def discover_joint_families() -> dict[str, dict[str, object]]:
    families: dict[str, dict[str, object]] = {}
    for spec in JOINT_FAMILY_SPECS:
        args, groups = settings_metadata(spec["settings_cls"], spec["prefix"])
        family = dict(spec)
        family["args"] = args
        family["arg_groups"] = groups
        family["description"] = inspect.cleandoc(spec["settings_cls"].__doc__ or "").splitlines()[0]
        edge_map = {edge["id"]: edge for edge in family["edges"]}
        for role in family["roles"]:
            role["pair"] = "line1_edge" in role
            role["line0"] = edge_map[role["line0_edge"]]
            if "line1_edge" in role:
                role["line1"] = edge_map[role["line1_edge"]]
        families[family["id"]] = family
    return families


def joint_families_json() -> dict[str, object]:
    families = []
    for family in discover_joint_families().values():
        families.append(
            {
                "id": family["id"],
                "label": family["label"],
                "description": family["description"],
                "args": family["args"],
                "arg_groups": family["arg_groups"],
                "edges": [
                    {
                        "id": edge["id"],
                        "label": edge["label"],
                        "side_mode": edge["side_mode"],
                        "extra_args": edge.get("extra_args", []),
                    }
                    for edge in family["edges"]
                ],
                "roles": [
                    {
                        "id": role["id"],
                        "label": role["label"],
                        "pair": role["pair"],
                        "line0_edge": role["line0_edge"],
                        "line1_edge": role.get("line1_edge", ""),
                    }
                    for role in family["roles"]
                ],
            }
        )
    return {"families": families}


def generator_metadata(box_cls: type[boxes.Boxes]) -> dict[str, object]:
    box = box_cls()
    groups: list[dict[str, object]] = []
    args: list[dict[str, object]] = []
    seen_dests: set[str] = set()

    for group in box.argparser._action_groups:
        group_items = []
        for action in group._group_actions:
            item = serialize_action(action, group.title or "Settings")
            if not item or item["dest"] in seen_dests:
                continue
            seen_dests.add(str(item["dest"]))
            group_items.append(item)
            args.append(item)
        if group_items:
            groups.append({"title": group.title or "Settings", "args": [item["dest"] for item in group_items]})

    sample_image = resolve_sample_image(box_cls)
    return {
        "id": box_cls.__name__.lower(),
        "name": box_cls.__name__,
        "label": box_cls.__name__,
        "group": getattr(box_cls, "ui_group", "Misc"),
        "short_description": box.metadata.get("short_description", ""),
        "description": box.metadata.get("description", ""),
        "sample_image": sample_image,
        "sample_thumbnail": resolve_thumbnail_image(sample_image),
        "args": args,
        "arg_groups": groups,
    }


def catalog_json() -> dict[str, object]:
    generators = discover_generators()
    known_groups = {group.name: group for group in boxes.generators.ui_groups}
    categories = [
        {
            "id": group.name,
            "title": group.title,
            "description": group.description,
            "sample_image": normalize_sample_path(group.image),
            "sample_thumbnail": normalize_sample_path(group.thumbnail),
        }
        for group in boxes.generators.ui_groups
    ]

    missing_groups = sorted(
        {
            getattr(cls, "ui_group", "Misc")
            for cls in generators.values()
            if getattr(cls, "ui_group", "Misc") not in known_groups
        }
    )
    for group_name in missing_groups:
        categories.append(
            {
                "id": group_name,
                "title": group_name,
                "description": "",
                "sample_image": None,
                "sample_thumbnail": None,
            }
        )

    items = [generator_metadata(cls) for _, cls in sorted(generators.items(), key=lambda item: item[0])]
    return {"categories": categories, "generators": items}


def split_call_args(args: list[str]) -> tuple[list[str], dict[str, str]]:
    box_args: list[str] = []
    call_args: dict[str, str] = {}
    i = 0
    while i < len(args):
        token = args[i]
        if token.startswith("--call_"):
            if i + 1 >= len(args):
                raise ValueError(f"missing value for {token}")
            call_args[token[len("--call_"):]] = args[i + 1]
            i += 2
            continue
        box_args.append(token)
        i += 1
    return box_args, call_args


def classify_rgb(rgb: object) -> str | None:
    if not isinstance(rgb, (list, tuple)) or len(rgb) != 3:
        return None
    channels = tuple(int(round(float(c) * 255.0)) for c in rgb)
    if channels == (0, 0, 255):
        return "inner_cut"
    if channels == (0, 255, 0):
        return "etching"
    if channels == (0, 255, 255):
        return "etching_deep"
    if channels == (255, 0, 0):
        return "annotations"
    if channels in {(0, 0, 0), (255, 0, 255), (255, 255, 0), (255, 255, 255)}:
        return "outer_cut"
    return None


def joint_edge_segments(surface) -> tuple[list[dict[str, object]], list[float], list[float]]:
    segments: list[dict[str, object]] = []
    bbox_min = [float("inf"), float("inf")]
    bbox_max = [float("-inf"), float("-inf")]

    def expand(pt):
        bbox_min[0] = min(bbox_min[0], pt[0])
        bbox_min[1] = min(bbox_min[1], pt[1])
        bbox_max[0] = max(bbox_max[0], pt[0])
        bbox_max[1] = max(bbox_max[1], pt[1])

    for part in surface.parts:
        for path in part.pathes:
            layer = classify_rgb(path.params.get("rgb"))
            if layer is None:
                continue
            current = None
            for cmd in path.path:
                kind = cmd[0]
                if kind == "M":
                    current = [float(cmd[1]), float(cmd[2])]
                    expand(current)
                elif kind == "L" and current is not None:
                    dst = [float(cmd[1]), float(cmd[2])]
                    segments.append({"kind": "line", "layer": layer, "p1": current, "p2": dst})
                    expand(dst)
                    current = dst
                elif kind == "C" and current is not None:
                    dst = [float(cmd[1]), float(cmd[2])]
                    c1 = [float(cmd[3]), float(cmd[4])]
                    c2 = [float(cmd[5]), float(cmd[6])]
                    segments.append({"kind": "bezier", "layer": layer, "p1": current, "c1": c1, "c2": c2, "p2": dst})
                    expand(c1)
                    expand(c2)
                    expand(dst)
                    current = dst

    if not segments:
        bbox_min = [0.0, 0.0]
        bbox_max = [0.0, 0.0]
    return segments, bbox_min, bbox_max


def render_joint_edge_json(family_id: str, edge_id: str, length_text: str, args: list[str]) -> int:
    families = discover_joint_families()
    family = families.get(family_id)
    if family is None:
        sys.stderr.write(f"unsupported joint family: {family_id}\n")
        return 2

    edge = next((edge for edge in family["edges"] if edge["id"] == edge_id), None)
    if edge is None:
        sys.stderr.write(f"unsupported joint edge: {family_id}:{edge_id}\n")
        return 2

    try:
        length = float(length_text)
    except ValueError:
        sys.stderr.write(f"invalid joint edge length: {length_text}\n")
        return 2

    try:
        box_args, call_args = split_call_args(args)
    except ValueError as e:
        sys.stderr.write(str(e) + "\n")
        return 2

    box = boxes.Boxes()
    box.translations = get_translation()
    box.addSettingsArgs(family["settings_cls"], prefix=family["prefix"])
    parse_args = [
        "--format",
        "svg",
        "--output",
        "-",
        "--reference",
        "0",
        "--labels",
        "0",
        "--qr_code",
        "0",
    ]
    parse_args.extend(box_args)
    try:
        box.parseArgs(parse_args)
        box.metadata["reproducible"] = True
        box.open()
        edge_obj = box.edges[edge["char"]]
        if family_id == "flex":
            height = float(call_args.get("height", "30.0"))
            edge_obj(length, height)
        else:
            edge_obj(length)
        box.ctx.stroke()
        segments, bbox_min, bbox_max = joint_edge_segments(box.surface)
    except Exception as e:
        sys.stderr.write(f"joint edge render failed: {e}\n")
        return 2

    json.dump(
        {
            "family": family_id,
            "edge": edge_id,
            "segments": segments,
            "bbox_min": bbox_min,
            "bbox_max": bbox_max,
        },
        sys.stdout,
        ensure_ascii=True,
    )
    return 0


def render_box(name: str, args: list[str]) -> tuple[boxes.Boxes, bytes]:
    box_cls = discover_generators().get(name.strip().lower())
    if box_cls is None:
        raise KeyError(name)

    box = box_cls()
    box.translations = get_translation()
    box.parseArgs(args)
    box.metadata["reproducible"] = True
    box.open()
    box.render()
    data = box.close()
    return box, data.getvalue()


def render_generator(name: str, args: list[str]) -> int:
    try:
        box, data = render_box(name, args)
    except KeyError:
        sys.stderr.write(f"unsupported bundled generator: {name}\n")
        return 2

    if box.output == "-":
        os.write(sys.stdout.fileno(), data)
    else:
        with open(box.output, "wb") as f:
            f.write(data)
    return 0


def grouped_svg_json(name: str, args: list[str]) -> int:
    try:
        _, data = render_box(name, args)
    except KeyError:
        sys.stderr.write(f"unsupported bundled generator: {name}\n")
        return 2

    svg_text = data.decode("utf-8")
    root = ET.fromstring(svg_text)
    width = root.attrib.get("width", "100mm")
    height = root.attrib.get("height", "100mm")
    view_box = root.attrib.get("viewBox", "0 0 100 100")

    groups: list[dict[str, str]] = []
    loose_children: list[ET.Element] = []
    for child in list(root):
        tag = child.tag.split("}")[-1]
        if tag == "g":
            group_xml = ET.tostring(child, encoding="unicode")
            groups.append(
                {
                    "id": child.attrib.get("id", ""),
                    "svg": (
                        f'<?xml version="1.0" encoding="utf-8"?>\n'
                        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="{view_box}">'
                        f"{group_xml}</svg>"
                    ),
                }
            )
        elif tag in {"path", "text"}:
            loose_children.append(child)

    if loose_children:
        svg = [
            '<?xml version="1.0" encoding="utf-8"?>',
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="{view_box}">',
        ]
        for child in loose_children:
            svg.append(ET.tostring(child, encoding="unicode"))
        svg.append("</svg>")
        groups.append({"id": "__loose__", "svg": "".join(svg)})

    json.dump({"groups": groups}, sys.stdout, ensure_ascii=True)
    return 0


def main() -> int:
    if len(sys.argv) >= 2 and sys.argv[1] == "--catalog-json":
        json.dump(catalog_json(), sys.stdout, ensure_ascii=True)
        return 0
    if len(sys.argv) >= 2 and sys.argv[1] == "--joint-families-json":
        json.dump(joint_families_json(), sys.stdout, ensure_ascii=True)
        return 0
    if len(sys.argv) >= 3 and sys.argv[1] == "--grouped-svg-json":
        return grouped_svg_json(sys.argv[2], sys.argv[3:])
    if len(sys.argv) >= 5 and sys.argv[1] == "--joint-edge-json":
        return render_joint_edge_json(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5:])

    if len(sys.argv) < 2:
        sys.stderr.write(
            "usage: boxes_runner.py [--catalog-json] [--joint-families-json] [--grouped-svg-json <generator>] "
            "[--joint-edge-json <family> <edge> <length>] <generator> [args...]\n"
        )
        return 2

    return render_generator(sys.argv[1], sys.argv[2:])


if __name__ == "__main__":
    raise SystemExit(main())
