#!/usr/bin/python3

import version as v
import xml.etree.ElementTree as ET
tree = ET.parse('io.github.eriark.dxfsketcher.metainfo.xml')
root = tree.getroot()

rc = 0
releases = root.find("releases")
release = releases.find("release") if releases is not None else None
if release is not None:
	version_from_xml = release.attrib.get("version", "")
	if version_from_xml == v.string:
		print("Version okay")
	else:
		print("Version mismatch %s != %s" % (v.string, version_from_xml))
		rc = 1

	url_node = release.find("url")
	expected_url = f"https://github.com/EriArk/-DXF-Sketcher/releases/tag/v{v.string}"
	if url_node is None or url_node.text is None or url_node.text.strip() != expected_url:
		print("Release URL mismatch")
		rc = 1
else:
	print("No <releases> section in metainfo, skipping release checks")

#Check changelog versions

for filename in ("CHANGELOG.md", "scripts/CHANGELOG.md.in") :
	first_line = next(open(filename, "r")).strip()
	if first_line != f"# Version {v.string}" :
		print(f"{filename} version mismatch")
		rc = 1
	else :
		print(f"{filename} version okay")

exit(rc)
