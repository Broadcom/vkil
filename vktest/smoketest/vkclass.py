import sys
from subprocess import call

# vk_fmpeg test obj
class vk_ffmpeg:

    # initializes a vk_ffmpeg testing object
    #     - construct a simple cmdline to be run
    #          - supports multiple inputs and outputs
    #          - default to vk codecs unless specified
    #     - alternatively, use the cmdline argument
    #
    # Note:
    # - make sure there is a corresponding dec/enc (if specified) for in/out
    def __init__(self, in_lst = [], out_lst = [], dec_lst = [], enc_lst = [], cmdline = ''):
        if not ((in_lst and out_lst) or cmdline):
            sys.exit(1)
        if cmdline:
            self.cmd_args = cmdline.split()
        else:
            if dec_lst and len(dec_lst) != len(in_lst)  : sys.exit(1)
            if enc_lst and len(enc_lst) != len(out_lst) : sys.exit(1)

            self.cmd_args = ["ffmpeg", "-hwaccel", "vk", "-loglevel", "error"]
            for i, j in enumerate(in_lst):
                self.cmd_args.append("-c:v")

                if dec_lst: self.cmd_args.append(dec_lst[i])
                else      : self.cmd_args.append("h264_vk")

                self.cmd_args.append("-i")
                self.cmd_args.append(j)


            for i, j in enumerate(out_lst):
                self.cmd_args.append("-c:v")

                if dec_lst: self.cmd_args.append(enc_lst[i])
                else      : self.cmd_args.append("h264_vk")

                if j == "-" or j == "/dev/null":
                    self.cmd_args.append("-f")
                    self.cmd_args.append("null")

                self.cmd_args.append(j)

    # if file_obj is passed in, stdout and stderr get redirect to the file
    # caller should be responsible for openning and closing the file_obj
    def run(self, file_obj = None):
        if file_obj is None:
            return call(self.cmd_args)
        else:
            return call(self.cmd_args, stdout=file_obj, stderr=file_obj)


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
