#!/usr/bin/python
# -*- coding: UTF-8 -*-

import sys, getopt, os
from datetime import date
from datetime import datetime

# Compile all the shaders

VULKAN_SDK = os.getenv('VULKAN_SDK')
GLSLC = VULKAN_SDK + "/bin/glslc"
HLSL_DIR = "./data/shaders/hlsl"

def main(argv):
    print("GLSLC version:")
    os.system("{glslc} --version".format(glslc=GLSLC))

    dir = os.walk(HLSL_DIR)
    for path, dirs, files in os.walk(HLSL_DIR):
        # print("files: {files}".format(files=files))
        # print("dirs: {dirs}".format(dirs=dirs))
        # print("path {path}".format(path=path))
        for file in files:
            _, ext = os.path.splitext(file)
            if ext == ".frag" or ext == ".vert":
                # print("compiling {path}/{path}".format(glslc=GLSLC, file=file, path=path))
                command = "{glslc} -x hlsl {path}/{file} -o {path}/{file}.spv".format(glslc=GLSLC, file=file, path=path)
                print(command)
                os.system(command)

if __name__ == "__main__":
    main(sys.argv[1:]) 