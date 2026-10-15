"""Embed the git revision as RCLAB_BUILD so `info` can name the flashed source."""

import subprocess

Import("env")  # noqa: F821 - PlatformIO SCons global


def git(*args: str) -> str:
    return subprocess.run(["git", *args], check=True, capture_output=True, text=True).stdout.strip()


build = git("rev-parse", "--short=12", "HEAD")
if git("status", "--porcelain", "--", "."):
    build += "-dirty"
env.Append(CPPDEFINES=[("RCLAB_BUILD", env.StringifyMacro(build))])  # noqa: F821
