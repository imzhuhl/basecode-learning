"""auto
"""
import os
import sys
import subprocess


def main():
    # print(sys.argv)
    arg_list = sys.argv[1:]

    if "clean" in arg_list:
        print("Delete build dir, 1(yes) or 2(no)")
        ans = int(input())
        if ans == 1:
            subprocess.run("rm -rf build", shell=True)
        else:
            return

    if "gen" in arg_list:
        # subprocess.run(["cmake", "-G", "Ninja", "-B", "build"])
        subprocess.run("cmake -G \"Ninja\" -B build", shell=True)
    
    if "build" in arg_list:
        subprocess.run("cmake --build build -v", shell=True)
    
    if "run" in arg_list:
        subprocess.run("./build/main", shell=True)


if __name__ == '__main__':
    main()

