from textwrap import dedent
import argparse
import os
import tempfile


def main(lock_list,
         shared_var_list,
         dyn_var_list,
         split_output_file,
         output_tessla_spec_file='outfile.tessla'):
    # type: (list, list, list, bool, str) -> None

    if split_output_file:
        filename, ext = os.path.splitext(output_tessla_spec_file)
        first_output_file = filename + '_1st' + ext
        f = open(first_output_file, "w")
        first_half(f, lock_list, shared_var_list, dyn_var_list)
        f.close()

        # TODO: Remove '--' at the beginning of the Tessla streams only when we split the Tessla spec into two files
        second_output_file = filename + '_2nd' + ext
        f = open(second_output_file, "w")
        second_half(f, lock_list, shared_var_list, dyn_var_list)
        f.close()

        echo_file(first_output_file)
        echo_file(second_output_file)

    else:
        f = open(output_tessla_spec_file, "w")
        first_half(f, lock_list, shared_var_list, dyn_var_list)
        second_half(f, lock_list, shared_var_list, dyn_var_list)
        f.close()

        echo_file(output_tessla_spec_file)


def echo_file(filename):
    # type: (str) -> None

    f = open(filename, "r")
    content = f.read()
    print(content)
    f.close()


def first_intro(f):
    # type: (io.BinaryIO) -> None

    HEADER = '''
    -- This specification has been generated via the EU H2020 RIA project
    -- COEMS - Continuous Observation of Embedded Multicore Systems
    -- https://www.coems.eu
    --
    -- This work is licensed under a Creative Commons Attribution 4.0 International License,
    -- see https://creativecommons.org/licenses/by/4.0/.
    --
    -- The COEMS project is funded through the European Horizon 2020 program
    -- under grant agreement no. 732016.\n'''

    INIT = '''
    in threadid: Events[Int]
    in readaddr: Events[Int]
    in writeaddr: Events[Int]
    in mutexlockaddr: Events[Int]
    in mutexunlockaddr: Events[Int]
    -- Use a CF-event to track when we actually begin to be concurrent.
    -- The symbols is specific to our platform. It is only mildly brittle since
    -- hopefully the toolchain will shout at us if it's not found.
    @FunctionCall("__pthread_create_2_1")
    in pcreateid: Events[Unit]
    in line: Events[Int]
    in column: Events[Int]
    in dyn_base: Events[Int]
    in dyn_lock: Events[Int]
    out dyn_base -- avoid dead stream for now, see tessla/epu-compiler#34
    out dyn_lock
    -- -------------------------------------------------------------------------------
    def at[A,B](x: Events[A], y: Events[B]) := {
      def filterPredicate = time(x) == time(y)
      filter(first(y, filterPredicate), filterPredicate)
    }\n'''
    f.write(dedent(HEADER))
    f.write(dedent(INIT))


def lock_unlock(f, lock_list, dyn_flag):
    # type: (io.BinaryIO, list, bool) -> None

    LOCKS = '''
    -- -------------------------------------------------------------------------------
    -- -----LOCKS---------------------------------------------------------------------
    -- -------------------------------------------------------------------------------\n'''
    f.write(dedent(LOCKS))
    for i, l in enumerate(lock_list):
        f.write('def lock_{0} := filter(const((), mutexlockaddr == {1}), mutexlockaddr == {1})\n'.format(i, l))
        f.write('def unlock_{0} := filter(const((), mutexunlockaddr == {1}), mutexunlockaddr == {1})\n'.format(i, l))

        # If we're using dynamic tracing, currently we need to split the spec earlier
        #  and transfer those events to the second stage. So here, we always declare them as output
        #  streams although there might be some scenarious where a) you don't split and b) are not
        #  interested in having those events in the ouput. We can't really do the same for the
        #  INPUTS of the second stage, since we would like the unsplit version to be valid TeSSLa.

        if dyn_flag:
            f.write('out lock_{0}\n'.format(i))
            f.write('out unlock_{0}\n'.format(i))


    # 'slot' in {0..3}
    dynamic_lock_list = range(1) # TODO: parameter!

    num_locks = len(lock_list)
    num_dyn_locks = len(dynamic_lock_list)

    # TODO: size restriction on current EPUs.
    # Should we print this comment out into the generated file as well?
    # f.write('def dyn_temp := 2 * (((dyn_lock >> 1) ))\n')
    f.write('def dyn_temp := (((dyn_lock ) ))\n')
    f.write('def slot := dyn_lock - dyn_temp\n')

    # i in [num_locks, num_locks+1, ..., num_dyn_locks]
    for i, dl in enumerate(dynamic_lock_list, start=num_locks):
        # f.write('def dyn_lock_{0} := dyn_temp\n'.format(dl))
        f.write('def dyn_lock_{0} = filter(dyn_temp, slot == {0})\n'.format(dl))
        f.write('def lock_{0} := filter(mutexlockaddr == dyn_lock_{1}, mutexlockaddr == dyn_lock_{1})\n'.format(i, dl))
        f.write('def unlock_{0} := filter(mutexunlockaddr == dyn_lock_{1}, mutexunlockaddr == dyn_lock_{1})\n'.format(i, dl))
        f.write('out lock_{0}\n'.format(i))
        f.write('out unlock_{0}\n'.format(i))


def shared_variables(f, shared_variable_list, dynamic_variable_list):
    # type: (io.BinaryIO, list, list) -> None

    SHARED_VARIABLES = '''
    -- -------------------------------------------------------------------------------
    -- -----SHARED VARIABLES----------------------------------------------------------
    -- -------------------------------------------------------------------------------
    -- access_N should be a unit-stream, but then get discrepancies between hw- & sw
    --  in the 2nd stage, see #45. So just print 0.\n'''

    f.write(dedent(SHARED_VARIABLES))
    for i, sv in enumerate(shared_variable_list):
        f.write('def read_{0} := filter(readaddr == {1}, readaddr == {1})\n'.format(i, sv))
        f.write('def write_{0} := filter(writeaddr == {1}, writeaddr == {1})\n'.format(i, sv))
        f.write('def access_{0} := merge(read_{0}, write_{0})\n'.format(i))
        f.write('def access_after_pc_{0} := on(last(pcreateid, access_{0}), line)\n'.format(i))

    num_shared_vars = len(shared_variable_list)
    num_dyn_vars = len(dynamic_variable_list)

    # i in [num_shared_vars, num_shared_vars+1, ..., num_dyn_vars]
    for i, dv in enumerate(dynamic_variable_list, start=num_shared_vars):
        f.write('def tmp_{0} := dyn_base+{1}\n'.format(i, dv))
        f.write('def read_{0} := filter(readaddr == tmp_{0}, readaddr == tmp_{0})\n'.format(i))
        f.write('def write_{0} := filter(writeaddr == tmp_{0}, writeaddr == tmp_{0})\n'.format(i))
        f.write('def access_{0} := merge(read_{0}, write_{0})\n'.format(i))
        f.write('def access_after_pc_{0} := on(last(pcreateid, access_{0}), line)\n'.format(i))


def accessing(f, num_mem_locations):
    # type: (io.BinaryIO, int) -> None

    ACCESS = '''
    -- -------------------------------------------------------------------------------
    -- --- TODO: Check EPU numbers for TeSSLa 1.0
    -- -----------4 EPUs up to here---------------------------------------------------
    -- -------------------------------------------------------------------------------
    -- -----THREAD ACCESSING ---------------------------------------------------------
    -- -------------------------------------------------------------------------------\n'''
    f.write(dedent(ACCESS))
    for k in range(num_mem_locations):
        f.write('def thread_accessing_{0} := last(threadid*32768 + line, access_after_pc_{0})\n'.format(k))
        f.write('out thread_accessing_{0}\n'.format(k))


def holding(f, num_locks):
    # type: (io.BinaryIO, int) -> None

    HOLD = '''
    -- -------------------------------------------------------------------------------
    -- -----------5 EPUs up to here---------------------------------------------------
    -- -------------------------------------------------------------------------------
    -- -----THREAD HOLDING LOCK-------------------------------------------------------
    -- -------------------------------------------------------------------------------\n'''
    f.write(dedent(HOLD))
    for l in range(num_locks):
        f.write('def holding_{0} := default(merge(last(threadid, lock_{0}), last(-1, unlock_{0})), -1)\n'.format(l))
        f.write('out holding_{0}\n'.format(l))


def first_half(f, lock_list, shared_var_list, dyn_var_list):
    # type: (io.BinaryIO, list, list, list) -> None

    num_locks = len(lock_list)
    num_mem_locations = len(shared_var_list) + len(dyn_var_list)

    # The list of dynamic memory allocated variables is an optional parameter
    dyn_flag = len(dyn_var_list) > 0

    first_intro(f)
    f.write('\n')
    lock_unlock(f, lock_list, dyn_flag)
    f.write('\n')
    shared_variables(f, shared_var_list, dyn_var_list)
    f.write('\n')
    accessing(f, num_mem_locations)
    f.write('\n')
    # TODO: Should be num_locks + |dyn_locks|
    num_locks = num_locks + 1
    holding(f, num_locks)
    f.write('\n')


def second_intro(f, num_locks, num_mem_locations):
    # type: (io.BinaryIO, int, int) -> None
    INTRO = '''
    -- -----------6 EPUs up to here------6 with last----------------------------------
    -- -------------------------------------------------------------------------------\n'''
    f.write(dedent(INTRO))

    for i in range(num_mem_locations):
        f.write('--in thread_accessing_{0}: Events[Int]\n'.format(i))

    for i in range(num_locks):
        f.write('--in holding_{0}: Events[Int]\n'.format(i))


def checking(f, num_locks, num_mem_locations):
    # type: (io.BinaryIO, int, int) -> None
    INTRO = '''
    -- -------------------------------------------------------------------------------
    --def at[A,B](x: Events[A], y: Events[B]) := { def filterPredicate = time(x) == time(y); filter(first(y, filterPredicate), filterPredicate) }
    def detect_change(x:Events[Bool]) := merge(filter(x, !x), true)\n'''

    f.write(dedent(INTRO))
    for k in range(num_mem_locations):
        CHECKING = '''
        -- -------------------------------------------------------------------------------
        -- -----CHECKING {0}----------------------------------------------------------------
        -- -------------------------------------------------------------------------------\n'''.format(k)
        f.write(dedent(CHECKING))
        for j in range(num_locks):
            f.write('def protecting_{0}_with_{1} := detect_change(default(thread_accessing_{0}/32768 == last(holding_{1}, thread_accessing_{0}), true))\n'.format(k, j))
            f.write('--out protecting_{0}_with_{1}\n'.format(k, j))


def error_reporting(f, num_locks, num_mem_locations):
    # type: (io.BinaryIO, int, int) -> None

    def protecting_k_with_j(k, j):
        # type: (int, int) -> str
        return 'protecting_{0}_with_{1}'.format(k, j)

    def protecting_k(k, numLocks):
        # type: (int, int) -> str
        protect = (protecting_k_with_j(k, j) for j in range(numLocks))
        return ' || '.join(protect)

    ERROR_REPORT = '''
    -- -------------------------------------------------------------------------------
    -- -----------13 EPUs up to here--------------------------------------------------
    -- -------------------------------------------------------------------------------
    -- -----------ERROR REPORTING-----------------------------------------------------
    -- -the number of EPUs for the error reporting increases with the number of locks
    -- -----------MINIMUM 23 EPUs-----------------------------------------------------\n'''
    f.write(dedent(ERROR_REPORT))

    for k in range(num_mem_locations):
        ERROR = '''
        -- -------------------------------------------------------------------------------
        -- -----ERROR REPORT {0}----------------------------------------------------------------
        -- -------------------------------------------------------------------------------\n'''.format(k)
        f.write(dedent(ERROR))
        f.write('def error_{0} := on(thread_accessing_{0},!({1}))\n'.format(k, protecting_k(k, num_locks)))
        f.write('out error_{0}\n'.format(k))
        f.write('-- -------------------------------------------------------------------------------\n')

        f.write('def error_{0}_in_line := on(filter(error_{0}, error_{0}), thread_accessing_{0} & 32767)\n'.format(k))
        f.write('out error_{0}_in_line\n'.format(k))


def second_half(f, lock_list, shared_var_list, dyn_var_list):
    # type: (io.BinaryIO, list, list, list) -> None

    # TODO: dyn_locks
    num_locks = len(lock_list) + 1
    num_mem_locations = len(shared_var_list) + len(dyn_var_list)

    second_intro(f, num_locks, num_mem_locations)
    checking(f, num_locks, num_mem_locations)
    f.write('\n')
    error_reporting(f, num_locks, num_mem_locations)
    f.write('\n')


if __name__ == "__main__":
    # Example:
    # python tessla_generator_race_detection.py -l "1 2 3" -s "4 5 6" -d 4 outfile.tessla

    parser = argparse.ArgumentParser(description='Tessla generator for race conditions',
                                     epilog='python {0} -l "1 2 3" -s "4 5 6" -d 4 outfile.tessla'.format(__file__))
    parser.add_argument('-l', '--locks', type=str,
                        help='Locks in the format of memory addresses (i.e., -s "<mem_addr_1> ... <mem_addr_n>"')
    parser.add_argument('-s', '--shared', type=str,
                        help='Shared variables in the format of memory addresses (i.e., -s "<mem_addr_1> ... <mem_addr_n>"')
    parser.add_argument('-d', '--dynamic', type=int,
                        help='Number of bytes to observe starting from the memory base address that is emitted on runtime (e.g., -d 4 will check base, base+1,..., base+3 bytes)')
    parser.add_argument('-t', '--two', action='store_true',
                        help='When set "on", this flag splits the output Tessla specification into two separated fiels')
    parser.add_argument('outfile', type=str, nargs='?', default=None,
                        help='File with the generated Tessla specification')
    args = parser.parse_args()

    lock_list = []
    shared_var_list = []
    dyn_var_list = []

    if args.locks:
        locks = args.locks
        lock_list = [int(l) for l in locks.split()]

    if args.shared:
        shared = args.shared
        shared_var_list = [int(sv) for sv in shared.split()]

    if args.dynamic:
        dyn = args.dynamic

        # Memory addresses for the dynamically allocated memory are:
        # base+0, base+1, ... base+(dyn-1)
        dyn_var_list = list(range(dyn))

    if args.outfile is None:
        tf = tempfile.NamedTemporaryFile()
        args.outfile = tf.name

    # print(locks)
    # print(shared)
    # print(dyn)
    # print(lock_list)
    # print(shared_var_list)
    # print(dyn_var_list)
    # print(args.outfile)
    # print(args.two)
    main(output_tessla_spec_file=args.outfile, split_output_file=args.two, lock_list=lock_list,
         shared_var_list=shared_var_list, dyn_var_list=dyn_var_list)
