import shutil
import subprocess
from pathlib import Path

from setuptools import setup
from setuptools.command.build_py import build_py
from setuptools.dist import Distribution

ROOT = Path(__file__).resolve().parent
ARTIFACTS = ["libath_fresh.a", "libath_intern.a", "libath_fresh.so", "libath_intern.so"]


# Build the C runtime and bundle the artifacts into athc/_runtime/ before packaging.
class BuildRuntime(build_py):
    def run(self):
        subprocess.run(["make", "runtime"], cwd=ROOT, check=True)
        dest = ROOT / "athc" / "_runtime"
        dest.mkdir(parents=True, exist_ok=True)
        for a in ARTIFACTS:
            shutil.copy2(ROOT / "runtime" / a, dest / a)
        # Bundle the .ath stdlib so angle-bracket importf resolves post-install.
        stdlib_dest = ROOT / "athc" / "_stdlib"
        stdlib_dest.mkdir(parents=True, exist_ok=True)
        for src in (ROOT / "stdlib").glob("*.ath"):
            shutil.copy2(src, stdlib_dest / src.name)
        super().run()


# Mark the wheel platform-specific since it carries compiled binaries.
class BinaryDist(Distribution):
    def has_ext_modules(self):
        return True


setup(cmdclass={"build_py": BuildRuntime}, distclass=BinaryDist)
