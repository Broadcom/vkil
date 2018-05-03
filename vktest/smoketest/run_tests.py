from session_test import run_session_test
from transcode_test import run_transcode_test

if __name__ == "__main__":
    res1 = run_transcode_test()
    res2 = run_session_test()

    if res1 and res2:
        print("All tests passed!")
    else:
        print("One or more of the tests failed")
