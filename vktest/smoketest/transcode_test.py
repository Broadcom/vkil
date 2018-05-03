# Transcode tests
# Test on vk codec's trancoding correctness
# *** NOTE: this file is a place holder for now ***
# different formats, video sizes, qualities, etc, are expected to be tested within this test
import filecmp
from vkclass import vk_ffmpeg
from datetime import datetime
from os import remove

time_str = str(datetime.now().strftime("%Y%m%d%H%M%S"))
log_f    = "../log/transcoding_test-" + time_str + ".log"
log_fo   = open(log_f, 'w')
in_f     = "/home/vp024039/Downloads/vids/small.mp4"
out_f1   = "output.264"
out_f2   = "output2.264"
dummy_f1 = out_f1
dummy_f2 = out_f2

def run_session(out_f, cmp_f):
    vk_f = vk_ffmpeg(in_lst = [in_f], out_lst = [out_f])
    ret = vk_f.run(log_fo)
    return filecmp.cmp(out_f, cmp_f)

def dummy_test1():
    return run_session(out_f1, dummy_f1)

def dummy_test2():
    return run_session(out_f2, dummy_f2)

def run_transcode_test():
    print("Running transcode tests...")
    print("")
    res1 = dummy_test1()
    print("dummy_test1 : " + ("passed" if res1 else "failed"))

    res2 = dummy_test2()
    print("dummy_test2 : " + ("passed" if res2 else "failed"))
    print("")

    log_fo.close()

    if res1 and res2:
        remove(log_f)
    else:
        print("*** One or more of the session tests failed, please refer to {} ***".format(log_f))
        print("")

    remove(out_f1)
    remove(out_f2)

    return res1+res2

if __name__ == "__main__":
    run_transcode_test()
