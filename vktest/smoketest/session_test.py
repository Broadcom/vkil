# Session tests
# Test on vkil's functionalties on managing concurrent sessions.
# Max sessions vkil can handle is 128.
# *** NOTE: vkil is complete, tests are to be modified!***

from vkclass import vk_ffmpeg
from multiprocessing import Process, Value
from os import remove, devnull
from subprocess import call

log_fo   = open(devnull, 'w')
in_f     = "/home/vp024039/Downloads/vids/small.mp4" 
sessions = [1, 2, 4, 8, 16, 32, 64, 128, 256]

def run_session(c):
    # outputs to null as they dont matter in this test
    # process many inputs/outputs to keep each session busier
    vk_f = vk_ffmpeg(in_lst  = [in_f, in_f, in_f, in_f, in_f, in_f, in_f, in_f, in_f, in_f], 
                     out_lst = ["/dev/null", "/dev/null", "/dev/null", "/dev/null", "/dev/null",
                                "/dev/null", "/dev/null", "/dev/null", "/dev/null", "/dev/null"])
    ret = vk_f.run(log_fo)
    if ret != 0:
        with c.get_lock():
            c.value += ret

# run sessions sequentially
def synchronized_session_test():
    error_count = Value('i', 0)
    p = True

    for s in sessions:
        print("Running synchronized_session_test with {} sessions...".format(s))
        for i in range(s):
            p = Process(target=run_session, args=(error_count,))
            p.start()
            p.join()

        if error_count.value != 0:
            print("- failed")
            if p: p = False
        else:
            print("- passed")

    return p

# run sessions (ideally) concurrently
def concurrent_session_test():
    error_count = Value('i', 0)
    p = True

    for s in sessions:
        print("Running concurrent_session_test with {} sessions...".format(s))
        jobs = []
        for i in range(s):
            p = Process(target=run_session, args=(error_count,))
            p.start() # do not join, want the processes to clump
            jobs.append(p)

        # print "All processes started"
        for p in jobs:
            while (p.is_alive()):
                pass

        if s <= 128 and error_count.value != 0:
            print("- failed")
            if p: p = False
        # expect more than 128 running concurrently at some point, thus some error would arise
        elif s > 128 and error_count.value == 0:
            print("- failed")
            if p: p = False
        else:
            print("- passed")

    return p

def run_session_test():
    print("Running sessoin tests...")
    print("")
    res1 = synchronized_session_test()
    print("synchronized_session_test : " + ("passed" if res1 else "failed"))
    print("")

    res2 = concurrent_session_test()
    print("concurrent_session_test : " + ("passed" if res2 else "failed"))
    print("")

    log_fo.close()

    # Decided not to log this test, as the resulting file gets too large
    if not (res1 and res2):
        print("*** One or more of the session tests failed ***")
        print("")

    # the concurrent session tests are likely to mess up the terminal io, resets the terminal settings
    call(["stty", "sane"])

    return res1 and res2

if __name__ == "__main__":
    run_session_test()
