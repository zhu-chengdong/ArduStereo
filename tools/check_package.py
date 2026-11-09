#!/usr/bin/env python3
"""Static package checks only; does not import ROS or execute camera code."""
import ast
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

import yaml  # Development check only; not used by the node.

ROOT = Path(__file__).resolve().parents[1]


class UniqueLoader(yaml.SafeLoader):
    pass


def unique_mapping(loader, node, deep=False):
    result = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if key in result:
            raise ValueError("Duplicate YAML key: " + str(key))
        result[key] = loader.construct_object(value_node, deep=deep)
    return result


UniqueLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, unique_mapping)


def check():
    checks = []
    package = ET.parse(ROOT / "package.xml").getroot()
    assert package.findtext("name") == "ardustereo"
    assert package.findtext("license") == "MIT"
    cmake_version = re.search(r"project\(ardustereo VERSION ([0-9.]+)",
                              (ROOT / "CMakeLists.txt").read_text()).group(1)
    assert package.findtext("version") == cmake_version
    dependencies = {e.text for e in package.findall("depend")}
    assert {"roscpp", "sensor_msgs", "camera_info_manager", "diagnostic_msgs", "libopencv-dev"} <= dependencies
    checks.append("package metadata and declared runtime/build dependencies")

    for file in list((ROOT / "launch").glob("*.launch")) + list((ROOT / "test").glob("*.test")):
        tree = ET.parse(file).getroot()
        assert tree.tag == "launch"
        for element in tree.iter():
            for attribute in ("file", "default", "value"):
                path = element.get(attribute, "")
                if "$(find ardustereo)/" in path:
                    path = path.replace("file://", "").replace("$(find ardustereo)/", "")
                    assert (ROOT / path).is_file(), (file, path)
    checks.append("launch XML and concrete package file references")

    yaml_files = [p for p in ROOT.rglob("*.yaml") if "build" not in p.parts]
    for path in yaml_files:
        with path.open(encoding="utf-8") as stream:
            assert isinstance(yaml.load(stream, Loader=UniqueLoader), dict), path
    checks.append("YAML parses without duplicate keys")

    for name in ("b0266.yaml", "synthetic.yaml"):
        c = yaml.load((ROOT / "config" / name).read_text(), Loader=UniqueLoader)
        for key in ("width", "height", "split_column", "buffer_size", "warmup_frames", "queue_size",
                    "mono16_shift", "max_read_failures"):
            assert type(c[key]) is int, (name, key)
        assert 0 < c["split_column"] < c["width"] and c["height"] > 0
        assert type(c["publish_composite"]) is bool
        assert type(c["require_calibration"]) is bool
        assert not {"gain", "fps"} & set(c)  # Avoid aliases with ambiguous driver units.
        if name == "b0266.yaml":
            assert c["fourcc"] == "GREY"
            assert (c["frame_rate"], c["exposure"], c["analogue_gain"]) == (20, 681, 100)
        else:
            assert not {"frame_rate", "exposure", "analogue_gain"} & set(c)
        assert c["left_camera_info_url"] == c["right_camera_info_url"] == ""
        for key in ("diagnostics_period", "diagnostics_window", "frame_timeout",
                    "diagnostic_expected_fps", "diagnostic_min_fps_ratio"):
            assert type(c[key]) in (int, float), (name, key)
        assert 0.05 <= c["diagnostics_period"] <= 60
        assert 0.1 <= c["diagnostics_window"] <= 300
        assert 0.05 <= c["frame_timeout"] <= 300
        assert c["diagnostic_expected_fps"] == (20 if name == "b0266.yaml" else 0)
        assert 0 < c["diagnostic_min_fps_ratio"] <= 1
    assert yaml.safe_load((ROOT / "config/synthetic.yaml").read_text())["source"] == "synthetic"
    assert yaml.safe_load((ROOT / "config/b0266.yaml").read_text())["source"] == "v4l2"
    checks.append("shipped geometry, types and explicit B0266 startup-control policy")

    for directory in (ROOT / "test", ROOT / "tools", ROOT / "scripts"):
        for path in directory.glob("*.py"):
            ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    checks.append("Python test/tool syntax (ROS imports not executed)")

    cmake_text = (ROOT / "CMakeLists.txt").read_text()
    for path in re.findall(r"(?:src/[A-Za-z0-9_./-]+\.cpp|test/[A-Za-z0-9_./-]+\.(?:py|test)|cmake/[A-Za-z0-9_./-]+\.cmake\.in)", cmake_text):
        assert (ROOT / path).is_file(), path
    checks.append("CMake source and template paths")

    for path in [ROOT / "README.md"] + list((ROOT / "docs").glob("*.md")):
        for target in re.findall(r"\[[^\]]+\]\(([^)]+)\)", path.read_text(encoding="utf-8")):
            if not target.startswith(("http://", "https://", "#")):
                assert (path.parent / target.split("#")[0]).exists(), (path, target)
    checks.append("documentation local links")
    print(json.dumps({"kind": "STATIC_CHECKS_ONLY", "checks_passed": len(checks),
                      "checks": checks, "yaml_files": len(yaml_files)}, indent=2))


if __name__ == "__main__":
    check()
