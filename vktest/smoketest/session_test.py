# Session tests
# Test on vkil's functionalties on managing concurrent sessions.
# Max sessions vkil can handle is 128.
# *** NOTE: vkil is complete, tests are to be modified!***

from vkclass import vk_ffmpeg
from multiprocessing import Process, Value
from datetime import datetime
from os import remove, devnull
from subprocess import call

time_str = str(datetime.now().strftime("%Y%m%d%H%M%S"))
log_fo   = open(devnull, 'w')
in_f     = "/home/vp024039/Downloads/vids/small.mp4" 

def run_session(c):
    # outputs to null as they dont matter in this test
    vk_f = vk_ffmpeg(args = ("-i {} -o {}".format(in_f, "/dev/null")).split())
    ret = vk_f.run(log_fo)
    vk_f.rm_output()
    with c.get_lock():
        c.value += ret

# run sessions sequentially
def synchronized_session_test():
    error_count = Value('i', 0)
    iterations = [1, 2, 4, 8, 16, 32, 64, 128, 256]
    failed = 0

    for itr in iterations:
        print("Running synchronized_session_test with {} sessions...".format(itr))
        for i in range(itr):
            p = Process(target=run_session, args=(error_count,))
            p.start()
            p.join()

        if error_count.value != 0:
            print("- failed")
            if failed == 0: failed = 1
        else:
            print("- passed")

    return failed

# run sessions (ideally) concurrently
def concurrent_session_test():
    error_count = Value('i', 0)
    iterations = [1, 2, 4, 8, 16, 32, 64, 128, 5000]
    failed = 0

    for itr in iterations:
        print("Running concurrent_session_test with {} sessions...".format(itr))
        jobs = []
        for i in range(itr):
            p = Process(target=run_session, args=(error_count,))
            p.start() # do not join, want the processes to clump
            jobs.append(p)

        for p in jobs:
            while (p.is_alive()):
                pass

        if itr == iterations[0] and error_count.value != 0:
            print("- failed")
            if failed == 0: failed = 1
        # expect more than 128 running concurrently at some point, thus some error would arise
        elif itr > 128 and error_count.value == 0:
            print("- failed")
            if failed == 0: failed = 1
        else:
            print("- passed")

    return failed

def run_session_test():
    print("Running sessoin tests...")
    print("")
    res1 = synchronized_session_test()
    print("synchronized_session_test : " + ("failed" if res1==-1 else "passed"))
    print("")

    res2 = concurrent_session_test()
    print("concurrent_session_test : " + ("failed" if res2==-1 else "passed"))
    print("")

    log_fo.close()

    # Decided not to log this test, as the resulting file gets too large
    if (res1+res2) != 0:
        print("*** One or more of the session tests failed ***")
        print("")

    # the concurrent session tests are likely to mess up the terminal io, resets the terminal settings
    call(["stty", "sane"])

    return res1+res2

if __name__ == "__main__":
    run_session_test()