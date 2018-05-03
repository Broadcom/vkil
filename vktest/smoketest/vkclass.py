import sys, getopt
from subprocess import call
from os import remove

# vk_fmpeg test obj
class vk_ffmpeg:
    def usage(self):
        print("Initializes a vk_ffmpeg testing object:\n")
        print("(This object uses the vk components by default)")
        print("")
        print("Required options:")
        print("-i --input       input file")
        print("-o --output      output file")
        print("")
        print("Other options:")
        print("-d --dec         video decoder")
        print("-e --enc         video encoder")
        print("-h --hwaccel     hardware accelerator")
        print("-u --usage       usage menu")
        print("")

    def __init__(self, args):
        try:
            opts, args = getopt.getopt(args, "i:o:d:e:h:u", ["input=", "output=", "dec=", "enc=", "hwaccel=", "usage"])
        except getopt.GetoptError:
            self.usage()
            sys.exit(1)

        self.input   = ""
        self.output  = ""
        self.dec     = "h264_vk"
        self.enc     = "h264_vk"
        self.hwaccel = "vk"

        for opt, arg in opts:
            if opt in ("-u", "--usage"):
                self.usage()
                sys.exit()
            elif opt in ("-i", "--ipaddress"): self.input   = arg
            elif opt in ("-o", "--width"):     self.output  = arg
            elif opt in ("-d", "--height"):    self.dec     = arg
            elif opt in ("-e", "--ltrf"):      self.enc     = arg
            elif opt in ("-h", "--profile"):   self.hwaccel = arg

        if self.input == "":
            print("Input file not specified")
            self.usage()
            print("")
            sys.exit(1)
        if self.output == "":
            print("Output file not specified")
            print("")
            self.usage()
            sys.exit(1)

        self.cmd_args = ["ffmpeg",
                         "-hwaccel"  , self.hwaccel,
                         "-loglevel" , "error",
                         "-c:v"      , self.dec,
                         "-i"        , self.input]

        if self.output == "-" or self.output == "/dev/null":
            self.cmd_args.append("-f")
            self.cmd_args.append("null")
            self.cmd_args.append(self.output)
        else:
            self.cmd_args.append("-c:v")
            self.cmd_args.append(self.enc)
            self.cmd_args.append(self.output)

    def rm_output(self):
        if self.output != "-" and self.output != "/dev/null":
            remove(self.output)

    def cmp_output_with(self, file):
        return cmp(self.output, file)

    # user should be responsible of openning and closing the log_file_object
    def run(self, log_file_object = None):
        if log_file_object is None:
            return call(self.cmd_args)
        else:
            return call(self.cmd_args, stdout=log_file_object, stderr=log_file_object)


class vk_hwaccel:
    def __init__(self, name):
        self.name = name
    def __str__(self):
        return self.name

class vk_decoder:
    def __init__(self, name):
        self.name = name
    def __str__(self):
        return self.name

class vk_encoder:
    def __init__(self, name):
        self.name = name
    def __str__(self):
        return self.name

class vk_scaler:
    def __init__(self, name):
        self.name = name
    def __str__(self):
        return self.name
