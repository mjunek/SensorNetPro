#
#  Sensor Net Pro
#  Copyright © 2023-2026 Michael Junek
#
#  This file is part of SensorNetPro.
#
#  SensorNetPro is free software: you can redistribute it and/or modify it under the terms
#  of the GNU General Public License as published by the Free Software Foundation, either
#  version 3 of the License, or (at your option) any later version.
#
#  SensorNetPro is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
#  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#  See the GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along with SensorNetPro in a file called LICENSE.
#  If not, see <https://www.gnu.org/licenses/>.
#

# PlatformIO pre-script: build the filesystem image from a gzipped copy of data/, so the web UI
# fits in the SPIFFS partition next to two OTA app slots. ESPAsyncWebServer serves "x.gz" for "x"
# with Content-Encoding: gzip when only the compressed file exists. Images are already compressed
# and are copied as they are.

import gzip
import os
import shutil

Import("env")  # noqa: F821 (provided by PlatformIO)

STORE_AS_IS = (".png", ".jpg", ".gif", ".ico", ".gz")

source_dir = env.subst("$PROJECT_DATA_DIR")  # noqa: F821
target_dir = os.path.join(env.subst("$BUILD_DIR"), "data_gz")  # noqa: F821


def build_gzipped_data():
    shutil.rmtree(target_dir, ignore_errors=True)
    os.makedirs(target_dir)
    for name in sorted(os.listdir(source_dir)):
        source = os.path.join(source_dir, name)
        if not os.path.isfile(source):
            continue
        if name.lower().endswith(STORE_AS_IS):
            shutil.copyfile(source, os.path.join(target_dir, name))
            continue
        with open(source, "rb") as f:
            content = f.read()
        # mtime=0 keeps the image identical between builds of the same files
        with open(os.path.join(target_dir, name + ".gz"), "wb") as f:
            f.write(gzip.compress(content, compresslevel=9, mtime=0))


if set(["buildfs", "uploadfs", "uploadfsota"]) & set(COMMAND_LINE_TARGETS):  # noqa: F821
    build_gzipped_data()
    env.Replace(PROJECT_DATA_DIR=target_dir)  # noqa: F821
