""" Utility functions for chplenv modules """
import subprocess

# List of Chapel Environment Variables
chplvars = [
             'CHPL_HOME',
             'CHPL_HOST_PLATFORM',
             'CHPL_HOST_COMPILER',
             'CHPL_TARGET_PLATFORM',
             'CHPL_TARGET_COMPILER',
             'CHPL_TARGET_ARCH',
             'CHPL_LOCALE_MODEL',
             'CHPL_COMM',
             'CHPL_COMM_SUBSTRATE',
             'CHPL_GASNET_SEGMENT',
             'CHPL_TASKS',
             'CHPL_LAUNCHER',
             'CHPL_TIMERS',
             'CHPL_UNWIND',
             'CHPL_MEM',
             'CHPL_MAKE',
             'CHPL_ATOMICS',
             'CHPL_NETWORK_ATOMICS',
             'CHPL_GMP',
             'CHPL_HWLOC',
             'CHPL_REGEXP',
             'CHPL_WIDE_POINTERS',
             'CHPL_LLVM',
             'CHPL_AUX_FILESYS',
           ]


def memoize(func):
    cache = func.cache = {}

    def memoize_wrapper(*args, **kwargs):
        if kwargs:
            return func(*args, **kwargs)
        if args not in cache:
            cache[args] = func(*args)
        return cache[args]
    return memoize_wrapper


class CommandError(Exception):
    pass


# This could be replaced by subprocess.check_output, but that isn't available
# until python 2.7 and we only have 2.6 on most machines :(
def run_command(command, stdout=True, stderr=False):
    process = subprocess.Popen(command,
                               stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    output = process.communicate()
    if process.returncode != 0:
        raise CommandError(
            "command `{0}` failed - output was \n{1}".format(command,
                                                             output[1]))
    else:
        output = (output[0].decode(), output[1].decode())
        if stdout and stderr:
            return output
        elif stdout:
            return output[0]
        elif stderr:
            return output[1]
        else:
            return ''

