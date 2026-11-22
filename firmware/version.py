# The firmware version shown on the page: git describe plus the build time. A script, not a shell
# one-liner in platformio.ini, so that it also builds on Windows.
import subprocess, time
Import("projenv")
git = subprocess.run(["git", "describe", "--always", "--dirty"], capture_output=True, text=True).stdout.strip() or "unknown"
projenv.Append(CPPDEFINES=[("FW_VERSION", projenv.StringifyMacro(git + time.strftime("-%y%m%d.%H%M")))])
