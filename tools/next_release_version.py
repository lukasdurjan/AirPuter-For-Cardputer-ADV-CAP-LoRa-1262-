"""Choose the next patch release from existing vMAJOR.MINOR.PATCH Git tags."""

import re
import subprocess

MINIMUM_NEXT_VERSION = (1, 0, 17)


def next_version(tags):
    versions = []
    for tag in tags:
        match = re.fullmatch(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", tag)
        if match:
            versions.append(tuple(map(int, match.groups())))
    if not versions:
        candidate = (1, 0, 0)
    else:
        major, minor, patch = max(versions)
        candidate = (major, minor, patch + 1)
    major, minor, patch = max(candidate, MINIMUM_NEXT_VERSION)
    return f"v{major}.{minor}.{patch}"


if __name__ == "__main__":
    tags = subprocess.check_output(["git", "tag", "--list"], text=True).splitlines()
    print(next_version(tags))
