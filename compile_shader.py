#!/usr/bin/python
# -*- coding: UTF-8 -*-

import sys, getopt, os
from datetime import date
from datetime import datetime

# Compile all the shaders

VULKAN_SDK = os.getenv('VULKAN_SDK')
GLSLC = VULKAN_SDK + "/bin/glslc"
SHADER_DIR = "./data/shaders"

def main(argv):
    try:
        opts, args = getopt.getopt(argv, "x:", ["proj="])
    except getopt.GetoptError:
        help = "Usage: cmd [options]\n" +\
               "Options:\n" +\
               "  -x <language>\n" +\
               "  --proj=<project>"
        print(help)
        sys.exit(2)

    language = "hlsl"
    proj = "none"
    for opt, arg in opts:
        if opt == "-x":
            language = arg
        elif opt == "--proj":
            proj = arg

    print("GLSLC version:")
    os.system("{glslc} --version".format(glslc=GLSLC))

    print("\n")
    print("Compile {lang} shaders in project {dir}".format(lang=language, dir=proj))
    
    for path, dirs, files in os.walk(os.path.join(SHADER_DIR, language)):
        # print("files: {files}".format(files=files))
        # print("dirs: {dirs}".format(dirs=dirs))
        # print("path {path}".format(path=path))
        
        pathTail = os.path.split(path)[1]
        if proj != "all" and proj != pathTail:
            continue;

        for file in files:
            _, ext = os.path.splitext(file)
            if ext == ".frag" or ext == ".vert":
                # print("compiling {path}/{path}".format(glslc=GLSLC, file=file, path=path))
                command = "{glslc} -x {lang} {path}/{file} -o {path}/{file}.spv".format(glslc=GLSLC, lang=language, file=file, path=path)
                print(command)
                os.system(command)

    print("Compiling has done.")

if __name__ == "__main__":
    main(sys.argv[1:]) 